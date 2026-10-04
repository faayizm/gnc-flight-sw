"""Fault injection: the simulator lies to the flight software, on purpose.

Every fault here is something real hardware does. Each one is injected where
it would really happen -- at the sensor, at the actuator, or on the data bus
between them and the flight computer -- and never by reaching into the flight
software. Whatever the flight software notices, it has to notice from the
data alone, exactly as it would in orbit.

    SENSOR FAULTS (target: mag, gyro, sun, star, gps)
      frozen     the sensor keeps reporting the value it had when the fault
                 began. Still flagged valid: a stuck sensor does not know it
                 is stuck.
      range      the sensor reports a physically impossible value (a broken
                 ADC reads full scale). Also still flagged valid.

    ACTUATOR FAULTS (target: wheel_x, wheel_y, wheel_z)
      dead       the wheel's drive electronics stop: no motor torque, ever
                 again. The wheel spins down on its own friction.
      latched    a single-event latch-up: a particle switches on a parasitic
                 path in the drive's electronics and the drive stops, exactly
                 like `dead` -- except that switching the power off and on
                 again clears it. Telling the two apart is the point of the
                 "retry" step in a recovery ladder.

    BUS FAULTS (target: bus)
      drop       the data bus goes quiet: no sensor frames reach the flight
                 computer and no commands come back. The actuators hold their
                 last command for a second, then their own timeout zeroes them.
      corrupt    a fraction `rate` of sensor frames have one bit flipped in
                 transit. The flight software's CRC check should reject every
                 one, and it must never answer a frame it rejected.

A fault is active for start <= t < end. Faults never draw from the
simulation's own random generator, so a run with a fault is identical to the
same run without it up to the moment the fault begins.
"""

from __future__ import annotations

import math
import random
from dataclasses import dataclass

SENSORS = ("mag", "gyro", "sun", "star", "gps")
WHEELS = {"wheel_x": 0, "wheel_y": 1, "wheel_z": 2}

# What a sensor reads when its converter is stuck at full scale.
OUT_OF_RANGE = {
    "mag": (2.0e-3, 2.0e-3, 2.0e-3),          # tesla; Earth's field in LEO is < 6e-5
    "gyro": (35.0, -35.0, 35.0),              # rad/s; that is 2000 deg/s
    "sun": (4.0, 0.0, 0.0),                   # not a unit vector
    "star": (0.0, 0.0, 0.0, 0.0),             # not a rotation
    "gps": ((1.0e9, 0.0, 0.0), (0.0, 0.0, 0.0)),
}


@dataclass
class Fault:
    kind: str
    target: str
    start: float
    end: float = math.inf
    rate: float = 0.1          # corrupt only: fraction of frames hit

    def active(self, t: float) -> bool:
        return self.start <= t < self.end

    def __post_init__(self):
        ok = {"frozen": SENSORS, "range": SENSORS, "dead": tuple(WHEELS),
              "latched": tuple(WHEELS), "drop": ("bus",), "corrupt": ("bus",)}
        if self.kind not in ok or self.target not in ok[self.kind]:
            raise ValueError(f"no such fault: {self.kind} on {self.target}")

    def __str__(self) -> str:
        end = "" if math.isinf(self.end) else f"-{self.end:.0f}"
        return f"{self.kind} {self.target} @ {self.start:.0f}{end} s"


class Injector:
    """Applies a list of faults to sensor readings, actuators and the bus."""

    def __init__(self, faults: list[Fault], seed: int):
        self.faults = list(faults)
        self.rng = random.Random(seed ^ 0x5EEDFA17)
        self.held: dict[str, object] = {}     # sensor -> value captured when frozen
        self.latched: dict[int, bool] = {}    # fault index -> latch still holding
        self.frames_corrupted = 0
        self.frames_dropped = 0

    def _active(self, kind: str, target: str, t: float) -> bool:
        return any(f.kind == kind and f.target == target and f.active(t) for f in self.faults)

    def sensor(self, name: str, t: float, value):
        """Pass a reading through. Returns what the flight software receives."""
        if self._active("range", name, t):
            return OUT_OF_RANGE[name]
        if self._active("frozen", name, t):
            return self.held.setdefault(name, value)
        self.held.pop(name, None)
        return value

    def wheel_faults(self, t: float, wheels, powered: bool) -> None:
        """Dead wheels stay dead. A latch-up holds until the rail is cycled."""
        for i, f in enumerate(self.faults):
            if f.target not in WHEELS or t < f.start:
                continue
            axis = WHEELS[f.target]
            if f.kind == "dead":
                wheels.failed[axis] = True
            elif f.kind == "latched":
                if i not in self.latched:
                    self.latched[i] = True          # the particle strikes, once
                    wheels.failed[axis] = True
                elif self.latched[i] and not powered:
                    self.latched[i] = False         # power removed: the latch collapses
                    wheels.failed[axis] = False

    def bus_dropped(self, t: float) -> bool:
        hit = self._active("drop", "bus", t)
        self.frames_dropped += hit
        return hit

    def corrupt(self, t: float) -> int | None:
        """Bit to flip in this sensor frame, or None. Counted from the start
        of the frame's body, so the length word is never hit: a corrupted
        length is a different fault (a misaligned stream)."""
        for f in self.faults:
            if f.kind == "corrupt" and f.active(t) and self.rng.random() < f.rate:
                self.frames_corrupted += 1
                return self.rng.randrange(8, 8 * 150)
        return None
