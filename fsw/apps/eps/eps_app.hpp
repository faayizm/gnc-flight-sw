// ============================================================================
//  fsw/apps/eps/eps_app.hpp -- the electrical power system application.
//
//  Reads battery and array telemetry from Topic::SensorData, estimates state
//  of charge, decides the power state and how much load to shed, and from
//  that and the spacecraft mode works out which rails should be on. The
//  rules are in power_policy.hpp; this class only wires them to the bus.
//
//  Ground requests to switch a rail (ST[8,3]) set what the ground *wants*;
//  the policy can still overrule it. The payload switched on by an operator
//  goes off anyway when the battery runs low -- and comes back by itself when
//  it recovers, because the operator's wish is remembered, not overwritten.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

#include "apps/eps/power_policy.hpp"
#include "apps/messages.hpp"
#include "core/bus.hpp"
#include "core/event_log.hpp"
#include "core/param_store.hpp"
#include "generated/telemetry.hpp"

namespace fsw::eps {

class EpsApp {
 public:
    EpsApp(core::Bus& bus, core::EventLog& events, const core::ParamStore& params)
        : bus_(bus), events_(events), params_(params) {}

    core::Status init();

    // One sample of power telemetry. Public for unit tests.
    void step(const msg::SensorFrame& s);

    dict::PowerState power_state() const { return state_; }
    uint8_t  shed()      const { return level_; }
    uint16_t rails()     const { return rails_; }
    double   soc()       const { return soc_.soc(); }
    void set_mode(dict::SystemMode m) { mode_ = m; }
    // Uplink activity is stamped with the time of the latest sensor sample,
    // not the wall clock: every timing decision EPS makes is on the same
    // timeline as the battery it measures. (In the simulator, the two clocks
    // differ by whatever factor the host happens to run at.)
    void note_uplink() { last_uplink_s_ = last_t_; }

    // How long the transmitter stays on after the ground is last heard, when
    // shedding has switched it to "between passes only".
    static constexpr double kTxHoldS = 600.0;

 private:
    static void on_sensor(void* ctx, core::Topic, const uint8_t* data, size_t length);
    static void on_mode(void* ctx, core::Topic, const uint8_t* data, size_t length);
    static void on_uplink(void* ctx, core::Topic, const uint8_t*, size_t);
    static void on_rail(void* ctx, core::Topic, const uint8_t* data, size_t length);

    core::Bus&              bus_;
    core::EventLog&         events_;
    const core::ParamStore& params_;

    SocEstimator     soc_;
    dict::PowerState state_ = dict::PowerState::UNKNOWN;
    dict::SystemMode mode_  = dict::SystemMode::BOOT;
    uint8_t  level_ = 0;
    uint16_t ground_rails_ = kDefaultGroundRails;
    uint16_t rails_ = kDefaultGroundRails;
    bool     have_t_ = false;
    double   last_t_ = 0.0;
    double   last_uplink_s_ = -1e12;
    tlm::EpsHk hk_{};
};

}  // namespace fsw::eps
