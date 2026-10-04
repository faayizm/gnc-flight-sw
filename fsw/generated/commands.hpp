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
    uint16_t    arg_bytes;      // fixed argument length, ignored if variable
    bool        variable;       // argument block is free-form, checked by its handler
    const char* name;
    const char* description;
};

inline constexpr CommandInfo kCommands[] = {
    { 17, 1, 0, false, "TEST_CONNECTION", "ST[17,1] connection test. Flight software answers with ST[17,2]." },
    { 3, 5, 1, false, "ENABLE_HK", "ST[3,5] enable periodic generation of a housekeeping structure." },
    { 3, 6, 1, false, "DISABLE_HK", "ST[3,6] disable periodic generation of a housekeeping structure." },
    { 20, 1, 2, false, "REPORT_PARAM", "ST[20,1] request the value of one on-board parameter, answered by ST[20,2]." },
    { 20, 3, 10, false, "SET_PARAM", "ST[20,3] set one on-board parameter. Value is interpreted per the parameter type." },
    { 8, 1, 1, false, "SET_MODE", "ST[8,1] request a spacecraft mode transition. The mode manager may refuse." },
    { 8, 3, 2, false, "SWITCH_RAIL", "ST[8,3] (mission-specific) enable or disable a power rail. The rail is on only if the ground enables it AND the power and mode policy allow it." },
    { 8, 2, 0, false, "RESET_COUNTERS", "ST[8,2] clear the housekeeping statistics counters." },
    { 9, 1, 1, false, "SET_TIME_REPORT_RATE", "ST[9,1] generate a CUC time report every 2^rate_exp seconds; 255 stops them." },
    { 9, 128, 8, false, "ADJUST_TIME", "ST[9,128] (mission-specific) shift the on-board clock by delta_s and mark time as correlated. The ground computes delta from a time report." },
    { 11, 1, 0, false, "ENABLE_SCHEDULE", "ST[11,1] enable the release of time-tagged telecommands." },
    { 11, 2, 0, false, "DISABLE_SCHEDULE", "ST[11,2] disable release; activities stay stored." },
    { 11, 3, 0, false, "RESET_SCHEDULE", "ST[11,3] delete every scheduled activity." },
    { 11, 4, 0, true, "INSERT_ACTIVITIES", "ST[11,4] insert time-tagged telecommands. Data: count (u8), then per activity release time (CUC coarse u32, fine u16) and a complete telecommand packet. All-or-nothing: one bad activity rejects the request." },
    { 15, 1, 1, false, "ENABLE_STORAGE", "ST[15,1] start recording telemetry into a packet store." },
    { 15, 2, 1, false, "DISABLE_STORAGE", "ST[15,2] stop recording into a packet store." },
    { 15, 9, 9, false, "RETRIEVE_BY_TIME", "ST[15,9] replay stored packets with on-board time in [from_s, to_s] on the playback virtual channel." },
    { 15, 11, 5, false, "DELETE_STORE_UP_TO", "ST[15,11] delete stored packets older than to_s." },
    { 15, 12, 1, false, "REPORT_STORE_SUMMARY", "ST[15,12] request a packet store summary, answered by ST[15,13]." },
};
inline constexpr size_t kCommandCount = 19;

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

// ST[8,3] (mission-specific) enable or disable a power rail. The rail is on only if the ground enables it AND the power and mode policy allow it.
struct SwitchRailArgs {
    uint8_t rail{};  // Rail to switch
    uint8_t on{};  // 1 to enable

    static constexpr uint8_t  kService   = 8;
    static constexpr uint8_t  kSubtype   = 3;
    static constexpr uint16_t kArgBytes  = 2;

    bool deserialize(core::ByteReader& r) {
        return true
            && r.read_uint8(rail)
            && r.read_uint8(on)
            ;
    }

    bool serialize(core::ByteWriter& w) const {
        return true
            && w.write_uint8(rail)
            && w.write_uint8(on)
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

// ST[9,1] generate a CUC time report every 2^rate_exp seconds; 255 stops them.
struct SetTimeReportRateArgs {
    uint8_t rate_exp{};  // Report period exponent (0 = every second)

    static constexpr uint8_t  kService   = 9;
    static constexpr uint8_t  kSubtype   = 1;
    static constexpr uint16_t kArgBytes  = 1;

    bool deserialize(core::ByteReader& r) {
        return true
            && r.read_uint8(rate_exp)
            ;
    }

    bool serialize(core::ByteWriter& w) const {
        return true
            && w.write_uint8(rate_exp)
            ;
    }
};

// ST[9,128] (mission-specific) shift the on-board clock by delta_s and mark time as correlated. The ground computes delta from a time report.
struct AdjustTimeArgs {
    double delta_s{};  // Correction to add to on-board time

    static constexpr uint8_t  kService   = 9;
    static constexpr uint8_t  kSubtype   = 128;
    static constexpr uint16_t kArgBytes  = 8;

    bool deserialize(core::ByteReader& r) {
        return true
            && r.read_float64(delta_s)
            ;
    }

    bool serialize(core::ByteWriter& w) const {
        return true
            && w.write_float64(delta_s)
            ;
    }
};

// ST[11,1] enable the release of time-tagged telecommands.
struct EnableScheduleArgs {
    // no arguments

    static constexpr uint8_t  kService   = 11;
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

// ST[11,2] disable release; activities stay stored.
struct DisableScheduleArgs {
    // no arguments

    static constexpr uint8_t  kService   = 11;
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

// ST[11,3] delete every scheduled activity.
struct ResetScheduleArgs {
    // no arguments

    static constexpr uint8_t  kService   = 11;
    static constexpr uint8_t  kSubtype   = 3;
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

// ST[11,4] insert time-tagged telecommands. Data: count (u8), then per activity release time (CUC coarse u32, fine u16) and a complete telecommand packet. All-or-nothing: one bad activity rejects the request.
struct InsertActivitiesArgs {
    // no arguments

    static constexpr uint8_t  kService   = 11;
    static constexpr uint8_t  kSubtype   = 4;
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

// ST[15,1] start recording telemetry into a packet store.
struct EnableStorageArgs {
    uint8_t store_id{};  // Packet store identifier

    static constexpr uint8_t  kService   = 15;
    static constexpr uint8_t  kSubtype   = 1;
    static constexpr uint16_t kArgBytes  = 1;

    bool deserialize(core::ByteReader& r) {
        return true
            && r.read_uint8(store_id)
            ;
    }

    bool serialize(core::ByteWriter& w) const {
        return true
            && w.write_uint8(store_id)
            ;
    }
};

// ST[15,2] stop recording into a packet store.
struct DisableStorageArgs {
    uint8_t store_id{};  // Packet store identifier

    static constexpr uint8_t  kService   = 15;
    static constexpr uint8_t  kSubtype   = 2;
    static constexpr uint16_t kArgBytes  = 1;

    bool deserialize(core::ByteReader& r) {
        return true
            && r.read_uint8(store_id)
            ;
    }

    bool serialize(core::ByteWriter& w) const {
        return true
            && w.write_uint8(store_id)
            ;
    }
};

// ST[15,9] replay stored packets with on-board time in [from_s, to_s] on the playback virtual channel.
struct RetrieveByTimeArgs {
    uint8_t store_id{};  // Packet store identifier
    uint32_t from_s{};  // Start of the time range (CUC coarse seconds)
    uint32_t to_s{};  // End of the time range (CUC coarse seconds)

    static constexpr uint8_t  kService   = 15;
    static constexpr uint8_t  kSubtype   = 9;
    static constexpr uint16_t kArgBytes  = 9;

    bool deserialize(core::ByteReader& r) {
        return true
            && r.read_uint8(store_id)
            && r.read_uint32(from_s)
            && r.read_uint32(to_s)
            ;
    }

    bool serialize(core::ByteWriter& w) const {
        return true
            && w.write_uint8(store_id)
            && w.write_uint32(from_s)
            && w.write_uint32(to_s)
            ;
    }
};

// ST[15,11] delete stored packets older than to_s.
struct DeleteStoreUpToArgs {
    uint8_t store_id{};  // Packet store identifier
    uint32_t to_s{};  // Delete everything before this time (CUC coarse seconds)

    static constexpr uint8_t  kService   = 15;
    static constexpr uint8_t  kSubtype   = 11;
    static constexpr uint16_t kArgBytes  = 5;

    bool deserialize(core::ByteReader& r) {
        return true
            && r.read_uint8(store_id)
            && r.read_uint32(to_s)
            ;
    }

    bool serialize(core::ByteWriter& w) const {
        return true
            && w.write_uint8(store_id)
            && w.write_uint32(to_s)
            ;
    }
};

// ST[15,12] request a packet store summary, answered by ST[15,13].
struct ReportStoreSummaryArgs {
    uint8_t store_id{};  // Packet store identifier

    static constexpr uint8_t  kService   = 15;
    static constexpr uint8_t  kSubtype   = 12;
    static constexpr uint16_t kArgBytes  = 1;

    bool deserialize(core::ByteReader& r) {
        return true
            && r.read_uint8(store_id)
            ;
    }

    bool serialize(core::ByteWriter& w) const {
        return true
            && w.write_uint8(store_id)
            ;
    }
};

}  // namespace fsw::cmd
