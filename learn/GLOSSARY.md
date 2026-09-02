# Glossary

Space engineering is drowning in acronyms, and they are a real barrier to
learning. Everything used anywhere in this repository is here, in plain
language.

Lesson numbers point to where each idea is taught properly.

---

## Spacecraft parts

| Term | Plain meaning |
|---|---|
| **ADCS** | Attitude Determination and Control System. Works out which way the spacecraft points, and turns it. Lessons 13–16 |
| **AOCS** | Attitude and Orbit Control System. The same thing, plus orbit control. European usage |
| **Bus** | The spacecraft itself, minus the payload — structure, power, computer, radio. Confusingly also means a data bus |
| **EPS** | Electrical Power System. Solar panels, battery, and deciding what to switch off. Lesson 17 |
| **FDIR** | Fault Detection, Isolation and Recovery. What happens when something breaks. Lesson 18 |
| **GNC** | Guidance, Navigation and Control. Where to go, where you are, how to get there |
| **OBC** | On-Board Computer. The flight computer |
| **Payload** | The reason the mission exists — camera, instrument, transponder |
| **TT&C** | Telemetry, Tracking and Command. The radio and everything behind it. Lessons 2, 5–9 |

## Talking to the ground

| Term | Plain meaning |
|---|---|
| **APID** | Application Process Identifier. 11 bits saying which part of the spacecraft a packet came from. Lesson 5 |
| **CCSDS** | Consultative Committee for Space Data Systems. The body where the world's space agencies agree how spacecraft talk. Lesson 5 |
| **CRC** | Cyclic Redundancy Check. A short number that reveals whether a message was damaged. Lesson 4 |
| **CUC** | CCSDS Unsegmented Code. The timestamp format: whole seconds plus a fraction in 1/65536ths. Lesson 6 |
| **Downlink** | Spacecraft → ground |
| **ECSS** | European Cooperation for Space Standardization. Publishes PUS |
| **Endianness** | Which end of a multi-byte number goes first. Space uses big endian, always. Lesson 3 |
| **Ground segment** | Everything on Earth: antennas, mission control, the software |
| **ICD** | Interface Control Document. The formal list of every message. Ours is [`docs/ICD.md`](../docs/ICD.md), generated |
| **Pass** | The few minutes a satellite is above the horizon and can be talked to |
| **PUS** | Packet Utilisation Standard. Says what a message *means*, where CCSDS says how to wrap it. Lesson 6 |
| **Reed–Solomon** | Error-correcting code that repairs damage rather than only detecting it. Phase 4 |
| **Space Packet** | The universal CCSDS message envelope: 6 bytes of header, then data. Lesson 5 |
| **ST[a,b]** | PUS service `a`, subtype `b`. `ST[17,1]` is a connection test everywhere. Lesson 6 |
| **Telecommand / TC** | A message from the ground asking the spacecraft to do something |
| **Telemetry / TM** | A message from the spacecraft reporting on itself |
| **Transfer frame** | The layer beneath Space Packets on a real radio link, carrying sync and error correction. Phase 4 |
| **Uplink** | Ground → spacecraft |

## Orbits

| Term | Plain meaning |
|---|---|
| **Apogee / Perigee** | The highest and lowest points of an orbit |
| **ECEF** | Earth-Centred Earth-Fixed. Origin at Earth's centre, axes rotating with the ground |
| **ECI** | Earth-Centred Inertial. Origin at Earth's centre, axes fixed to the stars. Lesson 12 |
| **Eclipse** | The part of each orbit spent in Earth's shadow. Roughly a third. Lesson 17 |
| **Escape velocity** | Fast enough to never come back. √2 times circular speed. Lesson 12 |
| **GEO** | Geostationary orbit, 35,786 km. One orbit takes exactly a day, so it hangs over one spot |
| **Inclination** | The tilt of the orbit plane relative to the equator |
| **J2** | The effect of Earth's equatorial bulge on an orbit. Used deliberately for sun-synchronous orbits. Lesson 12 |
| **LEO** | Low Earth Orbit, roughly 200–2000 km. Where this spacecraft lives |
| **LVLH** | Local Vertical, Local Horizontal. A frame based on nadir and the direction of travel |
| **Nadir** | Straight down, towards Earth's centre. The opposite of zenith |
| **SSO** | Sun-Synchronous Orbit. Crosses each latitude at the same local solar time daily. Lesson 12 |
| **TLE** | Two-Line Element set. The compact text format orbits are published in |
| **μ (mu)** | Gravitational parameter. For Earth, 3.986 × 10¹⁴ m³/s². Lesson 12 |

## Attitude and control

| Term | Plain meaning |
|---|---|
| **Attitude** | Which way the spacecraft is pointing. Not its altitude. Lesson 13 |
| **B-dot** | The rate of change of the magnetic field, and the detumble law built on it. Lesson 16 |
| **Detumble** | Stopping the spin a spacecraft has after separation. Lesson 16 |
| **Euler angles** | Pitch, roll, yaw. Intuitive, and they suffer gimbal lock. Lesson 13 |
| **Gimbal lock** | An orientation where two rotation axes align and a degree of freedom is lost. Lesson 13 |
| **Magnetorquer** | An electromagnet that pushes against Earth's magnetic field. No fuel, no moving parts. Lesson 16 |
| **MEKF** | Multiplicative Extended Kalman Filter. The standard attitude estimator. Lesson 15 |
| **Moment of inertia** | How hard something is to spin. Rotation's version of mass. A matrix, not a number |
| **Momentum dumping** | Using magnetorquers to slow a saturated reaction wheel. Lesson 16 |
| **Quaternion** | Four numbers describing a rotation without gimbal lock. Lesson 13 |
| **Reaction wheel** | A heavy motorised wheel; spin it one way, the spacecraft turns the other. Lesson 16 |
| **Saturation** | A reaction wheel at its maximum speed, unable to absorb more. Lesson 16 |
| **Star tracker** | Photographs the sky and matches it to a catalogue. The most accurate attitude sensor |
| **TRIAD** | Attitude from two measured direction vectors. Simple, no tuning, no history. Lesson 15 |

## Sensors and estimation

| Term | Plain meaning |
|---|---|
| **Allan variance** | How gyroscope error is specified: error against averaging time. Lesson 14 |
| **Bias** | A consistent offset. Averaging never removes it. Lesson 14 |
| **Complementary filter** | Blends a fast-but-drifting sensor with a slow-but-honest one. Lesson 15 |
| **Drift** | Error that grows with time, typically from integrating a bias. Lesson 14 |
| **Estimation** | Working out something you cannot measure directly, from things you can. Lesson 15 |
| **IGRF** | International Geomagnetic Reference Field. The standard model of Earth's magnetic field |
| **Kalman filter** | An estimator that tracks its own uncertainty and weights measurements by it. Lesson 15 |
| **Observability** | Whether the measurements you have can determine the quantity you want. Lesson 15 |
| **Quantisation** | The step size of a digital sensor. A noise floor you cannot average below. Lesson 14 |
| **Random walk** | A quantity that drifts by small random steps. What a real gyro bias does. Lesson 14 |
| **Sensor fusion** | Combining several imperfect sensors into one better answer. Lesson 15 |

## Software and reliability

| Term | Plain meaning |
|---|---|
| **Big endian** | Most significant byte first. What everything on a spacecraft link uses. Lesson 3 |
| **EDAC** | Error Detection And Correction. Extra bits so a flipped bit can be repaired. Lesson 18 |
| **Fault injection** | Deliberately breaking things to prove the recovery code works. Lesson 18 |
| **HAL** | Hardware Abstraction Layer. The interfaces that make code portable. Lesson 11 |
| **Housekeeping** | Routine periodic health telemetry. Lesson 8 |
| **Hysteresis** | Different thresholds for entering and leaving a state, so it cannot flap. Lesson 17 |
| **Latch-up (SEL)** | A radiation-induced short circuit. Power-cycle fast or the part burns. Lesson 18 |
| **Overrun** | A task that took longer than its allotted time slot. Lesson 10 |
| **Port** | An interface describing what the software *needs*, not what the hardware *is*. Lesson 11 |
| **Rate group** | A set of tasks running at one frequency in a deterministic scheduler. Lesson 10 |
| **Safe mode** | Minimal survival state. Entered autonomously; left only by ground command. Lesson 17 |
| **Scrubbing** | Continuously reading and rewriting memory so single bit flips are fixed before they accumulate. Lesson 18 |
| **SEU** | Single-Event Upset. A cosmic ray flips a bit. The hardware is fine; the data is wrong. Lesson 18 |
| **SIL** | Software-in-the-Loop. Real flight software against a simulator, no hardware |
| **HIL** | Hardware-in-the-Loop. Real flight software on real hardware against a simulator |
| **TID** | Total Ionising Dose. Cumulative radiation damage over a mission's life. Lesson 18 |
| **Watchdog** | A timer that resets the processor unless the software keeps telling it not to. Lesson 10 |

## Units you will meet

| Symbol | Meaning |
|---|---|
| **°/s** or **dps** | Degrees per second — a rotation rate |
| **rad/s** | Radians per second. 1 rad/s ≈ 57.3 °/s |
| **N·m** | Newton metre — torque, a twisting force |
| **A·m²** | Ampere metre squared — magnetic dipole moment, the "strength" of a magnetorquer |
| **T** | Tesla — magnetic field. Earth's field in orbit is about 30 μT |
| **W** | Watt — power |
| **SoC** | State of Charge, as a percentage of a full battery |

---

Missing something? A term that is not here is a bug in this glossary — please
open an issue.
