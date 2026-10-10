# Working on this repository

HYPERSAT: a teaching flight software stack (C++17 flight code, Python simulator
and ground station) plus an 18-lesson course in `learn/`. The roadmap is
`docs/ROADMAP.md`. **If `docs/HANDOFF.md` exists, read it first:** it holds the
state of work in progress and what to do next.

## Build and test

| Command | What | Time |
|---|---|---|
| `make build` / `make test` | Compile; 141+ unit tests | ~1 min |
| `make sil` | End-to-end tests against the real binary | ~30 s |
| `make detumble` | Includes the determinism check: hash must stay `06721ea6df543912` unless physics or control deliberately changed | ~1 min |
| `make pointing`, `store-forward`, `power`, `fdir`, `radiation` | Flight scenarios, asserted on simulator truth | ~1 min each |
| `make monte-carlo RUNS=8` | Dispersed wheel failures, in parallel | ~1 min |
| `make check-lessons` | Runs the lessons' own commands and checks the output they quote | ~1 min |
| `make check` | Everything CI runs | ~15 min; run it in the background |

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
- Scenario harnesses start `build/fsw` on free ports; `make run` uses port
  50001 and `hypersat_nvm.bin` in the repo root (delete it after experiments).
