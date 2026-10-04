// ============================================================================
//  fsw/apps/adcs/adcs_app.cpp
// ============================================================================
#include "apps/adcs/adcs_app.hpp"

#include <cmath>
#include <cstring>

#include "apps/adcs/ephemeris.hpp"
#include "apps/adcs/triad.hpp"

namespace fsw::adcs {

namespace {
constexpr double kRadToDeg = 57.29577951308232;

Vec3  to_d(const Vec3f& v) {
    return {static_cast<double>(v.x), static_cast<double>(v.y), static_cast<double>(v.z)};
}
Vec3f to_f(const Vec3& v) {
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}
}  // namespace

core::Status AdcsApp::init() {
    core::Status st = bus_.subscribe(core::Topic::SensorData, &AdcsApp::on_sensor, this);
    if (!core::is_ok(st)) { return st; }
    st = bus_.subscribe(core::Topic::WheelHealth, &AdcsApp::on_wheels, this);
    if (!core::is_ok(st)) { return st; }
    return bus_.subscribe(core::Topic::ModeChanged, &AdcsApp::on_mode, this);
}

void AdcsApp::on_sensor(void* ctx, core::Topic, const uint8_t* data, size_t length) {
    if (length != sizeof(msg::SensorFrame)) { return; }
    msg::SensorFrame s;
    std::memcpy(&s, data, sizeof s);
    static_cast<AdcsApp*>(ctx)->step(s);
}

void AdcsApp::on_mode(void* ctx, core::Topic, const uint8_t* data, size_t length) {
    if (length != sizeof(msg::ModeChange)) { return; }
    msg::ModeChange m;
    std::memcpy(&m, data, sizeof m);
    static_cast<AdcsApp*>(ctx)->system_mode_ = static_cast<dict::SystemMode>(m.to);
}

void AdcsApp::on_wheels(void* ctx, core::Topic, const uint8_t* data, size_t length) {
    if (length != sizeof(msg::WheelHealth)) { return; }
    msg::WheelHealth w;
    std::memcpy(&w, data, sizeof w);
    static_cast<AdcsApp*>(ctx)->wheels_usable_ = w.usable;
}

void AdcsApp::run_orbit(const msg::SensorFrame& s) {
    if (s.gps_valid) {
        orbit_.set_state(Vec3{s.gps_pos[0], s.gps_pos[1], s.gps_pos[2]},
                         Vec3{s.gps_vel[0], s.gps_vel[1], s.gps_vel[2]}, s.time_s);
        last_gps_t_ = s.time_s;
    } else {
        orbit_.propagate_to(s.time_s);
    }
}

void AdcsApp::run_estimator(const msg::SensorFrame& s, double dt) {
    // Without a gyro reading the attitude still moves. Coasting on the last
    // good reading is far better than freezing the estimate while the
    // spacecraft turns underneath it; the star tracker corrects what drifts.
    if (s.gyro_valid) { last_gyro_ = to_d(s.gyro_rps); }
    if (mekf_.initialised() && dt > 0.0) {
        mekf_.propagate(last_gyro_, dt, mekf_cfg_);
    }
    if (!orbit_.valid()) { return; }

    const Vec3 b_ref   = ephem::magnetic_field(orbit_.position(), s.time_s);
    const Vec3 sun_ref = ephem::sun_direction(s.time_s);
    const Vec3 mag     = to_d(s.mag_t);

    const Quat star{static_cast<double>(s.star_q[0]), static_cast<double>(s.star_q[1]),
                    static_cast<double>(s.star_q[2]), static_cast<double>(s.star_q[3])};

    if (!mekf_.initialised()) {
        // The star tracker is the better start when it has a solution, but it
        // cannot work while the body is still turning fast; TRIAD can.
        Quat q;
        uint32_t source = 0;
        if (s.star_valid) {
            q = star;
            source = 2;
        } else if (s.mag_valid && s.sun_valid && triad(mag, to_d(s.sun_b), b_ref, sun_ref, q)) {
            source = 1;
        }
        if (source != 0) {
            mekf_.init(q, (source == 2 ? 0.05 : 3.0) / kRadToDeg, 1.0e-3);
            est_init_t_ = s.time_s;
            bad_innovations_ = 0;
            est_state_ = dict::AdcsEstState::INITIALISING;
            events_.raise(dict::EventId::ESTIMATOR_INIT, source);
        }
        return;
    }

    double worst = 0.0;
    if (s.star_valid) {
        const double inn = mekf_.update_attitude(star, kStarSigma);
        worst = inn > worst ? inn : worst;
        last_star_t_ = s.time_s;
    }
    if (s.mag_valid) {
        // Noise is fixed in tesla, so as an angle it grows where the field is weak.
        double sigma = 60e-9 / norm(mag);
        if (sigma < kMagSigmaFloor) { sigma = kMagSigmaFloor; }
        const double inn = mekf_.update(mag, b_ref, sigma);
        worst = inn > worst ? inn : worst;
    }
    // The coarse sun sensor's error is bias, not noise, so averaging does not
    // remove it: fed into a filter that has a star tracker, it only pulls the
    // estimate off. It is used for acquisition, and when the tracker has been
    // out long enough that the gyro alone would drift further than the sun
    // sensor's bias.
    const bool star_recent = (s.time_s - last_star_t_) < kStarCoastS;
    if (s.sun_valid && !star_recent) {
        const double inn = mekf_.update(to_d(s.sun_b), sun_ref, kSunSigma);
        worst = inn > worst ? inn : worst;
    }

    // A filter that disagrees with every measurement for ten seconds has lost
    // track. Throw it away and start again from TRIAD rather than let it
    // drag the controller somewhere wrong.
    bad_innovations_ = (worst > kBadInnovation) ? bad_innovations_ + 1 : 0;
    if (bad_innovations_ > kBadInnovationLimit) {
        mekf_.reset();
        est_state_ = dict::AdcsEstState::INVALID;
        events_.raise(dict::EventId::ESTIMATOR_RESET);
        return;
    }

    const double sig = mekf_.sigma_attitude();
    if (est_state_ == dict::AdcsEstState::CONVERGED) {
        if (sig > kLostSigma) { est_state_ = dict::AdcsEstState::CONVERGING; }
    } else if (s.time_s - est_init_t_ < 10.0) {
        est_state_ = dict::AdcsEstState::INITIALISING;
    } else if (sig < kConvergedSigma) {
        est_state_ = dict::AdcsEstState::CONVERGED;
        events_.raise(dict::EventId::ESTIMATOR_CONVERGED);
    } else {
        est_state_ = dict::AdcsEstState::CONVERGING;
    }
}

msg::ActuatorCommand AdcsApp::step(const msg::SensorFrame& s) {
    ++samples_;

    double dt = 0.0;
    if (have_last_t_) {
        dt = s.time_s - last_sample_t_;
        if (dt < 0.0 || dt > 5.0) { dt = 0.0; mekf_.reset(); est_state_ = dict::AdcsEstState::INVALID; }
    }
    last_sample_t_ = s.time_s;
    have_last_t_ = true;

    run_orbit(s);
    run_estimator(s, dt);

    const Vec3 gyro  = to_d(s.gyro_rps);
    const Vec3 omega = mekf_.initialised() ? mekf_.rate(gyro) : gyro;
    if (s.gyro_valid) { last_rate_dps_ = norm(omega) * kRadToDeg; }

    switch (system_mode_) {
        case dict::SystemMode::DETUMBLE:
        case dict::SystemMode::SAFE:     ctrl_mode_ = dict::AdcsCtrlMode::DETUMBLE; break;
        case dict::SystemMode::POINTING: ctrl_mode_ = dict::AdcsCtrlMode::POINTING; break;
        case dict::SystemMode::STANDBY:  ctrl_mode_ = dict::AdcsCtrlMode::STANDBY;  break;
        case dict::SystemMode::BOOT:     ctrl_mode_ = dict::AdcsCtrlMode::IDLE;     break;
    }

    BdotConfig bcfg;
    bcfg.gain_a_m2_per_t_s = params_.get_f32(dict::ParamId::BDOT_GAIN);
    bcfg.max_dipole_a_m2   = params_.get_f32(dict::ParamId::MTQ_MAX_DIPOLE);
    bcfg.filter_tau_s      = params_.get_f32(dict::ParamId::BDOT_FILTER_TAU_S);

    // B-dot sees every sample, so its derivative history is warm whenever it
    // is needed; only its output is gated by the mode.
    Vec3f bdot_m{};
    if (s.mag_valid) { bdot_m = bdot_.update(s.mag_t, s.time_s, bcfg); } else { bdot_.reset(); }

    msg::ActuatorCommand out;
    Vec3 body_torque{};
    double err_rad = 0.0;

    if (ctrl_mode_ == dict::AdcsCtrlMode::DETUMBLE) {
        out.dipole_a_m2 = bdot_m;
        out.mtq_commanded = true;
        body_torque = cross(to_d(bdot_m), to_d(s.mag_t));
    } else if (ctrl_mode_ == dict::AdcsCtrlMode::POINTING && s.wheels_valid && s.gyro_valid &&
               mekf_.initialised() &&
               orbit_.valid()) {
        PointingConfig pc;
        pc.bandwidth_rps = params_.get_f64(dict::ParamId::POINT_BANDWIDTH_RADPS);
        pc.max_slew_rps  = params_.get_f64(dict::ParamId::POINT_MAX_SLEW_DPS) / kRadToDeg;
        pc.dump_gain     = params_.get_f64(dict::ParamId::MOMENTUM_DUMP_GAIN);
        pc.max_dipole    = params_.get_f64(dict::ParamId::MTQ_MAX_DIPOLE);
        pc.wheels_usable = wheels_usable_;
        const PointingOutput po = nadir_control(mekf_.attitude(), omega, to_d(s.wheel_h),
                                                orbit_.position(), orbit_.velocity(),
                                                to_d(s.mag_t), s.mag_valid, pc);
        out.dipole_a_m2 = to_f(po.dipole);
        out.wheel_torque_nm = to_f(po.wheel_torque);
        out.mtq_commanded = true;
        out.wheels_commanded = true;
        body_torque = po.body_torque;
        err_rad = po.error_rad;
    }

    // ---- housekeeping ----------------------------------------------------
    hk_.est_state = static_cast<uint8_t>(est_state_);
    hk_.ctrl_mode = static_cast<uint8_t>(ctrl_mode_);
    const Quat q = mekf_.attitude();
    hk_.q_est_0 = static_cast<float>(q.w);
    hk_.q_est_1 = static_cast<float>(q.x);
    hk_.q_est_2 = static_cast<float>(q.y);
    hk_.q_est_3 = static_cast<float>(q.z);
    hk_.omega_x = static_cast<float>(omega.x);
    hk_.omega_y = static_cast<float>(omega.y);
    hk_.omega_z = static_cast<float>(omega.z);
    const Vec3 bias = mekf_.bias();
    hk_.gyro_bias_x = static_cast<float>(bias.x);
    hk_.gyro_bias_y = static_cast<float>(bias.y);
    hk_.gyro_bias_z = static_cast<float>(bias.z);
    hk_.att_sigma_deg = mekf_.initialised() ? static_cast<float>(mekf_.sigma_attitude() * kRadToDeg) : 0.0f;
    hk_.pointing_err_deg = static_cast<float>(err_rad * kRadToDeg);
    hk_.rate_norm = static_cast<float>(last_rate_dps_);
    hk_.mag_valid = s.mag_valid ? 1 : 0;
    hk_.sun_valid = s.sun_valid ? 1 : 0;
    hk_.gps_valid = (s.time_s - last_gps_t_ < kGpsStaleS) ? 1 : 0;
    hk_.eclipse = (orbit_.valid() && ephem::in_eclipse(orbit_.position(), s.time_s)) ? 1 : 0;
    hk_.pos_eci_x = orbit_.position().x;
    hk_.pos_eci_y = orbit_.position().y;
    hk_.pos_eci_z = orbit_.position().z;
    hk_.wheel_h_x = s.wheel_h.x;
    hk_.wheel_h_y = s.wheel_h.y;
    hk_.wheel_h_z = s.wheel_h.z;
    publish(out, body_torque);

    msg::AdcsStatus st;
    st.rate_dps = static_cast<float>(last_rate_dps_);
    st.rate_valid = s.gyro_valid;
    st.est_state = static_cast<uint8_t>(est_state_);
    st.orbit_valid = orbit_.valid();
    bus_.publish_object(core::Topic::AdcsStatus, st);
    return out;
}

void AdcsApp::publish(const msg::ActuatorCommand& out, const Vec3& body_torque) {
    hk_.torque_cmd_x = static_cast<float>(body_torque.x);
    hk_.torque_cmd_y = static_cast<float>(body_torque.y);
    hk_.torque_cmd_z = static_cast<float>(body_torque.z);
    hk_.dipole_cmd_x = out.dipole_a_m2.x;
    hk_.dipole_cmd_y = out.dipole_a_m2.y;
    hk_.dipole_cmd_z = out.dipole_a_m2.z;

    bus_.publish_object(core::Topic::ActuatorCommand, out);
    bus_.publish_object(core::Topic::AdcsHk, hk_);
}

}  // namespace fsw::adcs
