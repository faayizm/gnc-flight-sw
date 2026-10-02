// ============================================================================
//  fsw/apps/adcs/orbit_prop.cpp
// ============================================================================
#include "apps/adcs/orbit_prop.hpp"

#include "apps/adcs/ephemeris.hpp"

namespace fsw::adcs {

Vec3 OrbitPropagator::acceleration(const Vec3& r) {
    const double rn = norm(r);
    const double k  = -ephem::kMu / (rn * rn * rn);
    const double f  = -1.5 * ephem::kJ2 * ephem::kMu * ephem::kEarthRadiusM *
                      ephem::kEarthRadiusM / (rn * rn * rn * rn * rn);
    const double zz = 5.0 * r.z * r.z / (rn * rn);
    return {r.x * (k + f * (1.0 - zz)), r.y * (k + f * (1.0 - zz)), r.z * (k + f * (3.0 - zz))};
}

void OrbitPropagator::step(double dt) {
    const Vec3 k1r = v_,                     k1v = acceleration(r_);
    const Vec3 k2r = v_ + k1v * (dt / 2),    k2v = acceleration(r_ + k1r * (dt / 2));
    const Vec3 k3r = v_ + k2v * (dt / 2),    k3v = acceleration(r_ + k2r * (dt / 2));
    const Vec3 k4r = v_ + k3v * dt,          k4v = acceleration(r_ + k3r * dt);
    r_ = r_ + (k1r + 2.0 * k2r + 2.0 * k3r + k4r) * (dt / 6);
    v_ = v_ + (k1v + 2.0 * k2v + 2.0 * k3v + k4v) * (dt / 6);
    t_ += dt;
}

void OrbitPropagator::propagate_to(double t) {
    if (!valid_) { return; }
    while (t - t_ > 1e-9) {
        const double dt = (t - t_ > kMaxStepS) ? kMaxStepS : (t - t_);
        step(dt);
    }
}

}  // namespace fsw::adcs
