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
    FDIR = 0x004,
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

// Switched power rails, by bit number in rails_enabled. OBC, RX and SURVIVAL_HEATERS can never be switched off.
enum class PowerRail : uint8_t {
    OBC = 0,
    RX = 1,
    TX = 2,
    ADCS = 3,
    WHEELS = 4,
    PAYLOAD = 5,
    OPS_HEATERS = 6,
    SURVIVAL_HEATERS = 7,
};
constexpr const char* to_string(PowerRail v) {
    switch (v) {
        case PowerRail::OBC: return "OBC";
        case PowerRail::RX: return "RX";
        case PowerRail::TX: return "TX";
        case PowerRail::ADCS: return "ADCS";
        case PowerRail::WHEELS: return "WHEELS";
        case PowerRail::PAYLOAD: return "PAYLOAD";
        case PowerRail::OPS_HEATERS: return "OPS_HEATERS";
        case PowerRail::SURVIVAL_HEATERS: return "SURVIVAL_HEATERS";
    }
    return "UNKNOWN";
}

// Why the mode manager refused a ground mode request (MODE_REFUSED aux, low byte).
enum class ModeRefusal : uint8_t {
    NONE = 0,
    RATES_HIGH = 1,
    ATTITUDE_UNKNOWN = 2,
    POWER = 3,
    NOT_FROM_MODE = 4,
    INVALID = 5,
    WHEELS = 6,
};
constexpr const char* to_string(ModeRefusal v) {
    switch (v) {
        case ModeRefusal::NONE: return "NONE";
        case ModeRefusal::RATES_HIGH: return "RATES_HIGH";
        case ModeRefusal::ATTITUDE_UNKNOWN: return "ATTITUDE_UNKNOWN";
        case ModeRefusal::POWER: return "POWER";
        case ModeRefusal::NOT_FROM_MODE: return "NOT_FROM_MODE";
        case ModeRefusal::INVALID: return "INVALID";
        case ModeRefusal::WHEELS: return "WHEELS";
    }
    return "UNKNOWN";
}

// Why the spacecraft entered SAFE (SAFE_MODE_ENTERED aux).
enum class SafeReason : uint8_t {
    GROUND = 0,
    POWER_CRITICAL = 1,
    NO_CONTACT = 2,
    ACTUATORS = 3,
};
constexpr const char* to_string(SafeReason v) {
    switch (v) {
        case SafeReason::GROUND: return "GROUND";
        case SafeReason::POWER_CRITICAL: return "POWER_CRITICAL";
        case SafeReason::NO_CONTACT: return "NO_CONTACT";
        case SafeReason::ACTUATORS: return "ACTUATORS";
    }
    return "UNKNOWN";
}

// Sensors the I/O application screens (SENSOR_REJECTED / SENSOR_READMITTED aux).
enum class SensorId : uint8_t {
    MAG = 0,
    GYRO = 1,
    SUN = 2,
    STAR = 3,
    GPS = 4,
};
constexpr const char* to_string(SensorId v) {
    switch (v) {
        case SensorId::MAG: return "MAG";
        case SensorId::GYRO: return "GYRO";
        case SensorId::SUN: return "SUN";
        case SensorId::STAR: return "STAR";
        case SensorId::GPS: return "GPS";
    }
    return "UNKNOWN";
}

// Why a sensor reading was refused at the hardware boundary.
enum class SensorFault : uint8_t {
    NONE = 0,
    RANGE = 1,
    FROZEN = 2,
};
constexpr const char* to_string(SensorFault v) {
    switch (v) {
        case SensorFault::NONE: return "NONE";
        case SensorFault::RANGE: return "RANGE";
        case SensorFault::FROZEN: return "FROZEN";
    }
    return "UNKNOWN";
}

// Where the reaction-wheel recovery ladder is (fdir/wheel_ladder.hpp).
enum class FdirWheelState : uint8_t {
    MONITOR = 0,
    CYCLE_OFF = 1,
    VERIFY = 2,
};
constexpr const char* to_string(FdirWheelState v) {
    switch (v) {
        case FdirWheelState::MONITOR: return "MONITOR";
        case FdirWheelState::CYCLE_OFF: return "CYCLE_OFF";
        case FdirWheelState::VERIFY: return "VERIFY";
    }
    return "UNKNOWN";
}

// Why the flight computer last started (BOOT_COMPLETE aux, SYS_HK last_reset).
enum class ResetCause : uint8_t {
    POWER_ON = 0,
    WATCHDOG = 1,
};
constexpr const char* to_string(ResetCause v) {
    switch (v) {
        case ResetCause::POWER_ON: return "POWER_ON";
        case ResetCause::WATCHDOG: return "WATCHDOG";
    }
    return "UNKNOWN";
}

// PUS ST[12] checking status of one monitored parameter.
enum class MonitorStatus : uint8_t {
    UNCHECKED = 0,
    WITHIN = 1,
    BELOW = 2,
    ABOVE = 3,
};
constexpr const char* to_string(MonitorStatus v) {
    switch (v) {
        case MonitorStatus::UNCHECKED: return "UNCHECKED";
        case MonitorStatus::WITHIN: return "WITHIN";
        case MonitorStatus::BELOW: return "BELOW";
        case MonitorStatus::ABOVE: return "ABOVE";
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
    EPS_HK = 3,   // Power subsystem state.
    FDIR_HK = 4,   // Fault management state.
};
inline constexpr size_t kHkStructureCount = 4;

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
    SENSOR_TIMEOUT = 14,
    ESTIMATOR_INIT = 16,
    ESTIMATOR_CONVERGED = 17,
    ESTIMATOR_RESET = 19,
    TIME_ADJUSTED = 20,
    SCHED_RELEASED = 21,
    PLAYBACK_STARTED = 22,
    PLAYBACK_DONE = 23,
    STORE_WRAPPED = 24,
    POWER_STATE_CHANGED = 25,
    LOAD_SHED = 26,
    RAIL_SWITCHED = 27,
    SENSOR_RESTORED = 15,
    SENSOR_REJECTED = 12,
    WHEEL_FAULT = 28,
    WHEEL_POWER_CYCLE = 29,
    WHEEL_RECOVERED = 30,
    WHEEL_ISOLATED = 31,
    BATT_VOLTAGE_ALARM = 32,
    BATT_TEMP_ALARM = 33,
    RATE_ALARM = 34,
    POINTING_LOST = 35,
    WHEEL_MOMENTUM_HIGH = 36,
    EVENT_ACTION = 37,
    EDAC_UNCORRECTABLE = 38,
    PARAMS_RELOADED = 39,
    SENSOR_GAP = 18,
    SENSOR_READMITTED = 13,
};

struct EventInfo {
    EventId     id;
    Severity    severity;
    const char* name;
    const char* description;
};

inline constexpr EventInfo kEvents[] = {
    { EventId::BOOT_COMPLETE, Severity::INFO, "BOOT_COMPLETE", "Flight software finished initialisation; aux = ResetCause" },
    { EventId::MODE_CHANGED, Severity::INFO, "MODE_CHANGED", "Spacecraft mode transition executed; aux = old mode << 8 | new mode" },
    { EventId::LINK_CONNECTED, Severity::INFO, "LINK_CONNECTED", "Ground link established" },
    { EventId::LINK_LOST, Severity::LOW, "LINK_LOST", "Ground link dropped" },
    { EventId::TC_REJECTED, Severity::LOW, "TC_REJECTED", "Telecommand failed acceptance checks" },
    { EventId::HK_ENABLED, Severity::INFO, "HK_ENABLED", "Housekeeping structure generation enabled" },
    { EventId::HK_DISABLED, Severity::INFO, "HK_DISABLED", "Housekeeping structure generation disabled" },
    { EventId::PARAM_SET, Severity::INFO, "PARAM_SET", "On-board parameter modified from ground" },
    { EventId::SCHED_OVERRUN, Severity::MEDIUM, "SCHED_OVERRUN", "A rate group missed its deadline" },
    { EventId::MODE_REFUSED, Severity::LOW, "MODE_REFUSED", "Requested mode transition was refused; aux = requested mode << 8 | ModeRefusal" },
    { EventId::SAFE_MODE_ENTERED, Severity::HIGH, "SAFE_MODE_ENTERED", "Spacecraft entered safe mode; aux = SafeReason" },
    { EventId::SENSOR_TIMEOUT, Severity::MEDIUM, "SENSOR_TIMEOUT", "No sensor data from the simulator bridge; actuators commanded to zero" },
    { EventId::ESTIMATOR_INIT, Severity::INFO, "ESTIMATOR_INIT", "Attitude estimator initialised; aux 1 = from TRIAD, 2 = from the star tracker" },
    { EventId::ESTIMATOR_CONVERGED, Severity::INFO, "ESTIMATOR_CONVERGED", "Attitude estimator uncertainty fell below the pointing threshold" },
    { EventId::ESTIMATOR_RESET, Severity::MEDIUM, "ESTIMATOR_RESET", "Attitude estimator discarded after persistent large innovations" },
    { EventId::TIME_ADJUSTED, Severity::INFO, "TIME_ADJUSTED", "On-board time corrected from the ground; aux = correction in whole seconds, two's complement" },
    { EventId::SCHED_RELEASED, Severity::INFO, "SCHED_RELEASED", "A time-tagged telecommand was released; aux = its packet sequence count" },
    { EventId::PLAYBACK_STARTED, Severity::INFO, "PLAYBACK_STARTED", "Packet store retrieval began; aux = packets selected" },
    { EventId::PLAYBACK_DONE, Severity::INFO, "PLAYBACK_DONE", "Packet store retrieval finished; aux = packets replayed" },
    { EventId::STORE_WRAPPED, Severity::LOW, "STORE_WRAPPED", "The packet store filled and began overwriting its oldest packets" },
    { EventId::POWER_STATE_CHANGED, Severity::MEDIUM, "POWER_STATE_CHANGED", "Battery power state changed; aux = old << 8 | new (PowerState)" },
    { EventId::LOAD_SHED, Severity::MEDIUM, "LOAD_SHED", "Load-shedding level changed; aux = new level (0 = everything restored)" },
    { EventId::RAIL_SWITCHED, Severity::INFO, "RAIL_SWITCHED", "A power rail was switched by ground command; aux = rail << 8 | on" },
    { EventId::SENSOR_RESTORED, Severity::INFO, "SENSOR_RESTORED", "Sensor data resumed after a timeout" },
    { EventId::SENSOR_REJECTED, Severity::MEDIUM, "SENSOR_REJECTED", "A sensor failed its range or frozen-value check and is ignored; aux = SensorId << 8 | SensorFault" },
    { EventId::WHEEL_FAULT, Severity::MEDIUM, "WHEEL_FAULT", "A reaction wheel is not delivering its commanded torque; aux = wheel mask, bit 0 = X" },
    { EventId::WHEEL_POWER_CYCLE, Severity::INFO, "WHEEL_POWER_CYCLE", "Wheel drives switched off and on to clear a possible latch-up; aux = wheels under test" },
    { EventId::WHEEL_RECOVERED, Severity::INFO, "WHEEL_RECOVERED", "A wheel works again after its power cycle; aux = wheel mask" },
    { EventId::WHEEL_ISOLATED, Severity::HIGH, "WHEEL_ISOLATED", "A wheel still failed after its retry and is out of service; aux = wheel mask" },
    { EventId::BATT_VOLTAGE_ALARM, Severity::MEDIUM, "BATT_VOLTAGE_ALARM", "Battery voltage outside its monitoring limits; aux = monitor id << 8 | MonitorStatus" },
    { EventId::BATT_TEMP_ALARM, Severity::MEDIUM, "BATT_TEMP_ALARM", "Battery temperature outside its monitoring limits; aux = monitor id << 8 | MonitorStatus" },
    { EventId::RATE_ALARM, Severity::MEDIUM, "RATE_ALARM", "Body rate above its monitoring limit; aux = monitor id << 8 | MonitorStatus" },
    { EventId::POINTING_LOST, Severity::HIGH, "POINTING_LOST", "Pointing error above its limit for five minutes; aux = monitor id << 8 | MonitorStatus" },
    { EventId::WHEEL_MOMENTUM_HIGH, Severity::MEDIUM, "WHEEL_MOMENTUM_HIGH", "A reaction wheel is storing more than two thirds of its capacity; aux = monitor id << 8 | MonitorStatus" },
    { EventId::EVENT_ACTION, Severity::INFO, "EVENT_ACTION", "An on-board action ran in response to an event; aux = the triggering event id" },
    { EventId::EDAC_UNCORRECTABLE, Severity::HIGH, "EDAC_UNCORRECTABLE", "A parameter word has two flipped bits and cannot be corrected; it reads as its default until reloaded; aux = parameter id" },
    { EventId::PARAMS_RELOADED, Severity::MEDIUM, "PARAMS_RELOADED", "The parameter table was reloaded after an uncorrectable error; aux = 1 from non-volatile storage, 0 kept the defaults" },
    { EventId::SENSOR_GAP, Severity::MEDIUM, "SENSOR_GAP", "Sensor samples resumed after a gap in their own timestamps; aux = gap in milliseconds" },
    { EventId::SENSOR_READMITTED, Severity::INFO, "SENSOR_READMITTED", "A rejected sensor passed its checks again for long enough to be trusted; aux = SensorId" },
};
inline constexpr size_t kEventCount = 39;

inline const EventInfo* find_event(EventId id) {
    for (size_t i = 0; i < kEventCount; ++i) {
        if (kEvents[i].id == id) { return &kEvents[i]; }
    }
    return nullptr;
}

// --- Event-action definitions, PUS ST[19] ----------------------------------
// The telecommand each event triggers, stored exactly as it would be uplinked.
struct EventActionDef {
    EventId     event;
    uint8_t     service;
    uint8_t     subtype;
    uint8_t     args[16];
    uint8_t     arg_bytes;
    const char* description;
};

inline constexpr EventActionDef kEventActions[] = {
    { EventId::POINTING_LOST, 8, 1, {0x01}, 1, "SET_MODE mode=SAFE: Pointing has been lost for five minutes and nothing below has recovered it: stop trying, go SAFE, wait for the ground" },
};
inline constexpr size_t kEventActionCount = 1;

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
    FDIR_HK_PERIOD_MS = 16,
    BATT_CAPACITY_WH = 15,
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
    { ParamId::LINK_TIMEOUT_S, ParamType::U32, "LINK_TIMEOUT_S", 86400.0, 600.0, 604800.0, "s", "Time without hearing the ground before the spacecraft enters SAFE" },
    { ParamId::BDOT_GAIN, ParamType::F32, "BDOT_GAIN", 300000.0, 0.0, 10000000.0, "A*m^2/(T/s)", "B-dot proportional gain" },
    { ParamId::MTQ_MAX_DIPOLE, ParamType::F32, "MTQ_MAX_DIPOLE", 0.2, 0.0, 10.0, "A*m^2", "Largest magnetic dipole commanded on any axis" },
    { ParamId::BDOT_FILTER_TAU_S, ParamType::F32, "BDOT_FILTER_TAU_S", 3.0, 0.1, 60.0, "s", "Time constant of the filter applied to the field derivative" },
    { ParamId::POINT_BANDWIDTH_RADPS, ParamType::F32, "POINT_BANDWIDTH_RADPS", 0.1, 0.005, 1.0, "rad/s", "Natural frequency of the pointing control loop" },
    { ParamId::POINT_MAX_SLEW_DPS, ParamType::F32, "POINT_MAX_SLEW_DPS", 1.0, 0.05, 5.0, "deg/s", "Largest body rate the pointing controller will command while acquiring" },
    { ParamId::MOMENTUM_DUMP_GAIN, ParamType::F32, "MOMENTUM_DUMP_GAIN", 0.0005, 0.0, 0.1, "1/s", "Magnetic momentum-unloading gain" },
    { ParamId::FDIR_HK_PERIOD_MS, ParamType::U32, "FDIR_HK_PERIOD_MS", 1000.0, 100.0, 60000.0, "ms", "Generation period of FDIR_HK" },
    { ParamId::BATT_CAPACITY_WH, ParamType::F32, "BATT_CAPACITY_WH", 30.0, 1.0, 1000.0, "W*h", "Usable battery energy at 100% state of charge" },
};
inline constexpr size_t kParamCount = 16;

inline const ParamInfo* find_param(ParamId id) {
    for (size_t i = 0; i < kParamCount; ++i) {
        if (kParams[i].id == id) { return &kParams[i]; }
    }
    return nullptr;
}

}  // namespace fsw::dict
