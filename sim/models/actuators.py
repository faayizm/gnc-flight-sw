"""Magnetorquers.

A magnetorquer produces a magnetic dipole m; the torque is  tau = m x B.  That
cross product is the whole physical constraint: the torque is always
perpendicular to the local field, so no torquer can ever torque the satellite
about the field direction. It is computed here from the true field and the
actual (limited, quantised) dipole -- never from what the flight software
believes.
"""

from __future__ import annotations

from .linalg import Vec, cross
from .sensors import quantise


class Magnetorquers:
    def __init__(self, max_dipole: float = 0.2, bits: int = 12):
        self.max = max_dipole
        self.step = max_dipole / (2 ** (bits - 1) - 1)

    def dipole(self, command: Vec) -> Vec:
        return tuple(quantise(max(-self.max, min(self.max, c)), self.step) for c in command)  # type: ignore[return-value]

    def torque(self, command: Vec, b_body: Vec) -> Vec:
        return cross(self.dipole(command), b_body)
