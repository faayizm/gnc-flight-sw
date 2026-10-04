#!/usr/bin/env python3
"""Monte Carlo: the wheel-failure recovery, flown many times, never the same.

One successful run of a scenario proves that one case works. A spacecraft
meets the cases nobody wrote down: a slightly different mass distribution, a
different tumble, the fault at a different moment, on a different axis. A
Monte Carlo campaign flies the same scenario many times with every uncertain
quantity drawn at random -- DISPERSED -- and asks how many runs meet the
requirements, and how close the worst one came.

Dispersed in every run, all from one seed:
    true inertia            +-10% per axis (the flight software's own
                            estimate stays fixed: it cannot know the truth)
    initial attitude, rate  0.2 to 2 deg/s, any axis
    sensor errors           gyro bias, noise, every random draw
    the failure             which wheel dies, and when

Requirements, per run:
    the failed wheel is isolated within 60 s
    the spacecraft does not give up (no SAFE)
    900 s after the failure, the nadir error stays below 2 deg

How quickly a failure is DETECTED is reported, but not required, and the
first version of this campaign got that wrong. It demanded detection within
20 s, and seed 2042 missed: its X wheel was nearly at rest, and asked for
almost nothing, when it died. A dead wheel that is asked for nothing looks
exactly like a healthy one -- the evidence only arrives once the controller
leans on it, 37 s later. The same idleness is why that failure cost only
1.5 deg. A requirement must be about what matters (here, isolation in time
to keep pointing), not about a number the physics may not let you see.

A run that fails prints its seed. `--only SEED` flies exactly that run
again, because every run is reproducible.

    python3 -m sim.scenarios.monte_carlo --runs 8          (what CI flies)
    python3 -m sim.scenarios.monte_carlo --runs 100 --jobs 8
    make monte-carlo RUNS=100
"""

from __future__ import annotations

import argparse
import math
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
from sim.sil.faults import Fault                        # noqa: E402
from sim.sil.harness import FSW, Downlink, Flight       # noqa: E402
from sim.sil.simulation import Scenario, Simulation     # noqa: E402

NOMINAL_INERTIA = (0.10, 0.12, 0.04)
AXES = ("wheel_x", "wheel_y", "wheel_z")
ISOLATE_S = 60.0
SETTLE_S, OBSERVE_S = 900.0, 1500.0
REQUIREMENT_DEG = 2.0


def disperse(seed: int) -> dict:
    rng = random.Random(seed)
    inertia = tuple(round(i * rng.uniform(0.9, 1.1), 5) for i in NOMINAL_INERTIA)
    return dict(seed=seed, inertia=inertia, tumble=round(rng.uniform(0.2, 2.0), 2),
                axis=rng.randrange(3), fail_t=round(rng.uniform(1500.0, 2500.0), 1))


def fly(case: dict, time_scale: float = 100.0) -> dict:
    """One run. Returns the case with its results added."""
    fail_t = case["fail_t"]
    sc = Scenario(name="monte_carlo", seed=case["seed"], tumble_dps=case["tumble"],
                  inertia=case["inertia"], duration_s=fail_t + SETTLE_S + OBSERVE_S,
                  faults=[Fault("dead", AXES[case["axis"]], fail_t)])
    out = dict(case, pointing_t=None, detect_s=None, isolate_s=None, safe=False,
               peak_deg=0.0, worst_deg=0.0, worst_att_deg=0.0, error=None)
    try:
        with Flight(time_scale) as fl:
            bridge = Bridge(fl.sim)
            bridge.connect()
            down = Downlink(fl.ttc)
            sim = Simulation(sc, bridge)
            seen: dict[str, float] = {}

            def on_event(t: float, name: str) -> None:
                seen.setdefault(name, t)

            try:
                for k in range(int(sc.duration_s / sc.dt)):
                    sim.step()
                    if k % 2 == 0:
                        down.drain(sim.t, on_event=on_event)
                    if out["pointing_t"] is None and sim.flags & 2:
                        out["pointing_t"] = round(sim.t, 1)
                    if sim.t >= fail_t:
                        e = sim.nadir_error_deg()
                        out["peak_deg"] = max(out["peak_deg"], e)
                        if sim.t >= fail_t + SETTLE_S:
                            out["worst_deg"] = max(out["worst_deg"], e)
                            out["worst_att_deg"] = max(out["worst_att_deg"], sim.attitude_error_deg())
            finally:
                down.close()
                bridge.close()
        if "WHEEL_FAULT" in seen:
            out["detect_s"] = round(seen["WHEEL_FAULT"] - fail_t, 1)
        if "WHEEL_ISOLATED" in seen:
            out["isolate_s"] = round(seen["WHEEL_ISOLATED"] - fail_t, 1)
        out["safe"] = "SAFE_MODE_ENTERED" in seen
    except Exception as exc:                     # a crashed run is a failed run, with a reason
        out["error"] = f"{type(exc).__name__}: {exc}"
    out["passed"] = verdict(out) == ""
    return out


def verdict(r: dict) -> str:
    """Empty if the run met every requirement, else what it missed."""
    if r["error"]:
        return r["error"]
    if r["pointing_t"] is None or r["pointing_t"] > r["fail_t"]:
        return "never pointed before the failure"
    if r["isolate_s"] is None or r["isolate_s"] > ISOLATE_S:
        return f"isolation {r['isolate_s']} s"
    if r["safe"]:
        return "went SAFE"
    if r["worst_deg"] >= REQUIREMENT_DEG:
        return f"nadir error {r['worst_deg']:.2f} deg"
    return ""


def pct(values: list[float], p: float) -> float:
    s = sorted(values)
    return s[min(len(s) - 1, int(math.ceil(p / 100.0 * len(s))) - 1)]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--runs", type=int, default=8)
    ap.add_argument("--jobs", type=int, default=min(4, os.cpu_count() or 1))
    ap.add_argument("--seed", type=int, default=1000, help="seed of the first run")
    ap.add_argument("--only", type=int, help="fly just this seed")
    ap.add_argument("--time-scale", type=float, default=100.0)
    args = ap.parse_args()
    if not FSW.exists():
        print("build/fsw not found -- run `make build` first")
        return 2

    seeds = [args.only] if args.only is not None else list(range(args.seed, args.seed + args.runs))
    cases = [disperse(s) for s in seeds]
    print(f"monte carlo: {len(cases)} wheel-failure runs, {args.jobs} at a time")
    print(f"  requirements: isolated < {ISOLATE_S:.0f} s, no SAFE, "
          f"nadir < {REQUIREMENT_DEG} deg from {SETTLE_S:.0f} s after the failure\n")
    print("   seed  inertia (kg m^2)          tumble  wheel  fails at  detect  isolate"
          "   peak   worst  3-axis  result")
    t0 = time.time()
    results = []
    with ProcessPoolExecutor(max_workers=args.jobs) as pool:
        for r in pool.map(fly, cases, [args.time_scale] * len(cases)):
            results.append(r)
            why = verdict(r)
            inertia = "/".join(f"{i:.4f}" for i in r["inertia"])
            fmt = lambda v, w, p=1: f"{v:{w}.{p}f}" if v is not None else f"{'-':>{w}}"  # noqa: E731
            print(f"  {r['seed']:5d}  {inertia:<24}  {r['tumble']:4.2f}   {'XYZ'[r['axis']]}    "
                  f"{r['fail_t']:7.1f}  {fmt(r['detect_s'], 6)}  {fmt(r['isolate_s'], 7)}  "
                  f"{r['peak_deg']:5.1f}  {r['worst_deg']:6.2f}  {r['worst_att_deg']:6.1f}  "
                  f"{'ok' if not why else 'FAIL: ' + why}", flush=True)

    passed = [r for r in results if r["passed"]]
    print(f"\n  ({time.time() - t0:.0f} s wall clock)")
    print(f"\n  {len(passed)} of {len(results)} runs met every requirement")
    ok = [r for r in results if r["detect_s"] is not None]
    if ok:
        det = [r["detect_s"] for r in ok]
        iso = [r["isolate_s"] for r in ok if r["isolate_s"] is not None]
        worst = [r["worst_deg"] for r in results if not r["error"]]
        print(f"  detection   mean {statistics.mean(det):5.1f} s   worst {max(det):5.1f} s")
        if iso:
            print(f"  isolation   mean {statistics.mean(iso):5.1f} s   worst {max(iso):5.1f} s")
        print(f"  nadir error after settling: median {statistics.median(worst):.2f} deg, "
              f"90th percentile {pct(worst, 90):.2f} deg, worst {max(worst):.2f} deg "
              f"(requirement {REQUIREMENT_DEG} deg)")
    for r in results:
        if not r["passed"]:
            print(f"  replay a failure:  python3 -m sim.scenarios.monte_carlo --only {r['seed']}")
    return 0 if len(passed) == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
