# Path map

Every directory and every significant file, and what it is for. Each directory
also has its own `README.md` with the detail.

```
gnc_flight_sw/
├── README.md                    Start here
├── LICENSE                      Apache 2.0
├── Makefile                     Every workflow: build, test, sil, gen, run, demo
├── CMakeLists.txt               Four targets; the separation between them is the architecture
├── requirements.txt             Python dependencies (PyYAML, for the generator)
│
├── dictionary/                  ── THE SINGLE SOURCE OF TRUTH ──
│   ├── README.md
│   └── mission.yaml             Every telemetry point, command, event, parameter
│
├── tools/
│   ├── README.md
│   ├── gen.py                   Projects the dictionary into code, config and the ICD
│   └── check_links.py           Verifies every relative link in the documentation
│
├── fsw/                         ── FLIGHT SOFTWARE ──
│   ├── README.md                The dependency rule and the enforced constraints
│   ├── main.cpp                 Composition root: the whole spacecraft in one file
│   │
│   ├── core/                    Portable flight core. No OS, no heap, no exceptions
│   │   ├── README.md
│   │   ├── scheduler.hpp/.cpp   Rate-group scheduler. The heartbeat
│   │   ├── bus.hpp              Synchronous publish/subscribe
│   │   ├── event_log.hpp        Bounded event history + immediate downlink sink
│   │   ├── param_store.hpp/.cpp Range-checked, CRC-protected parameter table
│   │   ├── bytes.hpp            Big-endian serialisation. The only byte-order code
│   │   ├── crc.hpp              CCSDS CRC-16
│   │   ├── time.hpp             Monotonic and mission time, as distinct types
│   │   ├── static_vector.hpp    Fixed-capacity vector
│   │   ├── ring_buffer.hpp      Fixed-capacity FIFO with a stated overflow policy
│   │   └── status.hpp           Status and FailureCode, and the mapping between them
│   │
│   ├── hal/                     The ports. The portability seam
│   │   ├── README.md
│   │   ├── clock.hpp            IClock: monotonic time, mission time, sleep
│   │   ├── link.hpp             ILink: non-blocking bidirectional byte pipe
│   │   ├── storage.hpp          IStorage: non-volatile blocks
│   │   └── watchdog.hpp         IWatchdog: the last line of defence
│   │
│   ├── platform/                The adapters. The ONLY OS-aware code
│   │   ├── README.md
│   │   └── posix/
│   │       ├── README.md
│   │       ├── posix_clock.*         CLOCK_MONOTONIC + simulation time scaling
│   │       ├── tcp_server_link.*     Non-blocking TCP; the spacecraft listens
│   │       ├── posix_watchdog.*      Measures what a real watchdog would have done
│   │       └── posix_file_storage.*  A file of fixed blocks
│   │
│   ├── apps/                    The applications
│   │   ├── README.md
│   │   ├── ttc/                 Telemetry, tracking and command  ── WORKING
│   │   │   ├── README.md
│   │   │   ├── space_packet.*   CCSDS 133.0-B primary header
│   │   │   ├── pus.*            ECSS PUS headers, TC validation, TM assembly
│   │   │   └── ttc_app.*        Reassembly, dispatch, verification, housekeeping
│   │   └── modemgr/             Mode arbitration  ── Phase 5, design documented
│   │       └── README.md
│   │
│   └── generated/               ── GENERATED. NEVER EDIT ──
│       ├── README.md
│       ├── dictionary.hpp       Ids, enums, event and parameter tables
│       ├── telemetry.hpp        Packed HK structures with serialisers
│       └── commands.hpp         Telecommand argument structures with parsers
│
├── gnd/                         ── GROUND SEGMENT ──
│   ├── README.md                Why there are two, and when to use which
│   ├── pyground/                Dependency-free Python ground station
│   │   ├── README.md
│   │   ├── packets.py           Independent CCSDS/PUS implementation
│   │   ├── client.py            GroundClient: connect, send, decode
│   │   ├── __main__.py          CLI: monitor, send, params, commands, demo
│   │   └── dictionary.py        GENERATED
│   └── openc3/                  OpenC3 COSMOS plugin. Configuration only
│       ├── README.md
│       ├── INSTALL.md           How to bring COSMOS up against this spacecraft
│       ├── plugin.txt           GENERATED
│       └── targets/SAT/         GENERATED cmd/tlm definitions and screens
│
├── sim/                         ── SIMULATOR ── Phase 2
│   ├── README.md                Design, and why the FSW never sees the truth
│   ├── models/                  Orbit, attitude, environment, sensors, actuators
│   ├── sil/                     The bridge on its own TCP port
│   └── scenarios/               Reproducible, seeded test cases
│
├── tests/
│   ├── README.md
│   ├── framework.hpp            ~100-line dependency-free C++ test framework
│   ├── CMakeLists.txt
│   ├── unit/                    72 fast hermetic tests
│   │   ├── README.md
│   │   └── test_*.cpp
│   └── sil/                     35 checks against the real binary
│       ├── README.md
│       └── test_endtoend.py
│
├── learn/                       ── THE 18-LESSON COURSE ──
│   ├── README.md                Curriculum map and the three difficulty tracks
│   ├── GLOSSARY.md              Every acronym in the repository, plain language
│   ├── toolbox/                 Standalone teaching programs, Python only
│   │   ├── byte_order.py        Big vs little endian, and the disaster between
│   │   ├── crc_playground.py    Damage a message, watch the checksum catch it
│   │   ├── packet_explorer.py   A real packet, byte by byte (--live for a fresh one)
│   │   ├── orbit_sandbox.py     Orbital speeds, and an orbit from Newton's law alone
│   │   └── spin_sandbox.py      Euler's equation and B-dot detumble
│   ├── 01-what-is-a-satellite/  ─┐
│   ├── 02-first-contact/         │
│   ├── 03-bytes-and-numbers/     │
│   ├── 04-checksums/             ├─ Part 1: talking to a spacecraft
│   ├── 05-ccsds-packets/         │  (no programming needed)
│   ├── 06-pus-services/          │
│   ├── 07-did-it-work/           │
│   ├── 08-housekeeping-and-events/
│   ├── 09-parameters/           ─┘
│   ├── 10-the-heartbeat/        ─┬─ Part 2: inside the flight software
│   ├── 11-portability/          ─┘
│   ├── 12-orbits/               ─┐
│   ├── 13-attitude/              │
│   ├── 14-sensors-and-noise/     ├─ Part 3: physics and control
│   ├── 15-estimation/            │
│   ├── 16-control/               │
│   ├── 17-power-and-modes/       │
│   └── 18-when-things-break/    ─┘
│
├── docs/
│   ├── README.md
│   ├── ARCHITECTURE.md          Decisions, and the alternatives rejected
│   ├── ROADMAP.md               Seven phases; what exists and what does not
│   ├── PATHS.md                 This file
│   └── ICD.md                   GENERATED interface control document
│
└── .github/workflows/ci.yml     Generate, build, unit, SIL, layering — on every push
```

## Generated paths

Never edit these; they are overwritten by `make gen`.

| Path | From |
|---|---|
| `fsw/generated/*.hpp` | `dictionary/mission.yaml` |
| `gnd/pyground/dictionary.py` | `dictionary/mission.yaml` |
| `gnd/openc3/plugin.txt` | `dictionary/mission.yaml` |
| `gnd/openc3/targets/SAT/**` | `dictionary/mission.yaml` |
| `docs/ICD.md` | `dictionary/mission.yaml` |

CI regenerates and fails if any of them differ from what is committed.

## Paths that are not in git

| Path | What |
|---|---|
| `build/` | CMake output |
| `.venv/` | Python virtual environment for the generator |
| `*_nvm.bin` | Non-volatile storage files written by a running spacecraft |
