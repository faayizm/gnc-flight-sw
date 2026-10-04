// ============================================================================
//  fsw/apps/adcs/adcs_app.hpp -- attitude determination and control.
//
//  One sample from the simulator bridge drives one pass through the chain:
//
//    sensors --> orbit (GPS, or propagated) --> references (Sun, IGRF)
//            --> estimator (star tracker or TRIAD to start, then MEKF fusing
//                gyro, star tracker, magnetometer and sun sensor) --> modes
//            --> control law (B-dot, or nadir pointing with momentum dumping)
//            --> actuators (magnetorquers, reaction wheels)
//
//  MODES. ADCS does not decide what the spacecraft is doing; the mode
//  manager does, and ADCS follows (Topic::ModeChanged):
//
//    BOOT, STANDBY   no control. The estimator keeps running, so the
//                    attitude is known the moment it is needed.
//    DETUMBLE, SAFE  B-dot on the magnetorquers. In SAFE the wheels are
//                    unpowered anyway; B-dot needs only the magnetometer.
//    POINTING        nadir pointing on the wheels, momentum dumped by the
//                    magnetorquers.
//
//  What ADCS does decide, and publishes in Topic::AdcsStatus, is what it
//  knows: the body rate, whether the estimator has converged, whether a
//  position is available. The mode manager decides what to do about it.
//
//  FAILURE. Sensor data arrives from the I/O application on Topic::SensorData.
//  If it stops, the I/O application zeroes the actuators itself; ADCS sees
//  frames with every validity flag clear and drops its derivative history.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

#include "apps/adcs/adcs_math.hpp"
#include "apps/adcs/bdot.hpp"
#include "apps/adcs/mekf.hpp"
#include "apps/adcs/orbit_prop.hpp"
#include "apps/adcs/pointing.hpp"
#include "apps/messages.hpp"
#include "core/bus.hpp"
#include "core/event_log.hpp"
#include "core/param_store.hpp"
#include "generated/telemetry.hpp"

namespace fsw::adcs {

class AdcsApp {
 public:
    AdcsApp(core::Bus& bus, core::EventLog& events, const core::ParamStore& params)
        : bus_(bus), events_(events), params_(params) {}

    // Subscribe to sensor data and mode changes. Called once at startup.
    core::Status init();

    dict::AdcsCtrlMode mode()   const { return ctrl_mode_; }
    void set_system_mode(dict::SystemMode m) { system_mode_ = m; }
    dict::AdcsEstState est()    const { return est_state_; }
    uint32_t samples()          const { return samples_; }
    const tlm::AdcsHk& hk()     const { return hk_; }
    const Mekf& estimator()     const { return mekf_; }
    const OrbitPropagator& orbit() const { return orbit_; }

    // Process one sample. Public so unit tests can drive the chain with no
    // bus traffic and no scheduler.
    msg::ActuatorCommand step(const msg::SensorFrame& s);

 private:
    static void on_sensor(void* ctx, core::Topic, const uint8_t* data, size_t length);
    static void on_mode(void* ctx, core::Topic, const uint8_t* data, size_t length);
    static void on_wheels(void* ctx, core::Topic, const uint8_t* data, size_t length);

    void run_orbit(const msg::SensorFrame& s);
    void run_estimator(const msg::SensorFrame& s, double dt);
    void publish(const msg::ActuatorCommand& out, const Vec3& body_torque);

    static constexpr double kMagSigmaFloor    = 2.0e-3;   // rad
    // The coarse sun sensor's error is dominated by bias (albedo, face
    // misalignment, clipping), not noise, so it is weighted as though it were
    // twice as noisy as it is. That keeps it from dragging a star-tracker
    // solution, while still being usable alone.
    static constexpr double kSunSigma         = 2.5e-2;   // rad
    // Star tracker: 10 arcsec across the boresight (body X, Y), 60 about it (Z).
    static constexpr Vec3   kStarSigma{4.85e-5, 4.85e-5, 2.9e-4};
    static constexpr double kConvergedSigma   = 0.05 * 3.14159265358979 / 180.0;
    static constexpr double kLostSigma        = 1.0 * 3.14159265358979 / 180.0;
    static constexpr double kBadInnovation    = 10.0 * 3.14159265358979 / 180.0;
    static constexpr uint32_t kBadInnovationLimit = 100;  // samples, 10 s at 10 Hz
    static constexpr double kGpsStaleS        = 2.0;
    static constexpr double kStarCoastS       = 300.0;

    core::Bus&              bus_;
    core::EventLog&         events_;
    const core::ParamStore& params_;

    BdotController   bdot_;
    Mekf             mekf_;
    MekfConfig       mekf_cfg_;
    OrbitPropagator  orbit_;
    tlm::AdcsHk      hk_{};

    dict::SystemMode   system_mode_ = dict::SystemMode::BOOT;
    dict::AdcsCtrlMode ctrl_mode_   = dict::AdcsCtrlMode::IDLE;
    dict::AdcsEstState est_state_   = dict::AdcsEstState::INVALID;

    bool     have_last_t_     = false;
    double   last_sample_t_   = 0.0;
    double   est_init_t_      = 0.0;
    double   last_gps_t_      = -1e9;
    double   last_star_t_     = -1e9;
    double   last_rate_dps_   = 0.0;
    Vec3     last_gyro_{};
    uint8_t  wheels_usable_   = 0x7;      // from FDIR
    uint32_t bad_innovations_ = 0;
    uint32_t samples_         = 0;
};

}  // namespace fsw::adcs
