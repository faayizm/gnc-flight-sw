// ============================================================================
//  fsw/apps/ttc/schedule.hpp -- time-tagged telecommands (PUS ST[11]).
//
//  Commands loaded during one pass, to run when the ground cannot see the
//  spacecraft: switch an instrument on over a target, change a telemetry rate
//  before entering eclipse, anything that must happen at a time rather than
//  at the moment of uplink.
//
//  Each activity is a complete telecommand packet -- CRC and all -- with a
//  release time. At release it goes through exactly the path an uplinked
//  command does: acceptance, execution, verification reports. Nothing about a
//  scheduled command is special once it runs, which is what makes it safe to
//  reason about.
//
//  Fixed capacity. Insertion is all-or-nothing: a request carrying one bad
//  activity changes nothing, so the ground never has to work out which half
//  of a schedule made it on board.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "apps/ttc/space_packet.hpp"

namespace fsw::ttc {

class TimeSchedule {
 public:
    static constexpr size_t kSlots       = 32;
    static constexpr size_t kMaxTcBytes  = 256;

    struct Activity {
        bool     used = false;
        double   release_s = 0.0;
        uint16_t length = 0;
        uint8_t  packet[kMaxTcBytes]{};
    };

    size_t free_slots() const {
        size_t n = 0;
        for (const Activity& a : slots_) { n += a.used ? 0 : 1; }
        return n;
    }
    size_t pending() const { return kSlots - free_slots(); }

    bool insert(double release_s, const uint8_t* packet, size_t length) {
        if (length == 0 || length > kMaxTcBytes) { return false; }
        for (Activity& a : slots_) {
            if (!a.used) {
                a.used = true;
                a.release_s = release_s;
                a.length = static_cast<uint16_t>(length);
                std::memcpy(a.packet, packet, length);
                return true;
            }
        }
        return false;
    }

    // The earliest activity due at or before `now`, or nullptr. The caller
    // executes it and then calls release() on it.
    Activity* due(double now) {
        Activity* best = nullptr;
        for (Activity& a : slots_) {
            if (a.used && a.release_s <= now && (best == nullptr || a.release_s < best->release_s)) {
                best = &a;
            }
        }
        return best;
    }

    void release(Activity* a) { if (a != nullptr) { a->used = false; } }
    void reset() { for (Activity& a : slots_) { a.used = false; } }

    bool enabled() const { return enabled_; }
    void set_enabled(bool e) { enabled_ = e; }

 private:
    Activity slots_[kSlots];
    bool     enabled_ = true;
};

}  // namespace fsw::ttc
