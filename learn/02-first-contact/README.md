# Lesson 2 — First contact

🚀 **Explorer** · you will need a computer · about 20 minutes

---

## ❓ The question

Enough theory. Can we actually talk to a spacecraft?

Yes. Right now. On your own machine, using exactly the same message format that
real satellites use.

## 💡 The idea

You are going to run two programs:

```
  ┌─────────────────────┐              ┌─────────────────────┐
  │   THE SPACECRAFT    │◀────────────▶│   THE GROUND        │
  │                     │   messages   │   STATION           │
  │   ./build/fsw       │              │   pyground          │
  │                     │              │                     │
  │  terminal 1         │              │  terminal 2         │
  └─────────────────────┘              └─────────────────────┘
```

The spacecraft is real flight software — the same C++ that could be
cross-compiled and put on a satellite. The ground station is what mission
control uses to talk to it. The messages between them are real CCSDS packets.

Nothing here is pretend except the distance.

## 👀 See it

**Set up once:**

```bash
git clone https://github.com/faayizm/gnc-flight-sw
cd gnc-flight-sw
make build
```

You should see `72 passed, 0 failed`.

**Now launch your spacecraft.** In terminal 1:

```bash
make run
```

```
HYPERSAT flight software up.
  TT&C link   : TCP 127.0.0.1:50001 (waiting for the ground)
  base rate   : 50 Hz
  time scale  : 1.00x
  tasks       : 2 registered
  parameters  : 8
t=    1s  link=DOWN  tc=0/0  tm=0  load=0%  overruns=0
t=    2s  link=DOWN  tc=0/0  tm=0  load=0%  overruns=0
```

Your spacecraft is alive. It is running its control loop **fifty times a
second**, and it is waiting for someone to call. `link=DOWN` means no ground
station is connected yet.

**Now be the ground station.** Open terminal 2, go to the same folder:

```bash
make monitor
```

Watch terminal 1. `link=DOWN` becomes `link=UP`. You just made contact.

And in terminal 2, telemetry starts flowing:

```
  t=     6.044  apid=0x001 seq=    7  EVENT_INFO   LINK_CONNECTED aux=0
  t=     6.525  apid=0x001 seq=   11  SYS_HK   uptime_s=6  tick_count=326  mode=BOOT
  t=     6.525  apid=0x002 seq=    6  ADCS_HK  est_state=INVALID  q_est_0=0
  t=     6.525  apid=0x003 seq=    6  EPS_HK   power_state=UNKNOWN  batt_voltage=0
```

The spacecraft is reporting on itself once a second, without being asked.
That is **housekeeping telemetry**, and every satellite does it.

Read one line: `uptime_s=6` means it has been alive six seconds. `mode=BOOT`
means it is still in its start-up mode.

## 🧪 Try it — say something

Stop the monitor with `Ctrl-C`. Now **send a command**:

```bash
cd gnd
python3 -m pyground send TEST_CONNECTION
```

```
uplinked TEST_CONNECTION

  t=  12.345  VERIF_ACCEPT_OK    tc(apid=0x00A, seq=0)
  t=  12.345  TEST_REPORT
  t=  12.345  VERIF_COMPLETE_OK  tc(apid=0x00A, seq=0)
```

Three replies to one command. Read them in order — this is a small conversation:

| Reply | What the spacecraft is saying |
|---|---|
| `VERIF_ACCEPT_OK` | "I got your message and it made sense." |
| `TEST_REPORT` | "Here is the answer you asked for." |
| `VERIF_COMPLETE_OK` | "I have finished doing it, successfully." |

That is **ST[17,1]**, the connection test, and it is the first thing a real
operator sends at the start of a pass. It changes nothing on the spacecraft —
which is exactly why it is safe to send at any time. It only proves the whole
chain works: your keyboard, the uplink, the flight software, the downlink, your
screen.

## 🧪 Try it — break something

Now send a command that does not exist:

```bash
python3 -m pyground send FLY_TO_MARS
```

```
error: unknown telecommand 'FLY_TO_MARS'; known: DISABLE_HK, ENABLE_HK, ...
```

The ground station stopped you before anything was transmitted. Good — radio
time is precious and you should not waste it sending nonsense.

But what if a real corrupted message *did* arrive? Try the whole scripted pass:

```bash
cd /path/to/gnc-flight-sw
make demo
```

Watch for these two sections:

```
=== try an illegal value ====================================
  VERIF_ACCEPT_OK      tc(apid=0x00A, seq=4)
  VERIF_COMPLETE_FAIL  tc(apid=0x00A, seq=4) reason=ILLEGAL_ARG

=== send an unknown service =================================
  VERIF_ACCEPT_FAIL    tc(apid=0x00A, seq=99) reason=UNKNOWN_SERVICE
  EVENT_LOW            TC_REJECTED aux=UNKNOWN_SERVICE
```

The spacecraft refused both — and told you exactly **why**. Notice the
difference between the two:

- The illegal value was *accepted* (the message was well formed) but *failed to
  execute* (the number was out of range).
- The unknown service failed *acceptance* — the spacecraft could not even work
  out what was being asked.

A spacecraft that silently ignores a bad command is a spacecraft that will one
day leave an operator wondering for hours why nothing happened.

## 🔍 In the code

| What you saw | Where it lives |
|---|---|
| The spacecraft starting up | [`fsw/main.cpp`](../../fsw/main.cpp) |
| The 50 Hz loop | [`fsw/core/scheduler.cpp`](../../fsw/core/scheduler.cpp) |
| Commands being checked and answered | [`fsw/apps/ttc/ttc_app.cpp`](../../fsw/apps/ttc/ttc_app.cpp) |
| The ground station | [`gnd/pyground/client.py`](../../gnd/pyground/client.py) |
| The list of every command | [`dictionary/mission.yaml`](../../dictionary/mission.yaml) |

## ✅ Check yourself

1. Why does the spacecraft send *three* replies to one command?
2. `TEST_CONNECTION` changes nothing on the spacecraft. Why is that useful?
3. What is the difference between failing *acceptance* and failing
   *completion*?
4. In `make run`, `load=0%` is the processor load. Why does a spacecraft
   engineer care about that number?

## 🎓 Go deeper

Run `python3 -m pyground commands` for the full command list, then open
[`../../docs/ICD.md`](../../docs/ICD.md) — the Interface Control Document, the
formal specification of every message. It was generated automatically from
`dictionary/mission.yaml`, which is why it cannot be out of date.

---

**Next:** [Lesson 3 — Bytes and numbers](../03-bytes-and-numbers/) — what those
messages are actually made of.

<details>
<summary>✅ Answers</summary>

1. Because they answer different questions at different times. "Did it arrive?"
   is answered immediately; "did it work?" can only be answered after the
   spacecraft has tried. A command might be accepted and then still fail.
2. Because it is *safe*. You can send it in any mode, at any time, without
   changing the spacecraft's state — so it isolates the question "does the
   communications chain work?" from every other question.
3. Acceptance means the message was well formed, undamaged, and addressed
   something the spacecraft implements. Completion means the requested action
   actually succeeded. A perfectly valid command asking for an impossible value
   passes the first and fails the second.
4. Because it is the margin. A processor at 0% has room for the worst case that
   has not happened yet; one at 95% will miss deadlines the first time
   something takes longer than usual. See Lesson 10.

</details>
