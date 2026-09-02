// ============================================================================
//  fsw/core/ring_buffer.hpp -- a fixed-capacity FIFO with a defined
//  overflow policy.
//
//  Used for the event log and for telemetry awaiting downlink. The policy
//  question matters more than the data structure: when a bounded queue fills
//  up, something must be dropped, and pretending otherwise is how flight
//  software ends up blocking a control loop on a full downlink buffer.
//
//  Policy here: OVERWRITE THE OLDEST, and count the drops. For telemetry that
//  is the right trade -- fresh housekeeping is worth more than stale, and the
//  drop counter tells the ground it happened. A queue where the oldest entry
//  is the important one (a command queue, say) must not use this class.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

namespace fsw::core {

template <typename T, size_t Capacity>
class RingBuffer {
 public:
    static_assert(Capacity > 0, "a ring buffer needs room for at least one item");

    // Returns false when an older item had to be discarded to make room.
    bool push(const T& value) {
        bool dropped = false;
        if (size_ == Capacity) {
            head_ = (head_ + 1) % Capacity;
            --size_;
            ++dropped_count_;
            dropped = true;
        }
        items_[(head_ + size_) % Capacity] = value;
        ++size_;
        return !dropped;
    }

    bool pop(T& out) {
        if (size_ == 0) { return false; }
        out = items_[head_];
        head_ = (head_ + 1) % Capacity;
        --size_;
        return true;
    }

    bool peek(T& out) const {
        if (size_ == 0) { return false; }
        out = items_[head_];
        return true;
    }

    // Index 0 is the oldest retained item. Used to walk the event log for a
    // ground dump without consuming it.
    const T* at(size_t index) const {
        if (index >= size_) { return nullptr; }
        return &items_[(head_ + index) % Capacity];
    }

    void clear() { head_ = 0; size_ = 0; }

    size_t   size()          const { return size_; }
    bool     empty()         const { return size_ == 0; }
    bool     full()          const { return size_ == Capacity; }
    size_t   capacity()      const { return Capacity; }
    uint32_t dropped_count() const { return dropped_count_; }

 private:
    T        items_[Capacity]{};
    size_t   head_          = 0;
    size_t   size_          = 0;
    uint32_t dropped_count_ = 0;
};

}  // namespace fsw::core
