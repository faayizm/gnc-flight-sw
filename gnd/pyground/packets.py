"""
CCSDS Space Packet and ECSS PUS encoding and decoding.

This is deliberately an INDEPENDENT implementation of the same standards the
flight software implements in C++. It was not translated from that code. That
independence is the point: when both sides agree on a packet, it is evidence
that the packet matches the standard, rather than evidence that the same
misreading was made twice.

Field layouts are documented in docs/ICD.md, which is generated from the same
dictionary that generated the tables this module imports.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from typing import Any

from .dictionary import APIDS, COMMANDS, ENUMS, EVENTS, PARAMS, STRUCT_CODES, TELEMETRY

CCSDS_HEADER_BYTES = 6
PUS_TM_HEADER_BYTES = 13   # 1 + 1 + 1 + 2 + 2 + 6
PUS_TC_HEADER_BYTES = 5    # 1 + 1 + 1 + 2
CRC_BYTES = 2
PUS_VERSION = 2

ACK_ACCEPTANCE = 0x1
ACK_START = 0x2
ACK_PROGRESS = 0x4
ACK_COMPLETION = 0x8

# Failure codes, mirroring core::FailureCode. See docs/ICD.md.
FAILURE_CODES = {
    0: "OK",
    1: "BAD_CRC",
    2: "BAD_LENGTH",
    3: "UNKNOWN_SERVICE",
    4: "ILLEGAL_ARG",
    5: "UNAVAILABLE",
    6: "REFUSED",
}

SEVERITY_NAMES = {1: "INFO", 2: "LOW", 3: "MEDIUM", 4: "HIGH"}


def crc16(data: bytes, seed: int = 0xFFFF) -> int:
    """
    CCSDS CRC-16: polynomial 0x1021, seed 0xFFFF, no reflection, no final XOR.

    The check value for b"123456789" is 0x29B1, which is the same constant the
    C++ unit tests assert against.
    """
    crc = seed
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


# ---------------------------------------------------------------------------
# Telecommands (uplink)
# ---------------------------------------------------------------------------


@dataclass
class Telecommand:
    name: str
    service: int
    subtype: int
    args: dict[str, Any] = field(default_factory=dict)
    apid: int = APIDS["GND"]
    sequence_count: int = 0
    ack_flags: int = ACK_ACCEPTANCE | ACK_COMPLETION


def build_tc(name: str, sequence_count: int = 0, **args: Any) -> bytes:
    """
    Encode a telecommand by dictionary name, e.g. build_tc("SET_PARAM",
    param_id=1, value=500.0).

    Every argument is checked against the dictionary before anything is
    written, so a typo produces a clear error here rather than an
    UNKNOWN_SERVICE or BAD_LENGTH rejection from the spacecraft.
    """
    if name not in COMMANDS:
        raise KeyError(f"unknown telecommand {name!r}; "
                       f"known: {', '.join(sorted(COMMANDS))}")

    service, subtype, arg_spec = COMMANDS[name]

    expected = {a[0] for a in arg_spec}
    provided = set(args)
    if provided != expected:
        missing = expected - provided
        extra = provided - expected
        problems = []
        if missing:
            problems.append(f"missing {sorted(missing)}")
        if extra:
            problems.append(f"unexpected {sorted(extra)}")
        raise TypeError(f"{name}: " + ", ".join(problems))

    # Argument block, big-endian, in the order the dictionary declares.
    payload = b""
    for arg_name, arg_type, enum_name in arg_spec:
        value = args[arg_name]
        if enum_name is not None and isinstance(value, str):
            values = ENUMS[enum_name]
            if value not in values:
                raise ValueError(f"{name}.{arg_name}: {value!r} is not one of "
                                 f"{sorted(values)}")
            value = values[value]
        payload += struct.pack(">" + STRUCT_CODES[arg_type], value)

    data_field_len = PUS_TC_HEADER_BYTES + len(payload) + CRC_BYTES

    # CCSDS primary header. Type bit 1 = telecommand, secondary header flag 1,
    # sequence flags 3 = unsegmented, and the length field is "minus one".
    word0 = (0 << 13) | (1 << 12) | (1 << 11) | (APIDS["GND"] & 0x7FF)
    word1 = (0x3 << 14) | (sequence_count & 0x3FFF)
    packet = struct.pack(">HHH", word0, word1, data_field_len - 1)

    # PUS TC secondary header.
    packet += struct.pack(
        ">BBBH",
        ((PUS_VERSION & 0xF) << 4) | ((ACK_ACCEPTANCE | ACK_COMPLETION) & 0xF),
        service,
        subtype,
        0,  # source id
    )
    packet += payload
    packet += struct.pack(">H", crc16(packet))
    return packet


# ---------------------------------------------------------------------------
# Telemetry (downlink)
# ---------------------------------------------------------------------------


@dataclass
class Telemetry:
    apid: int
    sequence_count: int
    service: int
    subtype: int
    message_count: int
    destination: int
    time_s: float
    payload: bytes
    crc_ok: bool
    name: str = "UNKNOWN"
    fields: dict[str, Any] = field(default_factory=dict)

    def summary(self) -> str:
        """One line, suitable for a scrolling monitor."""
        if self.name.startswith("EVENT"):
            return (f"{self.name:<18} {self.fields.get('event_name', '?')} "
                    f"aux={self.fields.get('aux', 0)}")
        if self.name.startswith("VERIF"):
            extra = ""
            if "failure" in self.fields:
                extra = f" reason={self.fields['failure']}"
            return (f"{self.name:<18} tc(apid=0x{self.fields.get('req_apid', 0):03X}, "
                    f"seq={self.fields.get('req_seqcnt', 0)}){extra}")
        if self.name == "PARAM_REPORT":
            return (f"{self.name:<18} {self.fields.get('param_name', '?')} = "
                    f"{self.fields.get('value', 0)}")
        if self.fields:
            shown = list(self.fields.items())[:4]
            body = "  ".join(f"{k}={_fmt(v)}" for k, v in shown)
            return f"{self.name:<18} {body}"
        return self.name


def _fmt(value: Any) -> str:
    if isinstance(value, float):
        return f"{value:.4g}"
    return str(value)


def parse_tm(packet: bytes) -> Telemetry:
    """
    Decode one downlinked packet. Never raises on a malformed packet: a ground
    system that crashes on bad telemetry is a ground system that is useless at
    exactly the moment it is needed, so problems are reported in the returned
    object instead.
    """
    if len(packet) < CCSDS_HEADER_BYTES + PUS_TM_HEADER_BYTES + CRC_BYTES:
        return Telemetry(0, 0, 0, 0, 0, 0, 0.0, b"", False, name="TRUNCATED")

    crc_ok = crc16(packet) == 0

    word0, word1, _length = struct.unpack(">HHH", packet[:6])
    apid = word0 & 0x7FF
    sequence_count = word1 & 0x3FFF

    sec = packet[6:6 + PUS_TM_HEADER_BYTES]
    _ver_time, service, subtype, message_count, destination, coarse, fine = \
        struct.unpack(">BBBHHIH", sec)
    time_s = coarse + fine / 65536.0

    payload = packet[6 + PUS_TM_HEADER_BYTES:-CRC_BYTES]

    tm = Telemetry(apid, sequence_count, service, subtype, message_count,
                   destination, time_s, payload, crc_ok)
    _decode_payload(tm)
    return tm


def _decode_payload(tm: Telemetry) -> None:
    """Fill in tm.name and tm.fields from the service and subtype."""
    try:
        if tm.service == 3 and tm.subtype == 25:
            _decode_housekeeping(tm)
        elif tm.service == 5:
            _decode_event(tm)
        elif tm.service == 1:
            _decode_verification(tm)
        elif tm.service == 17 and tm.subtype == 2:
            tm.name = "TEST_REPORT"
        elif tm.service == 20 and tm.subtype == 2:
            _decode_param_report(tm)
        else:
            tm.name = f"ST[{tm.service},{tm.subtype}]"
    except (struct.error, IndexError, KeyError):
        # Malformed for its declared type. Keep whatever was decoded and mark
        # it, rather than losing the packet entirely.
        tm.name = f"{tm.name}(UNDECODABLE)"


def _decode_housekeeping(tm: Telemetry) -> None:
    sid = tm.payload[0]
    for name, (structure_id, _apid, fields) in TELEMETRY.items():
        if structure_id != sid:
            continue
        tm.name = name
        offset = 1
        for field_name, field_type, _units, enum_name in fields:
            code = STRUCT_CODES[field_type]
            size = struct.calcsize(code)
            (value,) = struct.unpack_from(">" + code, tm.payload, offset)
            offset += size
            if enum_name is not None:
                names = {v: k for k, v in ENUMS[enum_name].items()}
                value = names.get(value, value)
            tm.fields[field_name] = value
        return
    tm.name = f"HK(sid={sid})"


def _decode_event(tm: Telemetry) -> None:
    tm.name = f"EVENT_{SEVERITY_NAMES.get(tm.subtype, tm.subtype)}"
    event_id, aux = struct.unpack(">HI", tm.payload[:6])
    info = EVENTS.get(event_id)
    tm.fields["event_id"] = event_id
    tm.fields["event_name"] = info[0] if info else f"UNKNOWN({event_id})"
    tm.fields["description"] = info[2] if info else ""
    tm.fields["aux"] = aux
    # For a rejection, the auxiliary data is the failure code -- decode it,
    # because "TC_REJECTED aux=1" is much less useful than "BAD_CRC".
    if info and info[0] == "TC_REJECTED":
        tm.fields["aux"] = FAILURE_CODES.get(aux, aux)


def _decode_verification(tm: Telemetry) -> None:
    names = {1: "VERIF_ACCEPT_OK", 2: "VERIF_ACCEPT_FAIL",
             7: "VERIF_COMPLETE_OK", 8: "VERIF_COMPLETE_FAIL"}
    tm.name = names.get(tm.subtype, f"VERIF[{tm.subtype}]")
    req_apid, req_seq = struct.unpack(">HH", tm.payload[:4])
    tm.fields["req_apid"] = req_apid
    tm.fields["req_seqcnt"] = req_seq
    if tm.subtype in (2, 8) and len(tm.payload) >= 6:
        (code,) = struct.unpack(">H", tm.payload[4:6])
        tm.fields["failure"] = FAILURE_CODES.get(code, code)


def _decode_param_report(tm: Telemetry) -> None:
    tm.name = "PARAM_REPORT"
    param_id, value = struct.unpack(">Hd", tm.payload[:10])
    info = PARAMS.get(param_id)
    tm.fields["param_id"] = param_id
    tm.fields["param_name"] = info[0] if info else f"UNKNOWN({param_id})"
    tm.fields["value"] = value
    if info:
        tm.fields["units"] = info[5]
