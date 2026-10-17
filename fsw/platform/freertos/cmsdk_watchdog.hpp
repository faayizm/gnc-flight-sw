// ============================================================================
//  fsw/platform/freertos/cmsdk_watchdog.hpp -- the Arm CMSDK APB watchdog.
//
//  A real one, this time: unlike PosixWatchdog it does not model anything.
//  The counter runs down from LOAD on the peripheral clock. The first time it
//  reaches zero it raises an interrupt -- wired to the NMI on the MPS2 boards
//  -- and starts again; if it reaches zero a second time with that interrupt
//  still uncleared, it resets the system. A kick clears the interrupt, which
//  also reloads the counter. So the time from the last kick to the reset is
//  two counter periods, and enable() programs half the timeout into LOAD.
//
//  The NMI is the software's last word: the board's handler records "this
//  was the watchdog" in RAM that survives the reset, and does not clear the
//  interrupt, so the reset follows (targets/mps2-an500/main.cpp).
// ============================================================================
#pragma once

#include <cstdint>

#include "hal/watchdog.hpp"

namespace fsw::platform {

class CmsdkWatchdog final : public hal::IWatchdog {
 public:
    // `base` is the register block; `clock_hz` the clock it counts.
    // `reset_count` is how many watchdog resets the board has counted, which
    // it keeps in RAM that survives a reset.
    CmsdkWatchdog(uintptr_t base, uint32_t clock_hz, uint16_t reset_count)
        : base_(base), clock_hz_(clock_hz), resets_(reset_count) {}

    void     enable(uint32_t timeout_ms) override;
    void     kick() override;
    uint16_t reset_count() const override { return resets_; }

 private:
    uintptr_t base_;
    uint32_t  clock_hz_;
    uint16_t  resets_;
};

}  // namespace fsw::platform
