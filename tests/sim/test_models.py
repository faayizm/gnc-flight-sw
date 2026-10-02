#!/usr/bin/env python3
"""Checks on the simulator's physics models. No flight software involved.

A simulator that is wrong silently makes every scenario meaningless, so the
models are tested against things that must be true: conserved quantities, and
reference values from an independent IGRF implementation.

Run:  make test-sim
"""

import math
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT))

from sim.models.dynamics import RigidBody                      # noqa: E402
from sim.models.environment import (dipole_field_eci, in_eclipse,   # noqa: E402
                                    magnetic_field_eci, sun_direction_eci)
from sim.models.igrf import field_ecef                         # noqa: E402
from sim.models.linalg import norm, rotate, rotate_inv         # noqa: E402
from sim.models.orbit import Orbit, MU                         # noqa: E402

fails = 0


def check(ok, what):
    global fails
    print(f"  {'.' if ok else 'x'}  {what}")
    fails += not ok


def test_rigid_body():
    print("[rigid body]")
    rb = RigidBody((0.10, 0.12, 0.04), (1, 0, 0, 0), (0.1, 0.05, 0.2))
    h0, e0 = norm(rb.angular_momentum()), rb.kinetic_energy()
    for _ in range(20000):                       # 2000 s of torque-free tumbling
        rb.step((0.0, 0.0, 0.0), 0.1)
    check(abs(norm(rb.angular_momentum()) - h0) / h0 < 1e-9, "|H| is conserved with no torque")
    check(abs(rb.kinetic_energy() - e0) / e0 < 1e-9, "kinetic energy is conserved with no torque")
    check(abs(math.sqrt(sum(c * c for c in rb.q)) - 1.0) < 1e-12, "the quaternion stays unit length")
    v = (0.3, -0.5, 0.8)
    check(norm(tuple(a - b for a, b in zip(rotate_inv(rb.q, rotate(rb.q, v)), v))) < 1e-12,
          "rotate_inv undoes rotate")


def test_orbit():
    print("[orbit]")
    o = Orbit(500e3, 51.6)
    e0 = 0.5 * norm(o.v) ** 2 - MU / norm(o.r)
    for _ in range(5677):
        o.step(1.0)
    e1 = 0.5 * norm(o.v) ** 2 - MU / norm(o.r)
    check(abs(e1 - e0) / abs(e0) < 1e-3, "energy is nearly conserved over an orbit (J2 exchanges a little)")
    check(abs(norm(o.r) - 6878137.0) < 15e3, "a circular orbit stays circular to within J2's oblateness")


# Reference values from the independent `ppigrf` package, IGRF-14 at 2025.0,
# full degree 13. (lat deg, lon deg, altitude km) -> total field in nT.
IGRF_REFERENCE = [
    (0, 0, 500, 24259), (45, 100, 500, 45286), (-60, -30, 500, 25522),
    (80, 20, 500, 45123), (10, 250, 0, 34025), (-45, 170, 500, 46120),
]


def test_igrf():
    print("[IGRF]")
    worst = 0.0
    for lat, lon, alt, ref in IGRF_REFERENCE:
        r = (6371.2 + alt) * 1e3
        la, lo = math.radians(lat), math.radians(lon)
        b = field_ecef(r * math.cos(la) * math.cos(lo), r * math.cos(la) * math.sin(lo), r * math.sin(la))
        worst = max(worst, abs(norm(b) * 1e9 - ref))
    check(worst < 50.0, f"total field matches the reference to {worst:.0f} nT (limit 50; degree-10 truncation)")

    # Maxwell: a magnetic field has no divergence. Central differences, 1 km.
    r0, h = (5.0e6, 3.0e6, 3.5e6), 1000.0
    div = 0.0
    for axis in range(3):
        up = list(r0); dn = list(r0)
        up[axis] += h; dn[axis] -= h
        div += (field_ecef(*up)[axis] - field_ecef(*dn)[axis]) / (2 * h)
    scale = norm(field_ecef(*r0)) / norm(r0)
    check(abs(div) < 1e-3 * scale, "the field is divergence-free (a potential field, differentiated correctly)")

    r = (6.878e6 * 0.8, 6.878e6 * 0.5, 6.878e6 * 0.33)
    b_igrf, b_dip = magnetic_field_eci(r, 0.0), dipole_field_eci(r, 0.0)
    ratio = norm(b_igrf) / norm(b_dip)
    check(0.6 < ratio < 1.6, f"IGRF and the dipole agree on magnitude to within tens of percent ({ratio:.2f})")


def test_sun():
    print("[sun and eclipse]")
    check(abs(norm(sun_direction_eci(1.0e6)) - 1.0) < 1e-12, "the sun direction is a unit vector")
    s = sun_direction_eci(0.0)
    r = (-6.9e6 * s[0], -6.9e6 * s[1], -6.9e6 * s[2])
    check(in_eclipse(r, 0.0), "a point directly behind the Earth is in eclipse")
    check(not in_eclipse((6.9e6 * s[0], 6.9e6 * s[1], 6.9e6 * s[2]), 0.0), "a point on the sunward side is not")


if __name__ == "__main__":
    test_rigid_body(); test_orbit(); test_igrf(); test_sun()
    print(f"\n{'FAILED: ' + str(fails) if fails else 'all model checks passed'}")
    sys.exit(1 if fails else 0)
