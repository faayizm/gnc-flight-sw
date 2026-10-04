# `fsw/apps/modemgr/` — spacecraft mode management

> 📚 **Learning this?** See [Lesson 17 — Power and modes](../../../learn/17-power-and-modes/) in the lesson track.

The single authority on what the spacecraft is currently doing. Every other
application follows the mode it announces, and none of them decides it.

| File | What |
|---|---|
| `mode_logic.hpp` | The rules, as pure functions: autonomous transitions, and how a ground request is judged |
| `mode_manager.hpp/.cpp` | The rules wired to the bus: facts in, `ModeChanged` out |

The state diagram is at the top of `mode_logic.hpp`; the full account,
including the scenario that drives a drained battery into SAFE, is in
[docs/POWER_AND_MODES.md](../../../docs/POWER_AND_MODES.md).

## Commitments, kept

- **Ground requests are requests, not orders.** TT&C publishes
  `Topic::ModeRequest` on ST[8,1]; the mode manager judges it against the same
  facts its own rules use, and a refusal raises `MODE_REFUSED` with the
  requested mode and the reason (`ModeRefusal`) in the auxiliary data.
- **Entry to SAFE is autonomous and always permitted. Leaving it is not.**
  Nothing in the autonomous rules leaves SAFE; only a ground request does,
  and only once the battery is back to `NOMINAL`.
- **Transitions are hysteretic.** Enter DETUMBLE above `DETUMBLE_RATE_DPS`;
  leave it below 80% of `POINTING_RATE_DPS`. Between the two, nothing changes.
- **Every transition raises `MODE_CHANGED`**, old mode in the high byte, new
  in the low. Entering SAFE also raises `SAFE_MODE_ENTERED` with the reason.

## Inputs and outputs

In: `Topic::ModeRequest`, `Topic::AdcsStatus` (rates, estimator, orbit),
`Topic::PowerStatus` (battery state), `Topic::UplinkActivity` (for the
contact timeout, `LINK_TIMEOUT_S`). Out: `Topic::ModeChanged`. No reference
to any other application.
