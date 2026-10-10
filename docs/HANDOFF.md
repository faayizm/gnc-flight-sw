# Handoff: Phase 7 in progress

Written so a fresh session can continue without the old conversation. Delete
this file when Phase 7 is finished.

## Where things stand

- Phases 0–6 are done, pushed, and green on a full `make check` (Phase 6:
  FDIR, ST[12]/ST[19], radiation with EDAC/TMR, a real hosted watchdog,
  Monte Carlo; see `docs/FDIR.md`). The course is up to date with Phase 6.
- **Phase 7 ("Off the laptop", `docs/ROADMAP.md`) has started.** Step 1 below
  is done and committed; nothing else of Phase 7 exists yet.

## Phase 7 plan

1. ✅ **One composition, two mains.** `fsw/spacecraft.{hpp,cpp}` (in
   `fsw_apps`) owns the core and every application, counts boots, loads and
   saves parameters, registers the tasks. `fsw/main.cpp` now only builds the
   POSIX ports, works out the reset cause, and runs the loop. Verified:
   unit 141/141, SIL 51/51, determinism hash unchanged, lessons pass.
2. **Cross-compile `fsw_core` + `fsw_apps` for Cortex-M7** with
   arm-none-eabi-g++ and the same strict flags. Fix what breaks, and keep a
   list: it is Lesson 11 material ("what porting actually found").
3. **FreeRTOS platform + board**, as `fsw/platform/freertos/` (ports) and
   `targets/mps2-an500/` (startup, linker script, `FreeRTOSConfig.h`,
   `main.cpp`, CMake toolchain). `make target` builds `build-target/fsw.elf`.
4. **Fly it in QEMU** (`make target-run`), with the UARTs as TCP ports so the
   unchanged Python ground station and simulator connect. Run the SIL tests
   and at least one scenario against it (a harness option to launch QEMU
   instead of `build/fsw`). Show the watchdog resetting the board and the
   reboot reporting `WATCHDOG`.
5. **Budget report generated from the binary**: `tools/budget.py` writes
   `docs/BUDGET.md`: memory per component and largest symbols (from the ELF /
   map), stack per function (`-fstack-usage` `.su` files), and per-task
   timing from the scheduler's `max_us`, measured in a QEMU `-icount` run
   (deterministic instruction counts, stated as such, not real M7 cycles).
6. **HIL path, CI, course.** `docs/HIL.md`: how a dev board would replace
   QEMU (the links are byte streams, so `socat TCP-LISTEN:... /dev/ttyACM0`
   bridges them; say plainly it is untested here). A CI job that installs the
   toolchain, builds the target and runs the QEMU smoke test and scenario.
   Lesson 11 (portability) gets the real story with fresh outputs; also
   Lesson 10 (timing) if the budget numbers belong there; glossary; roadmap
   Phase 7 marked done.

## Decisions already made (and why)

- **Board: QEMU `mps2-an500` (Cortex-M7).** Its FPU does double precision in
  hardware, and the attitude code is all `double`. `mps2-an385` (M3) would
  soft-float everything. Memory is the constraint: the TT&C packet store is
  4 MiB, so put `.data`/`.bss` in the 16 MiB PSRAM (verify the address in
  QEMU's `hw/arm/mps2.c` for AN500 before writing the linker script; do not
  assume it) or make the store size a platform constant.
- **Links:** CMSDK UARTs. UART0 console, UART1 TT&C, UART2 sim bridge, mapped
  with `-serial` to TCP servers. A UART link is always "connected".
- **Time scale on the board:** pass arguments by semihosting
  (`-semihosting-config enable=on,arg=...`) so the board `main` can reuse the
  hosted options parser (`--time-scale`). Do NOT use `-icount sleep=off` for
  flying scenarios: virtual time races ahead while waiting for the
  simulator, and the sensor timeout fires.
- **Non-volatile storage on the board:** a `.noinit` RAM region, which
  survives a watchdog reset within one QEMU run (not a new QEMU process).
  Reset cause: a magic word in `.noinit` present at boot means "not a
  power-on", i.e. WATCHDOG, the only other reset source on this board.
- **Watchdog:** the CMSDK APB watchdog. **Check first** that QEMU models it
  on AN500 and that its reset reaches a system reset.
- **FreeRTOS kernel V11.1.0**, fetched at build time (CMake `FetchContent`,
  pinned tag), not vendored. Port `GCC/ARM_CM7/r0p1` (or `ARM_CM4F`).
  Static allocation only, matching the no-heap rule.
- **Don't promise identical hashes.** The board's newlib `sin`/`cos`/`atan2`
  differ from glibc in the last bits, and chaos amplifies it, so the
  host and board runs will not give the same digest. Assert that the board
  passes the same scenario checks, and explain why in Lesson 11 (a good
  lesson about what "the same software" means).
- **Expect it to be slower than the laptop.** QEMU emulates the FPU in
  software, and lockstep keeps every result correct whatever the speed, so
  measure the wall-clock time and say so.

## Setting up a fresh container

Not persistent: install again in each new session.

```bash
apt-get install -y gcc-arm-none-eabi libnewlib-arm-none-eabi \
    libstdc++-arm-none-eabi-newlib qemu-system-arm
# (if that fails: apt-get update first)
```

Versions used so far: arm-none-eabi-gcc 13.2.rel1, QEMU 8.2.2, FreeRTOS
Kernel V11.1.0 (cloned from GitHub without trouble through the proxy).
