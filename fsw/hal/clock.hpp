// ============================================================================
//  fsw/hal/clock.hpp -- the time port.
//
//  The flight core never calls clock_gettime, never calls sleep. It asks this
//  interface. That single indirection buys three things:
//
//    * the simulator can run the whole spacecraft faster than real time, or
//      step it one tick at a time under a debugger
//    * unit tests are deterministic -- a test clock advances only when the
//      test says so, so a "wait five minutes" behaviour costs no wall time
//    * porting to an RTOS or bare metal replaces one small class
// ============================================================================
#pragma once

#include "core/time.hpp"

namespace fsw::hal {

class IClock {
 public:
    virtual ~IClock() = default;

    // Monotonic time since boot. Must never decrease and must never jump.
    virtual core::Instant now() = 0;

    // Host time, monotonic and never scaled. On flight hardware it is now().
    // In an accelerated simulation it is the computer's own clock, for the one
    // kind of check that is about the computer rather than the spacecraft: is
    // the process on the other end of a link still alive? At 100x, a 20 ms
    // pause of the host is two seconds of spacecraft time, and a watchdog on
    // spacecraft time would see a dead link that is merely busy.
    virtual core::Instant host_now() { return now(); }

    // Mission time, seconds since the mission epoch. May be steered by the
    // ground, so it MAY jump. Never use it to measure an interval.
    virtual double mission_time_s() = 0;

    // Apply a time correlation from the ground.
    virtual void set_mission_time_s(double seconds) = 0;

    // Yield until roughly the given instant. A real-time platform sleeps; an
    // accelerated simulation may return immediately, which is exactly the
    // point of putting it behind the port.
    virtual void sleep_until(core::Instant deadline) = 0;
};

}  // namespace fsw::hal
