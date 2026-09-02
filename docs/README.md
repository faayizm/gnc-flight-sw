# `docs/` — documentation

| File | What it is | Written by |
|---|---|---|
| [`ARCHITECTURE.md`](ARCHITECTURE.md) | Why the system is shaped this way. The decisions, including the rejected alternatives | Hand |
| [`ROADMAP.md`](ROADMAP.md) | The seven phases: what exists, what does not, what each one delivers | Hand |
| [`PATHS.md`](PATHS.md) | Complete annotated map of every directory and file | Hand |
| [`ICD.md`](ICD.md) | Interface control document: every packet, field, command and parameter | **Generated** by `make gen` |

`ICD.md` is generated from `dictionary/mission.yaml` and must not be edited by
hand. It is the document you would send to whoever is building the ground
segment or the next subsystem.

## Learning material

[`../learn/`](../learn/) is a separate thing from this directory and serves a
different reader. `docs/` is reference material for someone working on the
system; `learn/` is an eighteen-lesson course for someone meeting these ideas
for the first time, graduated so that a ten-year-old can do the first nine
lessons and an engineer still finds something in the last nine.

If a concept here needs explaining rather than specifying, it belongs there.

## Where the rest of the documentation is

Most of it is not here. It is in the code, next to the decision it explains:

- Every directory has a `README.md` saying what belongs in it and what does not
- Every header opens with why the component exists and what it deliberately
  does not do
- Rejected alternatives are recorded where they were rejected — why the
  scheduler is not threaded, why the software bus dispatches synchronously, why
  the parameter store holds everything as a double

That placement is deliberate. Documentation in a separate directory goes stale
because nothing forces a reader to look at it while changing the code; a
comment above the function being edited does not.
