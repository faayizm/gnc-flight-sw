# Roadmap

Seven phases. Each ends with something you can watch happen, because a phase
that produces only internal machinery is a phase whose value cannot be checked.

**Currently at the end of Phase 5.**

---

## Phase 0 — Foundations ✅ done

The keystone and the skeleton.

- `dictionary/mission.yaml` and the generator that projects it into flight
  code, ground configuration and the ICD
- Flight core: rate-group scheduler, software bus, parameter store, event log,
  time, bounded containers, big-endian serialisation, CCSDS CRC-16
- HAL ports: clock, link, storage, watchdog
- POSIX adapters, including simulation time scaling
- Build with `-fno-exceptions -fno-rtti` and warnings as errors
- 72 unit tests with a dependency-free framework

## Phase 1 — TT&C end to end ✅ done

Everything built afterwards is visible and commandable from the first minute,
which is why this came before any GNC.

- CCSDS 133.0-B Space Packet Protocol
- PUS ST[01] verification, ST[03] housekeeping, ST[05] events, ST[17] test,
  ST[20] parameters
- TCP link, the spacecraft as server
- `pyground`: Python ground station, CLI and library
- OpenC3 COSMOS plugin generated from the dictionary
- 35 software-in-the-loop checks against the real binary

**Try it:** `make build && make run`, then `make demo`.

---

## Phase 2 — The simulator and detumble ✅ done

The first closed loop, and the first real GNC.

Done:

- ✅ Rigid-body attitude dynamics, quaternion state, RK4 integration
- ✅ Two-body orbit with J2
- ✅ Environment: IGRF-14 magnetic field (degree 10, epoch 2025.0, coefficients
  extracted from the published table and checked against an independent
  implementation), with the tilted dipole kept as a cross-check
- ✅ Sensors: gyroscope with bias random walk, noise and quantisation;
  magnetometer with noise, quantisation and range limit
- ✅ Magnetorquer model with dipole limit and quantisation; torque is
  `m × B`, so always perpendicular to the field
- ✅ The simulator bridge on its own TCP port (50000), lockstep, CRC-protected
  (`fsw/apps/adcs/sim_bridge.hpp` is the specification)
- ✅ **B-dot detumble control**, magnetometer only, with engage/release
  hysteresis and a sensor-timeout fail-safe
- ✅ ADCS application publishing `ADCS_HK`, downlinked by the existing TT&C
  chain with no change there
- ✅ Scenario `sim/scenarios/detumble.py`, bit-for-bit deterministic

- ✅ COSMOS `DETUMBLE` screen (generated) and `make detumble-live` to drive it
- ✅ Model checks (`make test-sim`): conservation laws, IGRF reference values,
  divergence-free field

**Ends with:** a spacecraft tumbling at 10 °/s, detumbled below 0.5 °/s, watched
live on a COSMOS graph. `make detumble` asserts it; `make detumble-live` lets you
watch it. The COSMOS screen has been generated and checked against the
dictionary, but not opened in a running COSMOS installation.

## Phase 3 — Attitude determination and pointing ✅ done

How it works, the filter derivation and the results: [ATTITUDE.md](ATTITUDE.md).

- ✅ TRIAD from the magnetometer and sun sensor, used to start the filter
- ✅ A multiplicative extended Kalman filter estimating attitude and gyro
  bias, with the derivation written down
- ✅ A star tracker model and filter update. This was added to the plan: the
  coarse sensors cannot reach 0.2° (see ATTITUDE.md, "Why there is a star
  tracker")
- ✅ GPS receiver model and on-board orbit propagation (two-body plus J2, RK4)
  that carries the last fix through outages
- ✅ On-board ephemeris: Sun direction, IGRF (generated from the same
  coefficient table as the simulator), eclipse
- ✅ Reaction wheel models with torque and momentum limits, Coulomb and viscous
  friction, and imperfect friction compensation
- ✅ Gravity-gradient torque in the simulator
- ✅ Quaternion feedback control with a rate-limited slew, nadir pointing
- ✅ Wheel and magnetorquer allocation, magnetic momentum dumping
- ✅ ADCS control modes DETUMBLE → STANDBY → POINTING, with hysteresis
- ✅ Scenario `nadir_pointing.py`, COSMOS `POINTING` screen
- ⬜ *Skipped:* the complementary filter that was to precede the MEKF. The
  MEKF was built directly, and the complementary filter would have taught
  nothing it does not.
- ⬜ *Deferred:* Orekit. Its JVM is a large dependency for gains (IERS frames,
  pass windows) that nothing here needs yet. Ground-station passes arrive with
  Phase 4, and that is where it will be weighed again.

**Ends with:** nadir pointing error below 0.2°, held through an eclipse.
`make pointing` asserts it against simulator truth: 0.11° worst once settled,
0.06° worst in eclipse.

## Phase 4 — A real communications link ✅ done

Making the ground link resemble a radio rather than a socket. How it works:
[LINK.md](LINK.md).

- ✅ TM and TC transfer frames (CCSDS 132.0-B, 232.0-B), with virtual
  channels for live telemetry, playback and idle frames
- ✅ Attached sync marker, pseudo-randomisation, Reed-Solomon (255,223) on the
  downlink, and BCH-coded CLTUs on the uplink (131.0-B, 231.0-B). This gives
  proper framing recovery and retires the limitation documented in
  `ARCHITECTURE.md`. RS is checked against libfec.
- ✅ COP-1: FARM-1 on board, FOP-1 on the ground, CLCW in every TM frame
- ✅ PUS ST[09] time reports and correlation: the time reference status
  field reads 1 once the ground has corrected the clock
- ✅ PUS ST[11] time-based command scheduling
- ✅ PUS ST[15] on-board storage and retrieval: everything recorded, contact
  or not, and replayed by time range on its own virtual channel
- ✅ A link model with pass windows from orbit geometry, propagation delay
  and an elevation-dependent bit error rate
- ✅ A front-end processor (`make fep`), so COSMOS still sees plain packets
- ⬜ *Decided against:* Orekit, deferred here from Phase 3. Pass prediction
  needed only the simulator's own orbit model and a ground station
  position, and its accuracy (seconds on pass times) is far below anything
  this link can notice.

**Ends with:** commands time-tagged during one pass and executing out of
contact, with the results played back during the next. `make store-forward`
does exactly that, through a noisy channel, and asserts on every step.

## Phase 5 — Power and modes ✅ done

The spacecraft starts making its own decisions. How it works:
[POWER_AND_MODES.md](POWER_AND_MODES.md).

- ✅ EPS model: solar arrays (deployed zenith panel and body panels) with
  sun angle and eclipse, a two-cell lithium-ion battery with an
  open-circuit voltage curve and internal resistance, and loads per rail.
  Wheels and magnetorquers draw more when they work harder.
- ✅ EPS application: state of charge by counting, anchored to voltage;
  power states with hysteresis; `EPS_HK`; load shedding in five levels;
  rails switched by policy and by ground request (ST[8,3])
- ✅ The mode manager: BOOT, SAFE, DETUMBLE, STANDBY, POINTING, with
  hysteresis on every threshold
- ✅ Autonomous transitions driven by body rates, estimator state, position,
  battery state and ground contact
- ✅ Ground mode requests via ST[8,1], judged and refused with a reason
- ✅ An I/O application as the single hardware boundary: sensor data on the
  bus, actuator and power-switch commands back out in one reply
- ✅ Scenario `power_and_modes.py`, COSMOS `POWER` screen

**Ends with:** an orbit simulated through eclipse cycles, the spacecraft
managing its own modes, and a deliberately drained battery driving it into
SAFE. `make power` does this with a stuck heater: load shedding in order,
SAFE at CRITICAL, and recovery only when the ground asks.

## Phase 6 — Fault handling and radiation

Where flight software stops being an application and starts being flight
software.

- PUS ST[12] on-board monitoring: limit checks defined in the dictionary
- An FDIR recovery ladder: report, retry, reconfigure, safe mode
- Fault injection at the simulator bridge: frozen sensors, out-of-range values,
  dropped links, unresponsive actuators, corrupted packets
- Single-event upset injection into memory, with EDAC and scrubbing
- Redundancy and voting on critical state
- Watchdog recovery paths, exercised
- Monte Carlo campaigns in CI

**Ends with:** a scenario that injects a wheel failure mid-pointing and shows
the spacecraft detecting it, isolating it, and recovering — autonomously.

## Phase 7 — Off the laptop

Making the portability claim concrete.

- FreeRTOS platform adapter, the same flight core cross-compiled
- Running on QEMU: Cortex-M or LEON3, the SPARC processor used across European
  missions
- Real timing measurements, real memory footprint, real watchdog behaviour
- A hardware-in-the-loop path to a development board
- A memory and timing budget report generated from the binary

**Ends with:** the identical flight core, unchanged, running on an emulated
flight processor and flying the same scenarios.

---

## Deliberately not planned

- **A telemetry GUI.** COSMOS exists and is better than anything worth building
  here.
- **A general-purpose flight software framework.** cFS and F´ exist. This is
  one spacecraft, built to be understood.
- **Multi-threading.** See [ARCHITECTURE.md](ARCHITECTURE.md).
- **Flight qualification.** This is a learning and demonstration system. It is
  built to flight software *practices*, which is not the same as being
  flight-qualified, and nothing here has been through the verification a real
  mission requires.
