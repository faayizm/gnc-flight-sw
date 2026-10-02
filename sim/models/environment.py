"""The environment: magnetic field, Sun and Earth's shadow.

Two magnetic field models are provided. `magnetic_field_eci` is the IGRF
(degree 10, see igrf.py) and is what the simulator uses. `dipole_field_eci` is
a tilted centred dipole, kept as the simple model the lessons derive by hand
and as a cross-check; it differs from the IGRF by tens of percent locally.
"""

from __future__ import annotations

import math

from .igrf import field_ecef

from .linalg import Vec, dot, scale, sub, norm
from .orbit import R_EARTH

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


def dipole_field_eci(r: Vec, t: float) -> Vec:
    """Tilted-dipole field in tesla at inertial position r (metres)."""
    rn = norm(r)
    rhat = scale(r, 1.0 / rn)
    m = dipole_axis_eci(t)
    k = MU0_OVER_4PI * DIPOLE_MOMENT / rn**3
    return scale(sub(scale(rhat, 3.0 * dot(m, rhat)), m), k)


OBLIQUITY = math.radians(23.44)
SUN_LONGITUDE_0 = math.radians(80.0)         # roughly the March equinox plus a month
SUN_RATE = math.radians(360.0 / 365.2422) / 86400.0   # rad/s along the ecliptic


def sun_direction_eci(t: float) -> Vec:
    """Unit vector from Earth to the Sun. Circular-ecliptic approximation:
    good to about a degree, which is far better than a coarse sun sensor."""
    lam = SUN_LONGITUDE_0 + SUN_RATE * t
    return (math.cos(lam),
            math.sin(lam) * math.cos(OBLIQUITY),
            math.sin(lam) * math.sin(OBLIQUITY))


def in_eclipse(r: Vec, t: float) -> bool:
    """Cylindrical Earth shadow: behind the Earth and within one Earth radius
    of the Sun-Earth line. Ignores the penumbra (a few seconds in LEO)."""
    s = sun_direction_eci(t)
    along = dot(r, s)
    if along >= 0.0:
        return False
    perp = sub(r, scale(s, along))
    return norm(perp) < R_EARTH


def magnetic_field_eci(r: Vec, t: float) -> Vec:
    """IGRF field in tesla at inertial position r (metres), inertial axes.

    The Earth-fixed frame is the inertial frame rotated about Z by the Earth
    rotation angle; Greenwich is aligned with inertial X at t = 0.
    """
    th = EARTH_RATE * t
    c, s = math.cos(th), math.sin(th)
    bx, by, bz = field_ecef(c * r[0] + s * r[1], -s * r[0] + c * r[1], r[2])
    return (c * bx - s * by, s * bx + c * by, bz)
