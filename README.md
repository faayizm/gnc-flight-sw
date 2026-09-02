# HYPERSAT — satellite flight software and its ground segment

A complete, portable satellite flight software stack built from first
principles, together with the simulation and ground tooling needed to actually
fly it. Written to be read: every design decision that matters is explained in
the file where it is made, including the ones that were rejected and why.

**Status: Phase 1 of 7.** The telemetry, tracking and command chain works end
to end — real CCSDS Space Packets, real ECSS PUS services, a real ground
station. Attitude determination and control, power, mode management and fault
handling are scheduled and scaffolded, not yet implemented. The roadmap in
[docs/ROADMAP.md](docs/ROADMAP.md) says exactly what exists and what does not.

```
                 ┌──────────────────────────────────────┐
  OpenC3 COSMOS  │                                      │   telemetry screens,
  or pyground ──▶│   CCSDS Space Packets over TCP       │◀── limits, command
                 │   ECSS-E-ST-70-41C PUS services      │    scripting
                 └───────────────────┬──────────────────┘
                                     │
                 ┌───────────────────▼──────────────────┐
                 │  FLIGHT SOFTWARE  (C++17, no heap,   │
                 │  no exceptions, no OS dependency)    │
                 │                                      │
                 │  TT&C · ADCS · EPS · modes · FDIR    │
                 │  ─────────────────────────────────   │
                 │  software bus · rate-group scheduler │
                 │  parameters · events · time          │
                 │  ─────────────────────────────────   │
                 │  HAL ports: clock, link, storage,    │
                 │             watchdog                 │
                 └───────────────────┬──────────────────┘
                                     │
                 ┌───────────────────▼──────────────────┐
                 │  PLATFORM  (the only OS-aware code)  │
                 │  POSIX today · FreeRTOS/bare metal   │
                 │  is a directory, not a rewrite       │
                 └──────────────────────────────────────┘
```

## Try it in two minutes

```bash
make build          # configure, compile, run 72 unit tests
make run            # the spacecraft boots and waits for a ground station
```

Then, in a second terminal:

```bash
make demo           # a scripted pass: connection test, parameter changes,
                    # a deliberately illegal command, a corrupted packet
```

You will see verification reports, housekeeping, events and failure codes come
back — the real protocol, not a simulation of one.

```
=== connection test =========================================
  t=  6.044  apid=0x001 seq=  8  VERIF_ACCEPT_OK    tc(apid=0x00A, seq=0)
  t=  6.044  apid=0x001 seq=  9  TEST_REPORT
  t=  6.044  apid=0x001 seq= 10  VERIF_COMPLETE_OK  tc(apid=0x00A, seq=0)
  t=  6.525  apid=0x001 seq= 11  SYS_HK   uptime_s=6  tick_count=326  mode=BOOT
```

Other useful entry points:

```bash
make test           # unit tests (fast, no network, no spacecraft)
make sil            # software-in-the-loop: starts the real binary, talks to it
make gen            # regenerate everything from dictionary/mission.yaml
make params         # read every on-board parameter back from the spacecraft
make help           # everything else
```

## The one idea worth stealing

Every telemetry point, telecommand, event and parameter is declared **once**,
in [`dictionary/mission.yaml`](dictionary/mission.yaml). A generator projects
that single declaration into the C++ structures and serialisers, the OpenC3
COSMOS command and telemetry definitions, the Python ground client's tables,
and the interface control document.

Adding a telemetry field is a one-line diff, and the ground system knows about
it on the next `make gen`. Nothing can drift out of sync, because there is only
one place for it to drift from. This is the decision the rest of the repository
is built around, and it is the one most worth copying into your own work.

## What is actually built

| Area | State |
|---|---|
| CCSDS 133.0-B Space Packet Protocol | Working, unit tested against the standard's bit layout |
| PUS ST[01] request verification | Working — acceptance and completion, success and failure |
| PUS ST[03] housekeeping | Working — periodic reports, enable/disable per structure |
| PUS ST[05] event reporting | Working — severity-coded, with a bounded on-board history |
| PUS ST[17] connection test | Working |
| PUS ST[20] parameter management | Working — range-checked, CRC-protected, survives a restart |
| Rate-group scheduler | Working — deterministic, deadline-monitored, single-threaded |
| Software bus | Working — synchronous pub/sub, statically bounded |
| Parameter store | Working — range checks, CRC on non-volatile storage, safe fallback |
| Ground segment | Working — Python client and CLI; OpenC3 COSMOS config generated |
| ADCS, EPS, mode manager, FDIR | **Not yet.** Phases 2–6. Scaffolded and documented |
| Orbit and attitude simulator | **Not yet.** Phase 2 |
| TM/TC transfer frames, Reed-Solomon | **Not yet.** Phase 4 |

## Repository map

Every directory has a `README.md` explaining what belongs in it and why.
[docs/PATHS.md](docs/PATHS.md) is the complete annotated map.

| Path | What lives there |
|---|---|
| [`dictionary/`](dictionary/) | The single source of truth. Start reading here |
| [`tools/`](tools/) | The generator that projects the dictionary everywhere |
| [`fsw/core/`](fsw/core/) | Portable flight core: scheduler, bus, parameters, events, time |
| [`fsw/hal/`](fsw/hal/) | The ports. Four small interfaces that make this portable |
| [`fsw/platform/`](fsw/platform/) | The adapters. The only OS-aware code in the tree |
| [`fsw/apps/`](fsw/apps/) | The applications. TT&C today; ADCS, EPS, FDIR to come |
| [`fsw/generated/`](fsw/generated/) | Generated C++. Never edit by hand |
| [`gnd/pyground/`](gnd/pyground/) | Dependency-free Python ground station and test driver |
| [`gnd/openc3/`](gnd/openc3/) | OpenC3 COSMOS plugin: screens, limits, command definitions |
| [`sim/`](sim/) | The orbit and attitude simulator. Phase 2 |
| [`tests/unit/`](tests/unit/) | Fast, hermetic tests of the flight core |
| [`tests/sil/`](tests/sil/) | Software-in-the-loop: the real binary over the real protocol |
| [`docs/`](docs/) | Architecture, roadmap, and the generated interface control document |

## Design rules

These are enforced, not aspirational. `fsw_core` and `fsw_apps` are compiled
with `-fno-exceptions -fno-rtti` and the full warning set as errors, so a
violation fails the build rather than being caught in review.

- **No allocation after initialisation.** Every container is bounded at compile
  time. Worst-case memory is a property of the type, visible in the source.
- **No exceptions on the flight path.** Everything that can fail returns a
  status.
- **One thread.** Rate groups, fixed order, deterministic. The same inputs give
  the same outputs on every run and every machine.
- **The core never calls the OS.** It calls a port. Porting is writing a new
  directory under `platform/`, not editing the core.
- **Untrusted input is validated in one place,** in a fixed order: integrity,
  then structure, then meaning. A packet that fails its CRC is never
  interpreted at all.

## Reading order

1. [`dictionary/mission.yaml`](dictionary/mission.yaml) — what this spacecraft says and accepts
2. [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — why it is shaped this way
3. [`fsw/core/scheduler.hpp`](fsw/core/scheduler.hpp) — the heartbeat, and why it is not threaded
4. [`fsw/apps/ttc/pus.hpp`](fsw/apps/ttc/pus.hpp) — the standards, explained
5. [`fsw/main.cpp`](fsw/main.cpp) — the whole spacecraft, wired together in one file
6. [`docs/ICD.md`](docs/ICD.md) — the generated interface control document

## Standards

- **CCSDS 133.0-B** Space Packet Protocol
- **ECSS-E-ST-70-41C** Packet Utilisation Standard (PUS-C)
- **CCSDS CRC-16** packet error control (polynomial `0x1021`, seed `0xFFFF`)

## Requirements

CMake 3.20+, a C++17 compiler, Python 3.10+. `make gen` additionally needs
PyYAML; `make venv` will set that up. OpenC3 COSMOS is optional and runs in
Docker — the Python ground station needs none of it.

## Licence

Apache 2.0. See [LICENSE](LICENSE).
