// ============================================================================
//  fsw/apps/adcs/bdot.hpp -- B-dot detumble control law.
//
//  THE IDEA. A tumbling satellite sees the magnetic field rotate in its own
//  body frame. The rate of change of the field *as measured by the body* is
//
//        dB/dt (body)  =  -omega x B   (+ a small term from the orbit)
//
//  so it carries the angular velocity, with no gyroscope involved. A
//  magnetorquer producing dipole  m = -k * dB/dt  creates torque
//
//        tau = m x B = -k (dB/dt) x B  =  k (omega x B) x B
//
//  and (omega x B) x B = -|B|^2 omega_perp, where omega_perp is the part of the
//  rate perpendicular to B. Torque therefore opposes rotation about every axis
//  except the one along the field -- which is why detumbling is never instant
//  and why it works best when the field direction keeps changing, as it does
//  around an orbit.
//
//  WHY THE FILTER. dB/dt is a difference of two noisy readings divided by a
//  small interval, so it amplifies noise by 1/dt. Near the end of a detumble
//  the true signal (omega * |B|, about 3e-7 T/s at 0.5 deg/s) is comparable
//  with that noise, and an unfiltered controller would dither the torquers
//  with nothing to do. A first-order low-pass trades a little phase lag for a
//  large reduction in noise.
//
//  WHAT IS NOT MODELLED HERE. The torquer's own field corrupts the
//  magnetometer while it is on. Real missions time-multiplex: torquers off,
//  measure, torquers on. The simulator does not model that coupling, so this
//  controller does not compensate for it. See sim/README.md.
//
//  Header-only, no dependencies, no state beyond the filter: it can be tested
//  without a scheduler, a bus or a link.
// ============================================================================
#pragma once

#include <cmath>

namespace fsw::adcs {

struct Vec3f {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

struct BdotConfig {
    float gain_a_m2_per_t_s = 1.0e5f;  // k in m = -k * dB/dt
    float max_dipole_a_m2   = 0.2f;    // magnetorquer limit, per axis
    float filter_tau_s      = 3.0f;    // low-pass time constant on dB/dt
};

class BdotController {
 public:
    // Discard history. Called when the sensor stream is interrupted: a
    // derivative across a gap of unknown length is meaningless.
    void reset() { have_prev_ = false; filtered_ = Vec3f{}; }

    // Feed one magnetometer sample (body frame, tesla) taken at `time_s`.
    // Returns the demanded dipole in A*m^2. The first sample after a reset has
    // no derivative to work with and returns zero.
    Vec3f update(const Vec3f& b_body, double time_s, const BdotConfig& cfg) {
        const double dt = time_s - prev_time_s_;
        if (!have_prev_ || dt <= 0.0 || dt > kMaxGapS) {
            prev_b_ = b_body;
            prev_time_s_ = time_s;
            have_prev_ = true;
            filtered_ = Vec3f{};
            return Vec3f{};
        }

        const float dtf = static_cast<float>(dt);
        const float inv = 1.0f / dtf;
        const float a = dtf / (cfg.filter_tau_s + dtf);

        const Vec3f raw{(b_body.x - prev_b_.x) * inv,
                        (b_body.y - prev_b_.y) * inv,
                        (b_body.z - prev_b_.z) * inv};
        filtered_.x += a * (raw.x - filtered_.x);
        filtered_.y += a * (raw.y - filtered_.y);
        filtered_.z += a * (raw.z - filtered_.z);
        prev_b_ = b_body;
        prev_time_s_ = time_s;

        Vec3f m{-cfg.gain_a_m2_per_t_s * filtered_.x,
                -cfg.gain_a_m2_per_t_s * filtered_.y,
                -cfg.gain_a_m2_per_t_s * filtered_.z};
        return saturate(m, cfg.max_dipole_a_m2);
    }

    const Vec3f& filtered_derivative() const { return filtered_; }

    // Scale the whole vector so no axis exceeds the limit. Clamping each axis
    // separately would change the dipole's direction, and with it the torque
    // direction; scaling keeps the direction and only costs magnitude.
    static Vec3f saturate(const Vec3f& m, float limit) {
        float peak = std::fabs(m.x);
        if (std::fabs(m.y) > peak) { peak = std::fabs(m.y); }
        if (std::fabs(m.z) > peak) { peak = std::fabs(m.z); }
        if (peak <= limit || peak <= 0.0f) { return m; }
        const float s = limit / peak;
        return Vec3f{m.x * s, m.y * s, m.z * s};
    }

 private:
    // A gap longer than this means samples were lost, not that the satellite
    // is rotating slowly.
    static constexpr double kMaxGapS = 5.0;

    bool   have_prev_ = false;
    double prev_time_s_ = 0.0;
    Vec3f  prev_b_{};
    Vec3f  filtered_{};
};

}  // namespace fsw::adcs
