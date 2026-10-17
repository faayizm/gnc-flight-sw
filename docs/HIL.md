# Hardware in the loop: from QEMU to a board on the desk

> **Untested here.** Everything in this repository has run on QEMU's model of
> the board, not on a physical one. This page is the plan for the step after
> that, written so someone with the hardware can take it. Where it says
> "should", that is what it means.

## What is already true

The board build (`make target`, [`targets/mps2-an500/`](../targets/mps2-an500/))
talks to the outside world through three serial ports and nothing else:

| UART | Carries | Under QEMU |
|---|---|---|
| UART0 | console: the start-up banner, `--verbose` status lines | QEMU's stdout |
| UART1 | TT&C: CLTUs up, CADUs down, exactly as on the laptop | TCP server, the ground's port |
| UART2 | the sensor/actuator bridge to the simulator | TCP server, the simulator's port |

Neither the ground station nor the simulator knows QEMU exists. They connect to
a TCP port and exchange bytes. **The links are byte streams**, so anything that
turns a serial port into a TCP port puts a real board in QEMU's place without
either program changing.

## The step

1. **A board with a Cortex-M7 and three UARTs.** The MPS2+ with the AN500 image
   is the exact match (`board.hpp` has its addresses and interrupt numbers).
   Any other M7 board needs its own `targets/<board>/`: the startup code and
   linker script for its memory map, and UART and watchdog drivers in
   `fsw/platform/` if its peripherals are not the CMSDK ones. Nothing in
   `fsw/core`, `fsw/apps` or `fsw/spacecraft.cpp` changes.
2. **Flash it** with the board's own tools; `build-target/fsw.elf` is a normal
   ELF file.
3. **Bridge each UART to the TCP port its peer expects**, with `socat`, one per
   link (device names depend on your USB-serial adapters):

   ```bash
   socat TCP-LISTEN:50001,reuseaddr,fork FILE:/dev/ttyUSB1,b115200,raw,echo=0   # TT&C
   socat TCP-LISTEN:50000,reuseaddr       FILE:/dev/ttyUSB2,b115200,raw,echo=0   # simulator
   ```

4. **Run the ground and the simulator as usual** against ports 50001 and 50000:
   `make demo`, or any scenario with the flight software already running.

## What will be different, and should be

- **No semihosting.** Without a debugger attached the board takes no options
  (the HardFault handler in `semihosting.cpp` turns the call into "no command
  line"), so it runs at time scale 1, real time. That is the right default for
  hardware: a real board's clock is the real clock.
- **The serial link sets the pace.** Each 0.1 s sensor sample is a 157-byte
  frame; at 115200 baud that is 14 ms of line time, and the reply comes back the
  other way. Real time needs 1.6 kB/s; the line gives 11.5 kB/s. A lockstep run
  could go at most about seven times faster than real time. QEMU manages about
  five, because its UART delivers bytes at a similar rate (Lesson 11 has the
  story of how that was measured).
- **Storage that survives a power cycle** needs a flash or FRAM driver behind
  `IStorage`. The RAM storage here survives a reset, which is what the watchdog
  test needs, but not switching the board off.
- **Timing becomes real.** `tools/budget.py` reports instruction counts from
  QEMU, stated as such. On hardware, the scheduler's own `max_us` statistics are
  real microseconds, and the same report could be generated from a run on the
  board, read off its console.

## Why not do it in CI

Because a board on a desk is not a CI runner. The CI job (`board` in
[`ci.yml`](../.github/workflows/ci.yml)) builds the image with the real
cross-compiler and flies it in QEMU, which catches everything a compiler, a
linker or a different C library can break. What it cannot catch (electrical
noise, a UART's real timing, flash wear) is exactly what a hardware-in-the-loop
bench is for.
