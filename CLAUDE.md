# Working on this repository

HYPERSAT: a teaching flight software stack (C++17 flight code, Python simulator
and ground station) plus an 18-lesson course in `learn/`. The roadmap is
`docs/ROADMAP.md`. **If `docs/HANDOFF.md` exists, read it first:** it holds the
state of work in progress and what to do next.

## Next up

Phases 0–7 of the roadmap are done. If the user just says "continue", work
through this list in order, tick items off here as they are finished, and
commit the updated list with the work:

1. **Health check.** Install the board toolchain (see Practicalities), then
   run `make check`, `make target` and `make target-sil`. Fix anything that
   is broken before starting anything else.
2. **Review the whole course for accuracy.** Read Lessons 1–18, the
   glossary and the toolbox as a student would, in order. Fix stale facts,
   numbers and commands (refresh quoted output from fresh runs), broken
   cross-references, and anything one lesson says that a later phase made
   untrue. Report what was changed and why.
3. **Propose a Phase 8**, for example a sun-pointing safe mode (the gap noted
   in `docs/ARCHITECTURE.md`). Write the proposal into `docs/ROADMAP.md` and
   **ask the user before building it**.

## Build and test

| Command | What | Time |
|---|---|---|
| `make build` / `make test` | Compile; 141+ unit tests | ~1 min |
| `make sil` | End-to-end tests against the real binary | ~30 s |
| `make detumble` | Includes the determinism check: hash must stay `06721ea6df543912` unless physics or control deliberately changed | ~1 min |
| `make pointing`, `store-forward`, `power`, `fdir`, `radiation` | Flight scenarios, asserted on simulator truth | ~1 min each |
| `make monte-carlo RUNS=8` | Dispersed wheel failures, in parallel | ~1 min |
| `make check-lessons` | Runs the lessons' own commands and checks the output they quote | ~1 min |
| `make target`, `target-sil` | The Cortex-M7 board (FreeRTOS) cross-compiled, and the SIL tests against it in QEMU | ~1 min each |
| `make target-detumble`, `budget` | A whole scenario on the board; `docs/BUDGET.md` regenerated | ~15 min, ~5 min |
| `make check` | Everything CI runs on the laptop build (the board has its own CI job) | ~15 min; run it in the background |

CI (`.github/workflows/ci.yml`) also builds with clang and runs unit + SIL
tests under ASan and UBSan. **gcc Debug + `-fsanitize=undefined` reports
`-Wsign-conversion` cases the normal build does not** (e.g. `(x >> i) & 1u`
with `int i`): write `((x >> i) & 1) != 0`.

## Rules the code keeps

- Flight code (`fsw/core`, `fsw/apps`): no heap, no exceptions, no RTTI, all
  warnings as errors. No OS headers outside `fsw/platform/`
  (`make check-layering`). Every object has static storage duration.
- `dictionary/mission.yaml` is the single source of truth. Never hand-edit
  `fsw/generated/`, `gnd/openc3/`, `gnd/pyground/dictionary.py` or
  `docs/ICD.md`: edit the dictionary, `make gen`, commit both.
- Determinism: decisions about the physics run on **sensor time** (once per
  sensor sample), never on the scheduler clock, so a run is identical at any
  time scale and host load. Only watchdog-like checks of the host use host
  time (`IClock::host_now`).
- Python is standard library only.
- `fsw/spacecraft.hpp` is the one composition of the spacecraft; each
  platform's `main` only builds the four HAL ports and runs the loop.

## The course is part of the deliverable

The user wants the lessons kept in step with the code, phase by phase.

- Every phase updates the lessons it touches, in the same piece of work.
- **Only paste output from a fresh run, from a single run.** Never write
  numbers from memory or stitch lines from two runs. `make check-lessons`
  enforces the lines it knows about; add checks for new hands-on sections.
- When something real goes wrong while building, it often makes the best
  teaching story (see the 🔍 boxes in Lessons 15–18). Tell it honestly.
- `learn/GLOSSARY.md` gets every new term; `learn/toolbox/` programs must run
  with `make check-toolbox`.

## Practicalities

- Develop on the branch named in the session instructions; push with
  `git push -u origin <branch>`. Do not open a PR unless asked.
- Never `pkill -f <pattern>` that also matches your own shell command line; it
  kills the shell. Kill by PID (write PIDs to a file) or use `pkill -x fsw`.
- The board build needs `gcc-arm-none-eabi libnewlib-arm-none-eabi
  libstdc++-arm-none-eabi-newlib qemu-system-arm` (apt; not persistent across
  sessions). `FSW_TARGET=qemu` points the scenario harness and SIL tests at it.
  The board flies at about 5x real time at most: its UART is the limit.
- Scenario harnesses start `build/fsw` on free ports; `make run` uses port
  50001 and `hypersat_nvm.bin` in the repo root (delete it after experiments).
