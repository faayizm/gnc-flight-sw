#!/usr/bin/env python3
"""Monte Carlo: SAFE, entered from anywhere, turns the main array to the Sun.

Every run starts in SAFE: the wheels are off, and the only tools are the
magnetorquers, the coarse sun sensor, the magnetometer and the gyro
(fsw/apps/adcs/sun_safe.hpp). Dispersed in every run, all from one seed:

    true inertia            +-10% per axis
    initial attitude, rate  0.2 to 3 deg/s: the faster ones need B-dot first
    where in the orbit      anywhere, so some runs enter SAFE in eclipse,
                            with no Sun to point at for half an hour
    sensor errors           gyro bias, noise, every random draw

Requirements, per run:
    the main array is acquired -- within 20 deg of the Sun, for 300 s of
    sunlight in a row -- within 1.5 orbits
    once acquired, its median angle to the Sun while sunlit stays below 20 deg
    the spacecraft is still in SAFE at the end (nothing here may leave it)

A run that fails prints its seed. `--only SEED` flies exactly that run again.

    python3 -m sim.scenarios.safe_mode_mc --runs 8
    make safe-mc RUNS=8
"""

from __future__ import annotations

import argparse
import os
import pathlib
import random
import statistics
import sys
import time
from concurrent.futures import ProcessPoolExecutor

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from sim.sil.bridge import Bridge                       # noqa: E402
from sim.sil.harness import FSW, Downlink, Flight       # noqa: E402
from sim.sil.simulation import Scenario, Simulation     # noqa: E402

ORBIT = 5677.0
NOMINAL_INERTIA = (0.10, 0.12, 0.04)
ACQUIRED_DEG, HOLD_S = 20.0, 300.0
ACQUIRE_WITHIN = 1.5 * ORBIT
DURATION = 2.0 * ORBIT


def disperse(seed: int) -> dict:
    rng = random.Random(seed)
    inertia = tuple(round(i * rng.uniform(0.9, 1.1), 5) for i in NOMINAL_INERTIA)
    return dict(seed=seed, inertia=inertia, tumble=round(rng.uniform(0.2, 3.0), 2),
                anomaly=round(rng.uniform(0.0, 360.0), 1))


def fly(case: dict, time_scale: float = 100.0) -> dict:
    sc = Scenario(name="safe_mode_mc", seed=case["seed"], tumble_dps=case["tumble"],
                  inertia=case["inertia"], true_anomaly_deg=case["anomaly"],
                  duration_s=DURATION, initial_soc=0.5)
    out = dict(case, eclipse_start=False, acquired_s=None, median_deg=None, still_safe=False,
               error=None)
    try:
        with Flight(time_scale) as fl:
            bridge = Bridge(fl.sim)
            bridge.connect()
            down = Downlink(fl.ttc)
            # SAFE before the first sensor sample: the ground may always ask for it.
            down.client.send("SET_MODE", mode="SAFE")
            if down.client.wait_for("VERIF_COMPLETE_OK", timeout=10.0) is None:
                raise RuntimeError("SET_MODE SAFE was not acknowledged")
            sim = Simulation(sc, bridge)
            held_since = acquired = None
            after: list[float] = []
            try:
                for k in range(int(sc.duration_s / sc.dt)):
                    sim.step()
                    if k == 0:
                        out["eclipse_start"] = sim.eclipsed
                    if k % 50 == 0:
                        down.drain(sim.t)
                    angle = sim.main_panel_sun_deg()
                    if angle is None:
                        continue
                    if angle < ACQUIRED_DEG:
                        held_since = sim.t if held_since is None else held_since
                        if acquired is None and sim.t - held_since >= HOLD_S:
                            acquired = held_since
                    else:
                        held_since = None
                    if acquired is not None:
                        after.append(angle)
                for _ in range(30):
                    sim.step()
                    down.drain(sim.t)
            finally:
                down.close()
                bridge.close()
        out["acquired_s"] = round(acquired, 0) if acquired is not None else None
        out["median_deg"] = round(statistics.median(after), 1) if after else None
        out["still_safe"] = down.hk.get("SYS_HK", {}).get("mode") == "SAFE"
    except Exception as exc:
        out["error"] = f"{type(exc).__name__}: {exc}"
    out["passed"] = verdict(out) == ""
    return out


def verdict(r: dict) -> str:
    if r["error"]:
        return r["error"]
    if r["acquired_s"] is None or r["acquired_s"] > ACQUIRE_WITHIN:
        return f"not acquired within {ACQUIRE_WITHIN:.0f} s"
    if r["median_deg"] is None or r["median_deg"] >= ACQUIRED_DEG:
        return f"median {r['median_deg']} deg after acquisition"
    if not r["still_safe"]:
        return "left SAFE"
    return ""


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--runs", type=int, default=8)
    ap.add_argument("--jobs", type=int, default=min(4, os.cpu_count() or 1))
    ap.add_argument("--seed", type=int, default=3000, help="seed of the first run")
    ap.add_argument("--only", type=int, help="fly just this seed")
    ap.add_argument("--time-scale", type=float, default=100.0)
    args = ap.parse_args()
    if not FSW.exists():
        print("build/fsw not found -- run `make build` first")
        return 2

    seeds = [args.only] if args.only is not None else list(range(args.seed, args.seed + args.runs))
    cases = [disperse(s) for s in seeds]
    print(f"safe mode monte carlo: {len(cases)} runs that start in SAFE, {args.jobs} at a time")
    print(f"  requirements: within {ACQUIRED_DEG:.0f} deg of the Sun for {HOLD_S:.0f} s of sunlight "
          f"within {ACQUIRE_WITHIN:.0f} s; median below {ACQUIRED_DEG:.0f} deg after; still in SAFE\n")
    print("   seed  inertia (kg m^2)          tumble  starts in   acquired   median  result")
    t0 = time.time()
    results = []
    with ProcessPoolExecutor(max_workers=args.jobs) as pool:
        for r in pool.map(fly, cases, [args.time_scale] * len(cases)):
            results.append(r)
            why = verdict(r)
            inertia = "/".join(f"{i:.4f}" for i in r["inertia"])
            acq = f"{r['acquired_s']:7.0f} s" if r["acquired_s"] is not None else f"{'-':>9}"
            med = f"{r['median_deg']:5.1f} deg" if r["median_deg"] is not None else f"{'-':>9}"
            print(f"  {r['seed']:5d}  {inertia:<24}  {r['tumble']:4.2f}   "
                  f"{'eclipse' if r['eclipse_start'] else 'sunlight':<9}  {acq}  {med}  "
                  f"{'ok' if not why else 'FAIL: ' + why}", flush=True)

    passed = [r for r in results if r["passed"]]
    print(f"\n  ({time.time() - t0:.0f} s wall clock)")
    print(f"\n  {len(passed)} of {len(results)} runs met every requirement")
    acq = [r["acquired_s"] for r in results if r["acquired_s"] is not None]
    if acq:
        print(f"  acquisition  median {statistics.median(acq):.0f} s, worst {max(acq):.0f} s "
              f"(requirement {ACQUIRE_WITHIN:.0f} s)")
    for r in results:
        if not r["passed"]:
            print(f"  replay a failure:  python3 -m sim.scenarios.safe_mode_mc --only {r['seed']}")
    return 0 if len(passed) == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
