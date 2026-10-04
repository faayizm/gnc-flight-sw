# Power and modes

How HYPERSAT keeps its battery alive and decides what it is doing. The code
is in [`fsw/apps/eps/`](../fsw/apps/eps/) and
[`fsw/apps/modemgr/`](../fsw/apps/modemgr/). The scenario that exercises all
of it is [`sim/scenarios/power_and_modes.py`](../sim/scenarios/power_and_modes.py)
(`make power`). The teaching version is
[Lesson 17](../learn/17-power-and-modes/).

## Who decides what

```
            SensorData                          PowerStatus
   io ──────────────────▶ adcs ─── AdcsStatus ──▶ modemgr ◀── eps
    ▲          │                                    │  ▲         │
    │          └──────────▶ eps                     │  │         │
    │                                   ModeChanged │  │ ModeRequest
    │  ActuatorCommand (adcs)                       ▼  │
    └─ PowerStatus (eps: rails)       adcs, eps, ttc   ttc (ST[8,1])
```

- **EPS** knows the battery. It decides the power state, how much load to
  shed, and which rails should be on.
- **ADCS** knows the attitude. It reports rates, estimator state and whether
  a position is known.
- **The mode manager** turns those facts into the mode, the one thing every
  other application follows.
- **TT&C** carries ground requests and decides nothing. That was promised in
  Phase 1, before any of this existed.

## The battery and the arrays

A two-cell lithium-ion pack, 30 Wh. A deployed panel faces zenith, plus
panels on the four side faces. Pointed at nadir, the spacecraft generates
about 22 W at orbit noon and nothing in eclipse. Its housekeeping loads are
about 8 W, and the payload adds 6 W more.

The state of charge is counted from voltage times current. It is anchored to
the open-circuit voltage at boot and pulled gently toward it afterwards, with
a one-hour time constant. Counting alone would drift; voltage alone sags
under load. In the scenario the estimate stays within about 0.3 percentage
points of the truth.

## Power states and load shedding

| Power state | Enter | Leave | Shed level |
|---|---|---|---|
| NOMINAL | | | 0: nothing |
| LOW | SOC < `BATT_LOW_SOC_PCT` (40) | SOC > 50 | 1: payload; 2 below the midpoint (30): transmitter, except for 10 minutes after the ground is heard |
| CRITICAL | SOC < `BATT_CRIT_SOC_PCT` (20) | SOC > 30 | 3: operational heaters |
| (SAFE mode) | | | 4: reaction wheels |

The computer, the receiver and the survival heaters are never shed. The
attitude sensors and magnetorquers stay on too, because SAFE needs them.

Ground rail requests (`SWITCH_RAIL`) record what the operator wants. The
policy decides whether they get it, and remembers the wish for when it can.

## Modes

The state diagram and every rule are at the top of
[`mode_logic.hpp`](../fsw/apps/modemgr/mode_logic.hpp). In short:

- **BOOT** waits for the first rates, then goes to DETUMBLE or STANDBY.
- **DETUMBLE ⇄ STANDBY** on the body rate, with a dead band between
  2.0 °/s and 0.4 °/s.
- **STANDBY → POINTING** once the attitude is converged, a position is known
  and power is NOMINAL.
- **POINTING → STANDBY** if the attitude is lost.
- **Any mode → SAFE** on CRITICAL power, or on no word from the ground for
  `LINK_TIMEOUT_S` (a day).
- **SAFE has no autonomous exit.**

Ground requests are judged against the same facts. A refusal raises
`MODE_REFUSED` with the reason: `RATES_HIGH`, `ATTITUDE_UNKNOWN`, `POWER`,
`NOT_FROM_MODE` or `INVALID`.

What each mode means to the actuators:

| Mode | ADCS | Wheels | Payload |
|---|---|---|---|
| BOOT, STANDBY | estimating, no control | on (STANDBY) | off |
| DETUMBLE | B-dot | off | off |
| POINTING | nadir pointing, momentum dumping | on | allowed |
| SAFE | B-dot | off | off |

## The scenario: a stuck heater

`make power` flies just over three orbits. A heater thermostat sticks closed
part-way through the first, drawing 12 W the flight software knows nothing
about.

| Time | What happens |
|---|---|
| 0–1000 s | BOOT → DETUMBLE → STANDBY → POINTING, unaided. A STANDBY request sent while still tumbling is refused: `RATES_HIGH` |
| 1200 s | The operator switches the payload on |
| 2500 s | The heater sticks. Into eclipse at 26 W of load |
| ~4200 s | LOW: payload shed |
| ~4700 s | Below the midpoint: transmitter off between contacts |
| ~5500 s | CRITICAL: operational heaters shed, which happens to remove the fault. Then SAFE: wheels off, B-dot only |
| next contact | Leaving SAFE while the battery is low is refused: `POWER` |
| ~12000 s | Battery NOMINAL. The spacecraft stays in SAFE |
| a contact later | The operator asks for POINTING (refused: `NOT_FROM_MODE`), leaves the heater rail off, and asks for STANDBY. The mode manager takes it to POINTING by itself |

The lowest true charge is about 19.6%. While the transmitter was shed,
nothing at all came down between contacts, and every contact woke it. The
assertions about what the spacecraft decided are made against its own
record: the whole flight, replayed from its packet store at the end.

## Two clocks, and which one each decision uses

The flight software has its own clock, and in the simulator it runs at
`--time-scale` times the wall clock. The simulator runs in lockstep, as fast
as the host allows, so its time and the flight clock drift apart by whatever
factor the host happens to run at. Decisions about the physics therefore use
the sensor samples' timestamps, as a real spacecraft would use GPS time. That
covers ADCS's dynamics, EPS's charge counting, and the transmitter's
10-minute hold after contact.

The mode manager's rules run once per sensor sample for the same reason, so a
mode change always lands on the same sample. The flight clock is kept for
things that must work with no sensors at all: detecting a dead sensor stream,
and the mode manager's day-long contact timeout.

Both mistakes were made first and caught by the scenarios:

- The transmitter hold was on the flight clock. At 50× it outlasted the gap
  between contacts, and the transmitter was never shed.
- The mode rules ran in a 10 Hz task on the flight clock. Mode changes then
  landed on different samples at different time scales, and the detumble
  scenario's determinism check failed.

## Simplifications

- **SAFE damps rates; it does not point the arrays at the Sun.** See
  [ARCHITECTURE.md](ARCHITECTURE.md).
- **The battery has no thermal model.** Temperature is reported but constant,
  and there is no ageing.
- **The transmitter rail cuts the downlink, but not the simulator's own
  radio link model.** Those are separate scenarios.
