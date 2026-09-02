# `dictionary/` — the single source of truth

One file lives here, and it is the most important file in the repository.

## `mission.yaml`

Every telemetry point, telecommand, event, on-board parameter and enumeration
this spacecraft has, declared once. Nothing else in the repository is permitted
to declare any of them independently.

`tools/gen.py` reads this file and writes:

| Output | Purpose |
|---|---|
| `fsw/generated/dictionary.hpp` | Identifiers, enumerations, event and parameter tables |
| `fsw/generated/telemetry.hpp` | Packed housekeeping structures with serialisers |
| `fsw/generated/commands.hpp` | Telecommand argument structures with parsers |
| `gnd/openc3/targets/SAT/cmd_tlm/` | OpenC3 COSMOS command and telemetry definitions |
| `gnd/openc3/targets/SAT/screens/` | A COSMOS overview screen |
| `gnd/pyground/dictionary.py` | The Python ground client's tables |
| `docs/ICD.md` | The interface control document |

## Why this exists

In a conventionally built spacecraft, a telemetry field is defined in at least
four places: the flight software structure, the serialisation code, the ground
system's telemetry database, and the interface document. Those four drift. The
drift is discovered during integration, or in orbit, and the cost of finding it
late is enormous.

Here there is one definition and five projections of it. Adding a field is a
one-line change followed by `make gen`. Disagreement between the spacecraft and
the ground is not a bug that can be introduced.

## Changing it

```bash
$EDITOR dictionary/mission.yaml
make gen          # regenerates everything; validates as it goes
make build test   # the C++ side must still compile and pass
```

The generator validates before it writes anything: duplicate structure
identifiers, duplicate `(service, subtype)` pairs, duplicate event or parameter
identifiers, unknown types, references to enumerations that do not exist, and
default parameter values outside their own declared limits. A dictionary that
is internally inconsistent fails loudly rather than producing subtly wrong code.

## Rules for editing

- **Never renumber anything that has flown.** APIDs, structure identifiers,
  event identifiers, parameter identifiers and failure codes are the interface.
  A ground system, a stored telemetry archive and an operations procedure all
  depend on them. Add new numbers; do not reuse old ones.
- **Every field needs a real description.** It appears in the ICD, in the
  generated C++ comments, and on the operator's screen. `desc: "the value"`
  helps nobody at three in the morning.
- **Declare units.** They propagate to COSMOS displays and the ICD.
- **Give parameters honest limits.** The flight software enforces them, and a
  limit that is wider than the value the subsystem can actually tolerate is a
  loaded gun.
