# `targets/mps2-an500/` — the board

> 📚 **Learning this?** See [Lesson 11 — Portability](../../learn/11-portability/).

HYPERSAT on Arm's MPS2 board with the AN500 image: a Cortex-M7 with a
double-precision FPU, under FreeRTOS, as QEMU 8.2 emulates it. The flight
core, the applications and `fsw/spacecraft.cpp` are the laptop's, compiled
from the same source list (`cmake/fsw_sources.cmake`) with the same flags.

```bash
make target            # build-target/fsw.elf, with a linker map and stack-usage files
make target-run        # boot it in QEMU: TT&C on TCP 50001, simulator on 50000
make target-sil        # the end-to-end tests, against the board
make target-detumble   # a whole scenario, about 15 minutes
make budget            # docs/BUDGET.md, from the binary and a QEMU -icount run
```

| File | What it is |
|---|---|
| `main.cpp` | The board's main: builds the four ports, finds the reset cause, runs the loop in the one FreeRTOS task |
| `startup.cpp` | From reset to `board_main()`: FPU on, `.data` copied, `.bss` zeroed, constructors; the vector table; the C runtime hooks that keep the heap out |
| `mps2_an500.ld` | The linker script: code in SSRAM1, data in SSRAM2/3, the spacecraft in PSRAM, 64 KiB that survives a reset |
| `board.hpp` | Addresses and interrupt numbers |
| `FreeRTOSConfig.h` | The kernel, mostly switched off: static allocation only, no queues, no timers |
| `console.cpp` | UART0 text out, without `printf` |
| `semihosting.cpp` | The command line from QEMU, an exit for `--max-ticks`, and a HardFault handler that lets the same image run without a debugger |
| `toolchain-arm-none-eabi.cmake` | The cross-compiler and the Cortex-M7 flags |
| `CMakeLists.txt` | The build; fetches FreeRTOS V11.1.0 at a pinned tag |

The ports themselves (clock, UART link, watchdog, RAM storage) are in
[`fsw/platform/freertos/`](../../fsw/platform/freertos/), because they are the
platform layer, not this one board. [`docs/HIL.md`](../../docs/HIL.md) is how a
physical board would take QEMU's place.
