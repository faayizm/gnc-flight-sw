# `fsw/apps/ttc/` — telemetry, tracking and command

> 📚 **Learning this?** See [Lessons 5–8 — packets, services, verification, housekeeping](../../../learn/) in the lesson track.


The spacecraft's mouth and ears: everything between the radio and the PUS
services.

| File | Layer | Responsibility |
|---|---|---|
| `space_packet.hpp/.cpp` | CCSDS 133.0-B | The six-octet primary header. Bit packing, and the "length minus one" arithmetic, in exactly one place |
| `pus.hpp/.cpp` | ECSS-E-ST-70-41C | TM and TC secondary headers, telecommand validation, telemetry assembly with automatic length back-patching and CRC |
| `channel_coding.hpp/.cpp` | CCSDS 131.0-B, 231.0-B | Reed-Solomon (255,223) in dual basis, pseudo-randomiser, attached sync marker, BCH(63,56) codeblocks |
| `tm_framer.hpp/.cpp` | CCSDS 132.0-B | Packets into fixed-length TM frames across virtual channels, then RS, randomisation and ASM |
| `tc_receiver.hpp/.cpp` | CCSDS 231.0-B, 232.0-B, 232.1-B | CLTU decoding, TC frame checks, FARM-1 and the CLCW |
| `schedule.hpp` | PUS ST[11] | Time-tagged telecommands awaiting release |
| `packet_store.hpp` | PUS ST[15] | The circular store every packet is recorded into |
| `ttc_app.hpp/.cpp` | Application | Service dispatch, verification reports, housekeeping, events, time, scheduling, storage and playback |

## The services implemented

| Service | Subtypes | What it does |
|---|---|---|
| ST[01] verification | 1, 2, 7, 8 | Acceptance and completion, success and failure. Without this the ground is commanding blind |
| ST[03] housekeeping | 5, 6, 25 | Periodic parameter reports; enable and disable per structure |
| ST[05] events | 1–4 | Severity-coded event reports. The subtype *is* the severity, which lets a ground system filter on urgency knowing nothing about this mission |
| ST[08] functions | 1, 2, 3 | Mode requests (judged by the mode manager), counter reset, power rail requests (applied by EPS) |
| ST[09] time | 1, 2, 128 | Periodic CUC time reports; ADJUST_TIME (mission-specific) applies the ground's correlation and sets the time reference status to 1 |
| ST[11] scheduling | 1, 2, 3, 4 | Time-tagged telecommands: enable, disable, reset, insert (all-or-nothing) |
| ST[15] storage | 1, 2, 9, 11, 12, 13 | Record every packet; replay a time range on the playback channel; delete; summary |
| ST[17] test | 1, 2 | A connection test that changes no state — safe to send at any time, in any mode |
| ST[20] parameters | 1, 2, 3 | Read and write on-board parameters, range-checked |

ST[12] on-board monitoring arrives with fault management in Phase 6.

## Validation order, and why it is fixed

`parse_tc()` checks in this order, and the order is part of the design:

1. **Length** — is there even enough here to be a telecommand?
2. **Integrity** — CRC over the whole packet, before a single field is read.
3. **Structure** — declared length against actual, packet type, PUS version.
4. **Meaning** — is this a service and subtype we implement, with the right
   argument size?

A packet whose CRC fails is **never interpreted**. Its service and subtype
fields are not trustworthy, so it is not dispatched anywhere — only counted and
reported as an event. It is also the one rejection that produces no ST[01]
verification report, because the APID and sequence count such a report would
have to quote back are exactly the fields that cannot be trusted.

## The link below the packets

Packets never touch the socket directly. Downlink packets are cut into
Reed-Solomon-coded transfer frames; uplink bytes are decoded from CLTUs and
filtered by FARM-1 before any packet is read. [docs/LINK.md](../../../docs/LINK.md)
has the whole stack.

## Memory

Every buffer is a fixed-size member of `TtcApp`. The worst-case memory
footprint of the entire communications stack is `sizeof(TtcApp)`, known at
compile time.

## Tested by

`tests/unit/test_space_packet.cpp` and `test_pus.cpp` for the protocol layers —
including bit layouts checked against literal octets, so that a reader and
writer which are both wrong in the same way cannot pass. `test_link.cpp` for
the coding and frames (Reed-Solomon against libfec vectors), `test_storage.cpp`
for the store and schedule. `tests/sil/` exercises
the application against the real binary over a real socket.
