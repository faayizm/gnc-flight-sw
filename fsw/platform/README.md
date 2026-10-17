# `fsw/platform/` — the adapters

> 📚 **Learning this?** See [Lesson 11 — Portability](../../learn/11-portability/) in the lesson track.


The only code in the flight software tree permitted to include a system header.
Everything here implements a port from `fsw/hal/`.

```
platform/
├── posix/      hosted Linux and macOS. The software-in-the-loop target.
└── freertos/   a Cortex-M under FreeRTOS: the RTOS tick for time, the Arm
                CMSDK UART and watchdog, RAM that survives a reset. Built by
                targets/mps2-an500/ (make target), flown in QEMU.
```

## What porting means

Adding a target is adding a directory here and a new `main.cpp`. That is the
whole claim of this architecture, and it is why `core/` and `apps/` are written
the way they are. Phase 7 tested it: the FreeRTOS board needed no change to
`core/` or `apps/` at all ([Lesson 11](../../learn/11-portability/) has what it
did need).

| Target | Directory | Status | What it took |
|---|---|---|---|
| POSIX (hosted) | `posix/` | done | sockets, `clock_gettime`, a file |
| FreeRTOS on Cortex-M7 | `freertos/` | done (Phase 7), in QEMU | the RTOS tick refined by SysTick, interrupt-driven UARTs, the CMSDK watchdog, RAM storage |
| Bare metal | `baremetal/` | not built | a timer ISR in place of the RTOS tick; the UART, watchdog and storage above would serve unchanged |

Note what is *not* on that list: nothing in `core/` or `apps/`.

## Rules

- One directory per target. No `#ifdef` ladders selecting between platforms
  inside a single file — that is how portable code becomes unreadable and then
  becomes unportable.
- An adapter implements exactly one port and holds no application state.
- An adapter may be a **model**, not only a driver. `PosixWatchdog` cannot reset
  anything, so instead it measures the gaps between kicks and reports how many
  times a real watchdog would have fired. That turns an otherwise untestable
  component into evidence.

## Checked by

`make check-layering` greps for system headers outside this directory and for
includes of `platform/` from `core/` or `apps/`. It is part of CI, because a
layering rule that is only in a README is a layering rule that will be broken.
