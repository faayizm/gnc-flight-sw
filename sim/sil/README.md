# `sim/sil/` — the simulator bridge

**Not yet implemented. Phase 2.**

The transport between the simulator and the flight software: the second TCP
link, on port 50000, separate from the TT&C link the ground uses.

## Why a separate link

Sensor and actuator traffic is not telemetry. It runs at the control rate,
carries no PUS structure, and on real hardware it would be a sensor bus, not a
radio. Sharing the ground link would blur a distinction that matters and would
make the ground segment's packet counts meaningless.

## Planned protocol

The same CCSDS Space Packet framing, on dedicated APIDs, so that one decoder
and one set of tests serve both links:

| Direction | Contents |
|---|---|
| Simulator → flight software | Gyroscope, magnetometer, sun sensor, GPS or orbit state, wheel tachometers, timestamp |
| Flight software → simulator | Wheel torque demands, magnetorquer dipole demands, mode |

## Time

The simulator is the timing master. The flight software's `IClock` will be
driven from the simulator's clock, which is what makes faster-than-real-time
runs and single-stepping possible without the flight software knowing.

## Fault injection

The bridge is where faults are injected in Phase 6, because it is the boundary
where the spacecraft meets its environment: a sensor that freezes, a value that
goes out of range, a bit flipped in a packet, a link that drops for a minute, a
wheel that stops responding.
