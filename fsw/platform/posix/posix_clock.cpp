// ============================================================================
//  fsw/platform/posix/posix_clock.cpp
// ============================================================================
#include "platform/posix/posix_clock.hpp"

#include <ctime>
#include <cerrno>

namespace fsw::platform {

PosixClock::PosixClock(double time_scale)
    : time_scale_(time_scale > 0.0 ? time_scale : 1.0) {
    boot_raw_us_ = raw_monotonic_us();
}

int64_t PosixClock::raw_monotonic_us() const {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000 +
           static_cast<int64_t>(ts.tv_nsec) / 1000;
}

core::Instant PosixClock::now() {
    // Scale elapsed time, not the absolute reading, so that changing the scale
    // never makes the monotonic clock jump backwards.
    const int64_t elapsed_raw = raw_monotonic_us() - boot_raw_us_;
    const auto    scaled = static_cast<int64_t>(static_cast<double>(elapsed_raw) * time_scale_);
    return core::Instant::from_micros(scaled);
}

double PosixClock::mission_time_s() {
    return mission_epoch_offset_s_ +
           static_cast<double>(now().to_micros()) * 1e-6;
}

void PosixClock::set_mission_time_s(double seconds) {
    // Re-anchor the offset so that mission_time_s() returns exactly what was
    // requested at this instant. Monotonic time is untouched.
    mission_epoch_offset_s_ = seconds - static_cast<double>(now().to_micros()) * 1e-6;
}

void PosixClock::sleep_until(core::Instant deadline) {
    const core::Instant current = now();
    if (deadline < current) { return; }

    // Convert the scaled interval back into real time before sleeping.
    const int64_t scaled_us = (deadline - current).to_micros();
    const auto    real_us   = static_cast<int64_t>(static_cast<double>(scaled_us) / time_scale_);
    if (real_us <= 0) { return; }

    timespec req{};
    req.tv_sec  = static_cast<time_t>(real_us / 1000000);
    req.tv_nsec = static_cast<long>((real_us % 1000000) * 1000);

    // Resume the sleep if a signal interrupts it, so a debugger attaching
    // does not silently shorten a tick.
    timespec rem{};
    while (nanosleep(&req, &rem) == -1 && errno == EINTR) {
        req = rem;
    }
}

}  // namespace fsw::platform
