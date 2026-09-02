# `fsw/platform/posix/` — hosted POSIX adapters

The software-in-the-loop target: Linux and macOS.

| File | Implements | Notes worth knowing |
|---|---|---|
| `posix_clock` | `IClock` | Uses `CLOCK_MONOTONIC`, so adjusting the system date under a running test cannot disturb it. Also implements **simulation time scaling**: `--time-scale 10` runs the spacecraft ten times faster than the wall clock and the flight software above cannot tell |
| `tcp_server_link` | `ILink` | The spacecraft **listens**; the ground connects. Fully non-blocking. Bounded transmit queue that refuses and counts rather than growing without limit |
| `posix_watchdog` | `IWatchdog` | Cannot reset a processor, so it measures the longest gap between kicks and counts how often a real watchdog would have fired |
| `posix_file_storage` | `IStorage` | A flat file of fixed blocks. Opens with `r+b` first so a restart does not erase the stored parameters |

## Details that cost real debugging time

**`SO_REUSEADDR`.** Without it, restarting the flight software inside the
`TIME_WAIT` window fails to bind — which during development is every restart.

**`TCP_NODELAY`.** Telemetry packets are small. With Nagle's algorithm enabled,
a 40-byte housekeeping packet can sit in the kernel waiting for company, and
the resulting latency looks exactly like a flight software problem.

**`SIGPIPE`.** Writing to a socket the ground has already closed raises
`SIGPIPE` and kills the process by default. Linux has `MSG_NOSIGNAL`; macOS and
the BSDs have `SO_NOSIGPIPE` instead, set on accept. Both are handled, because
a ground tool being shut down must never be able to take the spacecraft with it.

**One peer at a time.** A second connection while one is open is refused rather
than accepted. Two ground systems commanding the same spacecraft simultaneously
is not a situation worth supporting quietly.

## What this is not

Not a model of a radio. There is no propagation delay, no bit error rate, no
pass window, no bandwidth limit. Those belong to the link model in Phase 4,
which will sit between this adapter and the ground.
