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

namespace fsw::tlm {

// Core system health, scheduler timing and link statistics.
// PUS ST[3,25] report, structure id 1, APID 0x001, nominal rate 1 Hz.
struct SysHk {
    uint32_t uptime_s{};  // Seconds since boot [s]
    uint32_t tick_count{};  // Scheduler base ticks executed [ticks]
    uint8_t mode{};  // Current spacecraft mode
    uint16_t boot_count{};  // Power-on / reset counter [count]
    uint8_t cpu_load_pct{};  // Measured scheduler occupancy [%]
    uint16_t sched_overruns{};  // Rate-group deadline misses [count]
    uint32_t tc_received{};  // Telecommands accepted [count]
    uint32_t tc_rejected{};  // Telecommands rejected [count]
    uint32_t tm_sent{};  // Telemetry packets downlinked [count]
    uint8_t link_up{};  // Ground link connected [bool]
    uint32_t events_logged{};  // Events raised since boot [count]
    uint16_t last_event_id{};  // Identifier of most recent event [id]

    static constexpr dict::HkSid kSid  = dict::HkSid::SYS_HK;
    static constexpr dict::Apid  kApid = dict::Apid::TTC;
    static constexpr uint16_t kPayloadBytes = 33;
    static constexpr uint16_t kPacketBytes  = 55;

    // Serialises the field block only. The ST[3,25] structure id and the
    // packet headers are written by the telemetry builder.
    bool serialize(core::ByteWriter& w) const {
        return true
            && w.write_uint32(uptime_s)
            && w.write_uint32(tick_count)
            && w.write_uint8(mode)
            && w.write_uint16(boot_count)
            && w.write_uint8(cpu_load_pct)
            && w.write_uint16(sched_overruns)
            && w.write_uint32(tc_received)
            && w.write_uint32(tc_rejected)
            && w.write_uint32(tm_sent)
            && w.write_uint8(link_up)
            && w.write_uint32(events_logged)
            && w.write_uint16(last_event_id)
            ;
    }

    bool deserialize(core::ByteReader& r) {
        return true
            && r.read_uint32(uptime_s)
            && r.read_uint32(tick_count)
            && r.read_uint8(mode)
            && r.read_uint16(boot_count)
            && r.read_uint8(cpu_load_pct)
            && r.read_uint16(sched_overruns)
            && r.read_uint32(tc_received)
            && r.read_uint32(tc_rejected)
            && r.read_uint32(tm_sent)
            && r.read_uint8(link_up)
            && r.read_uint32(events_logged)
            && r.read_uint16(last_event_id)
            ;
    }
};
static_assert(sizeof(SysHk) > 0, "SysHk must be instantiable");

// Attitude determination and control state. Populated from Phase 2 onward.
// PUS ST[3,25] report, structure id 2, APID 0x002, nominal rate 1 Hz.
struct AdcsHk {
    uint8_t est_state{};  // Estimator convergence state
    float q_est_0{};  // Estimated attitude quaternion scalar part
    float q_est_1{};  // Estimated attitude quaternion x
    float q_est_2{};  // Estimated attitude quaternion y
    float q_est_3{};  // Estimated attitude quaternion z
    float omega_x{};  // Bias-corrected body rate about X [rad/s]
    float omega_y{};  // Bias-corrected body rate about Y [rad/s]
    float omega_z{};  // Bias-corrected body rate about Z [rad/s]
    float gyro_bias_x{};  // Estimated gyro bias X [rad/s]
    float gyro_bias_y{};  // Estimated gyro bias Y [rad/s]
    float gyro_bias_z{};  // Estimated gyro bias Z [rad/s]
    float pointing_err_deg{};  // Angle between body and reference pointing axis [deg]
    float rate_norm{};  // Magnitude of the body rate vector [deg/s]
    uint8_t sun_valid{};  // Sun sensor reference is usable [bool]
    uint8_t mag_valid{};  // Magnetometer reference is usable [bool]
    uint8_t eclipse{};  // Spacecraft is in Earth shadow [bool]
    float torque_cmd_x{};  // Commanded control torque X [N*m]
    float torque_cmd_y{};  // Commanded control torque Y [N*m]
    float torque_cmd_z{};  // Commanded control torque Z [N*m]
    double pos_eci_x{};  // On-board estimated position ECI X [m]
    double pos_eci_y{};  // On-board estimated position ECI Y [m]
    double pos_eci_z{};  // On-board estimated position ECI Z [m]

    static constexpr dict::HkSid kSid  = dict::HkSid::ADCS_HK;
    static constexpr dict::Apid  kApid = dict::Apid::ADCS;
    static constexpr uint16_t kPayloadBytes = 88;
    static constexpr uint16_t kPacketBytes  = 110;

    // Serialises the field block only. The ST[3,25] structure id and the
    // packet headers are written by the telemetry builder.
    bool serialize(core::ByteWriter& w) const {
        return true
            && w.write_uint8(est_state)
            && w.write_float32(q_est_0)
            && w.write_float32(q_est_1)
            && w.write_float32(q_est_2)
            && w.write_float32(q_est_3)
            && w.write_float32(omega_x)
            && w.write_float32(omega_y)
            && w.write_float32(omega_z)
            && w.write_float32(gyro_bias_x)
            && w.write_float32(gyro_bias_y)
            && w.write_float32(gyro_bias_z)
            && w.write_float32(pointing_err_deg)
            && w.write_float32(rate_norm)
            && w.write_uint8(sun_valid)
            && w.write_uint8(mag_valid)
            && w.write_uint8(eclipse)
            && w.write_float32(torque_cmd_x)
            && w.write_float32(torque_cmd_y)
            && w.write_float32(torque_cmd_z)
            && w.write_float64(pos_eci_x)
            && w.write_float64(pos_eci_y)
            && w.write_float64(pos_eci_z)
            ;
    }

    bool deserialize(core::ByteReader& r) {
        return true
            && r.read_uint8(est_state)
            && r.read_float32(q_est_0)
            && r.read_float32(q_est_1)
            && r.read_float32(q_est_2)
            && r.read_float32(q_est_3)
            && r.read_float32(omega_x)
            && r.read_float32(omega_y)
            && r.read_float32(omega_z)
            && r.read_float32(gyro_bias_x)
            && r.read_float32(gyro_bias_y)
            && r.read_float32(gyro_bias_z)
            && r.read_float32(pointing_err_deg)
            && r.read_float32(rate_norm)
            && r.read_uint8(sun_valid)
            && r.read_uint8(mag_valid)
            && r.read_uint8(eclipse)
            && r.read_float32(torque_cmd_x)
            && r.read_float32(torque_cmd_y)
            && r.read_float32(torque_cmd_z)
            && r.read_float64(pos_eci_x)
            && r.read_float64(pos_eci_y)
            && r.read_float64(pos_eci_z)
            ;
    }
};
static_assert(sizeof(AdcsHk) > 0, "AdcsHk must be instantiable");

// Power subsystem state. Populated from Phase 5 onward.
// PUS ST[3,25] report, structure id 3, APID 0x003, nominal rate 1 Hz.
struct EpsHk {
    uint8_t power_state{};  // Coarse battery state
    float batt_voltage{};  // Battery bus voltage [V]
    float batt_current{};  // Battery current [A]
    float batt_soc_pct{};  // State of charge [%]
    float batt_temp_c{};  // Battery temperature [degC]
    float solar_power_w{};  // Total array power generation [W]
    float load_power_w{};  // Total bus load [W]
    uint16_t rails_enabled{};  // Bitmask of enabled power rails [mask]
    uint8_t shed_level{};  // Active load-shedding level [level]

    static constexpr dict::HkSid kSid  = dict::HkSid::EPS_HK;
    static constexpr dict::Apid  kApid = dict::Apid::EPS;
    static constexpr uint16_t kPayloadBytes = 28;
    static constexpr uint16_t kPacketBytes  = 50;

    // Serialises the field block only. The ST[3,25] structure id and the
    // packet headers are written by the telemetry builder.
    bool serialize(core::ByteWriter& w) const {
        return true
            && w.write_uint8(power_state)
            && w.write_float32(batt_voltage)
            && w.write_float32(batt_current)
            && w.write_float32(batt_soc_pct)
            && w.write_float32(batt_temp_c)
            && w.write_float32(solar_power_w)
            && w.write_float32(load_power_w)
            && w.write_uint16(rails_enabled)
            && w.write_uint8(shed_level)
            ;
    }

    bool deserialize(core::ByteReader& r) {
        return true
            && r.read_uint8(power_state)
            && r.read_float32(batt_voltage)
            && r.read_float32(batt_current)
            && r.read_float32(batt_soc_pct)
            && r.read_float32(batt_temp_c)
            && r.read_float32(solar_power_w)
            && r.read_float32(load_power_w)
            && r.read_uint16(rails_enabled)
            && r.read_uint8(shed_level)
            ;
    }
};
static_assert(sizeof(EpsHk) > 0, "EpsHk must be instantiable");

}  // namespace fsw::tlm
