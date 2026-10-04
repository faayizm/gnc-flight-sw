# Lesson 8 — Housekeeping and events

🚀 **Explorer** · about 25 minutes

---

## ❓ The question

Your satellite is out of contact for 90 minutes. When it comes back over the
horizon you have eight minutes to find out whether anything went wrong.

How does a machine explain what happened while nobody was listening?

## 💡 The idea

Two completely different kinds of telemetry, for two different questions.

```
   HOUSEKEEPING                      EVENTS
   ────────────                      ──────
   "here are my numbers"             "something HAPPENED"
   sent on a timer, always           sent when it occurs, only then
   answers: how am I doing?          answers: what changed?
   like a heart monitor              like a diary entry
```

You need both. Housekeeping tells you the battery is at 43%. Events tell you
that four minutes ago it entered safe mode. Neither alone is enough.

## 👀 See it — housekeeping

Start your spacecraft and watch:

```bash
make monitor
```

```
  t=     1.122  apid=0x001 seq=    3  SYS_HK             uptime_s=1  tick_count=56  mode=BOOT  boot_count=1
  t=     1.122  apid=0x002 seq=    1  ADCS_HK            est_state=INVALID  ctrl_mode=IDLE  q_est_0=0  q_est_1=0
  t=     1.122  apid=0x003 seq=    1  EPS_HK             power_state=UNKNOWN  batt_voltage=0  batt_current=0  batt_soc_pct=0
  t=     1.122  apid=0x004 seq=    1  FDIR_HK            wheel_state=MONITOR  wheels_usable=7  wheel_retries=0  wheel_resid_x=0
  t=     2.122  apid=0x001 seq=    4  SYS_HK             uptime_s=2  tick_count=106  mode=BOOT  boot_count=1
```

Four reports, once a second, forever, without anyone asking. That is
`ST[3,25]`, and it is the bulk of what any spacecraft downlinks.

`SYS_HK` carries 24 fields about the flight software's own health. The monitor
shows only the first four of each report. The full list is in the dictionary:

```bash
cd gnd && python3 -m pyground monitor
```

or read the definition in
[`dictionary/mission.yaml`](../../dictionary/mission.yaml):

| Field | Why an engineer looks at it |
|---|---|
| `uptime_s` | Has it reset? An uptime that goes backwards means something bad |
| `boot_count` / `last_reset` | How many times has it started, and why the last time? (Lesson 18) |
| `cpu_load_pct` | Is there margin left for the worst case? |
| `sched_overruns` | Has the software ever missed a deadline? |
| `tc_received` / `tc_rejected` | Is the uplink healthy? A rising rejected count means noise |
| `tm_sent` | Is the downlink flowing? |
| `events_logged` | Has anything happened that I should look at? |

The rest describe the radio link (frames sent, damaged frames refused, bits
repaired: Lessons 4, 5 and 7), the clock and scheduled commands (Lesson 6),
and the on-board recorder (below).

**ADCS_HK and EPS_HK are all zeros.** That is honest, not broken. With only
`make run`, no simulator is connected, so there are no sensors and no battery
to report on. The packets still arrive correctly formatted, which proves the
telemetry chain works before there is anything real to put in it. With a
simulator connected (`make detumble-live`, Lesson 12) they fill with real
numbers.

`FDIR_HK`, the fault-management report, is the exception: `wheels_usable=7`
is a bit mask, X, Y and Z all set, meaning all three reaction wheels are in
service. That is true before a single sensor sample has arrived. Lesson 18 is
about what makes it change.

## 👀 See it — events

Events appear only when something happens. Connect and disconnect the ground
station and watch:

```
  EVENT_INFO   LINK_CONNECTED
  EVENT_INFO   PARAM_SET aux=1
  EVENT_LOW    TC_REJECTED aux=UNKNOWN_SERVICE
  EVENT_INFO   HK_DISABLED aux=3
```

Each has a **severity**, and here is the elegant part: in PUS, the *subtype is
the severity*.

| Report | Severity |
|---|---|
| `ST[5,1]` | Informative — normal, worth recording |
| `ST[5,2]` | Low — a bit odd |
| `ST[5,3]` | Medium — something is wrong |
| `ST[5,4]` | High — serious |

A ground system can filter on urgency **without knowing anything about this
particular mission's events**, because the severity lives in the standard
header rather than in mission-specific data.

## 💡 The bounded history

Here is the problem with events. What if something important happens while out
of contact, and then a hundred boring things happen after it? The important
one must not be lost — but memory is finite.

[`fsw/core/event_log.hpp`](../../fsw/core/event_log.hpp) keeps the last 64
events in a ring buffer, and does one thing that matters enormously:

> It overwrites the oldest when full and **counts what it discarded**, so the
> ground can tell the difference between "nothing happened" and "we lost the
> record".

An empty log means nothing happened. A log with `dropped_count = 47` means
forty-seven things happened that you will never know about. Those are wildly
different situations, and a system that cannot distinguish them is lying to
you by omission.

## 💡 The discipline that keeps it useful

From the same file:

> **DISCIPLINE.** Events are for state changes, not for tracing. An event
> raised every control cycle is a bug: it will swamp the downlink budget, evict
> the history that mattered, and hide the one event somebody needed to see.

This is a real failure mode. A well-meaning developer adds an event to help
debug something, it fires at 50 Hz, and now the only thing in the log is that
one message repeated 64 times. The genuinely important event from ten minutes
ago is gone.

Events are a *scarce, precious* resource. Treat them that way.

## 🧪 Try it — turn telemetry off

Housekeeping can be silenced per structure — `ST[3,6]` — which you would use
during a congested pass to make room for something more urgent.

With the spacecraft running and `make monitor` going in a third terminal:

```bash
cd gnd
python3 -m pyground send DISABLE_HK sid=3
```

`EPS_HK` stops. `SYS_HK` and `ADCS_HK` keep flowing. Turn it back on:

```bash
python3 -m pyground send ENABLE_HK sid=3
```

Notice that both produce an event — `HK_DISABLED` and `HK_ENABLED`. A change to
what the spacecraft reports is itself worth reporting, otherwise a later
operator sees missing telemetry and has no idea whether it was commanded or
broken.

## 🧪 Try it — make it talk faster

```bash
python3 -m pyground send SET_PARAM param_id=1 value=250
```

`SYS_HK` now arrives four times a second instead of once. Put it back with
`value=1000`.

There is a design decision hiding in there. Look at
[`ttc_app.cpp`](../../fsw/apps/ttc/ttc_app.cpp):

```cpp
// A shortened period must take effect NOW, not after the interval that
// was already running finishes. An operator who asks for faster
// telemetry during a pass has a reason, and making them wait up to a
// full old period -- possibly a minute -- would be surprising and useless.
```

That behaviour exists because a software-in-the-loop test caught the original
version doing the wrong thing. The test is still there, in
[`tests/sil/test_endtoend.py`](../../tests/sil/test_endtoend.py).

## 🧪 Try it — what happened while you weren't looking

A real ground station can hear its satellite for ten minutes in every ninety.
Housekeeping sent in the other eighty would be lost, so the spacecraft
**records everything it says**, contact or not, into a 4-megabyte on-board
store (PUS service ST[15]). Leave `make run` going for twenty seconds, then
ask what is in it:

```bash
cd gnd
python3 -m pyground send REPORT_STORE_SUMMARY store_id=1
```

```
  t=    20.045  apid=0x001 seq=   22  STORE_SUMMARY      store_id=1  oldest_s=0  newest_s=20  packets=79
```

Eighty-odd packets, from second 0 to second 20 (the exact number depends on
when you asked). Now ask for seconds 5 to 7 back:

```bash
python3 -m pyground send RETRIEVE_BY_TIME store_id=1 from_s=5 to_s=7
```

```
  t=    21.645  apid=0x001 seq=   29  EVENT_INFO         PLAYBACK_STARTED aux=12
  t=    21.645  apid=0x001 seq=   31  EVENT_INFO         PLAYBACK_DONE aux=12
  [replay] t=     5.425  apid=0x001 seq=    6  SYS_HK             uptime_s=5  tick_count=271  mode=BOOT  boot_count=1
  [replay] t=     5.425  apid=0x002 seq=    5  ADCS_HK            est_state=INVALID  ctrl_mode=IDLE  q_est_0=0  q_est_1=0
  ...
  [replay] t=     7.425  apid=0x004 seq=    7  FDIR_HK            wheel_state=MONITOR  wheels_usable=7  wheel_retries=0  wheel_resid_x=0
```

Twelve packets from fifteen seconds ago, with their **original** timestamps and
sequence numbers. That is how you can tell a replay from the live stream.
They travel on a separate channel, marked `[replay]` here, so a long playback
never holds up live telemetry.

Notice that `PLAYBACK_DONE` can arrive *before* the last replayed packets.
The two channels are independent, and the event took the faster one. A
ground system has to cope with that, and so do you when reading the output.

On a real mission the range would be "the ninety minutes since the last
pass". `make store-forward` does exactly that, and checks that not a single
packet from the dark part of the orbit was lost.

## ✅ Check yourself

1. Why does a spacecraft need both housekeeping and events?
2. `events_logged = 0` versus `events_logged = 200, dropped = 47`. What does
   each tell you?
3. Why is an event raised every control cycle a bug?
4. Why does disabling a housekeeping report itself generate an event?

## 🎓 Go deeper

**ST[12] on-board monitoring** lets the spacecraft check its own limits and
raise an event when a value strays, so it notices a problem immediately
rather than waiting for a human to spot it in a graph hours later. The limits
are in the `monitors:` section of
[`dictionary/mission.yaml`](../../dictionary/mission.yaml), and Lesson 18
shows one catching a fault that nothing else could.

---

**Next:** [Lesson 9 — Parameters](../09-parameters/) — changing a spacecraft's
behaviour from 500 km away without breaking it.

<details>
<summary>✅ Answers</summary>

1. Housekeeping answers "what is the state now?", sampled on a timer, so you
   can plot trends. Events answer "what changed, and when?", which a periodic
   sample can miss entirely if it happens between two samples.
2. The first says nothing noteworthy has happened. The second says 200 things
   happened, and the records of 47 of them have been overwritten and are gone
   forever. Without the drop counter you could not tell those apart.
3. Because it floods the downlink and, worse, evicts the whole history — the
   ring buffer fills with 64 copies of the same routine message, destroying
   exactly the record you would need when something goes wrong.
4. So that a later operator seeing missing telemetry can tell whether it was
   deliberately commanded off or whether something failed. Silence with no
   explanation is indistinguishable from a fault.

</details>
