# Lesson 4 — Checksums

🚀 **Explorer** · about 25 minutes

---

## ❓ The question

A radio signal travels 550 kilometres, arriving at the ground station
astonishingly faint — a fraction of a billionth of a watt. Along the way it
picks up noise from the Sun, from the atmosphere, from the receiver's own
electronics.

Sometimes a `0` arrives as a `1`.

You cannot stop that. So how do you know it happened?

## 💡 The idea

You cannot prevent damage, so instead you **detect** it.

Before sending, compute a small number from the message. Send it alongside.
The receiver recomputes it from what actually arrived and compares:

```
   SPACECRAFT                              GROUND
   ──────────                              ──────
   message:  BATTERY VOLTAGE 7.4V
   compute:  0x5237
   send:     [message][0x5237]  ───────▶   received: BATUERY VOLTAGE 7.4V
                                           compute:  0x42D5
                                           expected: 0x5237
                                                     ✗ MISMATCH
                                           → throw it away
```

Notice what the receiver does *not* do: it does not try to guess what the
message was supposed to say. It **refuses to trust it**. That is the whole job.

The number is called a **checksum**, and the particular kind used in
spaceflight is a **CRC** — Cyclic Redundancy Check.

## 👀 See it

```bash
python3 learn/toolbox/crc_playground.py
```

```
  Original message: BATTERY VOLTAGE 7.4V
  Its checksum:     0x5237

  Now a cosmic ray flips ONE bit -- byte 3, bit 0:

      before:  BATTERY VOLTAGE 7.4V
      after:   BATUERY VOLTAGE 7.4V

      checksum was 0x5237, is now 0x42D5
      -> DAMAGE DETECTED
```

One bit changed. `T` became `U`. And the checksum caught it.

The program then flips a random bit twenty thousand times and counts:

```
      caught:  20,000
      missed:  0
      rate:    100.00%
```

## 💡 How good is it, really?

A 16-bit CRC catches:

- **every** single-bit error
- **every** double-bit error
- **every** burst of damage up to 16 bits long
- about **99.998%** of everything else

For two bytes of overhead, that is extraordinary value. Not perfect — but the
failures are rare enough that other layers of the system can carry the risk.

## 💡 The magic number

There is one constant worth remembering. Every implementation of this CRC in
the world, in every language, computes the same value for the same nine
characters:

```
   CRC-16 of "123456789"  =  0x29B1
```

That single number is how you know your code will talk to a ground station
somebody else wrote, in another country, twenty years ago. It appears three
times in this repository, in three independent implementations:

| Where | Language |
|---|---|
| [`tests/unit/test_crc.cpp`](../../tests/unit/test_crc.cpp) | C++, flight software |
| [`gnd/pyground/packets.py`](../../gnd/pyground/packets.py) | Python, ground station |
| [`../toolbox/crc_playground.py`](../toolbox/crc_playground.py) | Python, this lesson |

When three independently written implementations produce `0x29B1`, that is not
evidence they agree with each other. It is evidence they all agree with **the
standard**.

## 🧪 Try it — the elegant trick

Run the playground again and look at Step 4:

```
      result: 0x0000
```

Here is what happened. Append the checksum to the message, then run the
checksum over the **whole thing, including the checksum**. The answer is always
zero, if nothing was damaged.

That is a mathematical property of how CRCs are built, and it is genuinely
useful: a receiver does not need to carefully split the message from its
checksum. It runs the CRC over everything that arrived and asks "is it zero?"

In [`fsw/core/crc.hpp`](../../fsw/core/crc.hpp):

```cpp
constexpr bool crc16_check(const uint8_t* packet, size_t length) {
    return length >= 2 && crc16(packet, length) == 0;
}
```

Two lines, and no chance of slicing the buffer wrongly.

## 🔍 In the code — the order that matters

This is the most important idea in the lesson. Open
[`fsw/apps/ttc/pus.cpp`](../../fsw/apps/ttc/pus.cpp) and find `parse_tc`. The
checks happen in a fixed order:

```
   1. LENGTH     is there even enough here to be a command?
   2. INTEGRITY  does the CRC pass — before reading a single field?
   3. STRUCTURE  does the declared length match? is it really a command?
   4. MEANING    is this something we know how to do?
```

**A packet that fails its CRC is never interpreted at all.** Not its command
number, not its arguments, nothing. Why so strict? Because if the message is
damaged, *every* field in it is suspect — including the field that says what
the command is. A damaged "set voltage to 3" could arrive looking exactly like
"fire the thrusters".

There is one more consequence, and it is subtle. When a packet fails its CRC,
the spacecraft sends **no reply at all**:

```cpp
// A packet that failed its CRC cannot be answered with a verification
// report: its APID and sequence count are exactly the fields we would
// have to quote back, and they are not trustworthy.
```

Replying would mean sending a report about a command that may never have been
sent, addressed to somebody who may not exist. Instead it raises an event —
"something arrived and it was broken" — which is honest.

## 🧪 Try it — watch it happen for real

Start a spacecraft in terminal 1 (`make run`), then in terminal 2:

```bash
cd gnd
python3 -c "
import sys; sys.path.insert(0, '.')
from pyground import GroundClient
from pyground.packets import build_tc

packet = bytearray(build_tc('TEST_CONNECTION'))
packet[8] ^= 0x01          # flip one bit, like a cosmic ray

with GroundClient() as g:
    g.send_raw(bytes(packet))
    for tm in g.poll(timeout=2.0):
        print(tm.summary())
"
```

```
EVENT_LOW          TC_REJECTED aux=BAD_CRC
```

No `TEST_REPORT`. No acceptance. The spacecraft detected the damage, threw the
message away, and told you why.

## 🎓 Go deeper

**What a checksum cannot do.** It detects *accidents*, not *attackers*. Anyone
who deliberately changes a message can simply recompute the checksum to match —
there is no secret involved. Protecting against tampering needs cryptographic
authentication, a different tool for a different problem. Knowing what a safety
mechanism does *not* protect you from is as important as knowing what it does.

**Collisions exist.** There are only 65,536 possible CRC-16 values, so
different messages must sometimes share one. That is fine here because the
threat model is random noise, not an adversary searching for a collision.

**Detecting versus correcting.** A CRC only detects. Real radio links also use
**forward error correction** — Reed–Solomon coding — which adds enough
redundancy to *repair* damage without asking for a retransmission. That matters
when a round trip takes 45 minutes, or when the spacecraft is at Mars and a
round trip takes 40 minutes. It is Phase 4 of this project; see
[`../../docs/ROADMAP.md`](../../docs/ROADMAP.md).

## ✅ Check yourself

1. Why does the spacecraft throw away a damaged message rather than trying to
   repair it?
2. Why is the CRC checked *before* the command number is read?
3. Why does a CRC failure produce no verification report, when every other kind
   of rejection does?
4. Would a checksum stop someone deliberately sending false commands?

---

**Next:** [Lesson 5 — CCSDS packets](../05-ccsds-packets/) — the envelope that
every spacecraft in the world uses.

<details>
<summary>✅ Answers</summary>

1. Because it does not know what the message was *supposed* to be. A CRC says
   "this is wrong", not "here is the fix". Guessing would risk acting on an
   invented command.
2. Because a damaged message's command number is damaged too. Reading it first
   means potentially executing a command that was never sent.
3. Because the report has to quote back which command it refers to — the APID
   and sequence count of the original. On a corrupted packet those fields
   cannot be trusted, so the report would be addressed to nobody, about
   nothing. An event is raised instead.
4. No. There is no secret in a CRC, so an attacker recomputes it after
   changing the message. That needs cryptographic authentication.

</details>
