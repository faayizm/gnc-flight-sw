# Lesson 7 — Did it work?

🚀 **Explorer** · about 20 minutes

---

## ❓ The question

You send a command to a spacecraft. Nothing visible happens.

Did it arrive? Did it arrive damaged and get thrown away? Did it arrive fine
but ask for something impossible? Did it work perfectly and simply not produce
anything you can see?

You have no way to tell. And you have eight minutes of contact left.

## 💡 The idea

This is called **commanding blind**, and it is intolerable. So PUS service 1 —
**request verification** — exists to answer exactly this, in four messages:

| Report | Means |
|---|---|
| `ST[1,1]` | **Accepted.** Your message arrived, undamaged, and made sense |
| `ST[1,2]` | **Rejected.** It arrived but I could not accept it — here is why |
| `ST[1,7]` | **Completed.** I did it, successfully |
| `ST[1,8]` | **Failed.** I tried and could not — here is why |

Two questions, asked at two different times:

```
   command sent
        │
        ▼
   "did it ARRIVE?"     ──▶  ST[1,1] accepted   or   ST[1,2] rejected
        │
        ▼  (spacecraft tries to do it)
        │
   "did it WORK?"       ──▶  ST[1,7] completed  or   ST[1,8] failed
```

A command can perfectly well be **accepted and then fail**. "Set the
housekeeping period to 5 milliseconds" is a well-formed, undamaged, legal
message asking for something the spacecraft will refuse.

## 👀 See it

Start your spacecraft, then:

```bash
make demo
```

Watch the successful case:

```
=== connection test =========================================
  VERIF_ACCEPT_OK      tc(apid=0x00A, seq=0)
  TEST_REPORT
  VERIF_COMPLETE_OK    tc(apid=0x00A, seq=0)
```

Then the interesting ones:

```
=== try an illegal value ====================================
  VERIF_ACCEPT_OK      tc(apid=0x00A, seq=4)
  VERIF_COMPLETE_FAIL  tc(apid=0x00A, seq=4) reason=ILLEGAL_ARG

=== send an unknown service =================================
  VERIF_ACCEPT_FAIL    tc(apid=0x00A, seq=99) reason=UNKNOWN_SERVICE
```

Read the difference carefully. The first was **accepted then failed** — the
message was fine, the value was not. The second **failed acceptance** — the
spacecraft could not even work out what was being asked.

## 💡 Quoting back which command

Look at what every report contains:

```
  VERIF_COMPLETE_FAIL  tc(apid=0x00A, seq=4) reason=ILLEGAL_ARG
                          └──────┬──────┘
                     which command this is about
```

The APID and sequence count of the original command. That is the only
unambiguous way to say which one, because during a busy pass you might have
thirty commands in flight and the replies do not necessarily come back in
order.

## 💡 Saying *why*

`ILLEGAL_ARG` is far more useful than "failed". The failure codes are part of
the interface, listed in [`../../docs/ICD.md`](../../docs/ICD.md):

| Code | Meaning |
|---|---|
| 1 | `BAD_CRC` — arrived damaged |
| 2 | `BAD_LENGTH` — the wrong size |
| 3 | `UNKNOWN_SERVICE` — I do not implement that |
| 4 | `ILLEGAL_ARG` — a value was out of range |
| 5 | `UNAVAILABLE` — I could, but not right now |
| 6 | `REFUSED` — I understood, and I decline |

`UNAVAILABLE` and `REFUSED` are genuinely different, and the distinction
matters at 3 a.m. "Try again later" is not the same answer as "no, and trying
again will not help."

In [`fsw/core/status.hpp`](../../fsw/core/status.hpp) there is a comment worth
noticing:

```cpp
// Ground-visible failure reason, carried in PUS ST[1,2] and ST[1,8].
// The numbering is part of the interface: see docs/ICD.md. Never renumber.
```

Once a mission has flown, those numbers appear in years of archived telemetry
and in operations procedures people have memorised. Renumbering them silently
rewrites history.

## 🧪 Try it — the one command that gets no reply

Send a deliberately corrupted packet (spacecraft running in another terminal):

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

**No verification report at all.** This is the one exception, and the reasoning
is in [`ttc_app.cpp`](../../fsw/apps/ttc/ttc_app.cpp):

> A packet that failed its CRC cannot be answered with a verification report:
> its APID and sequence count are exactly the fields we would have to quote
> back, and they are not trustworthy.

If the packet is damaged, the *identity* of the command is damaged too. A
report would be about a command that may never have been sent, addressed to a
spacecraft address that may be fabricated. So instead the spacecraft raises an
**event** — "something arrived and it was broken" — which is a true statement
that requires no trust in the corrupted data.

## 🧪 Try it — asking for silence

Every command's header says which reports the sender wants. Send one that asks
for nothing:

```bash
cd gnd
python3 -c "
import sys, struct; sys.path.insert(0, '.')
from pyground import GroundClient
from pyground.packets import crc16

# Hand-built TEST_CONNECTION with ack flags = 0000: tell me nothing.
word0 = (1 << 12) | (1 << 11) | 0x00A
word1 = (0x3 << 14) | 1
p  = struct.pack('>HHH', word0, word1, 5 + 2 - 1)
p += struct.pack('>BBBH', 0x20, 17, 1, 0)   # 0x20 = version 2, ack 0
p += struct.pack('>H', crc16(p))

with GroundClient() as g:
    g.send_raw(p)
    for tm in g.poll(timeout=2.0):
        print(tm.summary())
"
```

You get the `TEST_REPORT` — but no acceptance and no completion. The command
ran; the spacecraft simply did not chatter about it, because you asked it not
to.

## ✅ Check yourself

1. Give an example of a command that is *accepted* and then *fails*.
2. Why does every verification report quote back an APID and sequence count?
3. Why is a corrupted packet the one thing that gets no verification report?
4. Why do `UNAVAILABLE` and `REFUSED` both exist?

## 🎓 Go deeper

**ST[1,3] and ST[1,5/6]** also exist in the standard: "execution started" and
progress reports, for long operations like a memory dump. This project does
not implement them, because nothing here takes long enough to need them yet.

**The deeper principle.** A system that fails silently is worse than one that
fails loudly, because a silent failure consumes the one thing an operator
cannot get more of: time. Every rejection path in this flight software either
produces a report or raises an event. There is no path where a command
disappears without trace.

---

**Next:** [Lesson 8 — Housekeeping and events](../08-housekeeping-and-events/)
— how a spacecraft tells you it is well.

<details>
<summary>✅ Answers</summary>

1. `SET_PARAM param_id=1 value=5` — a perfectly formed message, undamaged,
   addressing a service the spacecraft implements, asking for a value below the
   declared minimum of 100. Accepted, then refused with `ILLEGAL_ARG`.
2. Because replies do not necessarily arrive in the order commands were sent,
   and during a pass there may be many in flight. The APID plus sequence count
   is the only unambiguous identifier.
3. Because the report would have to quote the corrupted packet's own APID and
   sequence count, which cannot be trusted. An event is raised instead — a
   statement that needs no trust in the damaged data.
4. Because they call for different actions. `UNAVAILABLE` means "try later" —
   the spacecraft is busy or in the wrong mode. `REFUSED` means "no" — retrying
   will not help, and something about the request needs to change.

</details>
