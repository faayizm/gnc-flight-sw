// ============================================================================
//  Tests for the fixed-capacity containers.
//
//  The overflow behaviour is the interesting part: these exist precisely so
//  that running out of room is an ordinary, testable return value rather than
//  an allocation failure in orbit.
// ============================================================================
#include "core/ring_buffer.hpp"
#include "core/static_vector.hpp"
#include "framework.hpp"

using fsw::core::RingBuffer;
using fsw::core::StaticVector;

TEST(static_vector, fills_up_and_then_refuses) {
    StaticVector<int, 4> v;
    CHECK(v.empty());
    for (int i = 0; i < 4; ++i) { CHECK(v.push_back(i)); }
    CHECK(v.full());
    CHECK_EQ(v.size(), static_cast<size_t>(4));

    CHECK(!v.push_back(99));            // refused, not resized
    CHECK_EQ(v.size(), static_cast<size_t>(4));
    CHECK_EQ(v[3], 3);
}

TEST(static_vector, erase_preserves_order) {
    StaticVector<int, 8> v;
    for (int i = 0; i < 5; ++i) { v.push_back(i); }
    CHECK(v.erase(1));
    CHECK_EQ(v.size(), static_cast<size_t>(4));
    CHECK_EQ(v[0], 0);
    CHECK_EQ(v[1], 2);
    CHECK_EQ(v[3], 4);
    CHECK(!v.erase(99));
}

TEST(static_vector, iterates_over_exactly_the_live_elements) {
    StaticVector<int, 8> v;
    for (int i = 1; i <= 3; ++i) { v.push_back(i); }
    int sum = 0;
    for (int x : v) { sum += x; }
    CHECK_EQ(sum, 6);
}

TEST(ring_buffer, first_in_first_out) {
    RingBuffer<int, 4> r;
    r.push(1); r.push(2); r.push(3);
    int out = 0;
    CHECK(r.pop(out)); CHECK_EQ(out, 1);
    CHECK(r.pop(out)); CHECK_EQ(out, 2);
    CHECK(r.pop(out)); CHECK_EQ(out, 3);
    CHECK(!r.pop(out));
}

TEST(ring_buffer, overflow_discards_the_oldest_and_counts_it) {
    RingBuffer<int, 3> r;
    CHECK(r.push(1));
    CHECK(r.push(2));
    CHECK(r.push(3));
    CHECK(!r.push(4));   // returns false to say something was dropped
    CHECK_EQ(r.dropped_count(), 1u);

    int out = 0;
    r.pop(out);
    CHECK_EQ(out, 2);    // the 1 is gone, the newest survived
}

TEST(ring_buffer, wraps_around_correctly_under_sustained_use) {
    RingBuffer<int, 4> r;
    for (int i = 0; i < 100; ++i) {
        r.push(i);
        int out = 0;
        CHECK(r.pop(out));
        CHECK_EQ(out, i);
    }
    CHECK(r.empty());
    CHECK_EQ(r.dropped_count(), 0u);
}

TEST(ring_buffer, at_indexes_from_the_oldest_without_consuming) {
    RingBuffer<int, 4> r;
    r.push(10); r.push(20); r.push(30);
    CHECK_EQ(*r.at(0), 10);
    CHECK_EQ(*r.at(2), 30);
    CHECK(r.at(3) == nullptr);
    CHECK_EQ(r.size(), static_cast<size_t>(3));   // still all there
}
