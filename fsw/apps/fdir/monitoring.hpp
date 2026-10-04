// ============================================================================
//  fsw/apps/fdir/monitoring.hpp -- PUS ST[12] on-board parameter monitoring.
//
//  The ground watches telemetry for values out of limits. But the ground sees
//  the spacecraft for perhaps forty minutes a day, and most faults choose
//  their own moment. On-board monitoring is the same watching, done by the
//  spacecraft itself, all the time.
//
//  WHAT IS WATCHED is declared in the dictionary, not in this code: each
//  monitor names a housekeeping field, a low and a high limit, how many
//  samples in a row must break a limit, and which event to raise. Adding a
//  monitor is a dictionary change; nothing here changes.
//
//  REPETITIONS are the filter between noise and a fault. A battery voltage
//  that dips for one sample while a wheel spins up is not an alarm; one that
//  stays low for five seconds is. The same counter applies on the way back,
//  so a value hovering at its limit does not flap between alarm and clear.
//
//  WHAT HAPPENS NEXT is not decided here either. A monitor raises an event.
//  Whether anything acts on that event is the event-action table's business
//  (PUS ST[19], see ttc_app.cpp): detection and response are kept apart, so
//  each can be changed -- or disabled from the ground -- without the other.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

#include "generated/telemetry.hpp"

namespace fsw::fdir {

struct MonitorTransition {
    uint8_t            id = 0;
    dict::MonitorStatus from = dict::MonitorStatus::UNCHECKED;
    dict::MonitorStatus to   = dict::MonitorStatus::UNCHECKED;
    double             value = 0.0;
    double             limit = 0.0;
};

class Monitoring {
 public:
    static constexpr size_t kCount = tlm::kMonitorCount;

    Monitoring() {
        for (size_t i = 0; i < kCount; ++i) { state_[i].enabled = true; }
    }

    // Check every enabled monitor that watches this housekeeping structure.
    // Transitions are written to `out` (room for kCount); returns how many.
    size_t check(dict::HkSid sid, const void* hk, MonitorTransition* out) {
        size_t n = 0;
        for (size_t i = 0; i < kCount; ++i) {
            const tlm::MonitorDef& def = tlm::kMonitors[i];
            State& s = state_[i];
            if (def.sid != sid || !s.enabled) { continue; }

            const double v = def.read(hk);
            dict::MonitorStatus seen = dict::MonitorStatus::WITHIN;
            if (v < def.low || !(v == v)) { seen = dict::MonitorStatus::BELOW; }
            else if (v > def.high)         { seen = dict::MonitorStatus::ABOVE; }

            // Count how long the CURRENT reading has persisted. The status
            // moves only once it has persisted long enough -- in either
            // direction. A first evaluation reports WITHIN at once: there is
            // nothing to filter yet, and the ground needs a starting point.
            if (seen == s.candidate) {
                if (s.count < 0xFFFF) { ++s.count; }
            } else {
                s.candidate = seen;
                s.count = 1;
            }
            const bool first = s.status == dict::MonitorStatus::UNCHECKED &&
                               seen == dict::MonitorStatus::WITHIN;
            if (seen != s.status && (first || s.count >= def.repetitions)) {
                MonitorTransition t;
                t.id = def.id;
                t.from = s.status;
                t.to = seen;
                t.value = v;
                t.limit = seen == dict::MonitorStatus::BELOW ? def.low : def.high;
                s.status = seen;
                out[n++] = t;
            }
        }
        return n;
    }

    // ST[12,1] / ST[12,2]. Returns false for an unknown monitor.
    bool set_enabled(uint8_t id, bool on) {
        for (size_t i = 0; i < kCount; ++i) {
            if (tlm::kMonitors[i].id != id) { continue; }
            State& s = state_[i];
            s.enabled = on;
            s.status = dict::MonitorStatus::UNCHECKED;     // checking starts afresh
            s.candidate = dict::MonitorStatus::UNCHECKED;
            s.count = 0;
            return true;
        }
        return false;
    }

    dict::MonitorStatus status(size_t index) const { return state_[index].status; }
    bool enabled(size_t index) const { return state_[index].enabled; }

    uint8_t enabled_count() const {
        uint8_t n = 0;
        for (const State& s : state_) { n = static_cast<uint8_t>(n + (s.enabled ? 1 : 0)); }
        return n;
    }
    uint8_t alarm_count() const {
        uint8_t n = 0;
        for (const State& s : state_) {
            n = static_cast<uint8_t>(n + ((s.status == dict::MonitorStatus::BELOW ||
                                          s.status == dict::MonitorStatus::ABOVE) ? 1 : 0));
        }
        return n;
    }

    static const tlm::MonitorDef* find(uint8_t id) {
        for (const tlm::MonitorDef& d : tlm::kMonitors) {
            if (d.id == id) { return &d; }
        }
        return nullptr;
    }

 private:
    struct State {
        bool                enabled   = false;
        dict::MonitorStatus status    = dict::MonitorStatus::UNCHECKED;
        dict::MonitorStatus candidate = dict::MonitorStatus::UNCHECKED;
        uint16_t            count     = 0;
    };
    State state_[kCount];
};

}  // namespace fsw::fdir
