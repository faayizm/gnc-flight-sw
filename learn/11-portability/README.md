# Lesson 11 — Portability

🔧 **Builder** · 🎓 Engineer · about 25 minutes

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
   │  today  │  │ Phase 7  │  │  Phase 7  │  │  fakes   │
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
about 3 seconds of yours. A 96-minute orbit would take ten minutes to simulate.

The flight software has no idea. Every line of it is unchanged — it asked
`IClock` for the time and believed the answer.

The same mechanism gives unit tests a clock that only moves when the test says
so (Lesson 10), which is why a test of an hour of scheduling runs instantly.

### 🧪 And now look at what it reports on the way out

```
shutting down after 1500 ticks (30 s)
  overruns     : 2
  watchdog     : longest gap 460 ms, 13 notional resets
```

Thirteen notional resets. Should you be worried?

No — but the reason is worth understanding, because it is a real lesson about
simulation. At 10× the host has to complete a tick every **2 milliseconds of
real time**, and printing a line to the terminal sometimes takes longer than
that. So the spacecraft genuinely did stall, in its own accelerated timeline,
and the watchdog genuinely noticed.

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

Instead, [`posix_watchdog.hpp`](../../fsw/platform/posix/posix_watchdog.hpp)
*measures what a real watchdog would have done*:

```cpp
void PosixWatchdog::kick() {
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

When you stop the spacecraft it reports:

```
  watchdog     : longest gap 21 ms, 0 notional resets
```

An otherwise untestable component has become a source of evidence. If the
hosted build says the loop went quiet for longer than the timeout, the flight
build on real hardware *would have reset* — and it is far better to learn that
on your desk.

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

## 🎓 Go deeper — what Phase 7 actually looks like

| Target | Directory | What it needs |
|---|---|---|
| POSIX | `platform/posix/` | done |
| FreeRTOS on Cortex-M | `platform/freertos/` | a tick hook, a UART driver, an independent watchdog, a flash driver |
| Bare metal | `platform/baremetal/` | a timer ISR, a polled UART, the same watchdog and storage |

Note what is *not* on that list: any change to `core/` or `apps/`. That is the
claim this whole layout exists to make, and it is why the layering check runs
on every commit rather than being trusted to good intentions.

## ✅ Check yourself

1. Why does the flight core ask an interface for the time instead of calling
   the operating system?
2. Why only four ports rather than twenty convenient ones?
3. Why is the layering rule checked by a program instead of written in a
   README?
4. Why must `ILink::receive()` never block?

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

</details>
