"""Actuators: magnetorquers and reaction wheels.

MAGNETORQUERS.

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


class ReactionWheels:
    """Three orthogonal wheels, one per body axis.

    Limits that matter to control design:
      * torque: the motor can only push so hard (max_torque)
      * momentum: the wheel can only spin so fast (max_momentum). A saturated
        wheel cannot absorb any more momentum in that direction, which is why
        momentum must be dumped by the magnetorquers before it gets there.
      * friction: Coulomb plus viscous, opposing the wheel's spin. The drive
        electronics compensate it from their own tachometer, but only to
        `compensation` -- the residual reaches the body as a disturbance the
        attitude controller has to reject. The Coulomb part flips sign as a
        wheel passes through zero speed, which is the classic source of
        pointing glitches on wheel-controlled spacecraft.

    State is each wheel's angular momentum, in N*m*s, body frame.
    """

    def __init__(self, max_torque: float = 2.0e-3, max_momentum: float = 0.03,
                 wheel_inertia: float = 3.0e-5, coulomb: float = 5.0e-6,
                 viscous: float = 5.0e-9, compensation: float = 0.9,
                 tach_resolution: float = 1.0e-6):
        self.max_torque, self.max_h, self.j = max_torque, max_momentum, wheel_inertia
        self.coulomb, self.viscous, self.comp = coulomb, viscous, compensation
        self.tach = tach_resolution
        self.h = [0.0, 0.0, 0.0]
        self.failed = [False, False, False]
        self.powered = True       # the WHEELS power rail

    def friction(self, h: float) -> float:
        speed = h / self.j
        sign = (speed > 0) - (speed < 0)
        return (1.0 - self.comp) * (self.coulomb * sign + self.viscous * speed)

    def momentum_rate(self, command: Vec) -> Vec:
        """dh/dt of each wheel over the next step, given the motor torque demand."""
        out = []
        for i in range(3):
            if self.failed[i] or not self.powered:
                # A dead or unpowered drive: no motor torque, and no friction
                # compensation -- that needs the drive electronics.
                speed = self.h[i] / self.j
                sign = (speed > 0) - (speed < 0)
                out.append(-(self.coulomb * sign + self.viscous * speed))
                continue
            t = max(-self.max_torque, min(self.max_torque, command[i]))
            if (self.h[i] >= self.max_h and t > 0) or (self.h[i] <= -self.max_h and t < 0):
                t = 0.0          # saturated: the motor cannot spin it any faster
            out.append(t - self.friction(self.h[i]))
        return (out[0], out[1], out[2])

    def advance(self, hdot: Vec, dt: float) -> None:
        for i in range(3):
            self.h[i] += hdot[i] * dt

    def measured(self) -> Vec:
        return tuple(round(h / self.tach) * self.tach for h in self.h)  # type: ignore[return-value]
