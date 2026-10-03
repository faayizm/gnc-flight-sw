"""
The ground end of the space link: CCSDS channel coding, transfer frames, and
COP-1's sending half (FOP-1).

Like packets.py, this is an independent implementation of the standards the
flight software implements in C++, written from the standards rather than
translated from that code. The Reed-Solomon decoder in particular has no
counterpart on board (the spacecraft only encodes); it is checked against
libfec's decoder in the tests.

    downlink:  bytes -> ASM search -> de-randomise -> RS decode -> TM frame
               -> per-virtual-channel packet reassembly -> packets, CLCW
    uplink:    packet -> TC frame (FOP-1 sequence number) -> BCH codeblocks
               -> CLTU -> bytes
"""

from __future__ import annotations

import time
from dataclasses import dataclass, field

from .dictionary import LINK
from .packets import crc16

SCID = LINK["scid"]
TM_FRAME = LINK["tm_frame_bytes"]
TM_DATA = TM_FRAME - 6 - 4
ASM = bytes.fromhex("1ACFFC1D")
CADU = 4 + 255
FHP_NONE, FHP_IDLE = 0x7FF, 0x7FE

# ---------------------------------------------------------------------------
# Reed-Solomon (255,223), CCSDS: GF(2^8) mod 0x187, roots beta^(112..143)
# with beta = alpha^11, symbols in Berlekamp's dual basis.
# ---------------------------------------------------------------------------

_EXP = [0] * 512
_LOG = [0] * 256
_x = 1
for _i in range(255):
    _EXP[_i] = _x
    _LOG[_x] = _i
    _x <<= 1
    if _x & 0x100:
        _x ^= 0x187
for _i in range(255, 512):
    _EXP[_i] = _EXP[_i - 255]

_TAL_ROWS = (0x8D, 0xEF, 0xEC, 0x86, 0xFA, 0x99, 0xAF, 0x7B)
TO_DUAL = [0] * 256
TO_CONV = [0] * 256
for _v in range(256):
    _d = 0
    for _k in range(8):
        if _v & (1 << _k):
            _d ^= _TAL_ROWS[7 - _k]
    TO_DUAL[_v] = _d
    TO_CONV[_d] = _v

FCR, PRIM, NROOTS = 112, 11, 32


def _mul(a: int, b: int) -> int:
    return 0 if a == 0 or b == 0 else _EXP[_LOG[a] + _LOG[b]]


def _div(a: int, b: int) -> int:
    return 0 if a == 0 else _EXP[(_LOG[a] - _LOG[b]) % 255]


def _beta_pow(n: int) -> int:
    return _EXP[(PRIM * n) % 255]


def _genpoly() -> list[int]:
    g = [1]
    for j in range(NROOTS):
        root = _beta_pow(FCR + j)
        # multiply g by (x + root); g[0] is the highest power
        g = [a ^ _mul(b, root) for a, b in zip(g + [0], [0] + g)]
    return g


_GEN = _genpoly()

# The encoder runs as a CRC-style shift register over a 256-bit integer: for
# each feedback symbol f, _FEEDBACK[f] is f times the generator's 32 lower
# coefficients, packed big-endian. One table lookup and two integer operations
# per input byte, instead of 32 field multiplications -- which is what lets a
# pure-Python ground station keep up with a 100-frame/s downlink.
_FEEDBACK = [int.from_bytes(bytes(_mul(f, g) for g in _GEN[1:]), "big") for f in range(256)]
_MASK = (1 << (8 * NROOTS)) - 1
_TO_CONV_TABLE = bytes(TO_CONV)
_TO_DUAL_TABLE = bytes(TO_DUAL)


def rs_encode(data: bytes) -> bytes:
    """32 parity bytes for 223 data bytes, all in the dual basis."""
    state = 0
    for c in bytes(data).translate(_TO_CONV_TABLE):
        fb = (state >> (8 * NROOTS - 8)) ^ c
        state = ((state << 8) & _MASK) ^ _FEEDBACK[fb]
    return state.to_bytes(NROOTS, "big").translate(_TO_DUAL_TABLE)


def rs_decode(codeword: bytes) -> tuple[bytes, int]:
    """Correct a 255-byte codeword in place. Returns (data, symbols corrected),
    with -1 for an uncorrectable codeword."""
    # Almost every frame is clean. Re-encoding is ~30x cheaper than computing
    # syndromes in Python, and a codeword whose parity re-encodes identically
    # has all-zero syndromes by definition.
    if rs_encode(codeword[:-NROOTS]) == bytes(codeword[-NROOTS:]):
        return bytes(codeword[:-NROOTS]), 0
    r = [TO_CONV[b] for b in codeword]
    n = len(r)
    # syndromes S_i = r(beta^(FCR+i)); r[0] is the coefficient of x^(n-1)
    synd = []
    for i in range(NROOTS):
        root = _beta_pow(FCR + i)
        s = 0
        for c in r:
            s = _mul(s, root) ^ c
        synd.append(s)
    if not any(synd):
        return bytes(codeword[:n - NROOTS]), 0

    # Berlekamp-Massey: error locator Lambda(x), lowest power first
    lam, prev = [1], [1]
    L, m, b = 0, 1, 1
    for k in range(NROOTS):
        d = synd[k]
        for i in range(1, L + 1):
            if i < len(lam):
                d ^= _mul(lam[i], synd[k - i])
        if d == 0:
            m += 1
            continue
        coef = _div(d, b)
        t = lam[:]
        shifted = [0] * m + [_mul(coef, c) for c in prev]
        lam = [x ^ y for x, y in zip(lam + [0] * (len(shifted) - len(lam)),
                                     shifted + [0] * (len(lam) - len(shifted)))]
        if 2 * L <= k:
            L, prev, b, m = k + 1 - L, t, d, 1
        else:
            m += 1
    while len(lam) > 1 and lam[-1] == 0:
        lam.pop()
    if L > NROOTS // 2 or len(lam) - 1 != L:
        return bytes(codeword[:n - NROOTS]), -1

    # Chien search. Position p (x^p) has locator X = beta^p.
    def poly_eval(poly: list[int], x: int) -> int:
        y = 0
        for c in reversed(poly):
            y = _mul(y, x) ^ c
        return y

    positions = [p for p in range(n) if poly_eval(lam, _beta_pow((255 - p) % 255)) == 0]
    if len(positions) != L:
        return bytes(codeword[:n - NROOTS]), -1

    # Forney, for a code whose first root is beta^FCR:
    #     error = X^(1-FCR) * Omega(X^-1) / Lambda'(X^-1)
    omega = [0] * NROOTS
    for i in range(NROOTS):
        for j in range(min(i + 1, len(lam))):
            omega[i] ^= _mul(lam[j], synd[i - j])
    dlam = [lam[i] if i % 2 == 1 else 0 for i in range(1, len(lam))]
    for p in positions:
        xinv = _beta_pow((255 - p) % 255)
        y = _div(poly_eval(omega, xinv), poly_eval(dlam, xinv))
        e = _div(_mul(y, _beta_pow(p)), _beta_pow((FCR * p) % 255))
        r[n - 1 - p] ^= e
    return bytes(TO_DUAL[c] for c in r[:n - NROOTS]), L


# ---------------------------------------------------------------------------
# Pseudo-randomiser and BCH
# ---------------------------------------------------------------------------


def _pn_sequence() -> bytes:
    reg = [1] * 8
    out = bytearray()
    for _ in range(255):
        byte = 0
        for _ in range(8):
            byte = (byte << 1) | reg[0]
            fb = reg[0] ^ reg[3] ^ reg[5] ^ reg[7]
            reg = reg[1:] + [fb]
        out.append(byte)
    return bytes(out)


PN = _pn_sequence()
_PN_INT = int.from_bytes(PN, "big")


def randomise(data: bytes) -> bytes:
    if len(data) == 255:
        return (int.from_bytes(data, "big") ^ _PN_INT).to_bytes(255, "big")
    return bytes(b ^ PN[i % 255] for i, b in enumerate(data))


def bch_parity(info: bytes) -> int:
    """BCH(63,56) parity byte: 7 complemented parity bits and a zero filler."""
    rem = 0
    for byte in info:
        for bit in range(7, -1, -1):
            top = ((rem >> 6) & 1) ^ ((byte >> bit) & 1)
            rem = (rem << 1) & 0x7F
            if top:
                rem ^= 0x45          # g(x) = x^7 + x^6 + x^2 + 1, without x^7
    return ((~rem) & 0x7F) << 1


CLTU_START = bytes.fromhex("EB90")
CLTU_TAIL = bytes.fromhex("C5C5C5C5C5C5C579")


def cltu(frame: bytes) -> bytes:
    out = bytearray(CLTU_START)
    for i in range(0, len(frame), 7):
        info = frame[i:i + 7].ljust(7, b"\x55")
        out += info + bytes([bch_parity(info)])
    return bytes(out + CLTU_TAIL)


def tc_frame(data: bytes, seq: int, bypass: bool = False, control: bool = False) -> bytes:
    length = 5 + len(data) + 2
    hdr = bytes([
        (0x20 if bypass else 0) | (0x10 if control else 0) | ((SCID >> 8) & 0x03),
        SCID & 0xFF,
        (LINK["tc_vc"] << 2) | (((length - 1) >> 8) & 0x03),
        (length - 1) & 0xFF,
        seq & 0xFF,
    ])
    body = hdr + data
    return body + crc16(body).to_bytes(2, "big")


# ---------------------------------------------------------------------------
# Downlink: frame sync, decoding and packet reassembly
# ---------------------------------------------------------------------------


@dataclass
class Clcw:
    lockout: bool
    wait: bool
    retransmit: bool
    farm_b: int
    report: int

    @staticmethod
    def parse(word: int) -> Clcw:
        return Clcw(bool(word >> 13 & 1), bool(word >> 12 & 1), bool(word >> 11 & 1),
                    word >> 9 & 3, word & 0xFF)


@dataclass
class LinkStats:
    cadus: int = 0
    rs_corrected: int = 0           # symbols
    rs_failed: int = 0              # frames
    sync_losses: int = 0
    frames_lost: dict = field(default_factory=dict)   # vcid -> count
    idle_frames: int = 0


class TmDecoder:
    """Turns a received byte stream into (vcid, packet) pairs and CLCWs."""

    def __init__(self):
        self.buf = bytearray()
        self.locked = False
        self.stats = LinkStats()
        self.vc_expect: dict[int, int] = {}
        self.vc_partial: dict[int, bytearray | None] = {}
        self.last_clcw: Clcw | None = None

    def push(self, data: bytes) -> list[tuple[int, bytes]]:
        self.buf += data
        out: list[tuple[int, bytes]] = []
        while True:
            if not self.locked:
                i = self.buf.find(ASM)
                if i < 0:
                    del self.buf[:max(0, len(self.buf) - 3)]
                    return out
                del self.buf[:i]
                self.locked = True
            if len(self.buf) < CADU:
                return out
            if self.buf[:4] != ASM:
                # Lost lock: the next frame did not start where it should.
                self.locked = False
                self.stats.sync_losses += 1
                del self.buf[0]
                continue
            cw = randomise(bytes(self.buf[4:CADU]))
            del self.buf[:CADU]
            self.stats.cadus += 1
            frame, fixed = rs_decode(cw)
            if fixed < 0:
                self.stats.rs_failed += 1
                continue
            self.stats.rs_corrected += fixed
            out += self._frame(frame)

    def _frame(self, f: bytes) -> list[tuple[int, bytes]]:
        scid = ((f[0] & 0x3F) << 4) | (f[1] >> 4)
        vcid = (f[1] >> 1) & 7
        if scid != SCID:
            return []
        self.last_clcw = Clcw.parse(int.from_bytes(f[-4:], "big"))
        if vcid == LINK["vc_idle"]:
            self.stats.idle_frames += 1
            return []
        vc_count = f[3]
        fhp = ((f[4] & 7) << 8) | f[5]
        data = f[6:6 + TM_DATA]

        expect = self.vc_expect.get(vcid)
        partial = self.vc_partial.get(vcid)
        if expect is not None and vc_count != expect:
            # A frame was lost: whatever packet was being assembled is now
            # incomplete. Throw it away and resume at this frame's pointer.
            self.stats.frames_lost[vcid] = self.stats.frames_lost.get(vcid, 0) + ((vc_count - expect) & 0xFF)
            partial = None
        self.vc_expect[vcid] = (vc_count + 1) & 0xFF

        packets: list[tuple[int, bytes]] = []
        if partial is None:
            if fhp in (FHP_NONE, FHP_IDLE):
                self.vc_partial[vcid] = None
                return []
            partial = bytearray()
            pos = fhp
        else:
            pos = 0
        while pos < len(data):
            if len(partial) < 6:
                need = 6 - len(partial)
                partial += data[pos:pos + need]
                pos += need
                if len(partial) < 6:
                    break
            total = 6 + int.from_bytes(partial[4:6], "big") + 1
            take = total - len(partial)
            partial += data[pos:pos + take]
            pos += take
            if len(partial) == total:
                apid = int.from_bytes(partial[0:2], "big") & 0x7FF
                if apid != 0x7FF:
                    packets.append((vcid, bytes(partial)))
                partial = bytearray()
        self.vc_partial[vcid] = partial
        return packets


# ---------------------------------------------------------------------------
# FOP-1: the ground half of COP-1
# ---------------------------------------------------------------------------


class Fop1:
    """
    Sends sequence-controlled (type AD) frames and guarantees they arrive,
    once each and in order, by reading the CLCW the spacecraft returns.

      * At most `window` frames are unacknowledged at once. That must not
        exceed half the FARM's window, or a single lost frame could push later
        ones outside it and lock the spacecraft out.
      * The CLCW's report value N(R) acknowledges every frame before it.
      * The retransmit flag, or silence for `timeout` seconds, means a frame
        went missing: everything unacknowledged is sent again, in order.
      * Lockout means the sequence is beyond repair. FOP-1 sends Unlock and
        Set V(R), then retransmits what was outstanding. A real operator would
        be asked first; a scripted pass cannot wait for one.
    """

    def __init__(self, transmit, window: int = LINK["farm_window"] // 2,
                 timeout: float = 2.0, limit: int = 10):
        self.transmit = transmit          # callable(bytes) -> None, sends a CLTU
        self.window, self.timeout, self.limit = window, timeout, limit
        self.vs = 0
        self.sent: list[list] = []        # [ns, frame, last_tx_time, tx_count]
        self.waiting: list[bytes] = []    # packets not yet framed
        self.retransmissions = 0
        self.lockouts_cleared = 0
        self.alert: str | None = None
        self._last_retransmit = 0.0

    def initialise(self) -> None:
        """Unlock, then Set V(R) to our V(S): both ends now agree."""
        self.transmit(cltu(tc_frame(b"\x00", 0, bypass=True, control=True)))
        self.transmit(cltu(tc_frame(bytes([0x82, 0x00, self.vs]), 0, bypass=True, control=True)))

    def send(self, packet: bytes) -> None:
        self.waiting.append(packet)
        self._fill()

    def _fill(self) -> None:
        while self.waiting and len(self.sent) < self.window:
            frame = tc_frame(self.waiting.pop(0), self.vs)
            self.sent.append([self.vs, frame, time.monotonic(), 1])
            self.vs = (self.vs + 1) & 0xFF
            self.transmit(cltu(frame))

    def _retransmit_all(self) -> None:
        now = time.monotonic()
        for entry in self.sent:
            if entry[3] >= self.limit:
                self.alert = f"frame {entry[0]} not acknowledged after {entry[3]} transmissions"
                return
            entry[2], entry[3] = now, entry[3] + 1
            self.transmit(cltu(entry[1]))
            self.retransmissions += 1
        self._last_retransmit = now

    def on_clcw(self, c: Clcw) -> None:
        if c.lockout:
            self.lockouts_cleared += 1
            first = self.sent[0][0] if self.sent else self.vs
            self.transmit(cltu(tc_frame(b"\x00", 0, bypass=True, control=True)))
            self.transmit(cltu(tc_frame(bytes([0x82, 0x00, first]), 0, bypass=True, control=True)))
            self._retransmit_all()
            return
        # Acknowledge everything before N(R), modulo 256 within the window.
        while self.sent and ((c.report - self.sent[0][0]) & 0xFF) in range(1, self.window + 1):
            self.sent.pop(0)
        if c.retransmit and self.sent and time.monotonic() - self._last_retransmit > 0.2:
            self._retransmit_all()
        self._fill()

    def service(self) -> None:
        """Call regularly: handles the acknowledgement timeout."""
        if self.sent and time.monotonic() - self.sent[0][2] > self.timeout:
            self._retransmit_all()
        self._fill()

    @property
    def outstanding(self) -> int:
        return len(self.sent) + len(self.waiting)
