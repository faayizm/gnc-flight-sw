"""Small vector and quaternion helpers. Standard library only.

Vectors are 3-tuples of floats; quaternions are (w, x, y, z), scalar first.

CONVENTION. A quaternion q describes the body's attitude: it rotates a vector
written in the body frame into the inertial frame,  v_i = q (x) v_b (x) q*.
To go the other way, use rotate_inv().
"""

from __future__ import annotations

import math

Vec = tuple[float, float, float]
Quat = tuple[float, float, float, float]


def add(a: Vec, b: Vec) -> Vec:
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def sub(a: Vec, b: Vec) -> Vec:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def scale(a: Vec, k: float) -> Vec:
    return (a[0] * k, a[1] * k, a[2] * k)


def dot(a: Vec, b: Vec) -> float:
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cross(a: Vec, b: Vec) -> Vec:
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def norm(a: Vec) -> float:
    return math.sqrt(dot(a, a))


def unit(a: Vec) -> Vec:
    n = norm(a)
    return (a[0] / n, a[1] / n, a[2] / n)


def q_mul(p: Quat, q: Quat) -> Quat:
    pw, px, py, pz = p
    qw, qx, qy, qz = q
    return (pw * qw - px * qx - py * qy - pz * qz,
            pw * qx + px * qw + py * qz - pz * qy,
            pw * qy - px * qz + py * qw + pz * qx,
            pw * qz + px * qy - py * qx + pz * qw)


def q_normalize(q: Quat) -> Quat:
    n = math.sqrt(sum(c * c for c in q))
    return (q[0] / n, q[1] / n, q[2] / n, q[3] / n)


def q_conj(q: Quat) -> Quat:
    return (q[0], -q[1], -q[2], -q[3])


def rotate(q: Quat, v: Vec) -> Vec:
    """Body -> inertial."""
    w, x, y, z = q
    # v' = v + 2w (u x v) + 2 u x (u x v), with u = (x, y, z)
    u = (x, y, z)
    t = scale(cross(u, v), 2.0)
    return add(add(v, scale(t, w)), cross(u, t))


def rotate_inv(q: Quat, v: Vec) -> Vec:
    """Inertial -> body."""
    return rotate(q_conj(q), v)
