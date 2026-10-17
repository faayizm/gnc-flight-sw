// ============================================================================
//  fsw/platform/freertos/freertos_clock.cpp
// ============================================================================
#include "platform/freertos/freertos_clock.hpp"

#include "FreeRTOS.h"
#include "task.h"
#include "platform/freertos/cortex_m.hpp"

namespace fsw::platform {

int64_t FreeRtosClock::board_us() {
    // Tick count and SysTick counter, read together with interrupts masked.
    // If the counter has wrapped but its interrupt has not run yet, the tick
    // count is one behind the counter: add it, or time steps back 1 ms.
    const uint32_t saved = cm::irq_save();
    uint32_t ticks = xTaskGetTickCount();
    uint32_t value = cm::systick_value();
    if ((cm::scb_icsr() & cm::kIcsrPendSysTick) != 0) {
        value = cm::systick_value();
        ++ticks;
    }
    const uint32_t load = cm::systick_load();
    if (ticks < last_ticks_) { ++wraps_; }
    last_ticks_ = ticks;
    cm::irq_restore(saved);

    const uint64_t all_ticks = (wraps_ << 32) | ticks;
    const uint64_t into_tick = (static_cast<uint64_t>(load - value) * 1000000u) / core_hz_;
    return static_cast<int64_t>(all_ticks * (1000000u / configTICK_RATE_HZ) + into_tick);
}

core::Instant FreeRtosClock::now() {
    return core::Instant::from_micros(
        static_cast<int64_t>(static_cast<double>(board_us()) * time_scale_));
}

core::Instant FreeRtosClock::host_now() {
    return core::Instant::from_micros(board_us());
}

double FreeRtosClock::mission_time_s() {
    return mission_epoch_offset_s_ + static_cast<double>(now().to_micros()) * 1e-6;
}

void FreeRtosClock::set_mission_time_s(double seconds) {
    mission_epoch_offset_s_ = seconds - static_cast<double>(now().to_micros()) * 1e-6;
}

void FreeRtosClock::sleep_until(core::Instant deadline) {
    const core::Instant current = now();
    if (deadline < current) { return; }
    const auto real_us = static_cast<int64_t>(
        static_cast<double>((deadline - current).to_micros()) / time_scale_);
    // Whole RTOS ticks, rounded up: wake at or just after the deadline, never
    // before it. Meanwhile the idle task sleeps the processor (wfi).
    const int64_t tick_us = 1000000 / configTICK_RATE_HZ;
    vTaskDelay(static_cast<TickType_t>((real_us + tick_us - 1) / tick_us));
}

}  // namespace fsw::platform
