# Lesson 17 — Power and modes

🔧 **Builder** · about 25 minutes

> **Phase 5 note.** EPS and the mode manager are Phase 5. The design is written
> down and the interfaces already exist — `EPS_HK`, `SET_MODE`, and the
> thresholds in the dictionary are all in place and reporting today.

---

## ❓ The question

Your satellite is in eclipse, the battery is at 18%, the camera wants to run,
and the next ground contact is in 70 minutes.

Nobody is going to tell it what to do. What should it decide?

## 💡 The power problem

A satellite's whole energy budget comes from sunlight, and for roughly a third
of every orbit there is none.

```
   ┌───────── one orbit, about 96 minutes ─────────┐

   ██████████████████████████░░░░░░░░░░░░░░░░░░░░░░
   |◀──── sunlight, ~60 min ────▶|◀─ eclipse, ~35 min ─▶|
        charging + running            battery only
```

Sixteen times a day, every day, for years. The battery is charged and
discharged about 30,000 times over a five-year mission, and every deep discharge
shortens its life.

So the flight software is doing energy accounting continuously: how much is
coming in, how much is going out, how long until sunrise, and what can be
switched off if the sums do not work.

`EPS_HK` already declares the numbers it will report:

```bash
make monitor
```

```
  EPS_HK  power_state=UNKNOWN  batt_voltage=0  batt_current=0  batt_soc_pct=0
```

All zeros — Phase 5 — but the shape of the answer is already fixed:
`batt_soc_pct` (state of charge), `solar_power_w`, `load_power_w`,
`rails_enabled` as a bitmask, and `shed_level`.

## 💡 Load shedding

When the sums do not work, something has to be switched off. In order of
increasing desperation:

| Level | What goes | Why it can go |
|---|---|---|
| 0 | nothing | normal |
| 1 | the payload | the mission can wait; the spacecraft cannot |
| 2 | the transmitter between passes | it is the biggest single load |
| 3 | non-essential heaters | tolerate the cold for a while |
| 4 | everything except the computer, the receiver and survival heaters | stay alive |

Level 4 is the floor. You always keep the **receiver** — because if the ground
cannot command you, nobody can help. And you always keep **survival heaters**,
because a frozen battery is a permanently dead battery.

Notice the ordering principle: **sacrifice the mission to save the
spacecraft.** A satellite with no science data is disappointing. A satellite
with a dead battery is scrap.

## 💡 Modes: one place where the decision lives

A spacecraft could try to make all these decisions locally — power decides
about power, attitude decides about attitude. That way lies chaos, because the
decisions interact: pointing at the Sun charges the battery but not at the
target; running the transmitter costs power but is the only way to get data
down.

So there is exactly **one** authority on what the spacecraft is currently
doing, called the **mode**.

```
        ┌────────┐  initialisation complete
        │  BOOT  │──────────────┐
        └────────┘              ▼
                          ┌───────────┐  rates high    ┌────────────┐
                    ┌────▶│  STANDBY  │───────────────▶│  DETUMBLE  │
                    │     └───────────┘                └────────────┘
      rates low and │           │  attitude known            │
      power nominal │           ▼      and power nominal     │ rates low
                    │     ┌───────────┐                      │
                    └─────│  POINTING │◀─────────────────────┘
                          └───────────┘
                                │
             any of: power critical, attitude lost,
             persistent fault, ground contact timeout
                                ▼
                          ┌───────────┐
                          │   SAFE    │  minimum power, sun-pointing,
                          └───────────┘  ground intervention to leave
```

Those are the five modes in
[`dictionary/mission.yaml`](../../dictionary/mission.yaml) today, and you can
see the current one in telemetry right now — it reads `BOOT`, honestly, because
the mode manager does not exist yet.

## 💡 Safe mode: the one that saves missions

**SAFE** is the mode a spacecraft enters when it does not know what else to do.
It has one job: survive, and stay reachable, indefinitely.

- point the solar panels at the Sun and nothing else
- switch off everything non-essential
- keep the receiver on
- transmit a minimal beacon
- **wait for a human**

And here is the crucial asymmetry:

> **Entry to SAFE is autonomous and always permitted. Leaving it is not.**

The spacecraft may put itself into safe mode any time, for any reason, without
asking. But it can never take itself out — that requires a ground command, from
someone who has looked at the telemetry and understands what happened.

Why so one-sided? Because whatever drove it into safe mode is probably still
there. A spacecraft that recovers itself, hits the same fault, recovers again,
and loops, will exhaust its battery doing so. Many missions have been saved by
safe mode. Some have been lost by systems that were too eager to leave it.

## 💡 Hysteresis: not flapping

Here is a subtle and important detail. Suppose the rule is "enter DETUMBLE if
the spin rate is above 2 °/s". What happens when the rate sits at exactly
2.0 °/s and jitters?

```
   2.01 → enter DETUMBLE
   1.99 → leave DETUMBLE
   2.01 → enter DETUMBLE
   ... sixteen times a second, forever
```

Every transition costs power, raises an event, and flooding the event log with
mode changes destroys the record you would need to diagnose anything.

The fix is **hysteresis**: different thresholds for entering and leaving. Which
is exactly why the dictionary has *two* parameters, not one:

```yaml
- {name: DETUMBLE_RATE_DPS, id: 4, default: 2.0,  units: deg/s,
   desc: Rate threshold above which detumble is commanded}
- {name: POINTING_RATE_DPS, id: 5, default: 0.5,  units: deg/s,
   desc: Rate threshold below which pointing is permitted}
```

Enter detumble above 2.0. Do not allow pointing until below 0.5. Between them
is a dead band where nothing changes, and the rate has to move decisively to
cause a transition.

The same pattern appears for power: `BATT_LOW_SOC_PCT` at 40% and
`BATT_CRIT_SOC_PCT` at 20%.

## 💡 Ground requests are requests

The ground can *ask* for a mode with `ST[8,1]`:

```bash
cd gnd
python3 -m pyground send SET_MODE mode=POINTING
```

Look at what the flight software does with that today, in
[`ttc_app.cpp`](../../fsw/apps/ttc/ttc_app.cpp):

```cpp
// Publish the request and let the mode manager arbitrate. TT&C has no
// business deciding whether a mode change is safe -- it only carries
// the request. The refusal, if any, arrives back as an event.
bus_.publish(mode_topic_, &args.mode, sizeof(args.mode));
```

The communications software does not decide. It publishes the request on the
software bus and the mode manager arbitrates — and may say no, raising
`MODE_REFUSED` so the operator learns why.

That matters because an operator on the ground is working from telemetry that
is at best seconds old and possibly an orbit old. The spacecraft knows its
current state better than they do. "Point at this target" is a reasonable
request; if the battery has since dropped to 15%, the reasonable answer is no.

## 🧪 Try it

Send a mode request now:

```bash
cd gnd
python3 -m pyground send SET_MODE mode=POINTING
```

```
  VERIF_ACCEPT_OK      tc(apid=0x00A, seq=0)
  VERIF_COMPLETE_OK    tc(apid=0x00A, seq=0)
```

The command was accepted and published to the bus — and nothing subscribed to
it, because the mode manager does not exist yet. Watch `SYS_HK`: `mode=BOOT`,
unchanged.

That is worth noticing rather than glossing over. Publishing into the void is
*not* an error in this design, and there is a test asserting it:

```bash
./build/tests/fsw_tests 2>&1 | grep nobody
```

```
  .  publishing_to_a_topic_nobody_listens_to_is_not_an_error
```

When the mode manager lands in Phase 5, it subscribes to that topic and starts
answering. Not one line of the TT&C code changes.

## ✅ Check yourself

1. Why does load shedding switch off the payload before the receiver?
2. Why is entering safe mode autonomous but leaving it not?
3. Why does the dictionary have two rate thresholds instead of one?
4. Why does the communications software publish a mode request instead of
   applying it?

## 🎓 Go deeper

**Energy balance over an orbit** is the sizing calculation for the whole
spacecraft: average generated power must exceed average consumed power, with
margin, across the worst-case eclipse. Get it wrong and no amount of clever
software helps.

**Depth of discharge and battery life.** Lithium-ion cells last far longer if
kept between roughly 20% and 80% charge. A mission that routinely drains to 5%
may lose years of life. So `BATT_LOW_SOC_PCT` is not an arbitrary number — it
is a decision about how long you want the spacecraft to survive.

---

**Next:** [Lesson 18 — When things break](../18-when-things-break/) — the last
lesson, and the one that matters most.

<details>
<summary>✅ Answers</summary>

1. Because you sacrifice the mission to save the spacecraft. Without a payload
   you collect no data; without a receiver nobody can command you, and the
   spacecraft is lost permanently rather than temporarily unproductive.
2. Because whatever caused safe mode is probably still present. Automatic
   recovery risks a loop — recover, fail, recover, fail — that drains the
   battery. Leaving requires a human who has looked at the telemetry.
3. For hysteresis. With one threshold, a value sitting on it causes the mode to
   flap back and forth many times a second, wasting power and destroying the
   event log. Two thresholds create a dead band.
4. Because deciding whether a mode change is safe requires knowing about
   attitude, power and faults — none of which is the communications software's
   business. Concentrating that judgement in one place is what stops the
   decisions contradicting each other.

</details>
