#!/usr/bin/env python3
"""Store and forward: commands loaded in one pass run out of contact, and
their results come home in the next.

    pass 1  correlate on-board time with the ground (ST[9])
            load three time-tagged commands for the dark part of the orbit (ST[11])
    gap     no contact for 90 minutes. The commands run; everything the
            spacecraft says is written into its packet store.
    pass 2  replay the gap from the store (ST[15]) on the playback channel,
            and check that every command ran when it was told to

All of it over the real link stack -- frames, Reed-Solomon, COP-1 -- through a
radio channel with pass windows from orbit geometry and a bit error rate that
worsens toward the horizon.

    python3 -m sim.scenarios.store_and_forward
    make store-forward
"""

from __future__ import annotations

import argparse
import pathlib
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "gnd"))

from sim.sil.harness import FSW, Checks, Flight     # noqa: E402
from sim.sil.radio import Geometry, GroundStation, Radio  # noqa: E402

from pyground.client import GroundClient            # noqa: E402
from pyground.packets import build_tc, schedule_data  # noqa: E402

STATION = GroundStation("mid-latitude station", 45.0, 30.0)
EPOCH = 844_300_800.0        # 2026-10-03T00:00:00, seconds since the mission epoch
SCHEDULE = [                 # (seconds after loss of signal, telecommand, sequence count)
    (600.0, ("SET_PARAM", dict(param_id=1, value=10000.0)), 900),   # slow HK in the dark
    (1800.0, ("TEST_CONNECTION", {}), 901),
    (4800.0, ("SET_PARAM", dict(param_id=1, value=1000.0)), 902),   # back to normal before the pass
]


class Pass2Collector:
    def __init__(self):
        self.playback = []
        self.live_events = []
        self.done = False


def wait_until(radio: Radio, gnd: GroundClient, t: float, label: str) -> list:
    """Keep the ground station serviced until mission time t."""
    print(f"  ... waiting for {label} (t = {t:.0f} s)")
    seen = []
    while radio.now() < t:
        seen += list(gnd.poll(timeout=0.05))
    return seen


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--time-scale", type=float, default=100.0)
    args = ap.parse_args()
    if not FSW.exists():
        print("build/fsw not found -- run `make build` first")
        return 2

    check = Checks()
    scale = args.time_scale
    geo = Geometry.predict(STATION, 500e3, 51.6, 3 * 3600)
    p1, p2 = geo.passes()[:2]
    print(f"store and forward via {STATION.name} ({STATION.lat_deg} N, {STATION.lon_deg} E)")
    print(f"  pass 1: {p1.aos:.0f}-{p1.los:.0f} s, max elevation {p1.max_el:.0f} deg")
    print(f"  pass 2: {p2.aos:.0f}-{p2.los:.0f} s, max elevation {p2.max_el:.0f} deg")
    t0 = time.time()

    with Flight(scale) as fl:
        radio = Radio(geo, fl.ttc, scale, seed=4)
        radio.begin()
        gnd = GroundClient(port=radio.port)
        gnd.connect()
        truth = lambda: EPOCH + radio.now()             # noqa: E731  the ground's clock
        try:
            # ---- pass 1 --------------------------------------------------
            wait_until(radio, gnd, p1.aos + 20, "acquisition of signal, pass 1")
            gnd.fop.initialise()                        # the FARM may have heard nothing yet
            check(gnd.wait_idle(10), "pass 1: the uplink is up (COP-1 initialised)")

            gnd.send("SET_TIME_REPORT_RATE", rate_exp=1)
            report = gnd.wait_for("TIME_REPORT", timeout=10)
            check(report is not None and (report.raw[6] & 0x0F) == 0,
                  "pass 1: on-board time arrives uncorrelated (time reference status 0)")
            delta = truth() - report.fields["time_s"]
            gnd.send("ADJUST_TIME", delta_s=delta)
            check(gnd.wait_for("VERIF_COMPLETE_OK", timeout=10) is not None,
                  f"pass 1: on-board time corrected by {delta / 86400 / 365.25:.1f} years")
            report = gnd.wait_for("TIME_REPORT", timeout=10)
            residual = truth() - report.fields["time_s"]
            check(report is not None and (report.raw[6] & 0x0F) == 1 and abs(residual) < 1.0,
                  f"pass 1: time now correlated, status 1, residual {residual * 1000:+.0f} ms "
                  f"(at {scale:.0f}x time scale; host latency is magnified as much)")
            gnd.send("SET_TIME_REPORT_RATE", rate_exp=255)

            los1 = EPOCH + p1.los
            activities = [(los1 + dt, build_tc(name, sequence_count=seq, **kw))
                          for dt, (name, kw), seq in SCHEDULE]
            gnd.send("INSERT_ACTIVITIES", data=schedule_data(activities))
            hk = None
            for tm in gnd.poll(timeout=5):
                if tm.name == "SYS_HK" and tm.fields["sched_pending"] == 3:
                    hk = tm
                    break
            check(hk is not None, "pass 1: three time-tagged commands are waiting on board")
            check(gnd.wait_idle(10), "pass 1: everything uplinked was acknowledged before loss of signal")

            # ---- the gap -------------------------------------------------
            seen = wait_until(radio, gnd, p1.los + 30, "loss of signal")
            cadus_at_los = gnd.link.cadus
            seen = wait_until(radio, gnd, p2.aos - 30, "the next pass, 90 minutes later")
            check(not seen and gnd.link.cadus == cadus_at_los,
                  "out of contact, nothing is heard from the spacecraft")

            # ---- pass 2 --------------------------------------------------
            wait_until(radio, gnd, p2.aos + 30, "acquisition of signal, pass 2")
            gnd.fop.initialise()
            gnd.wait_idle(10)
            live = list(gnd.poll(timeout=2))
            hk = next((tm for tm in reversed(live) if tm.name == "SYS_HK"), None)
            check(hk is not None and hk.fields["sched_pending"] == 0,
                  "pass 2: the schedule is empty -- everything was released")

            gnd.send("RETRIEVE_BY_TIME", store_id=1, from_s=int(los1), to_s=int(EPOCH + p2.aos))
            playback, done = [], None
            deadline = time.monotonic() + 120
            while done is None and time.monotonic() < deadline:
                for tm in gnd.poll(timeout=0.2):
                    if tm.vcid == 1:
                        playback.append(tm)
                    elif tm.fields.get("event_name") == "PLAYBACK_DONE":
                        done = tm
            # Frames still in flight when the DONE event (live channel) arrived.
            playback += [tm for tm in gnd.poll(timeout=1.0) if tm.vcid == 1]
        finally:
            gnd.close()
            radio.close()

    print(f"  ({time.time() - t0:.0f} s wall clock)\n")

    check(done is not None and done.fields["aux"] == len(playback),
          f"pass 2: the store replayed {len(playback)} packets from the gap, every one received")
    check(all(tm.crc_ok for tm in playback), "every replayed packet passes its CRC")
    check(all((tm.raw[6] & 0x0F) == 1 for tm in playback),
          "every replayed packet carries a correlated timestamp")

    released = {tm.fields["aux"]: tm.time_s for tm in playback
                if tm.fields.get("event_name") == "SCHED_RELEASED"}
    for dt, (name, _), seq in SCHEDULE:
        want = los1 + dt
        got = released.get(seq)
        check(got is not None and abs(got - want) < 0.5,
              f"{name} (seq {seq}) ran at LOS+{dt:.0f} s"
              + (f", {(got - want) * 1000:+.0f} ms from its release time" if got else ", but it never ran"))
    done_ok = {tm.fields["req_seqcnt"] for tm in playback if tm.name == "VERIF_COMPLETE_OK"}
    check({900, 901, 902} <= done_ok, "and each reported successful completion")
    check(any(tm.name == "TEST_REPORT" for tm in playback),
          "the connection test's reply was recorded and came home")

    hk_times = [tm.time_s for tm in playback if tm.name == "SYS_HK"]
    slow = [b - a for a, b in zip(hk_times, hk_times[1:])
            if los1 + 700 < a < los1 + 4700]
    check(slow and all(9.0 < g < 11.0 for g in slow),
          f"housekeeping really slowed to 10 s while the first command was in force ({len(slow)} gaps checked)")

    seqs = [tm.sequence_count for tm in playback if tm.apid == 0x001]
    gaps = sum(1 for a, b in zip(seqs, seqs[1:]) if (b - a) & 0x3FFF != 1)
    check(len(seqs) > 100 and gaps == 0,
          f"TT&C sequence counts run unbroken through the gap ({len(seqs)} packets): nothing lost on board")

    s, r = gnd.link, radio.stats
    print(f"\n  channel: {r.down_bit_errors} downlink and {r.up_bit_errors} uplink bit errors injected")
    print(f"  ground:  {s.cadus} frames, {s.rs_corrected} RS symbols corrected, "
          f"{s.rs_failed} uncorrectable, {sum(s.frames_lost.values())} counter gaps "
          f"(mostly the 90-minute silence, counted modulo 256); "
          f"COP-1 retransmitted {gnd.fop.retransmissions}")
    check(r.down_bit_errors > 0 and s.rs_corrected > 0,
          "the channel really was noisy, and Reed-Solomon repaired it")
    check(s.rs_failed <= 0.01 * s.cadus, "fewer than 1% of frames were beyond repair")
    return check.result()


if __name__ == "__main__":
    sys.exit(main())
