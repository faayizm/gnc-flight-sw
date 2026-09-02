# `fsw/hal/` — the ports

Four interfaces. They are the seam that makes this flight software portable,
and they are deliberately as small as they can possibly be.

| Port | Abstracts | Stands in for |
|---|---|---|
| `IClock` | Time | A POSIX monotonic clock, an RTOS tick, a hardware timer. Also simulation-time acceleration and a frozen test clock |
| `ILink` | A byte pipe | A TCP socket, a UART to a radio, a SpaceWire link |
| `IStorage` | Non-volatile memory | A file, a NOR flash sector, an FRAM |
| `IWatchdog` | The last line of defence | A hardware watchdog timer, or a measurement harness on a hosted build |

## The contract

- **Nothing blocks.** `ILink::receive()` returns what is there, or zero. A
  control loop that blocks on a radio is a control loop that stops controlling
  when the radio misbehaves.
- **Ports move bytes and time. They do not interpret.** Deciding where a packet
  starts and ends is `apps/ttc/`'s job. Mixing the two is what makes flight
  software impossible to port.
- **Every operation returns a status.** No exceptions cross this boundary.
- **Implementations live in `platform/`.** Nothing in `core/` or `apps/` may
  include a concrete implementation.

## Why so few

Every port is a decision that has to be re-made on each new target. Four is
enough to run a spacecraft; twenty would make the next port a rewrite. When a
new capability is needed, the first question is whether it can be expressed
through an existing port before a fifth is added.

## Two details worth reading the headers for

`IClock` separates monotonic time from mission time as *distinct types*, so
that using a ground-steerable clock to measure an interval does not compile.

`IWatchdog` documents the classic mistake — kicking the watchdog from a timer
interrupt, which faithfully proves the interrupt controller is alive while the
application is deadlocked.
