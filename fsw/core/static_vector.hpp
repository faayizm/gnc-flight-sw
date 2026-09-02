// ============================================================================
//  fsw/core/static_vector.hpp -- a vector whose capacity is a compile-time
//  constant and whose storage lives inside the object.
//
//  Flight software does not call malloc after initialisation. Heap use in a
//  long-running system invites fragmentation with no way to recover in orbit,
//  and it makes worst-case memory consumption impossible to prove on the
//  ground. So every collection here is bounded at compile time, its worst case
//  is visible in the type, and running out of room is an ordinary error return
//  rather than an allocation failure at the worst possible moment.
//
//  This is a deliberately small subset of std::vector: no reallocation, no
//  insertion in the middle, no exceptions.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

namespace fsw::core {

template <typename T, size_t Capacity>
class StaticVector {
 public:
    static constexpr size_t kCapacity = Capacity;

    constexpr StaticVector() = default;

    bool push_back(const T& value) {
        if (size_ >= Capacity) { return false; }
        items_[size_++] = value;
        return true;
    }

    // Remove by index, preserving order. Returns false if out of bounds.
    bool erase(size_t index) {
        if (index >= size_) { return false; }
        for (size_t i = index; i + 1 < size_; ++i) {
            items_[i] = items_[i + 1];
        }
        --size_;
        return true;
    }

    void clear() { size_ = 0; }

    T&       operator[](size_t i)       { return items_[i]; }
    const T& operator[](size_t i) const { return items_[i]; }

    T*       begin()       { return items_; }
    T*       end()         { return items_ + size_; }
    const T* begin() const { return items_; }
    const T* end()   const { return items_ + size_; }

    size_t size()     const { return size_; }
    bool   empty()    const { return size_ == 0; }
    bool   full()     const { return size_ == Capacity; }
    size_t capacity() const { return Capacity; }

 private:
    T      items_[Capacity]{};
    size_t size_ = 0;
};

}  // namespace fsw::core
