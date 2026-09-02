# Lesson 1 — What is a satellite?

🚀 **Explorer** · no computer needed for this one · about 15 minutes

---

## ❓ The question

There is a machine 550 kilometres above your head, travelling at 7.6 kilometres
every second, and nobody can reach it. Ever. If something goes wrong, no one is
coming to fix it.

So what is actually up there, and why does it need software?

## 💡 The idea

A satellite is a computer in a box, with a radio, some batteries, some solar
panels, and something useful — a camera, a science instrument, a
communications payload.

But here is the thing that makes it different from every other computer you
have used:

```
   YOUR LAPTOP                    A SATELLITE
   ───────────                    ───────────
   You can restart it             You can send a command and wait 45 minutes
   You can plug it in             It has whatever sunlight it can catch
   You can open it up             It is welded shut and 550 km away
   If it crashes, you notice      If it crashes, it may never speak again
   It runs for hours              It must run for years without a reboot
```

That last one deserves a pause. A satellite's software might run for **five
years**, non-stop, with no one watching most of the time, in an environment
that damages electronics. Everything about how the software in this repository
is written comes from that fact.

## 🛰️ The parts of a satellite

Nearly every spacecraft ever built has these same pieces, and they have
standard names you will meet everywhere:

| Name | Also called | What it does |
|---|---|---|
| **ADCS** | AOCS, GNC | Works out which way it is pointing, and turns it |
| **EPS** | Power | Solar panels, battery, deciding what to switch off |
| **TT&C** | Comms | The radio. Telemetry, Tracking and Command |
| **OBC** | Flight computer | The computer running all of it |
| **Payload** | — | The reason the mission exists at all |
| **Structure** | Bus | The frame everything bolts to |
| **Thermal** | — | Keeping things from freezing or cooking |

This repository builds the software for **all** of them. Right now TT&C works;
the others are being built in order.

## 🌍 Why it needs to point

A satellite that cannot control which way it faces is nearly useless:

- **Solar panels** must face the Sun, or the battery runs flat and it dies
- **The antenna** must face the ground station, or nobody can talk to it
- **The camera** must face the target, or you photograph empty space
- **The radiator** must face away from the Sun, or it overheats

And all four of those want *different* directions at the same time. Deciding
which one wins, right now, is a real job that software does.

## ⚡ Why it is hard

**1. You cannot test it where it will live.** No lab on Earth has vacuum,
weightlessness, radiation and thermal cycling all at once, for years. So you
build a *simulator* — which is what `sim/` in this repository is for.

**2. Radiation damages computers.** A high-energy particle passing through a
memory chip can flip a 1 into a 0. That is called a **single-event upset**, and
in low Earth orbit it happens regularly. Software has to expect its own memory
to change underneath it.

**3. You get about 10 minutes of contact per pass.** A satellite in low orbit
races over a ground station in a few minutes and is then gone for 90 minutes.
For most of its life, nobody is watching. It has to look after itself.

**4. Everything is scarce.** Power, memory, processor speed, radio bandwidth.
A flight computer might have less memory than a cheap phone from 2005 — because
parts that survive radiation are years behind consumer ones.

## 🧪 Try it (no computer)

Look up at the sky about an hour after sunset, away from streetlights. Watch
for a steady point of light moving smoothly across the sky, taking a few
minutes to cross, not blinking.

That is a satellite, catching sunlight from over the horizon while you stand in
darkness. There are thousands of them up there right now.

The International Space Station is the bright one, and
[spotthestation.nasa.gov](https://spotthestation.nasa.gov) will tell you exactly
when it passes over you. It is the size of a football pitch, moving at
7.66 km/s, 400 km up, with people inside.

## ✅ Check yourself

1. Why can't a satellite just be restarted when something goes wrong?
2. Name three things on a satellite that all want to point in different
   directions.
3. Why is a flight computer usually much slower than your phone?
4. What is a single-event upset?

## 🎓 Go deeper

**Orbits and how long you get.** A satellite at 550 km orbits in about 96
minutes. A ground station sees it for roughly 5–10 minutes per pass, and only
on the passes that come near enough — often 4 to 6 times a day. Run
[`../toolbox/orbit_sandbox.py`](../toolbox/orbit_sandbox.py) to see where those
numbers come from.

**Why "flight software" is its own discipline.** The constraints above rule out
most of what ordinary software engineering takes for granted: allocating memory
whenever you like, throwing exceptions, using many threads, restarting on
failure. [`../../docs/ARCHITECTURE.md`](../../docs/ARCHITECTURE.md) explains
each decision this project made and what it gave up.

**Real missions to read about.** The Mars Pathfinder priority-inversion bug of
1997 — a scheduling problem that repeatedly reset a spacecraft on another
planet, diagnosed and patched from Earth. It is the best story in this field
and it is about a scheduler exactly like the one in Lesson 10.

---

**Next:** [Lesson 2 — First contact](../02-first-contact/) — let's launch one
and say hello.

<details>
<summary>✅ Answers</summary>

1. Because "restarting it" means sending a radio command and hoping the
   spacecraft is still healthy enough to receive it. If the fault stopped the
   radio, or left it pointing the wrong way, there is no path back in. Software
   must therefore recover on its own.
2. Solar panels want the Sun; the antenna wants the ground station; the camera
   wants its target; the radiator wants deep space. Any three of those.
3. Because flight processors must survive radiation, and radiation-tolerant
   parts are designed years behind consumer parts and are made in tiny
   quantities. Reliability is bought with speed.
4. A charged particle passing through a memory cell or logic gate and flipping a
   bit — a 0 becomes a 1. The hardware is fine afterwards; the *data* is wrong.
   Software has to detect it, which is why checksums are everywhere (Lesson 4).

</details>
