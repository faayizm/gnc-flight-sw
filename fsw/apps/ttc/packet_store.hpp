// ============================================================================
//  fsw/apps/ttc/packet_store.hpp -- on-board storage of telemetry (PUS ST[15]).
//
//  A low Earth orbiter sees its ground station for perhaps ten minutes in a
//  ninety-minute orbit. Everything the spacecraft says in the other eighty is
//  lost unless it is written down. This is where it is written down: every
//  telemetry packet the spacecraft generates goes into a circular store, and
//  the ground asks for a time range of it during the next pass.
//
//  CIRCULAR. When full, the oldest packets are overwritten -- the ground cares
//  more about what happened recently than about what it failed to collect a
//  day ago. The first overwrite raises STORE_WRAPPED, because data is now
//  being lost and an operator should know.
//
//  LAYOUT. One byte ring holding records back to back: a 2-byte length, then
//  the packet. The packet's own PUS timestamp is what retrieval selects on, so
//  no separate index is kept; a retrieval walks the ring once, which at
//  4 MiB is a few milliseconds spread over many ticks.
//
//  Fixed size, allocated with the object. `Capacity` is a template argument so
//  tests can exercise wrapping with a store of a few hundred bytes.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

namespace fsw::ttc {

// Time in a stored PUS TM packet: CUC coarse seconds at byte 13 (6-byte primary
// header + 1 version/status + 1 service + 1 subtype + 2 count + 2 destination).
inline uint32_t stored_packet_time(const uint8_t* p, size_t n) {
    if (n < 19) { return 0; }
    return (static_cast<uint32_t>(p[13]) << 24) | (static_cast<uint32_t>(p[14]) << 16) |
           (static_cast<uint32_t>(p[15]) << 8) | p[16];
}

template <size_t Capacity>
class PacketStore {
 public:
    // Store one packet. Overwrites the oldest records if needed. Returns true
    // if this call caused the first ever overwrite.
    bool record(const uint8_t* packet, size_t n) {
        if (!enabled_ || n == 0 || n + 2 > Capacity / 2) { return false; }
        bool wrapped_now = false;
        while (used_ + n + 2 > Capacity) {
            drop_oldest();
            if (!wrapped_) { wrapped_ = true; wrapped_now = true; }
        }
        put(tail(), static_cast<uint8_t>(n >> 8));
        put(tail() + 1, static_cast<uint8_t>(n & 0xFF));
        for (size_t i = 0; i < n; ++i) { put(tail() + 2 + i, packet[i]); }
        used_ += n + 2;
        ++count_;
        newest_ = stored_packet_time(packet, n);   // packets arrive in time order
        return wrapped_now;
    }

    // Begin a retrieval of everything with time in [from, to]. Returns how many
    // packets that will be.
    uint32_t start_retrieval(uint32_t from, uint32_t to) {
        from_ = from;
        to_ = to;
        cursor_ = head_;
        remaining_bytes_ = used_;
        retrieving_ = true;
        uint32_t n = 0;
        size_t c = head_, left = used_;
        while (left > 0) {
            const size_t len = rec_len(c);
            const uint32_t t = rec_time(c, len);
            if (t >= from && t <= to) { ++n; }
            c = (c + len + 2) % Capacity;
            left -= len + 2;
        }
        return n;
    }

    // Copy the next matching packet into `out`. Returns its length, or 0 when
    // the retrieval is complete.
    size_t next(uint8_t* out, size_t cap) {
        while (retrieving_ && remaining_bytes_ > 0) {
            const size_t len = rec_len(cursor_);
            const uint32_t t = rec_time(cursor_, len);
            const size_t at = cursor_;
            cursor_ = (cursor_ + len + 2) % Capacity;
            remaining_bytes_ -= len + 2;
            if (t >= from_ && t <= to_ && len <= cap) {
                for (size_t i = 0; i < len; ++i) { out[i] = buf_[(at + 2 + i) % Capacity]; }
                return len;
            }
        }
        retrieving_ = false;
        return 0;
    }

    // Delete every packet older than `to`.
    void delete_up_to(uint32_t to) {
        while (used_ > 0 && rec_time(head_, rec_len(head_)) < to) { drop_oldest(); }
    }

    void set_enabled(bool e) { enabled_ = e; }
    bool enabled()     const { return enabled_; }
    bool retrieving()  const { return retrieving_; }
    uint32_t count()   const { return count_; }
    size_t   used()    const { return used_; }
    uint8_t  used_pct() const { return static_cast<uint8_t>((used_ * 100) / Capacity); }
    uint32_t oldest()  const { return used_ ? rec_time(head_, rec_len(head_)) : 0; }
    uint32_t newest()  const { return used_ ? newest_ : 0; }

 private:
    size_t tail() const { return (head_ + used_) % Capacity; }
    void   put(size_t at, uint8_t v) { buf_[at % Capacity] = v; }

    size_t rec_len(size_t at) const {
        return (static_cast<size_t>(buf_[at % Capacity]) << 8) | buf_[(at + 1) % Capacity];
    }
    uint32_t rec_time(size_t at, size_t len) const {
        if (len < 19) { return 0; }
        uint32_t t = 0;
        for (size_t i = 0; i < 4; ++i) { t = (t << 8) | buf_[(at + 2 + 13 + i) % Capacity]; }
        return t;
    }
    void drop_oldest() {
        const size_t len = rec_len(head_);
        // A retrieval in progress must not read a record being overwritten.
        if (retrieving_ && cursor_ == head_) {
            cursor_ = (cursor_ + len + 2) % Capacity;
            remaining_bytes_ = remaining_bytes_ >= len + 2 ? remaining_bytes_ - len - 2 : 0;
        }
        head_ = (head_ + len + 2) % Capacity;
        used_ -= len + 2;
        --count_;
    }

    uint8_t  buf_[Capacity]{};
    size_t   head_ = 0;
    size_t   used_ = 0;
    uint32_t count_ = 0;
    uint32_t newest_ = 0;
    bool     enabled_ = true;
    bool     wrapped_ = false;

    bool     retrieving_ = false;
    size_t   cursor_ = 0;
    size_t   remaining_bytes_ = 0;
    uint32_t from_ = 0, to_ = 0;
};

}  // namespace fsw::ttc
