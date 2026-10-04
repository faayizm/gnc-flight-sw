#!/usr/bin/env python3
"""
Error correction: how a spacecraft's message survives damage, instead of
just being thrown away.

Run me:  python3 learn/toolbox/fec_playground.py

Used by lesson 04-checksums. The Reed-Solomon code here is the real one: the
same module the ground station uses to decode HYPERSAT's downlink, checked
against the reference implementation most ground stations descend from.
"""

import pathlib
import random
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "gnd"))

from pyground.link import rs_decode, rs_encode   # noqa: E402

BOX = "─" * 62


def title(text: str) -> None:
    print(f"\n{BOX}\n  {text}\n{BOX}")


def bits_of(text: str) -> list[int]:
    return [(b >> i) & 1 for b in text.encode() for i in range(7, -1, -1)]


def text_of(bits: list[int]) -> str:
    out = bytearray()
    for i in range(0, len(bits), 8):
        v = 0
        for b in bits[i:i + 8]:
            v = (v << 1) | b
        out.append(v)
    return out.decode(errors="replace")


def damage(data: bytes, how_many: int, rng: random.Random) -> bytes:
    """Wreck `how_many` different bytes completely."""
    out = bytearray(data)
    for i in rng.sample(range(len(out)), how_many):
        out[i] ^= rng.randrange(1, 256)
    return bytes(out)


def main() -> None:
    rng = random.Random(4)     # fixed seed: you get the same results I did

    print("""
          CAN A DAMAGED MESSAGE BE REPAIRED, NOT JUST REJECTED?

  A checksum (lesson 4) tells you a message is wrong. Then you throw
  it away and ask again. But a spacecraft might be out of contact for
  an hour before you can ask -- or at Mars, forty minutes away each
  way. Better to send the message so that the receiver can FIX it.
""")

    title("Idea 1: say everything three times")
    message = "WHEELS OK"
    bits = bits_of(message)
    sent = [b for b in bits for _ in range(3)]          # every bit, three times
    received = list(sent)
    for i in rng.sample(range(0, len(sent), 3), 4):     # four bits hit by noise...
        received[i + rng.randrange(3)] ^= 1             # ...one copy each
    decoded = [1 if sum(received[i:i + 3]) >= 2 else 0 for i in range(0, len(received), 3)]
    print(f"""
  Message:        {message!r}  ({len(bits)} bits)
  Sent:           every bit three times = {len(sent)} bits
  Noise flipped:  4 bits on the way
  Without fixing: {text_of(received[::3])!r}
  Majority vote:  {text_of(decoded)!r}   <- each bit is whatever 2 of its 3 copies say

  It works. But it TRIPLED the amount of radio time. Radio time is
  the scarcest thing a small satellite has.""")

    title("Idea 2: Reed-Solomon -- the code spacecraft actually use")
    data = b"HYPERSAT housekeeping: battery 7.62 V, 3 wheels nominal, " \
           b"attitude converged, star tracker locked. " 
    data = (data * 4)[:223]
    parity = rs_encode(data)
    print(f"""
  Take 223 bytes of telemetry and add {len(parity)} bytes of carefully
  computed extra information. That is 14% more, not 200% more.

  The promise: ANY 16 bytes of the 255 can be wrecked -- not just a
  bit flipped, the whole byte replaced with rubbish -- and the
  receiver rebuilds the original exactly.
""")
    codeword = data + parity
    print("  bytes wrecked    result")
    print("  -------------    ------------------------------------------")
    for n in (1, 5, 10, 16, 17, 25):
        received = damage(codeword, n, rng)
        repaired, fixed = rs_decode(received)
        if fixed < 0:
            verdict = "TOO DAMAGED -- and it knows it, so it says so"
        elif repaired == data:
            verdict = f"repaired perfectly ({fixed} byte{'s' if fixed != 1 else ''} fixed)"
        else:
            verdict = "WRONG ANSWER (a miscorrection -- see lesson 4)"
        print(f"  {n:>13}    {verdict}")

    print("""
  Up to 16: always repaired. Beyond 16 it cannot repair -- but it
  does not pretend. It reports "too damaged", and the frame is thrown
  away like any other failed checksum. A code that silently "fixed" a
  message into a different message would be worse than no code.""")

    title("Why bytes, not bits")
    one_byte = bytearray(codeword)
    one_byte[100] ^= 0xFF                 # all 8 bits of one byte
    _, fixed = rs_decode(bytes(one_byte))
    print(f"""
  Radio noise often comes in BURSTS: a crackle wipes out several bits
  in a row. Reed-Solomon counts damage in bytes, so 8 bad bits in one
  byte cost it just 1 of its 16 repairs (this one fixed: {fixed} byte).

  Saying-it-three-times does badly here: 8 bad bits IN A ROW cover
  all three copies of two or three message bits, and a majority vote
  of three wrong copies is simply wrong.""")

    title("On HYPERSAT")
    print("""
  Every frame the spacecraft sends down is one Reed-Solomon codeword:
  223 bytes of frame + 32 of repair information. Run

      make store-forward

  and the last lines say how many bytes the ground station repaired
  over two passes through a noisy radio channel. The answer is about
  a thousand -- and the number of frames beyond repair is zero.

  Try this:
    1. Change the list (1, 5, 10, 16, 17, 25) in main(). Try 15, 16,
       17, 18. Where exactly is the edge?
    2. In "Idea 1", flip TWO copies of the same bit. What does the
       majority vote do now?
""")


if __name__ == "__main__":
    main()
