# Attitude determination and control

How HYPERSAT knows which way it is pointing, and how it holds the Earth in its
sights. The code is in [`fsw/apps/adcs/`](../fsw/apps/adcs/); the scenario that
proves it is [`sim/scenarios/nadir_pointing.py`](../sim/scenarios/nadir_pointing.py)
(`make pointing`).

## The chain, once per sensor sample (10 Hz)

```
 GPS fix ──────────┐   (or, during an outage, the on-board propagator)
                   ▼
            position, velocity ──▶ IGRF field, Sun direction    (inertial frame)
                                          │
 magnetometer ─┐                          ▼
 sun sensor  ──┼──────────────▶  TRIAD (start) ──▶ MEKF ──▶ attitude, gyro bias
 star tracker ─┤                                     ▲
 gyro ─────────┴─────────────────────────────────────┘
                                          │
                     mode logic ◀─────────┤
                        │                 ▼
      DETUMBLE: B-dot ──┤          POINTING: quaternion feedback on the wheels,
                        │                    momentum dumped by magnetorquers
                        ▼
              magnetorquer dipole, wheel motor torque
```

## Conventions

One quaternion convention throughout, in C++ and Python alike: `q = (w, x, y, z)`
rotates body-frame vectors into the inertial frame, and
`q_dot = ½ q ⊗ (0, ω_body)`. [`adcs_math.hpp`](../fsw/apps/adcs/adcs_math.hpp)
says why this matters.

## The multiplicative EKF, derived

The true attitude is written as the estimate followed by a small rotation
`δθ`, expressed in the body frame:

    q = q̂ ⊗ δq(δθ),      δq(δθ) ≈ (1, δθ/2)

**Error dynamics.** The truth evolves as `q_dot = ½ q ⊗ ω`, the estimate as
`q̂_dot = ½ q̂ ⊗ ω̂` with `ω̂ = gyro − b̂`. Differentiating the error quaternion
and keeping first-order terms gives

    δθ_dot = −ω̂ × δθ − δb − η_v
    δb_dot = η_u

where `δb = b − b̂` is the bias error, and `η_v`, `η_u` are the gyro's angle
random walk and bias random walk. That is the linear system the filter's
covariance obeys:

    F = [ −[ω̂×]  −I ]          Φ ≈ I + F Δt
        [   0      0 ]

**Vector measurements.** A sensor measures a direction `r` known in the
inertial frame. With `A(q)` the inertial→body rotation, `A(q) = (I − [δθ×]) A(q̂)`
to first order, so

    b = A(q) r = b̂ − δθ × b̂ = b̂ + [b̂×] δθ        ⇒   H = [ [b̂×]  0 ]

**Star tracker.** The tracker measures the whole attitude. The residual is the
rotation between estimate and measurement, `δθ_meas = 2·vec(q̂* ⊗ q_meas)`,
with `H = [ I  0 ]`.

**Reset.** After each update the estimated `δθ` is folded into the reference,
`q̂ ← q̂ ⊗ δq(δθ)`, and zeroed. This is what keeps the covariance 6×6 and
well-conditioned. A filter on the four quaternion components directly would
have a singular covariance, because the unit-norm constraint removes one
degree of freedom.

The covariance update uses the Joseph form, which stays symmetric and positive
definite where the short form `(I − KH)P` can drift.

## Why there is a star tracker

The roadmap first planned this phase around the magnetometer and the coarse
sun sensor alone. Flown against the simulator, that combination reached
0.1–0.3° in sunlight and drifted to 0.8° in eclipse, while the filter
reported 0.03° of uncertainty.

The filter's overconfidence was the clue. The coarse sun sensor's error is
**bias**: each face's reading is clipped at zero, which skews the combined
vector by a few tenths of a degree. Averaging more samples does not remove a
bias. On a real spacecraft, Earth albedo and face misalignment make it worse,
usually degrees. No tuning makes coarse sensors meet a 0.2° requirement.
Missions that need it carry a star tracker.

So the estimator uses the sensors the way flight systems do:

| Sensor | Used for |
|---|---|
| Star tracker | Fine attitude, at 2 Hz, whenever it has a solution |
| Gyro | Propagation between measurements, and coasting through tracker outages |
| Magnetometer | Always; on its own it constrains two axes |
| Coarse sun sensor | TRIAD at acquisition (the tracker cannot see stars above 1 °/s), and only after the tracker has been out for 5 minutes |

The tracker is blinded whenever the Sun comes within 30° of its boresight.
In the scenario that happens near orbit noon, and the gyro bridges the gap.

## Control

**Detumble.** Magnetometer-only B-dot; see [`bdot.hpp`](../fsw/apps/adcs/bdot.hpp).

**Nadir pointing.** The commanded body rate comes from a quaternion-feedback
attitude loop and is clamped to a maximum slew rate. An inner rate loop
follows it, and the gyroscopic coupling `ω × (Iω + h_w)` is cancelled. For
small errors this is exactly a PD controller with natural frequency
`POINT_BANDWIDTH_RADPS`. For large errors it slews at a bounded rate, so
acquiring from any attitude cannot saturate the wheels. See
[`pointing.hpp`](../fsw/apps/adcs/pointing.hpp).

**Momentum.** External torques (gravity gradient, uncompensated wheel
friction) accumulate as wheel momentum. The magnetorquers remove it with
`m = (k/|B|²) h_w × B`. Their torque is known, so the wheels are told to supply
only what remains, and dumping costs no pointing accuracy.

## What the scenario shows

From a 3 °/s tumble, two orbits, every number checked against simulator truth:

| | |
|---|---|
| Detumble and pointing engaged | ~960 s |
| Worst nadir error once settled | 0.11° (requirement 0.2°) |
| Worst nadir error in eclipse | 0.06° |
| Worst nadir error during a 15-minute GPS outage | 0.11° |
| Gyro bias learned to | ~3°/h, from up to 75°/h |
| Peak wheel momentum | under 1 mNms of 30 |

## What is flattering, and should not be mistaken for reality

- **The magnetic field model is perfect.** The flight software's IGRF is the
  same model the simulator flies through. Real fields differ from IGRF by
  100 nT or more.
- **Inertia is known to about 5%.** That is realistic, but nothing else about
  the mass properties is wrong.
- **Sensors are perfectly aligned** with the body axes. Real ones are
  calibrated in flight.
- **No disturbance beyond gravity gradient and wheel friction.** There is no
  drag, solar pressure or residual dipole.

Each of these is a later task, and each will cost some of the margin shown
above.
