# `fsw/apps/eps/` — electrical power

> 📚 **Learning this?** See [Lesson 17 — Power and modes](../../../learn/17-power-and-modes/) in the lesson track.

| File | What |
|---|---|
| `power_policy.hpp` | Every power decision as a pure function: state of charge, power state with hysteresis, shed level, which rails are on |
| `eps_app.hpp/.cpp` | The policy wired to the bus |

Battery and array telemetry arrive in `Topic::SensorData`. Out go
`Topic::PowerStatus` (state, shed level, the rails that should be on, which
the I/O application turns into switch commands) and `EPS_HK`.

The ground switches rails with ST[8,3] `SWITCH_RAIL`, but that only records
what the operator *wants*. The policy can still overrule it, and the wish is
remembered: a payload switched off by load shedding comes back by itself when
the battery recovers.

See [docs/POWER_AND_MODES.md](../../../docs/POWER_AND_MODES.md).
