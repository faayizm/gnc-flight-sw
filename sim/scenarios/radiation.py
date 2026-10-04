#!/usr/bin/env python3
"""Radiation: an orbit of single-event upsets, and nobody notices.

The simulator flips bits in the flight computer's memory at random -- about a
thousand times more often than a real orbit would, and ten times more often
again over the South Atlantic Anomaly -- while the spacecraft points at the
Earth. The flight software's defences have to make all of it invisible:

  * the parameter table is under EDAC: every single flipped bit is corrected
    when read, and the scrubber repairs it in memory before a second flip
    can join it (core/edac.hpp)
  * the spacecraft's mode and the wheel-isolation mask are kept in
    triplicate and voted on every read (core/tmr.hpp)

And one upset the defences cannot fix, on purpose: two bits flipped in the
same parameter word, back to back. EDAC can tell, but not repair it. The
word falls back to its default, FDIR raises EDAC_UNCORRECTABLE, and reloads
the whole table from non-volatile storage -- which still holds the value the
ground set, not the default.

    python3 -m sim.scenarios.radiation
    make radiation
"""

from __future__ import annotations

import argparse
import pathlib
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from sim.sil.bridge import Bridge                       # noqa: E402
from sim.sil.faults import Fault, Radiation             # noqa: E402
from sim.sil.harness import FSW, Checks, Downlink, Flight  # noqa: E402
from sim.sil.simulation import Scenario, Simulation     # noqa: E402

ORBIT = 5677.0
DUMP_GAIN_ID, DUMP_GAIN_INDEX = 14, 13      # MOMENTUM_DUMP_GAIN: id 14, 14th in the table
GROUND_VALUE = 0.0006                       # what the ground sets; the default is 0.0005
DOUBLE_T = 3000.0
# Two flips in one 72-bit word, a tenth of a second apart, between scrubs.
DOUBLE = [Fault("upset", "params", DOUBLE_T, bit=DUMP_GAIN_INDEX * 72 + 51),
          Fault("upset", "params", DOUBLE_T + 0.05, bit=DUMP_GAIN_INDEX * 72 + 20)]

SCENARIO = Scenario(name="radiation", seed=5, duration_s=ORBIT, tumble_dps=0.3,
                    radiation=Radiation(rate=0.02, saa_factor=10.0), faults=DOUBLE)
SETTLE_S = 600.0            # after pointing engages, to acquire the target
REQUIREMENT_DEG = 0.2


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--time-scale", type=float, default=100.0)
    args = ap.parse_args()
    if not FSW.exists():
        print("build/fsw not found -- run `make build` first")
        return 2

    check = Checks()
    print("radiation: one orbit of upsets, ten times worse over the South Atlantic Anomaly")
    t0 = time.time()
    worst = 0.0
    pointing_since = None
    events: list[tuple[float, str, int]] = []

    with Flight(args.time_scale) as fl:
        bridge = Bridge(fl.sim)
        bridge.connect()
        down = Downlink(fl.ttc)

        # Before the flight starts: the ground tunes a parameter. Within a
        # second it is in non-volatile storage too.
        down.client.send("SET_PARAM", param_id=DUMP_GAIN_ID, value=GROUND_VALUE)
        ok = down.client.wait_for("VERIF_COMPLETE_OK", timeout=5.0) is not None
        time.sleep(1.5)

        sim = Simulation(SCENARIO, bridge)

        def on_event(t: float, name: str) -> None:
            if name == "SCHED_OVERRUN":
                return
            aux = down.event_aux.get(name, [0])[-1] or 0
            events.append((t, name, aux))
            print(f"  t={t:7.1f} s  event {name}" + (f"  aux={aux}" if aux else ""))

        try:
            for k in range(int(SCENARIO.duration_s / SCENARIO.dt)):
                sim.step()
                if k % 5 == 0:
                    down.drain(sim.t, on_event=on_event)
                if pointing_since is None and sim.flags & 2:
                    pointing_since = sim.t
                if pointing_since is not None and sim.t > pointing_since + SETTLE_S:
                    worst = max(worst, sim.nadir_error_deg())
                if k % 6000 == 0:
                    print(f"  t={sim.t:7.0f} s  upsets so far {sum(sim.faults.upsets.values()):4d}"
                          f"  nadir err {sim.nadir_error_deg():6.3f} deg", flush=True)
            for _ in range(30):                  # let the last housekeeping come down
                sim.step()
                down.drain(sim.t, on_event=on_event)
            down.client.send("REPORT_PARAM", param_id=DUMP_GAIN_ID)
            report = None
            for _ in range(200):
                sim.step()
                for tm in down.client.poll_nowait():
                    if tm.name == "PARAM_REPORT":
                        report = tm
                    elif tm.name == "FDIR_HK":
                        down.hk["FDIR_HK"] = tm.fields
                if report is not None:
                    break
        finally:
            down.close()
            bridge.close()

    print(f"  ({time.time() - t0:.1f} s wall clock, {sim.t:.0f} s simulated)\n")
    hk = down.hk.get("FDIR_HK", {})
    up = sim.faults.upsets
    total = sum(up.values())
    print(f"  upsets delivered: {total} ({sim.faults.upsets_in_saa} in the SAA) -- "
          f"parameter table {up['params']}, mode {up['mode']}, wheel isolation {up['wheel_isolation']}")
    print(f"  on board: {hk.get('edac_corrected')} corrected by EDAC, "
          f"{hk.get('edac_uncorrectable')} uncorrectable, {hk.get('tmr_repairs')} repaired by TMR voting\n")

    check(ok, "the ground set MOMENTUM_DUMP_GAIN before the flight")
    check(total > 100 and sim.faults.upsets_in_saa > total / 4,
          f"{total} upsets, {sim.faults.upsets_in_saa} of them in the South Atlantic Anomaly")
    uncorrectable = hk.get("edac_uncorrectable", -1)
    check(hk.get("edac_corrected", 0) + 2 * uncorrectable == up["params"],
          "every upset in the parameter table is accounted for: corrected, or one of a pair")
    check(hk.get("tmr_repairs") == up["mode"] + up["wheel_isolation"],
          f"every upset in a triplicated value was voted out ({hk.get('tmr_repairs')} repairs)")
    modes = [n for _, n, _ in events if n.startswith("MODE")]
    check(modes == ["MODE BOOT->STANDBY", "MODE STANDBY->POINTING"],
          "the mode only ever changed on purpose: no upset reached it")
    check(not any(n.startswith("WHEEL") for _, n, _ in events),
          "no wheel was ever believed to have failed")
    bad = [(t, a) for t, n, a in events if n == "EDAC_UNCORRECTABLE"]
    check(any(abs(t - DOUBLE_T) < 3 and a == DUMP_GAIN_ID for t, a in bad),
          "the deliberate double upset was detected as uncorrectable, naming MOMENTUM_DUMP_GAIN")
    check(all(any(n == "PARAMS_RELOADED" and a == 1 and abs(t - tb) < 1 for t, n, a in events) for tb, _ in bad),
          "and each time the table was reloaded from non-volatile storage")
    check(report is not None and abs(report.fields["value"] - GROUND_VALUE) < 1e-9,
          f"so MOMENTUM_DUMP_GAIN is still the ground's {GROUND_VALUE}, not the default 0.0005")
    check(pointing_since is not None and worst < REQUIREMENT_DEG,
          f"and pointing never noticed any of it (worst {worst:.3f} deg, from {SETTLE_S:.0f} s "
          f"after pointing engaged)")
    return check.result()


if __name__ == "__main__":
    sys.exit(main())
