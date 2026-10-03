# `gnd/pyground/` — the Python ground station

A complete ground segment in about six hundred lines, with no dependencies
outside the Python standard library.

| File | What it is |
|---|---|
| `packets.py` | CCSDS and PUS encoding and decoding. An **independent** implementation of the same standards the C++ flight software implements |
| `client.py` | `GroundClient`: connect, uplink telecommands, reassemble and decode telemetry |
| `__main__.py` | The command-line tool |
| `dictionary.py` | **Generated** from `dictionary/mission.yaml`. Do not edit |

## Why an independent implementation

`packets.py` was written from the standards, not translated from the C++. That
independence is the point: when both sides agree on a packet, it is evidence
that the packet matches the standard, rather than evidence that the same
misreading was made twice. The CRC check value `0x29B1` for `b"123456789"` is
asserted on both sides for the same reason.

## Using it

```bash
make demo                       # a scripted pass exercising the whole slice
make monitor                    # watch the downlink, Ctrl-C to stop
make params                     # read every on-board parameter back

cd gnd
python3 -m pyground commands                                # what can be sent
python3 -m pyground send TEST_CONNECTION
python3 -m pyground send SET_PARAM param_id=1 value=250
python3 -m pyground send SET_MODE mode=POINTING             # enums by name
python3 -m pyground monitor
```

## As a library

```python
import sys; sys.path.insert(0, "gnd")
from pyground import GroundClient

with GroundClient(port=50001) as gnd:
    gnd.send("TEST_CONNECTION")
    report = gnd.wait_for("TEST_REPORT", timeout=2.0)
    print(report.summary() if report else "no answer")

    for tm in gnd.poll(timeout=5.0):
        if tm.name == "SYS_HK":
            print(tm.fields["uptime_s"], tm.fields["cpu_load_pct"])
```

## Two design choices worth noting

**`parse_tm()` never raises.** A ground system that crashes on malformed
telemetry is useless at exactly the moment it is needed. Problems are reported
in the returned object — `crc_ok`, a name of `TRUNCATED` or `UNDECODABLE` —
rather than as exceptions.

**Every command goes through the full link stack.** `send()` hands the packet
to FOP-1, which frames it, codes it into a CLTU and retransmits it until the
spacecraft's CLCW acknowledges it; `poll()` runs the downlink decoder
(frame sync, Reed-Solomon, reassembly) and keeps COP-1 serviced. `wait_idle()`
blocks until everything sent has been acknowledged. See
[docs/LINK.md](../../docs/LINK.md).

**`send_raw()` bypasses every check.** This is how the spacecraft's input
validation gets tested: deliberately corrupt packets, wrong lengths, unknown
services. It sends in a bypass (type BD) frame, outside COP-1's sequence. A
ground library that can only produce valid packets cannot test a receiver's
error handling, and the SIL suite depends on being able to send nonsense on
purpose. `send_bytes()` goes lower still: raw bytes on the uplink, below the
CLTU layer.
