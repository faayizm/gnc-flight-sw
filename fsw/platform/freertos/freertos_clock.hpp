// ============================================================================
//  fsw/platform/freertos/freertos_clock.hpp -- IClock under FreeRTOS.
//
//  Monotonic time is the RTOS tick count (1 kHz) refined by the SysTick
//  counter inside the current tick, so a task's run time can be measured in
//  microseconds rather than whole milliseconds. Widened to 64 bits here: the
//  32-bit tick count wraps after 49 days, and IClock promises never to jump.
//
//  The time scale works as it does on the laptop: spacecraft time runs
//  `time_scale` times faster than the board's own clock, and host_now() is
//  the board's own clock, unscaled. On a real flight computer the scale is 1.
//
//  sleep_until() blocks the task in the RTOS for whole ticks, rounded up, and
//  the idle task sleeps the processor meanwhile. So a scheduler tick starts up
//  to 1 ms of board time late, which at real time is 5% jitter on a 20 ms tick
//  and harmless. At high time scales it is not: at 100x a scheduler tick is
//  200 us of board time, shorter than one RTOS tick, so ticks come no faster
//  than one per millisecond, and the spacecraft runs at about 20x however
//  fast it was asked to. Busy-waiting instead would be precise, and would
//  cost a real board its power budget for nothing.
// ============================================================================
#pragma once

#include <cstdint>

#include "hal/clock.hpp"

namespace fsw::platform {

class FreeRtosClock final : public hal::IClock {
 public:
    // `core_hz` is the SysTick clock: the core clock, which FreeRTOS is
    // configured to count.
    explicit FreeRtosClock(uint32_t core_hz) : core_hz_(core_hz) {}

    // Set before the scheduler starts; changing it later would make now()
    // jump.
    void set_time_scale(double scale) { time_scale_ = scale > 0.0 ? scale : 1.0; }
    double time_scale() const { return time_scale_; }

    core::Instant now() override;
    core::Instant host_now() override;
    double mission_time_s() override;
    void   set_mission_time_s(double seconds) override;
    void   sleep_until(core::Instant deadline) override;

 private:
    int64_t board_us();

    uint32_t core_hz_;
    double   time_scale_  = 1.0;
    uint32_t last_ticks_  = 0;   // tick count at the last reading, to spot the wrap
    uint64_t wraps_       = 0;   // how many times the 32-bit tick count has wrapped
    double   mission_epoch_offset_s_ = 0.0;
};

}  // namespace fsw::platform
