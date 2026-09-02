# `fsw/generated/` — generated code

**Do not edit anything in this directory.** Every file is overwritten by
`make gen`.

| File | Contains |
|---|---|
| `dictionary.hpp` | APIDs, mission enumerations with `to_string`, housekeeping structure identifiers, the event table, the parameter table |
| `telemetry.hpp` | One packed structure per housekeeping report, with big-endian serialiser and deserialiser |
| `commands.hpp` | The telecommand table, and one argument structure per command with its parser |

Source: `dictionary/mission.yaml`. Generator: `tools/gen.py`.

## To change something here

Edit the dictionary, then:

```bash
make gen
make build test
```

## Why it is committed rather than built

So that the repository can be read and the flight software built without Python
or PyYAML, and so that a change to the generator shows up in review as a diff
of its output — which is where an unintended consequence is actually visible.

CI regenerates and fails if the result differs from what is committed, so the
two cannot silently diverge.

## Design notes on the generated code

- **Serialisation is field by field, never `memcpy` of a structure.** Structure
  padding and layout are implementation-defined; the wire format is not.
- **Everything is big-endian**, because CCSDS says so.
- **Tables are `inline constexpr`**, so they live in ROM and need no separate
  translation unit.
- **Enumeration constants keep the dictionary's exact spelling**, so a name in
  the ICD, on a COSMOS screen and in the C++ source is one string, greppable
  across the whole repository.
