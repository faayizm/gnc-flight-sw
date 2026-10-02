// ============================================================================
//  Tests for the detumble controller and the simulator bridge protocol.
// ============================================================================
#include <cmath>
#include <cstring>

#include "apps/adcs/bdot.hpp"
#include "apps/adcs/sim_bridge.hpp"
#include "core/crc.hpp"
#include "framework.hpp"

using namespace fsw::adcs;

TEST(bdot, first_sample_has_no_derivative_and_commands_nothing) {
    BdotController c;
    const Vec3f m = c.update(Vec3f{1e-5f, 0, 0}, 0.0, BdotConfig{});
    CHECK_NEAR(m.x, 0.0f, 1e-9f);
    CHECK_NEAR(m.y, 0.0f, 1e-9f);
}

TEST(bdot, dipole_opposes_the_measured_field_derivative) {
    BdotController c;
    BdotConfig cfg;
    cfg.filter_tau_s = 0.1f;
    c.update(Vec3f{0, 0, 0}, 0.0, cfg);
    const Vec3f m = c.update(Vec3f{1e-7f, 0, 0}, 0.1, cfg);
    CHECK(m.x < 0.0f);            // field rising in +x => dipole in -x
    CHECK_NEAR(m.y, 0.0f, 1e-9f);
}

TEST(bdot, output_is_limited_but_keeps_its_direction) {
    const Vec3f big{-4.0f, 2.0f, 0.0f};
    const Vec3f m = BdotController::saturate(big, 0.2f);
    CHECK_NEAR(m.x, -0.2f, 1e-6f);
    CHECK_NEAR(m.y, 0.1f, 1e-6f);   // ratio 2:1 preserved, not clamped to 0.2
}

TEST(bdot, a_gap_in_the_data_discards_the_history) {
    BdotController c;
    BdotConfig cfg;
    c.update(Vec3f{0, 0, 0}, 0.0, cfg);
    const Vec3f m = c.update(Vec3f{1e-5f, 0, 0}, 60.0, cfg);   // 60 s later
    CHECK_NEAR(m.x, 0.0f, 1e-9f);
}

TEST(bridge, sensor_frame_round_trips_through_the_codec) {
    SensorFrame in;
    in.seq = 42; in.time_s = 12.5;
    in.mag_t = Vec3f{1e-5f, -2e-5f, 3e-5f};
    in.gyro_rps = Vec3f{0.1f, -0.2f, 0.3f};
    in.mag_valid = true; in.gyro_valid = false;

    uint8_t buf[64];
    const size_t n = encode_sensor(in, buf, sizeof buf);
    CHECK_EQ(n, kSensorFrameBytes);

    SensorFrame out;
    CHECK(decode_sensor(buf + 2, n - 2, out));
    CHECK_EQ(out.seq, 42u);
    CHECK_NEAR(out.time_s, 12.5, 1e-12);
    CHECK_NEAR(out.mag_t.z, 3e-5f, 1e-12f);
    CHECK(out.mag_valid);
    CHECK(!out.gyro_valid);
}

TEST(bridge, a_corrupted_sensor_frame_is_rejected) {
    SensorFrame in;
    uint8_t buf[64];
    const size_t n = encode_sensor(in, buf, sizeof buf);
    buf[10] ^= 0x01;
    SensorFrame out;
    CHECK(!decode_sensor(buf + 2, n - 2, out));
}

TEST(bridge, actuator_frame_has_the_documented_size_and_a_valid_crc) {
    ActuatorFrame a;
    a.seq = 7; a.dipole_a_m2 = Vec3f{0.1f, 0.0f, -0.1f}; a.commanded = true;
    uint8_t buf[64];
    const size_t n = encode_actuator(a, buf, sizeof buf);
    CHECK_EQ(n, kActuatorFrameBytes);
    CHECK_EQ(static_cast<size_t>((buf[0] << 8) | buf[1]), n - 2);
    CHECK(fsw::core::crc16_check(buf + 2, n - 2));
}
