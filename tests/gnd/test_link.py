#!/usr/bin/env python3
"""Checks on the ground station's link layer (gnd/pyground/link.py).

The Reed-Solomon vectors come from Phil Karn's libfec; the C++ unit tests check
the flight encoder against the same ones, so flight and ground are each tied
to an outside reference rather than only to each other.

Run:  make test-gnd
"""

import pathlib
import random
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "gnd"))

from pyground.link import (CADU, PN, ASM, Fop1, TmDecoder, Clcw, bch_parity, cltu,  # noqa: E402
                           randomise, rs_decode, rs_encode, tc_frame)

fails = 0


def check(ok, what):
    global fails
    print(f"  {'.' if ok else 'x'}  {what}")
    fails += not ok


def lcg_data():
    s, out = 12345, bytearray()
    for _ in range(223):
        s = (s * 1103515245 + 12345) & 0xFFFFFFFF
        out.append((s >> 16) & 0xFF)
    return bytes(out)


def test_reed_solomon():
    print("[Reed-Solomon]")
    check(rs_encode(bytes(range(223))).hex() ==
          "4ffb92dd557ec67f27fb8982cf58f8fd028ad117fcef6b2793d0418826578651",
          "encoder matches libfec (counting pattern)")
    data = lcg_data()
    parity = rs_encode(data)
    check(parity.hex() == "a52b7b78c3a32a8315f0c1bd3b4aeb32e68728b5944bcc51fd18dad18e19ea4e",
          "encoder matches libfec (pseudo-random data)")
    cw = bytearray(data + parity)
    cw[3] ^= 0xFF; cw[100] ^= 0x11; cw[250] ^= 0x80
    out, n = rs_decode(bytes(cw))
    check(out == data and n == 3, "decoder repairs libfec's three-error case, as libfec does")

    rng, ok = random.Random(1), True
    for _ in range(200):
        d = bytes(rng.randrange(256) for _ in range(223))
        c = bytearray(d + rs_encode(d))
        k = rng.randint(0, 16)
        for p in rng.sample(range(255), k):
            c[p] ^= rng.randrange(1, 256)
        out, n = rs_decode(bytes(c))
        ok &= (out == d and n == k)
    check(ok, "200 random codewords with up to 16 symbol errors are all repaired")
    flagged = 0
    for _ in range(30):
        d = bytes(rng.randrange(256) for _ in range(223))
        c = bytearray(d + rs_encode(d))
        for p in rng.sample(range(255), 24):
            c[p] ^= rng.randrange(1, 256)
        flagged += rs_decode(bytes(c))[1] == -1
    check(flagged == 30, "codewords beyond 16 errors are reported uncorrectable, not 'fixed' wrongly")


def test_randomiser_and_bch():
    print("[randomiser and BCH]")
    check(PN[:8].hex() == "ff480ec09a0d70bc", "pseudo-random sequence starts FF 48 0E C0 9A 0D 70 BC")
    check(randomise(randomise(b"hello")) == b"hello", "randomising twice is the identity")
    c = cltu(b"\x01\x02\x03\x04\x05\x06\x07")
    check(c[:2] == b"\xEB\x90" and c[-8:] == bytes.fromhex("C5C5C5C5C5C5C579") and len(c) == 18,
          "a CLTU is start sequence, codeblocks, tail sequence")
    check(bch_parity(bytes(7)) == 0xFE, "the parity of all-zero information is all ones (complemented), filler zero")


def test_tm_decoder():
    print("[TM frames]")
    # Build two frames by hand: a 300-byte packet spanning them, then a small one.
    def frame(vc_count, fhp, data):
        h = bytes([(0x1A5 >> 4) & 0x3F, ((0x1A5 & 0xF) << 4) | 0x01, vc_count, vc_count,
                   0x18 | (fhp >> 8), fhp & 0xFF])
        f = h + data + (1 << 24 | 5).to_bytes(4, "big")
        return ASM + randomise(f + rs_encode(f))

    big = bytes([0x08, 0x01, 0xC0, 0x00]) + (300 - 7).to_bytes(2, "big") + bytes(294)
    small = bytes([0x08, 0x02, 0xC0, 0x01, 0x00, 0x03]) + b"abcd"
    idle = bytes([0x07, 0xFF, 0xC0, 0x00]) + (213 * 2 - 310 - 7).to_bytes(2, "big")
    stream = big + small + idle + b"\x55" * (213 * 2 - 310 - 6)
    f1 = frame(0, 0, stream[:213])
    f2 = frame(1, 300 - 213, stream[213:426])
    d = TmDecoder()
    noisy = bytearray(f2)
    for p in (10, 50, 99, 200):
        noisy[p] ^= 0x5A
    got = d.push(b"\x00junk\x1A\xCF" + f1) + d.push(bytes(noisy))
    check([p for _, p in got] == [big, small], "a packet spanning two frames is reassembled, after junk, through bit errors")
    check(d.stats.rs_corrected == 4, "and the decoder counted the four repaired symbols")
    check(d.last_clcw is not None and d.last_clcw.report == 5, "the CLCW is read from the OCF")

    d3 = TmDecoder()
    d3.push(f1)
    damaged = bytearray(f2)
    damaged[0] ^= 0x81                  # two bit errors in the sync marker itself
    got = d3.push(bytes(damaged))
    check([p for _, p in got] == [big, small] and d3.stats.sync_losses == 0,
          "while locked, a sync marker with two bit errors is still recognised")

    d2 = TmDecoder()
    got = d2.push(f2)
    check([p for _, p in got] == [small], "joining mid-stream, the first header pointer finds the next whole packet")


def test_fop():
    print("[FOP-1]")
    sent = []
    fop = Fop1(sent.append, window=3)
    for i in range(5):
        fop.send(bytes([i]))
    check(len(sent) == 3, "no more than the window is ever unacknowledged")
    fop.on_clcw(Clcw(False, False, False, 0, 2))     # frames 0 and 1 acknowledged
    check(len(sent) == 5 and fop.outstanding == 3, "acknowledgement slides the window and releases queued frames")
    fop.on_clcw(Clcw(False, False, True, 0, 2))      # frame 2 lost
    check(fop.retransmissions == 3, "the retransmit flag resends everything unacknowledged, in order")


if __name__ == "__main__":
    test_reed_solomon(); test_randomiser_and_bch(); test_tm_decoder(); test_fop()
    print(f"\n{'FAILED: ' + str(fails) if fails else 'all ground link checks passed'}")
    sys.exit(1 if fails else 0)
