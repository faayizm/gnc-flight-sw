// ============================================================================
//  fsw/apps/adcs/sim_bridge.hpp -- the simulator bridge protocol.
//
//  The flight software never sees the simulator's truth. It receives exactly
//  what sensors would produce, and returns exactly what the actuator drivers
//  would be told. This file is that boundary, and nothing crosses it that a
//  real sensor or actuator interface would not carry.
//
//  FRAMING. TCP is a byte stream, so each frame carries its own length:
//
//      +-----------+------+--------------------+-----------+
//      | length u16| type |      payload       | CRC-16 u16|
//      +-----------+------+--------------------+-----------+
//                  '------ covered by CRC -----'
//
//  `length` counts everything after itself (type + payload + CRC). All fields
//  are big-endian, like the rest of the spacecraft. The CRC is the same
//  CCSDS CRC-16 used on the telecommand link.
//
//  FRAMES
//    0x01 SENSOR    simulator -> flight
//        seq        u32       frame counter
//        time       f64       s since mission epoch (GPS time on a real bus)
//        mag        f32[3]    tesla, body frame
//        gyro       f32[3]    rad/s, body frame
//        sun        f32[3]    unit vector, body frame
//        wheel_h    f32[3]    N*m*s, wheel momentum from the tachometers
//        gps_pos    f64[3]    m, inertial frame
//        gps_vel    f64[3]    m/s, inertial frame
//        star_q     f32[4]    star tracker attitude, body -> inertial, w first
//        flags      u8        bit0 mag, bit1 gyro, bit2 sun, bit3 GPS,
//                             bit4 wheels, bit5 star tracker -- each set when
//                             that data is valid
//
//    0x02 ACTUATOR  flight -> simulator
//        seq        u32       echo of the sensor frame being answered
//        dipole     f32[3]    A*m^2, body frame, magnetorquer demand
//        wheel_trq  f32[3]    N*m, torque demanded of each wheel motor (the
//                             body feels the opposite)
//        flags      u8        bit0 magnetorquers commanded, bit1 wheels
//                             commanded
//
//  LOCKSTEP. The simulator sends one SENSOR frame and waits for the matching
//  ACTUATOR frame before advancing time. That is what makes a run exactly
//  reproducible whatever the host load or time scale: the flight software
//  answers every sample, in order, and the simulator never races ahead of it.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

#include "apps/adcs/bdot.hpp"
#include "core/status.hpp"
#include "hal/link.hpp"

namespace fsw::adcs {

constexpr uint8_t kFrameSensor   = 0x01;
constexpr uint8_t kFrameActuator = 0x02;

struct SensorFrame {
    uint32_t seq        = 0;
    double   time_s     = 0.0;
    Vec3f    mag_t{};
    Vec3f    gyro_rps{};
    Vec3f    sun_b{};
    Vec3f    wheel_h{};
    double   gps_pos[3] = {0.0, 0.0, 0.0};
    double   gps_vel[3] = {0.0, 0.0, 0.0};
    bool     mag_valid   = false;
    bool     gyro_valid  = false;
    bool     sun_valid   = false;
    bool     gps_valid   = false;
    float    star_q[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    bool     wheels_valid = false;
    bool     star_valid  = false;
};

struct ActuatorFrame {
    uint32_t seq       = 0;
    Vec3f    dipole_a_m2{};
    Vec3f    wheel_torque_nm{};
    bool     commanded = false;
    bool     wheels_commanded = false;
};

constexpr size_t kSensorFrameBytes   = 2 + 1 + 125 + 2;
constexpr size_t kActuatorFrameBytes = 2 + 1 + 29 + 2;

// Pure codecs, separated from the link so they can be tested byte for byte.
size_t encode_actuator(const ActuatorFrame& f, uint8_t* out, size_t capacity);
size_t encode_sensor(const SensorFrame& f, uint8_t* out, size_t capacity);  // used by tests
bool   decode_sensor(const uint8_t* body, size_t length, SensorFrame& out);

class SimBridge {
 public:
    explicit SimBridge(hal::ILink& link) : link_(link) {}

    // Pump the link and reassemble frames. Returns true if at least one SENSOR
    // frame arrived; `out` then holds the newest one. Older frames in the same
    // call are counted as dropped -- under lockstep there should never be any.
    bool poll(SensorFrame& out);

    core::Status send(const ActuatorFrame& f);

    bool     connected()     const { return link_.connected(); }
    uint32_t frames_ok()     const { return frames_ok_; }
    uint32_t frames_bad()    const { return frames_bad_; }
    uint32_t frames_dropped() const { return frames_dropped_; }

 private:
    static constexpr size_t kRxBytes = 512;

    hal::ILink& link_;
    uint8_t     rx_[kRxBytes]{};
    size_t      used_ = 0;
    uint32_t    frames_ok_ = 0;
    uint32_t    frames_bad_ = 0;
    uint32_t    frames_dropped_ = 0;
};

}  // namespace fsw::adcs
