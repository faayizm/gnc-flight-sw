// ============================================================================
//  fsw/apps/adcs/adcs_app.hpp -- attitude determination and control.
//
//  Phase 2 scope: detumble only. The application reads magnetometer and gyro
//  samples from the simulator bridge, runs the B-dot law while the body is
//  spinning faster than it should, and answers every sample with a magnetorquer
//  dipole demand. It publishes ADCS_HK on the software bus; TT&C downlinks it
//  without knowing what is in it.
//
//  ENGAGE / RELEASE. Detumble engages above DETUMBLE_RATE_DPS and releases
//  below 80% of POINTING_RATE_DPS -- the rate under which pointing is permitted;
//  the margin covers gyro bias and noise. The gap between the two is the
//  hysteresis: without it, a body hovering near one threshold would switch the
//  controller on and off with every noisy gyro sample. The application starts
//  engaged, because after separation the safe assumption is that the satellite
//  is tumbling.
//
//  FAILURE. If the sensor stream stops, the dipole is commanded to zero and an
//  event is raised. A controller that keeps applying its last demand to a
//  satellite it can no longer observe is how a small fault becomes a large one.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

#include "apps/adcs/bdot.hpp"
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
};

class AdcsApp {
 public:
    AdcsApp(hal::ILink& bridge_link, hal::IClock& clock, core::Bus& bus,
            core::EventLog& events, const core::ParamStore& params)
        : bridge_(bridge_link), clock_(clock), bus_(bus),
          events_(events), params_(params) {}

    // 50 Hz: service the bridge, run the control law on any new sample, and
    // check for a stalled sensor stream. Runs fast so reply latency to the
    // simulator is at most one base tick.
    static void task_run(void* context);

    bool     detumbling()    const { return detumble_active_; }
    bool     sensors_ok()    const { return sensors_ok_; }
    uint32_t samples()       const { return samples_; }
    const tlm::AdcsHk& hk()  const { return hk_; }

    // Process one sample. Public so unit tests can drive the control logic
    // with no link and no scheduler.
    ActuatorFrame step(const SensorFrame& s);

 private:
    void update_detumble_state(double rate_dps);
    void publish(const Vec3f& dipole, const Vec3f& b);

    static constexpr double kSensorTimeoutS = 2.0;
    static constexpr double kReleaseMargin  = 0.8;

    SimBridge             bridge_;
    hal::IClock&          clock_;
    core::Bus&            bus_;
    core::EventLog&       events_;
    const core::ParamStore& params_;
    BdotController        bdot_;
    tlm::AdcsHk           hk_{};

    bool     detumble_active_ = true;
    bool     sensors_ok_      = false;
    bool     ever_had_data_   = false;
    double   last_rx_s_       = 0.0;
    double   last_rate_dps_   = 0.0;
    uint32_t samples_         = 0;
};

}  // namespace fsw::adcs
