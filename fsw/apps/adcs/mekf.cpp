// ============================================================================
//  fsw/apps/adcs/mekf.cpp
// ============================================================================
#include "apps/adcs/mekf.hpp"

#include <cmath>

namespace fsw::adcs {

namespace {

void skew(const Vec3& a, double out[3][3]) {
    out[0][0] = 0.0;  out[0][1] = -a.z; out[0][2] = a.y;
    out[1][0] = a.z;  out[1][1] = 0.0;  out[1][2] = -a.x;
    out[2][0] = -a.y; out[2][1] = a.x;  out[2][2] = 0.0;
}

bool invert3(const double a[3][3], double inv[3][3]) {
    const double c00 = a[1][1] * a[2][2] - a[1][2] * a[2][1];
    const double c01 = a[1][2] * a[2][0] - a[1][0] * a[2][2];
    const double c02 = a[1][0] * a[2][1] - a[1][1] * a[2][0];
    const double det = a[0][0] * c00 + a[0][1] * c01 + a[0][2] * c02;
    if (std::fabs(det) < 1e-300) { return false; }
    const double k = 1.0 / det;
    inv[0][0] = c00 * k;
    inv[0][1] = (a[0][2] * a[2][1] - a[0][1] * a[2][2]) * k;
    inv[0][2] = (a[0][1] * a[1][2] - a[0][2] * a[1][1]) * k;
    inv[1][0] = c01 * k;
    inv[1][1] = (a[0][0] * a[2][2] - a[0][2] * a[2][0]) * k;
    inv[1][2] = (a[0][2] * a[1][0] - a[0][0] * a[1][2]) * k;
    inv[2][0] = c02 * k;
    inv[2][1] = (a[0][1] * a[2][0] - a[0][0] * a[2][1]) * k;
    inv[2][2] = (a[0][0] * a[1][1] - a[0][1] * a[1][0]) * k;
    return true;
}

}  // namespace

void Mekf::init(const Quat& q, double sigma_att_rad, double sigma_bias_rps) {
    q_ = normalized(q);
    bias_ = Vec3{};
    p_ = Mat6{};
    for (size_t i = 0; i < 3; ++i) {
        p_.m[i][i] = sigma_att_rad * sigma_att_rad;
        p_.m[i + 3][i + 3] = sigma_bias_rps * sigma_bias_rps;
    }
    init_ = true;
}

void Mekf::propagate(const Vec3& gyro, double dt, const MekfConfig& cfg) {
    if (!init_ || dt <= 0.0) { return; }
    const Vec3 w = gyro - bias_;
    q_ = normalized(q_ * quat_from_rotvec(w * dt));

    // Phi = I + F dt,  F = [ -[w x]  -I ; 0  0 ]
    double wx[3][3];
    skew(w, wx);
    Mat6 phi = Mat6::identity();
    for (size_t i = 0; i < 3; ++i) {
        for (size_t j = 0; j < 3; ++j) { phi.m[i][j] -= wx[i][j] * dt; }
        phi.m[i][i + 3] = -dt;
    }
    p_ = phi * p_ * transpose(phi);

    const double sv = cfg.gyro_noise_rad_per_rt_s, su = cfg.bias_walk_rad_per_s_rt_s;
    const double q_att  = sv * sv * dt + su * su * dt * dt * dt / 3.0;
    const double q_bias = su * su * dt;
    for (size_t i = 0; i < 3; ++i) {
        p_.m[i][i] += q_att;
        p_.m[i + 3][i + 3] += q_bias;
    }
}

double Mekf::update(const Vec3& meas_body, const Vec3& ref_inertial, double sigma) {
    if (!init_) { return 0.0; }
    const Vec3 b  = unit(meas_body);
    const Vec3 bh = unit(rotate_inv(q_, unit(ref_inertial)));

    // H (3x6) = [ [bh x]  0 ]
    double h[3][6] = {};
    double sk[3][3];
    skew(bh, sk);
    for (size_t i = 0; i < 3; ++i) {
        for (size_t j = 0; j < 3; ++j) { h[i][j] = sk[i][j]; }
    }

    // PHt (6x3) and S = H P H^T + R (3x3)
    double pht[6][3] = {};
    for (size_t i = 0; i < 6; ++i) {
        for (size_t j = 0; j < 3; ++j) {
            double s = 0.0;
            for (size_t k = 0; k < 6; ++k) { s += p_.m[i][k] * h[j][k]; }
            pht[i][j] = s;
        }
    }
    double s3[3][3];
    for (size_t i = 0; i < 3; ++i) {
        for (size_t j = 0; j < 3; ++j) {
            double s = 0.0;
            for (size_t k = 0; k < 6; ++k) { s += h[i][k] * pht[k][j]; }
            s3[i][j] = s + (i == j ? sigma * sigma : 0.0);
        }
    }
    double sinv[3][3];
    if (!invert3(s3, sinv)) { return 0.0; }

    double k[6][3] = {};
    for (size_t i = 0; i < 6; ++i) {
        for (size_t j = 0; j < 3; ++j) {
            k[i][j] = pht[i][0] * sinv[0][j] + pht[i][1] * sinv[1][j] + pht[i][2] * sinv[2][j];
        }
    }

    const Vec3 y = b - bh;
    double dx[6];
    for (size_t i = 0; i < 6; ++i) { dx[i] = k[i][0] * y.x + k[i][1] * y.y + k[i][2] * y.z; }

    // Joseph form: P = (I - KH) P (I - KH)^T + K R K^T
    Mat6 ikh = Mat6::identity();
    for (size_t i = 0; i < 6; ++i) {
        for (size_t j = 0; j < 6; ++j) {
            ikh.m[i][j] -= k[i][0] * h[0][j] + k[i][1] * h[1][j] + k[i][2] * h[2][j];
        }
    }
    Mat6 p = ikh * p_ * transpose(ikh);
    const double r = sigma * sigma;
    for (size_t i = 0; i < 6; ++i) {
        for (size_t j = 0; j < 6; ++j) {
            p.m[i][j] += r * (k[i][0] * k[j][0] + k[i][1] * k[j][1] + k[i][2] * k[j][2]);
        }
    }
    p_ = p;

    // Reset: fold the error into the reference and zero it.
    q_ = normalized(q_ * quat_from_rotvec(Vec3{dx[0], dx[1], dx[2]}));
    bias_ = bias_ + Vec3{dx[3], dx[4], dx[5]};

    return std::acos(dot(b, bh) > 1.0 ? 1.0 : (dot(b, bh) < -1.0 ? -1.0 : dot(b, bh)));
}

double Mekf::update_attitude(const Quat& q_meas, const Vec3& sigma) {
    if (!init_) { return 0.0; }
    Quat dq = conj(q_) * normalized(q_meas);
    if (dq.w < 0.0) { dq = Quat{-dq.w, -dq.x, -dq.y, -dq.z}; }
    const Vec3 y = dq.vec() * 2.0;

    // S = P[0:3,0:3] + R ;  K = P[:,0:3] S^-1
    double s3[3][3];
    for (size_t i = 0; i < 3; ++i) {
        for (size_t j = 0; j < 3; ++j) { s3[i][j] = p_.m[i][j]; }
        s3[i][i] += sigma[i] * sigma[i];
    }
    double sinv[3][3];
    if (!invert3(s3, sinv)) { return 0.0; }
    double k[6][3];
    for (size_t i = 0; i < 6; ++i) {
        for (size_t j = 0; j < 3; ++j) {
            k[i][j] = p_.m[i][0] * sinv[0][j] + p_.m[i][1] * sinv[1][j] + p_.m[i][2] * sinv[2][j];
        }
    }
    double dx[6];
    for (size_t i = 0; i < 6; ++i) { dx[i] = k[i][0] * y.x + k[i][1] * y.y + k[i][2] * y.z; }

    Mat6 ikh = Mat6::identity();
    for (size_t i = 0; i < 6; ++i) {
        for (size_t j = 0; j < 3; ++j) { ikh.m[i][j] -= k[i][j]; }
    }
    Mat6 p = ikh * p_ * transpose(ikh);
    for (size_t i = 0; i < 6; ++i) {
        for (size_t j = 0; j < 6; ++j) {
            p.m[i][j] += k[i][0] * k[j][0] * sigma[0] * sigma[0] +
                         k[i][1] * k[j][1] * sigma[1] * sigma[1] +
                         k[i][2] * k[j][2] * sigma[2] * sigma[2];
        }
    }
    p_ = p;
    q_ = normalized(q_ * quat_from_rotvec(Vec3{dx[0], dx[1], dx[2]}));
    bias_ = bias_ + Vec3{dx[3], dx[4], dx[5]};
    return norm(y);
}

double Mekf::sigma_attitude() const {
    double worst = 0.0;
    for (size_t i = 0; i < 3; ++i) { worst = p_.m[i][i] > worst ? p_.m[i][i] : worst; }
    return std::sqrt(worst);
}

double Mekf::sigma_bias() const {
    double worst = 0.0;
    for (size_t i = 3; i < 6; ++i) { worst = p_.m[i][i] > worst ? p_.m[i][i] : worst; }
    return std::sqrt(worst);
}

}  // namespace fsw::adcs
