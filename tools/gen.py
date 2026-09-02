#!/usr/bin/env python3
"""
Code generator: dictionary/mission.yaml -> flight software, ground system, ICD.

This is the keystone of the repository. Every telemetry point, telecommand,
event and parameter is declared exactly once, in the dictionary, and this
script projects that declaration into every place it has to appear:

    fsw/generated/dictionary.hpp   ids, enums, event and parameter tables
    fsw/generated/telemetry.hpp    packed housekeeping structures + serialisers
    fsw/generated/commands.hpp     telecommand argument structures + parsers
    gnd/openc3/...                 OpenC3 COSMOS command & telemetry definitions
    gnd/pyground/dictionary.py     the Python ground client's view of the same
    docs/ICD.md                    the interface control document

Run with `make gen`. Never hand-edit the outputs.
"""

from __future__ import annotations

import datetime as _dt
import pathlib
import sys

import yaml

ROOT = pathlib.Path(__file__).resolve().parent.parent
DICT_PATH = ROOT / "dictionary" / "mission.yaml"

BANNER_C = """// ============================================================================
//  GENERATED FILE -- DO NOT EDIT.
//  Source:    dictionary/mission.yaml
//  Generator: tools/gen.py
//  Edit the dictionary and run `make gen` instead.
// ============================================================================
"""

BANNER_HASH = """# ============================================================================
#  GENERATED FILE -- DO NOT EDIT.
#  Source:    dictionary/mission.yaml
#  Generator: tools/gen.py
#  Edit the dictionary and run `make gen` instead.
# ============================================================================
"""

# name -> (C++ type, size in bytes, COSMOS type, COSMOS bit size, python struct code)
TYPES = {
    "uint8":   ("uint8_t",  1, "UINT",  8,  "B"),
    "int8":    ("int8_t",   1, "INT",   8,  "b"),
    "uint16":  ("uint16_t", 2, "UINT",  16, "H"),
    "int16":   ("int16_t",  2, "INT",   16, "h"),
    "uint32":  ("uint32_t", 4, "UINT",  32, "I"),
    "int32":   ("int32_t",  4, "INT",   32, "i"),
    "uint64":  ("uint64_t", 8, "UINT",  64, "Q"),
    "int64":   ("int64_t",  8, "INT",   64, "q"),
    "float32": ("float",    4, "FLOAT", 32, "f"),
    "float64": ("double",   8, "FLOAT", 64, "d"),
}

# Byte layout of the CCSDS primary header and the two PUS secondary headers.
CCSDS_PRIMARY_BYTES = 6
PUS_TM_SEC_BYTES = 13   # ver/timeref(1) service(1) subtype(1) msgcnt(2) destid(2) time(6)
PUS_TC_SEC_BYTES = 5    # ver/ack(1) service(1) subtype(1) sourceid(2)
CRC_BYTES = 2


def camel(name: str) -> str:
    """SYS_HK -> SysHk, test_connection -> TestConnection."""
    return "".join(part.capitalize() for part in name.split("_"))


def cpp_default(type_name: str, value) -> str:
    if type_name == "float32":
        return f"{float(value)}f"
    if type_name == "float64":
        return f"{float(value)}"
    return str(int(value))


class Dictionary:
    """Parsed and validated view of mission.yaml."""

    def __init__(self, raw: dict):
        self.mission = raw["mission"]
        self.apids = {k: int(v) for k, v in self.mission["apids"].items()}
        self.enums = raw.get("enums", {})
        self.telemetry = raw.get("telemetry", [])
        self.commands = raw.get("commands", [])
        self.events = raw.get("events", [])
        self.params = raw.get("params", [])
        self._validate()

    def _validate(self) -> None:
        errors = []

        seen_sid = {}
        for tm in self.telemetry:
            if tm["sid"] in seen_sid:
                errors.append(f"telemetry sid {tm['sid']} used by both "
                              f"{seen_sid[tm['sid']]} and {tm['name']}")
            seen_sid[tm["sid"]] = tm["name"]
            if tm["apid"] not in self.apids:
                errors.append(f"telemetry {tm['name']} references unknown apid {tm['apid']}")
            for f in tm["fields"]:
                if f["type"] not in TYPES:
                    errors.append(f"telemetry {tm['name']}.{f['name']} has unknown type {f['type']}")
                if "enum" in f and f["enum"] not in self.enums:
                    errors.append(f"telemetry {tm['name']}.{f['name']} references unknown enum {f['enum']}")

        seen_st = {}
        for cmd in self.commands:
            key = (cmd["service"], cmd["subtype"])
            if key in seen_st:
                errors.append(f"command ST[{key[0]},{key[1]}] used by both "
                              f"{seen_st[key]} and {cmd['name']}")
            seen_st[key] = cmd["name"]
            for a in cmd.get("args") or []:
                if a["type"] not in TYPES:
                    errors.append(f"command {cmd['name']}.{a['name']} has unknown type {a['type']}")

        seen_evt = {}
        for evt in self.events:
            if evt["id"] in seen_evt:
                errors.append(f"event id {evt['id']} used twice")
            seen_evt[evt["id"]] = evt["name"]
            if evt["severity"] not in self.enums["Severity"]["values"]:
                errors.append(f"event {evt['name']} has unknown severity {evt['severity']}")

        seen_par = {}
        for p in self.params:
            if p["id"] in seen_par:
                errors.append(f"parameter id {p['id']} used twice")
            seen_par[p["id"]] = p["name"]
            if p["type"] not in TYPES:
                errors.append(f"parameter {p['name']} has unknown type {p['type']}")
            if not (p["min"] <= p["default"] <= p["max"]):
                errors.append(f"parameter {p['name']} default {p['default']} outside "
                              f"[{p['min']}, {p['max']}]")

        if errors:
            for e in errors:
                print(f"dictionary error: {e}", file=sys.stderr)
            raise SystemExit(1)

    # -- derived sizes ------------------------------------------------------

    @staticmethod
    def payload_size(fields) -> int:
        return sum(TYPES[f["type"]][1] for f in fields)

    def hk_packet_size(self, tm) -> int:
        """Total on-the-wire size of one ST[3,25] report for this structure."""
        return (CCSDS_PRIMARY_BYTES + PUS_TM_SEC_BYTES + 1
                + self.payload_size(tm["fields"]) + CRC_BYTES)

    def tc_packet_size(self, cmd) -> int:
        return (CCSDS_PRIMARY_BYTES + PUS_TC_SEC_BYTES
                + self.payload_size(cmd.get("args") or []) + CRC_BYTES)


# ---------------------------------------------------------------------------
# C++ : dictionary.hpp
# ---------------------------------------------------------------------------

def gen_dictionary_hpp(d: Dictionary) -> str:
    o = [BANNER_C, "#pragma once", "", "#include <cstddef>", "#include <cstdint>", "",
         "namespace fsw::dict {", ""]

    o.append("// --- CCSDS application process identifiers ---------------------------------")
    o.append("enum class Apid : uint16_t {")
    for name, value in d.apids.items():
        o.append(f"    {name} = 0x{value:03X},")
    o.append("};")
    o.append("constexpr uint16_t apid_value(Apid a) { return static_cast<uint16_t>(a); }")
    o.append("")

    o.append("// --- Mission enumerations --------------------------------------------------")
    for ename, edef in d.enums.items():
        o.append(f"// {edef.get('desc', '')}")
        o.append(f"enum class {ename} : uint8_t {{")
        for vname, vval in edef["values"].items():
            o.append(f"    {vname} = {vval},")
        o.append("};")
        o.append(f"constexpr const char* to_string({ename} v) {{")
        o.append("    switch (v) {")
        for vname in edef["values"]:
            o.append(f"        case {ename}::{vname}: return \"{vname}\";")
        o.append("    }")
        o.append("    return \"UNKNOWN\";")
        o.append("}")
        o.append("")

    o.append("// --- Housekeeping structure identifiers ------------------------------------")
    o.append("enum class HkSid : uint8_t {")
    for tm in d.telemetry:
        o.append(f"    {tm['name']} = {tm['sid']},   // {tm['desc']}")
    o.append("};")
    o.append(f"inline constexpr size_t kHkStructureCount = {len(d.telemetry)};")
    o.append("")

    o.append("// --- On-board events, downlinked as PUS ST[05] -----------------------------")
    o.append("enum class EventId : uint16_t {")
    for e in d.events:
        o.append(f"    {e['name']} = {e['id']},")
    o.append("};")
    o.append("")
    o.append("struct EventInfo {")
    o.append("    EventId     id;")
    o.append("    Severity    severity;")
    o.append("    const char* name;")
    o.append("    const char* description;")
    o.append("};")
    o.append("")
    o.append("inline constexpr EventInfo kEvents[] = {")
    for e in d.events:
        o.append(f"    {{ EventId::{e['name']}, Severity::{e['severity']}, "
                 f"\"{e['name']}\", \"{e['desc']}\" }},")
    o.append("};")
    o.append(f"inline constexpr size_t kEventCount = {len(d.events)};")
    o.append("")
    o.append("inline const EventInfo* find_event(EventId id) {")
    o.append("    for (size_t i = 0; i < kEventCount; ++i) {")
    o.append("        if (kEvents[i].id == id) { return &kEvents[i]; }")
    o.append("    }")
    o.append("    return nullptr;")
    o.append("}")
    o.append("")

    o.append("// --- On-board parameters, accessed through PUS ST[20] ----------------------")
    o.append("enum class ParamId : uint16_t {")
    for p in d.params:
        o.append(f"    {p['name']} = {p['id']},")
    o.append("};")
    o.append("")
    o.append("enum class ParamType : uint8_t { U8, I8, U16, I16, U32, I32, U64, I64, F32, F64 };")
    o.append("")
    o.append("struct ParamInfo {")
    o.append("    ParamId     id;")
    o.append("    ParamType   type;")
    o.append("    const char* name;")
    o.append("    double      default_value;")
    o.append("    double      min_value;")
    o.append("    double      max_value;")
    o.append("    const char* units;")
    o.append("    const char* description;")
    o.append("};")
    o.append("")
    type_to_enum = {"uint8": "U8", "int8": "I8", "uint16": "U16", "int16": "I16",
                    "uint32": "U32", "int32": "I32", "uint64": "U64", "int64": "I64",
                    "float32": "F32", "float64": "F64"}
    o.append("inline constexpr ParamInfo kParams[] = {")
    for p in d.params:
        o.append(f"    {{ ParamId::{p['name']}, ParamType::{type_to_enum[p['type']]}, "
                 f"\"{p['name']}\", {float(p['default'])}, {float(p['min'])}, "
                 f"{float(p['max'])}, \"{p['units']}\", \"{p['desc']}\" }},")
    o.append("};")
    o.append(f"inline constexpr size_t kParamCount = {len(d.params)};")
    o.append("")
    o.append("inline const ParamInfo* find_param(ParamId id) {")
    o.append("    for (size_t i = 0; i < kParamCount; ++i) {")
    o.append("        if (kParams[i].id == id) { return &kParams[i]; }")
    o.append("    }")
    o.append("    return nullptr;")
    o.append("}")
    o.append("")
    o.append("}  // namespace fsw::dict")
    return "\n".join(o) + "\n"


# ---------------------------------------------------------------------------
# C++ : telemetry.hpp
# ---------------------------------------------------------------------------

def gen_telemetry_hpp(d: Dictionary) -> str:
    o = [BANNER_C, "#pragma once", "", "#include <cstdint>", "",
         '#include "core/bytes.hpp"', '#include "generated/dictionary.hpp"', "",
         "namespace fsw::tlm {", ""]

    for tm in d.telemetry:
        struct = camel(tm["name"])
        payload = d.payload_size(tm["fields"])
        o.append(f"// {tm['desc']}")
        o.append(f"// PUS ST[3,25] report, structure id {tm['sid']}, "
                 f"APID 0x{d.apids[tm['apid']]:03X}, nominal rate {tm['rate_hz']} Hz.")
        o.append(f"struct {struct} {{")
        for f in tm["fields"]:
            ctype = TYPES[f["type"]][0]
            comment = f"  // {f['desc']}"
            if f.get("units") and f["units"] != "-":
                comment += f" [{f['units']}]"
            o.append(f"    {ctype} {f['name']}{{}};{comment}")
        o.append("")
        o.append(f"    static constexpr dict::HkSid kSid  = dict::HkSid::{tm['name']};")
        o.append(f"    static constexpr dict::Apid  kApid = dict::Apid::{tm['apid']};")
        o.append(f"    static constexpr uint16_t kPayloadBytes = {payload};")
        o.append(f"    static constexpr uint16_t kPacketBytes  = {d.hk_packet_size(tm)};")
        o.append("")
        o.append("    // Serialises the field block only. The ST[3,25] structure id and the")
        o.append("    // packet headers are written by the telemetry builder.")
        o.append("    bool serialize(core::ByteWriter& w) const {")
        o.append("        return true")
        for f in tm["fields"]:
            o.append(f"            && w.write_{f['type']}({f['name']})")
        o.append("            ;")
        o.append("    }")
        o.append("")
        o.append("    bool deserialize(core::ByteReader& r) {")
        o.append("        return true")
        for f in tm["fields"]:
            o.append(f"            && r.read_{f['type']}({f['name']})")
        o.append("            ;")
        o.append("    }")
        o.append("};")
        o.append(f"static_assert(sizeof({struct}) > 0, \"{struct} must be instantiable\");")
        o.append("")

    o.append("}  // namespace fsw::tlm")
    return "\n".join(o) + "\n"


# ---------------------------------------------------------------------------
# C++ : commands.hpp
# ---------------------------------------------------------------------------

def gen_commands_hpp(d: Dictionary) -> str:
    o = [BANNER_C, "#pragma once", "", "#include <cstdint>", "",
         '#include "core/bytes.hpp"', '#include "generated/dictionary.hpp"', "",
         "namespace fsw::cmd {", ""]

    o.append("// Every telecommand this spacecraft accepts, as (service, subtype).")
    o.append("struct CommandInfo {")
    o.append("    uint8_t     service;")
    o.append("    uint8_t     subtype;")
    o.append("    uint16_t    arg_bytes;")
    o.append("    const char* name;")
    o.append("    const char* description;")
    o.append("};")
    o.append("")
    o.append("inline constexpr CommandInfo kCommands[] = {")
    for c in d.commands:
        args = c.get("args") or []
        o.append(f"    {{ {c['service']}, {c['subtype']}, {d.payload_size(args)}, "
                 f"\"{c['name']}\", \"{c['desc']}\" }},")
    o.append("};")
    o.append(f"inline constexpr size_t kCommandCount = {len(d.commands)};")
    o.append("")
    o.append("inline const CommandInfo* find_command(uint8_t service, uint8_t subtype) {")
    o.append("    for (size_t i = 0; i < kCommandCount; ++i) {")
    o.append("        if (kCommands[i].service == service && kCommands[i].subtype == subtype) {")
    o.append("            return &kCommands[i];")
    o.append("        }")
    o.append("    }")
    o.append("    return nullptr;")
    o.append("}")
    o.append("")

    for c in d.commands:
        struct = camel(c["name"]) + "Args"
        args = c.get("args") or []
        o.append(f"// {c['desc']}")
        o.append(f"struct {struct} {{")
        for a in args:
            ctype = TYPES[a["type"]][0]
            o.append(f"    {ctype} {a['name']}{{}};  // {a['desc']}")
        if not args:
            o.append("    // no arguments")
        o.append("")
        o.append(f"    static constexpr uint8_t  kService   = {c['service']};")
        o.append(f"    static constexpr uint8_t  kSubtype   = {c['subtype']};")
        o.append(f"    static constexpr uint16_t kArgBytes  = {d.payload_size(args)};")
        o.append("")
        o.append("    bool deserialize(core::ByteReader& r) {")
        if args:
            o.append("        return true")
            for a in args:
                o.append(f"            && r.read_{a['type']}({a['name']})")
            o.append("            ;")
        else:
            o.append("        (void)r;")
            o.append("        return true;")
        o.append("    }")
        o.append("")
        o.append("    bool serialize(core::ByteWriter& w) const {")
        if args:
            o.append("        return true")
            for a in args:
                o.append(f"            && w.write_{a['type']}({a['name']})")
            o.append("            ;")
        else:
            o.append("        (void)w;")
            o.append("        return true;")
        o.append("    }")
        o.append("};")
        o.append("")

    o.append("}  // namespace fsw::cmd")
    return "\n".join(o) + "\n"


# ---------------------------------------------------------------------------
# OpenC3 COSMOS
# ---------------------------------------------------------------------------

def _cosmos_tm_header(apid: int, service: int, subtype: int) -> list[str]:
    return [
        '  APPEND_ITEM    CCSDS_VERSION   3 UINT   "Packet version number, always 0"',
        '  APPEND_ITEM    CCSDS_TYPE      1 UINT   "Packet type, 0 = telemetry"',
        '  APPEND_ITEM    CCSDS_SECHDR    1 UINT   "Secondary header flag, always 1 for PUS"',
        f'  APPEND_ID_ITEM CCSDS_APID     11 UINT {apid} "Application process identifier"',
        '  APPEND_ITEM    CCSDS_SEQFLAGS  2 UINT   "Sequence flags, 3 = unsegmented"',
        '  APPEND_ITEM    CCSDS_SEQCNT   14 UINT   "Packet sequence count"',
        '  APPEND_ITEM    CCSDS_LENGTH   16 UINT   "Packet data length minus one"',
        '  APPEND_ITEM    PUS_VERSION     4 UINT   "PUS version number, always 2"',
        '  APPEND_ITEM    PUS_TIMEREF     4 UINT   "Spacecraft time reference status"',
        f'  APPEND_ID_ITEM PUS_SERVICE     8 UINT {service} "PUS service type"',
        f'  APPEND_ID_ITEM PUS_SUBTYPE     8 UINT {subtype} "PUS message subtype"',
        '  APPEND_ITEM    PUS_MSGCNT     16 UINT   "Message type counter"',
        '  APPEND_ITEM    PUS_DESTID     16 UINT   "Destination identifier"',
        '  APPEND_ITEM    PUS_TIME_SEC   32 UINT   "CUC coarse time, seconds since mission epoch"',
        '  APPEND_ITEM    PUS_TIME_SUB   16 UINT   "CUC fine time, units of 1/65536 s"',
    ]


def gen_cosmos_tlm(d: Dictionary) -> str:
    o = [BANNER_HASH]
    for tm in d.telemetry:
        apid = d.apids[tm["apid"]]
        o.append(f'TELEMETRY SAT {tm["name"]} BIG_ENDIAN "{tm["desc"]}"')
        o += _cosmos_tm_header(apid, 3, 25)
        o.append(f'  APPEND_ID_ITEM SID             8 UINT {tm["sid"]} "Housekeeping structure identifier"')
        for f in tm["fields"]:
            ctype, _, ctype_cosmos, bits, _ = TYPES[f["type"]]
            o.append(f'  APPEND_ITEM    {f["name"].upper():<14} {bits:>2} {ctype_cosmos:<5} "{f["desc"]}"')
            units = f.get("units")
            if units and units not in ("-", "bool", "mask", "count", "id", "level", "ticks"):
                o.append(f'    UNITS {units} {units}')
            if "enum" in f:
                for vname, vval in d.enums[f["enum"]]["values"].items():
                    o.append(f'    STATE {vname} {vval}')
            elif units == "bool":
                o.append('    STATE FALSE 0')
                o.append('    STATE TRUE 1')
        o.append('  APPEND_ITEM    PACKET_CRC     16 UINT  "CCSDS CRC-16 packet error control"')
        o.append("")

    # Event reports, ST[05]. One definition per severity subtype.
    for sev_name, sev_val in d.enums["Severity"]["values"].items():
        o.append(f'TELEMETRY SAT EVENT_{sev_name} BIG_ENDIAN "PUS ST[5,{sev_val}] {sev_name.lower()} event report"')
        o += _cosmos_tm_header(d.apids["TTC"], 5, sev_val)
        o.append('  APPEND_ITEM    EVENT_ID       16 UINT  "On-board event identifier"')
        for e in d.events:
            o.append(f'    STATE {e["name"]} {e["id"]}')
        o.append('  APPEND_ITEM    AUX_DATA       32 UINT  "Event auxiliary data"')
        o.append('  APPEND_ITEM    PACKET_CRC     16 UINT  "CCSDS CRC-16 packet error control"')
        o.append("")

    # Request verification, ST[01].
    verif = [(1, "ACCEPT_SUCCESS", "acceptance succeeded"),
             (2, "ACCEPT_FAIL", "acceptance failed"),
             (7, "COMPLETE_SUCCESS", "execution completed"),
             (8, "COMPLETE_FAIL", "execution failed")]
    for subtype, name, desc in verif:
        o.append(f'TELEMETRY SAT VERIF_{name} BIG_ENDIAN "PUS ST[1,{subtype}] {desc}"')
        o += _cosmos_tm_header(d.apids["TTC"], 1, subtype)
        o.append('  APPEND_ITEM    REQ_APID       16 UINT  "APID of the telecommand being reported on"')
        o.append('  APPEND_ITEM    REQ_SEQCNT     16 UINT  "Sequence count of that telecommand"')
        if subtype in (2, 8):
            o.append('  APPEND_ITEM    FAILURE_CODE   16 UINT  "Reason the request failed"')
            o.append('    STATE OK 0')
            o.append('    STATE BAD_CRC 1')
            o.append('    STATE BAD_LENGTH 2')
            o.append('    STATE UNKNOWN_SERVICE 3')
            o.append('    STATE ILLEGAL_ARG 4')
            o.append('    STATE UNAVAILABLE 5')
            o.append('    STATE REFUSED 6')
        o.append('  APPEND_ITEM    PACKET_CRC     16 UINT  "CCSDS CRC-16 packet error control"')
        o.append("")

    # Connection test report, ST[17,2].
    o.append('TELEMETRY SAT TEST_REPORT BIG_ENDIAN "PUS ST[17,2] connection test report"')
    o += _cosmos_tm_header(d.apids["TTC"], 17, 2)
    o.append('  APPEND_ITEM    PACKET_CRC     16 UINT  "CCSDS CRC-16 packet error control"')
    o.append("")

    # Parameter value report, ST[20,2].
    o.append('TELEMETRY SAT PARAM_REPORT BIG_ENDIAN "PUS ST[20,2] parameter value report"')
    o += _cosmos_tm_header(d.apids["TTC"], 20, 2)
    o.append('  APPEND_ITEM    PARAM_ID       16 UINT  "Parameter identifier"')
    for p in d.params:
        o.append(f'    STATE {p["name"]} {p["id"]}')
    o.append('  APPEND_ITEM    PARAM_VALUE    64 FLOAT "Parameter value, widened to double"')
    o.append('  APPEND_ITEM    PACKET_CRC     16 UINT  "CCSDS CRC-16 packet error control"')
    o.append("")

    return "\n".join(o) + "\n"


def gen_cosmos_cmd(d: Dictionary) -> str:
    o = [BANNER_HASH]
    gnd_apid = d.apids["GND"]
    for c in d.commands:
        args = c.get("args") or []
        data_len = PUS_TC_SEC_BYTES + d.payload_size(args) + CRC_BYTES
        o.append(f'COMMAND SAT {c["name"]} BIG_ENDIAN "{c["desc"]}"')
        o.append('  APPEND_PARAMETER    CCSDS_VERSION   3 UINT 0 0 0 "Packet version number"')
        o.append('  APPEND_PARAMETER    CCSDS_TYPE      1 UINT 1 1 1 "Packet type, 1 = telecommand"')
        o.append('  APPEND_PARAMETER    CCSDS_SECHDR    1 UINT 1 1 1 "Secondary header flag"')
        o.append(f'  APPEND_PARAMETER    CCSDS_APID     11 UINT {gnd_apid} {gnd_apid} {gnd_apid} "Ground segment APID"')
        o.append('  APPEND_PARAMETER    CCSDS_SEQFLAGS  2 UINT 3 3 3 "Sequence flags, 3 = unsegmented"')
        o.append('  APPEND_PARAMETER    CCSDS_SEQCNT   14 UINT 0 16383 0 "Packet sequence count"')
        o.append(f'  APPEND_PARAMETER    CCSDS_LENGTH   16 UINT 0 65535 {data_len - 1} "Packet data length minus one"')
        o.append('  APPEND_PARAMETER    PUS_VERSION     4 UINT 2 2 2 "PUS version number"')
        o.append('  APPEND_PARAMETER    PUS_ACK         4 UINT 0 15 9 "Acknowledgement flags, 9 = acceptance and completion"')
        o.append(f'  APPEND_ID_PARAMETER PUS_SERVICE     8 UINT {c["service"]} {c["service"]} {c["service"]} "PUS service type"')
        o.append(f'  APPEND_ID_PARAMETER PUS_SUBTYPE     8 UINT {c["subtype"]} {c["subtype"]} {c["subtype"]} "PUS message subtype"')
        o.append('  APPEND_PARAMETER    PUS_SOURCEID   16 UINT 0 65535 0 "Source identifier, echoed in verification reports"')
        for a in args:
            _, _, ctype_cosmos, bits, _ = TYPES[a["type"]]
            if "enum" in a:
                vals = d.enums[a["enum"]]["values"]
                lo, hi = min(vals.values()), max(vals.values())
                o.append(f'  APPEND_PARAMETER    {a["name"].upper():<14} {bits:>2} {ctype_cosmos} {lo} {hi} {lo} "{a["desc"]}"')
                for vname, vval in vals.items():
                    o.append(f'    STATE {vname} {vval}')
            elif ctype_cosmos == "FLOAT":
                o.append(f'  APPEND_PARAMETER    {a["name"].upper():<14} {bits:>2} {ctype_cosmos} MIN MAX 0.0 "{a["desc"]}"')
            else:
                o.append(f'  APPEND_PARAMETER    {a["name"].upper():<14} {bits:>2} {ctype_cosmos} MIN MAX 0 "{a["desc"]}"')
        o.append('  APPEND_PARAMETER    PACKET_CRC     16 UINT 0 65535 0 "CCSDS CRC-16, filled in by the CRC protocol"')
        o.append("")
    return "\n".join(o) + "\n"


def gen_cosmos_plugin(d: Dictionary) -> str:
    return BANNER_HASH + f"""
# OpenC3 COSMOS plugin definition for {d.mission['name']}.
#
# Bring it up with:   cd gnd/openc3 && ./install.sh
# The flight software listens as a TCP server, so COSMOS connects as a client.
#
# Framing: raw CCSDS Space Packets, no transfer frames. The LENGTH protocol
# reads the 16-bit CCSDS packet data length at bit offset 32 and adds 7
# (6 header bytes, plus 1 because CCSDS stores "length minus one").
#
# A real RF link would wrap these in TM/TC transfer frames with an attached
# sync marker, pseudo-randomisation and Reed-Solomon coding. That belongs to
# Phase 4 and is deliberately not present yet.

VARIABLE sat_target_name SAT
VARIABLE sat_host host.docker.internal
VARIABLE sat_port 50001

TARGET SAT <%= sat_target_name %>

INTERFACE <%= sat_target_name %>_INT tcpip_client_interface.rb <%= sat_host %> <%= sat_port %> <%= sat_port %> 10.0 nil LENGTH 32 16 7 1 BIG_ENDIAN 0 nil nil true
  MAP_TARGET <%= sat_target_name %>
  # Fill in the trailing CCSDS CRC-16 (poly 0x1021, seed 0xFFFF, no reflection)
  # on every uplinked telecommand, and verify it on every downlinked packet.
  PROTOCOL WRITE CrcProtocol PACKET_CRC FALSE ERROR -16 16 BIG_ENDIAN 0x1021 0xFFFF 0x0 FALSE FALSE
"""


def gen_cosmos_target(d: Dictionary) -> str:
    return BANNER_HASH + """
LANGUAGE ruby
IGNORE_PARAMETER CCSDS_VERSION
IGNORE_PARAMETER CCSDS_TYPE
IGNORE_PARAMETER CCSDS_SECHDR
IGNORE_PARAMETER CCSDS_APID
IGNORE_PARAMETER CCSDS_SEQFLAGS
IGNORE_PARAMETER CCSDS_SEQCNT
IGNORE_PARAMETER CCSDS_LENGTH
IGNORE_PARAMETER PUS_VERSION
IGNORE_PARAMETER PUS_SERVICE
IGNORE_PARAMETER PUS_SUBTYPE
IGNORE_PARAMETER PACKET_CRC
"""


def gen_cosmos_screen(d: Dictionary) -> str:
    """A single overview screen showing the health of the whole spacecraft."""
    o = [BANNER_HASH, "SCREEN AUTO AUTO 1.0", "",
         'VERTICALBOX "Spacecraft Overview"',
         "  LABELVALUE SAT SYS_HK MODE WITH_UNITS",
         "  LABELVALUE SAT SYS_HK UPTIME_S WITH_UNITS",
         "  LABELVALUE SAT SYS_HK CPU_LOAD_PCT WITH_UNITS",
         "  LABELVALUE SAT SYS_HK SCHED_OVERRUNS WITH_UNITS",
         "END", ""]
    o += ['VERTICALBOX "Ground Link"',
          "  LABELVALUE SAT SYS_HK LINK_UP WITH_UNITS",
          "  LABELVALUE SAT SYS_HK TC_RECEIVED WITH_UNITS",
          "  LABELVALUE SAT SYS_HK TC_REJECTED WITH_UNITS",
          "  LABELVALUE SAT SYS_HK TM_SENT WITH_UNITS",
          "END", ""]
    o += ['VERTICALBOX "Attitude"',
          "  LABELVALUE SAT ADCS_HK EST_STATE WITH_UNITS",
          "  LABELVALUE SAT ADCS_HK POINTING_ERR_DEG WITH_UNITS",
          "  LABELVALUE SAT ADCS_HK RATE_NORM WITH_UNITS",
          "  LINEGRAPH SAT ADCS_HK RATE_NORM",
          "  LINEGRAPH SAT ADCS_HK POINTING_ERR_DEG",
          "END", ""]
    o += ['VERTICALBOX "Power"',
          "  LABELVALUE SAT EPS_HK POWER_STATE WITH_UNITS",
          "  LABELVALUE SAT EPS_HK BATT_SOC_PCT WITH_UNITS",
          "  LABELVALUE SAT EPS_HK BATT_VOLTAGE WITH_UNITS",
          "  LINEGRAPH SAT EPS_HK BATT_SOC_PCT",
          "END", ""]
    return "\n".join(o) + "\n"


# ---------------------------------------------------------------------------
# Python ground client dictionary
# ---------------------------------------------------------------------------

def gen_pyground_dict(d: Dictionary) -> str:
    o = [BANNER_HASH, '"""Machine-generated mirror of the mission dictionary."""', "",
         "APIDS = {"]
    for k, v in d.apids.items():
        o.append(f"    {k!r}: 0x{v:03X},")
    o.append("}")
    o.append("")
    o.append("ENUMS = {")
    for ename, edef in d.enums.items():
        o.append(f"    {ename!r}: {edef['values']!r},")
    o.append("}")
    o.append("")
    o.append("# name -> (sid, apid, [(field, type, units, enum_or_None), ...])")
    o.append("TELEMETRY = {")
    for tm in d.telemetry:
        fields = [(f["name"], f["type"], f.get("units", ""), f.get("enum")) for f in tm["fields"]]
        o.append(f"    {tm['name']!r}: ({tm['sid']}, 0x{d.apids[tm['apid']]:03X}, {fields!r}),")
    o.append("}")
    o.append("")
    o.append("# name -> (service, subtype, [(arg, type, enum_or_None), ...])")
    o.append("COMMANDS = {")
    for c in d.commands:
        args = [(a["name"], a["type"], a.get("enum")) for a in (c.get("args") or [])]
        o.append(f"    {c['name']!r}: ({c['service']}, {c['subtype']}, {args!r}),")
    o.append("}")
    o.append("")
    o.append("# id -> (name, severity, description)")
    o.append("EVENTS = {")
    for e in d.events:
        o.append(f"    {e['id']}: ({e['name']!r}, {e['severity']!r}, {e['desc']!r}),")
    o.append("}")
    o.append("")
    o.append("# id -> (name, type, default, min, max, units, description)")
    o.append("PARAMS = {")
    for p in d.params:
        o.append(f"    {p['id']}: ({p['name']!r}, {p['type']!r}, {p['default']!r}, "
                 f"{p['min']!r}, {p['max']!r}, {p['units']!r}, {p['desc']!r}),")
    o.append("}")
    o.append("")
    o.append("STRUCT_CODES = {")
    for tname, spec in TYPES.items():
        o.append(f"    {tname!r}: {spec[4]!r},")
    o.append("}")
    o.append("")
    return "\n".join(o) + "\n"


# ---------------------------------------------------------------------------
# ICD
# ---------------------------------------------------------------------------

def gen_icd(d: Dictionary) -> str:
    o = [f"# {d.mission['name']} Interface Control Document", "",
         "*Generated from `dictionary/mission.yaml` by `tools/gen.py`. Do not edit.*", "",
         "All fields are big-endian. Packets follow CCSDS 133.0-B Space Packet Protocol",
         "with ECSS-E-ST-70-41C (PUS-C) secondary headers.", ""]

    o.append("## Application process identifiers")
    o.append("")
    o.append("| Application | APID |")
    o.append("|---|---|")
    for k, v in d.apids.items():
        o.append(f"| {k} | `0x{v:03X}` ({v}) |")
    o.append("")

    o.append("## Packet headers")
    o.append("")
    o.append("### CCSDS primary header (6 bytes, all packets)")
    o.append("")
    o.append("| Field | Bits | Value |")
    o.append("|---|---|---|")
    o.append("| Packet version number | 3 | 0 |")
    o.append("| Packet type | 1 | 0 = TM, 1 = TC |")
    o.append("| Secondary header flag | 1 | 1 |")
    o.append("| APID | 11 | see table above |")
    o.append("| Sequence flags | 2 | 3 (unsegmented) |")
    o.append("| Packet sequence count | 14 | increments per APID |")
    o.append("| Packet data length | 16 | octets after the header, minus one |")
    o.append("")
    o.append("### PUS TM secondary header (13 bytes)")
    o.append("")
    o.append("| Field | Bytes |")
    o.append("|---|---|")
    o.append("| TM packet PUS version (4 b) + time reference status (4 b) | 1 |")
    o.append("| Service type | 1 |")
    o.append("| Message subtype | 1 |")
    o.append("| Message type counter | 2 |")
    o.append("| Destination identifier | 2 |")
    o.append(f"| Time, CUC {d.mission['time']['coarse_bytes']} + {d.mission['time']['fine_bytes']} | 6 |")
    o.append("")
    o.append(f"Time is CCSDS Unsegmented Code referenced to **{d.mission['time']['epoch']}**: "
             "4 octets of coarse seconds followed by 2 octets of fine time in units of 1/65536 s.")
    o.append("")
    o.append("### PUS TC secondary header (5 bytes)")
    o.append("")
    o.append("| Field | Bytes |")
    o.append("|---|---|")
    o.append("| TC packet PUS version (4 b) + acknowledgement flags (4 b) | 1 |")
    o.append("| Service type | 1 |")
    o.append("| Message subtype | 1 |")
    o.append("| Source identifier | 2 |")
    o.append("")
    o.append("Every packet ends with a 2-byte packet error control field: CCSDS CRC-16, "
             "polynomial `0x1021`, seed `0xFFFF`, no reflection, no final XOR, computed "
             "over all preceding octets of the packet.")
    o.append("")

    o.append("## Housekeeping telemetry (PUS ST[3,25])")
    o.append("")
    for tm in d.telemetry:
        o.append(f"### {tm['name']} — structure id {tm['sid']}, APID `0x{d.apids[tm['apid']]:03X}`")
        o.append("")
        o.append(f"{tm['desc']} Nominal generation rate {tm['rate_hz']} Hz. "
                 f"Total packet size {d.hk_packet_size(tm)} bytes.")
        o.append("")
        o.append("| Offset | Field | Type | Units | Description |")
        o.append("|---:|---|---|---|---|")
        off = 0
        for f in tm["fields"]:
            size = TYPES[f["type"]][1]
            units = f.get("units", "")
            desc = f["desc"]
            if "enum" in f:
                states = ", ".join(f"{k}={v}" for k, v in d.enums[f["enum"]]["values"].items())
                desc += f" ({states})"
            o.append(f"| {off} | `{f['name']}` | {f['type']} | {units} | {desc} |")
            off += size
        o.append("")

    o.append("## Telecommands")
    o.append("")
    o.append("| Service | Subtype | Name | Args | Description |")
    o.append("|---:|---:|---|---:|---|")
    for c in d.commands:
        args = c.get("args") or []
        o.append(f"| {c['service']} | {c['subtype']} | `{c['name']}` | "
                 f"{d.payload_size(args)} B | {c['desc']} |")
    o.append("")
    for c in d.commands:
        args = c.get("args") or []
        if not args:
            continue
        o.append(f"### `{c['name']}` — ST[{c['service']},{c['subtype']}]")
        o.append("")
        o.append("| Offset | Argument | Type | Description |")
        o.append("|---:|---|---|---|")
        off = 0
        for a in args:
            desc = a["desc"]
            if "enum" in a:
                states = ", ".join(f"{k}={v}" for k, v in d.enums[a["enum"]]["values"].items())
                desc += f" ({states})"
            o.append(f"| {off} | `{a['name']}` | {a['type']} | {desc} |")
            off += TYPES[a["type"]][1]
        o.append("")

    o.append("## Request verification (PUS ST[01])")
    o.append("")
    o.append("| Subtype | Meaning |")
    o.append("|---:|---|")
    o.append("| 1 | Successful acceptance |")
    o.append("| 2 | Failed acceptance, carries a 16-bit failure code |")
    o.append("| 7 | Successful completion of execution |")
    o.append("| 8 | Failed completion, carries a 16-bit failure code |")
    o.append("")
    o.append("Each report carries the APID and packet sequence count of the telecommand "
             "it refers to, so the ground can correlate it unambiguously.")
    o.append("")
    o.append("| Failure code | Meaning |")
    o.append("|---:|---|")
    for i, name in enumerate(["OK", "BAD_CRC", "BAD_LENGTH", "UNKNOWN_SERVICE",
                              "ILLEGAL_ARG", "UNAVAILABLE", "REFUSED"]):
        o.append(f"| {i} | {name} |")
    o.append("")

    o.append("## Events (PUS ST[05])")
    o.append("")
    o.append("The message subtype carries the severity: 1 informative, 2 low, 3 medium, "
             "4 high. The source data is a 16-bit event identifier followed by 32 bits "
             "of auxiliary data whose meaning depends on the event.")
    o.append("")
    o.append("| ID | Name | Severity | Description |")
    o.append("|---:|---|---|---|")
    for e in d.events:
        o.append(f"| {e['id']} | `{e['name']}` | {e['severity']} | {e['desc']} |")
    o.append("")

    o.append("## On-board parameters (PUS ST[20])")
    o.append("")
    o.append("| ID | Name | Type | Default | Min | Max | Units | Description |")
    o.append("|---:|---|---|---:|---:|---:|---|---|")
    for p in d.params:
        o.append(f"| {p['id']} | `{p['name']}` | {p['type']} | {p['default']} | "
                 f"{p['min']} | {p['max']} | {p['units']} | {p['desc']} |")
    o.append("")
    o.append("`ST[20,1]` requests one parameter and is answered by `ST[20,2]`, which "
             "reports the identifier followed by the value widened to a 64-bit float. "
             "`ST[20,3]` sets a parameter; the value is sent as a 64-bit float and "
             "converted to the parameter's declared type, and is rejected with "
             "`ILLEGAL_ARG` if it falls outside the declared range.")
    o.append("")
    return "\n".join(o) + "\n"


# ---------------------------------------------------------------------------

def write(path: pathlib.Path, content: str, written: list) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    old = path.read_text() if path.exists() else None
    if old == content:
        written.append(("unchanged", path))
        return
    path.write_text(content)
    written.append(("written", path))


def main() -> int:
    raw = yaml.safe_load(DICT_PATH.read_text())
    d = Dictionary(raw)

    written: list = []
    write(ROOT / "fsw/generated/dictionary.hpp", gen_dictionary_hpp(d), written)
    write(ROOT / "fsw/generated/telemetry.hpp", gen_telemetry_hpp(d), written)
    write(ROOT / "fsw/generated/commands.hpp", gen_commands_hpp(d), written)
    write(ROOT / "gnd/openc3/plugin.txt", gen_cosmos_plugin(d), written)
    write(ROOT / "gnd/openc3/targets/SAT/target.txt", gen_cosmos_target(d), written)
    write(ROOT / "gnd/openc3/targets/SAT/cmd_tlm/tlm.txt", gen_cosmos_tlm(d), written)
    write(ROOT / "gnd/openc3/targets/SAT/cmd_tlm/cmd.txt", gen_cosmos_cmd(d), written)
    write(ROOT / "gnd/openc3/targets/SAT/screens/overview.txt", gen_cosmos_screen(d), written)
    write(ROOT / "gnd/pyground/dictionary.py", gen_pyground_dict(d), written)
    write(ROOT / "docs/ICD.md", gen_icd(d), written)

    changed = sum(1 for s, _ in written if s == "written")
    for status, path in written:
        mark = "*" if status == "written" else " "
        print(f" {mark} {path.relative_to(ROOT)}")
    print(f"\n{len(written)} outputs, {changed} changed "
          f"({_dt.datetime.now().strftime('%H:%M:%S')})")
    print(f"telemetry={len(d.telemetry)} commands={len(d.commands)} "
          f"events={len(d.events)} params={len(d.params)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
