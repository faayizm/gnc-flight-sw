// ============================================================================
//  fsw/apps/fdir/wheel_ladder.hpp -- what to do about a wheel that has failed.
//
//  FDIR -- fault detection, isolation and recovery -- is a ladder. Each rung
//  costs more than the one below it, so the software climbs only as far as
//  the fault forces it to:
//
//    1. REPORT       raise an event, so the ground knows, whatever happens next.
//    2. RETRY        switch the wheel drives off and on again. Many faults in
//                    orbit are not broken hardware but a latch-up: a particle
//                    has turned on a parasitic current path, and removing the
//                    power is the only thing that turns it off. A power cycle
//                    costs three seconds of attitude control. Isolating a wheel
//                    that only needed a power cycle costs it for the rest of
//                    the mission.
//    3. ISOLATE AND  if the wheel still fails after its retry, stop using it.
//       RECONFIGURE  The attitude controller is told which wheels remain, and
//                    carries on without the failed one (adcs/pointing.hpp).
//    4. SAFE MODE    with fewer than two wheels left there is nothing to
//                    reconfigure into. That decision belongs to the mode
//                    manager, which is told how many wheels are usable.
//
//      MONITOR ──fault──▶ CYCLE_OFF ──3 s──▶ VERIFY ──3 good windows──▶ MONITOR
//                                              │                (RECOVERED)
//                                              └──fails again──▶ ISOLATED
//
//  A wheel is retried at most kMaxRetries times. A latch-up can happen again
//  -- a busy radiation belt hits the same part more than once -- but a wheel
//  that keeps failing is failing, and further power cycles only cost control.
//
//  Pure logic, driven by time and the wheel check's verdicts; the FDIR
//  application turns its outputs into events and power switching.
// ============================================================================
#pragma once

#include <cstdint>

#include "core/tmr.hpp"
#include "generated/dictionary.hpp"

namespace fsw::fdir {

struct LadderEvent {
    dict::EventId id;
    uint32_t      aux;
};

struct LadderOutput {
    bool        wheels_off  = false;   // ask EPS to remove power from the drives
    bool        reset_check = false;   // the evidence so far is stale: start again
    int         event_count = 0;
    LadderEvent events[3]{};

    void raise(dict::EventId id, uint32_t aux) {
        if (event_count < 3) { events[event_count++] = LadderEvent{id, aux}; }
    }
};

class WheelLadder {
 public:
    static constexpr double  kOffS          = 3.0;
    static constexpr double  kVerifyLimitS  = 60.0;
    static constexpr uint32_t kGoodToRecover = 3;
    static constexpr uint8_t kMaxRetries    = 2;

    // `failing` is the wheel check's current verdict; `good` the number of
    // passing windows each axis has had since the check was last reset.
    LadderOutput step(double t, uint8_t failing, const uint32_t good[3]) {
        LadderOutput out;
        failing = static_cast<uint8_t>(failing & ~isolated_.get());
        switch (state_) {
            case dict::FdirWheelState::MONITOR:
                if (failing != 0) { begin(t, failing, out); }
                break;

            case dict::FdirWheelState::CYCLE_OFF:
                out.wheels_off = true;
                if (t - since_ >= kOffS) {
                    out.wheels_off = false;
                    out.reset_check = true;
                    state_ = dict::FdirWheelState::VERIFY;
                    since_ = t;
                }
                break;

            case dict::FdirWheelState::VERIFY: {
                const uint8_t still = static_cast<uint8_t>(failing & suspects_);
                const bool timed_out = t - since_ > kVerifyLimitS;
                if (still != 0 || timed_out) {
                    // The retry did not help -- or never settled the question,
                    // which is treated the same way: a wheel nobody can vouch
                    // for is not used.
                    isolate(timed_out ? suspects_ : still, out);
                }
                uint8_t cleared = 0;
                for (int i = 0; i < 3; ++i) {
                    const auto bit = static_cast<uint8_t>(1u << i);
                    if ((suspects_ & bit) && good[i] >= kGoodToRecover) { cleared |= bit; }
                }
                if (cleared != 0) {
                    suspects_ = static_cast<uint8_t>(suspects_ & ~cleared);
                    out.raise(dict::EventId::WHEEL_RECOVERED, cleared);
                }
                if (suspects_ == 0) { state_ = dict::FdirWheelState::MONITOR; }
                break;
            }
        }
        return out;
    }

    dict::FdirWheelState state() const { return state_; }
    uint8_t isolated() const { return isolated_.vote(); }
    uint8_t usable()   const { return static_cast<uint8_t>(~isolated_.vote() & 0x7u); }
    uint8_t retries(int axis) const { return retries_[axis]; }

    // The ground's override: put an isolated wheel back into service (after,
    // say, diagnosing it from telemetry). Its retry budget is restored too.
    void restore(uint8_t mask) {
        isolated_.set(static_cast<uint8_t>(isolated_.get() & ~mask));
        for (int i = 0; i < 3; ++i) {
            if ((mask >> i) & 1u) { retries_[i] = 0; }
        }
    }

    // Which wheels are out of service is kept in triplicate (core/tmr.hpp):
    // an upset that "repaired" a dead wheel would hand it back to the
    // controller, and one that "killed" a good wheel would throw it away.
    core::Tmr<uint8_t>& isolation_store() { return isolated_; }

 private:
    void begin(double t, uint8_t failing, LadderOutput& out) {
        out.raise(dict::EventId::WHEEL_FAULT, failing);
        uint8_t retry = 0;
        uint8_t give_up = 0;
        for (int i = 0; i < 3; ++i) {
            const auto bit = static_cast<uint8_t>(1u << i);
            if (!(failing & bit)) { continue; }
            if (retries_[i] < kMaxRetries) {
                ++retries_[i];
                retry |= bit;
            } else {
                give_up |= bit;
            }
        }
        if (give_up != 0) { isolate(give_up, out); }
        if (retry != 0) {
            suspects_ = retry;
            state_ = dict::FdirWheelState::CYCLE_OFF;
            since_ = t;
            out.wheels_off = true;
            out.raise(dict::EventId::WHEEL_POWER_CYCLE, retry);
        }
    }

    void isolate(uint8_t mask, LadderOutput& out) {
        if (mask == 0) { return; }
        isolated_.set(static_cast<uint8_t>(isolated_.get() | mask));
        suspects_ = static_cast<uint8_t>(suspects_ & ~mask);
        out.raise(dict::EventId::WHEEL_ISOLATED, mask);
    }

    dict::FdirWheelState state_ = dict::FdirWheelState::MONITOR;
    double  since_    = 0.0;
    uint8_t suspects_ = 0;
    core::Tmr<uint8_t> isolated_{0};
    uint8_t retries_[3]{};
};

}  // namespace fsw::fdir
