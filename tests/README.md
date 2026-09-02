# `tests/` — the test suites

Two suites with genuinely different jobs.

```
tests/
├── framework.hpp   a dependency-free C++ test framework, about 100 lines
├── unit/           fast, hermetic tests of the flight core        72 tests
└── sil/            software-in-the-loop against the real binary   35 checks
```

| | `unit/` | `sil/` |
|---|---|---|
| Runs | The flight code, in-process | The real `fsw` binary, over a real socket |
| Time | Under a second | About 18 seconds |
| Clock | A fake clock the test controls | Real |
| Catches | Logic errors inside a component | Errors in how components fit together |

Both matter, and neither substitutes for the other. Unit tests cannot catch a
packet that is assembled correctly and framed wrongly; SIL tests cannot
efficiently explore the boundary conditions of a CRC.

## Running

```bash
make test      # unit tests
make sil       # software-in-the-loop (builds first if needed)
make check     # everything: generate, build, unit, SIL, layering
```

## No external test framework

`framework.hpp` has no dependencies. This project's central claim is that the
flight code builds anywhere; a test suite that needs a network fetch and a C++
package manager would quietly undermine it. The framework provides `TEST`,
`CHECK`, `CHECK_EQ`, `CHECK_NE` and `CHECK_NEAR`, which has been sufficient so
far. If it stops being sufficient, that is the moment to reconsider — not
before.

## What a good test looks like here

Test names are sentences describing behaviour, because a failing test's name is
the first and often only thing anyone reads:

```
  x  a_corrupted_telecommand_is_rejected_for_crc_before_anything_else
  x  the_rejected_write_left_the_old_value_untouched_not_clamped
  x  a_late_tick_does_not_trigger_catch_up_ticks
```

The most valuable tests here assert things that must **not** happen: that a bad
CRC is never interpreted, that an out-of-range parameter is not clamped, that a
late tick does not cause a burst of catch-up work. Those are the properties
that get broken by well-meaning changes.
