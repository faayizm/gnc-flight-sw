# `fsw/apps/ttc/` — telemetry, tracking and command

The spacecraft's mouth and ears, and the only application that currently works
end to end.

| File | Layer | Responsibility |
|---|---|---|
| `space_packet.hpp/.cpp` | CCSDS 133.0-B | The six-octet primary header. Bit packing, and the "length minus one" arithmetic, in exactly one place |
| `pus.hpp/.cpp` | ECSS-E-ST-70-41C | TM and TC secondary headers, telecommand validation, telemetry assembly with automatic length back-patching and CRC |
| `ttc_app.hpp/.cpp` | Application | Reassembly, service dispatch, verification reports, periodic housekeeping, event downlink |

## The services implemented

| Service | Subtypes | What it does |
|---|---|---|
| ST[01] verification | 1, 2, 7, 8 | Acceptance and completion, success and failure. Without this the ground is commanding blind |
| ST[03] housekeeping | 5, 6, 25 | Periodic parameter reports; enable and disable per structure |
| ST[05] events | 1–4 | Severity-coded event reports. The subtype *is* the severity, which lets a ground system filter on urgency knowing nothing about this mission |
| ST[17] test | 1, 2 | A connection test that changes no state — safe to send at any time, in any mode |
| ST[20] parameters | 1, 2, 3 | Read and write on-board parameters, range-checked |

Phase 4 adds ST[11] time-based scheduling, ST[12] on-board monitoring, ST[15]
storage and retrieval, and ST[09] time correlation.

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

## Framing over TCP, honestly

Packet boundaries come from the CCSDS length field itself. That works only
while the byte stream stays in sync, and a corrupted length field would
desynchronise it; the recovery — discard one octet and retry — is a limited
mitigation, not a solution.

A real RF link does not rely on this. TM/TC transfer frames carry an attached
sync marker precisely so a receiver can regain framing after a burst of noise,
along with pseudo-randomisation and Reed-Solomon coding. That is Phase 4, and
until then this limitation is stated rather than hidden.

## Memory

Every buffer is a fixed-size member of `TtcApp`. The worst-case memory
footprint of the entire communications stack is `sizeof(TtcApp)`, known at
compile time.

## Tested by

`tests/unit/test_space_packet.cpp` and `test_pus.cpp` for the protocol layers —
including bit layouts checked against literal octets, so that a reader and
writer which are both wrong in the same way cannot pass. `tests/sil/` exercises
the application against the real binary over a real socket.
