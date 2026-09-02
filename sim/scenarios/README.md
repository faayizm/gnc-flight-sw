# `sim/scenarios/` — reproducible test cases

**Not yet implemented. Phase 2.**

One file per scenario. Each declares an initial state, a duration, a random
seed, any faults to inject, and the assertions that must hold. They run in CI,
so a regression in an estimator or a control law fails a build rather than
being noticed weeks later.

## The shape of a scenario

```python
SCENARIO = Scenario(
    name="detumble",
    description="B-dot control removes an initial tumble.",
    seed=20260901,                       # every run is identical
    duration_s=3000,
    initial=State(
        orbit=SunSynchronous(altitude_km=550, ltan_h=10.5),
        attitude=Random(),
        body_rate_dps=(8.0, -6.0, 4.0),
    ),
    asserts=[
        RateBelow(0.5, by_time_s=2400),  # detumbled within 40 minutes
        NoEventAbove(Severity.MEDIUM),   # and without alarming anybody
        WheelMomentumBelow(0.9),         # with margin left
    ],
)
```

## Rules

- **Every scenario declares a seed.** Two runs must be bit-identical.
- **Assertions are about behaviour, not implementation.** "Detumbled within
  forty minutes" survives a rewrite of the controller; "the gain equalled 3.2"
  does not.
- **A scenario that has ever caught a real bug is kept forever**, with a
  comment saying what it caught.
