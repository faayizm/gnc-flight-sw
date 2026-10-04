// ============================================================================
//  fsw/apps/fdir/fdir_app.cpp
// ============================================================================
#include "apps/fdir/fdir_app.hpp"

#include <cstring>

namespace fsw::fdir {

namespace {
constexpr uint16_t kWheelsRail = 1u << static_cast<unsigned>(dict::PowerRail::WHEELS);
}

core::Status FdirApp::init() {
    core::Status s = bus_.subscribe(core::Topic::SensorData, &FdirApp::on_sensor, this);
    if (core::is_ok(s)) { s = bus_.subscribe(core::Topic::ActuatorCommand, &FdirApp::on_actuators, this); }
    if (core::is_ok(s)) { s = bus_.subscribe(core::Topic::PowerStatus, &FdirApp::on_power, this); }
    if (core::is_ok(s)) { s = bus_.subscribe(core::Topic::WheelRestore, &FdirApp::on_restore, this); }
    if (core::is_ok(s)) { s = bus_.subscribe(core::Topic::AdcsHk, &FdirApp::on_adcs_hk, this); }
    if (core::is_ok(s)) { s = bus_.subscribe(core::Topic::EpsHk, &FdirApp::on_eps_hk, this); }
    if (core::is_ok(s)) { s = bus_.subscribe(core::Topic::MonitorControl, &FdirApp::on_monitor_control, this); }
    return s;
}

void FdirApp::on_sensor(void* ctx, core::Topic, const uint8_t* data, size_t length) {
    if (length != sizeof(msg::SensorFrame)) { return; }
    msg::SensorFrame s;
    std::memcpy(&s, data, sizeof s);
    static_cast<FdirApp*>(ctx)->step(s);
}

void FdirApp::on_actuators(void* ctx, core::Topic, const uint8_t* data, size_t length) {
    if (length == sizeof(msg::ActuatorCommand)) {
        std::memcpy(&static_cast<FdirApp*>(ctx)->cmd_, data, length);
    }
}

void FdirApp::on_power(void* ctx, core::Topic, const uint8_t* data, size_t length) {
    if (length == sizeof(msg::PowerStatus)) {
        std::memcpy(&static_cast<FdirApp*>(ctx)->power_, data, length);
    }
}

void FdirApp::on_restore(void* ctx, core::Topic, const uint8_t* data, size_t length) {
    if (length != 1) { return; }
    auto* self = static_cast<FdirApp*>(ctx);
    self->ladder_.restore(data[0]);
    self->publish_health();
}

void FdirApp::on_adcs_hk(void* ctx, core::Topic, const uint8_t* data, size_t length) {
    if (length != sizeof(tlm::AdcsHk)) { return; }
    tlm::AdcsHk hk;
    std::memcpy(&hk, data, sizeof hk);
    static_cast<FdirApp*>(ctx)->monitor(dict::HkSid::ADCS_HK, &hk);
}

void FdirApp::on_eps_hk(void* ctx, core::Topic, const uint8_t* data, size_t length) {
    if (length != sizeof(tlm::EpsHk)) { return; }
    tlm::EpsHk hk;
    std::memcpy(&hk, data, sizeof hk);
    static_cast<FdirApp*>(ctx)->monitor(dict::HkSid::EPS_HK, &hk);
}

void FdirApp::on_monitor_control(void* ctx, core::Topic, const uint8_t* data, size_t length) {
    if (length != sizeof(msg::MonitorControl)) { return; }
    msg::MonitorControl c;
    std::memcpy(&c, data, sizeof c);
    static_cast<FdirApp*>(ctx)->monitoring_.set_enabled(c.id, c.on);
}

void FdirApp::monitor(dict::HkSid sid, const void* hk) {
    MonitorTransition t[Monitoring::kCount];
    const size_t n = monitoring_.check(sid, hk, t);
    for (size_t i = 0; i < n; ++i) {
        if (t[i].to == dict::MonitorStatus::BELOW || t[i].to == dict::MonitorStatus::ABOVE) {
            const tlm::MonitorDef* def = Monitoring::find(t[i].id);
            if (def != nullptr) {
                events_.raise(def->event, (static_cast<uint32_t>(t[i].id) << 8) |
                                          static_cast<uint32_t>(t[i].to));
            }
        }
        msg::MonitorReport r;
        r.id = t[i].id;
        r.from = static_cast<uint8_t>(t[i].from);
        r.to = static_cast<uint8_t>(t[i].to);
        r.value = t[i].value;
        r.limit = t[i].limit;
        bus_.publish_object(core::Topic::MonitorReport, r);
    }
}

void FdirApp::upset(uint8_t target, uint32_t bit) {
    switch (target) {
        case 0: if (params_ != nullptr) { params_->flip(bit); } break;
        case 1: if (mode_ != nullptr) { mode_->flip(bit); } break;
        case 2: ladder_.isolation_store().flip(bit); break;
        default: break;
    }
}

void FdirApp::scrub() {
    if (params_ == nullptr) { return; }
    const core::ScrubReport r = params_->scrub();
    if (r.uncorrectable == 0) { return; }
    // The word now holds its default, which is safe but may not be what the
    // ground chose. The last saved table is the better answer, if it is good.
    events_.raise(dict::EventId::EDAC_UNCORRECTABLE, static_cast<uint32_t>(r.first_bad));
    const bool reloaded = reload_fn_ != nullptr && reload_fn_(reload_ctx_);
    events_.raise(dict::EventId::PARAMS_RELOADED, reloaded ? 1u : 0u);
}

void FdirApp::step(const msg::SensorFrame& s) {
    // Scrub on the sensor clock, not a timer, so a run is reproducible:
    // whether a second upset finds the first one still there must not depend
    // on how busy the host was.
    if (++samples_ % kScrubEverySamples == 0) { scrub(); }

    // Judge the period that has just ended against what was commanded for it...
    const double h[3] = {static_cast<double>(s.wheel_h.x), static_cast<double>(s.wheel_h.y),
                         static_cast<double>(s.wheel_h.z)};
    const uint8_t failing = check_.sample(s.time_s, h, s.wheels_valid, ladder_.isolated(), check_cfg_);
    const uint32_t good[3] = {check_.good_windows(0), check_.good_windows(1), check_.good_windows(2)};

    const LadderOutput out = ladder_.step(s.time_s, failing, good);
    for (int i = 0; i < out.event_count; ++i) { events_.raise(out.events[i].id, out.events[i].aux); }
    if (out.reset_check) { check_.reset(); }

    const uint16_t off = out.wheels_off ? kWheelsRail : 0;
    if (off != rails_.off) {
        rails_.off = off;
        bus_.publish_object(core::Topic::FdirRails, rails_);
    }
    if (ladder_.usable() != health_.usable || static_cast<uint8_t>(ladder_.state()) != health_.state) {
        publish_health();
    }

    // ...then note what is commanded for the period starting now. Torque only
    // reaches a wheel if the drives are powered and ADCS actually commanded it.
    const bool powered = power_.valid ? (power_.rails & kWheelsRail) != 0 && off == 0 : off == 0;
    const double t[3] = {static_cast<double>(cmd_.wheel_torque_nm.x),
                         static_cast<double>(cmd_.wheel_torque_nm.y),
                         static_cast<double>(cmd_.wheel_torque_nm.z)};
    const double zero[3] = {0.0, 0.0, 0.0};
    check_.commanded(cmd_.wheels_commanded ? t : zero, powered);

    hk_.wheel_state   = static_cast<uint8_t>(ladder_.state());
    hk_.wheels_usable = ladder_.usable();
    hk_.wheel_retries = static_cast<uint8_t>(ladder_.retries(0) + ladder_.retries(1) + ladder_.retries(2));
    hk_.wheel_resid_x = static_cast<float>(check_.residual(0));
    hk_.wheel_resid_y = static_cast<float>(check_.residual(1));
    hk_.wheel_resid_z = static_cast<float>(check_.residual(2));
    hk_.edac_corrected     = params_ != nullptr ? params_->edac_corrected() : 0;
    hk_.edac_uncorrectable = static_cast<uint16_t>(params_ != nullptr ? params_->edac_uncorrectable() : 0);
    hk_.tmr_repairs = static_cast<uint16_t>((mode_ != nullptr ? mode_->repairs() : 0) +
                                            ladder_.isolation_store().repairs());
    hk_.monitors_enabled = monitoring_.enabled_count();
    hk_.monitors_alarm   = monitoring_.alarm_count();
    monitor(dict::HkSid::FDIR_HK, &hk_);
    bus_.publish_object(core::Topic::FdirHk, hk_);
}

void FdirApp::publish_health() {
    health_.usable = ladder_.usable();
    health_.state  = static_cast<uint8_t>(ladder_.state());
    bus_.publish_object(core::Topic::WheelHealth, health_);
}

}  // namespace fsw::fdir
