# Lesson 12 — Orbits

🚀 **Explorer** · 🔧 Builder · about 30 minutes

> **Phase 2 note.** The physics here is real and the experiments run today. The
> flight code that *uses* it is still being written — see
> [the roadmap](../../docs/ROADMAP.md). The `pos_eci_*` fields in `ADCS_HK` are
> currently zero for exactly this reason.

---

## ❓ The question

Why doesn't a satellite fall down?

## 💡 The idea

**It does.** Constantly. It is falling right now.

The trick is that it is also moving *sideways* so fast that by the time it has
fallen, the ground has curved away underneath it. It keeps falling and keeps
missing.

That is an orbit: falling, and missing the planet.

Newton drew this in 1687. Imagine a cannon on a very tall mountain, firing
horizontally:

```
        slow  ──▶  ....                     lands nearby
                 ▁▁▁▁▁▁▁▁
        faster ──▶ .......                  lands further away
                 ▁▁▁▁▁▁▁▁▁▁▁▁▁▁
        fast   ──▶ ..............           goes right around
                 ▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁
```

Nothing about being "in space" makes you weightless. Gravity at the Space
Station is about **90%** of what it is on the ground. Astronauts float because
they are falling, not because gravity has gone away.

## 👀 See it

```bash
python3 learn/toolbox/orbit_sandbox.py
```

```
  WHERE                       ALTITUDE       SPEED     ONE ORBIT
  --------------------------------------------------------------
  Space Station                  400km      7.67km/s        92 min
  This project's satellite       550km      7.59km/s        96 min
  Sun-synchronous imaging        800km      7.46km/s       101 min
  GPS                          20200km      3.87km/s    12.0 hours
  Geostationary (TV)           35786km      3.07km/s    23.9 hours
```

Look at that pattern: the **higher** you go, the **slower** you travel, and the
**longer** a lap takes. That feels backwards until you realise gravity is
weaker up there, so less speed is needed to balance it.

Geostationary is the special one. At 35,786 km an orbit takes exactly one day,
so the satellite hangs above the same spot on Earth forever — which is why a TV
dish never has to move.

## 💡 The one equation

For a circular orbit, gravity provides exactly the centripetal force needed:

```
        m v²            μ m                         ┌─────────┐
        ────    =    ───────         →         v =  │  μ / r
          r             r²                          └─────────┘
```

where **μ** (mu) is Earth's gravitational parameter, 3.986 × 10¹⁴ m³/s², and
**r** is measured from Earth's *centre*, not its surface.

That is it. Everything else about orbits is elaboration.

The period follows:

```
        T  =  2π √(r³ / μ)
```

which is Kepler's third law, discovered by watching planets seventy years
before Newton explained why.

## 🧪 Try it — get the speed wrong

The sandbox shows what happens when injection speed is off:

```
      YOUR SPEED  RESULT
  --------------------------------------------------------------
       6.451 km/s  falls back down -- hits the ground
       7.210 km/s  falls back down -- hits the ground
       7.589 km/s  a perfect circle
       7.968 km/s  an ellipse: 550 km up to 2131 km
       9.107 km/s  an ellipse: 550 km up to 11426 km
      10.733 km/s  escapes Earth entirely -- never comes back
```

Too slow and you come back down. Too fast and you swing out into an ellipse.
About 41% too fast and you leave for good — that is **escape velocity**, and
the 41% is √2, which is not a coincidence.

Being 5% off is not "close". This is why launch is so unforgiving.

## 🧪 Try it — actually fly one

The sandbox then integrates the real equation of motion, step by step:

```
        acceleration  =  −μ · position / |position|³
```

That is Newton's law of gravitation, and it is the *entire* physics of an
orbit. Sixty thousand small steps later:

```
     SPEED    LOWEST POINT    HIGHEST POINT
  --------------------------------------------------------------
      90%         CRASHED
     100%           550 km            550 km
     110%           550 km           4230 km
```

Those numbers came from nothing but Newton's law and arithmetic. No orbital
mechanics formulas, no library. And they agree with the analytic answer to the
nearest kilometre — which is a satisfying thing to check for yourself.

**Now the most important experiment in the whole sandbox.** Open the file and
change the timestep:

```python
def simulate(speed_factor: float, steps: int = 60_000, dt: float = 1.0):
```

Set `dt = 60.0` and rerun. The answers get worse — a "circular" orbit slowly
spirals. Nothing about the physics changed; only the size of the steps.

Every simulation makes this trade. Smaller steps are more accurate and slower.
The step size you choose is an engineering decision, and getting it wrong
produces results that look perfectly plausible and are wrong. When Phase 2
builds the real simulator, this is the first question it has to answer.

## 💡 Why our satellite needs to know

To point a camera at a place on Earth, the spacecraft must know **where it is**.
To talk to a ground station, it must know **when it will be overhead**. To
manage its battery, it must know **when it enters Earth's shadow**.

So flight software carries its own orbit model on board and propagates it
forward between GPS fixes. In `ADCS_HK` you will find three fields:

```
  pos_eci_x   pos_eci_y   pos_eci_z
```

That is the spacecraft's own answer to "where am I?", in **ECI** coordinates —
Earth-Centred Inertial, a frame with its origin at Earth's centre that does
*not* rotate with the planet. Watch them:

```bash
make monitor
```

They are zero. In Phase 2 they stop being zero, and the interesting number
becomes the *difference* between the spacecraft's own estimate and the
simulator's truth.

## 🎓 Go deeper — what the simple model leaves out

Real orbits are not ellipses, because Earth is not a point mass and space is
not empty:

| Effect | What it does | Matters for |
|---|---|---|
| **J2** — Earth's equatorial bulge | Rotates the orbit plane slowly | Everything in low orbit |
| **Atmospheric drag** | Slowly lowers the orbit | Below ~600 km; eventually reentry |
| **Solar radiation pressure** | Sunlight pushes | Large, light spacecraft |
| **Sun and Moon gravity** | Third-body tugs | High orbits |

J2 is the interesting one, because engineers *use* it. Choose the right
altitude and inclination and the orbit plane rotates exactly once per year,
keeping the satellite over each point at the same local solar time every day.
That is a **sun-synchronous orbit**, and nearly every Earth-imaging satellite
flies one — so shadows in the pictures are always consistent.

This is why **Orekit** joins the project at Phase 3: it implements all of these
properly, with real Earth orientation data, and reimplementing that well is a
career rather than a lesson.

## ✅ Check yourself

1. Why do astronauts float, if gravity at the Space Station is 90% of surface
   gravity?
2. Why does a higher satellite move *slower*?
3. In the sandbox, why does a larger timestep make a circular orbit spiral?
4. What is special about a sun-synchronous orbit, and why do imaging satellites
   want one?

---

**Next:** [Lesson 13 — Attitude](../13-attitude/) — knowing which way it points.

<details>
<summary>✅ Answers</summary>

1. Because they are in free fall, and so is the station around them. Falling
   together at the same rate means no contact force between them — and the
   sensation of weight *is* the contact force, not gravity itself.
2. Because gravity is weaker further out, so less speed is needed to balance
   it. v = √(μ/r): larger r, smaller v.
3. Because each step approximates a curve by a straight line, and the error per
   step grows with step size. The errors accumulate in a consistent direction,
   so the orbit drifts instead of closing.
4. Its orbit plane rotates once per year — using J2 rather than fuel — so the
   satellite crosses each latitude at the same local solar time every day.
   Imaging satellites want it so lighting and shadows are consistent between
   pictures taken weeks apart.

</details>
