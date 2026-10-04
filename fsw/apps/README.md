# `fsw/apps/` — the applications

The parts that know what a spacecraft is. Everything below them — the
scheduler, the bus, the parameter table — knows nothing about attitude, power
or radios.

```
apps/
├── ttc/        telemetry, tracking and command      WORKING (docs/LINK.md)
├── io/         the hardware boundary                WORKING
├── adcs/       attitude determination and control   WORKING (docs/ATTITUDE.md)
├── eps/        electrical power                     WORKING (docs/POWER_AND_MODES.md)
├── modemgr/    spacecraft mode arbitration          WORKING (docs/POWER_AND_MODES.md)
└── fdir/       fault detection, isolation, recovery WORKING (docs/FDIR.md)

messages.hpp    every bus payload, with its publisher and subscribers
```

## How an application is built

1. It is a plain class. No base class, no framework, no registration macro.
2. It takes its dependencies as constructor references — a link, a clock, the
   bus, the event log, the parameter store. It never reaches for a global.
3. It exposes `static void task_*(void* context)` entry points, which the
   scheduler calls at a declared rate. Each simply recovers `this` from the
   context pointer.
4. `main.cpp` constructs it and registers its tasks. That is the only place the
   application's existence is known.

## How applications talk to each other

**They do not.** They publish and subscribe on the software bus.

TT&C reports attitude housekeeping without knowing that an ADCS application
exists: ADCS publishes on `Topic::AdcsHk`, TT&C caches whatever last arrived.
When a ground command asks for a mode change, TT&C publishes
`Topic::ModeRequest` and the mode manager decides — TT&C has no business
judging whether a mode change is safe, and does not.

The consequence is that adding a subsystem needs no change to any existing
application, only a new entry in the dictionary and a new directory here.

## Rules

- Same constraints as `core/`: no allocation after init, no exceptions, no OS
  headers.
- All buffers are fixed-size members. The worst-case memory footprint of an
  application is `sizeof` that application.
- An application owns its state. Nothing reaches into another application's
  data.
