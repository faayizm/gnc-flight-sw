"""
TCP ground client: connects to the flight software, sends telecommands,
reassembles and decodes downlinked telemetry.

The framing question is the same one the flight software faces, and it gets the
same answer: raw CCSDS Space Packets over TCP, with packet boundaries taken
from the CCSDS length field. That is honest about what this is -- a
software-in-the-loop stand-in for a link. A real RF chain would add TM/TC
transfer frames with an attached sync marker, pseudo-randomisation and
Reed-Solomon, which is what lets a receiver regain framing after noise. See
docs/ARCHITECTURE.md for where that is planned.
"""

from __future__ import annotations

import socket
import struct
import time
from collections.abc import Iterator

from .packets import CCSDS_HEADER_BYTES, Telemetry, build_tc, parse_tm


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
        self._rx = bytearray()
        self._seq = 0

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

    def send(self, command: str, **args: object) -> bytes:
        """Encode and uplink one telecommand by dictionary name."""
        if self._sock is None:
            raise ConnectionError("not connected")
        packet = build_tc(command, sequence_count=self._seq, **args)
        self._seq = (self._seq + 1) & 0x3FFF
        self._sock.sendall(packet)
        self.tc_sent += 1
        return packet

    def send_raw(self, packet: bytes) -> None:
        """
        Uplink arbitrary bytes, bypassing every check in build_tc().

        This is how the spacecraft's input validation gets tested: deliberately
        corrupt packets, wrong lengths, unknown services. A ground library that
        can only produce valid packets cannot test a receiver's error handling.
        """
        if self._sock is None:
            raise ConnectionError("not connected")
        self._sock.sendall(packet)

    # -- downlink -----------------------------------------------------------

    def poll(self, timeout: float = 1.0) -> Iterator[Telemetry]:
        """
        Yield every complete packet that arrives within `timeout` seconds.
        Returns as soon as the socket goes quiet for the remaining budget.
        """
        if self._sock is None:
            raise ConnectionError("not connected")

        deadline = time.monotonic() + timeout
        while True:
            yield from self._drain()

            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return
            self._sock.settimeout(remaining)
            try:
                chunk = self._sock.recv(4096)
            except socket.timeout:
                return
            if not chunk:
                return   # spacecraft closed the link
            self._rx += chunk

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

    def _drain(self) -> Iterator[Telemetry]:
        """Extract whole Space Packets from the receive buffer."""
        while len(self._rx) >= CCSDS_HEADER_BYTES:
            (length_field,) = struct.unpack(">H", self._rx[4:6])
            total = CCSDS_HEADER_BYTES + length_field + 1   # the "minus one" again

            if total > 65536 or total < CCSDS_HEADER_BYTES:
                # Cannot be a packet. Without an attached sync marker there is
                # no principled way to resynchronise, so drop one octet and
                # retry -- the same limited mitigation the flight software uses.
                del self._rx[0]
                continue
            if len(self._rx) < total:
                return

            packet = bytes(self._rx[:total])
            del self._rx[:total]

            tm = parse_tm(packet)
            self.tm_received += 1
            if not tm.crc_ok:
                self.crc_failures += 1
            yield tm
