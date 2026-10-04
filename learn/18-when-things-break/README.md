# Lesson 18 — When things break

🔧 **Builder** · 🎓 Engineer · about 45 minutes

> **Built.** Everything in this lesson flies. `make fdir` breaks the spacecraft
> four ways in one flight. `make radiation` flips hundreds of bits in its
> memory. `make monte-carlo` kills a wheel in dozens of slightly different
> spacecraft. And you can crash the flight computer yourself, on purpose, in
> the last exercise.

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
                   a power cycle clears a latch-up

   3. RECONFIGURE  stop using the broken part, carry on without it
                   degraded, but still working

   4. SAFE MODE    stop the mission, keep the spacecraft calm,
                   wait for a human. The mission pauses; the spacecraft lives

   5. RESET        the watchdog fires; start again from scratch
                   the last resort
```

Each rung costs more than the one below. Going straight to level 4 because a
sensor produced one odd reading wastes an orbit of science for nothing. Staying
on level 1 while the battery drains loses the spacecraft.

## 💡 Breaking it on purpose

Here is the central idea of the whole lesson.

**Recovery code that has never run is not working code. It is a guess.**

Most flight software bugs live in the error paths, because those are the paths
nobody exercises. The code that handles a nominal orbit runs millions of times;
the code that handles a failed reaction wheel runs never — until the day it
matters, and then it has a bug in it.

So faults are injected deliberately, constantly. In this repository the
simulator does it, in [`sim/sil/faults.py`](../../sim/sil/faults.py), at
exactly the place a real fault would happen: at the sensor, at the actuator,
or on the data bus between them and the flight computer. It never reaches
into the flight software. Whatever the flight software notices, it has to
notice from the data alone, as it would in orbit.

| Injected fault | What it should prove | ✅ |
|---|---|---|
| A sensor freezes at one value, still saying "valid" | Is stale data caught, or trusted forever? | `make fdir` |
| A sensor reports an impossible value | Is it rejected, or does it poison the filter? | `make fdir` |
| Sensor frames are corrupted, or stop arriving | Is every bad frame refused, and the gap reported? | `make fdir` |
| A wheel's drive latches up | Does a power cycle bring it back? | `make fdir` |
| A wheel's drive dies for good | Is it isolated, and can the rest compensate? | `make fdir`, `make monte-carlo` |
| Bits flip in memory | Are they corrected before they matter? | `make radiation` |
| The software stops making progress | Does the watchdog reset it, and does it come back? | the exercise below |

## 🔍 Sensors that lie

A sensor's own "valid" flag means only that its electronics *think* they
produced a reading. Two common failures never clear it:

- **Out of range.** A converter stuck at full scale. Earth's magnetic field in
  low orbit is 20 to 60 microtesla, so a magnetometer reading two millitesla is
  wrong, whatever its flag says.
- **Frozen.** The same reading, exactly, sample after sample. Every real sensor
  has noise, so a value that never changes is not a quiet sensor: it is a dead
  one. This is one of the few places where noise is *useful*.

The check lives at the hardware boundary,
[`fsw/apps/io/sensor_screen.hpp`](../../fsw/apps/io/sensor_screen.hpp). A
sensor it distrusts has its valid flag cleared, so everything downstream just
sees "no data", which the estimator and the mode manager already know how to
handle. The fault stops at the boundary instead of spreading: that is
**isolation**. Act 1 of `make fdir`:

```
  t=  301.3 s  event SENSOR_REJECTED  aux=258
  t=  332.3 s  event SENSOR_READMITTED  aux=1
  t=  400.3 s  event SENSOR_REJECTED  aux=1
  t=  432.1 s  event SENSOR_READMITTED
  t=  504.9 s  event SENSOR_REJECTED  aux=770
  t=  569.9 s  event SENSOR_READMITTED  aux=3
  t=  703.5 s  event SENSOR_GAP  aux=3100
```

`aux=258` is `1 << 8 | 2`: sensor 1 (the gyro), fault 2 (frozen). The
magnetometer's impossible reading (`aux=1`: sensor 0, fault 1, out of range)
is refused on the very first sample. Each sensor comes back only after twenty
good readings in a row, so a sensor flickering between broken and working is
not trusted on its first good one. Through all of it:

```
  .  nadir error stays below 0.5 deg throughout (worst 0.181 deg)
```

> 🔍 **A frozen star tracker that was never caught.** The first version
> reset its "how many identical readings" count on every sample with no
> reading. A star tracker reports twice a second, so four samples in five
> carry nothing from it, and the count never got past one. A frozen tracker
> was trusted completely, and the pointing was 6.8° off before the fault
> ended. The bug was only found because a test froze the tracker on purpose.

## 🔍 The ladder, climbed by a wheel

How do you know a reaction wheel has failed? Not by asking it. You check a
piece of bookkeeping. A wheel motor's job is to change the wheel's momentum at
the rate it is commanded. The tachometer measures the momentum. So over five
seconds, per wheel:

```
   measured change    h(now) - h(5 s ago)
   commanded change   sum of (torque command x 0.1 s) over those 5 s
```

A healthy wheel's two numbers agree, to within the friction its drive does
not quite cancel. A dead drive delivers no torque, and its wheel slows on
bearing friction instead: the two disagree, quickly and by a lot. Five
seconds, not one sample, because the tachometer counts in steps of a
millionth of a newton-metre-second. Over one sample, that rounding is larger
than the friction the check has to see. Over a window, the rounding only
matters at the two ends. Code:
[`fsw/apps/fdir/wheel_check.hpp`](../../fsw/apps/fdir/wheel_check.hpp).

What to do about it is
[`fsw/apps/fdir/wheel_ladder.hpp`](../../fsw/apps/fdir/wheel_ladder.hpp), the
ladder above made real. Acts 2 and 3 of `make fdir`:

```
  t= 1008.5 s  event WHEEL_FAULT  aux=2
  t= 1008.5 s  event WHEEL_POWER_CYCLE  aux=2
  t= 1026.7 s  event WHEEL_RECOVERED  aux=2
  t= 1511.5 s  event WHEEL_FAULT  aux=1
  t= 1511.5 s  event WHEEL_POWER_CYCLE  aux=1
  t= 1524.9 s  event WHEEL_ISOLATED  aux=1
```

At t=1000 the simulator gave the Y wheel (`aux=2`, bit 1) a **latch-up**: a
particle switched on a parasitic current path in its drive. Rung 1: report
it. Rung 2: switch the drives off for three seconds and on again. That is
exactly what clears a latch-up, and the wheel comes back. Isolating it would
have cost a working wheel for the rest of the mission.

At t=1500 the X wheel (`aux=1`) dies for good. The power cycle does not help.
Rung 3: take it out of service and **reconfigure**. The pointing controller,
[`fsw/apps/adcs/pointing.hpp`](../../fsw/apps/adcs/pointing.hpp), gives the
X axis to the magnetorquers. Lesson 16's coil law, turned round: the dipole
that makes a wanted torque τ is

```
        m  =  (B × τ) / |B|²       which gives       m × B  =  τ  minus its part along B
```

Everything except the part along the field, which no coil can ever make. A
coil is hundreds of times weaker than a wheel, so that axis gets a gentler
control loop, one the coil can actually deliver:

```
  .  after 900 s to settle, nadir error stays below 2.0 deg on two wheels and the magnetorquers (worst 0.91 deg; peak while the dead wheel spun down 6.0 deg)
```

The 6° peak is the dead wheel's stored spin draining into the spacecraft as
its bearings slow it. With the ladder switched off, the same kind of failure
in a test flight let the error grow past 35° within ten minutes.

Rung 4 belongs to the mode manager: with fewer than two wheels left there is
nothing to reconfigure into, and it puts the spacecraft in SAFE. A unit test
checks that rule, and `make monte-carlo` checks that one dead wheel never
triggers it.

## 💡 Watching from the inside: ST[12] and ST[19]

The ground watches telemetry for values out of limits, but it sees the
spacecraft for perhaps forty minutes a day. **On-board monitoring**, PUS
service ST[12], is the same watching done by the spacecraft itself, all the
time. What is watched is declared in
[`dictionary/mission.yaml`](../../dictionary/mission.yaml), not written in
code:

```yaml
  - {id: 4, name: POINTING,     packet: ADCS_HK, field: pointing_err_deg, low: -1.0,  high: 15.0, repetitions: 3000, event: POINTING_LOST}
```

Above 15° for 3,000 samples in a row (five minutes) means pointing is lost.
The repetition count is the filter between noise and a fault. A value that
dips for one sample is not an alarm. One that stays out for five minutes is.

A monitor only raises an event. What happens next is a separate table,
**event-action**, PUS service ST[19]:

```yaml
event_actions:
  - {event: POINTING_LOST, command: SET_MODE, args: {mode: SAFE},
     desc: "Pointing has been lost for five minutes and nothing below has recovered it: stop trying, go SAFE, wait for the ground"}
```

Detection and response are kept apart, so either can be changed, or
switched off from the ground (`DISABLE_EVENT_ACTION`), without touching the
other. And the action is stored as a complete **telecommand**, so it goes
through every check a command from the ground would, and can do nothing the
ground could not.

Act 4 of `make fdir` needs both. With the X wheel already dead, the
magnetometer fails too, and the magnetorquers that replaced the wheel are
blind. Nothing below the top of the ladder can help. SAFE switches the
transmitter off, so the ground learns what happened the way it really would:
at the next pass, from the spacecraft's own record (Lesson 8):

```
  replayed: POINTING_LOST, EVENT_ACTION, MODE_CHANGED, SAFE_MODE_ENTERED, LOAD_SHED, SENSOR_READMITTED
```

## 🔍 What the Monte Carlo found

One successful flight proves that one case works. A spacecraft meets the
cases nobody wrote down: a slightly different mass, a different tumble, the
fault at a different moment, on a different axis. A **Monte Carlo** campaign
flies the same scenario many times with every uncertain quantity drawn at
random, and asks how many runs meet the requirements:

```bash
make monte-carlo
```

```
   seed  inertia (kg m^2)          tumble  wheel  fails at  detect  isolate   peak   worst  3-axis  result
   1000  0.1056/0.1241/0.0368      0.84   Y     1665.8    11.3     24.7    6.0    1.10     1.1  ok
   1001  0.1059/0.1094/0.0421      1.75   X     2055.6     8.9     22.1   14.2    0.97     1.0  ok
   ...
   1007  0.1071/0.1272/0.0408      1.80   Z     2217.9    11.6     25.0    0.1    0.05    15.3  ok

  8 of 8 runs met every requirement
```

CI flies eight. The first longer campaigns, 48 runs each, turned up four
failures of two different kinds, and both are worth knowing.

**Three runs went SAFE over nothing.** All three lost the Z wheel, which spins
about the boresight, the axis the payload looks along. Its stored spin
drained into a slow yaw, and the spacecraft turned 140° *about the direction
it was pointing*. The payload still saw the Earth's centre to within 0.1°.
But the pointing monitor watched `pointing_err_deg`. The dictionary describes
that field as the boresight's error, and the code had been filling it with
the full three-axis error, yaw included. So the monitor declared pointing
lost, and the event-action sent a working spacecraft to SAFE. The field now
means what the dictionary says, the three-axis error has its own field
(`att_err_deg`), and the "3-axis" column above shows the yaw that is now,
correctly, tolerated.

**One run was slow to notice.** Seed 2042's X wheel was almost at rest, and
being asked for almost nothing, when it died. A dead wheel that is asked for
nothing looks exactly like a healthy one. The evidence only arrived when the
controller started leaning on it, 37 seconds later. That broke a requirement
of "detect within 20 s". But the same idleness meant the failure cost only
1.5°. The requirement was wrong, not the software: a fault cannot be seen in
something that is not being used (that is called **observability**). The
campaign now requires what matters: isolation within 60 seconds, no SAFE,
and pointing held.

Neither problem appears in any single scripted flight. That is what the
campaign is for.

## ✅ Rungs you met earlier

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

**Recovery from corrupted storage.** ✅ The stored parameter table carries a
CRC, and a failure falls back to compiled-in defaults rather than booting on
values it cannot vouch for (Lesson 9).

**Detection of timing failure.** ✅ The scheduler counts deadline misses and
raises `SCHED_OVERRUN` (Lesson 10). The exercise at the end causes one.

**Retry.** ✅ A command lost on the radio is sent again until it arrives, in
order, exactly once: COP-1 (Lesson 7).

**Reconfigure: a sensor stops talking.** ✅ If sensor data stops, the
spacecraft zeroes every actuator and raises `SENSOR_TIMEOUT`. A controller
that keeps applying its last command to a spacecraft it can no longer see is
how a small fault becomes a large one.

> 🔍 **Two seconds of *which* clock?** The first version timed those two
> seconds on the flight clock. The tests fly at 100 times real speed, so two
> flight seconds were 20 real milliseconds. When the test computer got busy
> and paused the simulator for that long, the spacecraft decided its sensors
> were dead, threw away its filter history, and flew differently from the
> same run on a quiet computer. The determinism check caught it. The fix is
> to time this one check on the computer's own clock. It is watching the
> simulator *program*, not the spacecraft, and a busy program is not a dead
> sensor. On real hardware the two clocks are the same clock. A short gap in
> the data is still caught, from the sensors' own timestamps: that is the
> `SENSOR_GAP` in act 1 above.

**Reconfigure: the estimator loses track.** ✅ If the attitude filter disagrees
with every measurement for ten seconds, it is thrown away and restarted from
scratch, with an `ESTIMATOR_RESET` event.

**Safe mode, for a power fault.** ✅ `make power` injects a heater stuck on
(Lesson 17). The spacecraft sheds load in order and puts itself into SAFE:

```
                   shedding 1 2 3 4 0
```

(Sometimes more numbers follow the 0. After recovery the payload comes back
on, and depending on exactly when the operator stepped in, the battery may dip
to LOW once more.)

## 💡 Radiation: when the computer itself is the fault

This is the part with no equivalent on the ground.

A high-energy particle passing through a memory cell can flip a bit. Your
variable changes value with nothing having written to it. In low Earth orbit
this happens regularly. Over the **South Atlantic Anomaly**, where the inner
radiation belt dips lowest, it happens much more often.

Three flavours, and they are genuinely different:

| Effect | What happens | What you do |
|---|---|---|
| **SEU** — single-event upset | A bit flips. Hardware is fine, data is wrong | Correct it with EDAC; scrub memory continuously |
| **SEL** — single-event latch-up | A parasitic current path switches on; the part stops, and may overheat | Power-cycle it — exactly act 2 above |
| **TID** — total ionising dose | Cumulative damage over years; parts slowly degrade | Choose radiation-tolerant parts; shield; accept a lifetime |

**EDAC** — error detection and correction — stores a few extra bits with every
word, so that a single flipped bit can be *found*, not just noticed. Try it:

```bash
python3 learn/toolbox/edac_playground.py
```

```
      flipped    stored                 syndrome   points at
      bit 1      [1] 1  1  0  0  1  1       1          bit 1
      bit 2       0 [0] 1  0  0  1  1       2          bit 2
      bit 3       0  1 [0] 0  0  1  1       3          bit 3
```

The check bits are chosen so that the positions of all the 1 bits XOR to
zero. Flip any bit and that XOR becomes its position: the **syndrome** is the
address of the broken bit. The flight software does the same to 64-bit words
with 8 check bits ([`fsw/core/edac.hpp`](../../fsw/core/edac.hpp)): it can
correct any single flip, and detect, but not correct, any double flip. Every
value in the parameter table lives in such a word.

Correction happens when a word is read, but the stored word stays wrong
until it is rewritten. A second particle in the same word then makes two
flips, which cannot be corrected. So a **scrubber** walks memory, reading and
rewriting. The playground counts what that is worth, over a simulated week:

```
      scrubbed every     uncorrectable errors
      never              6062
      24 h               2576
      1 h                154
      1 min              2
      1.6 s              0
```

For a few values whose corruption would be a disaster on their own, the
spacecraft's mode and the list of failed wheels, there is an older trick:
keep **three copies and vote**
([`fsw/core/tmr.hpp`](../../fsw/core/tmr.hpp)). One upset can corrupt only
one copy, so the vote is always right, and it repairs the odd one out.

`make radiation` flies an orbit with upsets a thousand times more frequent
than real ones, and ten times more again in the Anomaly:

```
  upsets delivered: 331 (234 in the SAA) -- parameter table 312, mode 9, wheel isolation 10
  on board: 310 corrected by EDAC, 1 uncorrectable, 19 repaired by TMR voting
```

Seven in ten of the upsets happened in the Anomaly, which covers a small
part of the orbit. The one uncorrectable error is deliberate: two flips in
the same word, back to back. That word reads as its default, never as
garbage, and FDIR reloads the table from non-volatile storage, which still
holds the value the ground set:

```
  .  so MOMENTUM_DUMP_GAIN is still the ground's 0.0006, not the default 0.0005
  .  and pointing never noticed any of it (worst 0.059 deg, from 600 s after pointing engaged)
```

## 🧪 Try it — make the watchdog bite

The watchdog (Lesson 11) is the one defence that works when the software has
gone completely wrong, so it is the one you most need to see work. This
build has a real one: if the main loop stops servicing it for five seconds,
the computer is reset.

`TEST_WATCHDOG` stops the servicing, and nothing else. The software carries
on running. Terminal 1:

```bash
make run
```

Terminal 2:

```bash
cd gnd
python3 -m pyground send TEST_WATCHDOG
```

Terminal 1 keeps printing its status line for five more seconds, then:

```
make: *** [Makefile:189: run] Error 86
```

The flight computer was reset. (On a laptop "reset" means the process ends
with status 86. On a real spacecraft the processor restarts by itself.) Play
the hardware, and power it up again:

```bash
make run
```

```
  boot        : #2, after WATCHDOG
```

It knows it was reset, and why. That was saved in non-volatile memory, like
the boot count, and it is downlinked in `SYS_HK` as `boot_count` and
`last_reset`. A boot count that rises when nobody asked is one of the most
important numbers an operator watches. Parameters set before the crash
survive it too: they reach storage within a second of being set, because a
reset does not run any shutdown code.

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

In another terminal, `make monitor`. You will see `SCHED_OVERRUN` events,
and `sched_overruns` climbing in `SYS_HK`. The spacecraft detected its own
timing failure and reported it, with a severity, a count and enough detail to
act on.

**Remove that task afterwards.**

## 💡 The two rules of fault handling

**1. Fail safe, not silent.**

Every rejection path in this flight software either sends a report or raises an
event. There is no path where something goes wrong and nothing is said. A
system that fails silently consumes the one thing an operator cannot get more
of: time.

**2. Do not make it worse.**

Recovery actions can cause the fault they were meant to fix. The Mars Pathfinder
watchdog reset the spacecraft repeatedly. The recovery was working exactly as
designed, and the spacecraft kept dying, because the diagnosis was wrong. It was
saved by engineers on Earth working out the *actual* cause and uploading a fix.

This is why the ladder exists, why the wheel ladder retries a wheel only
twice, why entering safe mode is easier than leaving it (Lesson 17), and why
every automatic recovery is counted and reported. A recovery that has fired
forty times is not a recovery; it is a symptom.

## ✅ Check yourself

1. Why is isolation harder than detection?
2. Why not go straight to safe mode whenever anything looks wrong?
3. A wheel stopped responding. Why power-cycle it before taking it out of
   service?
4. Why does EDAC need a scrubber, when every read already corrects the value?
5. Why is the event-action stored as a telecommand rather than as a function
   call?
6. Why must recovery code be tested by deliberately injecting faults?

## 🎓 Go deeper

**Failure Modes and Effects Analysis (FMEA)** is the systematic version of this
lesson: list every component, every way it can fail, the effect of each failure,
and what detects and handles it. It is tedious, and it is how real missions find
the gap where a failure has no detection at all.

**Observability, again.** Lesson 15 met it in the estimator: a filter cannot
learn a gyro bias about an axis that never turns. Seed 2042 is the same idea
in fault detection. A check can only judge what the system exercises, and
some missions deliberately exercise their redundant parts ("health checks")
for exactly that reason.

**Single point of failure.** Any component whose failure ends the mission. You
either add redundancy or accept the risk consciously — and the phrase "accept
consciously" is the important half. This spacecraft has three wheels, and
survives losing one. Many carry four, in a pyramid, so that any three can
still give full three-axis control.

---

## 🎉 You have finished

Eighteen lessons, from "what is a satellite" to fault recovery on a machine
nobody can reach.

**What next?**

- **Fly the whole thing.** `make detumble`, `make pointing`,
  `make store-forward`, `make power`, `make fdir`, `make radiation` and
  `make monte-carlo` each fly the spacecraft against the simulator and check
  every claim against the truth. Read their output with the lessons beside
  you.
- **Run a bigger campaign.** `make monte-carlo RUNS=100`, and see whether the
  requirements still hold at the hundredth case.
- **Build the next phase.** [`docs/ROADMAP.md`](../../docs/ROADMAP.md) says
  what comes next: Phase 7 takes the flight software off the laptop.
- **Read the architecture.**
  [`docs/ARCHITECTURE.md`](../../docs/ARCHITECTURE.md) explains every major
  decision, including the alternatives that were rejected.
- **Break something and fix it.** Add a monitor to `dictionary/mission.yaml`,
  run `make gen`, and watch it appear in the flight code, the ground system
  and the ICD at once.
- **Ask a question.** A confusing lesson is a bug in the lesson. Open an issue.

<details>
<summary>✅ Answers</summary>

1. Because a symptom can have many causes. A zero voltage reading might be a
   dead battery, a failed sensor, a broken wire or a software bug, and each
   calls for a different response. Detection only asks "is something wrong";
   isolation has to answer "what", with limited information.
2. Because safe mode stops the mission. Doing it for a single odd sensor reading
   costs an orbit or more of science for something that was probably noise. The
   ladder exists so the response is proportionate to the evidence.
3. Because many failures in orbit are latch-ups, not broken hardware, and
   removing the power is the only thing that clears one. A three-second power
   cycle is cheap. Isolating a wheel that only needed one costs it for the rest
   of the mission.
4. Because a read corrects the value it returns, not the value stored. The
   stored word stays wrong, and a second upset in the same word makes two
   flips, which EDAC can detect but not correct. The scrubber rewrites every
   word before that second hit is likely to arrive.
5. So that it goes through every check a ground command does, and can do
   nothing the ground could not. An on-board action that bypassed the mode
   manager's rules would be a second, unchecked way to change the spacecraft's
   state.
6. Because recovery code is the code that never runs in normal operation, so it
   is where bugs survive undetected. The frozen star tracker and the yaw that
   sent a working spacecraft to SAFE were both found only because a test caused
   the fault on purpose.

</details>
