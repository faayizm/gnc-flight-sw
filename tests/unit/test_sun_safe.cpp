// ============================================================================
//  Tests for the sun-pointing safe-mode law (apps/adcs/sun_safe.hpp).
// ============================================================================
#include <cmath>

#include "apps/adcs/sun_safe.hpp"
#include "framework.hpp"

using namespace fsw::adcs;

namespace {
constexpr double kPi = 3.14159265358979323846;
const Vec3 kField{2.0e-5, -1.0e-5, 2.5e-5};   // tesla, a typical low-orbit field
}  // namespace

TEST(sun_safe, the_wanted_torque_leans_the_spin_towards_the_sun) {
    // Array normal -Z, spinning at exactly the commanded rate, Sun along +X.
    // The torque must be along +X, the part of the Sun across the spin axis:
    // that precesses the spin axis towards the Sun. It is not along n x s
    // (that is -Y here), which would turn a body that is not spinning; a
    // spinning one would go sideways.
    SunSafeConfig c;
    const Vec3 spinning = c.panel_normal * c.spin_rps;
    const SunSafeOutput o = sun_safe(Vec3{1, 0, 0}, true, spinning, kField, true, c);
    CHECK(o.state == SunSafeState::Pointing);
    CHECK(o.torque_wanted.x > 0.0);
    CHECK_NEAR(o.torque_wanted.y, 0.0, 1e-15);
    CHECK_NEAR(o.torque_wanted.z, 0.0, 1e-15);
    CHECK_NEAR(o.sun_angle_rad, kPi / 2.0, 1e-12);
}

TEST(sun_safe, the_coils_never_claim_torque_along_the_field) {
    SunSafeConfig c;
    const Vec3 w = c.panel_normal * c.spin_rps + Vec3{1e-3, -2e-3, 5e-4};   // small enough not to saturate
    const SunSafeOutput o = sun_safe(Vec3{0.6, 0.0, 0.8}, true, w, kField, true, c);
    CHECK_NEAR(dot(o.torque, kField) / norm(kField), 0.0, 1e-18);
    // And what they do make is the wanted torque less its part along B.
    const Vec3 bh = kField * (1.0 / norm(kField));
    const Vec3 perp = o.torque_wanted - bh * dot(o.torque_wanted, bh);
    CHECK_NEAR(norm(o.torque - perp), 0.0, 1e-18);
}

TEST(sun_safe, in_eclipse_it_only_holds_the_spin) {
    SunSafeConfig c;
    const Vec3 spinning = c.panel_normal * c.spin_rps;
    const SunSafeOutput still = sun_safe(Vec3{}, false, spinning, kField, true, c);
    CHECK(still.state == SunSafeState::Damping);
    CHECK_NEAR(norm(still.torque_wanted), 0.0, 1e-18);   // at the spin rate: nothing to do
    CHECK_NEAR(still.sun_angle_rad, -1.0, 0.0);
    const Vec3 wobble{2e-3, 0.0, -1e-3};
    const SunSafeOutput o = sun_safe(Vec3{}, false, spinning + wobble, kField, true, c);
    CHECK(dot(o.torque_wanted, wobble) < 0.0);       // opposes the error
}

TEST(sun_safe, without_a_magnetometer_the_coils_are_off) {
    SunSafeConfig c;
    const SunSafeOutput o = sun_safe(Vec3{1, 0, 0}, true, Vec3{1e-3, 0, 0}, kField, false, c);
    CHECK(o.state == SunSafeState::CoilsOff);
    CHECK_NEAR(norm(o.dipole), 0.0, 0.0);
}

TEST(sun_safe, saturation_keeps_the_dipole_direction) {
    SunSafeConfig c;
    c.rate_rps = 5.0;                               // absurdly stiff: must saturate
    const SunSafeOutput o = sun_safe(Vec3{1, 0, 0}, true, Vec3{0.1, 0.2, -0.1}, kField, true, c);
    const double peak = std::fmax(std::fabs(o.dipole.x), std::fmax(std::fabs(o.dipole.y), std::fabs(o.dipole.z)));
    CHECK_NEAR(peak, c.max_dipole, 1e-12);
    c.max_dipole = 1e9;                             // the unsaturated demand
    const SunSafeOutput u = sun_safe(Vec3{1, 0, 0}, true, Vec3{0.1, 0.2, -0.1}, kField, true, c);
    CHECK_NEAR(dot(o.dipole, u.dipole) / (norm(o.dipole) * norm(u.dipole)), 1.0, 1e-12);
}

TEST(sun_safe, a_tumbling_body_under_gravity_gradient_acquires_the_sun) {
    // Euler's equation with the law in the loop, and the two things that beat
    // the first, non-spinning version of this law: a tumble to start from,
    // and gravity gradient. The field direction turns twice per orbit, as it
    // roughly does in an inclined low orbit; the Sun is fixed in inertial
    // space, and the array (body -Z) starts facing away from it.
    SunSafeConfig c;
    const Vec3 inertia{0.10, 0.12, 0.04};
    Quat q{1.0, 0.0, 0.0, 0.0};
    Vec3 w{0.03, -0.02, 0.025};                     // about 2.5 deg/s
    const Vec3 sun_eci{0.0, 0.0, 1.0};
    const double dt = 0.1, orbit = 5700.0, n = 2.0 * kPi / orbit;
    double worst_late = 0.0;
    for (int k = 0; k < static_cast<int>(2.0 * orbit / dt); ++k) {
        const double t = k * dt;
        const double a = 2.0 * n * t;
        const Vec3 b_eci{3.0e-5 * std::cos(a), 1.0e-5, 3.0e-5 * std::sin(a)};
        const Vec3 r_eci{std::cos(n * t), 0.0, std::sin(n * t)};   // the Sun is in the orbit plane
        const Vec3 sun_b = rotate_inv(q, sun_eci);
        const SunSafeOutput o = sun_safe(sun_b, true, w, rotate_inv(q, b_eci), true, c);
        const Vec3 r = rotate_inv(q, r_eci);
        const Vec3 ir{inertia.x * r.x, inertia.y * r.y, inertia.z * r.z};
        const Vec3 iw{inertia.x * w.x, inertia.y * w.y, inertia.z * w.z};
        const Vec3 rhs = o.torque + cross(r, ir) * (3.0 * n * n) - cross(w, iw);
        w = w + Vec3{rhs.x / inertia.x, rhs.y / inertia.y, rhs.z / inertia.z} * dt;
        q = normalized(q * quat_from_rotvec(w * dt));
        if (t > 1.5 * orbit) { worst_late = std::fmax(worst_late, o.sun_angle_rad); }
    }
    CHECK(worst_late < 10.0 * kPi / 180.0);
}
