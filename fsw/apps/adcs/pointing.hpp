// ============================================================================
//  fsw/apps/adcs/pointing.hpp -- nadir pointing and momentum management.
//
//  TARGET. The body +Z axis points at the centre of the Earth, +Y along the
//  negative orbit normal and +X completes the triad (close to the velocity
//  direction for a near-circular orbit). That frame rotates once per orbit, so
//  holding it means turning continuously at the orbit rate about -Y.
//
//  CONTROL LAW. Quaternion-feedback PD (Wie, Weiss and Arapostathis, 1989)
//  written as a rate loop around an attitude loop:
//
//      w_cmd = clamp( -(wn / 2 zeta) * 2 q_err_vec ,  max slew )
//      tau   = -Kd (w_err - w_cmd) + w x (I w + h_w)
//
//  with Kd = 2 zeta wn I. For small errors this is exactly PD with
//  Kp = I wn^2; for large ones the commanded rate is clamped, so acquisition
//  from any attitude happens at a bounded slew rate and cannot spin the
//  wheels up to saturation. The w x (I w + h_w) term cancels the gyroscopic
//  coupling, so each axis behaves like an independent double integrator.
//
//  ACTUATORS. Reaction wheels make the torque; the body feels the opposite of
//  what the wheel motor applies. Any external torque -- gravity gradient,
//  residual friction -- is absorbed by the wheels as stored momentum, which
//  would grow until a wheel saturates. The magnetorquers bleed it off:
//
//      m = (k / |B|^2) h_w x B      =>      m x B = -k h_w(perpendicular to B)
//
//  The torque from that dipole is known, so it is subtracted from what the
//  wheels are asked to do. Dumping then costs no pointing accuracy at all,
//  which is the point of doing it this way.
// ============================================================================
#pragma once

#include <cmath>

#include "apps/adcs/adcs_math.hpp"

namespace fsw::adcs {

struct PointingConfig {
    Vec3   inertia{0.105, 0.115, 0.042};   // kg*m^2, the mass-properties estimate
    double bandwidth_rps   = 0.1;          // wn
    double damping         = 0.9;          // zeta
    double max_slew_rps    = 0.0175;
    double max_wheel_torque = 2.0e-3;      // N*m, per wheel, from the datasheet
    double dump_gain       = 5.0e-4;       // k, 1/s
    double max_dipole      = 0.2;          // A*m^2, per axis
};

struct PointingOutput {
    Vec3   body_torque{};     // what the controller wants the body to feel
    Vec3   wheel_torque{};    // what each wheel motor is asked for
    Vec3   dipole{};          // magnetorquer demand for momentum dumping
    double error_rad = 0.0;   // estimated three-axis attitude error
};

// Nadir/along-track target attitude (body -> inertial) and its inertial
// angular velocity, from the on-board position and velocity.
inline Quat nadir_target(const Vec3& r, const Vec3& v, Vec3& omega_target_eci) {
    const Vec3 z = -unit(r);
    const Vec3 h = cross(r, v);
    const Vec3 y = -unit(h);
    const Vec3 x = cross(y, z);
    omega_target_eci = h * (1.0 / dot(r, r));
    return quat_from_matrix(Mat3::from_columns(x, y, z));
}

// Scale a vector so no component exceeds `limit`, keeping its direction.
inline Vec3 limit_per_axis(const Vec3& v, double limit) {
    double peak = std::fabs(v.x);
    if (std::fabs(v.y) > peak) { peak = std::fabs(v.y); }
    if (std::fabs(v.z) > peak) { peak = std::fabs(v.z); }
    return (peak > limit && peak > 0.0) ? v * (limit / peak) : v;
}

inline PointingOutput nadir_control(const Quat& q, const Vec3& omega, const Vec3& wheel_h,
                                    const Vec3& r, const Vec3& v, const Vec3& b_body,
                                    bool mag_valid, const PointingConfig& c) {
    PointingOutput out;

    Vec3 w_target_eci;
    const Quat qt = nadir_target(r, v, w_target_eci);
    Quat qe = conj(qt) * q;                  // body relative to target
    if (qe.w < 0.0) { qe = Quat{-qe.w, -qe.x, -qe.y, -qe.z}; }
    out.error_rad = quat_angle(qe);

    const Vec3 w_ref = rotate_inv(q, w_target_eci);
    const Vec3 w_err = omega - w_ref;

    Vec3 w_cmd = qe.vec() * (-2.0 * c.bandwidth_rps / (2.0 * c.damping));
    const double wn = norm(w_cmd);
    if (wn > c.max_slew_rps) { w_cmd = w_cmd * (c.max_slew_rps / wn); }

    const double kd = 2.0 * c.damping * c.bandwidth_rps;
    const Vec3 iw{c.inertia.x * omega.x, c.inertia.y * omega.y, c.inertia.z * omega.z};
    const Vec3 de = w_err - w_cmd;
    Vec3 tau = Vec3{-kd * c.inertia.x * de.x, -kd * c.inertia.y * de.y, -kd * c.inertia.z * de.z}
               + cross(omega, iw + wheel_h);

    // Momentum dumping, and the torque it will produce.
    Vec3 tau_mtq{};
    const double b2 = dot(b_body, b_body);
    if (mag_valid && b2 > 0.0 && c.dump_gain > 0.0) {
        out.dipole = limit_per_axis(cross(wheel_h, b_body) * (c.dump_gain / b2), c.max_dipole);
        tau_mtq = cross(out.dipole, b_body);
    }

    // Wheels supply what the magnetorquers do not; the body feels -motor torque.
    const Vec3 wheel_body = limit_per_axis(tau - tau_mtq, c.max_wheel_torque);
    out.body_torque = wheel_body + tau_mtq;
    out.wheel_torque = -wheel_body;
    return out;
}

}  // namespace fsw::adcs
