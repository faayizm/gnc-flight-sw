"""Rigid-body attitude dynamics, with internal reaction wheels.

    Euler:       I w_dot = tau_ext - w x (I w + h_w) - h_w_dot
    Kinematics:  q_dot  = 1/2 q (x) (0, w)

h_w is the wheels' stored angular momentum (body frame). Spinning a wheel up
pushes the body the other way (the -h_w_dot term), and a spinning wheel makes
the body gyroscopically stiff (the w x h_w term). With no wheels both are zero
and this is the plain rigid body.

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

    def _omega_dot(self, w: Vec, tau: Vec, hw: Vec = (0.0, 0.0, 0.0),
                   hw_dot: Vec = (0.0, 0.0, 0.0)) -> Vec:
        ix, iy, iz = self.inertia
        h = (ix * w[0] + hw[0], iy * w[1] + hw[1], iz * w[2] + hw[2])
        g = cross(w, h)
        t = (tau[0] - g[0] - hw_dot[0], tau[1] - g[1] - hw_dot[1], tau[2] - g[2] - hw_dot[2])
        return (t[0] / ix, t[1] / iy, t[2] / iz)

    @staticmethod
    def _q_dot(q: Quat, w: Vec) -> Quat:
        d = q_mul(q, (0.0, w[0], w[1], w[2]))
        return (0.5 * d[0], 0.5 * d[1], 0.5 * d[2], 0.5 * d[3])

    def step(self, tau: Vec, dt: float, hw: Vec = (0.0, 0.0, 0.0),
             hw_dot: Vec = (0.0, 0.0, 0.0)) -> None:
        """Advance dt with the external torque and the wheel acceleration held
        constant (zero-order hold). hw is the wheel momentum at the start of the
        step; it grows linearly at hw_dot across it."""
        q, w = self.q, self.omega
        hw_mid = add(hw, scale(hw_dot, dt / 2))
        hw_end = add(hw, scale(hw_dot, dt))

        def qadd(a: Quat, b: Quat, k: float) -> Quat:
            return (a[0] + k * b[0], a[1] + k * b[1], a[2] + k * b[2], a[3] + k * b[3])

        k1q, k1w = self._q_dot(q, w), self._omega_dot(w, tau, hw, hw_dot)
        w2 = add(w, scale(k1w, dt / 2))
        k2q, k2w = self._q_dot(qadd(q, k1q, dt / 2), w2), self._omega_dot(w2, tau, hw_mid, hw_dot)
        w3 = add(w, scale(k2w, dt / 2))
        k3q, k3w = self._q_dot(qadd(q, k2q, dt / 2), w3), self._omega_dot(w3, tau, hw_mid, hw_dot)
        w4 = add(w, scale(k3w, dt))
        k4q, k4w = self._q_dot(qadd(q, k3q, dt), w4), self._omega_dot(w4, tau, hw_end, hw_dot)

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
