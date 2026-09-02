// ============================================================================
//  Tests for the rate-group scheduler.
//
//  These use a TestClock rather than the real one, which is exactly the payoff
//  of putting time behind hal::IClock: a test that a task runs at 1 Hz for a
//  simulated hour takes microseconds and produces the same answer every time.
// ============================================================================
#include "core/scheduler.hpp"
#include "framework.hpp"

using fsw::core::Duration;
using fsw::core::Instant;
using fsw::core::Scheduler;
using fsw::core::Status;

namespace {

// A clock that moves only when the test says so. It can also be told to charge
// a fixed cost to every task, which is how deadline behaviour is exercised
// without relying on real execution time.
class TestClock final : public fsw::hal::IClock {
 public:
    Instant now() override { return Instant::from_micros(us_); }
    double  mission_time_s() override { return mission_s_ + static_cast<double>(us_) * 1e-6; }
    void    set_mission_time_s(double s) override { mission_s_ = s - static_cast<double>(us_) * 1e-6; }

    // A test clock never really sleeps: it jumps straight to the deadline.
    void sleep_until(Instant deadline) override {
        if (deadline.to_micros() > us_) { us_ = deadline.to_micros(); }
        ++sleeps_;
    }

    void     advance(int64_t micros) { us_ += micros; }
    int64_t  micros() const { return us_; }
    uint32_t sleeps() const { return sleeps_; }

 private:
    int64_t  us_        = 0;
    double   mission_s_ = 0.0;
    uint32_t sleeps_    = 0;
};

struct Counter {
    uint32_t runs = 0;
    TestClock* clock = nullptr;
    int64_t cost_us = 0;

    static void run(void* ctx) {
        auto* self = static_cast<Counter*>(ctx);
        ++self->runs;
        if (self->clock != nullptr && self->cost_us > 0) {
            self->clock->advance(self->cost_us);
        }
    }
};

}  // namespace

TEST(scheduler, a_task_with_divider_one_runs_every_tick) {
    TestClock clock;
    Scheduler s(clock);
    Counter c;
    CHECK(fsw::core::is_ok(s.add_task("every", &Counter::run, &c, 1)));

    for (int i = 0; i < 10; ++i) { s.run_tick(); }
    CHECK_EQ(c.runs, 10u);
    CHECK_EQ(s.tick_count(), 10u);
}

TEST(scheduler, dividers_produce_the_declared_rates) {
    TestClock clock;
    Scheduler s(clock);
    Counter fast, medium, slow;
    s.add_task("50hz", &Counter::run, &fast, 1);
    s.add_task("10hz", &Counter::run, &medium, 5);
    s.add_task("1hz",  &Counter::run, &slow, Scheduler::kBaseRateHz);

    // One simulated second at the 50 Hz base rate.
    for (uint32_t i = 0; i < Scheduler::kBaseRateHz; ++i) { s.run_tick(); }
    CHECK_EQ(fast.runs, 50u);
    CHECK_EQ(medium.runs, 10u);
    CHECK_EQ(slow.runs, 1u);
}

TEST(scheduler, offsets_keep_same_rate_tasks_off_the_same_tick) {
    TestClock clock;
    Scheduler s(clock);
    Counter a, b;
    s.add_task("a", &Counter::run, &a, 5, 0);
    s.add_task("b", &Counter::run, &b, 5, 2);

    // After 3 ticks (0,1,2): a has run on tick 0, b on tick 2, never together.
    s.run_tick(); CHECK_EQ(a.runs, 1u); CHECK_EQ(b.runs, 0u);
    s.run_tick(); CHECK_EQ(a.runs, 1u); CHECK_EQ(b.runs, 0u);
    s.run_tick(); CHECK_EQ(a.runs, 1u); CHECK_EQ(b.runs, 1u);
}

TEST(scheduler, tasks_run_in_registration_order) {
    TestClock clock;
    Scheduler s(clock);

    static int sequence[4];
    static int index;
    index = 0;

    struct Recorder {
        static void first(void*)  { sequence[index++] = 1; }
        static void second(void*) { sequence[index++] = 2; }
    };
    s.add_task("first",  &Recorder::first,  nullptr, 1);
    s.add_task("second", &Recorder::second, nullptr, 1);

    s.run_tick();
    s.run_tick();
    CHECK_EQ(sequence[0], 1);
    CHECK_EQ(sequence[1], 2);
    CHECK_EQ(sequence[2], 1);
    CHECK_EQ(sequence[3], 2);
}

TEST(scheduler, an_offset_at_or_beyond_the_divider_is_refused) {
    TestClock clock;
    Scheduler s(clock);
    Counter c;
    // Would never fire. Almost certainly a typo, so it is an error rather
    // than a silently dead task.
    CHECK(s.add_task("bad", &Counter::run, &c, 5, 5) == Status::OutOfRange);
    CHECK(s.add_task("bad", &Counter::run, &c, 0, 0) == Status::Invalid);
    CHECK(s.add_task("bad", nullptr, &c, 1, 0) == Status::Invalid);
}

TEST(scheduler, the_task_table_is_bounded) {
    TestClock clock;
    Scheduler s(clock);
    Counter c;
    for (size_t i = 0; i < Scheduler::kMaxTasks; ++i) {
        CHECK(fsw::core::is_ok(s.add_task("t", &Counter::run, &c, 1)));
    }
    CHECK(s.add_task("one_too_many", &Counter::run, &c, 1) == Status::NoSpace);
}

TEST(scheduler, overrunning_the_tick_period_is_detected_and_counted) {
    TestClock clock;
    Scheduler s(clock);

    Counter greedy;
    greedy.clock   = &clock;
    greedy.cost_us = Scheduler::tick_period().to_micros() + 1000;   // 1 ms over
    s.add_task("greedy", &Counter::run, &greedy, 1);

    CHECK(!s.run_tick());
    CHECK_EQ(s.overrun_count(), 1u);
}

TEST(scheduler, a_task_inside_its_budget_does_not_trip_the_deadline) {
    TestClock clock;
    Scheduler s(clock);

    Counter polite;
    polite.clock   = &clock;
    polite.cost_us = Scheduler::tick_period().to_micros() / 2;
    s.add_task("polite", &Counter::run, &polite, 1);

    CHECK(s.run_tick());
    CHECK_EQ(s.overrun_count(), 0u);
}

TEST(scheduler, an_overrun_is_reported_to_the_installed_handler) {
    TestClock clock;
    Scheduler s(clock);

    static uint32_t reported;
    static const char* reported_name;
    reported = 0;
    reported_name = nullptr;
    s.set_overrun_handler([](void*, const char* name, uint32_t used_us) {
        reported = used_us;
        reported_name = name;
    }, nullptr);

    Counter greedy;
    greedy.clock   = &clock;
    greedy.cost_us = Scheduler::tick_period().to_micros() * 2;
    s.add_task("greedy", &Counter::run, &greedy, 1);

    s.run_tick();
    CHECK(reported > static_cast<uint32_t>(Scheduler::tick_period().to_micros()));
    CHECK(reported_name != nullptr);
    // The slowest task that ran is named, which is the useful diagnostic.
    CHECK_EQ(std::string(reported_name), std::string("greedy"));
}

TEST(scheduler, execution_statistics_are_recorded_per_task) {
    TestClock clock;
    Scheduler s(clock);
    Counter c;
    c.clock   = &clock;
    c.cost_us = 200;
    s.add_task("measured", &Counter::run, &c, 1);

    s.run_tick();
    s.run_tick();
    CHECK_EQ(s.tasks()[0].runs, 2u);
    CHECK_EQ(s.tasks()[0].last_us, 200u);
    CHECK_EQ(s.tasks()[0].max_us, 200u);
    CHECK_EQ(s.tasks()[0].total_us, static_cast<uint64_t>(400));
}

TEST(scheduler, a_late_tick_does_not_trigger_catch_up_ticks) {
    // The important non-behaviour: after falling behind, the scheduler
    // re-anchors instead of running a burst of back-to-back ticks, which is
    // what turns one slow tick into a cascading collapse.
    TestClock clock;
    Scheduler s(clock);

    Counter greedy;
    greedy.clock   = &clock;
    greedy.cost_us = Scheduler::tick_period().to_micros() * 5;   // massively over
    s.add_task("greedy", &Counter::run, &greedy, 1);

    s.start();
    const uint32_t before_sleeps = clock.sleeps();
    s.run_tick_realtime();

    CHECK_EQ(greedy.runs, 1u);           // exactly one execution, not five
    CHECK_EQ(clock.sleeps(), before_sleeps);   // and it did not sleep
}

TEST(scheduler, uptime_is_derived_from_the_tick_count) {
    TestClock clock;
    Scheduler s(clock);
    for (uint32_t i = 0; i < Scheduler::kBaseRateHz * 3; ++i) { s.run_tick(); }
    CHECK_EQ(s.uptime_s(), 3u);
}
