// ============================================================================
//  fsw/apps/modemgr/mode_logic.hpp -- the mode rules, as pure functions.
//
//        ┌────────┐  first sensor data
//        │  BOOT  │──────────────┬──────────────────────────────┐
//        └────────┘   rate low   ▼                    rate high ▼
//                          ┌───────────┐  rate > ENGAGE  ┌────────────┐
//                    ┌────▶│  STANDBY  │───────────────▶│  DETUMBLE  │
//                    │     └───────────┘◀───────────────└────────────┘
//    attitude lost   │           │      rate < RELEASE
//                    │           │ attitude known, position known,
//                    │           ▼ power NOMINAL
//                    │     ┌───────────┐  rate > ENGAGE: DETUMBLE
//                    └─────│  POINTING │
//                          └───────────┘
//
//     from any mode: power CRITICAL, or no word from the ground for
//     LINK_TIMEOUT_S  ──▶  SAFE.   Leaving SAFE takes a ground command.
//
//     from POINTING: fewer than two reaction wheels in service ──▶ SAFE.
//     FDIR has already climbed every rung below this one (fdir/wheel_ladder.hpp);
//     with one wheel left there is no configuration to fall back to.
//
//  RELEASE is 80% of POINTING_RATE_DPS and ENGAGE is DETUMBLE_RATE_DPS: the
//  gap between them is the hysteresis that stops a rate hovering near one
//  threshold from flipping the mode back and forth.
//
//  GROUND REQUESTS (ST[8,1]) are requests. Each is checked against the same
//  facts the autonomous rules use, and refused with a reason if it does not
//  make sense: no pointing without a converged attitude, no leaving SAFE
//  while the battery is still low, no BOOT ever.
// ============================================================================
#pragma once

#include <cstdint>

#include "generated/dictionary.hpp"

namespace fsw::modemgr {

struct Facts {
    bool             have_rates   = false;    // ADCS has reported a valid rate
    double           rate_dps     = 0.0;
    bool             attitude_ok  = false;    // estimator CONVERGED
    bool             attitude_lost = false;   // estimator not even initialised
    bool             orbit_ok     = false;
    dict::PowerState power        = dict::PowerState::UNKNOWN;
    double           since_contact_s = 0.0;   // since the ground was last heard
    int              wheels       = 3;        // reaction wheels in service, per FDIR
};

struct Limits {
    double engage_dps     = 2.0;
    double release_dps    = 0.4;
    double link_timeout_s = 86400.0;
};

struct Decision {
    dict::SystemMode mode;
    dict::SafeReason safe_reason = dict::SafeReason::GROUND;
};

inline Decision autonomous(dict::SystemMode m, const Facts& f, const Limits& l) {
    using M = dict::SystemMode;
    if (m != M::SAFE) {
        if (f.power == dict::PowerState::CRITICAL) { return {M::SAFE, dict::SafeReason::POWER_CRITICAL}; }
        if (f.since_contact_s > l.link_timeout_s)  { return {M::SAFE, dict::SafeReason::NO_CONTACT}; }
    }
    if (!f.have_rates) { return {m}; }
    switch (m) {
        case M::BOOT:
            return {f.rate_dps > l.release_dps ? M::DETUMBLE : M::STANDBY};
        case M::DETUMBLE:
            return {f.rate_dps < l.release_dps ? M::STANDBY : M::DETUMBLE};
        case M::STANDBY:
            if (f.rate_dps > l.engage_dps) { return {M::DETUMBLE}; }
            if (f.attitude_ok && f.orbit_ok && f.power == dict::PowerState::NOMINAL &&
                f.rate_dps < l.release_dps && f.wheels >= 2) {
                return {M::POINTING};
            }
            return {M::STANDBY};
        case M::POINTING:
            if (f.wheels < 2) { return {M::SAFE, dict::SafeReason::ACTUATORS}; }
            if (f.rate_dps > l.engage_dps) { return {M::DETUMBLE}; }
            if (f.attitude_lost || !f.orbit_ok) { return {M::STANDBY}; }
            return {M::POINTING};
        case M::SAFE:
            return {M::SAFE};
    }
    return {m};
}

inline dict::ModeRefusal judge_request(dict::SystemMode from, dict::SystemMode to,
                                       const Facts& f, const Limits& l) {
    using M = dict::SystemMode;
    using R = dict::ModeRefusal;
    switch (to) {
        case M::SAFE:
            return R::NONE;                                    // always allowed
        case M::BOOT:
            return R::INVALID;
        case M::DETUMBLE:
            if (from == M::SAFE && f.power == dict::PowerState::CRITICAL) { return R::POWER; }
            return R::NONE;
        case M::STANDBY:
            if (from == M::SAFE && f.power != dict::PowerState::NOMINAL) { return R::POWER; }
            if (f.have_rates && f.rate_dps > l.engage_dps) { return R::RATES_HIGH; }
            return R::NONE;
        case M::POINTING:
            if (from != M::STANDBY && from != M::POINTING) { return R::NOT_FROM_MODE; }
            if (f.power != dict::PowerState::NOMINAL) { return R::POWER; }
            if (!f.have_rates || f.rate_dps > l.release_dps) { return R::RATES_HIGH; }
            if (!f.attitude_ok || !f.orbit_ok) { return R::ATTITUDE_UNKNOWN; }
            if (f.wheels < 2) { return R::WHEELS; }
            return R::NONE;
    }
    return R::INVALID;
}

}  // namespace fsw::modemgr
