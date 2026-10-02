#!/usr/bin/env python3
"""Nadir pointing: from a tumble to holding the Earth's centre within 0.2 deg,
through eclipse, through a GPS outage, with momentum kept off the wheels.

The whole Phase 3 chain in one flight:

    detumble (B-dot) -> TRIAD -> MEKF converges, learns the gyro bias
    -> nadir acquisition on the wheels -> fine pointing, with magnetic
    momentum dumping -> eclipse (sun sensor blind) -> GPS outage (on-board
    orbit propagation) -> still pointing

Every assertion is against the simulator's truth.

    python3 -m sim.scenarios.nadir_pointing      (from the repository root)
    make pointing
"""

from __future__ import annotations

import argparse
import math
import pathlib
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from sim.sil.bridge import Bridge                       # noqa: E402
from sim.sil.harness import FSW, Checks, Downlink, Flight  # noqa: E402
from sim.sil.simulation import Scenario, Simulation     # noqa: E402

ORBIT = 5677.0
GPS_OUTAGE = (7000.0, 7900.0)
SCENARIO = Scenario(name="nadir_pointing", seed=7, duration_s=2.0 * ORBIT, tumble_dps=3.0,
                    gps_outages=[GPS_OUTAGE])
SETTLE_S = 600.0            # allowed between pointing engaging and the 0.2 deg requirement
REQUIREMENT_DEG = 0.2


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--time-scale", type=float, default=100.0)
    args = ap.parse_args()
    if not FSW.exists():
        print("build/fsw not found -- run `make build` first")
        return 2

    check = Checks()
    print("nadir pointing: 3 deg/s tumble -> detumble -> estimate -> point, two orbits")
    t0 = time.time()

    pointing_since = None
    worst_nadir = worst_att = 0.0
    worst_in_eclipse = worst_in_outage = 0.0
    settled = eclipse_settled = outage_settled = 0
    max_h = 0.0

    with Flight(args.time_scale) as fl:
        bridge = Bridge(fl.sim)
        bridge.connect()
        down = Downlink(fl.ttc)
        sim = Simulation(SCENARIO, bridge)
        log = lambda t, n: print(f"  t={t:7.0f} s  event {n}")  # noqa: E731
        try:
            for k in range(int(SCENARIO.duration_s / SCENARIO.dt)):
                sim.step()
                if k % 50 == 0:
                    down.drain(sim.t, on_event=lambda t, n: None if n == "SCHED_OVERRUN" else log(t, n))
                if pointing_since is None and sim.flags & 2:
                    pointing_since = sim.t
                if pointing_since is not None:
                    max_h = max(max_h, max(abs(h) for h in sim.wheels.h))
                    if sim.t - pointing_since > SETTLE_S:
                        e, a = sim.nadir_error_deg(), sim.attitude_error_deg()
                        settled += 1
                        worst_nadir, worst_att = max(worst_nadir, e), max(worst_att, a)
                        if sim.eclipsed:
                            eclipse_settled += 1
                            worst_in_eclipse = max(worst_in_eclipse, e)
                        if GPS_OUTAGE[0] <= sim.t < GPS_OUTAGE[1]:
                            outage_settled += 1
                            worst_in_outage = max(worst_in_outage, e)
                if k % 3000 == 0:
                    print(f"  t={sim.t:7.0f} s  rate {sim.rate_dps:6.3f} deg/s  nadir err "
                          f"{sim.nadir_error_deg():7.3f} deg  wheels {sim.wheel_momentum * 1e3:5.2f} mNms"
                          f"{'  [eclipse]' if sim.eclipsed else ''}", flush=True)
        finally:
            down.close()
            bridge.close()

    print(f"  ({time.time() - t0:.1f} s wall clock, {sim.t:.0f} s simulated)\n")
    hk = down.hk.get("ADCS_HK", {})

    check(down.saw("DETUMBLE_COMPLETE"), "detumble completes")
    check(down.event_aux.get("ESTIMATOR_INIT", [None])[0] == 1,
          "the estimator initialises from TRIAD (the tracker cannot see stars at 3 deg/s)")
    check(down.saw("ESTIMATOR_CONVERGED"), "the estimator reports convergence")
    check(not down.saw("ESTIMATOR_RESET"), "the estimator never loses track")
    check(pointing_since is not None and pointing_since < ORBIT,
          "nadir pointing engages within one orbit"
          + (f" (t = {pointing_since:.0f} s)" if pointing_since else ""))
    check(settled > 0 and worst_nadir < REQUIREMENT_DEG,
          f"nadir error stays below {REQUIREMENT_DEG} deg once settled (worst {worst_nadir:.3f} deg)")
    check(worst_att < 2 * REQUIREMENT_DEG, f"three-axis error stays small too (worst {worst_att:.3f} deg)")
    check(eclipse_settled > 5000 and worst_in_eclipse < REQUIREMENT_DEG,
          f"held through eclipse ({eclipse_settled / 10:.0f} s in shadow, worst {worst_in_eclipse:.3f} deg)")
    check(outage_settled > 5000 and worst_in_outage < REQUIREMENT_DEG,
          f"held through a {GPS_OUTAGE[1] - GPS_OUTAGE[0]:.0f} s GPS outage (worst {worst_in_outage:.3f} deg)")
    check(max_h < 0.5 * sim.wheels.max_h,
          f"momentum dumping keeps the wheels below half capacity (peak {max_h * 1e3:.2f} mNms)")

    if hk:
        est_bias = (hk["gyro_bias_x"], hk["gyro_bias_y"], hk["gyro_bias_z"])
        err = max(abs(a - b) for a, b in zip(est_bias, sim.gyro.bias))
        check(err < 2e-5, f"gyro bias learned to within {math.degrees(err) * 3600:.1f} deg/h "
              f"(true bias up to {math.degrees(max(abs(b) for b in sim.gyro.bias)) * 3600:.0f} deg/h)")
        check(hk["ctrl_mode"] == "POINTING" and hk["est_state"] == "CONVERGED",
              "ADCS_HK reports POINTING with a CONVERGED estimator")
    else:
        check(False, "ADCS_HK was downlinked")
    return check.result()


if __name__ == "__main__":
    sys.exit(main())
