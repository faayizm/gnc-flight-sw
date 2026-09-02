# `gnd/` — the ground segment

> 📚 **Learning this?** See [Lesson 2 — First contact](../learn/02-first-contact/) in the lesson track.


Two ground systems, deliberately. They have different jobs and neither replaces
the other.

```
gnd/
├── pyground/   Python client and CLI.  The engineer's tool and the test driver.
└── openc3/     OpenC3 COSMOS plugin.   The operator's tool.
```

| | `pyground` | OpenC3 COSMOS |
|---|---|---|
| Needs | Python 3.10+ and nothing else | Docker, several containers |
| Starts in | Milliseconds | A minute or so |
| Good for | Scripting, automated tests, protocol debugging | Watching, limits, logging, real operations |
| Telemetry screens | A scrolling terminal | Real ones, with graphs and colour |
| Role | **Reference implementation** of the protocol | The realistic operations environment |

Use `pyground` when the question is *"is the spacecraft or the ground tool
wrong?"*, because it is small enough to read in one sitting. Use COSMOS when
the question is *"what is the spacecraft doing?"*

## They cannot disagree with the spacecraft

Both are generated from `dictionary/mission.yaml`:

- `gnd/pyground/dictionary.py` — the Python client's tables
- `gnd/openc3/targets/SAT/cmd_tlm/` — the COSMOS command and telemetry database
- `gnd/openc3/targets/SAT/screens/` — a COSMOS overview screen

Run `make gen` after any dictionary change and both ground systems know about
it immediately.

## The link

Raw CCSDS Space Packets over TCP. The **spacecraft listens** on port 50001 and
the ground connects, which is the reverse of the physical situation and is
deliberate: the spacecraft is the long-lived process, and a ground system that
can attach and detach at will is far easier to work with.

Only one ground system can be connected at a time. Stop `pyground` before
starting COSMOS, or run the spacecraft twice on different ports.
