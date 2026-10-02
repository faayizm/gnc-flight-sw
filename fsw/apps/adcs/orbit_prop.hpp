// ============================================================================
//  fsw/apps/adcs/orbit_prop.hpp -- on-board orbit propagation.
//
//  The attitude chain needs to know where the spacecraft is: the magnetic
//  field reference depends on position, and the nadir target is defined by it.
//  A GPS receiver gives position directly, but only while it has a fix. When
//  it does not -- outage, antenna pointing the wrong way, receiver reset --
//  the spacecraft carries its last fix forward with this propagator.
//
//  Two-body gravity plus J2, RK4, stepped at the attitude sample rate. Over a
//  ten-minute outage in LEO that drifts by metres, which moves the magnetic
//  reference by well under a nanotesla. Drag, which dominates beyond a few
//  hours, is deliberately not modelled: outages that long belong to FDIR.
// ============================================================================
#pragma once

#include "apps/adcs/adcs_math.hpp"

namespace fsw::adcs {

class OrbitPropagator {
 public:
    void set_state(const Vec3& r, const Vec3& v, double t) {
        r_ = r; v_ = v; t_ = t; valid_ = true;
    }

    // Advance to time t. Steps of at most kMaxStepS keep RK4 accurate however
    // long the gap since the last call.
    void propagate_to(double t);

    bool        valid() const { return valid_; }
    const Vec3& position() const { return r_; }
    const Vec3& velocity() const { return v_; }
    double      time() const { return t_; }

    static Vec3 acceleration(const Vec3& r);

 private:
    static constexpr double kMaxStepS = 1.0;
    void step(double dt);

    Vec3   r_{}, v_{};
    double t_ = 0.0;
    bool   valid_ = false;
};

}  // namespace fsw::adcs
