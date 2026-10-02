"""The magnetic field.

A tilted, rotating centred dipole -- NOT the IGRF. The roadmap lists IGRF; this
is the honest stand-in until then. A dipole gets the things detumbling cares
about right: the field magnitude (about 25-50 uT in LEO), its roughly 2:1
variation between equator and poles, and the way its direction sweeps in the
inertial frame around an orbit. It misses the higher-order terms, which change
the field by up to tens of percent locally.
"""

from __future__ import annotations

import math

from .linalg import Vec, dot, scale, sub, norm

DIPOLE_MOMENT = 7.94e22          # A*m^2
MU0_OVER_4PI = 1.0e-7
EARTH_RATE = 7.2921159e-5        # rad/s
POLE_COLATITUDE = math.radians(9.4)     # geomagnetic north pole, ~80.6 N
POLE_LONGITUDE = math.radians(-72.7)


def dipole_axis_eci(t: float) -> Vec:
    """Unit vector along the dipole moment (pointing to geographic south) at time t.

    The axis is fixed to the rotating Earth, so in the inertial frame it
    precesses about Z once per sidereal day. Greenwich is aligned with inertial
    X at t = 0.
    """
    lon = POLE_LONGITUDE + EARTH_RATE * t
    north = (math.sin(POLE_COLATITUDE) * math.cos(lon),
             math.sin(POLE_COLATITUDE) * math.sin(lon),
             math.cos(POLE_COLATITUDE))
    return scale(north, -1.0)


def magnetic_field_eci(r: Vec, t: float) -> Vec:
    """Field in tesla at inertial position r (metres)."""
    rn = norm(r)
    rhat = scale(r, 1.0 / rn)
    m = dipole_axis_eci(t)
    k = MU0_OVER_4PI * DIPOLE_MOMENT / rn**3
    return scale(sub(scale(rhat, 3.0 * dot(m, rhat)), m), k)
