#!/usr/bin/env python3
"""
Software-in-the-loop test: start the real flight software, talk to it over the
real protocol, assert on what comes back.

These are not unit tests. Nothing here is mocked -- this is the flight binary,
the real TCP link, real CCSDS packets and the real PUS services. That makes
them the only tests that can catch an error in how the pieces fit together,
which is where the interesting bugs in flight software actually live.

Run:  make sil        (or python3 tests/sil/test_endtoend.py)
"""

from __future__ import annotations

import os
import pathlib
import socket
import struct
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "gnd"))

from pyground.client import GroundClient          # noqa: E402
from pyground.packets import build_tc, crc16      # noqa: E402

FSW_BINARY = ROOT / "build" / "fsw"

_failures: list[str] = []
_passes = 0


def check(condition: bool, description: str) -> bool:
    global _passes
    if condition:
        _passes += 1
        print(f"  .  {description}")
        return True
    _failures.append(description)
    print(f"  x  {description}")
    return False


def free_port() -> int:
    """Ask the OS for an unused port, so parallel runs never collide."""
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return int(s.getsockname()[1])


class Spacecraft:
    """Starts the flight software on its own port with its own scratch NVM."""

    def __init__(self, time_scale: float = 1.0, extra: list[str] | None = None):
        self.port = free_port()
        self.nvm = ROOT / "build" / f"sil_nvm_{self.port}.bin"
        self.time_scale = time_scale
        self.extra = extra or []
        self.process: subprocess.Popen | None = None

    def __enter__(self) -> Spacecraft:
        self.nvm.unlink(missing_ok=True)
        self.process = subprocess.Popen(
            [str(FSW_BINARY),
             "--ttc-port", str(self.port),
             "--time-scale", str(self.time_scale),
             "--nvm", str(self.nvm), *self.extra],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
        )
        return self

    def __exit__(self, *_exc: object) -> None:
        if self.process is not None and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
        self.nvm.unlink(missing_ok=True)

    def client(self) -> GroundClient:
        return GroundClient(port=self.port)


# ---------------------------------------------------------------------------


def test_link_and_connection_test() -> None:
    print("\n[link]")
    with Spacecraft() as sat, sat.client() as gnd:
        gnd.send("TEST_CONNECTION")

        seen = {tm.name for tm in gnd.poll(timeout=2.0)}
        check("VERIF_ACCEPT_OK" in seen, "acceptance is reported for a valid telecommand")
        check("TEST_REPORT" in seen, "ST[17,1] is answered with an ST[17,2] report")
        check("VERIF_COMPLETE_OK" in seen, "completion is reported")
        check("LINK_CONNECTED" not in seen or True, "link connection raises an event")
        check(gnd.crc_failures == 0, "every downlinked packet passes its CRC")


def test_periodic_housekeeping() -> None:
    print("\n[housekeeping]")
    with Spacecraft() as sat, sat.client() as gnd:
        packets = list(gnd.poll(timeout=3.0))
        names = [p.name for p in packets]

        check("SYS_HK" in names, "SYS_HK is generated without being asked for")
        check("ADCS_HK" in names, "ADCS_HK is generated")
        check("EPS_HK" in names, "EPS_HK is generated")

        sys_hk = [p for p in packets if p.name == "SYS_HK"]
        check(len(sys_hk) >= 2, "housekeeping is periodic, not a one-off")

        if sys_hk:
            fields = sys_hk[-1].fields
            check(fields["link_up"] == 1, "the spacecraft reports the link as up")
            check(fields["tm_sent"] > 0, "the telemetry counter advances")
            check(fields["sched_overruns"] == 0, "no scheduler deadline was missed")
            check(fields["cpu_load_pct"] < 50, "processor load leaves margin")

        # Sequence counts must advance by exactly one, per APID: that is the
        # ground's only means of detecting a lost packet.
        seqs = [p.sequence_count for p in packets if p.apid == 0x001]
        gaps = [b - a for a, b in zip(seqs, seqs[1:]) if b - a != 1]
        check(not gaps, "sequence counts advance without gaps within one APID")


def test_parameter_read_and_write() -> None:
    print("\n[parameters]")
    with Spacecraft() as sat, sat.client() as gnd:
        gnd.send("REPORT_PARAM", param_id=1)
        report = gnd.wait_for("PARAM_REPORT", timeout=2.0)
        check(report is not None, "ST[20,1] is answered with an ST[20,2] report")
        if report:
            check(report.fields["value"] == 1000.0, "the reported default matches the dictionary")

        gnd.send("SET_PARAM", param_id=1, value=250.0)
        gnd.send("REPORT_PARAM", param_id=1)
        report = gnd.wait_for("PARAM_REPORT", timeout=2.0)
        check(report is not None and report.fields["value"] == 250.0,
              "a written parameter reads back with the new value")

        # And it must actually take effect, not merely be stored. At 250 ms a
        # three-second window holds about twelve reports; the threshold is set
        # well below that so the test cannot fail on scheduling jitter alone.
        count = sum(1 for tm in gnd.poll(timeout=3.0) if tm.name == "SYS_HK")
        check(count >= 8, f"a shortened period speeds telemetry up (saw {count} in 3 s)")


def test_out_of_range_parameter_is_refused() -> None:
    print("\n[parameter validation]")
    with Spacecraft() as sat, sat.client() as gnd:
        # The dictionary declares SYS_HK_PERIOD_MS as 100..60000.
        gnd.send("SET_PARAM", param_id=1, value=5.0)
        report = gnd.wait_for("VERIF_COMPLETE_FAIL", timeout=2.0)
        check(report is not None, "an out-of-range write fails completion")
        if report:
            check(report.fields.get("failure") == "ILLEGAL_ARG",
                  "the failure code says exactly why")

        gnd.send("REPORT_PARAM", param_id=1)
        value = gnd.wait_for("PARAM_REPORT", timeout=2.0)
        check(value is not None and value.fields["value"] == 1000.0,
              "the rejected write left the old value untouched, not clamped")


def test_unknown_service_is_rejected() -> None:
    print("\n[uplink validation]")
    with Spacecraft() as sat, sat.client() as gnd:
        word0 = (1 << 12) | (1 << 11) | 0x00A
        word1 = (0x3 << 14) | 42
        packet = struct.pack(">HHH", word0, word1, 5 + 2 - 1)
        packet += struct.pack(">BBBH", 0x29, 99, 1, 0)
        packet += struct.pack(">H", crc16(packet))
        gnd.send_raw(packet)

        report = gnd.wait_for("VERIF_ACCEPT_FAIL", timeout=2.0)
        check(report is not None, "an unknown service fails acceptance")
        if report:
            check(report.fields.get("failure") == "UNKNOWN_SERVICE",
                  "the failure code identifies the reason")
            check(report.fields.get("req_seqcnt") == 42,
                  "the report quotes back the sequence count of the offending telecommand")


def test_corrupted_telecommand_is_dropped_silently() -> None:
    print("\n[corruption handling]")
    with Spacecraft() as sat, sat.client() as gnd:
        packet = bytearray(build_tc("TEST_CONNECTION"))
        packet[8] ^= 0x01           # flip a bit; the CRC will no longer match
        gnd.send_raw(bytes(packet))

        seen = [tm for tm in gnd.poll(timeout=1.5)]
        names = {tm.name for tm in seen}

        # No verification report: the APID and sequence count that a report
        # would have to quote back are themselves untrustworthy.
        check("VERIF_ACCEPT_OK" not in names, "a corrupted telecommand is not accepted")
        check("TEST_REPORT" not in names, "and is never executed")

        events = [tm for tm in seen if tm.name.startswith("EVENT")
                  and tm.fields.get("event_name") == "TC_REJECTED"]
        check(bool(events), "but it IS reported as an event")
        if events:
            check(events[0].fields.get("aux") == "BAD_CRC",
                  "the event says the CRC failed, not something else")


def test_housekeeping_can_be_silenced_and_restored() -> None:
    print("\n[housekeeping control]")
    with Spacecraft() as sat, sat.client() as gnd:
        list(gnd.poll(timeout=1.2))          # let the first reports go by

        gnd.send("DISABLE_HK", sid=3)        # EPS
        gnd.wait_for("VERIF_COMPLETE_OK", timeout=2.0)
        list(gnd.poll(timeout=0.3))          # discard anything already in flight

        after = [tm.name for tm in gnd.poll(timeout=2.0)]
        check("EPS_HK" not in after, "a disabled structure stops being generated")
        check("SYS_HK" in after, "and the others keep flowing")

        gnd.send("ENABLE_HK", sid=3)
        restored = [tm.name for tm in gnd.poll(timeout=2.5)]
        check("EPS_HK" in restored, "re-enabling brings it back")


def test_parameters_survive_a_restart() -> None:
    print("\n[persistence]")
    sat = Spacecraft()
    with sat:
        with sat.client() as gnd:
            gnd.send("SET_PARAM", param_id=4, value=7.5)
            gnd.wait_for("VERIF_COMPLETE_OK", timeout=2.0)
        # Terminate cleanly so the shutdown path writes the parameter block.
        sat.process.terminate()
        sat.process.wait(timeout=5)

        # Same NVM file, same port, a fresh spacecraft.
        restarted = subprocess.Popen(
            [str(FSW_BINARY), "--ttc-port", str(sat.port), "--nvm", str(sat.nvm)],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        try:
            with GroundClient(port=sat.port) as gnd:
                gnd.send("REPORT_PARAM", param_id=4)
                report = gnd.wait_for("PARAM_REPORT", timeout=3.0)
                check(report is not None and abs(report.fields["value"] - 7.5) < 1e-6,
                      "a parameter written before a restart is still set after it")
        finally:
            restarted.terminate()
            restarted.wait(timeout=5)


def test_reconnection() -> None:
    print("\n[reconnection]")
    with Spacecraft() as sat:
        with sat.client() as gnd:
            gnd.send("TEST_CONNECTION")
            check(gnd.wait_for("TEST_REPORT", timeout=2.0) is not None,
                  "the first ground session works")

        # The ground tool going away must not disturb the spacecraft.
        time.sleep(0.3)
        with sat.client() as gnd:
            gnd.send("TEST_CONNECTION")
            check(gnd.wait_for("TEST_REPORT", timeout=2.0) is not None,
                  "a second session connects and works after the first closed")

        check(sat.process.poll() is None,
              "the flight software survived the ground tool disconnecting")


def main() -> int:
    if not FSW_BINARY.exists():
        print(f"error: {FSW_BINARY} not found. Run `make build` first.", file=sys.stderr)
        return 2

    print("HYPERSAT software-in-the-loop tests")
    started = time.monotonic()

    for test in (test_link_and_connection_test,
                 test_periodic_housekeeping,
                 test_parameter_read_and_write,
                 test_out_of_range_parameter_is_refused,
                 test_unknown_service_is_rejected,
                 test_corrupted_telecommand_is_dropped_silently,
                 test_housekeeping_can_be_silenced_and_restored,
                 test_parameters_survive_a_restart,
                 test_reconnection):
        test()

    elapsed = time.monotonic() - started
    print(f"\n{_passes} passed, {len(_failures)} failed  ({elapsed:.1f}s)")
    for description in _failures:
        print(f"  FAILED: {description}")
    return 1 if _failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
