# `tests/unit/` — unit tests

Fast and hermetic: no sockets, no files, no sleeping, no spacecraft.

| File | Covers | Worth reading for |
|---|---|---|
| `test_crc.cpp` | `core/crc.hpp` | The standard check value `0x29B1`, and the identity that lets a receiver verify a packet without slicing off its CRC |
| `test_bytes.cpp` | `core/bytes.hpp` | Byte order asserted against **literal octets**, not by round-tripping |
| `test_containers.cpp` | `static_vector`, `ring_buffer` | Overflow behaviour, which is why these types exist |
| `test_space_packet.cpp` | `apps/ttc/space_packet` | The "length minus one" encoding, from every angle |
| `test_pus.cpp` | `apps/ttc/pus` | Telecommand validation: corruption, truncation, lying length fields, wrong packet type |
| `test_scheduler.cpp` | `core/scheduler` | Rates, offsets, ordering, deadline detection, and no catch-up ticks |
| `test_param_store.cpp` | `core/param_store` | Range checks, NaN, corruption recovery, defaults surviving a bad load |
| `test_bus.cpp` | `core/bus` | Topic isolation, synchronous delivery, bounded subscriptions |

## Byte order is checked against literals, deliberately

```cpp
CHECK_EQ(buf[0], static_cast<uint8_t>(0x12));
CHECK_EQ(buf[1], static_cast<uint8_t>(0x34));
```

rather than writing a value and reading it back. A reader and writer that are
both wrong in the same way round-trip perfectly and cannot talk to any real
ground system. The same reasoning drives the CRC check value: `0x29B1` for
`b"123456789"` is what every other implementation of CRC-16/CCITT-FALSE
produces, so matching it is evidence of interoperability rather than
self-consistency.

## The fake clock

`test_scheduler.cpp` defines a `TestClock` that moves only when the test says
so, and can charge a fixed cost to each task. That is what makes it possible to
test a deadline overrun without actually being slow, and to test an hour of
scheduling in microseconds. It is the clearest payoff of putting time behind
`hal::IClock`.

## Adding a test

Add it to an existing file if it fits, then add the file to `FSW_TEST_SOURCES`
in `tests/CMakeLists.txt` if it is new. Registration is automatic.
