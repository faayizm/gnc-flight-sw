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
//  MODES (AdcsCtrlMode, reported in ADCS_HK):
//
//    DETUMBLE   B-dot on the magnetorquers. Entered at boot, and from any mode
//               if the body rate exceeds DETUMBLE_RATE_DPS.
//    STANDBY    Rates are low; actuators off while the estimator converges.
//               Entered when the rate falls below 80% of POINTING_RATE_DPS.
//    POINTING   Nadir pointing on the wheels, momentum dumped by the
//               magnetorquers. Entered from STANDBY once the estimator reports
//               CONVERGED and a position is known.
//
//  Every threshold has a gap between its entry and exit values, so a noisy
//  signal near one threshold cannot make the controller chatter.
//
//  These are the ADCS's own sub-modes. The spacecraft-level mode in SYS_HK
//  belongs to the mode manager, which arrives in Phase 5 and will command
//  these rather than let ADCS decide alone.
//
//  FAILURE. If the sensor stream stops, every actuator is commanded to zero
//  and an event is raised. A controller that keeps applying its last demand to
//  a satellite it can no longer observe is how a small fault becomes a large
//  one.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

#include "apps/adcs/adcs_math.hpp"
#include "apps/adcs/bdot.hpp"
#include "apps/adcs/mekf.hpp"
#include "apps/adcs/orbit_prop.hpp"
#include "apps/adcs/pointing.hpp"
#include "apps/adcs/sim_bridge.hpp"
#include "core/bus.hpp"
#include "core/event_log.hpp"
#include "core/param_store.hpp"
#include "generated/telemetry.hpp"
#include "hal/clock.hpp"

namespace fsw::adcs {

// Payload of Topic::ActuatorCommand.
struct ActuatorCommandMsg {
    float dipole_a_m2[3];
    float wheel_torque_nm[3];
};

class AdcsApp {
 public:
    AdcsApp(hal::ILink& bridge_link, hal::IClock& clock, core::Bus& bus,
            core::EventLog& events, const core::ParamStore& params)
        : bridge_(bridge_link), clock_(clock), bus_(bus),
          events_(events), params_(params) {}

    // 50 Hz: service the bridge, run the chain on any new sample, and check
    // for a stalled sensor stream. Runs fast so reply latency to the
    // simulator is at most one base tick.
    static void task_run(void* context);

    dict::AdcsCtrlMode mode()   const { return mode_; }
    dict::AdcsEstState est()    const { return est_state_; }
    bool     sensors_ok()       const { return sensors_ok_; }
    uint32_t samples()          const { return samples_; }
    const tlm::AdcsHk& hk()     const { return hk_; }
    const Mekf& estimator()     const { return mekf_; }
    const OrbitPropagator& orbit() const { return orbit_; }

    // Process one sample. Public so unit tests can drive the chain with no
    // link and no scheduler.
    ActuatorFrame step(const SensorFrame& s);

 private:
    void run_orbit(const SensorFrame& s);
    void run_estimator(const SensorFrame& s, double dt);
    void run_modes(double rate_dps);
    void set_mode(dict::AdcsCtrlMode m);
    void publish(const ActuatorFrame& out, const Vec3& body_torque);

    static constexpr double kSensorTimeoutS   = 2.0;
    static constexpr double kReleaseMargin    = 0.8;
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

    SimBridge               bridge_;
    hal::IClock&            clock_;
    core::Bus&              bus_;
    core::EventLog&         events_;
    const core::ParamStore& params_;

    BdotController   bdot_;
    Mekf             mekf_;
    MekfConfig       mekf_cfg_;
    OrbitPropagator  orbit_;
    tlm::AdcsHk      hk_{};

    dict::AdcsCtrlMode mode_      = dict::AdcsCtrlMode::DETUMBLE;
    dict::AdcsEstState est_state_ = dict::AdcsEstState::INVALID;

    bool     sensors_ok_      = false;
    bool     ever_had_data_   = false;
    bool     have_last_t_     = false;
    double   last_rx_s_       = 0.0;
    double   last_sample_t_   = 0.0;
    double   est_init_t_      = 0.0;
    double   last_gps_t_      = -1e9;
    double   last_star_t_     = -1e9;
    double   last_rate_dps_   = 0.0;
    uint32_t bad_innovations_ = 0;
    uint32_t samples_         = 0;
};

}  // namespace fsw::adcs
