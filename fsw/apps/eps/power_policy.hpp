// ============================================================================
//  fsw/apps/eps/power_policy.hpp -- every power decision, as pure functions.
//
//  No bus, no clock, no state beyond what is passed in: each rule can be
//  tested on its own, and read on its own.
//
//  THE BATTERY. A two-cell lithium-ion pack, 6.0-8.4 V. State of charge is
//  counted (energy in minus energy out), because voltage alone is a poor
//  gauge under load; the count is anchored to the open-circuit voltage at
//  boot and nudged toward it slowly afterwards, so that counting errors cannot
//  accumulate without limit.
//
//  POWER STATE, with hysteresis on both thresholds:
//
//      NOMINAL --(soc < LOW)--> LOW --(soc < CRIT)--> CRITICAL
//      NOMINAL <-(soc > LOW+10)- LOW <-(soc > CRIT+10)- CRITICAL
//
//  LOAD SHEDDING, in order of increasing desperation -- sacrifice the mission
//  to save the spacecraft (see learn/17-power-and-modes):
//
//      level 0  nothing
//      level 1  the payload                          LOW
//      level 2  the transmitter, between passes      LOW and below the midpoint
//      level 3  operational heaters                  CRITICAL
//      level 4  the reaction wheels                  SAFE mode
//
//  Never shed: the computer, the receiver and the survival heaters. Without
//  the receiver nobody can help; a frozen battery is a dead battery. The
//  attitude sensors and magnetorquers stay on too: SAFE mode's B-dot needs
//  them and they draw little.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

#include "generated/dictionary.hpp"

namespace fsw::eps {

constexpr double kInternalResistanceOhm = 0.08;
constexpr double kHysteresisPct = 10.0;
constexpr double kShedTwoBandPct = 5.0;
constexpr double kOcvPullTauS = 3600.0;

constexpr uint16_t rail_bit(dict::PowerRail r) { return static_cast<uint16_t>(1u << static_cast<unsigned>(r)); }

constexpr uint16_t kProtectedRails =
    rail_bit(dict::PowerRail::OBC) | rail_bit(dict::PowerRail::RX) |
    rail_bit(dict::PowerRail::SURVIVAL_HEATERS);

// At boot the payload is off until the ground asks for it; everything else on.
constexpr uint16_t kDefaultGroundRails = static_cast<uint16_t>(0xFF & ~rail_bit(dict::PowerRail::PAYLOAD));

// Open-circuit voltage against state of charge, from the cell datasheet.
// The simulator's battery uses the same table: on a real spacecraft this is
// the one place the model and the hardware are supposed to agree.
inline double soc_from_ocv(double v) {
    static constexpr double kV[11] = {6.0, 6.9, 7.2, 7.35, 7.45, 7.55, 7.7, 7.85, 8.0, 8.2, 8.4};
    if (v <= kV[0]) { return 0.0; }
    if (v >= kV[10]) { return 100.0; }
    for (size_t i = 0; i < 10; ++i) {
        if (v < kV[i + 1]) {
            return 10.0 * (static_cast<double>(i) + (v - kV[i]) / (kV[i + 1] - kV[i]));
        }
    }
    return 100.0;
}

class SocEstimator {
 public:
    // v: terminal volts; i: amps, positive charging; dt seconds.
    double update(double v, double i, double dt, double capacity_wh) {
        const double ocv_soc = soc_from_ocv(v - i * kInternalResistanceOhm);
        if (!init_) {
            soc_ = ocv_soc;
            init_ = true;
            return soc_;
        }
        soc_ += 100.0 * (v * i * dt / 3600.0) / capacity_wh;
        // Pull gently toward the voltage-based figure, with a one-hour time
        // constant -- in seconds, not samples, so it means the same thing at
        // any sample rate. Slow enough that a load transient's voltage sag
        // barely registers; fast enough that counting errors cannot pile up
        // over days.
        soc_ += (dt / kOcvPullTauS) * (ocv_soc - soc_);
        if (soc_ < 0.0) { soc_ = 0.0; }
        if (soc_ > 100.0) { soc_ = 100.0; }
        return soc_;
    }
    bool   initialised() const { return init_; }
    double soc() const { return soc_; }

 private:
    bool   init_ = false;
    double soc_  = 0.0;
};

inline dict::PowerState next_power_state(dict::PowerState now, double soc, double low, double crit) {
    using PS = dict::PowerState;
    switch (now) {
        case PS::UNKNOWN:
            return soc < crit ? PS::CRITICAL : (soc < low ? PS::LOW : PS::NOMINAL);
        case PS::NOMINAL:
            return soc < crit ? PS::CRITICAL : (soc < low ? PS::LOW : PS::NOMINAL);
        case PS::LOW:
            if (soc < crit) { return PS::CRITICAL; }
            return soc > low + kHysteresisPct ? PS::NOMINAL : PS::LOW;
        case PS::CRITICAL:
            return soc > crit + kHysteresisPct ? PS::LOW : PS::CRITICAL;
    }
    return now;
}

inline uint8_t shed_level(dict::PowerState ps, double soc, double low, double crit,
                          uint8_t previous, bool safe_mode) {
    uint8_t level = 0;
    switch (ps) {
        case dict::PowerState::LOW: {
            const double mid = 0.5 * (low + crit);
            const bool deep = previous >= 2 ? soc < mid + kShedTwoBandPct : soc < mid;
            level = deep ? 2 : 1;
            break;
        }
        case dict::PowerState::CRITICAL: level = 3; break;
        default:                         level = 0; break;
    }
    if (safe_mode && level < 4) { level = 4; }
    return level;
}

// Which rails should be on. `tx_hold` is true while the ground has been heard
// recently: at shed level 2 the transmitter stays on only then, so a pass
// still gets its telemetry while the time between passes costs nothing.
inline uint16_t rail_policy(dict::SystemMode mode, uint8_t level, bool tx_hold, uint16_t ground_mask) {
    using R = dict::PowerRail;
    uint16_t on = ground_mask;
    if (level >= 1) { on = static_cast<uint16_t>(on & ~rail_bit(R::PAYLOAD)); }
    if (level >= 2 && !tx_hold) { on = static_cast<uint16_t>(on & ~rail_bit(R::TX)); }
    if (level >= 3) { on = static_cast<uint16_t>(on & ~rail_bit(R::OPS_HEATERS)); }
    if (level >= 4) { on = static_cast<uint16_t>(on & ~rail_bit(R::WHEELS)); }

    if (mode != dict::SystemMode::POINTING) { on = static_cast<uint16_t>(on & ~rail_bit(R::PAYLOAD)); }
    if (mode != dict::SystemMode::POINTING && mode != dict::SystemMode::STANDBY) {
        on = static_cast<uint16_t>(on & ~rail_bit(R::WHEELS));
    }
    return static_cast<uint16_t>(on | kProtectedRails);
}

}  // namespace fsw::eps
