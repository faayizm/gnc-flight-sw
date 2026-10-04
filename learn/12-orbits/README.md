# Lesson 12 — Orbits

🚀 **Explorer** · 🔧 Builder · about 30 minutes

> **Built.** The simulator flies a real orbit
> ([`sim/models/orbit.py`](../../sim/models/orbit.py)), the flight software
> carries its own ([`orbit_prop.cpp`](../../fsw/apps/adcs/orbit_prop.cpp)), and
> `make store-forward` works out when a ground station can see the spacecraft.
> All three appear in this lesson.

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
produces results that look perfectly plausible and are wrong.

The real simulator had to answer this question too. It uses **RK4**
(fourth-order Runge–Kutta), which samples the acceleration four times per step
and blends the results. Its error shrinks with the *fourth* power of the step,
so halving the step cuts the error sixteen-fold. With 0.1-second steps the
simulated orbit holds its energy to a few parts in ten thousand per orbit,
and a test checks that every time the code changes:

```bash
make test-sim
```

```
[orbit]
  .  energy is nearly conserved over an orbit (J2 exchanges a little)
  .  a circular orbit stays circular to within J2's oblateness
```

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

With only `make run` they are zero: no simulator is connected, so there is no
GPS to ask. Start one that is, in terminal 1:

```bash
make detumble-live
```

and run `make monitor` in terminal 2. Now `pos_eci_x`, `pos_eci_y` and
`pos_eci_z` are millions of metres, changing every second. Their size is
always about 6,878,000 m, Earth's radius plus 500 km.

The interesting part is what happens **when GPS goes away**. The spacecraft
does not stop knowing where it is: it takes its last GPS fix and steps its own
orbit model forward, the same RK4 method as the simulator, on board. `make
pointing` switches GPS off for fifteen minutes in the middle of the flight.
The spacecraft keeps pointing at the centre of the Earth to within 0.11° the
whole time, because its own orbit estimate drifts by only metres.

## 💡 When will it be overhead?

A ground station sits on a spinning Earth, and the satellite crosses its sky
in about ten minutes. Then it is gone for over an hour, or for a whole day if
the orbit's track has drifted away from the station. Those minutes are the
**pass**, the only time anyone can talk to the spacecraft.

Whether the satellite is "in view" is just geometry. Draw a line from the
station to the satellite and measure its angle above the horizon (the
**elevation**). Below about 5°, buildings, hills and thick air block the
radio, so that is the cut-off.

```bash
make store-forward
```

```
store and forward via mid-latitude station (45.0 N, 30.0 E)
  pass 1: 591-1104 s, max elevation 25 deg
  pass 2: 6456-7026 s, max elevation 65 deg
```

Two passes, about 8.5 and 9.5 minutes long, with an hour and a half of
silence between them. Those numbers came from the same orbit you just
learned about: [`sim/sil/radio.py`](../../sim/sil/radio.py) steps the orbit
forward, turns the Earth under it, and checks the elevation every second. The
first pass is low, 25°, skimming the horizon. The second passes almost
overhead. Lesson 6 shows what a spacecraft does with the silence in between.

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

HYPERSAT's simulator models J2 and nothing else in this table. That is a
choice, not an oversight. The plan was to bring in **Orekit**, a library that
models all of these properly with real Earth-orientation data, and the
project decided against it. Pass times computed with J2 alone are right to
seconds, far finer than anything the link can notice. A dependency has to
earn its place, and this one would not have changed a single result.

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
