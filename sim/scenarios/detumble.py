#!/usr/bin/env python3
"""Detumble: B-dot brings a 10 deg/s tumble below 0.5 deg/s.

Starts the real flight binary, connects the simulator to it, flies the
scenario in lockstep, and asserts on the truth the simulator holds -- the one
thing the flight software is never shown.

    python3 -m sim.scenarios.detumble            (from the repository root)
    make detumble
"""

from __future__ import annotations

import argparse
import hashlib
import pathlib
import socket
import struct
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "gnd"))

from pyground.client import GroundClient          # noqa: E402
from sim.sil.bridge import Bridge                 # noqa: E402
from sim.sil.simulation import Scenario, Simulation  # noqa: E402

FSW = ROOT / "build" / "fsw"

SCENARIO = Scenario(name="detumble", seed=1, duration_s=2.5 * 5677.0, tumble_dps=10.0)
TARGET_DPS = 0.5


def free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return int(s.getsockname()[1])


class Flight:
    def __init__(self, time_scale: float):
        self.ttc, self.sim = free_port(), free_port()
        self.nvm = ROOT / "build" / f"sim_nvm_{self.sim}.bin"
        self.args = [str(FSW), "--ttc-port", str(self.ttc), "--sim-port", str(self.sim),
                     "--time-scale", str(time_scale), "--nvm", str(self.nvm)]

    def __enter__(self):
        self.nvm.unlink(missing_ok=True)
        self.proc = subprocess.Popen(self.args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return self

    def __exit__(self, *_):
        self.proc.terminate()
        try:
            self.proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.proc.kill()
        self.nvm.unlink(missing_ok=True)


def run(scenario: Scenario, time_scale: float, verbose: bool = True):
    """Fly a scenario. Returns (history, final_state_digest, last ADCS_HK fields, events)."""
    history = []        # (t, truth rate deg/s)
    adcs_hk = None
    events: list[str] = []
    with Flight(time_scale) as fl:
        bridge = Bridge(fl.sim)
        bridge.connect()
        gnd = GroundClient(port=fl.ttc)
        gnd.connect()
        sim = Simulation(scenario, bridge)
        steps = int(scenario.duration_s / scenario.dt)
        under_target_since = None
        try:
            for k in range(steps):
                sim.step()
                if k % 10 == 0:
                    history.append((sim.t, sim.rate_dps))
                if k % 50 == 0:
                    for tm in gnd.poll_nowait():
                        if tm.name == "ADCS_HK":
                            adcs_hk = tm.fields
                        elif tm.name.startswith("EVENT"):
                            events.append(tm.fields["event_name"])
                            if verbose:
                                print(f"  t={sim.t:8.0f} s  event {events[-1]}")
                    if verbose and k % 6000 == 0:
                        print(f"  t={sim.t:8.0f} s  rate={sim.rate_dps:7.3f} deg/s")
                if sim.rate_dps < TARGET_DPS:
                    under_target_since = under_target_since or sim.t
                else:
                    under_target_since = None
                # Stop once the flight software has handed over and the rate has
                # held below target for ten minutes of simulated time.
                if (scenario.name == "detumble" and "DETUMBLE_COMPLETE" in events
                        and under_target_since and sim.t - under_target_since > 600):
                    break
        finally:
            # No ADCS_HK is taken here: once the sim stops sending, the flight
            # software correctly declares the magnetometer stale.
            gnd.close()
            bridge.close()
    digest = hashlib.sha256(struct.pack(">8d", sim.t, *sim.body.q, *sim.body.omega)).hexdigest()[:16]
    return history, digest, adcs_hk, events, sim


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--time-scale", type=float, default=100.0)
    ap.add_argument("--determinism", action="store_true", help="also check two runs agree bit for bit")
    args = ap.parse_args()

    if not FSW.exists():
        print("build/fsw not found -- run `make build` first")
        return 2

    fails: list[str] = []

    def check(ok: bool, what: str) -> None:
        print(f"  {'.' if ok else 'x'}  {what}")
        if not ok:
            fails.append(what)

    print("detumble: 10 deg/s tumble, B-dot, magnetometer only")
    t0 = time.time()
    hist, digest, hk, events, sim = run(SCENARIO, args.time_scale)
    print(f"  ({time.time() - t0:.1f} s wall clock, {sim.t:.0f} s simulated)")

    first = next((t for t, r in hist if r < TARGET_DPS), None)
    check(first is not None, f"truth rate falls below {TARGET_DPS} deg/s"
          + (f" (at t = {first:.0f} s, {first / 5677:.2f} orbits)" if first else ""))
    check(first is not None and first < 2.0 * 5677.0, "within two orbits")
    check(hist[-1][1] < TARGET_DPS, f"and stays there (final {hist[-1][1]:.3f} deg/s)")
    check(max(r for _, r in hist[:5]) > 9.0, "the run really did start from a ~10 deg/s tumble")
    check(hk is not None and hk["mag_valid"] == 1, "ADCS_HK is downlinked with a valid magnetometer")
    if hk is not None:
        check(abs(hk["rate_norm"] - sim.rate_dps) < 0.3,
              f"downlinked rate ({hk['rate_norm']:.3f}) agrees with truth ({sim.rate_dps:.3f}) deg/s")
    check("DETUMBLE_COMPLETE" in events, "the flight software announced DETUMBLE_COMPLETE")

    if args.determinism:
        print("determinism: same seed, second run")
        short = Scenario(name="determinism", seed=1, duration_s=600.0)
        d1 = run(short, args.time_scale, verbose=False)[1]
        d2 = run(short, args.time_scale + 37.0, verbose=False)[1]
        check(d1 == d2, f"identical final state at different time scales ({d1})")

    print(f"\n{'FAILED: ' + str(len(fails)) if fails else 'all checks passed'}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
