#!/usr/bin/env python3
"""
Take a real spacecraft packet apart, byte by byte.

Run me:  python3 learn/toolbox/packet_explorer.py
   live: python3 learn/toolbox/packet_explorer.py --live   (needs `make run`)

Used by lessons 05-ccsds-packets and 06-pus-services.
"""

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "gnd"))

from pyground.packets import build_tc, crc16, parse_tm   # noqa: E402

BOX = "─" * 68


def title(text: str) -> None:
    print(f"\n{BOX}\n  {text}\n{BOX}")


def hexdump(raw: bytes) -> None:
    """Print bytes in rows of eight, with an offset ruler."""
    print("\n      offset  bytes")
    for start in range(0, len(raw), 8):
        chunk = raw[start:start + 8]
        hexes = " ".join(f"{b:02X}" for b in chunk)
        print(f"      {start:>6}  {hexes}")
    print(f"\n      total: {len(raw)} bytes")


def bits_of(value: int, width: int) -> str:
    return format(value, f"0{width}b")


def field(name: str, value: str, explanation: str) -> None:
    print(f"      {name:<22} {value:<14} {explanation}")


def bit_diagram(raw: bytes, fields: list[tuple[str, int, int]]) -> str:
    """
    Draw a bit-accurate diagram of a header.

    `fields` is a list of (label, first_bit, bit_count). The spans are
    computed from the bit positions rather than typed by hand, so the
    picture can never drift out of alignment with the bytes above it --
    which for a teaching diagram is the whole point.
    """
    bits = " ".join(format(b, "08b") for b in raw)

    def column(bit: int) -> int:
        """Where bit N lands in the string, allowing for the spaces."""
        return bit + bit // 8

    spans = [" "] * len(bits)
    labels = [" "] * len(bits)

    for number, (_label, start, count) in enumerate(fields, start=1):
        left = column(start)
        right = column(start + count - 1)
        width = right - left + 1
        tag = str(number)

        if width <= len(tag) + 1:
            # Too narrow for brackets; just fill the span with the number.
            for i in range(left, right + 1):
                spans[i] = tag[min(i - left, len(tag) - 1)]
        else:
            spans[left] = "["
            spans[right] = "]"
            for i in range(left + 1, right):
                spans[i] = "-"
            # Centre the number inside the span when there is room.
            middle = left + (width - len(tag)) // 2
            for offset, character in enumerate(tag):
                if left < middle + offset < right:
                    spans[middle + offset] = character

    del labels
    return f"      {bits}\n      {''.join(spans)}"


def explain_primary_header(raw: bytes) -> None:
    word0 = (raw[0] << 8) | raw[1]
    word1 = (raw[2] << 8) | raw[3]
    length = (raw[4] << 8) | raw[5]

    version = (word0 >> 13) & 0x7
    ptype = (word0 >> 12) & 0x1
    sec_hdr = (word0 >> 11) & 0x1
    apid = word0 & 0x7FF
    seq_flags = (word1 >> 14) & 0x3
    seq_count = word1 & 0x3FFF

    print("""
  The first SIX bytes are the CCSDS primary header. Every spacecraft
  in the world -- NASA, ESA, ISRO, JAXA, a university CubeSat -- puts
  exactly this in front of everything it sends.

  Here are those 48 bits, with each field numbered:
""")

    print(bit_diagram(raw[:6], [
        ("version", 0, 3),
        ("type", 3, 1),
        ("sec hdr", 4, 1),
        ("apid", 5, 11),
        ("seq flags", 16, 2),
        ("seq count", 18, 14),
        ("length", 32, 16),
    ]))

    print()
    field("1  version", bits_of(version, 3), "always 000")
    field("2  type", str(ptype), "0 = telemetry (down), 1 = telecommand (up)")
    field("3  secondary hdr", str(sec_hdr), "1 = a PUS header follows")
    field("4  APID", f"0x{apid:03X}", "WHICH part of the spacecraft is talking")
    field("5  sequence flags", bits_of(seq_flags, 2), "11 = one whole message, not a fragment")
    field("6  sequence count", str(seq_count), "counts up; a gap means a packet was lost")
    field("7  data length", str(length), "<-- careful! see below")

    print(f"""
      THE CLASSIC TRAP. The length field says {length}, but the data
      field is actually {length + 1} bytes. CCSDS stores "length MINUS ONE".

      Why? Because a packet with an EMPTY data field is not allowed,
      so a stored 0 would be wasted. Storing length-1 buys one extra
      byte of range. It has also caused approximately every ground
      station integration bug in history.

      total packet = 6 + {length} + 1 = {6 + length + 1} bytes
""")


def explain_tc_secondary(raw: bytes) -> None:
    sec = raw[6:11]
    version = (sec[0] >> 4) & 0xF
    ack = sec[0] & 0xF

    print("""
  The next FIVE bytes are the PUS telecommand header. CCSDS said how
  to wrap the message; PUS says what the message MEANS.
""")
    field("PUS version", str(version), "2 = PUS-C, the current standard")
    field("ack flags", bits_of(ack, 4), "which replies I want back")
    field("service type", str(sec[1]), "WHICH kind of thing I am asking for")
    field("message subtype", str(sec[2]), "WHICH specific request within it")
    field("source id", str((sec[3] << 8) | sec[4]), "who sent it")

    names = {1: "acceptance", 2: "start", 4: "progress", 8: "completion"}
    wanted = [text for bit, text in names.items() if ack & bit]
    print(f"""
      The ack flags are a small piece of good design. Instead of the
      spacecraft always replying (and flooding a narrow radio link),
      the sender ticks the boxes it cares about.

      This command asked for: {', '.join(wanted) if wanted else 'nothing'}
""")


def explain_crc(raw: bytes) -> None:
    crc = (raw[-2] << 8) | raw[-1]
    print(f"""
  The LAST TWO bytes are the checksum -- "packet error control".

      value:      0x{crc:04X}
      recomputed: 0x{crc16(raw[:-2]):04X}
      whole-packet check: 0x{crc16(raw):04X}  (zero means undamaged)

  See lesson 04 for why running the checksum over the checksum
  itself gives zero.
""")


def demo_telecommand() -> None:
    title("A TELECOMMAND: the ground asking the spacecraft to do something")

    packet = build_tc("SET_PARAM", param_id=1, value=250.0)

    print("""
  This is the command that tells the spacecraft "send housekeeping
  telemetry four times a second instead of once". Here it is as it
  would go over a radio:""")

    hexdump(packet)
    explain_primary_header(packet)
    explain_tc_secondary(packet)

    args = packet[11:-2]
    print(f"""
  Then the ARGUMENTS -- {len(args)} bytes, laid out exactly as
  dictionary/mission.yaml declares them for SET_PARAM:

      {' '.join(f'{b:02X}' for b in args)}
      └───┬──┘ └──────────────┬─────────────────┘
     param_id 1          value 250.0
     (uint16)            (float64, big endian)
""")
    explain_crc(packet)


def demo_telemetry(packet: bytes | None = None) -> None:
    title("TELEMETRY: the spacecraft answering")

    if packet is None:
        print("""
  Run this with --live while `make run` is going, and a REAL packet
  from the running spacecraft appears here instead of this note.
""")
        return

    hexdump(packet)
    explain_primary_header(packet)

    sec = packet[6:19]
    print("""
  The telemetry PUS header is THIRTEEN bytes -- longer than the
  command one, because telemetry carries a timestamp and a counter.
""")
    field("PUS version", str((sec[0] >> 4) & 0xF), "2 = PUS-C")
    field("time status", str(sec[0] & 0xF), "0 = clock not yet set from the ground")
    field("service type", str(sec[1]), "what kind of report this is")
    field("message subtype", str(sec[2]), "which specific report")
    field("message counter", str((sec[3] << 8) | sec[4]), "counts THIS kind of report")
    field("destination", str((sec[5] << 8) | sec[6]), "who it is for")
    coarse = int.from_bytes(sec[7:11], "big")
    fine = int.from_bytes(sec[11:13], "big")
    field("time (seconds)", str(coarse), "whole seconds since the mission epoch")
    field("time (fraction)", str(fine), f"= {fine / 65536:.4f} s, in 1/65536ths")

    decoded = parse_tm(packet)
    print(f"""
  Decoded, that whole packet means:

      {decoded.name}: {decoded.summary()}

  Every field name came from dictionary/mission.yaml. The spacecraft
  and the ground station agree because they were generated from the
  same file.
""")
    explain_crc(packet)


def fetch_live() -> bytes | None:
    from pyground.client import GroundClient
    try:
        with GroundClient(port=50001) as gnd:
            for tm in gnd.poll(timeout=3.0):
                if tm.name == "SYS_HK":
                    return _reassemble(gnd, tm)
    except ConnectionError:
        print("\n  (no spacecraft on port 50001 -- run `make run` first)\n")
    return None


def _reassemble(_gnd: object, _tm: object) -> None:
    # parse_tm() keeps the decoded view but not the original bytes, so the
    # live path grabs raw bytes directly instead. Kept simple on purpose.
    return None


def main() -> None:
    live = "--live" in sys.argv

    print("""
              WHAT DOES A SPACECRAFT MESSAGE LOOK LIKE?

  Not a picture. Not text. Just a row of numbers, with a structure
  that a hundred different organisations agreed on so that any ground
  station can understand any spacecraft.

  Let's take one apart.""")

    demo_telecommand()

    if live:
        demo_telemetry(_live_packet())
    else:
        demo_telemetry(None)

    title("Try this")
    print("""
  1. Change the command near the top of demo_telecommand():
        build_tc("TEST_CONNECTION")
     How many bytes now? Which parts got shorter?

  2. Run it twice. Which fields change, and which never do?

  3. Take the hexdump and decode it BY HAND on paper, using the
     diagram above. When you can do that, you can read the telemetry
     of any spacecraft in the world.

  4. Compare what you worked out against docs/ICD.md, which lists
     every field of every message this spacecraft has.
""")


def _live_packet() -> bytes | None:
    """Grab one real packet straight off the socket, bytes and all."""
    import socket
    import struct
    try:
        sock = socket.create_connection(("127.0.0.1", 50001), timeout=5.0)
    except OSError:
        print("\n  (no spacecraft on port 50001 -- run `make run` first)\n")
        return None

    buffer = bytearray()
    sock.settimeout(4.0)
    try:
        while len(buffer) < 4096:
            chunk = sock.recv(4096)
            if not chunk:
                break
            buffer += chunk
            while len(buffer) >= 6:
                (length,) = struct.unpack(">H", buffer[4:6])
                total = 6 + length + 1
                if len(buffer) < total:
                    break
                packet = bytes(buffer[:total])
                del buffer[:total]
                if packet[7] == 3 and packet[8] == 25:      # a housekeeping report
                    return packet
    except (OSError, socket.timeout):
        pass
    finally:
        sock.close()
    return None


if __name__ == "__main__":
    main()
