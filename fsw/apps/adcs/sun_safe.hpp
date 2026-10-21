// ============================================================================
//  fsw/apps/adcs/sun_safe.hpp -- turn the main solar array to the Sun, with
//  nothing but magnetorquers.
//
//  SAFE must work when the things that put the spacecraft in SAFE have failed.
//  So this law uses no star tracker, no attitude estimator, no orbit, no GPS
//  and no wheels: only the coarse sun sensor (a sun direction in body axes),
//  the magnetometer and the gyro.
//
//  THE LAW. Spin about the array normal n, and steer the spin's angular
//  momentum to the measured sun direction s:
//
//      tau = - I wr (w - W n)  +  h wp (s - (s.n) n)        h = I_n W
//
//  The first term holds the body rate at W about n and damps everything
//  else: a tumble, and the wobble (nutation) of the spin. The second leans
//  on the spinning body's angular momentum h, sideways towards the Sun. A
//  torque at right angles to a spin does not turn the body the way it
//  points; it turns the spin axis that way (a gyroscope precesses). So the
//  torque is along the part of s across n, not along n x s, and the array
//  normal follows the momentum round to the Sun at about wp rad/s.
//
//  WHY SPIN. The first version of this law did not spin: tau = kp (n x s) -
//  kd w, a spring and a damper. It worked from calm pointing and failed from
//  a tumble, 3 runs in 24. Gravity gradient (about 1e-7 N*m on this body)
//  is as strong as the spring the coils can afford, and it pulls the long
//  axis to the vertical twice an orbit, faster than a weak magnetic loop can
//  answer. A spinning body is stiff: its momentum stays put in inertial
//  space unless a torque acts for a long time, and the Sun stays put too.
//  The same law, spun, acquired 96 runs in 96 in the same toy.
//
//  WHICH AXIS. n is the minor axis of this body (I_z is the smallest). A
//  body that spins about its minor axis and loses energy (flexing panels,
//  sloshing fuel) turns over to spin about its major axis: Explorer 1 did,
//  in 1958. Here the rate term is what keeps it upright; the simulator's
//  body is rigid, so that is a limit of this model, and docs say so.
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
//  IN ECLIPSE there is no sun to point at. Only the rate term runs: the spin
//  carries on, and its momentum keeps the array where it was, near the Sun,
//  until the Sun comes back.
//
//  WITHOUT A MAGNETOMETER the coils have nothing to push against: any dipole
//  is a guess at a field that is not being measured. They are switched off.
//
//  The bandwidths are small because the coils are weak: at 0.2 A*m^2 in a 30
//  microtesla field they make 6e-6 N*m at most. SUN_SAFE_SPIN_DPS sets W and
//  SUN_SAFE_BANDWIDTH_RADPS sets wp; wr is fixed.
// ============================================================================
#pragma once

#include <cmath>

#include "apps/adcs/adcs_math.hpp"

namespace fsw::adcs {

struct SunSafeConfig {
    Vec3   panel_normal{0.0, 0.0, -1.0};   // the deployed main array faces body -Z
    double inertia       = 0.115;           // kg*m^2, a single figure for the rate loop
    double spin_inertia  = 0.04;            // kg*m^2, about panel_normal
    double spin_rps      = 0.0524;          // W, 3 deg/s
    double precession_rps = 0.003;          // wp
    double rate_rps      = 0.005;           // wr
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

    Vec3 tau = (omega - c.panel_normal * c.spin_rps) * (-c.inertia * c.rate_rps);
    if (sun_valid) {
        const double h = c.spin_inertia * c.spin_rps;
        const Vec3 across = sun - c.panel_normal * dot(sun, c.panel_normal);
        tau = tau + across * (h * c.precession_rps);
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
