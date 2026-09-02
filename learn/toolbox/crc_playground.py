#!/usr/bin/env python3
"""
Checksums: how a spacecraft knows a message arrived undamaged.

Run me:  python3 learn/toolbox/crc_playground.py

Used by lesson 04-checksums.
"""

import random

BOX = "─" * 62


def title(text: str) -> None:
    print(f"\n{BOX}\n  {text}\n{BOX}")


def crc16(data: bytes, seed: int = 0xFFFF) -> int:
    """
    The exact CRC every spacecraft using CCSDS standards computes.

    This is the same algorithm as fsw/core/crc.hpp in the flight software
    and gnd/pyground/packets.py in the ground station. Three independent
    implementations of one standard -- when they agree, that means something.
    """
    crc = seed
    for byte in data:
        crc ^= byte << 8                      # mix the byte into the top half
        for _ in range(8):                    # then shift out eight bits
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def flip_one_bit(data: bytes, rng: random.Random) -> tuple[bytes, int, int]:
    """Flip a single random bit, the way a cosmic ray might."""
    damaged = bytearray(data)
    index = rng.randrange(len(damaged))
    bit = rng.randrange(8)
    damaged[index] ^= 1 << bit
    return bytes(damaged), index, bit


def main() -> None:
    rng = random.Random(42)      # fixed seed: you get the same results I did

    print("""
            HOW DO YOU KNOW A MESSAGE WASN'T DAMAGED?

  A radio signal from a satellite 500 km up arrives incredibly faint.
  Along the way it picks up noise from the Sun, from the atmosphere,
  from the receiver's own electronics. Sometimes a 0 arrives as a 1.

  You cannot prevent that. So instead you DETECT it.
""")

    title("Step 1: the standard test")

    check = b"123456789"
    result = crc16(check)
    print(f"""
  Every implementation of this checksum in the world, in every
  language, computes the same value for the same nine characters:

      message:  {check.decode()}
      CRC-16:   0x{result:04X}

  Expected:     0x29B1   {"MATCH" if result == 0x29B1 else "MISMATCH!"}

  That single constant is how you know your code will talk to a
  ground station somebody else wrote, in another country, years ago.
""")

    title("Step 2: watch it catch damage")

    message = b"BATTERY VOLTAGE 7.4V"
    good = crc16(message)

    print(f"\n  Original message: {message.decode()}")
    print(f"  Its checksum:     0x{good:04X}\n")

    damaged, index, bit = flip_one_bit(message, rng)
    bad = crc16(damaged)

    print(f"  Now a cosmic ray flips ONE bit -- byte {index}, bit {bit}:\n")
    print(f"      before:  {message.decode()}")
    print(f"      after:   {damaged.decode(errors='replace')}")
    print(f"\n      checksum was 0x{good:04X}, is now 0x{bad:04X}")
    print(f"      -> {'DAMAGE DETECTED' if good != bad else 'missed it'}")

    print("""
  The spacecraft throws that message away and never acts on it. That
  is the whole job: not repairing the damage, just refusing to trust
  damaged data.
""")

    title("Step 3: how good is it, really?")

    trials = 20000
    caught = 0
    for _ in range(trials):
        damaged, _, _ = flip_one_bit(message, rng)
        if crc16(damaged) != good:
            caught += 1

    print(f"""
  Flipping one random bit, {trials:,} times over:

      caught:  {caught:,}
      missed:  {trials - caught:,}
      rate:    {100 * caught / trials:.2f}%

  A CRC-16 catches EVERY single-bit error, every double-bit error,
  every burst of damage up to 16 bits long, and about 99.998% of
  everything else. Not perfect -- but for two bytes of overhead,
  extraordinary value.
""")

    title("Step 4: the trick that makes checking easy")

    packet = message + bytes([good >> 8, good & 0xFF])
    print(f"""
  Here is something elegant. Append the checksum to the message, then
  run the checksum over the WHOLE thing, including the checksum:

      result: 0x{crc16(packet):04X}

  Zero. Always zero, if nothing was damaged.

  So a receiver doesn't need to split the message from its checksum.
  It runs the CRC over everything that arrived and asks "is it zero?"
  That is exactly what crc16_check() does in fsw/core/crc.hpp, and
  why parse_tc() can validate a packet before understanding it.
""")

    title("Step 5: what a checksum canNOT do")

    print("""
  A checksum detects ACCIDENTS. It does not stop an ATTACKER.

  Anyone who changes a message can simply recompute the checksum to
  match -- there is no secret involved. Protecting against deliberate
  tampering needs cryptographic authentication, which is a different
  tool for a different problem, and a real concern for real missions.

  Knowing what a safety mechanism does NOT protect you from is as
  important as knowing what it does.
""")

    title("Try this")

    print("""
  1. Change `message` in this file to your own text and run it again.
     Does the detection rate change? Should it?

  2. Flip TWO bits instead of one. Edit flip_one_bit to damage the
     message twice. Does the CRC still catch it? (It should.)

  3. Hard mode: can you find two DIFFERENT messages, the same length,
     with the same CRC? They exist -- there are only 65,536 possible
     checksums, so collisions are guaranteed. Finding one by random
     search takes a while. Why does that not worry spacecraft
     engineers?
""")


if __name__ == "__main__":
    main()
