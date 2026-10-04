# `sim/models/` — physics and hardware models

> 📚 **Learning this?** See [Lesson 14 — Sensors and noise](../../learn/14-sensors-and-noise/) in the lesson track.


**Built through Phase 3.** Everything in the table below exists except atmospheric density and drag. `igrf.py` and `igrf_coeffs.py` hold the field model.

Truth models. Nothing here is flight code, nothing here is under the flight
software's constraints, and nothing here may ever be linked into the flight
software.

## Planned contents

| Module | Models |
|---|---|
| `dynamics.py` | Rigid-body attitude: quaternion kinematics, Euler's equation, RK4 |
| `orbit.py` | Two-body with J2 |
| `environment.py` | IGRF magnetic field (and a dipole for comparison), solar vector, eclipse, gravity-gradient torque |
| `sensors.py` | Gyroscope, magnetometer, coarse sun sensors, star tracker, GPS — with the noise, bias, quantisation and blind spots that make estimation necessary |
| `power.py` | Solar arrays against sun angle and eclipse, a lithium-ion battery, loads per power rail, and a stuck-heater fault |
| `actuators.py` | Reaction wheels (torque and momentum limits, friction) and magnetorquers, with their real limits |

## The rule that matters

**Model the imperfections, not just the physics.** A gyroscope that returns the
true rate makes a complementary filter look brilliant and teaches nothing. The
bias random walk is what the filter exists to estimate; the quantisation is what
sets the noise floor; the sun sensor's blindness in eclipse is what forces the
mode logic to have an answer for losing a reference.

Every model states its units and its reference frame in its docstring. Frame
confusion — body versus ECI versus orbital, and which way a quaternion rotates —
is the single most common source of attitude control bugs, and the only defence
is being relentlessly explicit.
