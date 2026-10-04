# `fsw/apps/fdir/` — fault detection, isolation and recovery

Faults that need a view across applications, or a decision bigger than any
one of them. Local faults stay local: the I/O application screens its own
sensors, ADCS restarts its own estimator, EPS sheds its own load.
[docs/FDIR.md](../../../docs/FDIR.md) is the full map of which fault is handled
where; [Lesson 18](../../../learn/18-when-things-break/) teaches it.

| File | What |
|---|---|
| `wheel_check.hpp` | Is each reaction wheel delivering its commanded torque? Measured momentum change against commanded, over 5 s windows. Pure arithmetic |
| `wheel_ladder.hpp` | What to do about it: report, power-cycle retry, isolate. Pure logic, driven by time and the check's verdicts |
| `monitoring.hpp` | PUS ST[12]: housekeeping limits declared in the dictionary, with repetition filtering in both directions |
| `fdir_app.hpp/.cpp` | All of the above on the bus; scrubbing the EDAC-protected parameter table; routing the simulator's memory upsets |

Everything here runs once per sensor sample, never on a timer, so a flight is
reproducible whatever the host is doing: whether a scrub reaches a word
before a second upset does must not depend on how busy the laptop was.

What FDIR decides, it publishes. ADCS learns which wheels it may use from
`WheelHealth`, EPS which rails to hold off from `FdirRails`, and the mode
manager whether pointing is still possible. FDIR never switches a mode
itself: SAFE belongs to the mode manager, reached either by its own rule
(fewer than two wheels) or by an ST[19] event-action, which is an ordinary
telecommand.
