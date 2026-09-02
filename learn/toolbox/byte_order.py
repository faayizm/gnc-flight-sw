#!/usr/bin/env python3
"""
Byte order, and why spacecraft care about it.

Run me:  python3 learn/toolbox/byte_order.py

Used by lesson 03-bytes-and-numbers.
"""

import struct

BOX = "─" * 62


def title(text: str) -> None:
    print(f"\n{BOX}\n  {text}\n{BOX}")


def show_bytes(label: str, raw: bytes) -> None:
    hexes = " ".join(f"{b:02X}" for b in raw)
    print(f"  {label:<26} {hexes}")


def main() -> None:
    print("""
        HOW DO YOU WRITE A NUMBER DOWN FOR ANOTHER COMPUTER?

  A number like 305,419,896 is easy for you to read. But a computer
  has to store it as a row of bytes, and there are two ways to do
  that -- and they disagree about which end goes first.
""")

    number = 305419896          # 0x12345678, chosen so every byte is different
    title(f"The number {number:,}  (in hex: 0x{number:08X})")

    big = struct.pack(">I", number)
    little = struct.pack("<I", number)

    show_bytes("BIG endian    (network)", big)
    show_bytes("LITTLE endian (your PC)", little)

    print("""
  BIG endian writes the most important part first, the way you say a
  number out loud: "twelve, thirty-four, fifty-six, seventy-eight".

  LITTLE endian writes it backwards. Most laptops and phones do this
  internally, for reasons to do with how old processors did addition.
""")

    title("So what happens if the two ends disagree?")

    print("\n  A spacecraft writes 305,419,896 in BIG endian and sends it.")
    print("  A ground computer reads those same bytes as LITTLE endian:\n")

    (misread,) = struct.unpack("<I", big)
    print(f"      sent:  {number:>12,}")
    print(f"      read:  {misread:>12,}   <-- not even close")
    print(f"\n      difference: {misread - number:>12,}")
    print("      Same four bytes. Completely different number.")

    print("""
  If that number was a battery voltage, the ground would think the
  spacecraft had a bus voltage of two billion volts. If it was a
  commanded rotation rate, the spacecraft would try to spin itself
  apart.

  This is not a hypothetical. Getting byte order wrong is one of the
  most common bugs when two systems talk for the first time.
""")

    title("The rule everyone in spaceflight agreed on")

    print("""
  CCSDS -- the standards body for space communications -- says:
  everything on the wire is BIG endian. Every spacecraft, every
  ground station, every country. No exceptions, no negotiation.

  That is why in this repository there is exactly ONE file that
  knows about byte order:

      fsw/core/bytes.hpp

  Everything else calls write_uint32() and never thinks about it
  again. One place to get right, one place to test.
""")

    title("Negative numbers, and why they look strange")

    for value in (-1, -1000, 1000):
        raw = struct.pack(">i", value)
        show_bytes(f"{value:>6} as int32", raw)

    print("""
  -1 is FF FF FF FF: all ones. Computers store negative numbers using
  "two's complement", where counting down past zero wraps around to
  the top. It looks odd but it means the same addition circuit works
  for positive and negative numbers.
""")

    title("Decimals are stranger still")

    for value in (1.0, 0.5, 3.14159, -2.0):
        raw = struct.pack(">f", value)
        show_bytes(f"{value:>9} as float32", raw)

    print("""
  Floating point splits a number into a sign, an exponent and a
  fraction -- like scientific notation, in binary. This is why 0.1
  cannot be stored exactly, and why comparing two decimals with ==
  is a bad habit that will eventually bite you.

  Try it yourself:  python3 -c "print(0.1 + 0.2 == 0.3)"
""")

    title("Try this")

    print("""
  1. Change `number` at the top of this file and run it again.
     Find a number that reads the SAME in both byte orders.
     (Hint: what is special about 0x12121212?)

  2. In lesson 05 you will decode a real spacecraft packet by hand.
     Every field in it is big endian. Now you know why.
""")


if __name__ == "__main__":
    main()
