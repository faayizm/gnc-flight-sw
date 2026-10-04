// ============================================================================
//  fsw/apps/modemgr/mode_manager.cpp
// ============================================================================
#include "apps/modemgr/mode_manager.hpp"

#include <cstring>

namespace fsw::modemgr {

core::Status ModeManager::init() {
    core::Status s = bus_.subscribe(core::Topic::ModeRequest, &ModeManager::on_request, this);
    if (core::is_ok(s)) { s = bus_.subscribe(core::Topic::AdcsStatus, &ModeManager::on_adcs, this); }
    if (core::is_ok(s)) { s = bus_.subscribe(core::Topic::PowerStatus, &ModeManager::on_power, this); }
    if (core::is_ok(s)) { s = bus_.subscribe(core::Topic::UplinkActivity, &ModeManager::on_uplink, this); }
    if (core::is_ok(s)) { s = bus_.subscribe(core::Topic::WheelHealth, &ModeManager::on_wheels, this); }
    return s;
}

void ModeManager::on_wheels(void* ctx, core::Topic, const uint8_t* data, size_t length) {
    if (length == sizeof(msg::WheelHealth)) {
        std::memcpy(&static_cast<ModeManager*>(ctx)->wheels_, data, length);
    }
}

void ModeManager::on_request(void* ctx, core::Topic, const uint8_t* data, size_t length) {
    if (length != 1) { return; }
    static_cast<ModeManager*>(ctx)->request(static_cast<dict::SystemMode>(data[0]));
}

void ModeManager::on_adcs(void* ctx, core::Topic, const uint8_t* data, size_t length) {
    auto* self = static_cast<ModeManager*>(ctx);
    if (length == sizeof(msg::AdcsStatus)) {
        std::memcpy(&self->adcs_, data, length);
        self->have_adcs_ = self->have_adcs_ || self->adcs_.rate_valid;
        // EPS reports after ADCS for the same sample; wait for it, unless
        // there is no EPS data to wait for.
        if (!self->power_.valid) { self->evaluate(); }
    }
}

void ModeManager::on_power(void* ctx, core::Topic, const uint8_t* data, size_t length) {
    auto* self = static_cast<ModeManager*>(ctx);
    if (length == sizeof(msg::PowerStatus)) {
        std::memcpy(&self->power_, data, length);
        self->evaluate();
    }
}

void ModeManager::on_uplink(void* ctx, core::Topic, const uint8_t*, size_t) {
    auto* self = static_cast<ModeManager*>(ctx);
    self->last_contact_ = self->clock_.now();
}

Facts ModeManager::facts() {
    Facts f;
    f.have_rates    = have_adcs_ && adcs_.rate_valid;
    f.rate_dps      = static_cast<double>(adcs_.rate_dps);
    f.attitude_ok   = adcs_.est_state == static_cast<uint8_t>(dict::AdcsEstState::CONVERGED);
    f.attitude_lost = adcs_.est_state == static_cast<uint8_t>(dict::AdcsEstState::INVALID);
    f.orbit_ok      = adcs_.orbit_valid;
    f.power         = power_.valid ? static_cast<dict::PowerState>(power_.power_state)
                                   : dict::PowerState::UNKNOWN;
    f.since_contact_s = (clock_.now() - last_contact_).to_double_seconds();
    f.wheels = ((wheels_.usable >> 0) & 1) + ((wheels_.usable >> 1) & 1) + ((wheels_.usable >> 2) & 1);
    return f;
}

Limits ModeManager::limits() const {
    Limits l;
    l.engage_dps     = params_.get_f64(dict::ParamId::DETUMBLE_RATE_DPS);
    l.release_dps    = 0.8 * params_.get_f64(dict::ParamId::POINTING_RATE_DPS);
    l.link_timeout_s = params_.get_f64(dict::ParamId::LINK_TIMEOUT_S);
    return l;
}

void ModeManager::change_to(dict::SystemMode to, dict::SafeReason why) {
    const dict::SystemMode from = mode_.get();
    if (to == from) { return; }
    mode_.set(to);
    events_.raise(dict::EventId::MODE_CHANGED,
                  static_cast<uint32_t>((static_cast<unsigned>(from) << 8) | static_cast<unsigned>(to)));
    if (to == dict::SystemMode::SAFE) {
        events_.raise(dict::EventId::SAFE_MODE_ENTERED, static_cast<uint32_t>(why));
    }
    const msg::ModeChange m{static_cast<uint8_t>(from), static_cast<uint8_t>(to)};
    bus_.publish_object(core::Topic::ModeChanged, m);
}

void ModeManager::task_run(void* context) {
    // Only the rule that must work with no sensors at all runs on the clock:
    // a spacecraft that cannot hear the ground goes SAFE whether or not
    // anything else is working.
    auto* self = static_cast<ModeManager*>(context);
    self->start_contact_timer();
    if (self->mode_.get() != dict::SystemMode::SAFE &&
        (self->clock_.now() - self->last_contact_).to_double_seconds() > self->limits().link_timeout_s) {
        self->change_to(dict::SystemMode::SAFE, dict::SafeReason::NO_CONTACT);
    }
}

void ModeManager::start_contact_timer() {
    if (!started_) {
        // The contact timer starts at boot: a spacecraft that has never heard
        // the ground has, as far as autonomy is concerned, lost it.
        last_contact_ = clock_.now();
        started_ = true;
    }
}

void ModeManager::evaluate() {
    // Called once per sensor sample, so every decision lands on the same
    // sample however fast the host runs -- which keeps scenarios reproducible.
    start_contact_timer();
    const Decision d = autonomous(mode_.get(), facts(), limits());
    change_to(d.mode, d.safe_reason);
}

void ModeManager::request(dict::SystemMode to) {
    if (static_cast<uint8_t>(to) > static_cast<uint8_t>(dict::SystemMode::POINTING)) {
        events_.raise(dict::EventId::MODE_REFUSED,
                      static_cast<uint32_t>((static_cast<unsigned>(to) << 8) |
                                            static_cast<unsigned>(dict::ModeRefusal::INVALID)));
        return;
    }
    const dict::ModeRefusal why = judge_request(mode_.get(), to, facts(), limits());
    if (why != dict::ModeRefusal::NONE) {
        events_.raise(dict::EventId::MODE_REFUSED,
                      static_cast<uint32_t>((static_cast<unsigned>(to) << 8) | static_cast<unsigned>(why)));
        return;
    }
    change_to(to, dict::SafeReason::GROUND);
}

}  // namespace fsw::modemgr
