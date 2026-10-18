# Lesson 11 — Portability

🔧 **Builder** · 🎓 Engineer · about 45 minutes

---

## ❓ The question

The flight computer for a satellite is often chosen *late* — after the software
has been in development for a year. And it changes: a supplier stops making a
part, radiation testing rules one out, the budget moves.

How do you write software for a computer you have not chosen yet?

## 💡 The idea

You write the software against **what it needs**, not against **what it has**.

Your flight code needs to know the time. It does not need to know that on Linux
you call `clock_gettime`, or that on FreeRTOS you read a tick counter, or that
on bare metal you read a hardware register.

So describe the need as an interface — a **port** — and write a small adapter
for each machine:

```
   ┌──────────────────────────────────────────────┐
   │   FLIGHT CORE + APPLICATIONS                 │
   │   scheduler, TT&C, ADCS, EPS, modes, FDIR    │
   │   ── never calls the operating system ──     │
   └──────────────────┬───────────────────────────┘
                      │ uses
                      ▼
   ┌──────────────────────────────────────────────┐
   │   PORTS  (fsw/hal/)                          │
   │   IClock   ILink   IStorage   IWatchdog      │
   └──────────────────▲───────────────────────────┘
                      │ implemented by
        ┌─────────────┼─────────────┬─────────────┐
        │             │             │             │
   ┌────┴────┐  ┌─────┴────┐  ┌─────┴─────┐  ┌────┴─────┐
   │  POSIX  │  │ FreeRTOS │  │ bare metal│  │   test   │
   │ laptop  │  │Cortex-M7 │  │ not built │  │  fakes   │
   └─────────┘  └──────────┘  └───────────┘  └──────────┘
```

**Porting to a new machine means writing a new box on the bottom row.** Nothing
above it changes.

## 💡 Only four ports

That is a deliberate choice. Every port is a decision that must be re-made on
each new target, so a large collection of them makes the next port a rewrite.

| Port | Abstracts | Could be |
|---|---|---|
| `IClock` | Time | POSIX monotonic clock, an RTOS tick, a hardware timer |
| `ILink` | A byte pipe | A TCP socket, a UART to a radio, a SpaceWire link |
| `IStorage` | Non-volatile memory | A file, a flash sector, an FRAM |
| `IWatchdog` | The last line of defence | A hardware watchdog timer |

Four is enough to run a spacecraft. When a new capability is needed, the first
question is whether it fits through an existing port before a fifth is added.

## 💡 The rule, and why it is not just a suggestion

```
   core/ and apps/  MAY include  hal/
   core/ and apps/  MAY NOT include  platform/
   no system header appears outside platform/
```

A rule written in a README is a rule that will eventually be broken by someone
in a hurry — including you, in six months. So this one is checked:

```bash
make check-layering
```

```
checking layering rules...
  no system headers outside platform/
  core/hal/apps do not depend on platform/
  no dynamic allocation in core/ or apps/
```

It runs in CI on every push. A violation fails the build, not a code review.

## 🧪 Try it — break the architecture

Add this line to the top of
[`fsw/core/scheduler.cpp`](../../fsw/core/scheduler.cpp):

```cpp
#include <unistd.h>
```

Then:

```bash
make check-layering
```

```
error: system headers outside fsw/platform/:
fsw/core/scheduler.cpp
```

The build refuses. Remove the line.

That is the difference between a documented intention and an enforced one.

## 👀 See it — the same code, told a different time

`IClock` does more than hide the operating system. Because time comes from an
interface, you can hand the flight software a *different* clock and it cannot
tell.

```bash
./build/fsw --time-scale 10 --verbose --max-ticks 1500
```

```
  time scale  : 10.00x
t=    1s  link=DOWN  tc=0/0  tm=0  load=0%  overruns=0
t=    2s  link=DOWN  tc=0/0  tm=0  load=0%  overruns=0
...
```

Those lines are one *spacecraft* second apart, and they scroll past ten times
faster than a clock. 1500 ticks is 30 seconds of spacecraft life, and it takes
about 3 seconds of yours. A 94-minute orbit would take about ten minutes.

The flight software has no idea. Every line of it is unchanged — it asked
`IClock` for the time and believed the answer.

This is how the flight scenarios fly whole orbits in about a minute: `make
pointing` runs the spacecraft at 100 times real speed against the simulator.
There is one subtlety, and it caught this project out twice. The simulator
keeps its own time, and the two clocks drift apart. Lesson 10 tells that
story.

The same mechanism gives unit tests a clock that only moves when the test says
so (Lesson 10), which is why a test of an hour of scheduling runs instantly.

### 🧪 And now look at what it reports on the way out

```
shutting down after 1500 ticks (30 s)
  telecommands : 0 accepted, 0 rejected
  telemetry    : 0 packets
  overruns     : 0
  watchdog     : longest gap 25 ms, 0 notional resets
```

No missed deadlines, and the watchdog was never close to biting. Now ask for
ten times more, the speed the flight scenarios use:

```bash
./build/fsw --time-scale 100 --verbose --max-ticks 15000
```

```
shutting down after 15000 ticks (300 s)
  telecommands : 0 accepted, 0 rejected
  telemetry    : 0 packets
  overruns     : 14
  watchdog     : longest gap 939 ms, 32 notional resets
```

Thirty-two notional resets. Should you be worried?

No — but the reason is worth understanding, because it is a real lesson about
simulation. At 100× the host has to complete a tick every **0.2 milliseconds
of real time**. Now and then the operating system has something else to do
for longer than that (printing a line, running another program), and the
flight software simply does not get the processor. So the spacecraft
genuinely did stall, in its own accelerated timeline, and the watchdog
genuinely noticed. (Your numbers will differ: they measure your computer,
not the spacecraft. A busy laptop may show some even at 10×.)

That is not a false alarm. It is an honest measurement of a true fact: **this
simulation was not keeping up.** Run it at `--time-scale 1` and both numbers
drop to zero.

The general principle: when you accelerate a simulation, you are trading
real-time fidelity for wall-clock speed. Any result that depends on real-time
behaviour — timing margins, watchdog behaviour, deadline misses — becomes
suspect. Results that depend only on the *simulated* physics stay valid. Know
which kind of question you are asking.

## 💡 A port can be a measurement, not just a driver

Here is a nice one. On a laptop there is no hardware watchdog to reset the
processor. The lazy answer is to stub `IWatchdog` out to do nothing.

Instead, [`posix_watchdog.cpp`](../../fsw/platform/posix/posix_watchdog.cpp)
*measures what a real watchdog would have done*:

```cpp
void PosixWatchdog::kick() {
    if (host_timeout_ms_ > 0) { rearm(host_timeout_ms_); }
    if (!enabled_) { return; }

    const core::Instant now = clock_.now();
    const auto gap_ms = static_cast<uint32_t>((now - last_kick_).to_millis());

    if (gap_ms > longest_gap_ms_) { longest_gap_ms_ = gap_ms; }
    if (timeout_ms_ > 0 && gap_ms > timeout_ms_) {
        // On real hardware the processor would be resetting right now.
        ++would_have_reset_;
    }
    last_kick_ = now;
}
```

When you stop a `make run` with `Ctrl-C` it reports:

```
  watchdog     : longest gap 42 ms, 0 notional resets
```

An otherwise untestable component has become a source of evidence. If the
hosted build says the loop went quiet for longer than the timeout, the flight
build on real hardware *would have reset* — and it is far better to learn that
on your desk.

And it can also bite. That first line re-arms a real timer on the host's own
clock. If the loop stops kicking for five seconds, the timer fires, leaves a
note saying "the watchdog did this", and ends the program, as a reset would.
The next start reads the note. Five seconds rather than the flight build's
tens of milliseconds, because a busy laptop is not a wedged loop. Lesson 18
makes it happen on purpose.

## 🎓 Go deeper — what a port must not do

**Nothing blocks.** `ILink::receive()` returns whatever is there, or zero. A
control loop that blocks on a radio is a control loop that stops controlling
when the radio misbehaves.

**Ports move bytes and time; they do not interpret.** Deciding where a packet
starts and ends is `apps/ttc/`'s job, not the link's. Mixing the two is
precisely what makes flight software impossible to port — you end up with a
"link" that knows about CCSDS, and now every new radio driver has to know about
CCSDS too.

**No exceptions cross the boundary.** Every operation returns a status.

**One directory per target, no `#ifdef` ladders.** Selecting between platforms
inside a single file is how portable code becomes unreadable and then becomes
unportable.

## 🚀 Off the laptop: the same core on a flight processor

Everything so far is a claim. Phase 7 tested it: the flight core and every
application, **unchanged**, cross-compiled for an Arm Cortex-M7 under the
FreeRTOS real-time operating system, and flown in an emulator against the
same simulator and the same ground station.

The board is Arm's MPS2 with the AN500 image, as QEMU emulates it. A Cortex-M7
was chosen over a cheaper M3 for one reason: its floating-point unit does
**double precision in hardware**, and every number in the attitude code is a
`double`. On an M3, each of those would become a slow library call.

### What changed, and what did not

| | Laptop | Board |
|---|---|---|
| `fsw/core/`, `fsw/apps/` | ✔ | ✔ **the same files** |
| `fsw/spacecraft.cpp` (the one composition) | ✔ | ✔ **the same file** |
| clock | `clock_gettime` | the FreeRTOS tick, refined by the SysTick counter |
| links | TCP sockets | UARTs, on interrupts, through ring buffers |
| storage | a file | RAM that survives a reset |
| watchdog | a model that measures | the real thing: it resets the board |
| `main` | [`fsw/main.cpp`](../../fsw/main.cpp) | [`targets/mps2-an500/main.cpp`](../../targets/mps2-an500/main.cpp) |

The four new ports are in
[`fsw/platform/freertos/`](../../fsw/platform/freertos/). Both mains build their
four ports and hand them to the same `fsw::Spacecraft`
([`fsw/spacecraft.hpp`](../../fsw/spacecraft.hpp)), which is everything that
makes this software *this spacecraft*.

### 🧪 Try it

You need the Arm cross-compiler and QEMU (on Debian or Ubuntu:
`apt install gcc-arm-none-eabi libnewlib-arm-none-eabi
libstdc++-arm-none-eabi-newlib qemu-system-arm`). Then:

```bash
make target
```

```
Memory region         Used Size  Region Size  %age Used
            CODE:       87056 B         4 MB      2.08%
             RAM:        8280 B      4032 KB      0.20%
          NOINIT:           0 B        64 KB      0.00%
           PSRAM:     4326880 B        16 MB     25.79%
```

That is the linker telling you the spacecraft's **entire** memory footprint
before it has ever run: 87 kB of code, and 4.3 MB of data, nearly all of it the
packet store. It can know, because nothing is allocated at run time: the rule
`make check-layering` enforces, above.
[`docs/BUDGET.md`](../../docs/BUDGET.md) breaks it
down component by component, generated from the binary by `make budget`.

Now boot it:

```bash
make target-run
```

```
HYPERSAT flight software up (Cortex-M7, FreeRTOS V11.1.0).
  TT&C link   : UART1
  sim bridge  : UART2
  base rate   : 50 Hz
  time scale  : 1.00x
  tasks       : 5 registered
  parameters  : 16
  boot        : #1, after POWER_ON
  watchdog    : 1000 ms, 0 resets so far
```

QEMU connects the board's serial ports to TCP ports on your laptop, the same
ones `build/fsw` uses. So in a second terminal, Lesson 2's first contact works
unchanged:

```bash
cd gnd && python3 -m pyground send TEST_CONNECTION
```

```
uplinked TEST_CONNECTION 

  t=     0.927  apid=0x001 seq=    3  VERIF_ACCEPT_OK    tc(apid=0x00A, seq=0)
  t=     0.928  apid=0x001 seq=    4  TEST_REPORT
  t=     0.928  apid=0x001 seq=    5  VERIF_COMPLETE_OK  tc(apid=0x00A, seq=0)
  t=     1.126  apid=0x001 seq=    6  SYS_HK             uptime_s=1  tick_count=56  mode=BOOT  boot_count=1
```

The ground station cannot tell it is talking to a different computer. Nor can
the simulator: `FSW_TARGET=qemu` points every scenario at the board, and `make
target-sil` runs the end-to-end tests against it.

### 🔍 What porting actually found

You might expect a port to be a week of fixing the flight code. Here is the
honest list.

**In `core/` and `apps/`: nothing.** They compiled for the Cortex-M7 on the
first attempt, with every warning switched on and every warning an error. That
is not luck. On the laptop a `size_t` is 64 bits; on the board it is 32. Code
that mixed them carelessly would have broken here, and `-Wconversion` had
already refused it on the laptop, long before there was a board.

**The linker found the heap. Three times.** There is no `malloc` on the board:
the build deliberately provides no `_sbrk`, the function `malloc` needs to get
memory, so anything that so much as *refers* to `malloc` fails to link:

```
sbrkr.c:51:(.text+0xc): undefined reference to `_sbrk'
```

- A class with a virtual destructor has a hidden "deleting destructor" in its
  table of virtual functions, which calls `operator delete`, which calls
  `free`, which needs `malloc`. Nothing ever deletes anything, but the table
  names it anyway. The fix: the board defines its own `operator delete` that
  can never be reached ([`startup.cpp`](../../targets/mps2-an500/startup.cpp)).
- `snprintf`, even formatting a plain integer. The C library's version refers
  to `malloc` for the formats that grow their buffer, and the linker cannot
  know they are never used. The board's console prints numbers by hand.
- `atof`, to read `--time-scale`. Underneath it is `strtod`, which brings in
  `malloc`, `abort`, and file input/output: ten missing system calls from one
  innocent-looking function. The board parses the number by hand.

**The documents were wrong about the memory.** The board's documentation puts
RAM at `0x21000000`. QEMU's model has nothing there; its 16 MB of RAM is at
`0x60000000`. That came out of asking QEMU itself (`info mtree` in its monitor)
before writing the linker script, rather than after the first mysterious crash.

**RAM that survives a reset almost did not.** The parameter table and the boot
count live in RAM that nothing loads or zeroes, so a watchdog reset leaves them
alone. The first version made it a linker *section*, and reading the program
headers of the ELF file (`readelf -l`) showed it inside a loadable segment.
QEMU zero-fills every loadable segment when it reloads the program on reset,
so the first watchdog reset would have wiped exactly what that RAM was for. It
is now just an address the linker sets aside. Caught by reading, before it bit.

**A warning that only exists on ARM.** `-Wcast-align` says nothing on a laptop,
whose processor does not mind misaligned data. On the M7 it refused a cast in
the board's own new code, turning bytes into a structure with 4-byte fields.

### 🔍 The 40 milliseconds

The first flight on the board ran at **1.7 times** real time. The laptop flies
at 100. Some slowdown was expected (QEMU emulates every floating-point
instruction in software), but the numbers did not fit: 1.7 at a time scale of
20, and 1.7 at a time scale of 5.

So, one hypothesis at a time, each tested and not argued:

1. *The clock's busy-wait is starving QEMU.* Changed it to sleep. Still 1.7.
2. *The processor is overloaded.* The board reported its own load: 4%.
3. *The serial link is too slow.* Pushed 5,000 bytes through it: 10.7 kB/s,
   four times what the flight was using.

Every sensor sample is a 157-byte frame from the simulator, and a reply. 1.7
times real time is 17 replies a second: about **60 ms per round trip**, for
data that takes 15 ms to arrive. Something was waiting.

It was the network, between two programs on the same laptop. QEMU writes each
byte the board transmits to the TCP socket separately. TCP's **Nagle
algorithm** holds back small writes while earlier data is unacknowledged, to
save the network from a flood of tiny packets. The simulator's end, meanwhile,
**delays its acknowledgements** by up to 40 ms, hoping to piggy-back them on a
reply. Each side waits for the other. One option on QEMU's side, `nodelay=on`:

```
scale 5: 115 sim-s in 20 s = 5.7x real time
```

What is left is the serial link itself: about 10 kB/s through QEMU's UART,
which by coincidence is close to a real one at 115,200 baud. So a board on a
desk, wired up as [`docs/HIL.md`](../../docs/HIL.md) describes, would fly at
about the same speed. A slower flight is not a problem for the results:
decisions are made on sensor time (Lesson 10), so the spacecraft does the same
thing at 5x as at 100x. It only takes longer to watch.

### 👀 The watchdog, for real

On the laptop the watchdog is a measurement that can, at worst, end the
process. On the board it is the real peripheral, and it resets the processor:

```bash
make target-sil
```

```
[watchdog, on the board]
  .  first boot: boot count 1, started by power-on
  .  ST[17,128] is accepted: the main loop stops servicing the watchdog
  .  the watchdog reset the board, which booted again 0.9 s later
  .  inside the same QEMU process: a reset, not a restart
  .  the next boot knows why it happened: last_reset = WATCHDOG
  .  and counts it: boot count 2
  .  a parameter set before the reset survived it, in RAM nothing reloads or zeroes
```

No test harness restarts anything here. The counter runs down; the first time
it reaches zero it raises the processor's non-maskable interrupt, whose
handler writes "this was the watchdog" into the surviving RAM and waits; the
second time, the board resets. The next boot reads the note.

### 🎓 The same software, not the same numbers

Fly the same detumble on both computers. The laptop:

```
  .  truth rate falls below 0.5 deg/s (at t = 3927 s, 0.69 orbits)
  .  within two orbits
  .  and is below it at the end (final 0.063 deg/s)
determinism: same seed, second run
  .  identical final state at different time scales (06721ea6df543912)
```

And the board, `make target-detumble`, fourteen minutes of wall clock for the
main flight instead of half a minute:

```
  (851.9 s wall clock, 4813 s simulated)
  .  truth rate falls below 0.5 deg/s (at t = 3927 s, 0.69 orbits)
  .  within two orbits
  .  and is below it at the end (final 0.063 deg/s)
determinism: same seed, second run
  .  identical final state at different time scales (06721ea6df543912)
```

The digest comes from a separate ten-minute flight, flown twice at two
speeds (100× and 137× on the laptop; 5× and 3× on the board, which can go no
faster), with the simulator's final state hashed at the end. The same digest
on both computers: that state, every bit of it, is the same whichever
computer flew the spacecraft.

> 🔍 **A check that was not checking.** When this lesson was first written,
> the board's half of that claim was weaker than it looked. `make
> target-detumble` did not run the determinism check at all; the lines above
> came from running it by hand. And when it did run, both flights were held
> to the board's top speed of 5×, so "at different time scales" was not true:
> it compared two identical runs. Reviewing the course found it. Now the
> board's second flight goes slower instead of faster, and `make
> target-detumble` runs the check every time, in CI too.

The plan for this phase predicted the opposite of that agreement, and for good
reasons. The two computers **do not compute the same numbers**:

- **The maths library is different.** The board uses newlib, the laptop glibc.
  A test program fed both 2,000 angles: `sin` disagreed on 70, `cos` on 66,
  `atan2` on 306. Always by exactly one unit in the last place, about one part
  in 10<sup>16</sup>, and always enough to make two `double`s unequal.
- **The arithmetic is different.** On the Cortex-M7, the compiler turns
  `a * b + c` into one *fused multiply-add* instruction, which rounds once
  instead of twice. The board's binary has 392 of them. The laptop's processor
  is never asked for them, so it rounds twice.

So why do the results agree? Because of the interface. The flight software
sends its commands to the simulator as 32-bit `float`s: magnetic dipoles and
wheel torques, rounded to about seven significant digits. A difference in the
sixteenth digit almost never survives that rounding. The two computers think
slightly different thoughts and then say exactly the same thing. (Almost never
is not never: in a long enough flight, one value will sit right on a rounding
boundary, and from then on the two runs part company. The checks are written
against physics, "below 0.5 °/s within two orbits", not against digests,
precisely so that they survive that day.)

One more thing in the board's run is worth a look. Sixty-two times it raised
`SCHED_OVERRUN`: QEMU, emulating every floating-point instruction in software,
sometimes took longer than a tick. On the laptop that would be worrying. Here
it changed nothing, because every decision is made on sensor time (Lesson 10):
a late tick processes the same sample and makes the same decision, only later.
How long the work would take on a *real* Cortex-M7 is a different question,
and [`docs/BUDGET.md`](../../docs/BUDGET.md) answers it as well as an emulator
can: by counting instructions.

## ✅ Check yourself

1. Why does the flight core ask an interface for the time instead of calling
   the operating system?
2. Why only four ports rather than twenty convenient ones?
3. Why is the layering rule checked by a program instead of written in a
   README?
4. Why must `ILink::receive()` never block?
5. The board's build deliberately gives `malloc` no memory to hand out. What
   does that turn a hidden use of the heap into?
6. The board and the laptop compute `sin` differently in the last bit. Why
   did their detumble runs still end in exactly the same state, and why are
   the scenario checks written so that they would pass even if it had not?

---

**Next:** [Lesson 12 — Orbits](../12-orbits/) — and now the physics begins.

<details>
<summary>✅ Answers</summary>

1. So the same code runs on Linux, on an RTOS, on bare metal, against a
   fake clock in a unit test, and against an accelerated clock in a simulation
   — without changing a line. The flight computer is often chosen after the
   software is written.
2. Because every port must be re-implemented for every target. Four is a small
   porting job; twenty is a rewrite, which defeats the purpose.
3. Because a rule that is only written down gets broken by whoever is in a
   hurry, and the breakage is invisible until the port that was supposed to be
   easy turns out not to be. A check fails the build immediately.
4. Because it runs inside a scheduler tick with a 20 ms budget. Blocking on a
   misbehaving radio would stall the control loop — the radio failing would
   take the attitude control down with it.
5. A link error, on the developer's desk, the moment it is written, instead of
   a slow leak or a fragmented heap discovered in orbit. Even a function that
   only *refers* to `malloc` (`snprintf`, `atof`, a deleting destructor) is
   caught.
6. The commands cross to the simulator as 32-bit floats, and rounding to seven
   digits erases a difference in the sixteenth. But a value sitting on a
   rounding boundary would let the difference through, and from then on the
   runs diverge; so the checks test the physics ("below 0.5 °/s within two
   orbits"), which both computers must achieve, not the exact bits.

</details>
