// ============================================================================
//  fsw/apps/adcs/adcs_app.cpp
// ============================================================================
#include "apps/adcs/adcs_app.hpp"

#include <cmath>

namespace fsw::adcs {

namespace {
constexpr double kRadToDeg = 57.29577951308232;
}

void AdcsApp::task_run(void* context) {
    auto* self = static_cast<AdcsApp*>(context);

    SensorFrame s;
    if (self->bridge_.poll(s)) {
        self->last_rx_s_ = self->clock_.mission_time_s();
        self->ever_had_data_ = true;
        if (!self->sensors_ok_) {
            self->sensors_ok_ = true;
            if (self->samples_ > 0) { self->events_.raise(dict::EventId::SENSOR_RESTORED); }
        }
        const ActuatorFrame reply = self->step(s);
        self->bridge_.send(reply);
        return;
    }

    if (self->ever_had_data_ && self->sensors_ok_ &&
        self->clock_.mission_time_s() - self->last_rx_s_ > kSensorTimeoutS) {
        self->sensors_ok_ = false;
        self->bdot_.reset();
        self->hk_.mag_valid = 0;
        self->hk_.torque_cmd_x = self->hk_.torque_cmd_y = self->hk_.torque_cmd_z = 0.0f;
        self->events_.raise(dict::EventId::SENSOR_TIMEOUT);
        self->publish(Vec3f{}, Vec3f{});
    }
}

void AdcsApp::update_detumble_state(double rate_dps) {
    const double engage  = params_.get_f64(dict::ParamId::DETUMBLE_RATE_DPS);
    // Release with margin below the pointing threshold: the gyro carries bias
    // and noise, so releasing at exactly 0.5 would hand over a body that is
    // really turning at 0.52.
    const double release = kReleaseMargin * params_.get_f64(dict::ParamId::POINTING_RATE_DPS);

    if (!detumble_active_ && rate_dps > engage) {
        detumble_active_ = true;
        events_.raise(dict::EventId::DETUMBLE_STARTED);
    } else if (detumble_active_ && rate_dps < release) {
        detumble_active_ = false;
        events_.raise(dict::EventId::DETUMBLE_COMPLETE);
    }
}

ActuatorFrame AdcsApp::step(const SensorFrame& s) {
    ++samples_;

    BdotConfig cfg;
    cfg.gain_a_m2_per_t_s = params_.get_f32(dict::ParamId::BDOT_GAIN);
    cfg.max_dipole_a_m2   = params_.get_f32(dict::ParamId::MTQ_MAX_DIPOLE);
    cfg.filter_tau_s      = params_.get_f32(dict::ParamId::BDOT_FILTER_TAU_S);

    if (s.gyro_valid) {
        const double wx = static_cast<double>(s.gyro_rps.x);
        const double wy = static_cast<double>(s.gyro_rps.y);
        const double wz = static_cast<double>(s.gyro_rps.z);
        last_rate_dps_ = std::sqrt(wx * wx + wy * wy + wz * wz) * kRadToDeg;
        update_detumble_state(last_rate_dps_);
    }

    Vec3f dipole{};
    if (s.mag_valid) {
        // Always feed the filter, so its history is warm when detumble is
        // re-engaged; only the output is gated.
        const Vec3f m = bdot_.update(s.mag_t, s.time_s, cfg);
        if (detumble_active_) { dipole = m; }
    } else {
        bdot_.reset();
    }

    hk_.mag_valid = s.mag_valid ? 1 : 0;
    // Sun sensor validity is reported; the vector is not used until Phase 3.
    // `eclipse` stays 0: knowing it needs an on-board orbit, which also waits
    // for Phase 3. The sun sensor going blind is a symptom, not the same fact.
    hk_.sun_valid = s.sun_valid ? 1 : 0;
    hk_.omega_x = s.gyro_rps.x;
    hk_.omega_y = s.gyro_rps.y;
    hk_.omega_z = s.gyro_rps.z;
    hk_.rate_norm = static_cast<float>(last_rate_dps_);
    publish(dipole, s.mag_t);

    ActuatorFrame reply;
    reply.seq = s.seq;
    reply.dipole_a_m2 = dipole;
    reply.commanded = detumble_active_;
    return reply;
}

void AdcsApp::publish(const Vec3f& m, const Vec3f& b) {
    // Torque the command should produce, tau = m x B. Reported as what was
    // *commanded*, using the measured field -- the controller has no access to
    // the true one.
    hk_.torque_cmd_x = m.y * b.z - m.z * b.y;
    hk_.torque_cmd_y = m.z * b.x - m.x * b.z;
    hk_.torque_cmd_z = m.x * b.y - m.y * b.x;

    hk_.est_state = static_cast<uint8_t>(dict::AdcsEstState::INVALID);
    hk_.q_est_0 = 1.0f;  // no estimator yet: identity, flagged INVALID above

    const ActuatorCommandMsg cmd{{m.x, m.y, m.z}};
    bus_.publish_object(core::Topic::ActuatorCommand, cmd);
    bus_.publish_object(core::Topic::AdcsHk, hk_);
}

}  // namespace fsw::adcs
