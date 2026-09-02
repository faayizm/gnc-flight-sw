# Lesson 10 — The heartbeat

🔧 **Builder** · 🎓 Engineer · about 30 minutes

---

## ❓ The question

Your laptop runs hundreds of threads at once. Your phone does too. It is how
essentially all modern software is written.

Flight software uses **one**. On purpose. Why would anyone give that up?

## 💡 The problem with threads

Threads are wonderful for a laptop. The operating system decides when each runs,
switching between them thousands of times a second, and the result is a
responsive machine.

But "the operating system decides when each runs" means **you** do not decide.
Run the same program twice and the interleaving differs. On a laptop that is
almost always fine.

On a spacecraft it is disqualifying, and here is the reason:

> A bug seen once in orbit must be reproducible on the ground.

If task ordering varies between runs, a fault that appeared during one pass may
never appear again on your desk. You cannot debug what you cannot reproduce,
and you have exactly one spacecraft.

There is a famous case. **Mars Pathfinder, 1997.** The rover landed
successfully and then began resetting itself, repeatedly, on Mars. The cause
was *priority inversion*: a low-priority task held a resource a high-priority
task needed, a medium-priority task kept preempting the low one, and a watchdog
timer eventually gave up and reset the spacecraft. It had been seen once during
testing, could not be reproduced, and was written off as a glitch.

The fix was uploaded from Earth, to Mars, and it worked. It remains the best
story in this field, and it is about exactly the kind of scheduler this lesson
is about.

## 💡 The answer: rate groups

One thread. A steady tick. Each task declares how often it runs.

```
   tick:      0  1  2  3  4  5  6  7  8  9
   ttc_rx:    X  X  X  X  X  X  X  X  X  X      every tick  = 50 Hz
   ttc_tm:       X         X         X          every 5th   = 10 Hz
   adcs:      X     X     X     X     X         (Phase 2)
```

The base tick here is **50 Hz** — every 20 milliseconds. Each task has a
*divider* (run every N ticks) and an *offset* (which of those N).

The offset matters more than it looks. Two 10 Hz tasks on a 50 Hz base can be
given offsets 0 and 2, so they never land on the same tick and the work is
spread out instead of bunching up.

The result: **the same inputs produce the same outputs, every time, on every
machine.** That single property is what makes a simulator worth building.

## 👀 See it

```bash
make run
```

```
t=    1s  link=DOWN  tc=0/0  tm=0  load=0%  overruns=0
t=    2s  link=DOWN  tc=0/0  tm=0  load=0%  overruns=0
```

Two numbers there are the whole point of this lesson.

**`load`** is the fraction of available time actually used. A flight processor
comfortably below 50% has margin for the worst case that has not happened yet.
One at 90% will miss deadlines the first time something takes longer than usual.

**`overruns`** counts how many ticks took longer than their 20 ms. It should be
zero, always. When it is not, something needs looking at.

Both are downlinked in `SYS_HK`, because on a real mission you cannot watch a
terminal — you watch telemetry.

## 🔍 In the code

[`fsw/core/scheduler.hpp`](../../fsw/core/scheduler.hpp) opens with the whole
argument, and [`fsw/main.cpp`](../../fsw/main.cpp) registers the tasks:

```cpp
// Divider is in base ticks: 1 = 50 Hz, 5 = 10 Hz, 50 = 1 Hz.
// Offsets stagger the slower groups so they never land on the same tick.
scheduler.add_task("ttc_rx",  &TtcApp::task_receive,   &ttc, 1);
scheduler.add_task("ttc_tm",  &TtcApp::task_telemetry, &ttc, 5, 1);
```

Receiving commands runs at 50 Hz because command latency is the thing operators
feel most. Generating telemetry runs at 10 Hz, which is plenty.

Note there is no thread, no mutex, no lock anywhere in this codebase. There is
nothing to deadlock.

## 💡 The most important non-behaviour

Here is a subtle decision, and it is the kind of thing that separates working
flight software from software that works in a lab.

**What should happen when a tick runs late?**

The tempting answer is "catch up" — run the missed ticks back to back. It feels
responsible. It is a disaster:

```
   tick 100 takes 30 ms (10 ms late)
     → run tick 101 immediately to catch up
       → now tick 101 has 30 ms of work in a 20 ms slot, so it is late too
         → run tick 102 immediately
           → ... and the system falls over
```

One slow tick becomes a cascading collapse. So this scheduler does the opposite:

```cpp
if (now >= next_deadline_) {
    // We are already late. Do NOT run catch-up ticks: that converts a
    // single late tick into a burst of back-to-back work, which makes the
    // next tick late too, and so on until the system falls over. Re-anchor
    // to the future instead and accept the lost tick, which has been
    // counted as an overrun.
    next_deadline_ = now + tick_period();
}
```

It **drops** the tick, counts it, raises an event, and carries on at the right
rate. Losing one cycle is a much better failure than losing the spacecraft.

And because this is a non-behaviour — something that must *not* happen — there
is a test asserting it, which is the only way such a property survives future
edits:

```bash
./build/tests/fsw_tests 2>&1 | grep catch_up
```

```
  .  a_late_tick_does_not_trigger_catch_up_ticks
```

## 💡 The trick that makes this testable

A scheduler is about time, and testing time is normally miserable — a test of
"does this run once per hour" would take an hour.

Unless time is behind an interface. From
[`tests/unit/test_scheduler.cpp`](../../tests/unit/test_scheduler.cpp):

```cpp
class TestClock final : public fsw::hal::IClock {
 public:
    Instant now() override { return Instant::from_micros(us_); }

    // A test clock never really sleeps: it jumps straight to the deadline.
    void sleep_until(Instant deadline) override { ... }

    void advance(int64_t micros) { us_ += micros; }
};
```

The clock moves only when the test says so, and it can charge a fixed cost to
each task. So a test of an hour of scheduling runs in microseconds, and a
deadline overrun can be exercised without anything actually being slow.

That is the payoff of `hal::IClock` — the subject of the next lesson.

## 🧪 Try it — make it miss a deadline

Add a task that deliberately takes too long. In
[`fsw/main.cpp`](../../fsw/main.cpp), just before the watchdog is enabled:

```cpp
scheduler.add_task("hog", [](void*) {
    volatile double x = 0;
    for (long i = 0; i < 20000000; ++i) { x += i * 0.5; }
}, nullptr, 1);
```

Rebuild and run:

```bash
make build && make run
```

```
t=    3s  link=DOWN  tc=0/0  tm=0  load=100%  overruns=147
```

Load pinned at 100%, overruns climbing. Connect a monitor and you will see
`EVENT_MEDIUM  SCHED_OVERRUN` reports arriving — the spacecraft reporting its
own timing failure to the ground, which is exactly what you would want.

**Remove that task before continuing.**

## ✅ Check yourself

1. Why is deterministic execution order worth giving up threads for?
2. Two tasks both run at 10 Hz on a 50 Hz base. Why give them different
   offsets?
3. Why does the scheduler *not* catch up after a late tick?
4. `load` reads 85%. Why is that a problem even though nothing has failed?

## 🎓 Go deeper

**Rejected alternatives** are documented in
[`../../docs/ARCHITECTURE.md`](../../docs/ARCHITECTURE.md#decision-one-thread-rate-groups),
including a fully static cyclic executive — more analysable still, genuinely
used on flight hardware, and rejected here as too rigid at this scale.

**Worst-case execution time.** The scheduler records `max_us` per task, not
just an average. For a timing budget the worst case is what matters: an average
of 2 ms with occasional 25 ms spikes will miss deadlines, and the average will
never tell you.

**The watchdog.** [`fsw/hal/watchdog.hpp`](../../fsw/hal/watchdog.hpp) explains
a classic mistake: kicking the watchdog from a timer interrupt. It then
faithfully proves the interrupt controller is alive while the application is
deadlocked, and the spacecraft is lost with a perfectly healthy watchdog. In
this codebase it is kicked from exactly one place — the main loop, after the
scheduler confirms a tick completed.

---

**Next:** [Lesson 11 — Portability](../11-portability/) — writing code before
you know what computer it will run on.

<details>
<summary>✅ Answers</summary>

1. Because a fault seen once in orbit must be reproducible on the ground, and
   you get one spacecraft. Non-deterministic ordering means a bug can appear
   during a pass and never be seen again on your desk.
2. To spread the load. With the same offset, both run on the same tick and that
   tick has to fit twice the work; staggered, each tick carries one of them.
3. Because catching up puts extra work into an already-tight slot, making the
   next tick late too, cascading until the system collapses. Dropping the tick
   and counting it is a bounded failure.
4. Because 85% is the load you are seeing *now*, under conditions that have
   been fine so far. The worst case — a burst of uplink, a slower code path, a
   sensor that needs retrying — has not happened yet, and there is only 15% of
   room for it.

</details>
