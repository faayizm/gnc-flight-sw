// ============================================================================
//  fsw/apps/adcs/triad.hpp -- attitude from two vector observations.
//
//  Two non-parallel directions known in both frames fix an attitude
//  completely. TRIAD (Black, 1964) builds an orthonormal triad from each pair
//  and the rotation between the triads is the attitude:
//
//      t1 = v1,  t2 = unit(v1 x v2),  t3 = t1 x t2         (in each frame)
//      R(body -> inertial) = [t_inertial] [t_body]^T
//
//  The first vector is trusted fully and the second only for the rotation
//  about the first, so the more accurate sensor goes first. Here that is the
//  magnetometer: the coarse sun sensor is the noisier of the two.
//
//  Its role in this flight software is to give the Kalman filter a starting
//  point. A filter started 180 degrees out converges slowly, if at all;
//  started from TRIAD it is within a degree or two on the first sample.
// ============================================================================
#pragma once

#include "apps/adcs/adcs_math.hpp"

namespace fsw::adcs {

// Returns false if the two observations are too close to parallel to define
// an attitude (within ~5 degrees), in which case `out` is untouched.
inline bool triad(const Vec3& b1, const Vec3& b2, const Vec3& r1, const Vec3& r2, Quat& out) {
    const Vec3 tb1 = unit(b1), tr1 = unit(r1);
    const Vec3 cb = cross(tb1, unit(b2)), cr = cross(tr1, unit(r2));
    if (norm(cb) < 0.087 || norm(cr) < 0.087) { return false; }
    const Vec3 tb2 = unit(cb), tr2 = unit(cr);
    const Vec3 tb3 = cross(tb1, tb2), tr3 = cross(tr1, tr2);

    const Mat3 mb = Mat3::from_columns(tb1, tb2, tb3);
    const Mat3 mr = Mat3::from_columns(tr1, tr2, tr3);
    Mat3 r;  // mr * mb^T
    for (size_t i = 0; i < 3; ++i) {
        for (size_t j = 0; j < 3; ++j) {
            r.m[i][j] = mr.m[i][0] * mb.m[j][0] + mr.m[i][1] * mb.m[j][1] + mr.m[i][2] * mb.m[j][2];
        }
    }
    out = quat_from_matrix(r);
    return true;
}

}  // namespace fsw::adcs
