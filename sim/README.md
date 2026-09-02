# `sim/` — the spacecraft simulator

> 📚 **Learning this?** See [Lessons 12–16 — orbits, attitude, sensors, estimation, control](../learn/) in the lesson track.


**Not yet implemented. Phase 2.** This directory holds the design and the
scaffolding; the code arrives with the ADCS work.

```
sim/
├── models/       physics: orbit, attitude dynamics, environment
├── sil/          the bridge: sensor packets out, actuator commands in
└── scenarios/    reproducible test cases, each a single file
```

## What it will be

A Python simulator holding the **truth**: where the spacecraft actually is,
which way it is actually pointing, what its sensors would actually read. It
connects to the flight software on a second TCP port — separate from the TT&C
link on 50001 — and exchanges sensor and actuator packets every cycle.

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
