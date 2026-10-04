// ============================================================================
//  Tests for the attitude chain: math, ephemeris, TRIAD, MEKF, orbit
//  propagation and the pointing law.
// ============================================================================
#include <cmath>
#include <cstdint>

#include "apps/adcs/adcs_math.hpp"
#include "apps/adcs/ephemeris.hpp"
#include "apps/adcs/mekf.hpp"
#include "apps/adcs/orbit_prop.hpp"
#include "apps/adcs/pointing.hpp"
#include "apps/adcs/triad.hpp"
#include "framework.hpp"

using namespace fsw::adcs;

namespace {
constexpr double kDeg = 3.14159265358979323846 / 180.0;

double angle_between(const Quat& a, const Quat& b) { return quat_angle(conj(a) * b); }

// A deterministic pseudo-random sequence for noise in tests (no <random>).
struct Lcg {
    uint64_t s = 12345;
    double uniform() {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<double>(s >> 11) / 9007199254740992.0;
    }
    double gauss() {
        double u1 = uniform();
        if (u1 < 1e-300) { u1 = 1e-300; }
        return std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * 3.14159265358979 * uniform());
    }
};
}  // namespace

TEST(adcs_math, rotate_and_rotate_inv_are_inverses) {
    const Quat q = normalized(Quat{0.3, -0.5, 0.2, 0.7});
    const Vec3 v{0.1, -2.0, 3.0};
    const Vec3 back = rotate_inv(q, rotate(q, v));
    CHECK_NEAR(back.x, v.x, 1e-12);
    CHECK_NEAR(back.z, v.z, 1e-12);
}

TEST(adcs_math, matrix_to_quaternion_round_trips) {
    const Quat q = normalized(Quat{-0.1, 0.9, 0.3, -0.2});  // a near-180 degree case
    const Mat3 r = Mat3::from_columns(rotate(q, Vec3{1, 0, 0}), rotate(q, Vec3{0, 1, 0}),
                                      rotate(q, Vec3{0, 0, 1}));
    CHECK(angle_between(q, quat_from_matrix(r)) < 1e-9);
}

TEST(ephemeris, igrf_matches_the_simulator_implementation) {
    // Reference printed by sim/models/igrf.py, itself checked against ppigrf.
    const Vec3 b = ephem::igrf_ecef(Vec3{4.0e6, -3.5e6, 4.2e6});
    CHECK_NEAR(b.x, -3.039761e-05, 2e-11);
    CHECK_NEAR(b.y,  2.069254e-05, 2e-11);
    CHECK_NEAR(b.z, -2.923496e-06, 2e-11);
}

TEST(ephemeris, sun_direction_matches_the_simulator) {
    const Vec3 s = ephem::sun_direction(1.0e6);
    CHECK_NEAR(s.x, -0.024570981472179794, 1e-12);
    CHECK_NEAR(s.y,  0.9172001430353585, 1e-12);
    CHECK_NEAR(s.z,  0.39766841021812604, 1e-12);
}

TEST(triad, recovers_an_attitude_from_two_exact_vectors) {
    const Quat truth = normalized(Quat{0.5, 0.1, -0.7, 0.4});
    const Vec3 r1 = unit(Vec3{1, 2, 3}), r2 = unit(Vec3{-2, 0.5, 1});
    Quat est;
    CHECK(triad(rotate_inv(truth, r1), rotate_inv(truth, r2), r1, r2, est));
    CHECK(angle_between(truth, est) < 1e-9);
}

TEST(triad, refuses_nearly_parallel_vectors) {
    Quat est;
    const Vec3 a{1, 0, 0}, b{1, 0.01, 0};
    CHECK(!triad(a, b, a, b, est));
}

TEST(mekf, converges_and_learns_a_constant_gyro_bias) {
    // A body turning at orbit-ish rate, a biased gyro, two noisy vectors.
    Lcg rng;
    Quat truth = normalized(Quat{0.8, 0.2, -0.3, 0.4});
    const Vec3 w_true{0.002, -0.001, 0.0015};
    const Vec3 bias{3e-4, -2e-4, 1e-4};
    const Vec3 r1 = unit(Vec3{0.3, -0.8, 0.5}), r2 = unit(Vec3{0.9, 0.1, -0.2});

    Mekf f;
    MekfConfig cfg;
    f.init(normalized(truth * quat_from_rotvec(Vec3{0.03, -0.02, 0.04})), 3 * kDeg, 1e-3);
    const double dt = 0.1;
    for (int k = 0; k < 6000; ++k) {
        truth = normalized(truth * quat_from_rotvec(w_true * dt));
        const Vec3 gyro = w_true + bias + Vec3{rng.gauss(), rng.gauss(), rng.gauss()} * 1e-4;
        f.propagate(gyro, dt, cfg);
        const Vec3 n1{rng.gauss(), rng.gauss(), rng.gauss()};
        const Vec3 n2{rng.gauss(), rng.gauss(), rng.gauss()};
        f.update(rotate_inv(truth, r1) + n1 * 2e-3, r1, 2e-3);
        f.update(rotate_inv(truth, r2) + n2 * 2e-3, r2, 2e-3);
    }
    CHECK(angle_between(truth, f.attitude()) < 0.05 * kDeg);
    CHECK_NEAR(f.bias().x, bias.x, 2e-5);
    CHECK_NEAR(f.bias().y, bias.y, 2e-5);
    CHECK_NEAR(f.bias().z, bias.z, 2e-5);
    CHECK(f.sigma_attitude() < 0.05 * kDeg);   // and it knows it
}

TEST(mekf, a_star_tracker_update_pulls_the_estimate_to_the_measurement) {
    Mekf f;
    const Quat truth = normalized(Quat{0.2, 0.6, -0.1, 0.77});
    f.init(normalized(truth * quat_from_rotvec(Vec3{0.02, 0.0, -0.01})), 2 * kDeg, 1e-3);
    f.update_attitude(truth, Vec3{5e-5, 5e-5, 3e-4});
    CHECK(angle_between(truth, f.attitude()) < 0.01 * kDeg);
}

TEST(orbit_prop, a_circular_orbit_stays_circular_and_closes) {
    const double a = 6878137.0;
    const double v = std::sqrt(ephem::kMu / a);
    OrbitPropagator p;
    p.set_state(Vec3{a, 0, 0}, Vec3{0, v * std::cos(0.9), v * std::sin(0.9)}, 0.0);
    double rmin = a, rmax = a;
    for (int k = 1; k <= 5677; ++k) {
        p.propagate_to(static_cast<double>(k));
        const double r = norm(p.position());
        rmin = r < rmin ? r : rmin;
        rmax = r > rmax ? r : rmax;
    }
    CHECK(rmax - rmin < 30e3);        // J2 makes it slightly non-circular, no more
    CHECK(norm(p.position() - Vec3{a, 0, 0}) < 200e3);   // back near the start after a period
}

TEST(pointing, the_nadir_target_points_plus_z_at_the_earth) {
    const Vec3 r{7.0e6, 0, 0}, v{0, 7.5e3, 0};
    Vec3 w;
    const Quat qt = nadir_target(r, v, w);
    const Vec3 z = rotate(qt, Vec3{0, 0, 1});
    CHECK_NEAR(z.x, -1.0, 1e-12);
    const Vec3 x = rotate(qt, Vec3{1, 0, 0});
    CHECK_NEAR(x.y, 1.0, 1e-12);                 // +X along the velocity
    CHECK_NEAR(w.z, 7.5e3 / 7.0e6, 1e-15);       // turning once per orbit
}

TEST(pointing, torque_opposes_the_error_and_respects_the_wheel_limit) {
    const Vec3 r{7.0e6, 0, 0}, v{0, 7.5e3, 0};
    Vec3 w;
    const Quat qt = nadir_target(r, v, w);
    PointingConfig c;
    // Rotated +1 degree about body X from the target, not turning.
    const Quat q = qt * quat_from_rotvec(Vec3{1 * kDeg, 0, 0});
    const PointingOutput o = nadir_control(q, rotate_inv(q, w), Vec3{}, r, v, Vec3{}, false, c);
    CHECK(o.body_torque.x < 0.0);
    CHECK_NEAR(o.error_rad, 1 * kDeg, 1e-9);
    // A huge error saturates the wheels, never exceeds them.
    const Quat far = qt * quat_from_rotvec(Vec3{3.0, 0, 0});
    const PointingOutput o2 = nadir_control(far, Vec3{0.2, 0, 0}, Vec3{}, r, v, Vec3{}, false, c);
    CHECK(std::fabs(o2.wheel_torque.x) <= c.max_wheel_torque + 1e-15);
}

TEST(pointing, yaw_is_in_the_attitude_error_but_not_the_boresight_error) {
    const Vec3 r{7.0e6, 0, 0}, v{0, 7.5e3, 0};
    Vec3 w;
    const Quat qt = nadir_target(r, v, w);
    PointingConfig c;
    const Quat yawed = qt * quat_from_rotvec(Vec3{0, 0, 5 * kDeg});      // about the boresight
    const PointingOutput a = nadir_control(yawed, rotate_inv(yawed, w), Vec3{}, r, v, Vec3{}, false, c);
    CHECK_NEAR(a.error_rad, 5 * kDeg, 1e-9);
    CHECK_NEAR(a.boresight_rad, 0.0, 1e-9);                            // the payload still sees nadir
    const Quat tilted = qt * quat_from_rotvec(Vec3{5 * kDeg, 0, 0});    // the boresight itself
    const PointingOutput b = nadir_control(tilted, rotate_inv(tilted, w), Vec3{}, r, v, Vec3{}, false, c);
    CHECK_NEAR(b.boresight_rad, 5 * kDeg, 1e-9);
}

TEST(pointing, momentum_dumping_torque_opposes_stored_momentum) {
    const Vec3 r{7.0e6, 0, 0}, v{0, 7.5e3, 0};
    Vec3 w;
    const Quat qt = nadir_target(r, v, w);
    PointingConfig c;
    const Vec3 h{0.01, 0, 0}, b{0, 3e-5, 0};
    const PointingOutput o = nadir_control(qt, rotate_inv(qt, w), h, r, v, b, true, c);
    const Vec3 tau = cross(o.dipole, b);
    CHECK(dot(tau, h) < 0.0);     // removes momentum from the wheels
}
