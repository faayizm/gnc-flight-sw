# Lesson 18 — When things break

🔧 **Builder** · 🎓 Engineer · about 30 minutes

> **Phase 6 note.** FDIR, fault injection and radiation effects are Phase 6.
> Several ideas here are already implemented and you can exercise them today —
> those are marked ✅.

---

## ❓ The question

Something has failed. A sensor is stuck. A wheel has stopped. Memory has been
corrupted by a cosmic ray.

Nobody can go and look. Nobody can reboot it by hand. The next contact is in an
hour.

**Now what?**

This is the lesson that separates flight software from software.

## 💡 The name for it: FDIR

**Fault Detection, Isolation and Recovery.** Three separate jobs, and they fail
in different ways:

```
   DETECTION    something is wrong
                → miss it and you fly on broken data
                → over-trigger and you cry wolf constantly

   ISOLATION    WHAT is wrong
                → the hard part. A wrong diagnosis leads to
                  a wrong recovery, which can be worse than none

   RECOVERY     do something about it
                → and the something must not make it worse
```

Isolation is the difficult one. A battery voltage reading of zero might be a
dead battery, a failed sensor, a broken wire, or a software bug. Those call for
completely different responses, and you have one chance to choose.

## 💡 The recovery ladder

Never jump straight to the drastic option. Climb:

```
   1. REPORT       raise an event; keep going
                   most "faults" are noise

   2. RETRY        try again; many are transient
                   a bit flip in a packet fixes itself on retransmission

   3. RECONFIGURE  switch to the backup sensor, isolate the bad wheel
                   degraded, but still working

   4. SAFE MODE    stop, point at the Sun, wait for a human
                   the mission pauses but the spacecraft lives

   5. RESET        the watchdog fires; start again from scratch
                   the last resort
```

Each rung costs more than the one below. Going straight to level 4 because a
sensor produced one odd reading wastes an orbit of science for nothing. Staying
on level 1 while the battery drains loses the spacecraft.

## ✅ What already works

Three rungs of that ladder are in this repository today.

**Detection of corrupted data.** ✅ Every packet carries a CRC and a bad one is
never interpreted (Lesson 4). Try it:

```bash
cd gnd
python3 -c "
import sys; sys.path.insert(0, '.')
from pyground import GroundClient
from pyground.packets import build_tc
p = bytearray(build_tc('TEST_CONNECTION')); p[8] ^= 0x01
with GroundClient() as g:
    g.send_raw(bytes(p))
    for tm in g.poll(timeout=2.0): print(tm.summary())
"
```

```
EVENT_LOW   TC_REJECTED aux=BAD_CRC
```

Detected, isolated, reported. Rung 1.

**Recovery from corrupted storage.** ✅ The parameter table carries a CRC, and
a failure falls back to compiled-in defaults rather than booting on values it
cannot vouch for (Lesson 9). You see it on every fresh start:

```
note: stored parameters not usable (IO_ERROR), running on compiled-in defaults
```

There is a test that deliberately corrupts a byte and checks the defaults
survive:

```bash
./build/tests/fsw_tests 2>&1 | grep corrupted
```

```
  .  a_corrupted_block_is_refused_and_defaults_survive
```

**Detection of timing failure.** ✅ The scheduler counts deadline misses, raises
`SCHED_OVERRUN`, and reports the total in housekeeping (Lesson 10).

**The watchdog.** ✅ Modelled and measured even on a laptop, where there is no
hardware to reset (Lesson 11).

## 💡 Radiation: when the computer itself is the fault

This is the part with no equivalent on the ground.

A high-energy particle passing through a memory cell can flip a bit. Your
variable changes value with nothing having written to it. In low Earth orbit
this happens regularly; over the South Atlantic Anomaly, where the radiation
belts dip low, it happens much more often.

Three flavours, and they are genuinely different:

| Effect | What happens | What you do |
|---|---|---|
| **SEU** — single-event upset | A bit flips. Hardware is fine, data is wrong | Detect with EDAC; scrub memory continuously |
| **SEL** — single-event latch-up | A short circuit forms; the chip draws heavy current | Power-cycle the device fast, before it burns |
| **TID** — total ionising dose | Cumulative damage over years; parts slowly degrade | Choose radiation-tolerant parts; shield; accept a lifetime |

**EDAC** — error detection and correction — stores extra bits so a single flip
can be corrected and a double flip at least detected. **Scrubbing** means
walking through memory continuously, reading and rewriting, so that single
errors are fixed before a second one lands in the same word and becomes
uncorrectable.

For truly critical state you go further: store it three times and vote. Two
copies agreeing beat one that does not.

None of this is implemented yet. Phase 6 adds SEU injection, an EDAC model,
scrubbing, and voting on critical state — because the only way to know your
recovery paths work is to *cause* the faults deliberately.

## 💡 Fault injection: breaking it on purpose

Here is the central idea of the whole lesson.

**Recovery code that has never run is not working code. It is a guess.**

Most flight software bugs live in the error paths, because those are the paths
nobody exercises. The code that handles a nominal orbit runs millions of times;
the code that handles a failed reaction wheel runs never — until the day it
matters, and then it has a null pointer in it.

So you inject faults deliberately, on purpose, constantly. Phase 6 puts the
injection at the simulator bridge, because that is exactly where the spacecraft
meets its environment:

| Injected fault | What it should prove |
|---|---|
| Sensor freezes at its last value | Is stale data detected, or trusted forever? |
| Sensor returns an out-of-range value | Is it rejected, or does it poison the filter? |
| Ground link drops for ten minutes | Does the link timeout autonomy fire? |
| A reaction wheel stops responding | Is it isolated, and can the others compensate? |
| A bit is flipped in a stored parameter | Do the defaults take over? |
| A task is made to overrun its deadline | Is the overrun detected and reported? |

And then **Monte Carlo**: run hundreds of randomised scenarios in CI, with
random fault timings, and require that the spacecraft always ends up in a safe
state. Not "usually". Always.

## 💡 The two rules of fault handling

**1. Fail safe, not silent.**

Every rejection path in this flight software either sends a report or raises an
event. There is no path where something goes wrong and nothing is said. A
system that fails silently consumes the one thing an operator cannot get more
of: time.

**2. Do not make it worse.**

Recovery actions can cause the fault they were meant to fix. The Mars Pathfinder
watchdog reset the spacecraft repeatedly — the recovery was working exactly as
designed, and the spacecraft kept dying, because the diagnosis was wrong. It was
saved by engineers on Earth working out the *actual* cause and uploading a fix.

This is why the ladder exists, why entering safe mode is easier than leaving it
(Lesson 17), and why every automatic recovery should be counted and reported. A
recovery that has fired forty times is not a recovery; it is a symptom.

## 🧪 Try it — cause a real timing fault

Add a deliberately slow task to [`fsw/main.cpp`](../../fsw/main.cpp), before
the watchdog is enabled:

```cpp
scheduler.add_task("hog", [](void*) {
    volatile double x = 0;
    for (long i = 0; i < 20000000; ++i) { x += i * 0.5; }
}, nullptr, 1);
```

```bash
make build && make run
```

In another terminal, `make monitor`. You will see:

```
  EVENT_MEDIUM  SCHED_OVERRUN aux=...
  SYS_HK  ... sched_overruns=147  cpu_load_pct=100
```

The spacecraft detected its own timing failure and reported it to the ground —
with a severity, a count in housekeeping, and enough detail to act on. That is
rung 1 of the ladder, working, today.

**Remove that task afterwards.**

## ✅ Check yourself

1. Why is isolation harder than detection?
2. Why not go straight to safe mode whenever anything looks wrong?
3. Why must recovery code be tested by deliberately injecting faults?
4. Why is a recovery action that has fired forty times a problem, even if it
   worked every time?

## 🎓 Go deeper

**Failure Modes and Effects Analysis (FMEA)** is the systematic version of this
lesson: list every component, every way it can fail, the effect of each failure,
and what detects and handles it. It is tedious and it is how real missions find
the gap where a failure has no detection at all.

**Single point of failure.** Any component whose failure ends the mission. You
either add redundancy or accept the risk consciously — and the phrase "accept
consciously" is the important half.

**The best story in the field.** Mars Pathfinder, 1997. It is in Lesson 10, and
it is worth reading properly: a scheduling bug on another planet, seen once in
testing and dismissed, diagnosed remotely from telemetry and fixed by an upload.
Everything in this lesson is in that story.

---

## 🎉 You have finished

Eighteen lessons, from "what is a satellite" to fault recovery on a machine
nobody can reach.

**What next?**

- **Build the next phase.** [`docs/ROADMAP.md`](../../docs/ROADMAP.md) says
  exactly what comes next and why. Phase 2 is the simulator and B-dot detumble —
  and Lessons 12, 13 and 16 have already taught you the physics for it.
- **Read the architecture.**
  [`docs/ARCHITECTURE.md`](../../docs/ARCHITECTURE.md) explains every major
  decision, including the alternatives that were rejected.
- **Break something and fix it.** Change a value in
  `dictionary/mission.yaml`, run `make gen`, and watch it appear in the flight
  code, the ground system and the ICD at once.
- **Ask a question.** A confusing lesson is a bug in the lesson. Open an issue.

<details>
<summary>✅ Answers</summary>

1. Because a symptom can have many causes. A zero voltage reading might be a
   dead battery, a failed sensor, a broken wire or a software bug — and each
   calls for a different response. Detection only asks "is something wrong";
   isolation has to answer "what", with limited information.
2. Because safe mode stops the mission. Doing it for a single odd sensor reading
   costs an orbit or more of science for something that was probably noise. The
   ladder exists so the response is proportionate to the evidence.
3. Because recovery code is the code that never runs in normal operation, so it
   is where bugs survive undetected. The only way to know it works is to make
   the fault happen on purpose, repeatedly, in a place where it is safe.
4. Because it means the underlying fault keeps recurring and nothing is fixing
   it. The recovery is masking a problem rather than solving it, and each firing
   costs power and time. A rising recovery count is one of the most useful
   signals in telemetry.

</details>
