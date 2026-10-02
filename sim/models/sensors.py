"""Sensor models. Each adds the imperfections that make estimation necessary.

Everything draws from a single seeded random.Random handed in by the scenario,
so a run is reproducible bit for bit.
"""

from __future__ import annotations

import random

import math

from .linalg import Quat, Vec, dot, norm, q_mul, q_normalize, scale


def quantise(x: float, step: float) -> float:
    return round(x / step) * step


class Magnetometer:
    """Three-axis magnetometer: white noise, quantisation, range limit."""

    def __init__(self, rng: random.Random, noise_t: float = 50e-9,
                 resolution_t: float = 10e-9, range_t: float = 200e-6):
        self.rng, self.noise, self.res, self.range = rng, noise_t, resolution_t, range_t

    def read(self, b_body: Vec) -> Vec:
        out = []
        for c in b_body:
            v = c + self.rng.gauss(0.0, self.noise)
            v = max(-self.range, min(self.range, v))
            out.append(quantise(v, self.res))
        return (out[0], out[1], out[2])


class Gyro:
    """Rate gyro: white noise plus a bias that random-walks.

    The bias is what the estimator of Phase 3 exists to remove. Detumbling does
    not need it, but it is modelled from the start so the data the later
    estimator sees is already realistic.
    """

    def __init__(self, rng: random.Random, noise_rps: float = 1.0e-4,
                 bias_walk_rps_per_rt_s: float = 1.0e-6,
                 initial_bias_rps: float = 3.0e-4, resolution_rps: float = 1.0e-5):
        self.rng, self.noise, self.walk, self.res = rng, noise_rps, bias_walk_rps_per_rt_s, resolution_rps
        self.bias = [rng.uniform(-initial_bias_rps, initial_bias_rps) for _ in range(3)]

    def read(self, omega: Vec, dt: float) -> Vec:
        out = []
        for i in range(3):
            self.bias[i] += self.rng.gauss(0.0, self.walk * dt**0.5)
            out.append(quantise(omega[i] + self.bias[i] + self.rng.gauss(0.0, self.noise), self.res))
        return (out[0], out[1], out[2])


class SunSensor:
    """Six face-mounted cosine-law detectors, one on each body face.

    Each face reads max(0, s . n) for sun direction s and face normal n, plus
    noise. The sensor electronics combine opposite faces into a body-frame sun
    vector, and flag it invalid when no face sees enough sun. That happens in
    eclipse (all faces read zero) -- the 'eclipse blindness' the estimator must
    live with -- and also for any geometry where the sun grazes every face.
    """

    def __init__(self, rng: random.Random, noise: float = 0.01, threshold: float = 0.15):
        self.rng, self.noise, self.threshold = rng, noise, threshold

    def read(self, sun_body: Vec, eclipsed: bool):
        """Returns (sun vector, valid)."""
        if eclipsed:
            return (0.0, 0.0, 0.0), False
        comps = []
        peak = 0.0
        for c in sun_body:
            pos = max(0.0, c) + self.rng.gauss(0.0, self.noise)
            neg = max(0.0, -c) + self.rng.gauss(0.0, self.noise)
            pos, neg = max(0.0, pos), max(0.0, neg)
            peak = max(peak, pos, neg)
            comps.append(pos - neg)
        n = sum(c * c for c in comps) ** 0.5
        if peak < self.threshold or n == 0.0:
            return (0.0, 0.0, 0.0), False
        return (comps[0] / n, comps[1] / n, comps[2] / n), True


class Gps:
    """GPS receiver: position and velocity with white noise, one fix per
    second. `outages` is a list of (start, end) times with no fix."""

    def __init__(self, rng: random.Random, pos_noise_m: float = 10.0,
                 vel_noise_mps: float = 0.1, period_s: float = 1.0, outages=()):
        self.rng, self.pn, self.vn, self.period = rng, pos_noise_m, vel_noise_mps, period_s
        self.outages = list(outages)
        self.next_fix = 0.0

    def read(self, r: Vec, v: Vec, t: float):
        """Returns (pos, vel, valid). Always draws its noise, so an outage does
        not shift every later random number and change the rest of the run."""
        pos = tuple(c + self.rng.gauss(0.0, self.pn) for c in r)
        vel = tuple(c + self.rng.gauss(0.0, self.vn) for c in v)
        due = t + 1e-9 >= self.next_fix
        if due:
            self.next_fix += self.period
        if not due or any(a <= t < b for a, b in self.outages):
            return (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), False
        return pos, vel, True


class StarTracker:
    """Star tracker: attitude quaternion from a star field, the sensor that
    makes sub-0.1-degree pointing possible.

    Boresight along body -Z (zenith, when the spacecraft points +Z at nadir).
    Accuracy is anisotropic, as it is for every real tracker: stars pin the
    directions across the boresight to arcseconds, while rotation *about* the
    boresight is several times worse.

    No output when:
      * the Sun is within `sun_exclusion_deg` of the boresight (stray light)
      * the Earth's lit limb is within `earth_exclusion_deg` of the field of view
      * the body rate exceeds `max_rate_dps` (stars smear across the detector)
    The first of these happens near orbit noon for some geometries, and is why
    the estimator must still be able to coast on the gyro and the coarse
    sensors.
    """

    def __init__(self, rng: random.Random, sigma_cross_arcsec: float = 10.0,
                 sigma_roll_arcsec: float = 60.0, period_s: float = 0.5,
                 sun_exclusion_deg: float = 30.0, earth_exclusion_deg: float = 20.0,
                 max_rate_dps: float = 1.0):
        self.rng = rng
        self.sc = math.radians(sigma_cross_arcsec / 3600.0)
        self.sr = math.radians(sigma_roll_arcsec / 3600.0)
        self.period = period_s
        self.sun_ex = math.cos(math.radians(sun_exclusion_deg))
        self.earth_ex = math.radians(earth_exclusion_deg)
        self.max_rate = math.radians(max_rate_dps)
        self.next_fix = 0.0
        self.boresight = (0.0, 0.0, -1.0)

    def read(self, q: Quat, omega: Vec, sun_body: Vec, r_body: Vec, eclipsed: bool, t: float):
        """Returns (quaternion, valid). Noise is always drawn, valid or not,
        so availability never perturbs the random sequence of later draws."""
        d = (self.rng.gauss(0.0, self.sc), self.rng.gauss(0.0, self.sc), self.rng.gauss(0.0, self.sr))
        due = t + 1e-9 >= self.next_fix
        if due:
            self.next_fix += self.period
        if not due or norm(omega) > self.max_rate:
            return (1.0, 0.0, 0.0, 0.0), False
        if not eclipsed and dot(sun_body, self.boresight) > self.sun_ex:
            return (1.0, 0.0, 0.0, 0.0), False
        rn = norm(r_body)
        nadir = scale(r_body, -1.0 / rn)
        earth_radius_angle = math.asin(min(1.0, 6378137.0 / rn))
        off_nadir = math.acos(max(-1.0, min(1.0, dot(nadir, self.boresight))))
        if off_nadir < earth_radius_angle + self.earth_ex:
            return (1.0, 0.0, 0.0, 0.0), False
        a = math.sqrt(sum(c * c for c in d))
        k = math.sin(a / 2) / a if a > 0 else 0.5
        dq = (math.cos(a / 2), d[0] * k, d[1] * k, d[2] * k)
        qm = q_normalize(q_mul(q, dq))
        if qm[0] < 0:
            qm = (-qm[0], -qm[1], -qm[2], -qm[3])
        return qm, True
