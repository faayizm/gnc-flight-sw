// ============================================================================
//  fsw/core/tmr.hpp -- triple modular redundancy for small, critical state.
//
//  EDAC (core/edac.hpp) protects bulk memory cheaply: one check byte per
//  eight data bytes. For a handful of values whose corruption would be a
//  disaster on its own -- the spacecraft's mode, which wheels are believed
//  to be dead -- there is a simpler and older trick: keep three copies, and
//  believe any two that agree.
//
//  A single upset can corrupt only one copy, so the vote always returns the
//  right value, and repairs the odd one out as it does. Two upsets in the
//  same value before a vote repairs the first would beat it -- which is why
//  the vote runs every time the value is read, many times a second.
//
//  Costs 3x the memory, which is why it is for a few bytes, not for tables.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace fsw::core {

template <typename T>
class Tmr {
    static_assert(std::is_trivially_copyable<T>::value, "TMR copies are compared bytewise");

 public:
    explicit Tmr(const T& v = T{}) { set(v); }

    void set(const T& v) { copy_[0] = copy_[1] = copy_[2] = v; }

    // Vote, repair, return. Not const: a vote that finds a disagreement
    // rewrites the losing copy.
    T get() {
        const bool ab = same(0, 1), ac = same(0, 2), bc = same(1, 2);
        if (ab && ac) { return copy_[0]; }        // the usual case
        ++repairs_;
        if (ab) { copy_[2] = copy_[0]; return copy_[0]; }
        if (ac) { copy_[1] = copy_[0]; return copy_[0]; }
        if (bc) { copy_[0] = copy_[1]; return copy_[1]; }
        // All three differ: more upsets than this can survive. Keep the first
        // copy and say so; the caller decides what a lost value means.
        ++unrecoverable_;
        copy_[1] = copy_[2] = copy_[0];
        return copy_[0];
    }

    // The vote without the repair, for read-only callers.
    T vote() const {
        if (same(0, 1) || same(0, 2)) { return copy_[0]; }
        return same(1, 2) ? copy_[1] : copy_[0];
    }

    // A particle's view of the three copies.
    static constexpr size_t kBits = 3 * sizeof(T) * 8;
    void flip(size_t b) {
        b %= kBits;
        auto* bytes = reinterpret_cast<uint8_t*>(&copy_[b / (sizeof(T) * 8)]);
        const size_t k = b % (sizeof(T) * 8);
        bytes[k / 8] = static_cast<uint8_t>(bytes[k / 8] ^ (1u << (k % 8)));
    }

    uint32_t repairs()       const { return repairs_; }
    uint32_t unrecoverable() const { return unrecoverable_; }

 private:
    bool same(int i, int j) const { return std::memcmp(&copy_[i], &copy_[j], sizeof(T)) == 0; }

    T        copy_[3];
    uint32_t repairs_       = 0;
    uint32_t unrecoverable_ = 0;
};

}  // namespace fsw::core
