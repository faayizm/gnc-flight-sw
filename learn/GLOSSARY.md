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
| **ASM** | Attached Sync Marker. The four bytes `1A CF FC 1D` in front of every downlink frame, so a receiver can find where frames start. Lesson 5 |
| **BCH code** | The small error-correcting code on the uplink: it repairs one wrong bit in every eight bytes. Lesson 4 |
| **CCSDS** | Consultative Committee for Space Data Systems. The body where the world's space agencies agree how spacecraft talk. Lesson 5 |
| **CLCW** | Communications Link Control Word. Four bytes in every downlink frame telling the ground which command frame the spacecraft expects next. Lesson 7 |
| **CLTU** | Communications Link Transmission Unit. How a command frame is wrapped for the uplink radio, with error-correcting codeblocks. Lesson 7 |
| **COP-1** | Communications Operation Procedure. Guarantees commands arrive once each, in order, by numbering frames and resending lost ones. Lesson 7 |
| **CRC** | Cyclic Redundancy Check. A short number that reveals whether a message was damaged. Lesson 4 |
| **CUC** | CCSDS Unsegmented Code. The timestamp format: whole seconds plus a fraction in 1/65536ths. Lesson 6 |
| **Downlink** | Spacecraft → ground |
| **ECSS** | European Cooperation for Space Standardization. Publishes PUS |
| **Endianness** | Which end of a multi-byte number goes first. Space uses big endian, always. Lesson 3 |
| **Event** | A report sent when something *happens*, rather than on a timer. PUS ST[5], with the severity in the subtype. Lesson 8 |
| **FEC** | Forward Error Correction. Sending enough extra information that the receiver can repair damage itself, without asking again. Lesson 4 |
| **Ground segment** | Everything on Earth: antennas, mission control, the software |
| **ICD** | Interface Control Document. The formal list of every message. Ours is [`docs/ICD.md`](../docs/ICD.md), generated |
| **Parameter** | A setting the ground can change in flight, with a declared range, checked on board. PUS ST[20]. Lesson 9 |
| **Pass** | The few minutes a satellite is above the horizon and can be talked to. Lesson 12 |
| **Playback** | Sending down telemetry recorded on board while out of contact. PUS ST[15]. Lesson 8 |
| **PUS** | Packet Utilisation Standard. Says what a message *means*, where CCSDS says how to wrap it. Lesson 6 |
| **Reed–Solomon** | Error-correcting code that repairs damage rather than only detecting it. Any 16 damaged bytes in 255. Lesson 4 |
| **Scrambling** | Mixing a fixed pseudo-random pattern into every frame, so long runs of identical bits never reach the radio. Lesson 5 |
| **Space Packet** | The universal CCSDS message envelope: 6 bytes of header, then data. Lesson 5 |
| **ST[a,b]** | PUS service `a`, subtype `b`. `ST[17,1]` is a connection test everywhere. Lesson 6 |
| **Store and forward** | Load commands in one pass, let them run out of contact, collect the recorded results in the next. Lessons 6, 8 |
| **Telecommand / TC** | A message from the ground asking the spacecraft to do something |
| **Telemetry / TM** | A message from the spacecraft reporting on itself |
| **Time-tagged command** | A command stored on board to run at a given time. PUS ST[11]. Lesson 6 |
| **Transfer frame** | The fixed-size box Space Packets travel in over the radio, carrying sync and error correction. Lesson 5 |
| **Uplink** | Ground → spacecraft |
| **Verification report** | PUS ST[1]: the spacecraft saying a command was accepted or rejected, and then that it completed or failed, and why. Lesson 7 |

## Orbits

| Term | Plain meaning |
|---|---|
| **Apogee / Perigee** | The highest and lowest points of an orbit |
| **ECEF** | Earth-Centred Earth-Fixed. Origin at Earth's centre, axes rotating with the ground |
| **ECI** | Earth-Centred Inertial. Origin at Earth's centre, axes fixed to the stars. Lesson 12 |
| **Eclipse** | The part of each orbit spent in Earth's shadow. Roughly a third. Lesson 17 |
| **Elevation** | How high above the horizon the satellite appears from a ground station. Below about 5° it cannot be heard. Lesson 12 |
| **Escape velocity** | Fast enough to never come back. √2 times circular speed. Lesson 12 |
| **GEO** | Geostationary orbit, 35,786 km. One orbit takes exactly a day, so it hangs over one spot |
| **Inclination** | The tilt of the orbit plane relative to the equator |
| **J2** | The effect of Earth's equatorial bulge on an orbit. Used deliberately for sun-synchronous orbits. Lesson 12 |
| **LEO** | Low Earth Orbit, roughly 200–2000 km. Where this spacecraft lives |
| **LVLH** | Local Vertical, Local Horizontal. A frame based on nadir and the direction of travel |
| **Nadir** | Straight down, towards Earth's centre. The opposite of zenith |
| **RK4** | Fourth-order Runge–Kutta: a way of stepping an equation of motion forward that samples it four times per step. Halve the step and the error shrinks sixteen-fold. Lesson 12 |
| **SSO** | Sun-Synchronous Orbit. Crosses each latitude at the same local solar time daily. Lesson 12 |
| **TLE** | Two-Line Element set. The compact text format orbits are published in |
| **μ (mu)** | Gravitational parameter. For Earth, 3.986 × 10¹⁴ m³/s². Lesson 12 |

## Attitude and control

| Term | Plain meaning |
|---|---|
| **Attitude** | Which way the spacecraft is pointing. Not its altitude. Lesson 13 |
| **B-dot** | The rate of change of the magnetic field, and the detumble law built on it. Lesson 16 |
| **Boresight** | The direction an instrument or antenna looks along. On this spacecraft, body +Z. Lesson 18 |
| **Detumble** | Stopping the spin a spacecraft has after separation. Lesson 16 |
| **Euler angles** | Pitch, roll, yaw. Intuitive, and they suffer gimbal lock. Lesson 13 |
| **Gimbal lock** | An orientation where two rotation axes align and a degree of freedom is lost. Lesson 13 |
| **Intermediate axis theorem** | A body spun about its middle principal axis cannot keep spinning about it: the tennis racket flip. Lesson 13 |
| **Magnetorquer** | An electromagnet that pushes against Earth's magnetic field. No fuel, no moving parts. Lesson 16 |
| **MEKF** | Multiplicative Extended Kalman Filter. The standard attitude estimator. Lesson 15 |
| **Moment of inertia** | How hard something is to spin. Rotation's version of mass. A matrix, not a number |
| **Momentum dumping** | Using magnetorquers to slow a saturated reaction wheel. Lesson 16 |
| **Quaternion** | Four numbers describing a rotation without gimbal lock. Lesson 13 |
| **Reaction wheel** | A heavy motorised wheel; spin it one way, the spacecraft turns the other. Lesson 16 |
| **Saturation** | A reaction wheel at its maximum speed, unable to absorb more. Lesson 16 |
| **Star tracker** | Photographs the sky and matches it to a catalogue. The most accurate attitude sensor. Lesson 14 |
| **TRIAD** | Attitude from two measured direction vectors. Simple, no tuning, no history. Lesson 15 |

## Sensors and estimation

| Term | Plain meaning |
|---|---|
| **Allan variance** | How gyroscope error is specified: error against averaging time. Lesson 14 |
| **Bias** | A consistent offset. Averaging never removes it. Lesson 14 |
| **Complementary filter** | Blends a fast-but-drifting sensor with a slow-but-honest one. Lesson 15 |
| **Drift** | Error that grows with time, typically from integrating a bias. Lesson 14 |
| **Estimation** | Working out something you cannot measure directly, from things you can. Lesson 15 |
| **Gyroscope** | Measures rotation rate. Smooth and responsive, but its bias makes the integrated angle drift. Lesson 14 |
| **IGRF** | International Geomagnetic Reference Field. The standard model of Earth's magnetic field |
| **Kalman filter** | An estimator that tracks its own uncertainty and weights measurements by it. Lesson 15 |
| **Magnetometer** | Measures Earth's magnetic field, as a direction to compare against a model. Lesson 14 |
| **Observability** | Whether the measurements you have can determine the quantity you want. Lesson 15 |
| **Quantisation** | The step size of a digital sensor. A noise floor you cannot average below. Lesson 14 |
| **Random walk** | A quantity that drifts by small random steps. What a real gyro bias does. Lesson 14 |
| **Sensor fusion** | Combining several imperfect sensors into one better answer. Lesson 15 |
| **Sun sensor** | Measures the direction to the Sun. Blind in eclipse. Lesson 14 |
| **Validity flag** | A sensor's own statement that a reading can be believed. Necessary, and not sufficient: a frozen sensor still sets it. Lessons 14, 18 |

## Power and modes

| Term | Plain meaning |
|---|---|
| **Hysteresis** | Different thresholds for switching on and switching off, so a value hovering near one cannot flip the state back and forth. Lessons 16, 17 |
| **Load shedding** | Switching off loads in order of importance as the battery runs down. Lesson 17 |
| **Mode** | The one answer to "what is the spacecraft doing right now?": BOOT, SAFE, DETUMBLE, STANDBY or POINTING here. Decided in one place, the mode manager. Lesson 17 |
| **Rail** | One switched power line from the battery to a group of equipment: the payload, the transmitter, the heaters. Load shedding switches rails off. Lesson 17 |
| **Safe mode** | The mode a spacecraft enters when it does not know what else to do: minimum power, stay calm, keep listening. Entering is automatic; leaving needs a ground command. Lessons 17, 18 |
| **State of charge** | How full the battery is, as a percentage. `batt_soc_pct` in `EPS_HK`. Lesson 17 |

## Software and reliability

| Term | Plain meaning |
|---|---|
| **Big endian** | Most significant byte first. What everything on a spacecraft link uses. Lesson 3 |
| **Cortex-M** | Arm's family of small processors for embedded systems. The board in Phase 7 is a Cortex-M7, chosen because its floating-point unit does `double` in hardware. Lesson 11 |
| **Cross-compiler** | A compiler that runs on one kind of computer and produces programs for another: `arm-none-eabi-g++` runs on a laptop and writes code for a Cortex-M. Lesson 11 |
| **Cyclic executive** | A scheduler with a fixed, precomputed table of what runs in every slot. More rigid than rate groups, and even easier to analyse. Lesson 10 |
| **Delayed ACK** | TCP waiting up to 40 ms before acknowledging data, hoping to combine the acknowledgement with a reply. Together with Nagle's algorithm, a classic source of mysterious 40 ms stalls. Lesson 11 |
| **Determinism** | The same inputs always giving the same outputs, at any speed, on any computer. What makes a bug seen once reproducible. Lesson 10 |
| **EDAC** | Error Detection And Correction. Extra bits stored with every memory word, so a flipped bit can be found and repaired, and two flips at least detected. Lesson 18 |
| **ELF file** | The standard format of a compiled program: machine code, data, where each part goes in memory, and debug information. `build-target/fsw.elf` is the board's. Lesson 11 |
| **Event-action** | PUS ST[19]: when a named event is raised on board, run a stored telecommand. Keeps detecting a fault separate from responding to it. Lesson 18 |
| **Fault injection** | Deliberately breaking things to prove the recovery code works. Lesson 18 |
| **FMEA** | Failure Modes and Effects Analysis: every component, every way it can fail, and what detects and handles each. Lesson 18 |
| **FreeRTOS** | A small, widely used real-time operating system. Here it does only three things: start the flight task, keep time, and let it sleep between ticks. Lesson 11 |
| **Fused multiply-add** | One instruction that computes `a * b + c` with a single rounding instead of two. Why two computers can disagree in the last bit. Lesson 11 |
| **HAL** | Hardware Abstraction Layer. The interfaces that make code portable. Lesson 11 |
| **Heap** | Memory handed out while a program runs (`malloc`, `new`). Flight code here never uses one, so its whole memory footprint is known before it runs, and on the board the linker refuses anything that would. Lesson 11 |
| **HIL** | Hardware-in-the-Loop. Real flight software on real hardware against a simulator. Lesson 11, and `docs/HIL.md` |
| **Housekeeping** | Routine periodic health telemetry. Lesson 8 |
| **Interrupt** | A hardware signal that makes the processor stop what it is doing and run a handler at once: a byte arrived on a UART, a timer ticked. Lesson 11 |
| **Isolation (of a fault)** | Working out *which* part has failed, and then stopping using it so the fault cannot spread. The "I" in FDIR. Lesson 18 |
| **Latch-up (SEL)** | A radiation-induced short circuit. Power-cycle fast or the part burns. Lesson 18 |
| **Linker script** | The file that tells the linker where everything goes in the board's memory: code here, data there, the stack at the end. `targets/mps2-an500/mps2_an500.ld`. Lesson 11 |
| **Lockstep** | The simulator and the flight software taking turns: one sensor sample, one answer, then the next, as fast as the computer allows. Lesson 10 |
| **Monte Carlo** | Flying the same scenario many times with every uncertain quantity drawn at random, to find the cases nobody wrote down. Lesson 18 |
| **Nagle's algorithm** | TCP holding back small writes while earlier data is unacknowledged, to avoid flooding a network with tiny packets. Switched off with a "no delay" option. Lesson 11 |
| **NaN** | "Not a number": what 0 ÷ 0 gives in floating point. Every comparison with it is false, so a range check lets it through unless it tests for it. Lesson 9 |
| **newlib** | The C library used on small embedded systems, with the maths functions. Its `sin` and `cos` differ from a laptop's in the last bits. Lesson 11 |
| **NMI** | Non-Maskable Interrupt: one the software cannot switch off. On the board, the watchdog's last warning before it resets. Lesson 11 |
| **Non-volatile memory** | Storage that keeps its contents with the power off: the parameter table, the boot count. Lessons 2, 9 |
| **On-board monitoring** | PUS ST[12]: the spacecraft checks its own telemetry against limits, all the time, and raises an event when one is broken for long enough. Lesson 18 |
| **Overrun** | A task that took longer than its allotted time slot. Lesson 10 |
| **Port** | An interface describing what the software *needs*, not what the hardware *is*. Lesson 11 |
| **Priority inversion** | A high-priority task stuck waiting on a low-priority one, which keeps being pre-empted by a medium one. Mars Pathfinder, 1997. Lesson 10 |
| **QEMU** | An emulator: a program that pretends to be another computer, instruction by instruction. Here it plays a Cortex-M7 board. With `-icount` it counts instructions to keep time, which makes timing deterministic. Lesson 11 |
| **Rate group** | A set of tasks running at one frequency in a deterministic scheduler. Lesson 10 |
| **Reset cause** | Why the computer last started: power-on, or a watchdog reset. Saved in non-volatile memory and downlinked, with a boot count. Lesson 18 |
| **RTOS** | Real-Time Operating System: a small operating system whose job is doing things on time rather than doing many things at once. Lesson 11 |
| **SAA** | South Atlantic Anomaly: where the inner radiation belt dips lowest, and most radiation upsets in low orbit happen. Lesson 18 |
| **Scrubbing** | Continuously reading and rewriting memory so single bit flips are fixed before they accumulate. Lesson 18 |
| **Semihosting** | A way for a program on a board to ask an attached debugger, or an emulator, to do something for it: here, hand over the command line. Lesson 11 |
| **SEU** | Single-Event Upset. A cosmic ray flips a bit. The hardware is fine; the data is wrong. Lesson 18 |
| **SIL** | Software-in-the-Loop. Real flight software against a simulator, no hardware |
| **Single point of failure** | A component whose failure ends the mission. Lesson 18 |
| **Software bus** | How applications on board pass messages without knowing about each other: publish to a topic, and whoever subscribed receives it. Lessons 10, 17 |
| **Syndrome** | What an error-correcting code computes from a damaged word. In a Hamming code it is the position of the flipped bit. Lesson 18 |
| **SysTick** | The timer built into every Cortex-M processor. FreeRTOS counts its ticks with it. Lesson 11 |
| **TID** | Total Ionising Dose. Cumulative radiation damage over a mission's life. Lesson 18 |
| **TMR** | Triple Modular Redundancy: keep three copies of a critical value and believe any two that agree. Lesson 18 |
| **Two's complement** | How computers store negative whole numbers: −1 is every bit set. Lesson 3 |
| **UART** | The simplest serial port: bytes one bit at a time down a wire, at an agreed rate (115,200 bits a second is common). The board talks to the ground and to the simulator through two of them. Lesson 11 |
| **Watchdog** | A timer that resets the processor unless the software keeps telling it not to. Lessons 10, 11 and 18 |
| **WCET** | Worst-Case Execution Time: the longest a task can take. What a timing budget is built on, never the average. Lesson 10 |

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
