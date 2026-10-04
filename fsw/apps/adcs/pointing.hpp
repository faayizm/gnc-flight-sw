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
//
//  A FAILED WHEEL. When FDIR takes a wheel out of service (fdir/wheel_ladder.hpp)
//  its axis has no wheel, and the magnetorquers take over that axis too. The
//  dipole that produces a wanted torque tau is
//
//      m = (B x tau) / |B|^2      =>      m x B = tau - (tau . B^) B^
//
//  i.e. all of tau except its component along the field, which no coil can
//  ever make. The parts of m x B that land on the healthy axes are known, so
//  the healthy wheels cancel them, exactly as for dumping. Magnetorquers are
//  a few hundred times weaker than a wheel, so the failed axis is given a
//  bandwidth they can actually deliver; pointing on that axis becomes looser,
//  and loosest when the field happens to lie along it -- but it is pointing.
// ============================================================================
#pragma once

#include <cmath>
#include <cstdint>

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
    uint8_t wheels_usable  = 0x7;          // bit i: the wheel on body axis i is in service
    double magnetic_bandwidth_rps = 0.01;  // wn on an axis with no wheel
};

struct PointingOutput {
    Vec3   body_torque{};     // what the controller wants the body to feel
    Vec3   wheel_torque{};    // what each wheel motor is asked for
    Vec3   dipole{};          // magnetorquer demand for momentum dumping
    double error_rad = 0.0;   // estimated three-axis attitude error
    double boresight_rad = 0.0;  // estimated angle of body +Z (the payload) from nadir
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
    // The payload only cares where +Z points. Rotation about +Z (yaw) is in
    // the three-axis error but not in this one -- and with the Z wheel out
    // of service, yaw is exactly what the magnetorquers are slowest to hold.
    const Vec3 boresight = rotate(q, Vec3{0.0, 0.0, 1.0});
    const Vec3 nadir = -unit(r);
    out.boresight_rad = std::atan2(norm(cross(boresight, nadir)), dot(boresight, nadir));

    const Vec3 w_ref = rotate_inv(q, w_target_eci);
    const Vec3 w_err = omega - w_ref;

    // Per-axis bandwidth: the full loop where a wheel works, a gentle one
    // where only the magnetorquers can push.
    const bool wheel[3] = {(c.wheels_usable & 1u) != 0, (c.wheels_usable & 2u) != 0,
                           (c.wheels_usable & 4u) != 0};
    const double wn[3] = {wheel[0] ? c.bandwidth_rps : c.magnetic_bandwidth_rps,
                          wheel[1] ? c.bandwidth_rps : c.magnetic_bandwidth_rps,
                          wheel[2] ? c.bandwidth_rps : c.magnetic_bandwidth_rps};
    const Vec3 qv = qe.vec();
    Vec3 w_cmd{qv.x * (-2.0 * wn[0] / (2.0 * c.damping)), qv.y * (-2.0 * wn[1] / (2.0 * c.damping)),
               qv.z * (-2.0 * wn[2] / (2.0 * c.damping))};
    const double wc = norm(w_cmd);
    if (wc > c.max_slew_rps) { w_cmd = w_cmd * (c.max_slew_rps / wc); }

    const double kd[3] = {2.0 * c.damping * wn[0], 2.0 * c.damping * wn[1], 2.0 * c.damping * wn[2]};
    const Vec3 iw{c.inertia.x * omega.x, c.inertia.y * omega.y, c.inertia.z * omega.z};
    const Vec3 de = w_err - w_cmd;
    Vec3 tau = Vec3{-kd[0] * c.inertia.x * de.x, -kd[1] * c.inertia.y * de.y, -kd[2] * c.inertia.z * de.z}
               + cross(omega, iw + wheel_h);

    // Magnetorquers: dump what the working wheels have stored, and supply the
    // torque the missing wheels cannot. A dead wheel's momentum cannot be
    // dumped through it; it reaches the body as the wheel spins down, and the
    // loop above deals with it there.
    Vec3 tau_mtq{};
    const double b2 = dot(b_body, b_body);
    if (mag_valid && b2 > 0.0) {
        const Vec3 h_dump{wheel[0] ? wheel_h.x : 0.0, wheel[1] ? wheel_h.y : 0.0,
                          wheel[2] ? wheel_h.z : 0.0};
        const Vec3 tau_need{wheel[0] ? 0.0 : tau.x, wheel[1] ? 0.0 : tau.y, wheel[2] ? 0.0 : tau.z};
        Vec3 m = cross(h_dump, b_body) * (c.dump_gain / b2);
        if (c.wheels_usable != 0x7) { m = m + cross(b_body, tau_need) * (1.0 / b2); }
        out.dipole = limit_per_axis(m, c.max_dipole);
        tau_mtq = cross(out.dipole, b_body);
    }

    // Wheels supply what the magnetorquers do not; the body feels -motor torque.
    Vec3 wheel_body = tau - tau_mtq;
    if (!wheel[0]) { wheel_body.x = 0.0; }
    if (!wheel[1]) { wheel_body.y = 0.0; }
    if (!wheel[2]) { wheel_body.z = 0.0; }
    wheel_body = limit_per_axis(wheel_body, c.max_wheel_torque);
    out.body_torque = wheel_body + tau_mtq;
    out.wheel_torque = -wheel_body;
    return out;
}

}  // namespace fsw::adcs
