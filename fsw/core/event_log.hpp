// ============================================================================
//  fsw/core/event_log.hpp -- on-board event reporting.
//
//  An event is a discrete, noteworthy thing that happened: a mode change, a
//  rejected telecommand, a deadline miss. Events are how a spacecraft explains
//  itself after the fact, and they are usually the only evidence available
//  when something goes wrong outside a ground contact.
//
//  Two destinations, and both matter:
//
//    1. A bounded in-memory ring, so the last N events survive a loss of
//       signal and can be dumped when contact resumes. It overwrites the
//       oldest when full and counts what it discarded, so the ground can tell
//       the difference between "nothing happened" and "we lost the record".
//
//    2. An immediate PUS ST[05] report, when the downlink is up.
//
//  The sink is a callback rather than a direct call into the TT&C application,
//  so that core code has no dependency on the telemetry stack -- the event log
//  works identically in a unit test with no packets anywhere.
//
//  DISCIPLINE. Events are for state changes, not for tracing. An event raised
//  every control cycle is a bug: it will swamp the downlink budget, evict the
//  history that mattered, and hide the one event somebody needed to see.
// ============================================================================
#pragma once

#include <cstdint>

#include "core/ring_buffer.hpp"
#include "core/time.hpp"
#include "generated/dictionary.hpp"

namespace fsw::core {

struct EventRecord {
    dict::EventId  id       = static_cast<dict::EventId>(0);
    dict::Severity severity = dict::Severity::INFO;
    uint32_t       aux      = 0;   // event-specific detail, see docs/ICD.md
    CucTime        time{};         // mission time at which it was raised
};

class EventLog {
 public:
    static constexpr size_t kHistoryDepth = 64;

    // Called for every event, immediately, on the raising task's stack.
    using SinkFn = void (*)(void* context, const EventRecord& record);

    // Returns current mission time in seconds. Injected as a plain callback
    // rather than an IClock reference, so that core/ carries no dependency on
    // hal/ and the event log can be unit tested with nothing else present.
    using TimeFn = double (*)(void* context);

    void set_sink(SinkFn fn, void* context) { sink_ = fn; sink_ctx_ = context; }
    void set_time_source(TimeFn fn, void* context) { time_fn_ = fn; time_ctx_ = context; }

    void raise(dict::EventId id, uint32_t aux = 0) {
        const dict::EventInfo* info = dict::find_event(id);

        EventRecord rec;
        rec.id       = id;
        rec.severity = (info != nullptr) ? info->severity : dict::Severity::LOW;
        rec.aux      = aux;
        if (time_fn_ != nullptr) {
            rec.time = CucTime::from_seconds(time_fn_(time_ctx_));
        }

        history_.push(rec);
        ++raised_;
        last_id_ = static_cast<uint16_t>(id);

        if (sink_ != nullptr) { sink_(sink_ctx_, rec); }
    }

    const RingBuffer<EventRecord, kHistoryDepth>& history() const { return history_; }

    uint32_t raised_count()  const { return raised_; }
    uint32_t dropped_count() const { return history_.dropped_count(); }
    uint16_t last_id()       const { return last_id_; }

 private:
    RingBuffer<EventRecord, kHistoryDepth> history_;
    SinkFn       sink_     = nullptr;
    void*        sink_ctx_ = nullptr;
    TimeFn       time_fn_  = nullptr;
    void*        time_ctx_ = nullptr;
    uint32_t     raised_   = 0;
    uint16_t     last_id_  = 0;
};

}  // namespace fsw::core
