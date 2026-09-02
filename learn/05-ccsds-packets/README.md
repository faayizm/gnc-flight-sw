# Lesson 5 — CCSDS packets

🚀 **Explorer** · 🔧 Builder · about 30 minutes

---

## ❓ The question

A spacecraft built in India needs to be tracked by a ground station in Spain,
using software written in California, twenty years ago.

How is that possible? Somebody has to have agreed on the shape of a message.

## 💡 The idea

They did. The **Consultative Committee for Space Data Systems** — CCSDS — is a
body where the world's space agencies agree on how spacecraft talk. NASA, ESA,
JAXA, ISRO, Roscosmos, CNSA and dozens more.

Their most fundamental agreement is the **Space Packet**, and it is the
universal envelope of spaceflight. Six bytes of header, then your data.

```
   ┌───────────────────────┬──────────────────────────────┐
   │   PRIMARY HEADER      │        DATA FIELD            │
   │      6 bytes          │      1 to 65536 bytes        │
   └───────────────────────┴──────────────────────────────┘
```

Everything rides inside one: telemetry, commands, file transfers, science data,
images from Mars.

## 💡 What is in those six bytes

```
    byte 0    byte 1    byte 2    byte 3    byte 4    byte 5
    00011000  00001010  11000000  00000000  00000000  00010000
    [1]23[----4-----] 55[------6------] [-------7-------]
```

| # | Field | Bits | What it means |
|---|---|---|---|
| 1 | Version | 3 | Always `000` |
| 2 | Type | 1 | `0` = telemetry (down), `1` = telecommand (up) |
| 3 | Secondary header flag | 1 | `1` = more header follows |
| 4 | **APID** | 11 | *Which part* of the spacecraft is talking |
| 5 | Sequence flags | 2 | `11` = a whole message, not a fragment |
| 6 | **Sequence count** | 14 | Counts up. A gap means a packet was lost |
| 7 | **Data length** | 16 | How much data follows — *careful!* |

Three of those deserve attention.

**APID** — Application Process Identifier. Think of it as a return address
inside the spacecraft. In this project:

| APID | Who |
|---|---|
| `0x001` | The communications software |
| `0x002` | Attitude control |
| `0x003` | Power |
| `0x00A` | The ground station |

**Sequence count** is how the ground detects a lost packet. Each APID counts
its own packets: 1, 2, 3, 4… If the ground sees 5 then 7, packet 6 was lost in
the noise. It counts to 16,383 and wraps back to 0. There is a test in this
repository that checks exactly that:

```bash
./build/tests/fsw_tests 2>&1 | grep wraps
```

**Data length** is the famous trap. Read on.

## ⚠️ The minus-one trap

The length field does **not** hold the length. It holds the length **minus
one**.

```
   length field says 16   →   the data is actually 17 bytes
   total packet = 6 + 16 + 1 = 23 bytes
```

Why? Because a packet with an *empty* data field is not allowed, so storing 0
would be wasted. Storing length−1 buys one extra byte of range.

It is a completely reasonable decision that has caused approximately every
ground station integration bug in the history of spaceflight.

This is why, in this repository, that arithmetic exists in exactly **one
place** — [`fsw/apps/ttc/space_packet.hpp`](../../fsw/apps/ttc/space_packet.hpp):

```cpp
// Octets in the data field, i.e. everything after the primary header.
uint16_t data_field_bytes() const { return data_length + 1; }

// Total octets on the wire, header included.
size_t total_size() const { return kSpacePacketHeaderBytes + data_field_bytes(); }
```

Nothing else in the flight software is allowed to add or subtract that 1. And
[`tests/unit/test_space_packet.cpp`](../../tests/unit/test_space_packet.cpp)
attacks it from every angle, including the case that cannot be represented at
all:

```cpp
TEST(space_packet, a_zero_length_data_field_is_not_representable) {
    SpacePacketHeader h;
    CHECK(!h.set_data_field_bytes(0));
}
```

## 👀 See it — take a real packet apart

```bash
python3 learn/toolbox/packet_explorer.py
```

This takes a real command — "send housekeeping four times a second" — and pulls
it apart byte by byte, with the bit diagram above generated from the actual
bytes so it can never be misaligned.

Then, with your spacecraft running (`make run` in another terminal):

```bash
python3 learn/toolbox/packet_explorer.py --live
```

Now it grabs a **real packet off the socket**, from the running flight
software, and explains it the same way.

## 🧪 Try it — decode one by hand

This is the exercise that makes it stick. Here is a real packet:

```
      18 0A C0 00 00 10 29 14 03 00 00 00 01 40 6F 40 00 00 00 00 00 7E 40
```

Work out, on paper:

1. Is it a telecommand or telemetry? (byte 0, bit 3)
2. Which APID sent it? (bytes 0–1, bottom 11 bits)
3. What is the sequence count? (bytes 2–3, bottom 14 bits)
4. How many bytes is the whole packet? (bytes 4–5, then remember the trap)

Hints: `0x18` is `00011000` in binary. `0x0A` is `00001010`.

Check your answers by running the explorer. When you can do this, you can read
the telemetry of any spacecraft in the world — the header is the same on all of
them.

## 🔍 In the code

| Piece | File |
|---|---|
| The header structure and the minus-one arithmetic | [`space_packet.hpp`](../../fsw/apps/ttc/space_packet.hpp) |
| Packing the bits | [`space_packet.cpp`](../../fsw/apps/ttc/space_packet.cpp) |
| Tests, including every boundary | [`test_space_packet.cpp`](../../tests/unit/test_space_packet.cpp) |

Look at how the bits are packed:

```cpp
const uint16_t word0 =
    ((version & 0x7) << 13) |     // 3 bits, at the top
    (type << 12) |                // 1 bit
    (secondary_hdr << 11) |       // 1 bit
    (apid & kApidMask);           // 11 bits, at the bottom
```

`<<` shifts bits left into position; `|` merges them; `&` masks off anything
that would not fit. Note the masking — there is a test proving that an
oversized APID gets truncated to 11 bits rather than spilling over and
corrupting the version field beside it.

## 🎓 Go deeper

**This is not the whole story.** Over a real radio there is another layer
underneath: **transfer frames** (CCSDS 132.0-B and 231.0-B), carrying a fixed
sync marker, pseudo-randomisation, and Reed–Solomon error correction. The sync
marker is what lets a receiver find where a frame *starts* after a burst of
noise.

This project sends Space Packets straight over TCP, which works only because
TCP never loses or reorders bytes. That limitation is stated plainly in
[`../../docs/ARCHITECTURE.md`](../../docs/ARCHITECTURE.md#known-limitations-stated-rather-than-hidden)
and is Phase 4 of [the roadmap](../../docs/ROADMAP.md).

**Segmentation.** Sequence flags can also say "this is the first fragment of a
big message", "middle", or "last" — for data too big for one packet. This
project only ever uses `11` (unsegmented) and says so explicitly.

## ✅ Check yourself

1. A length field reads 41. How big is the whole packet?
2. The ground sees sequence counts 100, 101, 103. What happened?
3. Why does each APID have its *own* sequence counter instead of one shared
   counter?
4. Why is `total_size()` written in exactly one place in the whole codebase?

---

**Next:** [Lesson 6 — PUS services](../06-pus-services/) — CCSDS says how to
wrap a message; now we need to agree what it *means*.

<details>
<summary>✅ Answers</summary>

1. 6 + 41 + 1 = **48 bytes**.
2. Packet 102 was lost — damaged in the noise and thrown away by the CRC check,
   or never received. The gap is the only evidence, which is why the counter
   exists.
3. So a gap points at *which* part of the spacecraft lost a packet. With one
   shared counter, a gap tells you something was lost but not what — and losing
   an attitude report matters differently from losing a power report.
4. Because the "minus one" is the single most error-prone piece of arithmetic
   in the standard. One place means one thing to test and one thing to get
   right, instead of a dozen scattered opportunities to get it wrong.

</details>
