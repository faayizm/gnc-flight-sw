// ============================================================================
//  fsw/apps/eps/eps_app.cpp
// ============================================================================
#include "apps/eps/eps_app.hpp"

#include <cstring>

namespace fsw::eps {

core::Status EpsApp::init() {
    core::Status s = bus_.subscribe(core::Topic::SensorData, &EpsApp::on_sensor, this);
    if (core::is_ok(s)) { s = bus_.subscribe(core::Topic::ModeChanged, &EpsApp::on_mode, this); }
    if (core::is_ok(s)) { s = bus_.subscribe(core::Topic::UplinkActivity, &EpsApp::on_uplink, this); }
    if (core::is_ok(s)) { s = bus_.subscribe(core::Topic::RailRequest, &EpsApp::on_rail, this); }
    if (core::is_ok(s)) { s = bus_.subscribe(core::Topic::FdirRails, &EpsApp::on_fdir, this); }
    return s;
}

void EpsApp::on_sensor(void* ctx, core::Topic, const uint8_t* data, size_t length) {
    if (length != sizeof(msg::SensorFrame)) { return; }
    msg::SensorFrame s;
    std::memcpy(&s, data, sizeof s);
    static_cast<EpsApp*>(ctx)->step(s);
}

void EpsApp::on_mode(void* ctx, core::Topic, const uint8_t* data, size_t length) {
    if (length != sizeof(msg::ModeChange)) { return; }
    msg::ModeChange m;
    std::memcpy(&m, data, sizeof m);
    static_cast<EpsApp*>(ctx)->mode_ = static_cast<dict::SystemMode>(m.to);
}

void EpsApp::on_uplink(void* ctx, core::Topic, const uint8_t*, size_t) {
    static_cast<EpsApp*>(ctx)->note_uplink();
}

void EpsApp::on_rail(void* ctx, core::Topic, const uint8_t* data, size_t length) {
    if (length != sizeof(msg::RailRequest)) { return; }
    auto* self = static_cast<EpsApp*>(ctx);
    msg::RailRequest r;
    std::memcpy(&r, data, sizeof r);
    if (r.rail > 7) { return; }
    const auto bit = static_cast<uint16_t>(1u << r.rail);
    self->ground_rails_ = r.on ? static_cast<uint16_t>(self->ground_rails_ | bit)
                               : static_cast<uint16_t>(self->ground_rails_ & ~bit);
    self->events_.raise(dict::EventId::RAIL_SWITCHED, static_cast<uint32_t>((r.rail << 8) | (r.on ? 1 : 0)));
}

void EpsApp::on_fdir(void* ctx, core::Topic, const uint8_t* data, size_t length) {
    if (length != sizeof(msg::FdirRails)) { return; }
    msg::FdirRails r;
    std::memcpy(&r, data, sizeof r);
    // Never the rails that keep the spacecraft alive and listening.
    static_cast<EpsApp*>(ctx)->fdir_off_ = static_cast<uint16_t>(r.off & ~kProtectedRails);
}

void EpsApp::step(const msg::SensorFrame& s) {
    if (!s.eps_valid) { return; }

    double dt = 0.0;
    if (have_t_) {
        dt = s.time_s - last_t_;
        if (dt < 0.0 || dt > 5.0) { dt = 0.0; }
    }
    last_t_ = s.time_s;
    have_t_ = true;

    const double cap  = params_.get_f64(dict::ParamId::BATT_CAPACITY_WH);
    const double low  = params_.get_f64(dict::ParamId::BATT_LOW_SOC_PCT);
    const double crit = params_.get_f64(dict::ParamId::BATT_CRIT_SOC_PCT);
    const double soc  = soc_.update(static_cast<double>(s.batt_v), static_cast<double>(s.batt_i), dt, cap);

    const dict::PowerState next = next_power_state(state_, soc, low, crit);
    if (next != state_) {
        events_.raise(dict::EventId::POWER_STATE_CHANGED,
                      static_cast<uint32_t>((static_cast<unsigned>(state_) << 8) | static_cast<unsigned>(next)));
        state_ = next;
    }
    const uint8_t level = shed_level(state_, soc, low, crit, level_, mode_ == dict::SystemMode::SAFE);
    if (level != level_) {
        events_.raise(dict::EventId::LOAD_SHED, level);
        level_ = level;
    }
    const bool tx_hold = s.time_s - last_uplink_s_ < kTxHoldS;
    rails_ = static_cast<uint16_t>(rail_policy(mode_, level_, tx_hold, ground_rails_) & ~fdir_off_);

    msg::PowerStatus ps;
    ps.power_state = static_cast<uint8_t>(state_);
    ps.shed_level  = level_;
    ps.rails       = rails_;
    ps.soc_pct     = static_cast<float>(soc);
    ps.valid       = true;
    bus_.publish_object(core::Topic::PowerStatus, ps);

    hk_.power_state   = static_cast<uint8_t>(state_);
    hk_.batt_voltage  = s.batt_v;
    hk_.batt_current  = s.batt_i;
    hk_.batt_soc_pct  = static_cast<float>(soc);
    hk_.batt_temp_c   = s.batt_temp_c;
    hk_.solar_power_w = s.solar_w;
    hk_.load_power_w  = s.load_w;
    hk_.rails_enabled = rails_;
    hk_.shed_level    = level_;
    bus_.publish_object(core::Topic::EpsHk, hk_);
}

}  // namespace fsw::eps
