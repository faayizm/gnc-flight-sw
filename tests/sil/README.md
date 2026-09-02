# `tests/sil/` — software-in-the-loop tests

Nothing is mocked. These start the real `build/fsw` binary, connect over a real
TCP socket, send real CCSDS packets, and assert on what comes back.

## `test_endtoend.py`

35 checks across nine scenarios:

| Scenario | What it establishes |
|---|---|
| `link_and_connection_test` | ST[17,1] produces acceptance, a report, and completion |
| `periodic_housekeeping` | All three structures are generated unprompted, load has margin, no overruns, and **sequence counts advance without gaps** |
| `parameter_read_and_write` | ST[20] round-trips, and a shortened period really does speed telemetry up |
| `out_of_range_parameter_is_refused` | Rejection with `ILLEGAL_ARG`, and the old value is kept — not clamped |
| `unknown_service_is_rejected` | Acceptance failure quoting back the offending sequence count |
| `corrupted_telecommand_is_dropped_silently` | Never accepted, never executed, but **reported as an event** — the one rejection that produces no verification report |
| `housekeeping_can_be_silenced_and_restored` | ST[3,5] and ST[3,6] work, and affect only the named structure |
| `parameters_survive_a_restart` | The CRC-protected non-volatile store actually persists |
| `reconnection` | A ground tool disconnecting does not disturb the spacecraft |

## Running

```bash
make sil
```

Takes about eighteen seconds, most of it waiting for real periodic telemetry.

## How it stays reliable

- **Every spacecraft gets its own port**, requested from the operating system,
  so parallel runs and CI never collide.
- **Every spacecraft gets its own scratch non-volatile file**, deleted
  afterwards, so one test's parameter changes cannot leak into another's.
- **`GroundClient.connect()` retries briefly.** A test that starts the binary
  and immediately connects would otherwise race the spacecraft's `bind()` and
  fail intermittently — the worst kind of failure to debug.
- **Thresholds have margin.** The housekeeping-rate check allows for scheduling
  jitter rather than asserting an exact count.

## Writing a new one

Use the `Spacecraft` context manager, which handles the port, the scratch
storage and termination:

```python
def test_something() -> None:
    print("\n[my area]")
    with Spacecraft() as sat, sat.client() as gnd:
        gnd.send("TEST_CONNECTION")
        check(gnd.wait_for("TEST_REPORT", timeout=2.0) is not None,
              "a plain-English statement of what must be true")
```

`check()` records rather than raising, so one failure does not hide the ten
after it.
