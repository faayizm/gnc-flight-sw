// ============================================================================
//  Tests for fault detection, isolation and recovery: the sensor screen
//  (apps/io/sensor_screen.hpp), the reaction-wheel check and recovery ladder
//  (apps/fdir/), the pointing law with a wheel out of service, and the mode
//  rule that gives up when too few wheels remain.
// ============================================================================
#include "apps/adcs/pointing.hpp"
#include "apps/fdir/wheel_check.hpp"
#include "apps/fdir/wheel_ladder.hpp"
#include "apps/io/sensor_screen.hpp"
#include "apps/modemgr/mode_logic.hpp"
#include "framework.hpp"

using namespace fsw;
using SF = dict::SensorFault;

namespace {

// A healthy-looking frame: every sensor valid, in range, and slightly
// different each time (`k` stands in for noise).
msg::SensorFrame frame(int k) {
    msg::SensorFrame f;
    const float n = 1.0e-7f * static_cast<float>(k % 7);
    f.mag_t = {2.0e-5f + n, -1.0e-5f, 3.0e-5f};
    f.gyro_rps = {1.0e-3f + n, 2.0e-4f, -5.0e-4f};
    f.sun_b = {0.6f, 0.8f - n, 0.0f};
    f.star_q[0] = 1.0f - n; f.star_q[1] = 0.0f; f.star_q[2] = 0.0f; f.star_q[3] = 0.0f;
    f.gps_pos[0] = 6.9e6 + k; f.gps_pos[1] = 0.0; f.gps_pos[2] = 0.0;
    f.gps_vel[0] = 0.0; f.gps_vel[1] = 7.6e3; f.gps_vel[2] = 0.0;
    f.mag_valid = f.gyro_valid = f.sun_valid = f.star_valid = f.gps_valid = true;
    return f;
}

}  // namespace

// ---- sensor screen ----------------------------------------------------------

TEST(sensor_screen, healthy_sensors_pass_untouched) {
    io::SensorScreen s;
    io::ScreenChange ch[5];
    for (int k = 0; k < 100; ++k) {
        msg::SensorFrame f = frame(k);
        CHECK_EQ(s.screen(f, ch), 0);
        CHECK(f.mag_valid && f.gyro_valid && f.sun_valid && f.star_valid && f.gps_valid);
    }
}

TEST(sensor_screen, an_impossible_value_is_refused_at_once_and_readmitted_slowly) {
    io::SensorScreen s;
    io::ScreenChange ch[5];
    msg::SensorFrame f = frame(0);
    f.mag_t = {2.0e-3f, 2.0e-3f, 2.0e-3f};          // two millitesla: a stuck converter
    CHECK_EQ(s.screen(f, ch), 1);
    CHECK(ch[0].sensor == dict::SensorId::MAG && ch[0].fault == SF::RANGE);
    CHECK(!f.mag_valid);
    CHECK(f.gyro_valid);                            // only the liar is silenced

    int readmitted_after = -1;
    for (int k = 1; k < 100 && readmitted_after < 0; ++k) {
        msg::SensorFrame g = frame(k);
        if (s.screen(g, ch) == 1 && ch[0].fault == SF::NONE) { readmitted_after = k; }
        else { CHECK(!g.mag_valid); }
    }
    CHECK_EQ(readmitted_after, static_cast<int>(io::SensorScreen::kRecoverGood));
}

TEST(sensor_screen, not_a_number_is_out_of_range) {
    io::SensorScreen s;
    io::ScreenChange ch[5];
    msg::SensorFrame f = frame(0);
    f.gyro_rps.y = std::nanf("");
    CHECK_EQ(s.screen(f, ch), 1);
    CHECK(ch[0].sensor == dict::SensorId::GYRO);
    CHECK(!f.gyro_valid);
}

TEST(sensor_screen, a_value_that_never_changes_is_frozen) {
    io::SensorScreen s;
    io::ScreenChange ch[5];
    int flagged_at = -1;
    for (int k = 0; k < 30 && flagged_at < 0; ++k) {
        msg::SensorFrame f = frame(k);
        f.gyro_rps = {1.0e-3f, 2.0e-4f, -5.0e-4f};  // identical, every time
        if (s.screen(f, ch) == 1) {
            CHECK(ch[0].sensor == dict::SensorId::GYRO && ch[0].fault == SF::FROZEN);
            flagged_at = k;
        }
    }
    CHECK_EQ(flagged_at, static_cast<int>(io::SensorScreen::kFrozenRepeats) - 1);
}

TEST(sensor_screen, samples_without_a_reading_do_not_hide_a_freeze) {
    // A star tracker reports twice a second; most samples carry nothing from
    // it. A frozen tracker must still be caught.
    io::SensorScreen s;
    io::ScreenChange ch[5];
    bool caught = false;
    for (int k = 0; k < 200 && !caught; ++k) {
        msg::SensorFrame f = frame(k);
        f.star_valid = (k % 5 == 0);
        f.star_q[0] = 1.0f; f.star_q[1] = f.star_q[2] = f.star_q[3] = 0.0f;
        caught = s.screen(f, ch) == 1 && ch[0].sensor == dict::SensorId::STAR;
    }
    CHECK(caught);
}

// ---- wheel check --------------------------------------------------------------

namespace {

// Drive the check with a wheel model: each axis's momentum changes by the
// torque it actually delivers, which for a dead axis is bearing friction.
struct WheelRig {
    fdir::WheelCheck check;
    fdir::WheelCheckConfig cfg;
    double h[3] = {1.0e-4, -2.0e-4, 3.0e-4};
    double t = 0.0;
    uint8_t dead = 0;

    uint8_t run(double seconds, const double torque[3], uint8_t ignore = 0) {
        uint8_t failing = 0;
        for (int k = 0; k < static_cast<int>(seconds * 10.0 + 0.5); ++k) {
            check.commanded(torque, true);
            for (int i = 0; i < 3; ++i) {
                const double delivered = ((dead >> i) & 1u) ? -5.0e-6 * (h[i] > 0 ? 1 : -1) : torque[i];
                h[i] += delivered * 0.1;
            }
            t += 0.1;
            const double q[3] = {std::round(h[0] / 1e-6) * 1e-6, std::round(h[1] / 1e-6) * 1e-6,
                                 std::round(h[2] / 1e-6) * 1e-6};
            failing = check.sample(t, q, true, ignore, cfg);
        }
        return failing;
    }
};

}  // namespace

TEST(wheel_check, healthy_wheels_agree_with_their_commands) {
    WheelRig r;
    const double torque[3] = {1.0e-4, -3.0e-5, 0.0};
    r.check.sample(0.0, r.h, true, 0, r.cfg);
    CHECK_EQ(r.run(60.0, torque), 0);
    CHECK(r.check.good_windows(0) >= 10);
    CHECK(std::fabs(r.check.residual(0)) < 3.0e-6);
}

TEST(wheel_check, a_dead_wheel_is_named_within_two_windows) {
    WheelRig r;
    const double torque[3] = {1.0e-4, -3.0e-5, 2.0e-5};
    r.check.sample(0.0, r.h, true, 0, r.cfg);
    r.run(20.0, torque);
    r.dead = 2;                                     // Y stops
    CHECK_EQ(r.run(5.0, torque), 0);                // one bad window is only a suspicion
    CHECK_EQ(r.run(5.0, torque), 2);                // two is a verdict, and it is Y alone
    CHECK_EQ(r.run(20.0, torque, 2), 0);            // and once ignored, it is not reported
}

TEST(wheel_check, a_wheel_at_its_torque_limit_is_not_judged) {
    WheelRig r;
    const double torque[3] = {2.0e-3, 0.0, 0.0};    // flat out
    r.check.sample(0.0, r.h, true, 0, r.cfg);
    r.dead = 1;
    CHECK_EQ(r.run(30.0, torque), 0);
}

// ---- recovery ladder -------------------------------------------------------------

TEST(wheel_ladder, a_fault_is_reported_then_power_cycled_then_verified) {
    fdir::WheelLadder l;
    const uint32_t none[3] = {0, 0, 0};
    fdir::LadderOutput o = l.step(10.0, 1, none);
    CHECK_EQ(o.event_count, 2);
    CHECK(o.events[0].id == dict::EventId::WHEEL_FAULT && o.events[0].aux == 1);
    CHECK(o.events[1].id == dict::EventId::WHEEL_POWER_CYCLE);
    CHECK(o.wheels_off);
    CHECK(l.step(12.0, 1, none).wheels_off);        // still off: 3 s
    o = l.step(13.0, 1, none);
    CHECK(!o.wheels_off && o.reset_check);
    CHECK(l.state() == dict::FdirWheelState::VERIFY);

    const uint32_t good[3] = {3, 3, 3};
    o = l.step(30.0, 0, good);                      // the retry worked: a latch-up
    CHECK(o.event_count == 1 && o.events[0].id == dict::EventId::WHEEL_RECOVERED);
    CHECK(l.state() == dict::FdirWheelState::MONITOR);
    CHECK_EQ(l.usable(), 7);
}

TEST(wheel_ladder, a_wheel_that_fails_again_after_its_retry_is_isolated) {
    fdir::WheelLadder l;
    const uint32_t none[3] = {0, 0, 0};
    l.step(10.0, 4, none);
    l.step(13.0, 4, none);
    const fdir::LadderOutput o = l.step(23.0, 4, none);
    CHECK(o.event_count == 1 && o.events[0].id == dict::EventId::WHEEL_ISOLATED && o.events[0].aux == 4);
    CHECK_EQ(l.usable(), 3);
    CHECK(l.state() == dict::FdirWheelState::MONITOR);
    CHECK_EQ(l.step(30.0, 4, none).event_count, 0); // an isolated wheel is not reported again
}

TEST(wheel_ladder, retries_run_out) {
    fdir::WheelLadder l;
    const uint32_t none[3] = {0, 0, 0};
    const uint32_t good[3] = {3, 3, 3};
    double t = 0.0;
    for (int i = 0; i < fdir::WheelLadder::kMaxRetries; ++i) {   // latched, cleared, latched...
        l.step(t, 1, none);
        l.step(t + 3.0, 1, none);
        l.step(t + 20.0, 0, good);
        t += 100.0;
    }
    const fdir::LadderOutput o = l.step(t, 1, none);
    CHECK(!o.wheels_off);                           // no more power cycles: straight to isolation
    CHECK(o.events[o.event_count - 1].id == dict::EventId::WHEEL_ISOLATED);
    l.restore(1);                                   // until the ground says otherwise
    CHECK_EQ(l.usable(), 7);
    CHECK(l.step(t + 10.0, 1, none).wheels_off);
}

// ---- reconfigured control ---------------------------------------------------------

TEST(pointing, a_wheel_out_of_service_is_never_commanded_and_the_coils_take_its_axis) {
    // Off-target about X, with the X wheel isolated.
    const adcs::Vec3 r{6.9e6, 0.0, 0.0}, v{0.0, 7.6e3, 0.0};
    adcs::Vec3 w_t;
    const adcs::Quat qt = adcs::nadir_target(r, v, w_t);
    const adcs::Quat tilt{std::cos(0.01), std::sin(0.01), 0.0, 0.0};
    const adcs::Quat q = qt * tilt;
    const adcs::Vec3 b{1.0e-5, 2.0e-5, 2.5e-5};
    adcs::PointingConfig c;
    c.wheels_usable = 0x6;
    const adcs::PointingOutput o = adcs::nadir_control(q, adcs::rotate_inv(q, w_t), {}, r, v, b, true, c);
    CHECK_EQ(o.wheel_torque.x, 0.0);
    CHECK(o.body_torque.x < 0.0);                   // still pushed back towards the target
    CHECK(adcs::norm(o.dipole) > 0.0);

    c.wheels_usable = 0x7;                          // the same error, all wheels in service
    const adcs::PointingOutput all = adcs::nadir_control(q, adcs::rotate_inv(q, w_t), {}, r, v, b, true, c);
    CHECK(all.wheel_torque.x != 0.0);
}

TEST(modes, too_few_wheels_ends_pointing_and_forbids_starting_it) {
    using M = dict::SystemMode;
    modemgr::Facts f;
    f.have_rates = true;
    f.rate_dps = 0.06;
    f.attitude_ok = f.orbit_ok = true;
    f.power = dict::PowerState::NOMINAL;
    modemgr::Limits l;
    f.wheels = 2;
    CHECK(modemgr::autonomous(M::POINTING, f, l).mode == M::POINTING);    // one lost: carry on
    f.wheels = 1;
    const modemgr::Decision d = modemgr::autonomous(M::POINTING, f, l);
    CHECK(d.mode == M::SAFE && d.safe_reason == dict::SafeReason::ACTUATORS);
    CHECK(modemgr::autonomous(M::STANDBY, f, l).mode == M::STANDBY);
    CHECK(modemgr::judge_request(M::STANDBY, M::POINTING, f, l) == dict::ModeRefusal::WHEELS);
}
