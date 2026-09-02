// ============================================================================
//  fsw/core/scheduler.cpp -- see scheduler.hpp for the design rationale.
// ============================================================================
#include "core/scheduler.hpp"

namespace fsw::core {

Status Scheduler::add_task(const char* name, TaskFn fn, void* context,
                           uint32_t divider, uint32_t offset) {
    if (fn == nullptr || name == nullptr) { return Status::Invalid; }
    if (divider == 0) { return Status::Invalid; }
    // An offset at or beyond the divider would never fire, which is almost
    // certainly a configuration mistake rather than an intention.
    if (offset >= divider) { return Status::OutOfRange; }
    if (tasks_.full()) { return Status::NoSpace; }

    Task t;
    t.name    = name;
    t.fn      = fn;
    t.context = context;
    t.divider = divider;
    t.offset  = offset;
    tasks_.push_back(t);
    return Status::Ok;
}

void Scheduler::start() {
    next_deadline_ = clock_.now() + tick_period();
}

bool Scheduler::run_tick() {
    const Instant tick_start = clock_.now();

    for (size_t i = 0; i < tasks_.size(); ++i) {
        Task& t = tasks_[i];
        if ((tick_ % t.divider) != t.offset) { continue; }

        const Instant task_start = clock_.now();
        t.fn(t.context);
        const auto elapsed = static_cast<uint32_t>(
            (clock_.now() - task_start).to_micros());

        t.last_us = elapsed;
        t.total_us += elapsed;
        ++t.runs;
        if (elapsed > t.max_us) { t.max_us = elapsed; }
    }

    const auto busy_us = static_cast<uint64_t>((clock_.now() - tick_start).to_micros());
    ++tick_;

    // Recompute occupancy once per second so the figure is stable enough to
    // read on a ground display rather than flickering every tick.
    busy_us_accum_ += busy_us;
    if (++load_window_ticks_ >= kBaseRateHz) {
        const uint64_t window_us = static_cast<uint64_t>(tick_period().to_micros()) *
                                   load_window_ticks_;
        uint64_t pct = (busy_us_accum_ * 100) / (window_us == 0 ? 1 : window_us);
        if (pct > 100) { pct = 100; }
        load_pct_ = static_cast<uint8_t>(pct);
        busy_us_accum_     = 0;
        load_window_ticks_ = 0;
    }

    const bool overran = busy_us > static_cast<uint64_t>(tick_period().to_micros());
    if (overran) {
        ++overruns_;
        if (overrun_fn_ != nullptr) {
            // Attribute the overrun to the slowest task that ran, which is the
            // most useful single piece of information for diagnosing it.
            const char* worst_name = "?";
            uint32_t    worst_us   = 0;
            for (size_t i = 0; i < tasks_.size(); ++i) {
                const Task& t = tasks_[i];
                if (((tick_ - 1) % t.divider) == t.offset && t.last_us > worst_us) {
                    worst_us   = t.last_us;
                    worst_name = t.name;
                }
            }
            overrun_fn_(overrun_ctx_, worst_name, static_cast<uint32_t>(busy_us));
        }
    }
    return !overran;
}

bool Scheduler::run_tick_realtime() {
    const bool on_time = run_tick();

    const Instant now = clock_.now();
    if (now >= next_deadline_) {
        // We are already late. Do NOT run catch-up ticks: that converts a
        // single late tick into a burst of back-to-back work, which makes the
        // next tick late too, and so on until the system falls over. Re-anchor
        // to the future instead and accept the lost tick, which has been
        // counted as an overrun.
        next_deadline_ = now + tick_period();
    } else {
        clock_.sleep_until(next_deadline_);
        next_deadline_ = next_deadline_ + tick_period();
    }
    return on_time;
}

}  // namespace fsw::core
