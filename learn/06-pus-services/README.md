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

There are about twenty in the standard. This spacecraft implements five, and
the roadmap adds more:

| Service | Name | What it is for | Here? |
|---|---|---|---|
| **ST[01]** | Request verification | "Did my command arrive? Did it work?" | ✅ |
| **ST[03]** | Housekeeping | Periodic health reports | ✅ |
| **ST[05]** | Event reporting | "Something happened" | ✅ |
| **ST[08]** | Function management | Do a mission-specific thing | ✅ partly |
| **ST[17]** | Test | A safe are-you-there check | ✅ |
| **ST[20]** | Parameter management | Read and change settings | ✅ |
| ST[09] | Time management | Sync the spacecraft clock | Phase 4 |
| ST[11] | Time-based scheduling | "Do this at 14:32:07" | Phase 4 |
| ST[12] | On-board monitoring | Watch a value, alarm if it strays | Phase 6 |
| ST[15] | Storage and retrieval | Record telemetry, play it back later | Phase 4 |
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
COMMAND              PUS        ARGUMENTS
----------------------------------------------------------------------
DISABLE_HK           ST[3,6]   sid:uint8
ENABLE_HK            ST[3,5]   sid:uint8
REPORT_PARAM         ST[20,1]  param_id:uint16
RESET_COUNTERS       ST[8,2]   -
SET_MODE             ST[8,1]   mode:uint8
SET_PARAM            ST[20,3]  param_id:uint16, value:float64
TEST_CONNECTION      ST[17,1]  -
```

Every one of those service numbers means the same thing on a real ESA mission.

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
// Time reference status 0 means "not synchronised with a ground clock".
secondary.time_status = 0;
```

The spacecraft is *telling you its clock has never been set*. It is not
pretending. A ground system needs to know whether a timestamp can be trusted
for correlating events across a mission, and quietly claiming accuracy you do
not have is how two datasets end up impossible to line up years later.

Fixing that is ST[09], in Phase 4.

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
