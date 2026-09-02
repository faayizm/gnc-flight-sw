// ============================================================================
//  GENERATED FILE -- DO NOT EDIT.
//  Source:    dictionary/mission.yaml
//  Generator: tools/gen.py
//  Edit the dictionary and run `make gen` instead.
// ============================================================================

#pragma once

#include <cstdint>

#include "core/bytes.hpp"
#include "generated/dictionary.hpp"

namespace fsw::cmd {

// Every telecommand this spacecraft accepts, as (service, subtype).
struct CommandInfo {
    uint8_t     service;
    uint8_t     subtype;
    uint16_t    arg_bytes;
    const char* name;
    const char* description;
};

inline constexpr CommandInfo kCommands[] = {
    { 17, 1, 0, "TEST_CONNECTION", "ST[17,1] connection test. Flight software answers with ST[17,2]." },
    { 3, 5, 1, "ENABLE_HK", "ST[3,5] enable periodic generation of a housekeeping structure." },
    { 3, 6, 1, "DISABLE_HK", "ST[3,6] disable periodic generation of a housekeeping structure." },
    { 20, 1, 2, "REPORT_PARAM", "ST[20,1] request the value of one on-board parameter, answered by ST[20,2]." },
    { 20, 3, 10, "SET_PARAM", "ST[20,3] set one on-board parameter. Value is interpreted per the parameter type." },
    { 8, 1, 1, "SET_MODE", "ST[8,1] request a spacecraft mode transition. The mode manager may refuse." },
    { 8, 2, 0, "RESET_COUNTERS", "ST[8,2] clear the housekeeping statistics counters." },
};
inline constexpr size_t kCommandCount = 7;

inline const CommandInfo* find_command(uint8_t service, uint8_t subtype) {
    for (size_t i = 0; i < kCommandCount; ++i) {
        if (kCommands[i].service == service && kCommands[i].subtype == subtype) {
            return &kCommands[i];
        }
    }
    return nullptr;
}

// ST[17,1] connection test. Flight software answers with ST[17,2].
struct TestConnectionArgs {
    // no arguments

    static constexpr uint8_t  kService   = 17;
    static constexpr uint8_t  kSubtype   = 1;
    static constexpr uint16_t kArgBytes  = 0;

    bool deserialize(core::ByteReader& r) {
        (void)r;
        return true;
    }

    bool serialize(core::ByteWriter& w) const {
        (void)w;
        return true;
    }
};

// ST[3,5] enable periodic generation of a housekeeping structure.
struct EnableHkArgs {
    uint8_t sid{};  // Housekeeping structure identifier

    static constexpr uint8_t  kService   = 3;
    static constexpr uint8_t  kSubtype   = 5;
    static constexpr uint16_t kArgBytes  = 1;

    bool deserialize(core::ByteReader& r) {
        return true
            && r.read_uint8(sid)
            ;
    }

    bool serialize(core::ByteWriter& w) const {
        return true
            && w.write_uint8(sid)
            ;
    }
};

// ST[3,6] disable periodic generation of a housekeeping structure.
struct DisableHkArgs {
    uint8_t sid{};  // Housekeeping structure identifier

    static constexpr uint8_t  kService   = 3;
    static constexpr uint8_t  kSubtype   = 6;
    static constexpr uint16_t kArgBytes  = 1;

    bool deserialize(core::ByteReader& r) {
        return true
            && r.read_uint8(sid)
            ;
    }

    bool serialize(core::ByteWriter& w) const {
        return true
            && w.write_uint8(sid)
            ;
    }
};

// ST[20,1] request the value of one on-board parameter, answered by ST[20,2].
struct ReportParamArgs {
    uint16_t param_id{};  // Parameter identifier

    static constexpr uint8_t  kService   = 20;
    static constexpr uint8_t  kSubtype   = 1;
    static constexpr uint16_t kArgBytes  = 2;

    bool deserialize(core::ByteReader& r) {
        return true
            && r.read_uint16(param_id)
            ;
    }

    bool serialize(core::ByteWriter& w) const {
        return true
            && w.write_uint16(param_id)
            ;
    }
};

// ST[20,3] set one on-board parameter. Value is interpreted per the parameter type.
struct SetParamArgs {
    uint16_t param_id{};  // Parameter identifier
    double value{};  // New value

    static constexpr uint8_t  kService   = 20;
    static constexpr uint8_t  kSubtype   = 3;
    static constexpr uint16_t kArgBytes  = 10;

    bool deserialize(core::ByteReader& r) {
        return true
            && r.read_uint16(param_id)
            && r.read_float64(value)
            ;
    }

    bool serialize(core::ByteWriter& w) const {
        return true
            && w.write_uint16(param_id)
            && w.write_float64(value)
            ;
    }
};

// ST[8,1] request a spacecraft mode transition. The mode manager may refuse.
struct SetModeArgs {
    uint8_t mode{};  // Requested mode

    static constexpr uint8_t  kService   = 8;
    static constexpr uint8_t  kSubtype   = 1;
    static constexpr uint16_t kArgBytes  = 1;

    bool deserialize(core::ByteReader& r) {
        return true
            && r.read_uint8(mode)
            ;
    }

    bool serialize(core::ByteWriter& w) const {
        return true
            && w.write_uint8(mode)
            ;
    }
};

// ST[8,2] clear the housekeeping statistics counters.
struct ResetCountersArgs {
    // no arguments

    static constexpr uint8_t  kService   = 8;
    static constexpr uint8_t  kSubtype   = 2;
    static constexpr uint16_t kArgBytes  = 0;

    bool deserialize(core::ByteReader& r) {
        (void)r;
        return true;
    }

    bool serialize(core::ByteWriter& w) const {
        (void)w;
        return true;
    }
};

}  // namespace fsw::cmd
