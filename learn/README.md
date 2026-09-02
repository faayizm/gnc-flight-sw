# Learn to fly a satellite

Eighteen lessons that start with "what even *is* a satellite" and end with
estimating attitude from noisy sensors and recovering from a failed reaction
wheel. Every lesson is built around a **real spacecraft you can actually talk
to** — the flight software in this repository — rather than around toy examples.

You do not need to know C++. You do not need to know physics. You need a
computer and some curiosity.

## Three tracks

Every lesson is marked, and every lesson has something for every track.

| Badge | Who | What you need |
|:---:|---|---|
| 🚀 | **Explorer** — around 10 and up | Nothing. Run commands, watch what happens, break things on purpose |
| 🔧 | **Builder** — comfortable with some code | Python. Read the real files, change numbers, see the effect |
| 🎓 | **Engineer** — studying or working in this field | The standards, the derivations, the trade-offs and why they were made |

An Explorer can do lessons 1–9 with no programming at all and will genuinely
understand how a spacecraft talks to the ground. That is not a simplified
version of the real thing — it *is* the real thing, with real CCSDS packets.

## The lessons

### Part 1 — Talking to a spacecraft 🚀

You can do all of these with no programming.

| # | Lesson | The question it answers |
|---|---|---|
| 1 | [What is a satellite?](01-what-is-a-satellite/) | Why does a thing in orbit need software at all? |
| 2 | [First contact](02-first-contact/) | Let's actually launch one and say hello |
| 3 | [Bytes and numbers](03-bytes-and-numbers/) | How do you write a number so a different computer reads it right? |
| 4 | [Checksums](04-checksums/) | Space is noisy. How do you know a message wasn't damaged? |
| 5 | [CCSDS packets](05-ccsds-packets/) | The envelope every spacecraft in the world puts its messages in |
| 6 | [PUS services](06-pus-services/) | The shared language that lets a ground station fly a spacecraft it has never seen |
| 7 | [Did it work?](07-did-it-work/) | Commanding blind, and why verification reports exist |
| 8 | [Housekeeping and events](08-housekeeping-and-events/) | How a spacecraft tells you it is healthy — and how it explains itself afterwards |
| 9 | [Parameters](09-parameters/) | Changing a spacecraft's behaviour from 500 km away, without breaking it |

### Part 2 — Inside the flight software 🔧

| # | Lesson | The question it answers |
|---|---|---|
| 10 | [The heartbeat](10-the-heartbeat/) | Why does flight software use one thread when your laptop uses hundreds? |
| 11 | [Portability](11-portability/) | How do you write code before you know what computer it will run on? |

### Part 3 — Physics and control 🔧🎓

These teach the ideas behind Phase 2 and later. The maths is real and the
experiments run today; the flight code that uses them is still being written,
and each lesson says so plainly.

| # | Lesson | The question it answers |
|---|---|---|
| 12 | [Orbits](12-orbits/) | Why doesn't a satellite fall down? (It does. Constantly.) |
| 13 | [Attitude](13-attitude/) | Which way is it pointing, and why is that surprisingly hard to write down? |
| 14 | [Sensors and noise](14-sensors-and-noise/) | Every measurement is wrong. Now what? |
| 15 | [Estimation](15-estimation/) | Combining several wrong answers into one good one |
| 16 | [Control](16-control/) | Making it point where you want, using magnets and spinning wheels |
| 17 | [Power and modes](17-power-and-modes/) | Deciding what to do when the battery is running down |
| 18 | [When things break](18-when-things-break/) | Nobody can go and fix it. Now what? |

## How to use a lesson

Every lesson has the same shape, so you always know where you are:

```
  ❓ The question      why this matters, before any answer
  💡 The idea          plain language, with a picture or an analogy
  👀 See it            a command to run against the real spacecraft
  🔍 In the code       the actual file where it happens, with a link
  🧪 Try it            an experiment — usually breaking something on purpose
  ✅ Check yourself    questions, with answers at the bottom
  🎓 Go deeper         for the engineer: the standard, the maths, the trade-off
```

Explorers can stop after 🧪. Nothing after it is needed to do the next lesson.

## Setting up

You need this once, and it takes about a minute:

```bash
git clone https://github.com/faayizm/gnc-flight-sw
cd gnc-flight-sw
make build
```

If that printed `72 passed, 0 failed` at the end, you are ready.

Then, whenever a lesson says "launch the spacecraft":

```bash
make run          # leave this running in one terminal
```

and in a **second** terminal, run whatever the lesson asks for.

## The toolbox

[`learn/toolbox/`](toolbox/) holds small standalone programs used by the
lessons. They need only Python — no spacecraft, no build, no installation.

| Program | What it shows you |
|---|---|
| `byte_order.py` | The same number written two different ways, and why that breaks things |
| `crc_playground.py` | Damage a message and watch the checksum catch it |
| `packet_explorer.py` | A real packet taken apart byte by byte, with every field explained |
| `orbit_sandbox.py` | How fast you have to go to stay in orbit, and what happens if you don't |
| `spin_sandbox.py` | Why a tumbling satellite is hard to stop, and how a magnet does it |

## For teachers and parents

- **Lessons 1–5 make a good single session** of about an hour, and end with a
  child having sent a command to a spacecraft and read its reply.
- **Nothing is simulated away.** The packets in Lesson 5 are byte-for-byte what
  a real mission sends. A student who understands them understands the real
  standard.
- **Every lesson has a "break it" experiment.** In this field the interesting
  learning is nearly always in the failure, not the success — and here failure
  costs nothing.
- **The answers are in the lesson**, at the bottom, deliberately. The goal is
  understanding, not assessment.
- [`GLOSSARY.md`](GLOSSARY.md) explains every acronym in plain language. Space
  engineering is drowning in them and they are a real barrier.

## If you get stuck

- Every directory in this repository has a `README.md` explaining what is in it
- [`../docs/ARCHITECTURE.md`](../docs/ARCHITECTURE.md) explains *why* the
  software is built the way it is
- [`../docs/ICD.md`](../docs/ICD.md) is the complete list of every message this
  spacecraft can send or receive
- Open an issue. A confusing lesson is a bug in the lesson.
