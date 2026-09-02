# Lesson 15 — Estimation

🔧 **Builder** · 🎓 Engineer · about 35 minutes

> **Phase 3 note.** TRIAD, the complementary filter and the MEKF are built in
> Phase 3. The ideas and the experiments here run today.

---

## ❓ The question

You have a gyroscope that is smooth but drifts, and a magnetometer that never
drifts but is noisy. Neither tells you the truth.

Can two wrong answers make one good one?

## 💡 The idea

Yes — if they are wrong in **different ways**.

```
   GYROSCOPE                        MAGNETOMETER
   ─────────                        ────────────
   smooth, responsive               jumpy, noisy
   drifts without limit             never drifts
   trust it over SECONDS            trust it over MINUTES
```

Their errors have opposite character. So: use the gyroscope for fast changes
and the magnetometer to stop the slow drift. That is the whole idea, and every
attitude filter ever written is a more sophisticated version of it.

## 💡 Step 1: TRIAD — attitude from two directions

Before filtering anything, you need *an* attitude estimate. Here is the
surprising bit: **two known direction vectors are enough to fix your
orientation completely.**

Think about it. If you know where the Sun is, you have pinned down two of three
degrees of freedom — but you could still spin about the Sun line. Add a second
direction, say Earth's magnetic field, and that last freedom is gone.

TRIAD does exactly that, and the algorithm is almost embarrassingly simple:

```
   You MEASURE, in body axes:      s_body,  m_body     (sun and magnetic field)
   You PREDICT, in inertial axes:  s_ref,   m_ref      (from models + orbit)

   Build an orthogonal triad from each pair:

        t1 = s                    (trust the more accurate sensor most)
        t2 = (s x m) / |s x m|    (perpendicular to both)
        t3 = t1 x t2              (completes the set)

   Do that for both the measured and the reference pair, and the rotation
   between the two triads IS your attitude.
```

Note `t1 = s`: TRIAD trusts one sensor completely and uses the other only for
the remaining freedom. That is its weakness. It also throws away every
measurement older than the current one.

But it needs no initial guess, no tuning, and no history — so it is what you
use to *start*.

## 💡 Step 2: the complementary filter

Now combine the smooth-but-drifting gyroscope with the noisy-but-honest TRIAD
answer. The simplest useful version is one line:

```
     estimate  =  a * (estimate + gyro * dt)  +  (1 - a) * triad
                  |__________________________|    |____________|
                     trust the gyroscope           trust the absolute
                     for fast changes              reference for slow truth
```

with `a` close to 1 — say 0.98. The gyroscope carries the estimate between
updates; the 2% pull towards TRIAD prevents drift from accumulating.

It is called *complementary* because the two weights sum to one, and because
each sensor covers the frequency band where the other is weak — a low-pass on
one, a high-pass on the other.

**And it can do something better.** If the estimate is consistently being
pulled in one direction, that pull is telling you the gyroscope's bias. Track
the average correction and you are *estimating the bias* — the thing Lesson 14
showed you cannot average away. Subtract it, and the gyroscope becomes
dramatically better.

That is why the dictionary already has fields waiting:

```yaml
- {name: gyro_bias_x, type: float32, units: rad/s, desc: Estimated gyro bias X}
- {name: gyro_bias_y, type: float32, units: rad/s, desc: Estimated gyro bias Y}
- {name: gyro_bias_z, type: float32, units: rad/s, desc: Estimated gyro bias Z}
```

Watching those converge on the simulator's true injected bias is the moment the
filter proves it works.

## 👀 See it — a filter in twenty lines

Save this as `filter_demo.py` and run it:

```python
import random
random.seed(11)

TRUE_BIAS  = 0.05      # deg/s, what the gyro is secretly adding
GYRO_NOISE = 0.02
ABS_NOISE  = 3.0       # the absolute sensor is NOISY but unbiased
DT         = 0.1
ALPHA      = 0.98

true_angle = 0.0
estimate   = 0.0
bias_est   = 0.0
raw_only   = 0.0       # what you get with NO filter, for comparison

print("   time    truth    gyro-only     filtered   bias est")
for step in range(24001):
    true_rate = 2.0 * (1 if (step // 300) % 2 == 0 else -1)   # slew back and forth
    true_angle += true_rate * DT

    gyro = true_rate + TRUE_BIAS + random.gauss(0, GYRO_NOISE)
    absolute = true_angle + random.gauss(0, ABS_NOISE)        # noisy, no drift

    raw_only += gyro * DT                                      # naive integration

    predicted = estimate + (gyro - bias_est) * DT              # gyro carries it
    estimate = ALPHA * predicted + (1 - ALPHA) * absolute      # nudge to truth

    # The correction, spread over time, IS the bias.
    bias_est += 0.0005 * (predicted - estimate) / DT

    if step % 4000 == 0:
        print(f"  {step*DT:5.0f}s {true_angle:8.2f} {raw_only:12.2f} "
              f"{estimate:12.2f} {bias_est:10.4f}")
```

```
   time    truth    gyro-only     filtered   bias est
      0s     0.20         0.20         0.23    -0.0001
    400s    39.80        59.90        40.09     0.0425
    800s    40.20        80.50        40.06     0.0508
   1200s     0.20        60.60        -0.21     0.0523
   1600s    39.80       120.31        40.22     0.0484
   2000s    40.20       140.76        40.00     0.0509
   2400s     0.20       120.65         0.35     0.0486
```

Read the columns. **Gyro-only** drifts away without limit — after forty minutes
it is 120° wrong, and it would keep going forever. **Filtered** tracks the truth
to within a fraction of a degree, indefinitely.

Now look at the last column. The filter worked out the bias was about
**0.049**. The true value, which nobody told it, was **0.05**.

It discovered that number purely from the *disagreement* between two imperfect
sensors. Neither one knew it. Notice too that the estimate does not lock onto
0.05 and freeze — it wanders slightly around it, because it is continuously
re-measuring against a noisy reference. That wandering is what lets it track a
bias that *drifts*, which is what real gyroscopes do.

## 🧪 Try it

1. Set `ALPHA = 0.5`. The filter now trusts the noisy sensor heavily. Watch the
   estimate get jittery. Too much trust in the absolute reference is as bad as
   too little.
2. Set `ALPHA = 0.9999`. Almost pure gyroscope. Smooth — and drifting again.
   The tuning is a genuine trade, not a value with a right answer.
3. Set `TRUE_BIAS = 0.2`. Does the bias estimate still find it? How long?
4. Delete the `bias_est` update line. How much worse is it? That single line is
   doing most of the work.

## 💡 Step 3: the Kalman filter

The complementary filter has a fixed `a` — it always trusts each sensor by the
same amount. But trust should *change*. When you have just had a clean sun
measurement, believe the absolute reference more. In eclipse, believe it not at
all.

A **Kalman filter** does exactly that: it carries an estimate of *how uncertain
it currently is*, and weights each new measurement by how much that measurement
would reduce the uncertainty. When the filter is confident and the sensor is
noisy, the sensor barely moves it. When the filter is lost, one good
measurement moves it a long way.

It is provably optimal, for linear systems with Gaussian noise.

Attitude is neither linear nor unconstrained — quaternions live on a sphere, and
adding a correction to one takes you off it. The standard fix is the
**multiplicative extended Kalman filter** (MEKF): keep the quaternion as the
reference attitude, track only a small three-parameter *error* rotation in the
filter, and periodically fold the error into the quaternion and reset it to
zero. The error stays small, so linearising around it is valid, and the
quaternion stays normalised by construction.

That is Phase 3, and [the roadmap](../../docs/ROADMAP.md) commits to writing the
derivation down — because a filter you cannot derive is a filter you cannot
debug at three in the morning when it has diverged.

## 💡 Convergence, and telling the ground about it

A filter does not work instantly. It starts wrong and settles. During that time
its output must not be trusted, and something has to say so.

That is what this field is for:

```yaml
- {name: est_state, type: uint8, enum: AdcsEstState, desc: Convergence state of the attitude estimator}
```

```
   INVALID  ->  INITIALISING  ->  CONVERGING  ->  CONVERGED
```

Check it right now:

```bash
make monitor
```

```
  ADCS_HK  est_state=INVALID  q_est_0=0  q_est_1=0  q_est_2=0
```

`INVALID`, honestly, because there is no estimator yet. The mode manager
(Phase 5) will refuse to enter pointing mode unless this says `CONVERGED` —
which is the whole reason the field exists rather than the software just
assuming its own numbers are good.

## ✅ Check yourself

1. Why can combining two imperfect sensors beat either one alone?
2. In the demo, how did the filter discover a bias nobody told it about?
3. Why is a *fixed* alpha worse than a Kalman filter's changing gain?
4. Why does the estimator publish a convergence state instead of just
   publishing its best guess?

## 🎓 Go deeper

**Wahba's problem** (1965) is the general form: given several direction
measurements, find the rotation that best fits them all in a least-squares
sense. TRIAD is the two-vector special case; **QUEST** and **SVD** solve it
properly for many vectors and are what real missions use.

**Observability.** A filter can only estimate what the measurements actually
constrain. A gyroscope bias about an axis you never rotate about is
*unobservable* — no amount of filtering will find it. Knowing which parts of
your state are observable, under which conditions, is what separates a filter
that works from one that looks like it works.

---

**Next:** [Lesson 16 — Control](../16-control/) — now actually make it point.

<details>
<summary>✅ Answers</summary>

1. Because their errors have different character. The gyroscope is accurate over
   short intervals and drifts over long ones; the absolute sensor is noisy every
   instant but never drifts. Each covers the band where the other is weak.
2. From the persistent direction of the correction. If the estimate is
   repeatedly pulled the same way, that systematic pull is the bias, and
   accumulating it slowly recovers the value.
3. Because how much you *should* trust a sensor changes with circumstances — a
   sun sensor in eclipse deserves no weight at all. A fixed gain is a
   compromise that is wrong in both directions at different times.
4. Because a wrong answer presented confidently is more dangerous than no
   answer. The mode manager needs to know whether the attitude solution can be
   trusted before pointing the spacecraft using it.

</details>
