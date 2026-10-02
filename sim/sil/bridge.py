"""Simulator side of the bridge. Mirror of fsw/apps/adcs/sim_bridge.hpp --
that header is the specification; keep the two in step."""

from __future__ import annotations

import socket
import struct
import time

SENSOR, ACTUATOR = 0x01, 0x02


def crc16(data: bytes, crc: int = 0xFFFF) -> int:
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


ZERO3 = (0.0, 0.0, 0.0)


def encode_sensor(seq: int, t: float, mag, gyro, sun=ZERO3, wheel_h=ZERO3, gps_pos=ZERO3,
                  gps_vel=ZERO3, mag_valid=True, gyro_valid=True, sun_valid=False,
                  gps_valid=False, wheels_valid=False, star_q=(1.0, 0.0, 0.0, 0.0),
                  star_valid=False) -> bytes:
    flags = ((1 if mag_valid else 0) | (2 if gyro_valid else 0) | (4 if sun_valid else 0)
             | (8 if gps_valid else 0) | (16 if wheels_valid else 0) | (32 if star_valid else 0))
    body = struct.pack(">BId3f3f3f3f3d3d4fB", SENSOR, seq, t, *mag, *gyro, *sun, *wheel_h,
                       *gps_pos, *gps_vel, *star_q, flags)
    body += struct.pack(">H", crc16(body))
    return struct.pack(">H", len(body)) + body


def decode_actuator(body: bytes):
    if len(body) < 3 or crc16(body) != 0:
        raise ValueError("bad CRC on actuator frame")
    kind, seq, mx, my, mz, tx, ty, tz, flags = struct.unpack(">BI3f3fB", body[:-2])
    if kind != ACTUATOR:
        raise ValueError(f"unexpected frame type {kind}")
    return seq, (mx, my, mz), (tx, ty, tz), flags


class Bridge:
    def __init__(self, port: int, host: str = "127.0.0.1"):
        self.addr = (host, port)
        self.sock: socket.socket | None = None
        self.buf = b""

    def connect(self, retries: int = 100, delay: float = 0.05) -> None:
        for _ in range(retries):
            try:
                self.sock = socket.create_connection(self.addr, timeout=10)
                self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                return
            except OSError:
                time.sleep(delay)
        raise ConnectionError(f"flight software not listening on {self.addr}")

    def close(self) -> None:
        if self.sock:
            self.sock.close()

    def exchange(self, seq: int, t: float, mag, gyro, **kw):
        """Send one sensor frame and block for the matching actuator frame.
        Returns (dipole, wheel_torque, flags)."""
        assert self.sock is not None
        self.sock.sendall(encode_sensor(seq, t, mag, gyro, **kw))
        while True:
            if len(self.buf) >= 2:
                n = struct.unpack(">H", self.buf[:2])[0]
                if len(self.buf) >= 2 + n:
                    body, self.buf = self.buf[2:2 + n], self.buf[2 + n:]
                    rseq, dipole, wheel_torque, flags = decode_actuator(body)
                    if rseq != seq:
                        raise RuntimeError(f"lockstep broken: sent {seq}, got {rseq}")
                    return dipole, wheel_torque, flags
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError("flight software closed the bridge")
            self.buf += chunk
