"""The world: truth state, sensors, actuators, and the lockstep loop.

The simulator owns the truth. The flight software gets only the sensor frame.
"""

from __future__ import annotations

import math
import random
from dataclasses import dataclass, field

from ..models.actuators import Magnetorquers, ReactionWheels
from ..models.dynamics import RigidBody
from ..models.environment import (gravity_gradient_torque, in_eclipse, magnetic_field_eci,
                                  sun_direction_eci)
from ..models.linalg import (Quat, Vec, add, cross, dot, norm, q_normalize, rotate,
                             rotate_inv, scale, unit)
from ..models.orbit import Orbit
from ..models.power import PowerSystem
from ..models.sensors import Gps, Gyro, Magnetometer, StarTracker, SunSensor
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
    gps_outages: list = field(default_factory=list)    # [(start, end), ...] seconds
    wheel_failures: dict = field(default_factory=dict)  # {axis: time}
    initial_soc: float = 0.8
    stuck_heater: tuple | None = None    # (start time, extra watts)


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
        self.sun = SunSensor(rng)
        self.gps = Gps(rng, outages=sc.gps_outages)
        self.star = StarTracker(rng)
        self.star_valid_samples = 0
        self.mtq = Magnetorquers()
        self.wheels = ReactionWheels()
        self.power = PowerSystem(soc=sc.initial_soc)
        self.t = 0.0
        self.seq = 0
        self.dipole: Vec = (0.0, 0.0, 0.0)
        self.wheel_cmd: Vec = (0.0, 0.0, 0.0)
        self.flags = 0
        self.rails_cmd = self.power.rails
        self.eclipsed = False
        self.samples = self.eclipse_samples = self.sun_valid_samples = self.blind_violations = 0

    # -- truth, for assertions only; never sent to the flight software --------
    @property
    def rate_dps(self) -> float:
        return norm(self.body.omega) / DEG

    def nadir_error_deg(self) -> float:
        """Angle between the body +Z axis and the true nadir direction."""
        z_eci = rotate(self.body.q, (0.0, 0.0, 1.0))
        nadir = scale(unit(self.orbit.r), -1.0)
        return math.degrees(math.acos(max(-1.0, min(1.0, dot(z_eci, nadir)))))

    def attitude_error_deg(self) -> float:
        """Full three-axis error from the nadir/along-track target frame."""
        r, v = self.orbit.r, self.orbit.v
        z = scale(unit(r), -1.0)
        y = scale(unit(cross(r, v)), -1.0)
        x = cross(y, z)
        bx = rotate(self.body.q, (1.0, 0.0, 0.0))
        bz = rotate(self.body.q, (0.0, 0.0, 1.0))
        # angle of the rotation taking the target triad to the body triad
        tr = dot(bx, x) + dot(rotate(self.body.q, (0.0, 1.0, 0.0)), y) + dot(bz, z)
        return math.degrees(math.acos(max(-1.0, min(1.0, (tr - 1.0) / 2.0))))

    @property
    def wheel_momentum(self) -> float:
        return norm(tuple(self.wheels.h))  # type: ignore[arg-type]

    def step(self) -> None:
        dt = self.sc.dt
        for axis, when in self.sc.wheel_failures.items():
            if self.t >= when:
                self.wheels.failed[axis] = True
        if self.sc.stuck_heater and self.t >= self.sc.stuck_heater[0]:
            self.power.stuck_heater_w = self.sc.stuck_heater[1]
        adcs_on = bool(self.power.rails & (1 << 3))
        wheels_on = bool(self.power.rails & (1 << 4))
        self.wheels.powered = wheels_on

        b_eci = magnetic_field_eci(self.orbit.r, self.t)
        b_body = rotate_inv(self.body.q, b_eci)
        sun_body = rotate_inv(self.body.q, sun_direction_eci(self.t))
        self.eclipsed = in_eclipse(self.orbit.r, self.t)
        sun, sun_valid = self.sun.read(sun_body, self.eclipsed)
        self.samples += 1
        self.eclipse_samples += self.eclipsed
        self.sun_valid_samples += sun_valid
        self.blind_violations += (self.eclipsed and sun_valid)

        self.seq += 1
        mag = self.mag.read(b_body)
        gyro = self.gyro.read(self.body.omega, dt)
        gps_pos, gps_vel, gps_valid = self.gps.read(self.orbit.r, self.orbit.v, self.t)
        st_q, st_valid = self.star.read(self.body.q, self.body.omega, sun_body,
                                        rotate_inv(self.body.q, self.orbit.r), self.eclipsed, self.t)
        self.star_valid_samples += st_valid
        # An unpowered rail is not a sensor reporting zero: it is no report at all.
        self.dipole, self.wheel_cmd, rails, self.flags = self.bridge.exchange(
            self.seq, self.t, mag, gyro, sun=sun, sun_valid=sun_valid and adcs_on,
            mag_valid=adcs_on, gyro_valid=adcs_on,
            wheel_h=self.wheels.measured(), wheels_valid=wheels_on,
            gps_pos=gps_pos, gps_vel=gps_vel, gps_valid=gps_valid,
            star_q=st_q, star_valid=st_valid and adcs_on,
            eps=self.power.telemetry(), rails=self.power.rails, eps_valid=True)
        if self.flags & 4:
            self.power.command_rails(rails)
            self.rails_cmd = rails
        if not adcs_on:
            self.dipole = (0.0, 0.0, 0.0)
        if not wheels_on:
            self.wheel_cmd = (0.0, 0.0, 0.0)

        tau = add(self.mtq.torque(self.dipole, b_body),
                  gravity_gradient_torque(rotate_inv(self.body.q, self.orbit.r), self.sc.inertia))
        self.power.step(sun_body, self.eclipsed, self.dipole, self.wheel_cmd, dt)
        hdot = self.wheels.momentum_rate(self.wheel_cmd)
        self.body.step(tau, dt, tuple(self.wheels.h), hdot)  # type: ignore[arg-type]
        self.wheels.advance(hdot, dt)
        self.orbit.step(dt)
        self.t += dt
