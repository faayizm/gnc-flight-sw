// ============================================================================
//  GENERATED FILE -- DO NOT EDIT.
//  Source:    dictionary/mission.yaml
//  Generator: tools/gen.py
//  Edit the dictionary and run `make gen` instead.
// ============================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace fsw::dict {

// --- CCSDS application process identifiers ---------------------------------
enum class Apid : uint16_t {
    TTC = 0x001,
    ADCS = 0x002,
    EPS = 0x003,
    GND = 0x00A,
};
constexpr uint16_t apid_value(Apid a) { return static_cast<uint16_t>(a); }

// --- Space link --------------------------------------------------------------
namespace link {
constexpr uint16_t kScid = 421;
constexpr uint16_t kTmFrameBytes = 223;
constexpr uint16_t kTcMaxFrameBytes = 1024;
constexpr uint16_t kVcRealtime = 0;
constexpr uint16_t kVcPlayback = 1;
constexpr uint16_t kVcIdle = 7;
constexpr uint16_t kTcVc = 0;
constexpr uint16_t kFarmWindow = 10;
}  // namespace link

// --- Mission enumerations --------------------------------------------------
// Top-level spacecraft mode owned by the mode manager.
enum class SystemMode : uint8_t {
    BOOT = 0,
    SAFE = 1,
    DETUMBLE = 2,
    STANDBY = 3,
    POINTING = 4,
};
constexpr const char* to_string(SystemMode v) {
    switch (v) {
        case SystemMode::BOOT: return "BOOT";
        case SystemMode::SAFE: return "SAFE";
        case SystemMode::DETUMBLE: return "DETUMBLE";
        case SystemMode::STANDBY: return "STANDBY";
        case SystemMode::POINTING: return "POINTING";
    }
    return "UNKNOWN";
}

// Convergence state of the attitude estimator.
enum class AdcsEstState : uint8_t {
    INVALID = 0,
    INITIALISING = 1,
    CONVERGING = 2,
    CONVERGED = 3,
};
constexpr const char* to_string(AdcsEstState v) {
    switch (v) {
        case AdcsEstState::INVALID: return "INVALID";
        case AdcsEstState::INITIALISING: return "INITIALISING";
        case AdcsEstState::CONVERGING: return "CONVERGING";
        case AdcsEstState::CONVERGED: return "CONVERGED";
    }
    return "UNKNOWN";
}

// What the attitude controller is doing.
enum class AdcsCtrlMode : uint8_t {
    IDLE = 0,
    DETUMBLE = 1,
    STANDBY = 2,
    POINTING = 3,
};
constexpr const char* to_string(AdcsCtrlMode v) {
    switch (v) {
        case AdcsCtrlMode::IDLE: return "IDLE";
        case AdcsCtrlMode::DETUMBLE: return "DETUMBLE";
        case AdcsCtrlMode::STANDBY: return "STANDBY";
        case AdcsCtrlMode::POINTING: return "POINTING";
    }
    return "UNKNOWN";
}

// Coarse battery / power bus state used by mode arbitration.
enum class PowerState : uint8_t {
    UNKNOWN = 0,
    NOMINAL = 1,
    LOW = 2,
    CRITICAL = 3,
};
constexpr const char* to_string(PowerState v) {
    switch (v) {
        case PowerState::UNKNOWN: return "UNKNOWN";
        case PowerState::NOMINAL: return "NOMINAL";
        case PowerState::LOW: return "LOW";
        case PowerState::CRITICAL: return "CRITICAL";
    }
    return "UNKNOWN";
}

// PUS ST[05] event severity, mapped to subtypes 1..4.
enum class Severity : uint8_t {
    INFO = 1,
    LOW = 2,
    MEDIUM = 3,
    HIGH = 4,
};
constexpr const char* to_string(Severity v) {
    switch (v) {
        case Severity::INFO: return "INFO";
        case Severity::LOW: return "LOW";
        case Severity::MEDIUM: return "MEDIUM";
        case Severity::HIGH: return "HIGH";
    }
    return "UNKNOWN";
}

// --- Housekeeping structure identifiers ------------------------------------
enum class HkSid : uint8_t {
    SYS_HK = 1,   // Core system health, scheduler timing and link statistics.
    ADCS_HK = 2,   // Attitude determination and control state.
    EPS_HK = 3,   // Power subsystem state. Populated from Phase 5 onward.
};
inline constexpr size_t kHkStructureCount = 3;

// --- On-board events, downlinked as PUS ST[05] -----------------------------
enum class EventId : uint16_t {
    BOOT_COMPLETE = 1,
    MODE_CHANGED = 2,
    LINK_CONNECTED = 3,
    LINK_LOST = 4,
    TC_REJECTED = 5,
    HK_ENABLED = 6,
    HK_DISABLED = 7,
    PARAM_SET = 8,
    SCHED_OVERRUN = 9,
    MODE_REFUSED = 10,
    SAFE_MODE_ENTERED = 11,
    DETUMBLE_STARTED = 12,
    DETUMBLE_COMPLETE = 13,
    SENSOR_TIMEOUT = 14,
    ESTIMATOR_INIT = 16,
    ESTIMATOR_CONVERGED = 17,
    POINTING_STARTED = 18,
    ESTIMATOR_RESET = 19,
    SENSOR_RESTORED = 15,
};

struct EventInfo {
    EventId     id;
    Severity    severity;
    const char* name;
    const char* description;
};

inline constexpr EventInfo kEvents[] = {
    { EventId::BOOT_COMPLETE, Severity::INFO, "BOOT_COMPLETE", "Flight software finished initialisation" },
    { EventId::MODE_CHANGED, Severity::INFO, "MODE_CHANGED", "Spacecraft mode transition executed" },
    { EventId::LINK_CONNECTED, Severity::INFO, "LINK_CONNECTED", "Ground link established" },
    { EventId::LINK_LOST, Severity::LOW, "LINK_LOST", "Ground link dropped" },
    { EventId::TC_REJECTED, Severity::LOW, "TC_REJECTED", "Telecommand failed acceptance checks" },
    { EventId::HK_ENABLED, Severity::INFO, "HK_ENABLED", "Housekeeping structure generation enabled" },
    { EventId::HK_DISABLED, Severity::INFO, "HK_DISABLED", "Housekeeping structure generation disabled" },
    { EventId::PARAM_SET, Severity::INFO, "PARAM_SET", "On-board parameter modified from ground" },
    { EventId::SCHED_OVERRUN, Severity::MEDIUM, "SCHED_OVERRUN", "A rate group missed its deadline" },
    { EventId::MODE_REFUSED, Severity::LOW, "MODE_REFUSED", "Requested mode transition was refused" },
    { EventId::SAFE_MODE_ENTERED, Severity::HIGH, "SAFE_MODE_ENTERED", "Spacecraft autonomously entered safe mode" },
    { EventId::DETUMBLE_STARTED, Severity::INFO, "DETUMBLE_STARTED", "B-dot detumble control engaged" },
    { EventId::DETUMBLE_COMPLETE, Severity::INFO, "DETUMBLE_COMPLETE", "Body rate fell below the hand-over threshold and detumble control stopped" },
    { EventId::SENSOR_TIMEOUT, Severity::MEDIUM, "SENSOR_TIMEOUT", "No sensor data from the simulator bridge; actuators commanded to zero" },
    { EventId::ESTIMATOR_INIT, Severity::INFO, "ESTIMATOR_INIT", "Attitude estimator initialised; aux 1 = from TRIAD, 2 = from the star tracker" },
    { EventId::ESTIMATOR_CONVERGED, Severity::INFO, "ESTIMATOR_CONVERGED", "Attitude estimator uncertainty fell below the pointing threshold" },
    { EventId::POINTING_STARTED, Severity::INFO, "POINTING_STARTED", "Nadir pointing control engaged" },
    { EventId::ESTIMATOR_RESET, Severity::MEDIUM, "ESTIMATOR_RESET", "Attitude estimator discarded after persistent large innovations" },
    { EventId::SENSOR_RESTORED, Severity::INFO, "SENSOR_RESTORED", "Sensor data resumed after a timeout" },
};
inline constexpr size_t kEventCount = 19;

inline const EventInfo* find_event(EventId id) {
    for (size_t i = 0; i < kEventCount; ++i) {
        if (kEvents[i].id == id) { return &kEvents[i]; }
    }
    return nullptr;
}

// --- On-board parameters, accessed through PUS ST[20] ----------------------
enum class ParamId : uint16_t {
    SYS_HK_PERIOD_MS = 1,
    ADCS_HK_PERIOD_MS = 2,
    EPS_HK_PERIOD_MS = 3,
    DETUMBLE_RATE_DPS = 4,
    POINTING_RATE_DPS = 5,
    BATT_LOW_SOC_PCT = 6,
    BATT_CRIT_SOC_PCT = 7,
    LINK_TIMEOUT_S = 8,
    BDOT_GAIN = 9,
    MTQ_MAX_DIPOLE = 10,
    BDOT_FILTER_TAU_S = 11,
    POINT_BANDWIDTH_RADPS = 12,
    POINT_MAX_SLEW_DPS = 13,
    MOMENTUM_DUMP_GAIN = 14,
};

enum class ParamType : uint8_t { U8, I8, U16, I16, U32, I32, U64, I64, F32, F64 };

struct ParamInfo {
    ParamId     id;
    ParamType   type;
    const char* name;
    double      default_value;
    double      min_value;
    double      max_value;
    const char* units;
    const char* description;
};

inline constexpr ParamInfo kParams[] = {
    { ParamId::SYS_HK_PERIOD_MS, ParamType::U32, "SYS_HK_PERIOD_MS", 1000.0, 100.0, 60000.0, "ms", "Generation period of SYS_HK" },
    { ParamId::ADCS_HK_PERIOD_MS, ParamType::U32, "ADCS_HK_PERIOD_MS", 1000.0, 100.0, 60000.0, "ms", "Generation period of ADCS_HK" },
    { ParamId::EPS_HK_PERIOD_MS, ParamType::U32, "EPS_HK_PERIOD_MS", 1000.0, 100.0, 60000.0, "ms", "Generation period of EPS_HK" },
    { ParamId::DETUMBLE_RATE_DPS, ParamType::F32, "DETUMBLE_RATE_DPS", 2.0, 0.1, 30.0, "deg/s", "Rate threshold above which detumble is commanded" },
    { ParamId::POINTING_RATE_DPS, ParamType::F32, "POINTING_RATE_DPS", 0.5, 0.01, 10.0, "deg/s", "Rate threshold below which pointing is permitted" },
    { ParamId::BATT_LOW_SOC_PCT, ParamType::F32, "BATT_LOW_SOC_PCT", 40.0, 5.0, 90.0, "%", "State of charge entering the LOW power state" },
    { ParamId::BATT_CRIT_SOC_PCT, ParamType::F32, "BATT_CRIT_SOC_PCT", 20.0, 2.0, 80.0, "%", "State of charge entering the CRITICAL power state" },
    { ParamId::LINK_TIMEOUT_S, ParamType::U32, "LINK_TIMEOUT_S", 300.0, 10.0, 86400.0, "s", "Ground contact loss timeout before autonomy reacts" },
    { ParamId::BDOT_GAIN, ParamType::F32, "BDOT_GAIN", 300000.0, 0.0, 10000000.0, "A*m^2/(T/s)", "B-dot proportional gain" },
    { ParamId::MTQ_MAX_DIPOLE, ParamType::F32, "MTQ_MAX_DIPOLE", 0.2, 0.0, 10.0, "A*m^2", "Largest magnetic dipole commanded on any axis" },
    { ParamId::BDOT_FILTER_TAU_S, ParamType::F32, "BDOT_FILTER_TAU_S", 3.0, 0.1, 60.0, "s", "Time constant of the filter applied to the field derivative" },
    { ParamId::POINT_BANDWIDTH_RADPS, ParamType::F32, "POINT_BANDWIDTH_RADPS", 0.1, 0.005, 1.0, "rad/s", "Natural frequency of the pointing control loop" },
    { ParamId::POINT_MAX_SLEW_DPS, ParamType::F32, "POINT_MAX_SLEW_DPS", 1.0, 0.05, 5.0, "deg/s", "Largest body rate the pointing controller will command while acquiring" },
    { ParamId::MOMENTUM_DUMP_GAIN, ParamType::F32, "MOMENTUM_DUMP_GAIN", 0.0005, 0.0, 0.1, "1/s", "Magnetic momentum-unloading gain" },
};
inline constexpr size_t kParamCount = 14;

inline const ParamInfo* find_param(ParamId id) {
    for (size_t i = 0; i < kParamCount; ++i) {
        if (kParams[i].id == id) { return &kParams[i]; }
    }
    return nullptr;
}

}  // namespace fsw::dict
