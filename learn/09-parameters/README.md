# Lesson 9 — Parameters

🚀 **Explorer** · about 20 minutes

---

## ❓ The question

Your satellite is in orbit. You realise the detumble control is a little too
aggressive and you want to soften it.

You cannot go up there. Uploading new software to a spacecraft is possible but
risky — a bad upload can end a mission. Is there a safer way to change how it
behaves?

## 💡 The idea

Yes: decide in advance which numbers might need to change, and make those
adjustable from the ground. They are called **parameters**, and on a real
mission they are often the *only* way behaviour gets changed after launch.

This spacecraft has eight, declared in
[`dictionary/mission.yaml`](../../dictionary/mission.yaml):

| ID | Name | Default | Range | What it controls |
|---|---|---|---|---|
| 1 | `SYS_HK_PERIOD_MS` | 1000 | 100–60000 | How often system telemetry is sent |
| 2 | `ADCS_HK_PERIOD_MS` | 1000 | 100–60000 | How often attitude telemetry is sent |
| 3 | `EPS_HK_PERIOD_MS` | 1000 | 100–60000 | How often power telemetry is sent |
| 4 | `DETUMBLE_RATE_DPS` | 2.0 | 0.1–30 | Spin rate above which we detumble |
| 5 | `POINTING_RATE_DPS` | 0.5 | 0.01–10 | Spin rate below which we may point |
| 6 | `BATT_LOW_SOC_PCT` | 40 | 5–90 | Battery level counting as "low" |
| 7 | `BATT_CRIT_SOC_PCT` | 20 | 2–80 | Battery level counting as "critical" |
| 8 | `LINK_TIMEOUT_S` | 300 | 10–86400 | Silence before autonomy reacts |

Notice every one has a **declared range**. That is the heart of this lesson.

## 👀 See it

Start your spacecraft, then:

```bash
make params
```

```
 ID  NAME                         ON-BOARD       DEFAULT  UNITS
----------------------------------------------------------------------
  1  SYS_HK_PERIOD_MS                 1000          1000  ms
  2  ADCS_HK_PERIOD_MS                1000          1000  ms
  3  EPS_HK_PERIOD_MS                 1000          1000  ms
  4  DETUMBLE_RATE_DPS                   2           2.0  deg/s
  ...
```

The ground station just asked the spacecraft for every parameter, one
`ST[20,1]` at a time, and the spacecraft answered with `ST[20,2]`.

Now change one:

```bash
cd gnd
python3 -m pyground send SET_PARAM param_id=1 value=250
```

Telemetry speeds up to four times a second. Change it back with `value=1000`.

## 🧪 Try it — break it on purpose

The minimum for parameter 1 is 100. Ask for 5:

```bash
python3 -m pyground send SET_PARAM param_id=1 value=5
```

```
  VERIF_ACCEPT_OK      tc(apid=0x00A, seq=0)
  VERIF_COMPLETE_FAIL  tc(apid=0x00A, seq=0) reason=ILLEGAL_ARG
```

Refused. Now check what the value actually is:

```bash
python3 -m pyground send REPORT_PARAM param_id=1
```

Still 1000. **Not 100.**

This is the single most important design decision in the parameter system, and
it is worth stating explicitly:

> **A rejected value is not clamped to the nearest legal one.**

Why does that matter so much? Imagine it clamped silently. The operator sends 5,
the spacecraft quietly uses 100, and the operator believes the spacecraft is
running at 5. Every decision they make afterwards is built on a false belief
about the state of a machine they cannot see.

Refusing loudly is uncomfortable. Silently doing something different is
dangerous.

## 🔍 In the code

[`fsw/core/param_store.hpp`](../../fsw/core/param_store.hpp):

```cpp
Status set(dict::ParamId id, double value) {
    const size_t index = index_of(id);
    if (index >= dict::kParamCount) { return Status::NotFound; }

    const dict::ParamInfo& info = dict::kParams[index];
    if (value < info.min_value || value > info.max_value) {
        return Status::OutOfRange;
    }
    // Reject NaN, which compares false against every bound and would
    // otherwise slip through the check above.
    if (!(value == value)) { return Status::Invalid; }

    values_[index] = quantise(info.type, value);
    dirty_ = true;
    return Status::Ok;
}
```

That NaN check deserves a moment. **NaN** — "not a number" — is a special
floating-point value produced by things like 0÷0. It has a bizarre property:
*every* comparison with it is false. `nan < 100` is false. `nan > 60000` is
also false. So a naive range check lets it straight through, and from then on
every calculation touching that parameter produces NaN too, silently spreading
through the whole system.

`value == value` is false only for NaN. It is a strange-looking line with a
very good reason, and there is a test for it:

```bash
./build/tests/fsw_tests 2>&1 | grep nan
```

## 💡 Surviving a restart

Parameters live in non-volatile memory so a reset does not lose them. But
memory in orbit cannot be trusted — radiation flips bits, and a reset partway
through a write leaves a half-written block.

So the stored block carries a **CRC**, exactly as in Lesson 4, and the boot
sequence in [`fsw/main.cpp`](../../fsw/main.cpp) is deliberate:

```cpp
// Defaults first, unconditionally. Whatever happens next, the spacecraft is
// already running on values known to be within their declared limits.
params.reset_to_defaults();

if (is_ok(storage.read(kParamBlock, nvm_block, sizeof nvm_block))) {
    const Status s = params.load(nvm_block, sizeof nvm_block);
    if (!is_ok(s)) {
        std::fprintf(stderr, "note: stored parameters not usable (%s), "
                             "running on compiled-in defaults\n", to_string(s));
    }
}
```

Read that order again. **Defaults are loaded first, always.** Then the stored
values are attempted. If they fail — bad CRC, wrong count, out of range — the
spacecraft is *already* running on known-good numbers and simply says so.

It never boots on a value it cannot vouch for.

You can watch this happen. The very first line when you start a fresh
spacecraft is:

```
note: stored parameters not usable (IO_ERROR), running on compiled-in defaults
```

That is the file being all zeros on first run. Exactly the same path that
protects you after corruption.

## 🧪 Try it — persistence

```bash
cd gnd
python3 -m pyground send SET_PARAM param_id=4 value=7.5
```

Stop the spacecraft with `Ctrl-C`. Start it again with `make run`. Then:

```bash
python3 -m pyground send REPORT_PARAM param_id=4
```

Still 7.5. It survived the restart. There is a software-in-the-loop test that
checks exactly this, so it cannot quietly break:

```bash
make sil 2>&1 | grep -A1 persistence
```

## 🎓 Go deeper — the three protections

Every one of these is enforced in `ParamStore` rather than in each caller,
which means there is no path around them:

1. **Range checking**, from the dictionary. An out-of-range value never reaches
   flight code, and is reported rather than clamped.
2. **A CRC on stored values.** Corruption is detected and the compiled-in
   defaults are used instead.
3. **Re-validation on load.** A value that was legal when written is checked
   again against the *current* limits — because a software upload may have
   tightened a bound since. There is a test for that too.

**One simplification, stated honestly.** Every value is held internally as a
`double`. That is exact for any integer up to 2^53, which covers every
parameter type the dictionary currently allows. The trade is that one
representation goes over the wire and there is no union to get wrong. It is
documented at the top of `param_store.hpp` rather than left for someone to
discover.

## ✅ Check yourself

1. Why refuse an out-of-range value instead of clamping it to the nearest
   legal one?
2. Why is `value == value` in the code, and what does it catch?
3. Why load the defaults *before* trying the stored values, rather than only
   as a fallback afterwards?
4. A stored value passes its CRC but is outside the current declared range.
   What should happen, and why?

---

**Next:** [Lesson 10 — The heartbeat](../10-the-heartbeat/) — why flight
software uses one thread when your laptop uses hundreds.

<details>
<summary>✅ Answers</summary>

1. Because clamping leaves the operator believing the spacecraft is doing
   something it is not. Every subsequent decision would rest on a false model
   of a machine nobody can inspect. Loud refusal is safer than quiet
   substitution.
2. It is a NaN test — NaN is the only value not equal to itself. It catches a
   value that would pass both range comparisons (since every comparison with
   NaN is false) and then silently poison every calculation downstream.
3. So that the spacecraft is running on known-good values at every instant,
   including while the load is being attempted and including on every failure
   path. There is no window where it holds nothing valid.
4. Refuse it and keep the default. The limits may have been tightened by a
   software upload since it was written, so "it was legal once" is not evidence
   that it is safe now.

</details>
