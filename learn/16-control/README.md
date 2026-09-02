# Lesson 16 — Control

🔧 **Builder** · 🎓 Engineer · about 30 minutes

> **Phase 2–3 note.** B-dot detumble is Phase 2; quaternion feedback pointing is
> Phase 3. The sandbox here runs today and is real physics.

---

## ❓ The question

Lesson 15 worked out which way the spacecraft is facing. Now make it face
somewhere else.

With no fuel, nothing to push against, and no way to touch it.

## 💡 What you have to push with

A spacecraft in orbit has nothing to lean on. No air, no ground. So it uses two
tricks, and they are completely different in character.

### Magnetorquers — push against the planet

Earth is a giant magnet and its field reaches into orbit. Put an electromagnet
on board, run current through it, and it feels a twisting force:

```
        torque  =  m × B          m = your coil's magnetic moment
                                  B = Earth's field where you are
```

No fuel. No moving parts. Just electricity and the planet. A coil the size of a
pencil can detumble a small satellite in about an hour.

**The catch**, and it is fundamental: a cross product is always *perpendicular*
to both inputs. You can never produce torque along the direction of **B**. You
control two axes, not three, at any instant.

It works out because **B** keeps changing direction as you go round the orbit —
but it means magnetic control is slow, taking an orbit rather than a minute.

### Reaction wheels — push against yourself

A heavy wheel inside the spacecraft, on a motor. Spin the wheel one way and the
spacecraft turns the other. Conservation of angular momentum, exactly like a
spinning office chair.

Fast, precise, and it works about all three axes. It is how a telescope points.

**The catch** is different and more interesting. You have not removed angular
momentum — you have *moved* it into the wheel. Correct for a disturbance in one
direction long enough and the wheel spins faster and faster until it reaches
its limit. That is **saturation**, and a saturated wheel is a dead actuator.

So you periodically **dump momentum**: use the magnetorquers to push against
Earth while slowing the wheel, transferring the momentum out of the spacecraft
altogether. The two actuators are partners, not alternatives.

## 💡 The first control law: B-dot

Straight after separation the spacecraft is tumbling, has no idea which way it
is facing, and has no ground contact. It needs a controller that requires
almost nothing.

B-dot is that controller, and it is one line:

```
        m  =  −k · dB/dt
```

That is it. "B-dot" is engineering shorthand for the time derivative of **B**.

**Why it works:** if you are spinning, the measured magnetic field appears to
sweep around in your body frame. Push against that apparent motion and you
oppose the spin. You do not need to know your attitude, your orbit, or the
time. You need a magnetometer and the ability to subtract.

It is the first thing that runs on a new satellite, precisely because it needs
so little.

## 👀 See it

```bash
python3 learn/toolbox/spin_sandbox.py
```

```
  A 6U CubeSat, just separated, tumbling at 10.6 degrees per second.
  Magnetorquers only. No thrusters, no wheels, no ground contact.

  Spin rate over 90 minutes (degrees per second):

   10.64 |█
    9.46 |███
    8.28 |██████
    7.10 |████████
    5.91 |██████████
    4.73 |████████████
    3.55 |███████████████
    2.37 |█████████████████████████████
    1.18 |████████████████████████████████████████
         +----------------------------------------------------------

      start:  10.64 deg/s
      end:     0.46 deg/s      (96% removed)
```

One orbit, no fuel, using a coil and a planet.

The control law in the sandbox is three lines, and they are worth reading:

```python
dipole = scale(b_dot, -BDOT_GAIN)          # THE control law

magnitude = norm(dipole)                    # real coils saturate
if magnitude > MAX_DIPOLE:
    dipole = scale(dipole, MAX_DIPOLE / magnitude)

torque = cross(dipole, b_body)              # m x B
```

## 💡 Look at the shape of that curve

The plot is not a straight line, and the shape tells you what is happening.

**Early on** it comes down fast and roughly linearly. The commanded dipole is
larger than the coil can produce, so it is saturated — running flat out,
delivering constant torque.

**Later** it curves and flattens. The demand has dropped below saturation, the
controller is in its linear region, and the decay becomes exponential —
approaching zero without ever quite arriving.

**It never reaches zero.** That is not a bug, it is the physics: B-dot can only
remove rotation perpendicular to **B**, and **B** only sweeps round as fast as
the orbit. The last fraction takes a long time.

So a real mission accepts "slow enough" rather than "stopped". And "slow
enough" is a number the ground can change:

```yaml
- {name: DETUMBLE_RATE_DPS, id: 4, default: 2.0, min: 0.1, max: 30.0, units: deg/s,
   desc: Rate threshold above which detumble is commanded}
```

Read it back from the running spacecraft right now:

```bash
make params
```

## 💡 The second control law: pointing

Once detumbled and the estimator has converged, you can actually point.

The classic law is **quaternion feedback** — proportional-derivative control,
where the "position" error is a rotation:

```
        τ  =  −Kp · q_err_vector  −  Kd · ω
              └────────┬────────┘   └───┬──┘
              turn towards the       damp the rotation
              target                 so it does not overshoot
```

`q_err` is the rotation from where you *are* to where you want to be. Its
vector part points along the axis you need to rotate about, and its size grows
with the angle. So the first term turns you towards the target and the second
stops you sailing past it.

**Kp too high:** it overshoots and oscillates.
**Kd too high:** it becomes sluggish and never quite arrives.
Finding the balance is most of what control engineering is.

There is a classic trap here, and it comes from Lesson 13: `q` and `−q`
represent the *same* rotation. A controller that does not check the sign can
decide to rotate 359° when 1° would do. The fix is one line — if the scalar
part is negative, negate the whole quaternion — and forgetting it produces a
spacecraft that occasionally takes the scenic route.

## 🧪 Try it — tune a controller

Open [`../toolbox/spin_sandbox.py`](../toolbox/spin_sandbox.py) and change
`BDOT_GAIN`:

| Value | What to look for |
|---|---|
| 6000 (÷10) | Much slower. Does it finish within the orbit? |
| 60000 | The default |
| 600000 (×10) | Faster? Look carefully at the *shape* of the curve |

The third one is the interesting experiment. More gain is not simply better,
and working out why is the beginning of understanding control systems.

Then try `MAX_DIPOLE = 0.02` — a much smaller coil. What breaks, and at what
point in the curve?

Finally set `orbit_rate = 0`, pretending Earth's field never moves. Watch it
fail to fully detumble, and work out which component of the spin survives.
(Hint: re-read what a cross product can never produce.)

## 🎓 Go deeper

**Actuator allocation.** With three magnetorquers and four reaction wheels you
have seven actuators for three axes of torque. Which combination should produce
a given demand? There are infinitely many answers, and you choose using a
pseudo-inverse plus a cost — usually minimising power, or keeping the wheels
away from saturation and away from zero speed (where friction is worst and
behaviour is least predictable).

**Control in the presence of saturation.** Every real actuator has a limit.
A controller that ignores it will command 10 N·m, receive 0.001 N·m, see no
response, and demand more — *integral windup*. The fix is to tell the
controller what was actually delivered, not what was asked for.

**Disturbance torques** are what you are fighting: aerodynamic drag (below
~600 km), solar radiation pressure, gravity gradient, and residual magnetic
dipole from the spacecraft's own electronics. Gravity gradient is the elegant
one — a long thin satellite naturally settles pointing at Earth, and some
missions use that as free passive stabilisation.

## ✅ Check yourself

1. Why can a magnetorquer never produce torque along the magnetic field
   direction?
2. Reaction wheels do not use fuel. So why can they still "run out"?
3. B-dot needs no attitude estimate at all. Why does that matter so much
   immediately after separation?
4. In the detumble plot, why is the start roughly straight and the end curved?

---

**Next:** [Lesson 17 — Power and modes](../17-power-and-modes/) — deciding what
to do when the battery is running down.

<details>
<summary>✅ Answers</summary>

1. Because torque is the cross product m × B, and a cross product is always
   perpendicular to both inputs. The component of any demand along B simply
   cannot be produced.
2. Because they store angular momentum rather than removing it. Countering a
   steady disturbance spins the wheel faster and faster until it saturates and
   can absorb no more. Magnetorquers are then used to dump the momentum out of
   the spacecraft.
3. Because at that moment you have no attitude estimate — the spacecraft is
   spinning too fast for a star tracker, may be in eclipse, and has had no
   ground contact. A controller that needs an attitude solution cannot run.
   B-dot needs only a magnetometer.
4. The start is saturated: the coil is at its limit, delivering constant torque,
   so the rate falls linearly. Once the demand drops below the limit the
   controller enters its linear region and the decay becomes exponential.

</details>
