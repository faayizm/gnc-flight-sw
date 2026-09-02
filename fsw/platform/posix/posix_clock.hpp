// ============================================================================
//  fsw/platform/posix/posix_clock.hpp -- IClock on a hosted POSIX system.
//
//  Also the place where simulation time acceleration lives. A time_scale of
//  1.0 is real time; 10.0 runs the spacecraft ten times faster than the wall
//  clock, which turns a 90-minute orbit into a nine-minute test. The flight
//  software above this class cannot tell the difference, which is the whole
//  reason IClock exists.
//
//  Monotonic time comes from CLOCK_MONOTONIC, which by definition is unaffected
//  by anyone adjusting the system date underneath a running test.
// ============================================================================
#pragma once

#include "hal/clock.hpp"

namespace fsw::platform {

class PosixClock final : public hal::IClock {
 public:
    explicit PosixClock(double time_scale = 1.0);

    core::Instant now() override;
    double mission_time_s() override;
    void   set_mission_time_s(double seconds) override;
    void   sleep_until(core::Instant deadline) override;

    double time_scale() const { return time_scale_; }

 private:
    int64_t raw_monotonic_us() const;

    double  time_scale_;
    int64_t boot_raw_us_    = 0;   // raw monotonic reading at construction
    double  mission_epoch_offset_s_ = 0.0;  // mission time at boot
};

}  // namespace fsw::platform
