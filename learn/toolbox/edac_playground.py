#!/usr/bin/env python3
"""
Memory that repairs itself: how a spacecraft survives cosmic rays.

Run me:  python3 learn/toolbox/edac_playground.py

Used by lesson 18-when-things-break.
"""

import random
import struct

BOX = "─" * 66


def title(text: str) -> None:
    print(f"\n{BOX}\n  {text}\n{BOX}")


# ---------------------------------------------------------------------------
# Hamming(7,4): four data bits, three check bits. Small enough to do by hand.
# Positions are numbered 1..7. Positions 1, 2 and 4 (the powers of two) hold
# check bits; 3, 5, 6, 7 hold data.
# ---------------------------------------------------------------------------

DATA_POSITIONS = (3, 5, 6, 7)


def hamming7_encode(data: list[int]) -> list[int]:
    word = [0] * 8                         # index 0 unused, so index = position
    for pos, bit in zip(DATA_POSITIONS, data):
        word[pos] = bit
    # Check bit p makes the XOR of the POSITIONS of all set bits zero.
    x = 0
    for pos in DATA_POSITIONS:
        if word[pos]:
            x ^= pos
    for p in (1, 2, 4):
        word[p] = 1 if x & p else 0
    return word


def syndrome(word: list[int]) -> int:
    x = 0
    for pos in range(1, len(word)):
        if word[pos]:
            x ^= pos
    return x


def show(word: list[int], mark: int = 0) -> str:
    cells = []
    for pos in range(1, len(word)):
        b = str(word[pos])
        cells.append(f"[{b}]" if pos == mark else f" {b} ")
    return "".join(cells)


# ---------------------------------------------------------------------------
# The flight software's code: extended Hamming (72, 64). The same rule, with
# 64 data bits, 7 check bits and one overall parity bit. A mirror of
# fsw/core/edac.hpp, so the two can be compared line by line.
# ---------------------------------------------------------------------------

POS_OF = [p for p in range(3, 72) if p & (p - 1)][:64]
DATA_AT = {p: i for i, p in enumerate(POS_OF)}


def edac_encode(data: int) -> int:
    x = 0
    for i in range(64):
        if data >> i & 1:
            x ^= POS_OF[i]
    parity = (bin(data).count("1") + bin(x).count("1")) & 1
    return x | parity << 7


def edac_decode(data: int, check: int) -> tuple[str, int]:
    x = 0
    for i in range(64):
        if data >> i & 1:
            x ^= POS_OF[i]
    s = x ^ (check & 0x7F)
    parity_wrong = (bin(data).count("1") + bin(check).count("1")) & 1
    if s == 0 and not parity_wrong:
        return "clean", data
    if not parity_wrong:
        return "UNCORRECTABLE", data
    if s in DATA_AT:
        return "corrected", data ^ (1 << DATA_AT[s])
    return "corrected", data                     # a check bit was hit; the data is fine


def main() -> None:
    print("""
              MEMORY THAT REPAIRS ITSELF

  In orbit, a charged particle passing through a memory chip can flip a
  bit. Nothing is broken -- write the cell again and it is fine -- but
  until then it holds the wrong value. Your variable changes, and nobody
  wrote to it. Over the South Atlantic Anomaly it happens often.

  Lesson 4 used extra bits to DETECT damage on the radio. Here is how a
  few extra bits can FIND the broken bit -- and so put it back.""")

    title("Step 1: the trick, on seven bits")

    data = [1, 0, 1, 1]
    word = hamming7_encode(data)
    print(f"""
  Four data bits, {data}, stored in seven positions. The check bits go in
  the power-of-two positions -- 1, 2 and 4 -- and are chosen so that one
  rule holds:

      XOR together the POSITION NUMBERS of every bit that is 1,
      and you get zero.

      position   1  2  3  4  5  6  7
      stored    {show(word)}
      check:    syndrome = {syndrome(word)}
""")

    print("  Now let a particle flip each bit in turn, and apply the same rule:\n")
    print("      flipped    stored                 syndrome   points at")
    for pos in range(1, 8):
        hit = list(word)
        hit[pos] ^= 1
        s = syndrome(hit)
        print(f"      bit {pos}      {show(hit, pos)}      {s}          bit {s}")
    print("""
  The syndrome is not just "something is wrong". It is the ADDRESS of the
  broken bit. Flip it back and the memory is repaired. That is the whole
  idea, and Richard Hamming had it in 1950, tired of a relay computer that
  stopped every weekend over single errors.""")

    title("Step 2: the size the flight software uses")

    value = struct.unpack(">Q", struct.pack(">d", 123456.0))[0]   # a control gain, say
    check = edac_encode(value)
    print(f"""
  fsw/core/edac.hpp does the same thing to 64-bit words, with 7 check
  bits plus one more for overall parity: 72 bits stored for every 64 --
  12.5% extra. Every value in the parameter table lives in one.

      stored value  0x{value:016X}   (the number 123456.0)
      check byte    0x{check:02X}
""")
    rng = random.Random(18)
    for _ in range(3):
        bit = rng.randrange(64)
        status, fixed = edac_decode(value ^ (1 << bit), check)
        print(f"      flip data bit {bit:2d}  ->  {status:<10}  read back 0x{fixed:016X}"
              f"  {'(right)' if fixed == value else '(WRONG)'}")

    title("Step 3: two flips in one word")

    a, b = 3, 40
    status, _ = edac_decode(value ^ (1 << a) ^ (1 << b), check)
    print(f"""
  Flip bits {a} and {b} of the same word:  {status}

  Two flips cancel each other's parity, so the extra parity bit says
  "even" while the syndrome says "wrong": the code knows it cannot point
  at ONE bit, and says so instead of guessing. That is SEC-DED -- single
  error correct, double error detect.

  On board, an uncorrectable parameter reads back as its DEFAULT, never as
  the damaged value, and FDIR reloads the table from storage. Run
  `make radiation` to watch it happen on purpose.""")

    title("Step 4: why the memory is scrubbed")

    words, days, rate = 4096, 7, 2000.0       # upsets per day, a busy radiation day x 1000
    print(f"""
  Correction happens when a word is READ, but the stored word stays wrong.
  A second particle in the same word before anyone rewrites it makes two
  flips, which cannot be corrected. So a SCRUBBER walks the memory,
  reading and rewriting every word.

  {words} words, {rate:.0f} upsets a day (a thousand times a real rate), for
  {days} days. How often does a second hit find a word still damaged by
  the first -- an error that can no longer be corrected?
""")
    print("      scrubbed every     uncorrectable errors")
    for period_s in (None, 86400.0, 3600.0, 60.0, 1.6):
        sim = random.Random(7)
        dirty: dict[int, float] = {}                 # word -> when it was first hit
        lost = 0
        t = 0.0
        while True:
            t += sim.expovariate(rate / 86400.0)
            if t > days * 86400.0:
                break
            w = sim.randrange(words)
            first = dirty.get(w)
            if first is not None and (period_s is None or (first // period_s) == (t // period_s)):
                lost += 1                            # second hit before a scrub repaired the first
                dirty.pop(w)
            else:
                dirty[w] = t
        label = "never" if period_s is None else (f"{period_s / 3600:.0f} h" if period_s >= 3600
                                                  else f"{period_s:.1f} s" if period_s < 60 else "1 min")
        print(f"      {label:<16}   {lost}")
    print("""
  Scrub never and single errors pile up until they pair off. Scrub often
  and a second hit almost never finds the first still there. The flight
  software scrubs its whole parameter table every 1.6 seconds.""")

    title("Try this")
    print("""
  1. In step 1, flip TWO bits of the seven-bit word. What does the
     syndrome point at now? Why is that worse than no correction at all?
     (This is why the flight code adds the extra parity bit.)

  2. In step 4, double the upset rate. Does the "never" row double too?
     Look again: what does it grow with?

  3. Read fsw/core/edac.hpp next to this file. Find the line that turns
     the syndrome into "which data bit". Both programs have it.
""")


if __name__ == "__main__":
    main()
