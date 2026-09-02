// ============================================================================
//  fsw/core/scheduler.hpp -- deterministic rate-group scheduler.
//
//  This is the heartbeat of the flight software, and its design is the single
//  decision that most affects whether the system is analysable.
//
//  WHY NOT THREADS. A preemptive multi-threaded design gives you races,
//  priority inversion, and an execution order that differs between two runs of
//  the same scenario. On a spacecraft that means a bug seen once in orbit can
//  never be reproduced on the ground. Here there is ONE thread. Tasks run in a
//  fixed, declared order at fixed rates. The same inputs produce the same
//  outputs, every time, on every machine -- which is what makes the simulator
//  worth anything at all.
//
//  HOW IT WORKS. A base tick runs at kBaseRateHz. Each task declares a divider
//  (run every N ticks) and an offset (which of those N ticks). Offsets let the
//  load be spread out: two 10 Hz tasks on a 50 Hz base can be given offsets 0
//  and 2 so they never execute on the same tick.
//
//      tick:     0  1  2  3  4  5  6  7  8  9
//      div 1:    X  X  X  X  X  X  X  X  X  X     50 Hz
//      div 5/0:  X           X           X        10 Hz, offset 0
//      div 5/2:        X           X           X  10 Hz, offset 2
//
//  DEADLINES. After running a tick the scheduler compares elapsed time against
//  the tick period. Exceeding it is an overrun: it is counted, reported in
//  housekeeping, and raises an event. The scheduler does NOT try to catch up by
//  running ticks back to back, because that turns one late task into a
//  cascading collapse. It skips ahead to the next real deadline instead.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/static_vector.hpp"
#include "core/status.hpp"
#include "core/time.hpp"
#include "hal/clock.hpp"

namespace fsw::core {

// A plain function pointer plus a context pointer, rather than std::function,
// which would allocate. This is the standard flight idiom for a callback.
using TaskFn = void (*)(void* context);

struct Task {
    const char* name     = nullptr;
    TaskFn      fn       = nullptr;
    void*       context  = nullptr;
    uint32_t    divider  = 1;   // run every `divider` base ticks
    uint32_t    offset   = 0;   // on ticks where (tick % divider) == offset

    // Execution statistics, in microseconds. Worst case is what matters for a
    // timing budget; average is only useful for spotting drift.
    uint32_t    runs         = 0;
    uint32_t    last_us      = 0;
    uint32_t    max_us       = 0;
    uint64_t    total_us     = 0;
};

class Scheduler {
 public:
    // 50 Hz base. Fast enough for an attitude control loop on a small
    // satellite, slow enough that a 20 ms budget is comfortable.
    static constexpr uint32_t kBaseRateHz  = 50;
    static constexpr size_t   kMaxTasks    = 16;

    explicit Scheduler(hal::IClock& clock) : clock_(clock) {}

    // Register a task. All registration happens during initialisation, before
    // the loop starts; the task list never changes in flight, so there is no
    // question of what happens if a task is added mid-tick.
    Status add_task(const char* name, TaskFn fn, void* context,
                    uint32_t divider, uint32_t offset = 0);

    // Run exactly one base tick: every task whose turn it is, in registration
    // order. Returns false if the tick overran its deadline.
    bool run_tick();

    // Block until the next tick boundary, then run it. This is the real-time
    // main loop body. Returns false on overrun.
    bool run_tick_realtime();

    // Called once before the first tick, to anchor the tick timeline.
    void start();

    uint32_t tick_count()     const { return tick_; }
    uint32_t overrun_count()  const { return overruns_; }

    // Occupancy over the last second, as a percentage of available time.
    // This is the number to watch: a flight processor comfortably below 50%
    // has margin for the worst case; one at 90% does not.
    uint8_t  load_percent()   const { return load_pct_; }

    static constexpr Duration tick_period() {
        return Duration::micros(1000000 / kBaseRateHz);
    }

    const StaticVector<Task, kMaxTasks>& tasks() const { return tasks_; }

    // Uptime in whole seconds, derived from the tick count so that it agrees
    // exactly with the scheduler's own timeline rather than the wall clock.
    uint32_t uptime_s() const { return tick_ / kBaseRateHz; }

    // Installed by the event log so an overrun can be reported to the ground
    // without the scheduler having to know what an event is.
    using OverrunFn = void (*)(void* context, const char* task_name, uint32_t used_us);
    void set_overrun_handler(OverrunFn fn, void* context) {
        overrun_fn_ = fn;
        overrun_ctx_ = context;
    }

 private:
    hal::IClock&                  clock_;
    StaticVector<Task, kMaxTasks> tasks_;
    uint32_t                      tick_       = 0;
    uint32_t                      overruns_   = 0;
    uint8_t                       load_pct_   = 0;
    Instant                       next_deadline_{};
    uint64_t                      busy_us_accum_ = 0;
    uint32_t                      load_window_ticks_ = 0;
    OverrunFn                     overrun_fn_  = nullptr;
    void*                         overrun_ctx_ = nullptr;
};

}  // namespace fsw::core
