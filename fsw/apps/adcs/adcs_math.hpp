// ============================================================================
//  fsw/apps/adcs/adcs_math.hpp -- vectors, quaternions and small matrices.
//
//  Fixed-size, value types, double precision, no allocation, no templates
//  beyond what is needed. Everything the attitude code needs and nothing more.
//
//  QUATERNION CONVENTION (the same as sim/models/linalg.py, deliberately):
//    q = (w, x, y, z), scalar first, and q describes the body's attitude --
//    it rotates a body-frame vector into the inertial frame:
//
//        v_inertial = q (x) v_body (x) q*            rotate(q, v)
//        v_body     = q* (x) v_inertial (x) q        rotate_inv(q, v)
//
//    Kinematics:  q_dot = 1/2 q (x) (0, omega_body).
//
//  Two conventions are in common use and mixing them is the classic attitude
//  bug: an estimator that is right in one and a controller written in the
//  other produce a spacecraft that turns the wrong way, confidently.
// ============================================================================
#pragma once

#include <cmath>
#include <cstddef>

namespace fsw::adcs {

struct Vec3 {
    double x = 0.0, y = 0.0, z = 0.0;

    constexpr Vec3() = default;
    constexpr Vec3(double ax, double ay, double az) : x(ax), y(ay), z(az) {}

    double  operator[](size_t i) const { return i == 0 ? x : (i == 1 ? y : z); }
    double& operator[](size_t i)       { return i == 0 ? x : (i == 1 ? y : z); }
};

inline Vec3   operator+(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3   operator-(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3   operator-(const Vec3& a)                { return {-a.x, -a.y, -a.z}; }
inline Vec3   operator*(const Vec3& a, double k)      { return {a.x * k, a.y * k, a.z * k}; }
inline Vec3   operator*(double k, const Vec3& a)      { return a * k; }
inline double dot(const Vec3& a, const Vec3& b)       { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3   cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline double norm(const Vec3& a) { return std::sqrt(dot(a, a)); }
inline Vec3   unit(const Vec3& a) {
    const double n = norm(a);
    return n > 0.0 ? a * (1.0 / n) : Vec3{};
}

struct Quat {
    double w = 1.0, x = 0.0, y = 0.0, z = 0.0;

    constexpr Quat() = default;
    constexpr Quat(double aw, double ax, double ay, double az) : w(aw), x(ax), y(ay), z(az) {}
    Vec3 vec() const { return {x, y, z}; }
};

inline Quat operator*(const Quat& p, const Quat& q) {
    return {p.w * q.w - p.x * q.x - p.y * q.y - p.z * q.z,
            p.w * q.x + p.x * q.w + p.y * q.z - p.z * q.y,
            p.w * q.y - p.x * q.z + p.y * q.w + p.z * q.x,
            p.w * q.z + p.x * q.y - p.y * q.x + p.z * q.w};
}
inline Quat conj(const Quat& q) { return {q.w, -q.x, -q.y, -q.z}; }
inline Quat normalized(const Quat& q) {
    const double n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    return {q.w / n, q.x / n, q.y / n, q.z / n};
}

// Body -> inertial.
inline Vec3 rotate(const Quat& q, const Vec3& v) {
    const Vec3 u = q.vec();
    const Vec3 t = 2.0 * cross(u, v);
    return v + q.w * t + cross(u, t);
}
// Inertial -> body.
inline Vec3 rotate_inv(const Quat& q, const Vec3& v) { return rotate(conj(q), v); }

// The rotation by angle |phi| about phi, as a quaternion. Exact, with the
// small-angle limit handled so a zero rotation is not a division by zero.
inline Quat quat_from_rotvec(const Vec3& phi) {
    const double a = norm(phi);
    if (a < 1e-12) { return normalized(Quat{1.0, 0.5 * phi.x, 0.5 * phi.y, 0.5 * phi.z}); }
    const double s = std::sin(0.5 * a) / a;
    return {std::cos(0.5 * a), s * phi.x, s * phi.y, s * phi.z};
}

// Rotation angle of a quaternion, 0..pi, in radians.
inline double quat_angle(const Quat& q) {
    const double w = std::fabs(q.w) > 1.0 ? 1.0 : std::fabs(q.w);
    return 2.0 * std::acos(w);
}

// 3x3 matrix, row-major.
struct Mat3 {
    double m[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};

    static Mat3 identity() {
        Mat3 r;
        r.m[0][0] = r.m[1][1] = r.m[2][2] = 1.0;
        return r;
    }
    // Columns given as vectors.
    static Mat3 from_columns(const Vec3& c0, const Vec3& c1, const Vec3& c2) {
        Mat3 r;
        for (size_t i = 0; i < 3; ++i) {
            r.m[i][0] = c0[i];
            r.m[i][1] = c1[i];
            r.m[i][2] = c2[i];
        }
        return r;
    }
};

inline Vec3 operator*(const Mat3& a, const Vec3& v) {
    return {a.m[0][0] * v.x + a.m[0][1] * v.y + a.m[0][2] * v.z,
            a.m[1][0] * v.x + a.m[1][1] * v.y + a.m[1][2] * v.z,
            a.m[2][0] * v.x + a.m[2][1] * v.y + a.m[2][2] * v.z};
}

// Quaternion from a body->inertial rotation matrix (columns = body axes in
// inertial coordinates). Shepperd's method: pick the largest of the four
// candidate divisors so the result is accurate for every attitude.
inline Quat quat_from_matrix(const Mat3& r) {
    const double tr = r.m[0][0] + r.m[1][1] + r.m[2][2];
    Quat q;
    if (tr >= r.m[0][0] && tr >= r.m[1][1] && tr >= r.m[2][2]) {
        const double s = 2.0 * std::sqrt(1.0 + tr);
        q = {0.25 * s, (r.m[2][1] - r.m[1][2]) / s, (r.m[0][2] - r.m[2][0]) / s,
             (r.m[1][0] - r.m[0][1]) / s};
    } else if (r.m[0][0] >= r.m[1][1] && r.m[0][0] >= r.m[2][2]) {
        const double s = 2.0 * std::sqrt(1.0 + r.m[0][0] - r.m[1][1] - r.m[2][2]);
        q = {(r.m[2][1] - r.m[1][2]) / s, 0.25 * s, (r.m[0][1] + r.m[1][0]) / s,
             (r.m[0][2] + r.m[2][0]) / s};
    } else if (r.m[1][1] >= r.m[2][2]) {
        const double s = 2.0 * std::sqrt(1.0 + r.m[1][1] - r.m[0][0] - r.m[2][2]);
        q = {(r.m[0][2] - r.m[2][0]) / s, (r.m[0][1] + r.m[1][0]) / s, 0.25 * s,
             (r.m[1][2] + r.m[2][1]) / s};
    } else {
        const double s = 2.0 * std::sqrt(1.0 + r.m[2][2] - r.m[0][0] - r.m[1][1]);
        q = {(r.m[1][0] - r.m[0][1]) / s, (r.m[0][2] + r.m[2][0]) / s,
             (r.m[1][2] + r.m[2][1]) / s, 0.25 * s};
    }
    if (q.w < 0.0) { q = {-q.w, -q.x, -q.y, -q.z}; }
    return normalized(q);
}

// Square matrix of fixed size, row-major, for the Kalman filter.
template <size_t N>
struct MatN {
    double m[N][N] = {};

    static MatN identity() {
        MatN r;
        for (size_t i = 0; i < N; ++i) { r.m[i][i] = 1.0; }
        return r;
    }
};

template <size_t N>
MatN<N> operator*(const MatN<N>& a, const MatN<N>& b) {
    MatN<N> r;
    for (size_t i = 0; i < N; ++i) {
        for (size_t k = 0; k < N; ++k) {
            const double aik = a.m[i][k];
            if (aik == 0.0) { continue; }
            for (size_t j = 0; j < N; ++j) { r.m[i][j] += aik * b.m[k][j]; }
        }
    }
    return r;
}

template <size_t N>
MatN<N> transpose(const MatN<N>& a) {
    MatN<N> r;
    for (size_t i = 0; i < N; ++i) {
        for (size_t j = 0; j < N; ++j) { r.m[i][j] = a.m[j][i]; }
    }
    return r;
}

}  // namespace fsw::adcs
