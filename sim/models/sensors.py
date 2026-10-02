"""Sensor models. Each adds the imperfections that make estimation necessary.

Everything draws from a single seeded random.Random handed in by the scenario,
so a run is reproducible bit for bit.
"""

from __future__ import annotations

import random

from .linalg import Vec


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
