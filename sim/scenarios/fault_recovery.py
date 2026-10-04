#!/usr/bin/env python3
"""Fault recovery: break the spacecraft, on purpose, and watch it cope.

One flight, four acts, every fault injected by the simulator at the hardware
(sim/sil/faults.py) and every response decided on board, with nobody on the
ground to help:

  1. LYING SENSORS AND A NOISY BUS. A frozen gyro, an impossible
     magnetometer reading, a frozen star tracker, corrupted and dropped
     frames, an impossible GPS fix. Each is caught at the hardware boundary
     and kept out of the estimator; pointing barely notices.

  2. A LATCH-UP. The Y wheel's drive stops. FDIR notices it is not delivering
     its torque, power-cycles the drives -- and it works again. The cheapest
     rung of the ladder was the right one.

  3. A DEAD WHEEL. The X wheel's drive stops for good. The power cycle does
     not help, so FDIR takes the wheel out of service and attitude control
     carries on with two wheels and the magnetorquers. Pointing is looser,
     but it is pointing.

  4. A SECOND FAULT. The magnetometer then fails too -- and the
     magnetorquers were what replaced the dead wheel. Nothing below the top
     of the ladder can help now. The on-board pointing monitor (PUS ST[12])
     sees the error stay high for five minutes, its event triggers the
     event-action (PUS ST[19]), and the spacecraft commands itself into SAFE.
     SAFE switches the transmitter off, so the ground learns all this the
     way it really would: at the next pass, from the spacecraft's own record.

Every assertion is against the simulator's truth, or the telemetry that came
down -- never against what the flight software believes about itself.

    python3 -m sim.scenarios.fault_recovery
    make fdir
"""

from __future__ import annotations

import argparse
import pathlib
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from sim.sil.bridge import Bridge                       # noqa: E402
from sim.sil.faults import Fault                        # noqa: E402
from sim.sil.harness import FSW, Checks, Downlink, Flight  # noqa: E402
from sim.sil.simulation import Scenario, Simulation     # noqa: E402

ACT1 = [Fault("frozen", "gyro", 300, 330), Fault("range", "mag", 400, 430),
        Fault("frozen", "star", 500, 560), Fault("corrupt", "bus", 600, 660, rate=0.2),
        Fault("drop", "bus", 700, 703), Fault("range", "gps", 800, 840)]
LATCH_T = 1000.0
DEAD_T = 1500.0
SECOND_T = 6000.0
ACT2_3 = [Fault("latched", "wheel_y", LATCH_T), Fault("dead", "wheel_x", DEAD_T)]
ACT4 = [Fault("range", "mag", SECOND_T, 7200)]

SCENARIO = Scenario(name="fault_recovery", seed=3, duration_s=7500.0, tumble_dps=0.3,
                    faults=ACT1 + ACT2_3 + ACT4)

SETTLE_S = 900.0               # allowed for the reconfigured controller to settle
NOMINAL_DEG = 0.5              # act 1 requirement: sensor faults barely matter
DEGRADED_DEG = 2.0             # act 3 requirement: two wheels and the coils


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--time-scale", type=float, default=100.0)
    args = ap.parse_args()
    if not FSW.exists():
        print("build/fsw not found -- run `make build` first")
        return 2

    check = Checks()
    print("fault recovery: four acts of things going wrong, nobody on the ground")
    t0 = time.time()

    worst = {1: 0.0, 2: 0.0, 3: 0.0}
    act3_samples = 0
    peak_after_death = 0.0
    events: list[tuple[float, str, int]] = []
    playback: list = []

    with Flight(args.time_scale) as fl:
        bridge = Bridge(fl.sim)
        bridge.connect()
        down = Downlink(fl.ttc)
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
                if k % 2 == 0:            # every 0.2 s: detection times are measured from this
                    down.drain(sim.t, on_event=on_event)
                    for tm in down.other:
                        if tm.name == "MONITOR_REPORT":
                            print(f"  t={sim.t:7.1f} s  {tm.summary()}")
                    down.other.clear()
                e = sim.nadir_error_deg()
                if 200 <= sim.t < LATCH_T:
                    worst[1] = max(worst[1], e)
                elif LATCH_T <= sim.t < DEAD_T:
                    worst[2] = max(worst[2], e)
                elif DEAD_T <= sim.t < SECOND_T:
                    peak_after_death = max(peak_after_death, e)
                    if sim.t >= DEAD_T + SETTLE_S:
                        worst[3] = max(worst[3], e)
                        act3_samples += 1
                if k % 3000 == 0:
                    print(f"  t={sim.t:7.0f} s  nadir err {e:7.3f} deg  rate {sim.rate_dps:6.3f} deg/s  "
                          f"wheels {[round(h * 1e3, 2) for h in sim.wheels.h]} mNms", flush=True)

            # ---- the next pass: wake the transmitter, replay the record ----
            # Any uplink holds the transmitter on for ten minutes; the flight
            # carries on meanwhile, so EPS sees the uplink and acts on it.
            alive_until = down.last_time_s
            down.client.send("TEST_CONNECTION")
            for _ in range(100):
                sim.step()
                down.drain(sim.t)
            down.client.send("RETRIEVE_BY_TIME", store_id=1, from_s=int(alive_until) - 60,
                             to_s=0xFFFFFFFF)
            done = False
            for _ in range(3000):
                sim.step()
                for tm in down.client.poll_nowait():
                    if tm.vcid == 1:
                        playback.append(tm)
                    elif tm.fields.get("event_name") == "PLAYBACK_DONE":
                        done = True
                if done:
                    break
            playback += [tm for tm in down.client.poll(timeout=1.0) if tm.vcid == 1]
        finally:
            down.close()
            bridge.close()

    print(f"  ({time.time() - t0:.1f} s wall clock, {sim.t:.0f} s simulated)\n")

    def first(name: str, after: float = 0.0, aux: int | None = None):
        return next((t for t, n, a in events if n == name and t >= after
                     and (aux is None or a == aux)), None)

    def within(name: str, start: float, limit: float, aux: int | None = None) -> bool:
        t = first(name, start, aux)
        return t is not None and t - start <= limit

    print("act 1: lying sensors and a noisy bus")
    check(within("SENSOR_REJECTED", 300, 2, aux=(1 << 8) | 2), "a frozen gyro is refused within 2 s")
    check(within("SENSOR_REJECTED", 400, 1, aux=(0 << 8) | 1), "an impossible magnetometer reading at once")
    check(within("SENSOR_REJECTED", 500, 6, aux=(3 << 8) | 2), "a frozen star tracker within 6 s")
    check(within("SENSOR_REJECTED", 800, 2, aux=(4 << 8) | 1), "an impossible GPS fix within 2 s")
    readmitted = {a for t, n, a in events if n == "SENSOR_READMITTED" and t < LATCH_T}
    check(readmitted == {0, 1, 3, 4}, "each sensor is readmitted once it behaves")
    check(sim.faults.frames_corrupted > 50,
          f"{sim.faults.frames_corrupted} sensor frames were corrupted in transit, and none answered")
    check(within("SENSOR_GAP", 700, 5), "the 3 s bus dropout is reported when data resumes")
    check(worst[1] < NOMINAL_DEG, f"nadir error stays below {NOMINAL_DEG} deg throughout "
          f"(worst {worst[1]:.3f} deg)")

    print("act 2: a latch-up, cleared by a power cycle")
    check(within("WHEEL_FAULT", LATCH_T, 15, aux=2), "the Y wheel is caught not delivering its torque")
    check(within("WHEEL_POWER_CYCLE", LATCH_T, 15, aux=2), "the drives are power-cycled")
    check(within("WHEEL_RECOVERED", LATCH_T, 60, aux=2), "and the Y wheel works again")
    check(first("WHEEL_ISOLATED", LATCH_T) is None or first("WHEEL_ISOLATED", LATCH_T) > DEAD_T,
          "so it is NOT taken out of service")
    check(worst[2] < NOMINAL_DEG, f"pointing hardly notices (worst {worst[2]:.3f} deg)")

    print("act 3: a dead wheel, isolated, and control reconfigured around it")
    check(within("WHEEL_FAULT", DEAD_T, 15, aux=1), "the X wheel is caught within 15 s")
    check(within("WHEEL_ISOLATED", DEAD_T, 30, aux=1), "the retry fails, and it is isolated within 30 s")
    check(first("SAFE_MODE_ENTERED", DEAD_T) is None or first("SAFE_MODE_ENTERED", DEAD_T) > SECOND_T,
          "the spacecraft keeps pointing: one wheel down is not a reason to give up")
    check(act3_samples > 10000 and worst[3] < DEGRADED_DEG,
          f"after {SETTLE_S:.0f} s to settle, nadir error stays below {DEGRADED_DEG} deg on two wheels "
          f"and the magnetorquers (worst {worst[3]:.2f} deg; peak while the dead wheel spun down "
          f"{peak_after_death:.1f} deg)")

    print("act 4: a second fault removes the fallback (from the replayed on-board record)")
    record = [(tm.time_s, tm.fields.get("event_name"), tm.fields.get("aux")) for tm in playback
              if tm.name.startswith("EVENT")]
    reports = [tm for tm in playback if tm.name == "MONITOR_REPORT"]
    print("  replayed: " + ", ".join(n for _, n, _ in record if n != "SCHED_OVERRUN"))
    names = [n for _, n, _ in record]
    check(len(playback) > 0 and all(tm.crc_ok for tm in playback),
          f"the next pass replays {len(playback)} packets from the store, every one intact")
    check("POINTING_LOST" in names, "the ST[12] pointing monitor raised POINTING_LOST")
    check(any(tm.fields["monitor"] == "POINTING" and tm.fields["to"] == "ABOVE" for tm in reports),
          "and recorded an ST[12,12] check transition report, POINTING WITHIN -> ABOVE")
    order = [n for n in names if n in ("POINTING_LOST", "EVENT_ACTION", "SAFE_MODE_ENTERED")]
    check(order[:3] == ["POINTING_LOST", "EVENT_ACTION", "SAFE_MODE_ENTERED"],
          "the event triggered its ST[19] action, which commanded SAFE")
    reason = next((a for _, n, a in record if n == "SAFE_MODE_ENTERED"), None)
    check(reason == 0, "through the same path as a ground command (SAFE reason GROUND: the "
          "action IS a telecommand)")
    check(sim.rate_dps < 1.0, f"and in SAFE the spacecraft is calm (rate {sim.rate_dps:.2f} deg/s)")
    return check.result()


if __name__ == "__main__":
    sys.exit(main())
