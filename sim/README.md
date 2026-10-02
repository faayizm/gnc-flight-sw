# `sim/` — the spacecraft simulator

> 📚 **Learning this?** See [Lessons 12–16 — orbits, attitude, sensors, estimation, control](../learn/) in the lesson track.


**Phase 2, partly built.** Orbit, attitude dynamics, a dipole magnetic field,
gyro and magnetometer models, magnetorquers, the bridge and the detumble
scenario work. The sun sensor and eclipse work; IGRF and the other scenarios below do not exist yet.

```bash
make build
make detumble     # ~12 s: flies 10 deg/s down to ~0.4 deg/s over 0.8 orbit
```

Pure Python, standard library only — no NumPy — so it runs anywhere the rest
of the project does.

```
sim/
├── models/       physics: orbit, attitude dynamics, environment
├── sil/          the bridge: sensor packets out, actuator commands in
└── scenarios/    reproducible test cases, each a single file
```

## What it will be

A Python simulator holding the **truth**: where the spacecraft actually is,
which way it is actually pointing, what its sensors would actually read. It
connects to the flight software on a second TCP port (`--sim-port`, default
50000) — separate from the TT&C link on 50001 — and exchanges sensor and actuator packets every cycle.

```
   ┌──────────────────┐   sensor packets    ┌──────────────────┐
   │    SIMULATOR     │────────────────────▶│ FLIGHT SOFTWARE  │
   │                  │      port 50000     │                  │
   │  orbit, attitude │◀────────────────────│  estimate and    │
   │  sensors, actuators   actuator commands│  control         │
   └──────────────────┘                     └──────────────────┘
```

**The flight software never sees the truth.** It receives only what a sensor
would produce — noisy, biased, quantised, occasionally invalid — and must
estimate everything else. A simulator that hands the controller the true
attitude is a simulator that proves nothing, and it is an easy and tempting
mistake to make.

## The chosen approach

The dynamics are written here rather than taken from a library, because
deriving them is most of the learning. Planned:

- Rigid-body attitude dynamics, quaternion state, RK4 integration
- Two-body orbit with J2, which is enough for a low Earth orbit over days
- Environment: IGRF magnetic field, a solar vector with eclipse, atmospheric
  density for drag torque
- Sensors: gyroscope with bias random walk and noise, magnetometer, coarse sun
  sensors with a field of view and eclipse blindness, all quantised
- Actuators: reaction wheels with momentum limits and friction, magnetorquers
  with dipole limits and the constraint that torque is always perpendicular to
  the local magnetic field

**Orekit** enters later, at Phase 3, for high-fidelity orbit propagation,
proper IERS reference frames, eclipse geometry and ground station pass windows —
the places where its accuracy is genuinely worth adding a JVM to the loop.
Starting with it would have meant learning its API before seeing anything move.

## Determinism is a requirement

Every scenario declares its random seed. Two runs of the same scenario must
produce bit-identical results. Combined with the single-threaded flight
scheduler, that means a failure seen once can be reproduced exactly — which is
the property that makes a simulator worth building at all.

## Scenarios

Each scenario is one file: initial state, duration, what is injected, and what
is asserted. They run in CI. Planned for Phase 2 onwards:

| Scenario | Proves |
|---|---|
| `detumble.py` | B-dot brings 10 °/s of tumble below 0.5 °/s |
| `nadir_pointing.py` | Pointing error settles below 0.2° and stays there |
| `eclipse_cycle.py` | Attitude is held through loss of the sun reference |
| `gyro_bias.py` | The estimator converges on a real bias and removes it |
| `wheel_saturation.py` | Momentum is dumped to the magnetorquers before saturation |
| `sensor_dropout.py` | A failed magnetometer degrades cleanly instead of diverging |

## Things worth knowing about the detumble scenario

- **Lockstep.** The simulator sends one sensor frame and waits for the matching
  actuator frame before advancing. Results do not depend on host load or on
  `--time-scale`; `--determinism` checks that by running twice at different
  scales and comparing the final state.
- **The residual rate.** B-dot does not reach zero. It settles near twice the
  rate at which the field direction sweeps around the orbit — about 0.3 °/s
  here. That is why the hand-over threshold is 0.5 °/s and not lower.
- **The release margin.** The flight software releases detumble at 80 % of
  `POINTING_RATE_DPS`, because the gyro it judges by has bias and noise.
  Releasing at exactly 0.5 °/s left the true rate at 0.53.
- **Scaled time and timeouts.** The sensor-stream timeout (2 s) is measured on
  the flight clock, which `--time-scale` speeds up. A simulator that stalls for
  a few real milliseconds at 100× looks like a dead sensor, so the scenario
  drains telemetry without blocking. At high scales you will also see
  `SCHED_OVERRUN` events: the host, not the flight code, is late.
- **Not modelled.** The torquer's own field corrupting the magnetometer while
  it is on. Real missions switch the torquers off to measure.
