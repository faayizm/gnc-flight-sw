# Lesson 14 — Sensors and noise

🔧 **Builder** · 🎓 Engineer · about 25 minutes

> **Phase 2 note.** Sensor models are built in Phase 2. The ideas here are what
> the whole of estimation exists to deal with, so they come first.

---

## ❓ The question

Lesson 13 said a spacecraft has to know which way it is pointing. So put a
sensor on it and read the answer.

Except: **every measurement is wrong.** Always. By some amount. Now what?

## 💡 The idea

There is no such thing as a sensor that tells you the truth. There are only
sensors that are wrong in ways you understand well enough to work with.

A satellite's attitude sensors have four separate problems, and they are
genuinely different from one another:

| Problem | What it looks like | What you do about it |
|---|---|---|
| **Noise** | Random jitter around the right answer | Average it away — but averaging costs time |
| **Bias** | Consistently off by a fixed amount | *Estimate* it and subtract it |
| **Drift** | The bias itself slowly changes | Keep re-estimating, forever |
| **Blindness** | Sometimes no answer at all | Have a plan for the gap |

Noise is the one people think of. **Bias is the one that actually hurts**,
because averaging does not remove it — average a million readings from a
gyroscope with a bias and you get a beautifully precise wrong answer.

## 💡 The sensors on a small satellite

**Gyroscope** — measures rotation *rate*, three axes.

Excellent at short timescales, terrible at long ones. Integrate its rate to get
angle and any bias integrates too, so the angle error grows *linearly with
time*, forever. A tiny bias of 0.01 °/s becomes 36° of error after an hour.

It also cannot tell you your orientation at all — only how fast it is changing.
Start it up and it has no idea which way it is facing.

**Magnetometer** — measures Earth's magnetic field, three axes.

Cheap, always available in low orbit, and gives an absolute reference. But the
field is weak (about 30 microtesla), the spacecraft's own electronics interfere
with it, and the field model you compare against is itself imperfect.

**Sun sensor** — measures the direction to the Sun.

Accurate and simple. But useless in eclipse, which is roughly a third of every
orbit. A sensor that stops working for 30 minutes out of every 96 is not a
flaw to be fixed — it is a property to be designed around.

**Star tracker** — photographs the sky and matches it to a catalogue.

By far the most accurate: arcseconds. Also the most expensive, the heaviest,
and it fails when the Sun or Earth is in its field of view, or when the
spacecraft is spinning too fast to hold a sharp image. That last point matters:
you cannot use it until you have already detumbled.

## 💡 Quantisation: the error nobody expects

A sensor's output is a number in a computer, so it has a finite number of bits.
A 12-bit sensor covering ±100 °/s has a resolution of:

```
        200 °/s  /  4096 steps  =  0.049 °/s per step
```

You can never know your rate better than that, no matter how still you are, no
matter how much you average. It is not noise — it is a floor.

## 🔍 In the code — modelling the imperfection is the point

Here is the rule that
[`sim/models/README.md`](../../sim/models/README.md) states for Phase 2, and it
is the most important idea in this lesson:

> **Model the imperfections, not just the physics.** A gyroscope that returns
> the true rate makes a complementary filter look brilliant and teaches
> nothing. The bias random walk is what the filter exists to estimate; the
> quantisation is what sets the noise floor; the sun sensor's blindness in
> eclipse is what forces the mode logic to have an answer for losing a
> reference.

This is why the flight software is never given the truth. From
[`sim/README.md`](../../sim/README.md):

> **The flight software never sees the truth.** It receives only what a sensor
> would produce — noisy, biased, quantised, occasionally invalid — and must
> estimate everything else. A simulator that hands the controller the true
> attitude is a simulator that proves nothing, and it is an easy and tempting
> mistake to make.

It really is tempting. The truth is right there in the simulator, one variable
away, and using it makes every graph look wonderful. It also makes the whole
exercise worthless.

## 💡 Validity flags: admitting when you do not know

Look at the telemetry this spacecraft already declares, in
[`dictionary/mission.yaml`](../../dictionary/mission.yaml):

```yaml
- {name: sun_valid, type: uint8, units: bool, desc: Sun sensor reference is usable}
- {name: mag_valid, type: uint8, units: bool, desc: Magnetometer reference is usable}
- {name: eclipse,   type: uint8, units: bool, desc: Spacecraft is in Earth shadow}
```

Every sensor reading arrives with a flag saying whether to believe it.

That is not defensive padding. It is the difference between a system that
degrades and a system that diverges. A filter fed a garbage sun vector during
eclipse — because nobody flagged it — will confidently converge on a completely
wrong attitude, and then the controller will point the spacecraft accordingly.

The honest answer is often "I do not know right now", and software has to be
able to say it.

## 👀 See it — bias versus noise

Save this as `bias_demo.py` and run it:

```python
import random
random.seed(7)

TRUE_RATE = 1.0          # deg/s
NOISE     = 0.30         # random, symmetric
BIAS      = 0.05         # constant offset

def reading():
    return TRUE_RATE + BIAS + random.gauss(0, NOISE)

for n in (1, 10, 100, 10_000, 1_000_000):
    average = sum(reading() for _ in range(n)) / n
    print(f"  average of {n:>9,} readings: {average:.5f} deg/s"
          f"   error {average - TRUE_RATE:+.5f}")
```

```
  average of         1 readings: 0.97324 deg/s   error -0.02676
  average of        10 readings: 1.11131 deg/s   error +0.11131
  average of       100 readings: 1.03848 deg/s   error +0.03848
  average of    10,000 readings: 1.05175 deg/s   error +0.05175
  average of 1,000,000 readings: 1.05023 deg/s   error +0.05023
```

Averaging crushes the noise — and then stops dead at **+0.05**, the bias,
exactly. A million readings buy you nothing that ten thousand did not.

That number, 0.05, is why Lesson 15 exists. You cannot average a bias away. You
have to *estimate* it, using a second sensor that is wrong in a different way.

## 🧪 Try it

1. Set `BIAS = 0.0` and rerun. Now averaging converges on the truth. That is
   what noise-only looks like, and it is the easy case.
2. Set `NOISE = 0.0` and `BIAS = 0.05`. Every single reading is wrong by
   exactly the same amount, and no amount of data helps.
3. Make the bias *drift*: change `BIAS` to a global that creeps by `+0.000001`
   every reading. Now even a perfect estimate goes stale. That is a
   **random walk**, it is what real gyroscopes do, and it is why a filter has
   to keep working forever rather than calibrating once.

## 🎓 Go deeper

**Why two bad sensors beat one good one.** A gyroscope is superb over seconds
and drifts over hours. A magnetometer is noisy every instant but never drifts.
Their errors have opposite character — so combining them can be better than
either alone. That is the whole idea of Lesson 15.

**Allan variance** is how gyroscope performance is actually specified: a plot
of error against averaging time that separates white noise from bias
instability from rate random walk. When a datasheet says "bias instability
3 °/hr", that is a point on this curve.

**Sensor fusion is not averaging.** Averaging assumes both sources are equally
trustworthy and wrong in the same way. They are not. Weighting them properly,
according to how much you trust each *right now*, is exactly what a Kalman
filter does.

## ✅ Check yourself

1. Why does averaging remove noise but not bias?
2. Why can't a gyroscope alone tell you which way you are pointing?
3. Why does a sun sensor's blindness during eclipse count as a *design input*
   rather than a fault?
4. Why must the simulator never hand the flight software the true attitude?

---

**Next:** [Lesson 15 — Estimation](../15-estimation/) — combining several wrong
answers into one good one.

<details>
<summary>✅ Answers</summary>

1. Because noise is random and symmetric, so positive and negative errors
   cancel as the sample grows. A bias is the same value every time, so it
   survives averaging untouched — it *is* the average.
2. Because it measures rate of change, not orientation. Integrating rate gives
   you change-in-angle, but you still need to know where you started, and any
   bias in the rate accumulates into an angle error that grows without bound.
3. Because it happens every orbit, predictably, for about a third of the time.
   Software that treats it as a fault will declare an anomaly roughly sixteen
   times a day. It has to be a planned-for state with defined behaviour.
4. Because then the estimator and controller are never tested. Everything works
   beautifully in simulation and fails in orbit, where truth is not available.
   The simulator's job is to be as unhelpful as reality.

</details>
