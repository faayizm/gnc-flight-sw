# Lesson 13 — Attitude

🔧 **Builder** · 🎓 Engineer · about 30 minutes

> **Phase 2 note.** The physics and the experiments are real and run today. The
> flight code is Phase 2 — which is why `ADCS_HK` currently reports
> `est_state=INVALID` and a quaternion of all zeros.

---

## ❓ The question

Lesson 12 answered "where is it". Now: **which way is it facing?**

That sounds easier. It is not. It is one of the genuinely hard parts of
spacecraft engineering, and the difficulty starts with just *writing the answer
down*.

## 💡 Why it matters

A satellite that cannot control its orientation is nearly useless:

- solar panels must face the **Sun**, or the battery dies
- the antenna must face the **ground station**, or nobody can talk to it
- the camera must face its **target**, or you photograph empty space
- the radiator must face **deep space**, or it overheats

All four want different directions at once. In the jargon, orientation is
called **attitude**, and the software that manages it is **ADCS** — Attitude
Determination and Control System.

Note that it is two jobs, and they are separate:

```
   DETERMINATION            CONTROL
   ─────────────            ───────
   which way AM I facing?   how do I TURN to face that way?
   sensors + estimation     actuators + control laws
   Lessons 14 and 15        Lesson 16
```

## 💡 The hard part: writing down a rotation

You would think three numbers would do it — pitch, roll and yaw, like an
aeroplane. Those are **Euler angles**, they are intuitive, and they have a
fatal flaw.

**Gimbal lock.** At certain orientations, two of the three axes line up and you
lose a degree of freedom. The maths divides by zero. A real spacecraft passing
through that orientation would see its attitude solution blow up.

Try it with your hand. Point your thumb up, index finger forward. Rotate 90°
about one axis and you will find two of your rotations now do the same thing.

Apollo 11 flew with a physical gimbal that could lock, and the crew had to
actively avoid the "forbidden" orientations. Michael Collins joked about adding
a fourth gimbal; the reply was that it would cost weight. Software has no such
excuse.

## 💡 The answer: quaternions

A **quaternion** describes a rotation with **four** numbers instead of three:

```
        q = (q₀, q₁, q₂, q₃)      with   q₀² + q₁² + q₂² + q₃² = 1
```

The idea underneath is elegant. Euler proved that *any* rotation, however
complicated, is equivalent to a single rotation by some angle θ about some
single axis **n**. A quaternion just stores that:

```
        q₀ = cos(θ/2)
        (q₁, q₂, q₃) = n · sin(θ/2)
```

The four numbers are constrained to sum (in squares) to one, so there are still
only three real degrees of freedom — the fourth is redundancy that buys you
freedom from singularities.

| | Euler angles | Quaternion | Rotation matrix |
|---|---|---|---|
| Numbers | 3 | 4 | 9 |
| Gimbal lock | **yes** | no | no |
| Easy to read | yes | no | no |
| Cheap to combine | no | **yes** | moderate |
| Easy to renormalise | — | **one divide** | Gram-Schmidt |

Quaternions win on every count that matters in flight, and lose on the one that
matters to humans. So spacecraft compute in quaternions and convert to Euler
angles only for display.

You can see that decision in this repository's
[`dictionary/mission.yaml`](../../dictionary/mission.yaml):

```yaml
- {name: q_est_0, type: float32, units: "-", desc: Estimated attitude quaternion scalar part}
- {name: q_est_1, type: float32, units: "-", desc: Estimated attitude quaternion x}
- {name: q_est_2, type: float32, units: "-", desc: Estimated attitude quaternion y}
- {name: q_est_3, type: float32, units: "-", desc: Estimated attitude quaternion z}
```

Four fields, downlinked every second. There is also `pointing_err_deg` — a
single human-readable number — right next to them, for exactly the reason
above.

## 💡 Rotation is stranger than you expect

Push a box in space and it travels in a straight line forever. Simple.

Spin a box in space and it does **not** simply keep spinning about the same
axis. Unless you spin it exactly about one of its three principal axes, the
axis itself wanders. This is **Euler's equation**:

```
        I ω̇  =  τ  −  ω × (I ω)
                      └─────┬─────┘
                   this term exists even with
                   ZERO torque, and it is why
                   rotation is not intuitive
```

`I` is the **moment of inertia** — the rotational equivalent of mass, and it is
a matrix rather than a single number, because a satellite is harder to spin
about some axes than others.

You have seen that second term. Flip a tennis racket about its middle axis and
it always twists a half-turn. Toss a phone spinning about its middle axis and it
tumbles oddly. That is the **intermediate axis theorem**, it is real physics,
and a spacecraft has to live with it.

## 👀 See it

```bash
python3 learn/toolbox/spin_sandbox.py
```

That sandbox integrates Euler's equation for a real 6U CubeSat and shows the
tumble being removed. Lesson 16 covers the control law; for now, look at how
the physics is written:

```python
iw = (INERTIA[0]*omega[0], INERTIA[1]*omega[1], INERTIA[2]*omega[2])
gyroscopic = cross(omega, iw)                       # the ω × (Iω) term
domega = ((torque[0] - gyroscopic[0]) / INERTIA[0], ...)
omega = add(omega, scale(domega, dt))
```

Four lines, and it is exactly the equation above.

## 💡 Frames: the thing that actually catches people out

Ask "which way is it pointing?" and the honest answer is "relative to what?"

| Frame | Origin | Axes fixed to |
|---|---|---|
| **ECI** — Earth-Centred Inertial | Earth's centre | The stars. Does *not* rotate |
| **ECEF** — Earth-Centred Earth-Fixed | Earth's centre | The ground. Rotates once a day |
| **LVLH** — orbital frame | The spacecraft | Nadir and the velocity direction |
| **Body** | The spacecraft | The spacecraft's own structure |

Attitude is a rotation **between two frames** — normally body relative to ECI.
A quaternion on its own is meaningless without saying which two.

> **Frame confusion is the single most common source of attitude bugs.** Not
> the maths — the bookkeeping. Which frame, which direction, and does this
> quaternion rotate *from* body *to* inertial or the other way round?

The only defence is being relentlessly explicit, everywhere, forever. That is
why [`sim/models/README.md`](../../sim/models/README.md) makes it a rule:

> Every model states its units and its reference frame in its docstring.

## 🧪 Try it — feel gimbal lock

No computer needed.

1. Hold your phone flat, screen up, top edge pointing away from you.
2. **Yaw**: spin it flat, like a steering wheel.
3. **Pitch**: tip the top edge down towards the table.
4. **Roll**: tip it left and right.

Now pitch it 90° so the top edge points straight down at the table, and try yaw
and roll again.

They do the same thing. You have lost an axis. That is gimbal lock, and a
spacecraft computing its attitude in Euler angles at that orientation gets a
division by zero.

## 🎓 Go deeper

**The double cover.** `q` and `−q` describe the *same* rotation. Every
attitude has two quaternion representations. A controller that naively drives
the error quaternion to `(1,0,0,0)` may take the long way round — 359° instead
of 1° — unless it checks the sign. It is a classic bug and Lesson 16 revisits
it.

**Renormalisation.** Numerical integration slowly breaks the constraint that
the four numbers square-sum to one. A quaternion is fixed by dividing by its
own length — one operation. A rotation matrix needs Gram-Schmidt on three
vectors, which you can see in the sandbox:

```python
def orthonormalise(matrix):
    """
    Numerical integration slowly destroys the 'rotation-ness' of a matrix
    -- rows stop being unit length and stop being perpendicular. Gram-
    Schmidt puts it back. Every attitude system needs an equivalent step,
    which is one reason quaternions are popular: they need only a single
    divide to renormalise.
    """
```

**Where this goes.** Phase 2 builds the dynamics and B-dot detumble; Phase 3
adds TRIAD, a complementary filter, and then a multiplicative extended Kalman
filter — with the derivation written out, because a filter you cannot derive is
a filter you cannot debug.

## ✅ Check yourself

1. What is gimbal lock, and why do spacecraft avoid Euler angles because of it?
2. A quaternion has four numbers for three degrees of freedom. What does the
   extra one buy?
3. Why does the ω × (Iω) term make rotation unintuitive?
4. Someone hands you a quaternion `(0.707, 0, 0.707, 0)`. What is missing
   before you can use it?

---

**Next:** [Lesson 14 — Sensors and noise](../14-sensors-and-noise/) — every
measurement is wrong.

<details>
<summary>✅ Answers</summary>

1. At certain orientations two of the three rotation axes align and a degree of
   freedom is lost; the maths divides by zero. A spacecraft can reach any
   orientation, so a representation that fails at some of them is unusable.
2. Freedom from singularities. The redundancy means there is no orientation at
   which the representation degenerates, at the cost of a constraint that has
   to be maintained by renormalising.
3. Because it produces changes in the rotation axis even with no torque
   applied. An object spun about anything except a principal axis will not keep
   spinning about that axis — the tennis racket effect.
4. Which two frames it relates, and in which direction. A rotation is
   meaningless without saying "from what, to what" — and getting the direction
   backwards gives you the inverse rotation, which is a very hard bug to spot.

</details>
