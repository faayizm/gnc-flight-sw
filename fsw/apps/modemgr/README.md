# `fsw/apps/modemgr/` — spacecraft mode management

> 📚 **Learning this?** See [Lesson 17 — Power and modes](../../../learn/17-power-and-modes/) in the lesson track.


**Not yet implemented. Phase 5.** This directory is a placeholder with a
documented design, so that the interfaces it will need are visible now rather
than being retrofitted later.

## What will live here

The single authority on what the spacecraft is currently doing. Every other
application asks it, and no other application decides.

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

## Design commitments already made

- **Ground requests are requests, not orders.** TT&C publishes
  `Topic::ModeRequest` on ST[8,1]; the mode manager may refuse, and a refusal
  raises `MODE_REFUSED` so the operator learns why. TT&C has no vote.
- **Entry to SAFE is autonomous and always permitted.** Leaving it is not.
- **Transitions are hysteretic.** The thresholds for entering and leaving a
  mode differ, taken from `DETUMBLE_RATE_DPS` and `POINTING_RATE_DPS` in the
  dictionary, so that a value hovering on a boundary cannot induce a mode
  oscillation.
- **Every transition raises `MODE_CHANGED`**, with the old and new modes in the
  auxiliary data. The mode history is often the only usable record of what a
  spacecraft was thinking.

## Dependencies it will take

`Topic::ModeRequest` (in), `Topic::AdcsHk` and `Topic::EpsHk` (in),
`Topic::ModeChanged` (out), the parameter store for thresholds, the event log.
Nothing else — in particular, no direct reference to any other application.
