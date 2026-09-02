# `gnd/openc3/` — the OpenC3 COSMOS plugin

Configuration only. No COSMOS source is vendored here.

| Path | What it is |
|---|---|
| `plugin.txt` | Target and interface definition. **Generated** |
| `targets/SAT/target.txt` | Target settings. **Generated** |
| `targets/SAT/cmd_tlm/tlm.txt` | Every telemetry packet. **Generated** |
| `targets/SAT/cmd_tlm/cmd.txt` | Every telecommand. **Generated** |
| `targets/SAT/screens/overview.txt` | A spacecraft overview screen. **Generated** |

All of it comes from `dictionary/mission.yaml` via `make gen`. Editing these
files by hand means losing the change on the next regeneration — and, worse,
introducing exactly the flight-versus-ground drift this repository exists to
prevent. Change the dictionary instead.

## Why COSMOS rather than building a ground tool

[OpenC3 COSMOS](https://openc3.com) already does telemetry screens, limits
monitoring, packet logging, command sending and scripting, properly, and it is
open source. Rebuilding that would be weeks of work spent learning nothing
about flight software.

**Licensing:** COSMOS Core is AGPL-3.0. It is used here as a separate tool that
this repository *configures*; no COSMOS code is copied into this tree. That
keeps this repository cleanly Apache-2.0.

## Running it

COSMOS runs in Docker. See `INSTALL.md` in this directory for the current
procedure. In outline:

1. Start COSMOS from its own installation.
2. Build this directory into a plugin and install it through the COSMOS admin
   interface.
3. Start the flight software: `make run`.
4. The `SAT_INT` interface connects to `host.docker.internal:50001`.

The `host.docker.internal` hostname is how a container reaches a process on the
host. On Linux this may need `--add-host=host.docker.internal:host-gateway`, or
change the `sat_host` variable in `plugin.txt` to the host's address.

## The framing configuration

```
LENGTH 32 16 7 1 BIG_ENDIAN 0 nil nil true
```

Read as: the length field is 16 bits at bit offset 32; add 7 to get the total
packet size (6 header octets, plus 1 because CCSDS stores "length minus one");
one byte per count; big-endian; discard no leading bytes; no sync pattern.

That is the CCSDS Space Packet header, and the `7` is the same "minus one"
arithmetic that `SpacePacketHeader::total_size()` performs in the flight
software.

## If a packet does not decode

The Python ground station is the tiebreaker. `make monitor` uses an
independent implementation of the same standards; if it decodes a packet that
COSMOS does not, the problem is in this configuration rather than in the
spacecraft.
