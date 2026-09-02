# The toolbox

Small standalone programs used by the lessons. They need **only Python** — no
spacecraft, no build, no installation, no libraries.

Run any of them from the repository root:

```bash
python3 learn/toolbox/byte_order.py
```

| Program | What it shows | Lessons |
|---|---|---|
| `byte_order.py` | The same number written two ways, and the disaster when the two ends disagree | 3 |
| `crc_playground.py` | Damage a message and watch the checksum catch it, 20,000 times over | 4 |
| `packet_explorer.py` | A real packet taken apart byte by byte, with a bit diagram generated from the actual bytes | 5, 6 |
| `orbit_sandbox.py` | How fast you must go to stay in orbit, and an orbit integrated from Newton's law alone | 12 |
| `spin_sandbox.py` | Why a tumbling satellite keeps tumbling, and how a magnet stops it | 13, 16 |

## `packet_explorer.py --live`

With a spacecraft running (`make run` in another terminal), this one grabs a
**real packet off the socket** and explains it field by field, instead of using
a constructed example.

## They are meant to be edited

Every program ends with a **Try this** section suggesting changes. That is the
point — these are not demonstrations to watch, they are things to break.

Some of the more interesting ones:

- In `orbit_sandbox.py`, change the integration timestep from `1.0` to `60.0`
  and watch a circular orbit spiral. Nothing about the physics changed; only
  the size of the steps. This is the most important question in simulation.
- In `spin_sandbox.py`, set `orbit_rate = 0` so Earth's magnetic field never
  moves, and watch the detumble fail to finish. Then work out *which* component
  of the spin survives, and why. (Hint: what can a cross product never
  produce?)
- In `crc_playground.py`, flip two bits instead of one. Then try to find two
  different messages with the same checksum — they must exist, since there are
  only 65,536 possible values. Why does that not worry spacecraft engineers?

## Accuracy

Every number printed by these programs was checked. `orbit_sandbox.py`
reproduces the Space Station's 92-minute period and geostationary orbit's
23.9 hours; its numerical integrator agrees with the analytic two-body solution
to the nearest kilometre. `crc_playground.py` produces the standard check value
`0x29B1`, the same constant asserted in the C++ flight software's unit tests.
`spin_sandbox.py` integrates Euler's equation and the attitude kinematics
properly — an earlier version cut a corner there and made the satellite spin
*up* instead of down, which is noted in a comment in the file as a warning.
