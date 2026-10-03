"""
Ground client: connects to the spacecraft, sends telecommands, receives and
decodes telemetry -- through the full CCSDS link stack in gnd/pyground/link.py.

    send("SET_PARAM", ...)  -> PUS packet -> FOP-1 -> TC frame -> CLTU -> socket
    socket -> ASM sync -> RS decode -> TM frame -> packets -> poll()

Telecommands are sequence-controlled (COP-1 type AD) by default: FOP-1 keeps
them until the spacecraft's CLCW acknowledges them, and retransmits what it
does not. The caller never sees that happening except in the statistics.
"""

from __future__ import annotations

import socket
import struct
import time
from collections.abc import Iterator

from .link import Fop1, TmDecoder, cltu, tc_frame
from .packets import Telemetry, build_tc, parse_tm


class GroundClient:
    """
    A ground station session. Use as a context manager:

        with GroundClient() as gnd:
            gnd.send("TEST_CONNECTION")
            for tm in gnd.poll(timeout=2.0):
                print(tm.summary())
    """

    def __init__(self, host: str = "127.0.0.1", port: int = 50001,
                 timeout: float = 5.0):
        self.host = host
        self.port = port
        self.timeout = timeout
        self._sock: socket.socket | None = None
        self._seq = 0
        self.decoder = TmDecoder()
        self.fop = Fop1(self._transmit)
        self._ready: list[Telemetry] = []

        # Counters, so a test can assert on what actually happened rather than
        # on scraped console output.
        self.tc_sent = 0
        self.tm_received = 0
        self.crc_failures = 0

    # -- connection ---------------------------------------------------------

    def connect(self, retries: int = 20, delay: float = 0.1) -> None:
        """
        Connect, retrying briefly. The retry loop matters: a test that starts
        the flight software and immediately connects will otherwise race the
        spacecraft's bind() and fail intermittently, which is the worst kind of
        test failure to debug.
        """
        last_error: OSError | None = None
        for _ in range(retries):
            try:
                sock = socket.create_connection((self.host, self.port), timeout=self.timeout)
                sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                self._sock = sock
                # Bring the spacecraft's FARM-1 into step with our FOP-1
                # before the first sequence-controlled frame.
                self.fop.initialise()
                return
            except OSError as exc:
                last_error = exc
                time.sleep(delay)
        raise ConnectionError(
            f"could not reach the spacecraft at {self.host}:{self.port} "
            f"after {retries} attempts ({last_error}). Is ./build/fsw running?"
        )

    def close(self) -> None:
        if self._sock is not None:
            self._sock.close()
            self._sock = None

    def __enter__(self) -> GroundClient:
        self.connect()
        return self

    def __exit__(self, *_exc: object) -> None:
        self.close()

    # -- uplink -------------------------------------------------------------

    def _transmit(self, data: bytes) -> None:
        if self._sock is None:
            raise ConnectionError("not connected")
        self._sock.sendall(data)

    def send(self, command: str, **args: object) -> bytes:
        """Encode and uplink one telecommand by dictionary name, through FOP-1."""
        packet = build_tc(command, sequence_count=self._seq, **args)
        return self.send_packet(packet)

    def send_packet(self, packet: bytes) -> bytes:
        """Uplink an already-encoded telecommand packet, sequence-controlled."""
        self._seq = (self._seq + 1) & 0x3FFF
        self.fop.send(packet)
        self.tc_sent += 1
        return packet

    def send_raw(self, packet: bytes) -> None:
        """
        Uplink arbitrary packet bytes in a bypass (type BD) frame, skipping
        every check in build_tc() and COP-1's sequencing.

        This is how the spacecraft's input validation gets tested: deliberately
        corrupt packets, wrong lengths, unknown services. A ground library that
        can only produce valid packets cannot test a receiver's error handling.
        """
        self._transmit(cltu(tc_frame(packet, 0, bypass=True)))

    def send_bytes(self, data: bytes) -> None:
        """Put raw bytes on the uplink, below even the CLTU layer."""
        self._transmit(data)

    @property
    def link(self):
        """Downlink decoding statistics (RS corrections, lost frames...)."""
        return self.decoder.stats

    # -- downlink -----------------------------------------------------------

    def poll(self, timeout: float = 1.0) -> Iterator[Telemetry]:
        """
        Yield every packet that arrives within `timeout` seconds. Also keeps
        COP-1 running: acknowledgements, retransmissions, the queue.
        """
        if self._sock is None:
            raise ConnectionError("not connected")

        deadline = time.monotonic() + timeout
        while True:
            yield from self._take()
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return
            self._sock.settimeout(min(remaining, 0.05))
            try:
                chunk = self._sock.recv(65536)
            except socket.timeout:
                self._ingest(b"")
                continue
            if not chunk:
                return   # spacecraft closed the link
            self._ingest(chunk)

    def poll_nowait(self) -> Iterator[Telemetry]:
        """
        Yield whatever has already arrived, without ever waiting. For callers
        that are themselves in a tight loop -- a simulator in lockstep with the
        spacecraft cannot afford to block on the downlink.
        """
        if self._sock is None:
            raise ConnectionError("not connected")
        self._sock.setblocking(False)
        try:
            while True:
                try:
                    chunk = self._sock.recv(65536)
                except (BlockingIOError, InterruptedError):
                    break
                if not chunk:
                    break
                self._ingest(chunk)
        finally:
            self._sock.settimeout(None)
        self._ingest(b"")
        yield from self._take()

    def wait_for(self, name: str, timeout: float = 3.0) -> Telemetry | None:
        """
        Wait for the next packet with a given decoded name, discarding others.
        Returns None on timeout rather than raising, so a caller can express
        "did this happen?" without exception handling.
        """
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for tm in self.poll(timeout=min(0.25, deadline - time.monotonic())):
                if tm.name == name:
                    return tm
        return None

    def wait_idle(self, timeout: float = 5.0) -> bool:
        """Keep the link serviced until every telecommand is acknowledged."""
        deadline = time.monotonic() + timeout
        while self.fop.outstanding and time.monotonic() < deadline:
            for tm in self.poll(timeout=0.1):
                self._ready.append(tm)
        return self.fop.outstanding == 0

    def _ingest(self, chunk: bytes) -> None:
        for vcid, packet in self.decoder.push(chunk):
            tm = parse_tm(packet)
            tm.vcid = vcid
            tm.raw = packet
            self.tm_received += 1
            if not tm.crc_ok:
                self.crc_failures += 1
            self._ready.append(tm)
        if self.decoder.last_clcw is not None:
            self.fop.on_clcw(self.decoder.last_clcw)
            self.decoder.last_clcw = None
        self.fop.service()

    def _take(self) -> Iterator[Telemetry]:
        while self._ready:
            yield self._ready.pop(0)
