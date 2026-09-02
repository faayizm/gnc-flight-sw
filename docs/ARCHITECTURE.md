# Architecture

Why this system is shaped the way it is, including the alternatives that were
rejected and the reasons.

## The problem

A satellite's flight software has to do several things that pull against each
other. It must be **deterministic**, because a bug seen once in orbit has to be
reproducible on the ground. It must be **analysable**, because worst-case
timing and memory have to be provable before launch, not measured after. It
must be **portable**, because the processor is chosen late and changes. And it
must be **honest about failure**, because nobody can go and look.

Almost every decision below follows from one of those four.

## Layering

```
   main.cpp                          knows everything, is known by nothing
      │
      ├──────────────┐
      ▼              ▼
   apps/   ──────▶ core/ ──────▶ hal/ ◀────── platform/
   what a          mechanism    the ports     the adapters
   spacecraft      with no                    the only OS-aware
   is              opinions                   code in the tree
```

The rule: `core/` and `apps/` may include `hal/`; they may never include
`platform/`. No system header appears outside `platform/`. `make
check-layering` enforces this in CI, because a layering rule that lives only in
a README is a rule that will be broken.

The payoff is a concrete claim: porting to FreeRTOS or bare metal means adding
a directory under `platform/` and writing a new `main.cpp`. Not one line of
`core/` or `apps/` changes.

## Decision: one thread, rate groups

**Chosen.** A single thread. A base tick at 50 Hz. Each task declares a divider
(run every N ticks) and an offset (which of those N). Tasks run in registration
order.

**Rejected: preemptive threads with priorities.** It is the conventional
answer, and it buys races, priority inversion, and an execution order that
differs between two runs of the same scenario. On a spacecraft that last point
is disqualifying: a fault seen once during a pass may never be reproducible.

**Rejected: a fully static cyclic executive** with every task's slot decided at
build time. More analysable still, and genuinely used on flight hardware, but
the loss of flexibility is not worth it at this scale.

**The consequence to live with:** a task that overruns delays everything behind
it. That is why the scheduler measures per-task worst-case execution time and
reports processor load, and why an overrun raises an event rather than being
absorbed silently.

**A deliberate non-behaviour:** after a late tick the scheduler re-anchors to
the next real deadline instead of running catch-up ticks. Catching up converts
one slow tick into a burst of back-to-back work, which makes the next tick late
too, and so on until the system falls over. Losing a tick is the better
failure, and it is counted. `tests/unit/test_scheduler.cpp` asserts this
non-behaviour explicitly.

## Decision: synchronous software bus

**Chosen.** `publish()` calls every subscriber before returning. No queues, no
worker threads, no deferred delivery.

Execution order is fully determined by registration order. There is no queue to
overflow and no message to drop silently. A stack trace during debugging shows
the entire causal chain.

**The consequence to live with:** a slow subscriber directly delays its
publisher. That is acceptable only because everything runs in one thread inside
a rate group with a measured time budget. A subscriber that ever needs to do
something slow must record the request and do the work in its own task.

**Rejected: a queued bus with deferred delivery.** It decouples timing, at the
cost of a queue that can overflow, a delivery order that depends on when the
consumer runs, and a much harder answer to "why did this happen after that?"

## Decision: no allocation after initialisation

Every container is bounded at compile time. `StaticVector`, `RingBuffer`, fixed
buffers as class members.

Heap use in a long-running system invites fragmentation with no way to recover
in orbit, and it makes worst-case memory consumption impossible to prove on the
ground. Here the worst case is a property of the type, visible in the source,
and running out of room is an ordinary error return rather than an allocation
failure at the worst possible moment.

This is enforced, not requested: `fsw_core` and `fsw_apps` compile with
`-fno-exceptions -fno-rtti` and the full warning set as errors.

## Decision: one dictionary, many projections

The highest-leverage decision in the repository.

Every telemetry point, telecommand, event and parameter is declared once in
`dictionary/mission.yaml`. `tools/gen.py` projects it into the C++ structures
and serialisers, the OpenC3 COSMOS database, the Python ground client's tables,
and the interface control document.

In a conventionally built spacecraft the same field is defined in at least four
places, and those four drift. The drift is found during integration, or in
orbit. Here there is one definition and five projections, so disagreement
between spacecraft and ground is not a bug that can be introduced.

The generated code is committed rather than built, so the repository can be
read and the flight software compiled without Python. CI regenerates and fails
on any difference, so the two cannot silently diverge.

## Decision: validation order for untrusted input

Everything arriving on the uplink is untrusted. `parse_tc()` checks in a fixed
order — **integrity, then structure, then meaning**:

1. Is there enough here to be a telecommand at all?
2. Does the CRC pass, over the whole packet, before a single field is read?
3. Does the declared length match what arrived? Is it a telecommand? Is the PUS
   version right?
4. Is this a service and subtype we implement, with the right argument size?

A packet that fails its CRC is **never interpreted**. Its service and subtype
fields are untrustworthy, so it is not dispatched anywhere — only counted and
reported as an event. It is also the one rejection that produces no ST[01]
verification report, because the APID and sequence count such a report would
have to quote back are exactly the fields that cannot be trusted. Answering
anyway would mean sending a report about a command that may never have been
sent, to a spacecraft address that may be fabricated.

## Decision: the spacecraft listens, the ground connects

The flight software is a TCP server. That is the reverse of the physical
situation and it is deliberate: the spacecraft is the long-lived process, the
ground tool comes and goes, and a ground system that can attach and detach at
will is far easier to work with.

One peer at a time. A second connection is refused rather than accepted,
because two ground systems commanding the same spacecraft simultaneously is not
a situation worth supporting quietly.

## Decision: two ground systems

`pyground` is a dependency-free Python implementation of the protocol, written
from the standards rather than translated from the C++. When both sides agree
on a packet, that is evidence the packet matches the standard rather than
evidence the same misreading was made twice.

OpenC3 COSMOS is the operator's tool, used as configuration only — no COSMOS
source is vendored, which keeps this repository cleanly Apache-2.0 despite
COSMOS Core being AGPL.

**Rejected: building a telemetry GUI.** COSMOS already does screens, limits,
logging and scripting properly. Rebuilding it would be weeks spent learning
nothing about flight software.

## Known limitations, stated rather than hidden

**No transfer frames.** Packet boundaries over TCP come from the CCSDS length
field. That works only while the byte stream stays in sync; a corrupted length
field would desynchronise it, and the recovery — discard one octet and retry —
is a mitigation, not a solution. A real RF link carries TM/TC transfer frames
with an attached sync marker precisely so a receiver can regain framing after
noise, plus pseudo-randomisation and Reed-Solomon. Phase 4.

**Time is not correlated.** The PUS time reference status field is reported as
0, meaning "not synchronised with a ground clock", which is honest. ST[09] time
correlation is Phase 4.

**The parameter store holds every value as a `double`.** Exact for every
integer up to 2^53, which covers every type the dictionary currently allows
except a 64-bit integer above that magnitude. The generator would need
extending before such a parameter could be declared.

**No radiation effects yet.** No SEU injection, no EDAC, no memory scrubbing,
no redundancy on critical state. Phase 6.

**The mode reported in `SYS_HK` is hard-coded to `BOOT`** until the mode
manager exists in Phase 5.

## Where this goes

See [ROADMAP.md](ROADMAP.md). The short version: the architecture above was
chosen so that ADCS, EPS, mode management and FDIR can be added as
applications, and a flight processor can be added as a platform directory,
without any of it being a rewrite.
