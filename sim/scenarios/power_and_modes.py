#!/usr/bin/env python3
"""Power and modes: a stuck heater drains the battery into SAFE mode, and the
spacecraft survives it on its own.

    0 s       tumbling at 3 deg/s, battery at 60%. The mode manager runs
              BOOT -> DETUMBLE -> STANDBY -> POINTING by itself.
    early     the ground asks for STANDBY while still tumbling: refused.
    1200 s    the operator switches the payload on.
    2500 s    FAULT: a heater thermostat sticks closed, +12 W the flight
              software knows nothing about.
              Battery LOW      -> payload shed            (level 1)
              below midpoint   -> transmitter off between contacts (level 2)
              CRITICAL         -> operational heaters shed (level 3) -- which,
                                  as it happens, removes the fault
              CRITICAL         -> SAFE: wheels off, B-dot only (level 4)
    then      sunlight recharges the battery to NOMINAL. The spacecraft stays
              in SAFE: leaving it takes a human.
    contacts  every 30 minutes the operator says hello, which keeps the
              transmitter on for 10 minutes and shows what happened. Once the
              battery is back, the operator leaves the stuck heater's rail off
              and asks for STANDBY; the mode manager takes it to POINTING.

Every claim is checked against simulator truth or against the spacecraft's own
record, replayed from its packet store at the end.

    python3 -m sim.scenarios.power_and_modes
    make power
"""

from __future__ import annotations

import argparse
import pathlib
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "gnd"))

from pyground.dictionary import ENUMS                    # noqa: E402
from sim.sil.bridge import Bridge                        # noqa: E402
from sim.sil.harness import FSW, Checks, Downlink, Flight  # noqa: E402
from sim.sil.simulation import Scenario, Simulation      # noqa: E402

ORBIT = 5677.0
FAULT_T, FAULT_W = 2500.0, 12.0
SCENARIO = Scenario(name="power_and_modes", seed=7, duration_s=3.2 * ORBIT, tumble_dps=3.0,
                    initial_soc=0.6, stuck_heater=(FAULT_T, FAULT_W))
CONTACT_EVERY = 1800.0
MODES = {v: k for k, v in ENUMS["SystemMode"].items()}
REFUSALS = {v: k for k, v in ENUMS["ModeRefusal"].items()}
POWER = {v: k for k, v in ENUMS["PowerState"].items()}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--time-scale", type=float, default=100.0)
    args = ap.parse_args()
    if not FSW.exists():
        print("build/fsw not found -- run `make build` first")
        return 2

    check = Checks()
    print("power and modes: a stuck heater, load shedding, SAFE, and recovery by the ground")
    t0 = time.time()

    truth_modes: list[tuple[float, str]] = []        # from live telemetry, when it flows
    refusals: list[tuple[float, str, str]] = []
    tx_off_since = None            # truth: when the transmitter rail last went off
    tx_off_total = 0.0
    frames_while_tx_off = 0
    seen_nominal_in_safe_at = None
    soc_min, soc_at_safe = 1.0, None
    safe_t = standby_t = repointed_t = None
    asked_standby_early = asked_standby_low = recovered = False
    last_contact = 0.0
    payload_on = False
    hk: dict = {}

    with Flight(args.time_scale) as fl:
        bridge = Bridge(fl.sim)
        bridge.connect()
        down = Downlink(fl.ttc)
        sim = Simulation(SCENARIO, bridge)

        def on_event(t, name):
            if name.startswith("MODE ") and "->" in name:
                truth_modes.append((t, name[5:]))
            if name in ("SCHED_OVERRUN",):
                return
            print(f"  t={t:7.0f} s  event {name}")

        try:
            for k in range(int(SCENARIO.duration_s / SCENARIO.dt)):
                sim.step()
                tx_on = bool(sim.power.rails & (1 << 2))
                if not tx_on and tx_off_since is None:
                    tx_off_since = sim.t
                elif tx_on and tx_off_since is not None:
                    tx_off_total += sim.t - tx_off_since
                    tx_off_since = None
                if k % 50 == 0:
                    frames_before = down.client.link.cadus
                    down.drain(sim.t, on_event=on_event)
                    hk = down.hk
                    # Frames already in flight when the rail switched off may
                    # still land; anything arriving 5 s later had no transmitter.
                    if tx_off_since is not None and sim.t - tx_off_since > 5.0:
                        frames_while_tx_off += down.client.link.cadus - frames_before
                soc_min = min(soc_min, sim.power.soc)

                sys_mode = hk.get("SYS_HK", {}).get("mode")
                power = hk.get("EPS_HK", {}).get("power_state")

                # ---- the operator ---------------------------------------
                if not asked_standby_early and sim.t > 60:
                    down.client.send("SET_MODE", mode="STANDBY")
                    asked_standby_early = True
                if not payload_on and sim.t > 1200:
                    down.client.send("SWITCH_RAIL", rail="PAYLOAD", on=1)
                    payload_on = True
                if sim.t - last_contact > CONTACT_EVERY:
                    last_contact = sim.t
                    down.client.send("TEST_CONNECTION")      # a pass: wakes the transmitter
                if sys_mode == "SAFE" and power != "NOMINAL" and not asked_standby_low and safe_t:
                    down.client.send("SET_MODE", mode="STANDBY")       # too soon: must be refused
                    asked_standby_low = True
                # A real operator reviews what happened before acting: having
                # seen the battery back at NOMINAL in one contact, they
                # recover the spacecraft in the next.
                if sys_mode == "SAFE" and power == "NOMINAL" and seen_nominal_in_safe_at is None:
                    seen_nominal_in_safe_at = sim.t
                if (seen_nominal_in_safe_at is not None and not recovered and sys_mode == "SAFE"
                        and sim.t - seen_nominal_in_safe_at > CONTACT_EVERY - 60
                        and sim.t - last_contact < 5):
                    down.client.send("SET_MODE", mode="POINTING")      # not from SAFE: refused
                    down.client.send("SWITCH_RAIL", rail="OPS_HEATERS", on=0)
                    down.client.send("SET_MODE", mode="STANDBY")
                    recovered = True

                # ---- truth bookkeeping -------------------------------------
                if safe_t is None and any(m.endswith("->SAFE") for _, m in truth_modes):
                    safe_t = sim.t
                    soc_at_safe = sim.power.soc
                if recovered and standby_t is None and any(m == "SAFE->STANDBY" for _, m in truth_modes):
                    standby_t = sim.t
                if standby_t and repointed_t is None and any(m == "STANDBY->POINTING" for _, m in truth_modes[-3:]):
                    repointed_t = sim.t
                if k % 6000 == 0:
                    print(f"  t={sim.t:7.0f} s  battery {sim.power.soc * 100:5.1f}%  load {sim.power.last.get('load', 0):5.1f} W"
                          f"  solar {sim.power.last.get('solar', 0):5.1f} W  rate {sim.rate_dps:5.2f} deg/s"
                          f"{'  [eclipse]' if sim.eclipsed else ''}", flush=True)
                if repointed_t and sim.t - repointed_t > 1500:
                    break
            final_nadir = sim.nadir_error_deg()

            # ---- the spacecraft's own record ------------------------------
            down.client.send("RETRIEVE_BY_TIME", store_id=1, from_s=0, to_s=0xFFFFFFFF)
            playback, done = [], False
            deadline = time.monotonic() + 120
            while not done and time.monotonic() < deadline:
                for tm in down.client.poll(timeout=0.2):
                    if tm.vcid == 1:
                        playback.append(tm)
                    elif tm.fields.get("event_name") == "PLAYBACK_DONE":
                        done = True
            playback += [tm for tm in down.client.poll(timeout=1.0) if tm.vcid == 1]
        finally:
            down.close()
            bridge.close()

    print(f"  ({time.time() - t0:.0f} s wall clock, {sim.t:.0f} s simulated)\n")

    events = [(tm.time_s, tm.fields["event_name"], tm.fields.get("aux")) for tm in playback
              if tm.name.startswith("EVENT")]
    modes = [(t, f"{MODES[a >> 8]}->{MODES[a & 0xFF]}") for t, n, a in events if n == "MODE_CHANGED"]
    refused = [(t, MODES[a >> 8], REFUSALS[a & 0xFF]) for t, n, a in events if n == "MODE_REFUSED"]
    sheds = [a for _, n, a in events if n == "LOAD_SHED"]
    powers = [f"{POWER[a >> 8]}->{POWER[a & 0xFF]}" for _, n, a in events if n == "POWER_STATE_CHANGED"]
    safe_reasons = [a for _, n, a in events if n == "SAFE_MODE_ENTERED"]
    print("  on-board record: modes   " + ", ".join(m for _, m in modes))
    print("                   power   " + ", ".join(powers))
    print("                   shedding " + " ".join(str(s) for s in sheds))
    print("                   refused " + ", ".join(f"{m} ({r})" for _, m, r in refused) + "\n")

    seq = [m for _, m in modes]
    check(seq[:3] == ["BOOT->DETUMBLE", "DETUMBLE->STANDBY", "STANDBY->POINTING"],
          "the spacecraft brought itself from BOOT to POINTING")
    check(any(m == "STANDBY" and r == "RATES_HIGH" for _, m, r in refused),
          "STANDBY requested while tumbling was refused: RATES_HIGH")
    check(sheds[:3] == [1, 2, 3], f"load was shed in order as the battery fell: levels {sheds[:4]}")
    check(safe_reasons and safe_reasons[0] == ENUMS["SafeReason"]["POWER_CRITICAL"],
          "the spacecraft put itself in SAFE because the battery went CRITICAL")
    check(soc_min > 0.10, f"the battery never went flat: lowest true charge {soc_min * 100:.1f}%")
    check(any(m == "STANDBY" and r == "POWER" for _, m, r in refused),
          "leaving SAFE while the battery was still low was refused: POWER")
    nominal_t = next((t for t, n, a in events if n == "POWER_STATE_CHANGED" and (a & 0xFF) ==
                      ENUMS["PowerState"]["NOMINAL"] and (a >> 8) == ENUMS["PowerState"]["LOW"]), None)
    left_safe_t = next((t for t, m in modes if m == "SAFE->STANDBY"), None)
    held = (left_safe_t - nominal_t) if nominal_t and left_safe_t else 0
    check(held > 600, f"back at NOMINAL, it stayed in SAFE until the ground asked ({held:.0f} s later)")
    check(any(m == "POINTING" and r == "NOT_FROM_MODE" for _, m, r in refused),
          "POINTING straight from SAFE was refused: NOT_FROM_MODE")
    check("SAFE->STANDBY" in seq and seq[-1] == "STANDBY->POINTING",
          "on request it left SAFE for STANDBY, then returned to POINTING by itself")
    check(final_nadir < 0.5, f"and is pointing again at the end ({final_nadir:.3f} deg nadir error)")
    check(sim.power.rails & (1 << 6) == 0,
          "the stuck heater's rail stayed off, because the operator said so")
    if tx_off_since is not None:
        tx_off_total += sim.t - tx_off_since
    check(tx_off_total > 1000 and frames_while_tx_off == 0,
          f"with the transmitter shed, nothing was sent between contacts "
          f"({tx_off_total:.0f} s of silence, {frames_while_tx_off} frames heard in it)")
    return check.result()


if __name__ == "__main__":
    sys.exit(main())
