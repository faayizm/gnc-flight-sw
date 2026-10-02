// ============================================================================
//  fsw/apps/adcs/mekf.hpp -- multiplicative extended Kalman filter.
//
//  Estimates attitude and gyro bias from a gyro and any number of vector
//  measurements (here: magnetometer always, sun sensor when lit).
//
//  WHY "MULTIPLICATIVE". A quaternion has four numbers but three degrees of
//  freedom, and its covariance is singular. The MEKF (Lefferts, Markley and
//  Shuster, 1982) keeps the full quaternion as the reference and estimates a
//  small three-parameter rotation *error* about it:
//
//        q_true = q_est (x) dq(dtheta)          dtheta small, body frame
//
//  The filter state is x = [dtheta, dbias] (6), always zero after a reset;
//  the covariance P (6x6) is well-conditioned. After each update, dtheta is
//  folded back into q_est and zeroed.
//
//  PROPAGATION, with omega = gyro - bias:
//        q     <- q (x) dq(omega dt)
//        dtheta_dot = -omega x dtheta - dbias - noise_v
//        dbias_dot  = noise_u
//
//  MEASUREMENT of a unit vector known in the inertial frame, r:
//        predicted  b_hat = A(q) r
//        b = b_hat + [b_hat x] dtheta + noise         so  H = [ [b_hat x]  0 ]
//
//  The Joseph form is used for the covariance update. It costs a few more
//  multiplies and keeps P symmetric and positive definite in single sample
//  arithmetic, which the textbook (I - KH)P does not guarantee.
// ============================================================================
#pragma once

#include "apps/adcs/adcs_math.hpp"

namespace fsw::adcs {

struct MekfConfig {
    double gyro_noise_rad_per_rt_s   = 5.0e-5;   // angle random walk, sigma_v
    double bias_walk_rad_per_s_rt_s  = 1.0e-6;   // rate random walk, sigma_u
};

class Mekf {
 public:
    using Mat6 = MatN<6>;

    void init(const Quat& q, double sigma_att_rad, double sigma_bias_rps);
    void reset() { init_ = false; }

    void propagate(const Vec3& gyro_rps, double dt, const MekfConfig& cfg);

    // One unit-vector observation: measured in the body frame, known in the
    // inertial frame. Returns the innovation angle in radians, for monitoring.
    double update(const Vec3& meas_body, const Vec3& ref_inertial, double sigma_rad);

    // A full attitude measurement (star tracker), with per-axis noise in the
    // body frame -- trackers are much better across the boresight than about
    // it. Residual dtheta = 2 vec(q_est* (x) q_meas), H = [ I  0 ].
    // Returns the residual angle in radians.
    double update_attitude(const Quat& q_meas, const Vec3& sigma_rad);

    bool        initialised() const { return init_; }
    const Quat& attitude() const { return q_; }
    const Vec3& bias() const { return bias_; }
    Vec3        rate(const Vec3& gyro_rps) const { return gyro_rps - bias_; }

    // One-sigma attitude uncertainty, the worst of the three axes, radians.
    double sigma_attitude() const;
    double sigma_bias() const;

    const Mat6& covariance() const { return p_; }

 private:
    bool init_ = false;
    Quat q_{};
    Vec3 bias_{};
    Mat6 p_{};
};

}  // namespace fsw::adcs
