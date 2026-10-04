# Fault detection, isolation and recovery

How this spacecraft notices that something is wrong, decides what, and copes,
and how all of that is tested. The teaching version is
[Lesson 18](../learn/18-when-things-break/); this is the reference.

## Where each fault is handled

Small faults are handled where they happen, by the application that sees
them. Faults that need a view across applications, or a decision bigger than
any one of them, go to the FDIR application.

| Fault | Detected by | Response | Code |
|---|---|---|---|
| Corrupted telecommand | CRC, frame checks | Refused, `TC_REJECTED` | `apps/ttc/` |
| Lost telecommand | FARM-1 sequence numbers | Retransmitted by the ground (COP-1) | `apps/ttc/tc_receiver.*` |
| Sensor reading out of physical range | Range check at the hardware boundary | Valid flag cleared, `SENSOR_REJECTED` | `apps/io/sensor_screen.hpp` |
| Sensor frozen | Identical readings, 10 in a row | Valid flag cleared, `SENSOR_REJECTED` | `apps/io/sensor_screen.hpp` |
| Corrupted sensor frame | Bridge CRC | Frame dropped, never answered | `apps/io/sim_bridge.cpp` |
| Gap in sensor data | Sensor timestamps; host-clock timeout | `SENSOR_GAP`; `SENSOR_TIMEOUT`, actuators zeroed | `apps/io/sim_io_app.cpp` |
| Estimator diverged | 10 s of large innovations | Filter discarded and restarted, `ESTIMATOR_RESET` | `apps/adcs/adcs_app.cpp` |
| Battery running down | State of charge | Load shedding, then SAFE | `apps/eps/`, `apps/modemgr/` |
| Reaction wheel not delivering torque | Momentum bookkeeping, 5 s windows | The wheel ladder, below | `apps/fdir/wheel_check.hpp`, `wheel_ladder.hpp` |
| A housekeeping value out of limits | ST[12] monitors | Event, and an ST[19] action if one is defined | `apps/fdir/monitoring.hpp`, `apps/ttc/ttc_app.cpp` |
| Bit flip in the parameter table | EDAC on read; scrubber | Corrected; if uncorrectable, default then reload from storage | `core/edac.hpp`, `core/param_store.hpp`, `apps/fdir/fdir_app.cpp` |
| Bit flip in mode or wheel isolation | TMR vote on every read | Outvoted and repaired | `core/tmr.hpp` |
| Software stops making progress | Watchdog | Reset; boot count and reset cause recorded | `platform/posix/posix_watchdog.*`, `main.cpp` |

## The wheel ladder

```
   MONITOR ──fault──▶ CYCLE_OFF ──3 s──▶ VERIFY ──3 good windows──▶ MONITOR   (WHEEL_RECOVERED)
                                           └──fails again──▶ isolated     (WHEEL_ISOLATED)
```

1. **Report.** `WHEEL_FAULT`, aux = the wheel mask.
2. **Retry.** The WHEELS rail is switched off for three seconds
   (`FdirRails`, applied by EPS on top of every other rail decision) and on
   again. This clears a single-event latch-up. At most two retries per
   wheel; the ground can restore the budget with `RESTORE_WHEELS`.
3. **Isolate and reconfigure.** The wheel leaves `WheelHealth.usable`. The
   pointing controller gives its axis to the magnetorquers,
   `m = (B × τ) / |B|²`, at a bandwidth of 0.01 rad/s instead of 0.1. The
   healthy wheels cancel what the coils put on their axes, and momentum
   dumping leaves the dead wheel alone.
4. **Safe.** With fewer than two wheels in service, the mode manager leaves
   POINTING for SAFE (reason `ACTUATORS`) and refuses requests to point.

The check is skipped for a window in which a wheel is at its momentum or
torque limit, because a disagreement then proves nothing. It cannot judge a
wheel that is not being asked to do anything (see "What the Monte Carlo
found" below).

## Monitoring and event-action

Monitors (PUS ST[12]) and event-actions (ST[19]) are declared in the
dictionary and generated. They are listed in the [ICD](ICD.md).

- A monitor evaluates one housekeeping field each time it is published
  (10 Hz). The status changes only after `repetitions` samples in a row, in
  either direction. Every change is downlinked as ST[12,12], and a change to
  BELOW or ABOVE raises the monitor's event.
- An event-action is a complete telecommand, run through `handle_tc` exactly
  as if uplinked. Actions are queued by the event sink and run at the start
  of the next 50 Hz tick. Under lockstep that is always before the next
  sensor sample, so a run is reproducible.
- `pointing_err_deg`, which the POINTING monitor watches, is the boresight's
  angle from nadir. Yaw is in `att_err_deg`, and is not monitored.

## Radiation

| Memory | Protection | On an upset |
|---|---|---|
| Parameter table | EDAC, extended Hamming (72,64), one check byte per value | Single flip: corrected on every read, repaired by the scrubber (every 16 samples, on the sensor clock). Double flip: reads as the default, `EDAC_UNCORRECTABLE`, table reloaded from storage, `PARAMS_RELOADED` |
| Spacecraft mode | TMR | Outvoted on the next read, repaired |
| Wheel isolation mask | TMR | Outvoted on the next read, repaired |

In the software-in-the-loop build the simulator delivers upsets through a
field in the sensor frame (target, bit). Real hardware has no such field;
it is the only way a simulator can reach the flight computer's memory. FDIR
routes each upset to the memory it names.

## Tests

| What | Where |
|---|---|
| Each mechanism in isolation, including every single and double flip of the EDAC code | `tests/unit/test_fdir.cpp`, `tests/unit/test_radiation.cpp` |
| The watchdog resets the real binary, which comes back knowing why | `tests/sil/test_endtoend.py` |
| Sensor, bus and wheel faults, then a double fault ending in SAFE | `make fdir` |
| An orbit of upsets, ten times worse in the South Atlantic Anomaly | `make radiation` |
| A wheel failure with mass, tumble, sensor errors and the failure dispersed | `make monte-carlo RUNS=N` |

## What the Monte Carlo found

Two problems no scripted scenario had shown:

- **Yaw sent a working spacecraft to SAFE.** `pointing_err_deg` held the
  three-axis error, not the boresight error that the dictionary describes.
  After a Z-wheel failure the spacecraft drifted in yaw, harmlessly, but the
  POINTING monitor saw it. Fixed by making the field mean what the
  dictionary says, and adding `att_err_deg`.
- **A detection-time requirement that physics cannot meet.** A wheel at rest
  and commanded almost nothing is indistinguishable from a healthy one. It
  took 37 s to detect, and the failure cost 1.5°. The campaign's requirement
  is now isolation within 60 s and pointing held, rather than a detection
  time.

## Known limitations

- One power switch serves all three wheels, so a retry interrupts attitude
  control on every axis for three seconds.
- With the Z wheel out of service, yaw is controlled only magnetically and
  can wander by tens of degrees. Nadir pointing is unaffected.
- Only the parameter table, the mode and the isolation mask are protected.
  Every application's working state is unprotected RAM, as it would be on a
  processor without EDAC memory.
- The hosted watchdog's timeout is 5 s of host time, not the flight build's
  60 ms. It is there to catch a wedged loop, not to measure timing; the
  notional watchdog still measures that.
