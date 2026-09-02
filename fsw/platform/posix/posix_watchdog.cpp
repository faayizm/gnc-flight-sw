// ============================================================================
//  fsw/platform/posix/posix_watchdog.cpp
// ============================================================================
#include "platform/posix/posix_watchdog.hpp"

namespace fsw::platform {

void PosixWatchdog::enable(uint32_t timeout_ms) {
    enabled_    = true;
    timeout_ms_ = timeout_ms;
    last_kick_  = clock_.now();
}

void PosixWatchdog::kick() {
    if (!enabled_) { return; }

    const core::Instant now = clock_.now();
    const auto gap_ms = static_cast<uint32_t>((now - last_kick_).to_millis());

    if (gap_ms > longest_gap_ms_) { longest_gap_ms_ = gap_ms; }
    if (timeout_ms_ > 0 && gap_ms > timeout_ms_) {
        // On real hardware the processor would be resetting right now.
        ++would_have_reset_;
    }
    last_kick_ = now;
}

}  // namespace fsw::platform
