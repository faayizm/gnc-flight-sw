"""The International Geomagnetic Reference Field, synthesised from its Gauss
coefficients (degree 10, epoch 2025.0 -- see igrf_coeffs.py).

    V(r, th, ph) = a  SUM_n SUM_m (a/r)^(n+1) (g cos m ph + h sin m ph) P_n^m(cos th)
    B = -grad V

with Schmidt quasi-normalised associated Legendre functions P_n^m, reference
radius a = 6371.2 km, and r, th, ph geocentric spherical coordinates in the
Earth-fixed frame. Pure Python, no dependencies.

Why degree 10: at 500 km the degree 11-13 terms are well under 100 nT, below
the 50 nT sensor noise floor of the magnetometer modelled here, and every extra
degree costs about 20 % more per call.
"""

from __future__ import annotations

import math

from .igrf_coeffs import GAUSS

A_REF = 6371.2e3          # m
NMAX = max(n for n, _ in GAUSS)


def _legendre(ct: float, st: float):
    """Schmidt-normalised P[n][m] and dP[n][m]/d(theta), 0 <= m <= n <= NMAX."""
    p = [[0.0] * (NMAX + 1) for _ in range(NMAX + 1)]
    dp = [[0.0] * (NMAX + 1) for _ in range(NMAX + 1)]
    p[0][0] = 1.0
    for n in range(1, NMAX + 1):
        if n == 1:
            p[1][1], dp[1][1] = st, ct
        else:
            k = math.sqrt((2 * n - 1) / (2.0 * n))
            p[n][n] = k * st * p[n - 1][n - 1]
            dp[n][n] = k * (ct * p[n - 1][n - 1] + st * dp[n - 1][n - 1])
        for m in range(0, n):
            d = math.sqrt(n * n - m * m)
            c = (2 * n - 1)
            e = math.sqrt((n - 1) ** 2 - m * m) if n - 2 >= m else 0.0
            p2 = p[n - 2][m] if n - 2 >= m else 0.0
            dp2 = dp[n - 2][m] if n - 2 >= m else 0.0
            p[n][m] = (c * ct * p[n - 1][m] - e * p2) / d
            dp[n][m] = (c * (ct * dp[n - 1][m] - st * p[n - 1][m]) - e * dp2) / d
    return p, dp


def field_ecef(x: float, y: float, z: float):
    """Field in tesla at an Earth-fixed position (metres), Earth-fixed axes."""
    r = math.sqrt(x * x + y * y + z * z)
    ct = z / r
    st = max(math.sqrt(1.0 - ct * ct), 1e-9)      # guard the exact poles
    ph = math.atan2(y, x)
    p, dp = _legendre(ct, st)

    br = bt = bp = 0.0
    ar = A_REF / r
    ar_n = ar * ar                                # (a/r)^(n+2), starting at n = 0
    cos_m = [math.cos(m * ph) for m in range(NMAX + 1)]
    sin_m = [math.sin(m * ph) for m in range(NMAX + 1)]
    for n in range(1, NMAX + 1):
        ar_n *= ar
        for m in range(0, n + 1):
            g, h = GAUSS[(n, m)]
            c = g * cos_m[m] + h * sin_m[m]
            s = -g * sin_m[m] + h * cos_m[m]
            br += (n + 1) * ar_n * c * p[n][m]
            bt -= ar_n * c * dp[n][m]
            bp -= ar_n * m * s * p[n][m] / st
    # spherical (r, theta, phi) components -> Cartesian, nT -> T
    sp, cp = math.sin(ph), math.cos(ph)
    bx = br * st * cp + bt * ct * cp - bp * sp
    by = br * st * sp + bt * ct * sp + bp * cp
    bz = br * ct - bt * st
    return (bx * 1e-9, by * 1e-9, bz * 1e-9)
