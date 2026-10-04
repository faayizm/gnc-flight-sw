// ============================================================================
//  fsw/apps/modemgr/mode_manager.hpp -- the single authority on what the
//  spacecraft is doing.
//
//  Listens to what ADCS knows (Topic::AdcsStatus), what EPS knows
//  (Topic::PowerStatus), when the ground was last heard
//  (Topic::UplinkActivity), and what the ground asks for (Topic::ModeRequest).
//  Decides the mode by the rules in mode_logic.hpp, and announces every change
//  on Topic::ModeChanged and as a MODE_CHANGED event with the old and new
//  modes -- the mode history is often the only record of what a spacecraft
//  was thinking.
//
//  Autonomous rules are evaluated at 10 Hz in a task of their own. Ground
//  requests are judged the moment they arrive, so the refusal (MODE_REFUSED,
//  with the reason) reaches the ground in the same telemetry burst as the
//  command's verification report.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

#include "apps/messages.hpp"
#include "apps/modemgr/mode_logic.hpp"
#include "core/bus.hpp"
#include "core/event_log.hpp"
#include "core/param_store.hpp"
#include "hal/clock.hpp"

namespace fsw::modemgr {

class ModeManager {
 public:
    ModeManager(hal::IClock& clock, core::Bus& bus, core::EventLog& events, const core::ParamStore& params)
        : clock_(clock), bus_(bus), events_(events), params_(params) {}

    core::Status init();
    static void task_run(void* context);

    dict::SystemMode mode() const { return mode_; }

    // Exposed for tests.
    void evaluate();
    void request(dict::SystemMode to);

 private:
    static void on_request(void* ctx, core::Topic, const uint8_t* data, size_t length);
    static void on_adcs(void* ctx, core::Topic, const uint8_t* data, size_t length);
    static void on_power(void* ctx, core::Topic, const uint8_t* data, size_t length);
    static void on_uplink(void* ctx, core::Topic, const uint8_t*, size_t);

    Facts  facts();
    Limits limits() const;
    void   change_to(dict::SystemMode to, dict::SafeReason why);

    hal::IClock&            clock_;
    core::Bus&              bus_;
    core::EventLog&         events_;
    const core::ParamStore& params_;

    dict::SystemMode   mode_ = dict::SystemMode::BOOT;
    msg::AdcsStatus    adcs_{};
    msg::PowerStatus   power_{};
    bool               have_adcs_ = false;
    double             last_contact_s_ = 0.0;
    bool               started_ = false;
};

}  // namespace fsw::modemgr
