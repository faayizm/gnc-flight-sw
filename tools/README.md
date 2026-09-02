# `tools/` — the code generator

## `gen.py`

Reads `dictionary/mission.yaml` and projects it into flight code, ground
configuration and documentation. Run it with `make gen`.

```
dictionary/mission.yaml
          │
          ▼
     tools/gen.py ──┬──▶ fsw/generated/dictionary.hpp    ids, enums, tables
                    ├──▶ fsw/generated/telemetry.hpp     structs + serialisers
                    ├──▶ fsw/generated/commands.hpp      arg structs + parsers
                    ├──▶ gnd/openc3/plugin.txt           COSMOS interface
                    ├──▶ gnd/openc3/targets/SAT/…        COSMOS cmd/tlm/screens
                    ├──▶ gnd/pyground/dictionary.py      Python ground tables
                    └──▶ docs/ICD.md                     interface control doc
```

## How it is structured

- `Dictionary` — parses and **validates**. Every consistency rule lives in
  `_validate()`, and a violation exits non-zero before a single file is written.
  Half-generated output is worse than none.
- `TYPES` — the one table mapping a dictionary type name onto its C++ type,
  its size in octets, its COSMOS type and bit width, and its Python `struct`
  code. Adding a type means adding one row here.
- `gen_*` functions — one per output. Each is a pure function from the parsed
  dictionary to a string, which makes them trivial to test and to diff.
- `write()` — compares against what is already on disk and reports whether each
  output actually changed, so a no-op regeneration is visibly a no-op.

## Why generated code is committed

`fsw/generated/` and `gnd/openc3/targets/` are checked in rather than built.
That is a deliberate trade:

- someone can read the repository, or build the flight software, without
  Python or PyYAML installed;
- a change to the generator shows up in review as a diff of its *output*, which
  is where an unintended consequence is actually visible;
- the CI job regenerates and fails if the result differs from what is
  committed, so the two cannot silently diverge.

## Requirements

Python 3.10+ and PyYAML. `make venv` creates a local virtual environment with
it. Nothing else in the repository needs PyYAML.
