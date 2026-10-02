"""Orbit propagation: two-body gravity plus the J2 oblateness term.

Integrated with RK4 in the inertial frame. J2 is the one perturbation that
matters over days in low Earth orbit (it precesses the orbital plane); drag and
third bodies come later, if ever.
"""

from __future__ import annotations

import math

from .linalg import Vec, add, norm, scale

MU = 3.986004418e14        # m^3/s^2, Earth gravitational parameter
R_EARTH = 6378137.0        # m, equatorial radius
J2 = 1.08262668e-3


def accel(r: Vec) -> Vec:
    x, y, z = r
    rn = norm(r)
    k = -MU / rn**3
    f = -1.5 * J2 * MU * R_EARTH**2 / rn**5
    zz = 5.0 * z * z / (rn * rn)
    return (x * (k + f * (1.0 - zz)),
            y * (k + f * (1.0 - zz)),
            z * (k + f * (3.0 - zz)))


class Orbit:
    """State is (position, velocity) in the inertial frame, metres and m/s."""

    def __init__(self, altitude_m: float, inclination_deg: float,
                 raan_deg: float = 0.0, true_anomaly_deg: float = 0.0):
        a = R_EARTH + altitude_m
        v = math.sqrt(MU / a)
        i = math.radians(inclination_deg)
        o = math.radians(raan_deg)
        u = math.radians(true_anomaly_deg)
        # Circular orbit: position and velocity in the orbital plane, rotated
        # by inclination about X and RAAN about Z.
        pq = (a * math.cos(u), a * math.sin(u), 0.0)
        vq = (-v * math.sin(u), v * math.cos(u), 0.0)

        def to_eci(p: Vec) -> Vec:
            x1, y1, z1 = p[0], p[1] * math.cos(i), p[1] * math.sin(i)
            return (x1 * math.cos(o) - y1 * math.sin(o),
                    x1 * math.sin(o) + y1 * math.cos(o), z1)

        self.r = to_eci(pq)
        self.v = to_eci(vq)
        self.period_s = 2.0 * math.pi * math.sqrt(a**3 / MU)

    def step(self, dt: float) -> None:
        r, v = self.r, self.v
        k1r, k1v = v, accel(r)
        k2r = add(v, scale(k1v, dt / 2))
        k2v = accel(add(r, scale(k1r, dt / 2)))
        k3r = add(v, scale(k2v, dt / 2))
        k3v = accel(add(r, scale(k2r, dt / 2)))
        k4r = add(v, scale(k3v, dt))
        k4v = accel(add(r, scale(k3r, dt)))
        self.r = add(r, scale(add(add(k1r, scale(k2r, 2)), add(scale(k3r, 2), k4r)), dt / 6))
        self.v = add(v, scale(add(add(k1v, scale(k2v, 2)), add(scale(k3v, 2), k4v)), dt / 6))
