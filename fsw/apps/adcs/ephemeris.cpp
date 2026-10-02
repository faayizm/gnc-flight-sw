// ============================================================================
//  fsw/apps/adcs/ephemeris.cpp
// ============================================================================
#include "apps/adcs/ephemeris.hpp"

#include <cmath>

#include "generated/igrf_coeffs.hpp"

namespace fsw::adcs::ephem {

namespace {
constexpr double kPi            = 3.14159265358979323846;
constexpr double kObliquity     = 23.44 * kPi / 180.0;
constexpr double kSunLon0       = 80.0 * kPi / 180.0;
constexpr double kSunRate       = (2.0 * kPi / 365.2422) / 86400.0;
constexpr int    N              = igrf::kMaxDegree;
}  // namespace

Vec3 sun_direction(double t) {
    const double lam = kSunLon0 + kSunRate * t;
    return {std::cos(lam), std::sin(lam) * std::cos(kObliquity),
            std::sin(lam) * std::sin(kObliquity)};
}

bool in_eclipse(const Vec3& r, double t) {
    const Vec3 s = sun_direction(t);
    const double along = dot(r, s);
    if (along >= 0.0) { return false; }
    return norm(r - s * along) < kEarthRadiusM;
}

// Spherical-harmonic synthesis, B = -grad V. See sim/models/igrf.py for the
// same algorithm in Python, which is checked against an independent
// implementation; tests/unit/test_adcs_estimation.cpp checks this one against
// values produced by that Python.
Vec3 igrf_ecef(const Vec3& p) {
    const double r  = norm(p);
    const double ct = p.z / r;
    double st = std::sqrt(1.0 - ct * ct);
    if (st < 1e-9) { st = 1e-9; }
    const double ph = std::atan2(p.y, p.x);

    double P[N + 1][N + 1] = {};
    double dP[N + 1][N + 1] = {};
    P[0][0] = 1.0;
    for (int n = 1; n <= N; ++n) {
        if (n == 1) {
            P[1][1] = st;
            dP[1][1] = ct;
        } else {
            const double k = std::sqrt((2.0 * n - 1.0) / (2.0 * n));
            P[n][n] = k * st * P[n - 1][n - 1];
            dP[n][n] = k * (ct * P[n - 1][n - 1] + st * dP[n - 1][n - 1]);
        }
        for (int m = 0; m < n; ++m) {
            const double d  = std::sqrt(static_cast<double>(n * n - m * m));
            const double c  = 2.0 * n - 1.0;
            const bool   two = (n - 2 >= m);
            const double e  = two ? std::sqrt(static_cast<double>((n - 1) * (n - 1) - m * m)) : 0.0;
            const double p2 = two ? P[n - 2][m] : 0.0;
            const double d2 = two ? dP[n - 2][m] : 0.0;
            P[n][m]  = (c * ct * P[n - 1][m] - e * p2) / d;
            dP[n][m] = (c * (ct * dP[n - 1][m] - st * P[n - 1][m]) - e * d2) / d;
        }
    }

    double br = 0.0, bt = 0.0, bp = 0.0;
    const double ar = igrf::kReferenceRadiusM / r;
    double arn = ar * ar;
    for (int n = 1; n <= N; ++n) {
        arn *= ar;
        for (int m = 0; m <= n; ++m) {
            const double cm = std::cos(m * ph);
            const double sm = std::sin(m * ph);
            const double g = igrf::kG[n][m];
            const double h = igrf::kH[n][m];
            const double c = g * cm + h * sm;
            const double s = -g * sm + h * cm;
            br += (n + 1) * arn * c * P[n][m];
            bt -= arn * c * dP[n][m];
            bp -= arn * m * s * P[n][m] / st;
        }
    }
    const double sp = std::sin(ph), cp = std::cos(ph);
    return Vec3{br * st * cp + bt * ct * cp - bp * sp,
                br * st * sp + bt * ct * sp + bp * cp,
                br * ct - bt * st} * 1e-9;
}

Vec3 magnetic_field(const Vec3& r, double t) {
    const double th = kEarthRateRps * t;
    const double c = std::cos(th), s = std::sin(th);
    const Vec3 b = igrf_ecef(Vec3{c * r.x + s * r.y, -s * r.x + c * r.y, r.z});
    return {c * b.x - s * b.y, s * b.x + c * b.y, b.z};
}

}  // namespace fsw::adcs::ephem
