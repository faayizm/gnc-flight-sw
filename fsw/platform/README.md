# `fsw/platform/` — the adapters

The only code in the flight software tree permitted to include a system header.
Everything here implements a port from `fsw/hal/`.

```
platform/
└── posix/      hosted Linux and macOS. The software-in-the-loop target.
```

## What porting means

Adding a target is adding a directory here and a new `main.cpp`. That is the
whole claim of this architecture, and it is why `core/` and `apps/` are written
the way they are.

| Planned target | Directory | Phase | What it needs |
|---|---|---|---|
| POSIX (hosted) | `posix/` | done | sockets, `clock_gettime`, a file |
| FreeRTOS on Cortex-M | `freertos/` | 7 | a tick hook, a UART driver, an independent watchdog, an EEPROM or flash driver |
| Bare metal | `baremetal/` | 7 | a timer ISR, a polled UART, the same watchdog and storage |

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
