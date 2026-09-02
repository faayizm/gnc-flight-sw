// ============================================================================
//  fsw/platform/posix/posix_watchdog.hpp -- a watchdog that cannot reset a
//  processor, because there is no processor to reset.
//
//  On a hosted build there is no hardware watchdog. Rather than stub the
//  interface out to nothing, this implementation MEASURES what a real watchdog
//  would have done: it records the longest gap between kicks and reports how
//  many times that gap exceeded the configured timeout.
//
//  That turns a component which would otherwise be untestable on the ground
//  into a source of evidence. If the hosted build reports that the loop went
//  quiet for longer than the timeout, the flight build on real hardware would
//  have reset -- and it is far better to learn that here.
// ============================================================================
#pragma once

#include <cstdint>

#include "hal/clock.hpp"
#include "hal/watchdog.hpp"

namespace fsw::platform {

class PosixWatchdog final : public hal::IWatchdog {
 public:
    explicit PosixWatchdog(hal::IClock& clock) : clock_(clock) {}

    void     enable(uint32_t timeout_ms) override;
    void     kick() override;
    uint16_t reset_count() const override { return would_have_reset_; }

    uint32_t longest_gap_ms() const { return longest_gap_ms_; }
    bool     enabled() const { return enabled_; }

 private:
    hal::IClock&  clock_;
    bool          enabled_          = false;
    uint32_t      timeout_ms_       = 0;
    core::Instant last_kick_{};
    uint32_t      longest_gap_ms_   = 0;
    uint16_t      would_have_reset_ = 0;
};

}  // namespace fsw::platform
