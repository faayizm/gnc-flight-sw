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
//
//  AND IT CAN BITE. arm_host() adds a real watchdog on the host's own clock:
//  if the loop stops kicking for that long, a timer signal writes a reset-
//  cause marker next to the non-volatile storage file and ends the process,
//  as a reset would. Whoever plays the hardware (a test harness, or a shell
//  loop) starts it again, and the next boot finds the marker -- the hosted
//  equivalent of a processor's reset-cause register. The host timeout is
//  seconds, not the flight build's tens of milliseconds: it is there to
//  catch a wedged loop, and a busy test machine is not one.
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

    // Real expiry on the host clock. `marker_path` must outlive the process.
    void arm_host(uint32_t timeout_ms, const char* marker_path);

    // Exit status of a process ended by the host watchdog.
    static constexpr int kResetExitCode = 86;

    // At boot: was the last reset this watchdog? Reads and clears the marker.
    static bool consume_reset_marker(const char* marker_path);

    uint32_t longest_gap_ms() const { return longest_gap_ms_; }
    bool     enabled() const { return enabled_; }

 private:
    hal::IClock&  clock_;
    bool          enabled_          = false;
    uint32_t      timeout_ms_       = 0;
    core::Instant last_kick_{};
    uint32_t      longest_gap_ms_   = 0;
    uint16_t      would_have_reset_ = 0;
    uint32_t      host_timeout_ms_  = 0;
};

}  // namespace fsw::platform
