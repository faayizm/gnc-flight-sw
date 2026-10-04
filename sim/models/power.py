"""Electrical power: solar arrays, a battery, and the loads on each rail.

SOLAR ARRAYS. A deployed panel facing body -Z (zenith, when the spacecraft
points +Z at the Earth) and body-mounted panels on the four side faces. Each
produces flux x area x efficiency x cos(sun angle), zero when the sun is
behind it or the spacecraft is in eclipse. So power depends on attitude: a
nadir-pointed spacecraft sees its main panel swing from edge-on to face-on
and back every orbit, and a tumbling one averages whatever faces the sun.

BATTERY. Two lithium-ion cells in series, 6.0-8.4 V, with the open-circuit
voltage curve the flight software also carries (eps/power_policy.hpp), an
internal resistance, and a charge efficiency. Full means full: excess solar
power is shunted, not stored.

LOADS. Each switched rail draws its nominal power while on. The wheels and
magnetorquers add a share that grows with how hard they are working. And a
fault can be injected: a heater whose thermostat sticks closed draws far more
than its nominal share, and only switching the rail off stops it.
"""

from __future__ import annotations

from dataclasses import dataclass, field

from .linalg import Vec, dot

SOLAR_FLUX = 1361.0        # W/m^2 at 1 AU
CELL_EFFICIENCY = 0.28
MPPT_EFFICIENCY = 0.92

OCV_TABLE = (6.0, 6.9, 7.2, 7.35, 7.45, 7.55, 7.7, 7.85, 8.0, 8.2, 8.4)   # 0..100% in 10% steps

# rail bit -> (name, nominal watts). Bit numbers match dict::PowerRail.
RAILS = {
    0: ("OBC", 1.0),
    1: ("RX", 0.4),
    2: ("TX", 2.5),
    3: ("ADCS", 0.8),
    4: ("WHEELS", 1.2),
    5: ("PAYLOAD", 6.0),
    6: ("OPS_HEATERS", 1.5),
    7: ("SURVIVAL_HEATERS", 0.6),
}
PROTECTED = 0b1000_0011           # OBC, RX, survival heaters: hard-wired on

PANELS = (                        # (outward normal in body axes, area m^2)
    ((0.0, 0.0, -1.0), 0.060),    # deployed zenith panel
    ((1.0, 0.0, 0.0), 0.020),
    ((-1.0, 0.0, 0.0), 0.020),
    ((0.0, 1.0, 0.0), 0.020),
    ((0.0, -1.0, 0.0), 0.020),
)


def ocv(soc: float) -> float:
    s = min(1.0, max(0.0, soc)) * 10.0
    i = min(9, int(s))
    return OCV_TABLE[i] + (s - i) * (OCV_TABLE[i + 1] - OCV_TABLE[i])


@dataclass
class PowerSystem:
    capacity_wh: float = 30.0
    soc: float = 0.7
    r_internal: float = 0.08
    charge_efficiency: float = 0.95
    rails: int = 0xFF & ~(1 << 5)           # what the switches are actually set to
    stuck_heater_w: float = 0.0             # fault: extra draw on OPS_HEATERS while on
    temp_c: float = 18.0
    last: dict = field(default_factory=dict)

    def solar_power(self, sun_body: Vec, eclipsed: bool) -> float:
        if eclipsed:
            return 0.0
        p = 0.0
        for normal, area in PANELS:
            c = dot(normal, sun_body)
            if c > 0:
                p += SOLAR_FLUX * area * CELL_EFFICIENCY * c
        return p * MPPT_EFFICIENCY

    def load_power(self, dipole: Vec, wheel_torque: Vec) -> float:
        p = 0.0
        for bit, (name, watts) in RAILS.items():
            if self.rails & (1 << bit):
                p += watts
                if name == "OPS_HEATERS":
                    p += self.stuck_heater_w
        if self.rails & (1 << 3):        # magnetorquers: ~0.5 W per axis at full dipole
            p += sum(abs(m) for m in dipole) / 0.2 * 0.5
        if self.rails & (1 << 4):        # wheel motors: torque costs power
            p += sum(abs(t) for t in wheel_torque) * 400.0
        return p

    def command_rails(self, requested: int) -> None:
        self.rails = (requested & 0xFF) | PROTECTED

    def step(self, sun_body: Vec, eclipsed: bool, dipole: Vec, wheel_torque: Vec, dt: float) -> None:
        solar = self.solar_power(sun_body, eclipsed)
        load = self.load_power(dipole, wheel_torque)
        net = solar - load
        v_oc = ocv(self.soc)
        if net > 0 and self.soc >= 1.0:
            net = 0.0                    # full: the shunt regulator dumps the excess
        current = net / v_oc
        stored = net * (self.charge_efficiency if net > 0 else 1.0)
        self.soc = min(1.0, max(0.0, self.soc + stored * dt / 3600.0 / self.capacity_wh))
        voltage = v_oc + current * self.r_internal
        self.last = dict(v=voltage, i=current, solar=solar, load=load)

    def telemetry(self):
        return (self.last.get("v", ocv(self.soc)), self.last.get("i", 0.0), self.last.get("solar", 0.0),
                self.last.get("load", 0.0), self.temp_c)
