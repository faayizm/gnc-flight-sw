// ============================================================================
//  fsw/apps/adcs/sun_safe.hpp -- turn the main solar array to the Sun, with
//  nothing but magnetorquers.
//
//  SAFE must work when the things that put the spacecraft in SAFE have failed.
//  So this law uses no star tracker, no attitude estimator, no orbit, no GPS
//  and no wheels: only the coarse sun sensor (a sun direction in body axes),
//  the magnetometer and the gyro.
//
//  THE LAW. Turn the array normal n towards the measured sun direction s, and
//  damp the rate:
//
//      tau = kp (n x s) - kd w
//
//  n x s points along the axis about which n must turn to reach s, with a size
//  of sin(angle), so a torque along +(n x s) turns n towards s. (The sign is
//  easy to get wrong: the proposal for this law in the roadmap first had it
//  negative, which would turn the array away from the Sun. A unit test pins
//  it.) The rate term keeps
//  that turn from overshooting, and stops any spin about the sun line (which
//  the first term cannot see: turning about s changes nothing about the angle).
//
//  THE COILS. The magnetorquers make that torque as well as they can:
//
//      m = (B x tau) / |B|^2      =>      m x B = tau minus its part along B
//
//  the same mapping that flies a dead wheel's axis in pointing.hpp. The part
//  of tau along the field cannot be made, ever; but B turns as the orbit goes
//  round, so what cannot be done now can be done later. This law converges
//  over an orbit, not in minutes, and the requirements say so.
//
//  IN ECLIPSE there is no sun to point at. The rate is damped, and the
//  spacecraft waits for the Sun to come back.
//
//  WITHOUT A MAGNETOMETER the coils have nothing to push against: any dipole
//  is a guess at a field that is not being measured. They are switched off.
//
//  Gains come from a bandwidth wn and the inertia, as for the pointing law:
//  kp = I wn^2, kd = 2 zeta wn I. The bandwidth is small because the coils
//  are weak: at 0.2 A*m^2 in a 30 microtesla field they make 6e-6 N*m at most.
// ============================================================================
#pragma once

#include <cmath>

#include "apps/adcs/adcs_math.hpp"

namespace fsw::adcs {

struct SunSafeConfig {
    Vec3   panel_normal{0.0, 0.0, -1.0};   // the deployed main array faces body -Z
    double inertia       = 0.115;           // kg*m^2, a single figure: this law is not fussy
    double bandwidth_rps = 0.0015;          // wn: well below the field's turning rate
    double damping       = 0.9;             // zeta
    double max_dipole    = 0.2;             // A*m^2, per axis
};

enum class SunSafeState { Pointing, Damping, CoilsOff };

struct SunSafeOutput {
    Vec3         dipole{};       // A*m^2
    Vec3         torque_wanted{};
    Vec3         torque{};       // what the coils will actually produce, m x B
    double       sun_angle_rad = -1.0;   // array normal to measured sun; -1 without a sun
    SunSafeState state = SunSafeState::CoilsOff;
};

inline Vec3 limit_dipole(const Vec3& v, double limit) {
    double peak = std::fabs(v.x);
    if (std::fabs(v.y) > peak) { peak = std::fabs(v.y); }
    if (std::fabs(v.z) > peak) { peak = std::fabs(v.z); }
    return (peak > limit && peak > 0.0) ? v * (limit / peak) : v;
}

// One sample. `omega` is the gyro's body rate, rad/s; `sun` the coarse sun
// sensor's unit vector; `b` the magnetometer, tesla.
inline SunSafeOutput sun_safe(const Vec3& sun, bool sun_valid, const Vec3& omega,
                              const Vec3& b, bool mag_valid, const SunSafeConfig& c) {
    SunSafeOutput out;
    if (sun_valid) {
        out.sun_angle_rad = std::acos(std::fmax(-1.0, std::fmin(1.0, dot(c.panel_normal, sun))));
    }
    const double b2 = dot(b, b);
    if (!mag_valid || b2 <= 0.0) {
        out.state = SunSafeState::CoilsOff;
        return out;
    }

    const double kp = c.inertia * c.bandwidth_rps * c.bandwidth_rps;
    const double kd = 2.0 * c.damping * c.bandwidth_rps * c.inertia;
    Vec3 tau = omega * (-kd);
    if (sun_valid) {
        tau = tau + cross(c.panel_normal, sun) * kp;
        out.state = SunSafeState::Pointing;
    } else {
        out.state = SunSafeState::Damping;
    }
    out.torque_wanted = tau;
    out.dipole = limit_dipole(cross(b, tau) * (1.0 / b2), c.max_dipole);
    out.torque = cross(out.dipole, b);
    return out;
}

}  // namespace fsw::adcs
