# Roadmap

Seven phases. Each ends with something you can watch happen, because a phase
that produces only internal machinery is a phase whose value cannot be checked.

**Currently at the end of Phase 1.**

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

## Phase 2 — The simulator and detumble

The first closed loop, and the first real GNC.

- Rigid-body attitude dynamics, quaternion state, RK4 integration
- Two-body orbit with J2
- Environment: IGRF magnetic field, solar vector, eclipse
- Sensor models with the imperfections that make estimation necessary:
  gyroscope bias random walk, magnetometer noise, sun sensors with a field of
  view and eclipse blindness, quantisation throughout
- Actuator models: magnetorquers with dipole limits, and the constraint that
  torque is always perpendicular to the local field
- The simulator bridge on its own TCP port
- **B-dot detumble control**
- ADCS application publishing `ADCS_HK`, which the existing TT&C chain
  downlinks with no change

**Ends with:** a spacecraft tumbling at 10 °/s, detumbled below 0.5 °/s, watched
live on a COSMOS graph.

## Phase 3 — Attitude determination and pointing

- TRIAD from the sun and magnetic field vectors
- A complementary filter estimating gyroscope bias
- A multiplicative extended Kalman filter, replacing it, with the derivation
  written down
- On-board orbit propagation from a Kepler model, checked against simulator
  truth
- Reaction wheel models with momentum limits and friction
- Quaternion feedback control, nadir pointing
- Wheel and magnetorquer allocation, momentum dumping
- Orekit introduced for high-fidelity orbit, eclipse geometry and ground
  station pass windows

**Ends with:** nadir pointing error below 0.2°, held through an eclipse.

## Phase 4 — A real communications link

Making the ground link resemble a radio rather than a socket.

- TM and TC transfer frames (CCSDS 132.0-B, 231.0-B)
- Attached sync marker, pseudo-randomisation, Reed-Solomon — proper framing
  recovery, retiring the limitation documented in `ARCHITECTURE.md`
- COP-1 command operation procedure
- PUS ST[09] time correlation, so timestamps become trustworthy and the time
  reference status field stops reading 0
- PUS ST[11] time-based command scheduling
- PUS ST[15] on-board storage and retrieval: record telemetry out of contact,
  play it back during a pass
- A link model with pass windows, propagation delay and a bit error rate

**Ends with:** commands time-tagged during one pass and executing out of
contact, with the results played back during the next.

## Phase 5 — Power and modes

The spacecraft starts making its own decisions.

- EPS model: solar array generation with sun angle and eclipse, battery
  charge and discharge, bus loads
- EPS application, `EPS_HK`, load shedding
- The mode manager: BOOT, SAFE, DETUMBLE, STANDBY, POINTING, with hysteresis
  on every threshold
- Autonomous transitions driven by body rates, reference validity and battery
  state
- Ground mode requests via ST[8,1] — which the mode manager may refuse

**Ends with:** an orbit simulated through eclipse cycles, the spacecraft
managing its own modes, and a deliberately drained battery driving it into
SAFE.

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
