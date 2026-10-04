// ============================================================================
//  fsw/apps/messages.hpp -- what travels on the software bus between apps.
//
//  Applications never include each other's headers; they include this one.
//  Every payload is a plain struct, copied by value, valid only during the
//  delivery call -- see core/bus.hpp.
//
//      Topic            Publisher   Payload           Subscribers
//      ---------------  ----------  ----------------  ---------------------
//      SensorData       io          SensorFrame       adcs, eps
//      ActuatorCommand  adcs        ActuatorCommand   io
//      PowerStatus      eps         PowerStatus       io, modemgr, ttc
//      AdcsStatus       adcs        AdcsStatus        modemgr
//      ModeRequest      ttc         uint8_t (mode)    modemgr
//      ModeChanged      modemgr     ModeChange        adcs, eps, ttc
//      UplinkActivity   ttc         (none)            eps, modemgr
//      RailRequest      ttc         RailRequest       eps
//      WheelHealth      fdir        WheelHealth       adcs, modemgr
//      FdirRails        fdir        FdirRails         eps
//      WheelRestore     ttc         uint8_t (mask)    fdir
//      MonitorControl   ttc         MonitorControl    fdir
//      MonitorReport    fdir        MonitorReport     ttc
//      AdcsHk / EpsHk / FdirHk      tlm::...Hk        ttc
// ============================================================================
#pragma once

#include <cstdint>

namespace fsw::msg {

struct Vec3f {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

// One sample of everything the spacecraft can measure, as delivered by the
// hardware (in this build, the simulator bridge).
struct SensorFrame {
    uint32_t seq        = 0;
    double   time_s     = 0.0;
    Vec3f    mag_t{};
    Vec3f    gyro_rps{};
    Vec3f    sun_b{};
    Vec3f    wheel_h{};
    double   gps_pos[3] = {0.0, 0.0, 0.0};
    double   gps_vel[3] = {0.0, 0.0, 0.0};
    float    star_q[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    // Electrical power system
    float    batt_v      = 0.0f;   // V
    float    batt_i      = 0.0f;   // A, positive when charging
    float    solar_w     = 0.0f;   // W generated
    float    load_w      = 0.0f;   // W consumed by the bus
    float    batt_temp_c = 0.0f;
    uint16_t rails_actual = 0;     // which rails the power switches report on
    bool     mag_valid   = false;
    bool     gyro_valid  = false;
    bool     sun_valid   = false;
    bool     gps_valid   = false;
    bool     wheels_valid = false;
    bool     star_valid  = false;
    bool     eps_valid   = false;
};

struct ActuatorCommand {
    Vec3f dipole_a_m2{};
    Vec3f wheel_torque_nm{};
    bool  mtq_commanded    = false;
    bool  wheels_commanded = false;
};

struct AdcsStatus {
    float   rate_dps    = 0.0f;
    bool    rate_valid  = false;
    uint8_t est_state   = 0;        // dict::AdcsEstState
    bool    orbit_valid = false;
};

struct PowerStatus {
    uint8_t  power_state = 0;       // dict::PowerState
    uint8_t  shed_level  = 0;
    uint16_t rails       = 0xFFFF;  // rails that should be on
    float    soc_pct     = 0.0f;
    bool     valid       = false;
};

struct RailRequest {
    uint8_t rail = 0;               // dict::PowerRail
    bool    on   = false;
};

// Which reaction wheels attitude control may use. Published by FDIR whenever
// it changes; until the first one arrives, every wheel is usable.
struct WheelHealth {
    uint8_t usable = 0x7;           // bit 0 = X
    uint8_t state  = 0;             // dict::FdirWheelState
};

// Rails FDIR needs switched off regardless of policy or ground wishes, e.g.
// for a power-cycle retry. EPS applies it on top of everything else.
struct FdirRails {
    uint16_t off = 0;               // bit per dict::PowerRail
};

// ST[12,1] / ST[12,2], carried from TT&C to FDIR.
struct MonitorControl {
    uint8_t id = 0;
    bool    on = false;
};

// One ST[12] status change, for TT&C to downlink as ST[12,12].
struct MonitorReport {
    uint8_t id   = 0;
    uint8_t from = 0;               // dict::MonitorStatus
    uint8_t to   = 0;
    double  value = 0.0;
    double  limit = 0.0;
};

struct ModeChange {
    uint8_t from = 0;               // dict::SystemMode
    uint8_t to   = 0;
};

}  // namespace fsw::msg
