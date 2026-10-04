// ============================================================================
//  Tests for the power policy (apps/eps/power_policy.hpp) and the mode rules
//  (apps/modemgr/mode_logic.hpp). Both are pure functions, so every threshold
//  and every hysteresis band can be pinned down directly.
// ============================================================================
#include "apps/eps/power_policy.hpp"
#include "apps/modemgr/mode_logic.hpp"
#include "framework.hpp"

using namespace fsw;
using PS = dict::PowerState;
using M  = dict::SystemMode;
using R  = dict::PowerRail;

namespace {
constexpr double kLow = 40.0, kCrit = 20.0;
bool on(uint16_t rails, R r) { return (rails & eps::rail_bit(r)) != 0; }
}  // namespace

TEST(power, soc_from_open_circuit_voltage_follows_the_table) {
    CHECK_NEAR(eps::soc_from_ocv(6.0), 0.0, 1e-9);
    CHECK_NEAR(eps::soc_from_ocv(7.55), 50.0, 1e-9);
    CHECK_NEAR(eps::soc_from_ocv(8.4), 100.0, 1e-9);
    CHECK_NEAR(eps::soc_from_ocv(7.625), 55.0, 1e-9);
}

TEST(power, coulomb_counting_tracks_a_known_discharge) {
    eps::SocEstimator e;
    e.update(7.55, 0.0, 0.0, 30.0);                 // anchored at 50% at rest
    for (int i = 0; i < 600; ++i) { e.update(7.55, -1.0, 1.0, 30.0); }   // 7.55 W for ten minutes
    // 1.26 Wh of 30 Wh is 4.2%. The voltage figure here reads ~55% (a sagging
    // terminal voltage held artificially flat), so its slow pull takes back
    // about one point -- but only one: counting dominates in the short term.
    CHECK(e.soc() > 45.0 && e.soc() < 48.0);
}

TEST(power, states_have_hysteresis_on_both_thresholds) {
    PS s = PS::NOMINAL;
    s = eps::next_power_state(s, 39.0, kLow, kCrit);  CHECK(s == PS::LOW);
    s = eps::next_power_state(s, 45.0, kLow, kCrit);  CHECK(s == PS::LOW);       // inside the band
    s = eps::next_power_state(s, 51.0, kLow, kCrit);  CHECK(s == PS::NOMINAL);
    s = eps::next_power_state(s, 19.0, kLow, kCrit);  CHECK(s == PS::CRITICAL);  // straight down
    s = eps::next_power_state(s, 25.0, kLow, kCrit);  CHECK(s == PS::CRITICAL);
    s = eps::next_power_state(s, 31.0, kLow, kCrit);  CHECK(s == PS::LOW);
}

TEST(power, shedding_deepens_in_order_and_safe_mode_is_the_floor) {
    CHECK_EQ(eps::shed_level(PS::NOMINAL, 80, kLow, kCrit, 0, false), 0);
    CHECK_EQ(eps::shed_level(PS::LOW, 35, kLow, kCrit, 0, false), 1);
    CHECK_EQ(eps::shed_level(PS::LOW, 29, kLow, kCrit, 1, false), 2);
    CHECK_EQ(eps::shed_level(PS::LOW, 32, kLow, kCrit, 2, false), 2);   // its own band
    CHECK_EQ(eps::shed_level(PS::LOW, 36, kLow, kCrit, 2, false), 1);
    CHECK_EQ(eps::shed_level(PS::CRITICAL, 15, kLow, kCrit, 2, false), 3);
    CHECK_EQ(eps::shed_level(PS::NOMINAL, 90, kLow, kCrit, 0, true), 4);
}

TEST(power, the_receiver_computer_and_survival_heaters_are_never_shed) {
    const uint16_t rails = eps::rail_policy(M::SAFE, 4, false, 0);   // ground asked for nothing
    CHECK(on(rails, R::OBC));
    CHECK(on(rails, R::RX));
    CHECK(on(rails, R::SURVIVAL_HEATERS));
    CHECK(!on(rails, R::WHEELS));
    CHECK(!on(rails, R::TX));
}

TEST(power, the_transmitter_stays_on_after_contact_even_when_shed) {
    CHECK(!on(eps::rail_policy(M::POINTING, 2, false, 0xFF), R::TX));
    CHECK(on(eps::rail_policy(M::POINTING, 2, true, 0xFF), R::TX));
}

TEST(power, payload_runs_only_when_pointing_and_permitted) {
    CHECK(on(eps::rail_policy(M::POINTING, 0, true, 0xFF), R::PAYLOAD));
    CHECK(!on(eps::rail_policy(M::STANDBY, 0, true, 0xFF), R::PAYLOAD));
    CHECK(!on(eps::rail_policy(M::POINTING, 1, true, 0xFF), R::PAYLOAD));
    CHECK(!on(eps::rail_policy(M::POINTING, 0, true, eps::kDefaultGroundRails), R::PAYLOAD));
}

namespace {
modemgr::Facts calm() {
    modemgr::Facts f;
    f.have_rates = true; f.rate_dps = 0.1; f.attitude_ok = true; f.orbit_ok = true;
    f.power = PS::NOMINAL;
    return f;
}
}  // namespace

TEST(modes, boot_goes_to_detumble_or_standby_on_the_first_rates) {
    modemgr::Limits l;
    modemgr::Facts f = calm();
    f.rate_dps = 3.0;
    CHECK(modemgr::autonomous(M::BOOT, f, l).mode == M::DETUMBLE);
    f.rate_dps = 0.1;
    CHECK(modemgr::autonomous(M::BOOT, f, l).mode == M::STANDBY);
    f.have_rates = false;
    CHECK(modemgr::autonomous(M::BOOT, f, l).mode == M::BOOT);
}

TEST(modes, rate_thresholds_have_a_dead_band) {
    modemgr::Limits l;   // engage 2.0, release 0.4
    modemgr::Facts f = calm();
    f.rate_dps = 1.0;
    CHECK(modemgr::autonomous(M::DETUMBLE, f, l).mode == M::DETUMBLE);   // not low enough to leave
    CHECK(modemgr::autonomous(M::POINTING, f, l).mode == M::POINTING);   // not high enough to drop
    f.rate_dps = 2.5;
    CHECK(modemgr::autonomous(M::POINTING, f, l).mode == M::DETUMBLE);
}

TEST(modes, critical_power_or_silence_forces_safe_and_nothing_leaves_it) {
    modemgr::Limits l;
    modemgr::Facts f = calm();
    f.power = PS::CRITICAL;
    const modemgr::Decision d = modemgr::autonomous(M::POINTING, f, l);
    CHECK(d.mode == M::SAFE && d.safe_reason == dict::SafeReason::POWER_CRITICAL);
    f = calm();
    f.since_contact_s = l.link_timeout_s + 1;
    CHECK(modemgr::autonomous(M::STANDBY, f, l).safe_reason == dict::SafeReason::NO_CONTACT);
    CHECK(modemgr::autonomous(M::SAFE, calm(), l).mode == M::SAFE);      // all well, still SAFE
}

TEST(modes, pointing_needs_attitude_position_and_nominal_power) {
    modemgr::Limits l;
    modemgr::Facts f = calm();
    CHECK(modemgr::autonomous(M::STANDBY, f, l).mode == M::POINTING);
    f.power = PS::LOW;
    CHECK(modemgr::autonomous(M::STANDBY, f, l).mode == M::STANDBY);
    f = calm(); f.attitude_ok = false;
    CHECK(modemgr::autonomous(M::STANDBY, f, l).mode == M::STANDBY);
}

TEST(modes, ground_requests_are_judged) {
    modemgr::Limits l;
    modemgr::Facts f = calm();
    CHECK(modemgr::judge_request(M::POINTING, M::SAFE, f, l) == dict::ModeRefusal::NONE);
    CHECK(modemgr::judge_request(M::SAFE, M::POINTING, f, l) == dict::ModeRefusal::NOT_FROM_MODE);
    CHECK(modemgr::judge_request(M::STANDBY, M::BOOT, f, l) == dict::ModeRefusal::INVALID);
    f.power = PS::LOW;
    CHECK(modemgr::judge_request(M::SAFE, M::STANDBY, f, l) == dict::ModeRefusal::POWER);
    f = calm(); f.rate_dps = 3.0;
    CHECK(modemgr::judge_request(M::DETUMBLE, M::STANDBY, f, l) == dict::ModeRefusal::RATES_HIGH);
    f = calm(); f.attitude_ok = false;
    CHECK(modemgr::judge_request(M::STANDBY, M::POINTING, f, l) == dict::ModeRefusal::ATTITUDE_UNKNOWN);
}
