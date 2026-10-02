"""Rigid-body attitude dynamics.

    Euler:       I w_dot = tau - w x (I w)
    Kinematics:  q_dot  = 1/2 q (x) (0, w)

State is the quaternion (body -> inertial) and the body angular velocity,
integrated with RK4. The quaternion is renormalised after every step; RK4 does
not preserve its norm exactly.
"""

from __future__ import annotations

from .linalg import Quat, Vec, add, cross, q_mul, q_normalize, scale


class RigidBody:
    def __init__(self, inertia_diag: Vec, q: Quat, omega: Vec):
        self.inertia = inertia_diag
        self.q = q_normalize(q)
        self.omega = omega

    def _omega_dot(self, w: Vec, tau: Vec) -> Vec:
        ix, iy, iz = self.inertia
        h = (ix * w[0], iy * w[1], iz * w[2])
        t = (tau[0] - cross(w, h)[0], tau[1] - cross(w, h)[1], tau[2] - cross(w, h)[2])
        return (t[0] / ix, t[1] / iy, t[2] / iz)

    @staticmethod
    def _q_dot(q: Quat, w: Vec) -> Quat:
        d = q_mul(q, (0.0, w[0], w[1], w[2]))
        return (0.5 * d[0], 0.5 * d[1], 0.5 * d[2], 0.5 * d[3])

    def step(self, tau: Vec, dt: float) -> None:
        """Advance dt with the torque held constant (zero-order hold)."""
        q, w = self.q, self.omega

        def qadd(a: Quat, b: Quat, k: float) -> Quat:
            return (a[0] + k * b[0], a[1] + k * b[1], a[2] + k * b[2], a[3] + k * b[3])

        k1q, k1w = self._q_dot(q, w), self._omega_dot(w, tau)
        w2 = add(w, scale(k1w, dt / 2))
        k2q, k2w = self._q_dot(qadd(q, k1q, dt / 2), w2), self._omega_dot(w2, tau)
        w3 = add(w, scale(k2w, dt / 2))
        k3q, k3w = self._q_dot(qadd(q, k2q, dt / 2), w3), self._omega_dot(w3, tau)
        w4 = add(w, scale(k3w, dt))
        k4q, k4w = self._q_dot(qadd(q, k3q, dt), w4), self._omega_dot(w4, tau)

        qn = tuple(q[i] + dt / 6 * (k1q[i] + 2 * k2q[i] + 2 * k3q[i] + k4q[i]) for i in range(4))
        wn = tuple(w[i] + dt / 6 * (k1w[i] + 2 * k2w[i] + 2 * k3w[i] + k4w[i]) for i in range(3))
        self.q = q_normalize(qn)  # type: ignore[arg-type]
        self.omega = wn  # type: ignore[assignment]

    def angular_momentum(self) -> Vec:
        return (self.inertia[0] * self.omega[0],
                self.inertia[1] * self.omega[1],
                self.inertia[2] * self.omega[2])

    def kinetic_energy(self) -> float:
        return 0.5 * sum(i * w * w for i, w in zip(self.inertia, self.omega))
