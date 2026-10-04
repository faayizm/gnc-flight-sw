# Lesson 6 — PUS services

🚀 **Explorer** · 🔧 Builder · about 25 minutes

---

## ❓ The question

Lesson 5 gave us an envelope. But an envelope with no agreement about what goes
*inside* still means every mission invents its own language, and every ground
station has to be rewritten from scratch.

So what goes inside?

## 💡 The idea

The European Space Agency wrote down an answer, and much of the world adopted
it: the **Packet Utilisation Standard**, or **PUS** (formally
ECSS-E-ST-70-41C).

CCSDS says *how to wrap* a message. PUS says *what the message means*.

The idea is beautiful in its simplicity. Number the *kinds* of thing a
spacecraft does — call each one a **service** — and then number the specific
requests inside each service:

```
        ST[ 3 , 25 ]
           │    │
           │    └── subtype: which specific message
           └─────── service: which kind of thing
```

`ST[3,25]` is a housekeeping report, on every PUS spacecraft ever flown.
`ST[17,1]` is a connection test. `ST[5,4]` is a high-severity alarm. Always.

**The payoff:** a ground station that speaks PUS can operate a spacecraft it
has never seen before. It already knows how to ask for housekeeping, read
events, set parameters and dump memory — because those are standardised, not
invented per mission.

## 💡 The services

There are about twenty in the standard. This spacecraft implements eleven:

| Service | Name | What it is for | Here? |
|---|---|---|---|
| **ST[01]** | Request verification | "Did my command arrive? Did it work?" | ✅ |
| **ST[03]** | Housekeeping | Periodic health reports | ✅ |
| **ST[05]** | Event reporting | "Something happened" | ✅ |
| **ST[08]** | Function management | Do a mission-specific thing | ✅ partly |
| **ST[17]** | Test | A safe are-you-there check | ✅ |
| **ST[20]** | Parameter management | Read and change settings | ✅ |
| **ST[09]** | Time management | Sync the spacecraft clock | ✅ |
| **ST[11]** | Time-based scheduling | "Do this at 14:32:07" | ✅ |
| **ST[15]** | Storage and retrieval | Record telemetry, play it back later | ✅ |
| **ST[12]** | On-board monitoring | Watch a value, alarm if it strays | ✅ |
| **ST[19]** | Event-action | "When this happens, do that" | ✅ |
| ST[06] | Memory management | Read and patch memory | — |
| ST[13] | Large data transfer | Send something bigger than a packet | — |

ST[11] and ST[15] together are what make a satellite useful despite only being
in contact ten minutes a day: you queue up work during a pass, it happens while
you are out of contact, and you collect the results next time round.

## 👀 See it

Start your spacecraft (`make run`), then:

```bash
cd gnd
python3 -m pyground commands
```

```
COMMAND              PUS         ARGUMENTS
----------------------------------------------------------------------
ADJUST_TIME          ST[9,128]   delta_s:float64
DELETE_STORE_UP_TO   ST[15,11]   store_id:uint8, to_s:uint32
DISABLE_EVENT_ACTION ST[19,5]    event_id:uint16
DISABLE_HK           ST[3,6]     sid:uint8
DISABLE_MONITOR      ST[12,2]    monitor_id:uint8
DISABLE_SCHEDULE     ST[11,2]    -
DISABLE_STORAGE      ST[15,2]    store_id:uint8
ENABLE_EVENT_ACTION  ST[19,4]    event_id:uint16
ENABLE_HK            ST[3,5]     sid:uint8
ENABLE_MONITOR       ST[12,1]    monitor_id:uint8
ENABLE_SCHEDULE      ST[11,1]    -
ENABLE_STORAGE       ST[15,1]    store_id:uint8
INSERT_ACTIVITIES    ST[11,4]    -
REPORT_PARAM         ST[20,1]    param_id:uint16
REPORT_STORE_SUMMARY ST[15,12]   store_id:uint8
RESET_COUNTERS       ST[8,2]     -
RESET_SCHEDULE       ST[11,3]    -
RESTORE_WHEELS       ST[8,4]     mask:uint8
RETRIEVE_BY_TIME     ST[15,9]    store_id:uint8, from_s:uint32, to_s:uint32
SET_MODE             ST[8,1]     mode:uint8
SET_PARAM            ST[20,3]    param_id:uint16, value:float64
SET_TIME_REPORT_RATE ST[9,1]     rate_exp:uint8
SWITCH_RAIL          ST[8,3]     rail:uint8, on:uint8
TEST_CONNECTION      ST[17,1]    -
TEST_WATCHDOG        ST[17,128]  -
```

Every one of those service numbers means the same thing on a real ESA mission.
`TEST_WATCHDOG` (subtype 128, so this mission's own) is the odd one out:
Lesson 18 uses it to crash the computer on purpose.

## 💡 The extra header

A PUS packet adds a second header inside the CCSDS data field:

```
   ┌──────────────┬─────────────────┬──────────────┬──────┐
   │ CCSDS header │  PUS header     │  your data   │ CRC  │
   │   6 bytes    │  5 or 13 bytes  │              │  2   │
   └──────────────┴─────────────────┴──────────────┴──────┘
```

**Going up (a command), 5 bytes:** PUS version, acknowledgement flags, service,
subtype, source id.

**Coming down (telemetry), 13 bytes:** PUS version, time reference status,
service, subtype, message counter, destination, and a **timestamp**.

Telemetry is longer because it carries *when* it was produced. A measurement
without a time is much less useful than one with — especially when it arrives
in a batch played back hours later.

## 💡 A small piece of good design: acknowledgement flags

Four bits in the command header let the *sender* choose which replies it wants:

| Bit | Meaning |
|---|---|
| 1 | Tell me you received it |
| 2 | Tell me you started |
| 4 | Tell me about progress |
| 8 | Tell me you finished |

Why does this matter? Radio time is scarce. During a busy pass you might uplink
two hundred commands; if every one produced four reports, the downlink would
choke on chatter instead of carrying the science data you actually came for.

So the operator ticks the boxes that matter. Routine commands might ask for
nothing; a risky one asks for everything.

In [`fsw/apps/ttc/ttc_app.cpp`](../../fsw/apps/ttc/ttc_app.cpp) the spacecraft
honours that:

```cpp
if (tc.secondary.wants(kAckAcceptance)) {
    send_verification(tc, kVerifAcceptSuccess, core::FailureCode::Ok);
}
```

## 🧪 Try it — the timestamp

```bash
cd gnd
python3 -m pyground send TEST_CONNECTION
```

Look at the `t=` value on each reply. That is the spacecraft's own clock, in
**CUC** format — CCSDS Unsegmented Code: 4 bytes of whole seconds plus 2 bytes
of fraction in units of 1/65536 of a second, giving about 15 microseconds of
resolution.

Now notice something honest. In
[`fsw/apps/ttc/pus.cpp`](../../fsw/apps/ttc/pus.cpp):

```cpp
// Time reference status: 0 means "not synchronised with a ground clock",
// 1 that ST[9] correlation has been applied.
secondary.time_status   = time_status;
```

Every packet carries that small number, and until the ground sets the clock
it is **0**. The spacecraft is *telling you its clock has never been set*. It
is not pretending. A ground system needs to know whether a timestamp can be
trusted for correlating events across a mission. Quietly claiming accuracy
you do not have is how two datasets end up impossible to line up years
later.

## 🧪 Try it — set the spacecraft's clock

The spacecraft woke up thinking it was the very start of its calendar: zero
seconds since 1 January 2000. Ask it what time it thinks it is, every two
seconds (2 to the power `rate_exp`):

```bash
python3 -m pyground send SET_TIME_REPORT_RATE rate_exp=1
```

```
  t=     3.122  apid=0x001 seq=    7  TIME_REPORT        rate_exp=1  time_s=3.122
  t=     5.222  apid=0x001 seq=   10  TIME_REPORT        rate_exp=1  time_s=5.222
```

About three seconds into the year 2000. Now tell it the truth. From 1
January 2000 to 3 October 2026 is 844,300,800 seconds:

```bash
python3 -m pyground send ADJUST_TIME delta_s=844300800
```

```
  t=844300805.622  apid=0x001 seq=   15  EVENT_INFO         TIME_ADJUSTED aux=844300800
  t=844300805.622  apid=0x001 seq=   16  VERIF_COMPLETE_OK  tc(apid=0x00A, seq=0)
```

Watch the `t=` column jump 26.8 years in one packet. From that packet on,
the hidden time-status number in every header reads **1**: "my clock has been
set by the ground". (`ADJUST_TIME` is subtype 128. Numbers from 128 up are
left free by the standard for each mission's own additions, and this is one.)

## 🧪 Try it — tomorrow's work, today

Now the clock means something, you can ask the spacecraft to do something
*later*. Save this in `gnd/` as `later.py` (with `make run` still going, and
after setting the clock as above):

```python
import sys; sys.path.insert(0, '.')
from pyground import GroundClient
from pyground.packets import build_tc, schedule_data

with GroundClient() as g:
    report = g.wait_for("TIME_REPORT", timeout=5)
    now = report.fields["time_s"]
    print(f"spacecraft time is {now:.1f}")
    test = build_tc("TEST_CONNECTION", sequence_count=500)
    g.send("INSERT_ACTIVITIES", data=schedule_data([(now + 10, test)]))
    for tm in g.poll(timeout=13):
        if tm.name in ("VERIF_COMPLETE_OK", "TEST_REPORT") or tm.name.startswith("EVENT"):
            print(f"  t={tm.time_s - now:+6.2f} s  {tm.summary()}")
```

```
spacecraft time is 844300809.7
  t= +0.02 s  VERIF_COMPLETE_OK  tc(apid=0x00A, seq=0)
  t=+10.10 s  EVENT_INFO         SCHED_RELEASED aux=500
  t=+10.10 s  TEST_REPORT
  t=+10.10 s  VERIF_COMPLETE_OK  tc(apid=0x00A, seq=500)
```

The first line is the spacecraft accepting the *schedule*. Ten seconds later
it runs the stored command, completely by itself: `SCHED_RELEASED` names the
command by its sequence number (500), and then it executes exactly as if you
had just sent it.

Ten seconds is a toy. On a real mission it would be "switch the camera on as
we fly over Kenya, 47 minutes from now, when nobody on the ground can see
us". `make store-forward` does exactly that over two real passes of the
orbit, with the results recorded on board (ST[15]) and played back during
the next pass. [Lesson 12](../12-orbits/) explains where the passes come
from.

## 🔍 In the code

Open [`fsw/apps/ttc/pus.hpp`](../../fsw/apps/ttc/pus.hpp). The header comment
is a compact tour of the standard, and the service list is right there:

```cpp
enum class Service : uint8_t {
    Verification = 1,
    Housekeeping = 3,
    Event        = 5,
    Function     = 8,
    Test         = 17,
    Parameter    = 20,
};
```

Then in `ttc_app.cpp`, dispatch is a single switch — each service gets one
handler, and adding a service means adding one case:

```cpp
switch (static_cast<Service>(tc.secondary.service)) {
    case Service::Test:         result = svc_test(tc);         break;
    case Service::Housekeeping: result = svc_housekeeping(tc); break;
    case Service::Parameter:    result = svc_parameter(tc);    break;
    case Service::Function:     result = svc_function(tc);     break;
    default:                    result = FailureCode::UnknownService; break;
}
```

## ✅ Check yourself

1. What does `ST[5,4]` mean, and why can you answer that without knowing
   anything about this particular spacecraft?
2. Why is the telemetry PUS header longer than the command one?
3. Why let the sender choose which acknowledgements it gets?
4. The spacecraft reports its time reference status as 0. Why is that better
   than reporting 1?

## 🎓 Go deeper

Read [`../../docs/ICD.md`](../../docs/ICD.md) — the complete interface for this
spacecraft, generated from `dictionary/mission.yaml`. Compare its structure to
the ECSS standard and you will find the same shapes, because it *is* the same
standard.

---

**Next:** [Lesson 7 — Did it work?](../07-did-it-work/) — the service that
stops you flying blind.

<details>
<summary>✅ Answers</summary>

1. Service 5 is event reporting and subtype 4 is high severity: a serious alarm.
   You can answer it because PUS standardises the numbering across all
   missions — that is the entire point of the standard.
2. Because telemetry carries a timestamp (6 bytes) and a message type counter.
   A measurement without a time is far less useful, especially when it is
   played back hours after it was taken.
3. To protect the downlink budget. Two hundred commands each producing four
   reports would crowd out the data the pass exists to collect.
4. Because it is true. The clock has never been set from the ground, so
   timestamps are relative to an arbitrary boot time. Claiming synchronisation
   you do not have makes the data silently wrong rather than obviously limited.

</details>
