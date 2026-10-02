// ============================================================================
//  fsw/apps/adcs/sim_bridge.cpp
// ============================================================================
#include "apps/adcs/sim_bridge.hpp"

#include <cstring>

#include "core/bytes.hpp"
#include "core/crc.hpp"

namespace fsw::adcs {

namespace {

bool finish_frame(core::ByteWriter& w, uint8_t* out, size_t& total) {
    // Body = everything between the length word and the CRC.
    const uint16_t crc = core::crc16(out + 2, w.size() - 2);
    if (!w.write_uint16(crc)) { return false; }
    total = w.size();
    const auto length = static_cast<uint16_t>(total - 2);
    out[0] = static_cast<uint8_t>(length >> 8);
    out[1] = static_cast<uint8_t>(length & 0xFF);
    return true;
}

bool write_vec(core::ByteWriter& w, const Vec3f& v) {
    return w.write_float32(v.x) && w.write_float32(v.y) && w.write_float32(v.z);
}

bool read_vec(core::ByteReader& r, Vec3f& v) {
    return r.read_float32(v.x) && r.read_float32(v.y) && r.read_float32(v.z);
}

}  // namespace

size_t encode_actuator(const ActuatorFrame& f, uint8_t* out, size_t capacity) {
    core::ByteWriter w(out, capacity);
    w.write_uint16(0);  // length, patched in finish_frame
    w.write_uint8(kFrameActuator);
    w.write_uint32(f.seq);
    write_vec(w, f.dipole_a_m2);
    w.write_uint8(f.commanded ? 1 : 0);
    size_t total = 0;
    return finish_frame(w, out, total) ? total : 0;
}

size_t encode_sensor(const SensorFrame& f, uint8_t* out, size_t capacity) {
    core::ByteWriter w(out, capacity);
    w.write_uint16(0);
    w.write_uint8(kFrameSensor);
    w.write_uint32(f.seq);
    w.write_float64(f.time_s);
    write_vec(w, f.mag_t);
    write_vec(w, f.gyro_rps);
    write_vec(w, f.sun_b);
    w.write_uint8(static_cast<uint8_t>((f.mag_valid ? 1 : 0) | (f.gyro_valid ? 2 : 0) |
                                       (f.sun_valid ? 4 : 0)));
    size_t total = 0;
    return finish_frame(w, out, total) ? total : 0;
}

bool decode_sensor(const uint8_t* body, size_t length, SensorFrame& out) {
    // body = type + payload + CRC
    if (length < 3 || !core::crc16_check(body, length)) { return false; }
    core::ByteReader r(body, length - 2);
    uint8_t type = 0;
    uint8_t flags = 0;
    SensorFrame f;
    if (!r.read_uint8(type) || type != kFrameSensor) { return false; }
    if (!r.read_uint32(f.seq) || !r.read_float64(f.time_s) ||
        !read_vec(r, f.mag_t) || !read_vec(r, f.gyro_rps) ||
        !read_vec(r, f.sun_b) || !r.read_uint8(flags)) {
        return false;
    }
    if (!r.exhausted()) { return false; }
    f.mag_valid  = (flags & 1) != 0;
    f.gyro_valid = (flags & 2) != 0;
    f.sun_valid  = (flags & 4) != 0;
    out = f;
    return true;
}

bool SimBridge::poll(SensorFrame& out) {
    link_.poll();
    if (used_ < kRxBytes) {
        used_ += link_.receive(rx_ + used_, kRxBytes - used_);
    }

    bool got = false;
    size_t pos = 0;
    while (used_ - pos >= 2) {
        const size_t length = (static_cast<size_t>(rx_[pos]) << 8) | rx_[pos + 1];
        if (length < 3 || length + 2 > kRxBytes) {
            // Nonsense length: the stream cannot be trusted to be aligned.
            // TCP does not lose bytes, so this means a misbehaving peer;
            // discard everything and let the next frame start clean.
            ++frames_bad_;
            pos = used_;
            break;
        }
        if (used_ - pos < length + 2) { break; }  // wait for the rest

        SensorFrame f;
        if (decode_sensor(rx_ + pos + 2, length, f)) {
            if (got) { ++frames_dropped_; }
            out = f;
            got = true;
            ++frames_ok_;
        } else {
            ++frames_bad_;
        }
        pos += length + 2;
    }

    if (pos > 0) {
        std::memmove(rx_, rx_ + pos, used_ - pos);
        used_ -= pos;
    }
    return got;
}

core::Status SimBridge::send(const ActuatorFrame& f) {
    uint8_t buf[kActuatorFrameBytes];
    const size_t n = encode_actuator(f, buf, sizeof buf);
    if (n == 0) { return core::Status::Invalid; }
    return link_.send(buf, n);
}

}  // namespace fsw::adcs
