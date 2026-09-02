# `fsw/core/` — the portable flight core

The machinery every application needs, written to run anywhere. No operating
system, no allocation after initialisation, no exceptions, no RTTI. This is the
code that would be cross-compiled for a flight processor unchanged.

| File | What it is | Why it is the way it is |
|---|---|---|
| `scheduler.hpp/.cpp` | Rate-group cooperative scheduler | The heartbeat. Single-threaded and deterministic, so a scenario replays identically. Monitors deadlines; deliberately does **not** run catch-up ticks |
| `bus.hpp` | Topic-based publish/subscribe | Applications never call each other. Dispatch is synchronous — no queue to overflow, and the causal chain is visible in a stack trace |
| `event_log.hpp` | On-board event reporting | A bounded history that survives loss of signal, plus an immediate downlink sink. Counts what it had to discard |
| `param_store.hpp/.cpp` | The on-board parameter table | Range-checked on every write, CRC-protected in storage, and falls back to compiled-in defaults rather than booting on a value it cannot vouch for |
| `bytes.hpp` | Big-endian serialisation | The only place in the flight software that knows about byte order. Poisons itself on overflow so callers check once, not per field |
| `crc.hpp` | CCSDS CRC-16 | Bitwise, not table-driven: 256 entries of ROM is real money on a flight processor |
| `time.hpp` | Monotonic and mission time | Two clocks, deliberately distinct types. Control loops use monotonic time so a ground time correlation can never make `dt` negative |
| `static_vector.hpp` | Fixed-capacity vector | Worst case visible in the type; overflow is an ordinary return value |
| `ring_buffer.hpp` | Fixed-capacity FIFO | Overwrites the oldest and counts the drops. Correct for telemetry; wrong for a command queue, and says so |
| `status.hpp` | Result and failure codes | `Status` internally, `FailureCode` for what the ground is told. The mapping between them is explicit |

## What may go here

Mechanism that every application needs and that has no opinion about
spacecraft subsystems. If it mentions attitude, power or a radio, it belongs in
`apps/`. If it needs an operating system, it belongs behind a port in `hal/`.

## What may not

- Anything that includes a system header
- Anything that allocates after initialisation
- Anything that throws
- Anything that knows what a Space Packet is — that is `apps/ttc/`

## Tested by

`tests/unit/test_scheduler.cpp`, `test_bus.cpp`, `test_param_store.cpp`,
`test_bytes.cpp`, `test_crc.cpp`, `test_containers.cpp`. The scheduler tests
drive a fake clock, so a test of an hour of spacecraft operation runs in
microseconds — which is the whole reason time sits behind a port.
