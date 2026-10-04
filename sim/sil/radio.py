"""The radio channel between ground station and spacecraft.

A TCP proxy that stands where the RF link would be, and makes it behave like
one:

  * PASSES. Bytes get through only while the spacecraft is above the ground
    station's elevation mask. Pass windows come from orbit geometry -- the
    same orbit model the simulator flies -- not from a hand-written timetable.
  * PROPAGATION DELAY. Range over the speed of light, a few milliseconds,
    applied in mission time.
  * BIT ERRORS. A bit error rate that worsens toward the horizon, where the
    path is longest and the antenna gain lowest: 1e-6 overhead, about 1e-3 at
    the 5 degree mask. Without channel coding most frames near the horizon
    would be lost; with it, nearly none are.

Time runs `scale` times faster than the wall clock, matching the flight
software's --time-scale, from a common start. Every random draw comes from one
seeded generator, so the pattern of errors is reproducible.
"""

from __future__ import annotations

import math
import random
import select
import socket
import threading
import time
from dataclasses import dataclass, field

from ..models.environment import EARTH_RATE
from ..models.orbit import Orbit, R_EARTH

C = 299_792_458.0


@dataclass
class GroundStation:
    name: str
    lat_deg: float
    lon_deg: float
    mask_deg: float = 5.0

    def ecef(self):
        la, lo = math.radians(self.lat_deg), math.radians(self.lon_deg)
        return (R_EARTH * math.cos(la) * math.cos(lo), R_EARTH * math.cos(la) * math.sin(lo),
                R_EARTH * math.sin(la))


@dataclass
class Pass:
    aos: float
    los: float
    max_el: float


@dataclass
class Geometry:
    """Elevation and range against time, sampled once a second."""
    station: GroundStation
    elevation: list = field(default_factory=list)   # degrees, index = second
    range_m: list = field(default_factory=list)

    @staticmethod
    def predict(station: GroundStation, altitude_m: float, inclination_deg: float,
                duration_s: float) -> Geometry:
        g = Geometry(station)
        orbit = Orbit(altitude_m, inclination_deg)
        gs = station.ecef()
        up = tuple(c / R_EARTH for c in gs)
        for t in range(int(duration_s) + 1):
            th = EARTH_RATE * t
            c, s = math.cos(th), math.sin(th)
            r = orbit.r
            e = (c * r[0] + s * r[1], -s * r[0] + c * r[1], r[2])
            d = [e[i] - gs[i] for i in range(3)]
            rng = math.sqrt(sum(x * x for x in d))
            g.elevation.append(math.degrees(math.asin(sum(d[i] * up[i] for i in range(3)) / rng)))
            g.range_m.append(rng)
            orbit.step(1.0)
        return g

    def at(self, t: float) -> tuple[float, float]:
        i = max(0, min(len(self.elevation) - 1, int(t)))
        return self.elevation[i], self.range_m[i]

    def passes(self) -> list[Pass]:
        out, start, peak = [], None, 0.0
        for t, el in enumerate(self.elevation):
            if el > self.station.mask_deg:
                if start is None:
                    start, peak = t, el
                peak = max(peak, el)
            elif start is not None:
                out.append(Pass(float(start), float(t), peak))
                start = None
        return out


def bit_error_rate(elevation_deg: float) -> float:
    """1e-3 at the horizon falling to 1e-6 overhead: a fair caricature of a
    CubeSat S-band link budget, where free-space loss and atmospheric path
    length both peak at low elevation."""
    return 10.0 ** (-3.0 - 3.0 * math.sin(math.radians(max(0.0, elevation_deg))))


@dataclass
class ChannelStats:
    up_bytes: int = 0
    down_bytes: int = 0
    up_dropped: int = 0
    down_dropped: int = 0
    up_bit_errors: int = 0
    down_bit_errors: int = 0


class Radio:
    """Proxy: the ground connects to `port`; the radio connects to the
    spacecraft's TT&C port. Runs in a background thread."""

    def __init__(self, geometry: Geometry, fsw_port: int, scale: float, seed: int = 1,
                 ber_scale: float = 1.0):
        self.geo, self.fsw_port, self.scale = geometry, fsw_port, scale
        self.rng = random.Random(seed)
        self.ber_scale = ber_scale
        self.stats = ChannelStats()
        self.start = time.monotonic()
        self._stop = threading.Event()
        self._srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._srv.bind(("127.0.0.1", 0))
        self._srv.listen(1)
        self.port = self._srv.getsockname()[1]
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._delayed: list[tuple[float, socket.socket, bytes]] = []

    # mission time since the common start
    def now(self) -> float:
        return (time.monotonic() - self.start) * self.scale

    def in_pass(self, t: float | None = None) -> bool:
        el, _ = self.geo.at(self.now() if t is None else t)
        return el > self.geo.station.mask_deg

    def begin(self) -> None:
        self._thread.start()

    def close(self) -> None:
        self._stop.set()
        self._thread.join(timeout=2)
        self._srv.close()

    def _corrupt(self, data: bytes, ber: float) -> tuple[bytes, int]:
        """Flip bits at rate `ber`: geometric gaps between errors, so the cost
        is proportional to the number of errors, not the number of bits."""
        if ber <= 0:
            return data, 0
        out = bytearray(data)
        nbits = 8 * len(out)
        log1p = math.log(1.0 - ber)
        pos, flips = -1, 0
        while True:
            pos += 1 + int(math.log(1.0 - self.rng.random()) / log1p)
            if pos >= nbits:
                return bytes(out), flips
            out[pos // 8] ^= 0x80 >> (pos % 8)
            flips += 1

    def _run(self) -> None:
        ground, _ = self._srv.accept()
        # The flight software may still be starting: retry like Bridge.connect.
        for _ in range(100):
            try:
                fsw = socket.create_connection(("127.0.0.1", self.fsw_port))
                break
            except OSError:
                time.sleep(0.05)
        else:
            ground.close()
            return
        for s in (ground, fsw):
            s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        try:
            while not self._stop.is_set():
                ready, _, _ = select.select([ground, fsw], [], [], 0.005)
                t = self.now()
                el, rng_m = self.geo.at(t)
                visible = el > self.geo.station.mask_deg
                ber = bit_error_rate(el) * self.ber_scale
                for src in ready:
                    data = src.recv(65536)
                    if not data:
                        return
                    up = src is ground
                    if up:
                        self.stats.up_bytes += len(data)
                    else:
                        self.stats.down_bytes += len(data)
                    if not visible:
                        if up:
                            self.stats.up_dropped += len(data)
                        else:
                            self.stats.down_dropped += len(data)
                        continue
                    data, flips = self._corrupt(data, ber)
                    if up:
                        self.stats.up_bit_errors += flips
                    else:
                        self.stats.down_bit_errors += flips
                    # Light time, converted back from mission to wall-clock time.
                    due = time.monotonic() + (rng_m / C) / self.scale
                    self._delayed.append((due, fsw if up else ground, data))
                now = time.monotonic()
                keep = []
                for due, dst, data in self._delayed:
                    if due <= now:
                        dst.sendall(data)
                    else:
                        keep.append((due, dst, data))
                self._delayed = keep
        finally:
            ground.close()
            fsw.close()
