"""The world: truth state, sensors, actuators, and the lockstep loop.

The simulator owns the truth. The flight software gets only the sensor frame.
"""

from __future__ import annotations

import math
import random
from dataclasses import dataclass, field

from ..models.actuators import Magnetorquers
from ..models.dynamics import RigidBody
from ..models.environment import magnetic_field_eci
from ..models.linalg import Quat, Vec, norm, q_normalize, rotate_inv
from ..models.orbit import Orbit
from ..models.sensors import Gyro, Magnetometer
from .bridge import Bridge

DEG = math.pi / 180.0


@dataclass
class Scenario:
    name: str
    seed: int
    duration_s: float
    tumble_dps: float = 10.0
    altitude_m: float = 500e3
    inclination_deg: float = 51.6
    inertia: Vec = (0.10, 0.12, 0.04)       # kg*m^2, roughly a 6U CubeSat
    dt: float = 0.1                          # sensor sample period
    faults: dict = field(default_factory=dict)


class Simulation:
    def __init__(self, sc: Scenario, bridge: Bridge):
        self.sc, self.bridge = sc, bridge
        rng = random.Random(sc.seed)               # every random draw comes from here
        axis = [rng.gauss(0, 1) for _ in range(3)]
        n = math.sqrt(sum(a * a for a in axis))
        omega = tuple(a / n * sc.tumble_dps * DEG for a in axis)
        q: Quat = q_normalize(tuple(rng.gauss(0, 1) for _ in range(4)))  # type: ignore[arg-type]

        self.body = RigidBody(sc.inertia, q, omega)  # type: ignore[arg-type]
        self.orbit = Orbit(sc.altitude_m, sc.inclination_deg)
        self.mag = Magnetometer(rng)
        self.gyro = Gyro(rng)
        self.mtq = Magnetorquers()
        self.t = 0.0
        self.seq = 0
        self.dipole: Vec = (0.0, 0.0, 0.0)
        self.commanded = False

    # -- truth, for assertions only; never sent to the flight software --------
    @property
    def rate_dps(self) -> float:
        return norm(self.body.omega) / DEG

    def step(self) -> None:
        dt = self.sc.dt
        b_eci = magnetic_field_eci(self.orbit.r, self.t)
        b_body = rotate_inv(self.body.q, b_eci)

        self.seq += 1
        mag = self.mag.read(b_body)
        gyro = self.gyro.read(self.body.omega, dt)
        self.dipole, self.commanded = self.bridge.exchange(self.seq, self.t, mag, gyro)

        tau = self.mtq.torque(self.dipole, b_body)
        self.body.step(tau, dt)
        self.orbit.step(dt)
        self.t += dt
