// ============================================================================
//  fsw/hal/watchdog.hpp -- the last line of defence.
//
//  A hardware watchdog is a counter that resets the processor unless the
//  software keeps telling it not to. It is the one fault-tolerance mechanism
//  that survives the software having gone completely wrong, which is precisely
//  why it must be kicked from ONE place in the main loop, after the scheduler
//  confirms every rate group actually ran.
//
//  The classic mistake is kicking it from a timer interrupt. Then the watchdog
//  faithfully proves the interrupt controller is alive while the application
//  is deadlocked, and the spacecraft is lost with a perfectly healthy watchdog.
// ============================================================================
#pragma once

#include <cstdint>

namespace fsw::hal {

class IWatchdog {
 public:
    virtual ~IWatchdog() = default;

    // Arm with a timeout. Expiry after this long without a kick means reset.
    virtual void enable(uint32_t timeout_ms) = 0;

    // "I am still making progress." Call from the main loop only.
    virtual void kick() = 0;

    // How many times this processor has been reset by the watchdog, read back
    // from non-volatile storage at boot. Downlinked in housekeeping, because
    // a rising count is one of the few unambiguous signals of a real problem.
    virtual uint16_t reset_count() const = 0;
};

}  // namespace fsw::hal
