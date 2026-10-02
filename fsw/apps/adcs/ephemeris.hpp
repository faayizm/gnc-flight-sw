// ============================================================================
//  fsw/apps/adcs/ephemeris.hpp -- what the spacecraft knows about the sky.
//
//  An attitude estimator compares directions measured in the body frame with
//  the same directions known in the inertial frame. These functions supply the
//  inertial side: where the Sun is, what the magnetic field should be at the
//  spacecraft's position, and whether the Earth is in the way.
//
//  TIME. Every function takes `t`, seconds since the mission epoch, which in
//  this build comes from the sensor frame's timestamp (a GPS receiver would
//  supply it on a real spacecraft). The Earth rotation angle is zero at t = 0.
//
//  MODEL ERROR. The on-board magnetic model is the same IGRF the simulator
//  uses, to the same degree. On a real mission the on-board model differs
//  from the true field by 100 nT or more (crustal fields, external currents,
//  secular variation) -- an error the simulator does not yet impose. It is
//  the most flattering simplification in the whole attitude chain, and it is
//  written down here so nobody mistakes the resulting accuracy for a real one.
// ============================================================================
#pragma once

#include "apps/adcs/adcs_math.hpp"

namespace fsw::adcs::ephem {

constexpr double kEarthRadiusM  = 6378137.0;
constexpr double kMu            = 3.986004418e14;
constexpr double kJ2            = 1.08262668e-3;
constexpr double kEarthRateRps  = 7.2921159e-5;

// Unit vector from the Earth to the Sun, inertial frame. About 1 degree.
Vec3 sun_direction(double t);

// Geomagnetic field (tesla, inertial frame) at inertial position r (metres).
Vec3 magnetic_field(const Vec3& r_eci, double t);

// Same, in Earth-fixed axes at an Earth-fixed position. Exposed for testing.
Vec3 igrf_ecef(const Vec3& r_ecef);

// Cylindrical shadow model, matching the simulator's.
bool in_eclipse(const Vec3& r_eci, double t);

}  // namespace fsw::adcs::ephem
