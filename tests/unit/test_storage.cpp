// ============================================================================
//  Tests for the packet store (ST[15]) and the time-tagged schedule (ST[11]).
// ============================================================================
#include <cstring>

#include "apps/ttc/packet_store.hpp"
#include "apps/ttc/schedule.hpp"
#include "framework.hpp"

using namespace fsw::ttc;

namespace {
// A minimal PUS TM packet of `n` bytes stamped with time `t`.
void make_packet(uint8_t* p, size_t n, uint32_t t, uint8_t marker) {
    std::memset(p, marker, n);
    p[4] = 0; p[5] = static_cast<uint8_t>(n - 7);
    p[13] = static_cast<uint8_t>(t >> 24); p[14] = static_cast<uint8_t>(t >> 16);
    p[15] = static_cast<uint8_t>(t >> 8);  p[16] = static_cast<uint8_t>(t);
}
}  // namespace

TEST(packet_store, retrieval_returns_only_the_requested_time_range_in_order) {
    PacketStore<4096> s;
    uint8_t p[40];
    for (uint32_t t = 100; t < 110; ++t) { make_packet(p, sizeof p, t, static_cast<uint8_t>(t)); s.record(p, sizeof p); }
    CHECK_EQ(s.count(), 10u);
    CHECK_EQ(s.start_retrieval(103, 105), 3u);
    uint8_t out[64];
    for (uint32_t t = 103; t <= 105; ++t) {
        CHECK_EQ(s.next(out, sizeof out), sizeof p);
        CHECK_EQ(stored_packet_time(out, sizeof p), t);
    }
    CHECK_EQ(s.next(out, sizeof out), 0u);
    CHECK(!s.retrieving());
}

TEST(packet_store, a_full_store_overwrites_the_oldest_and_says_so_once) {
    PacketStore<512> s;
    uint8_t p[50];
    bool first_wrap = false;
    int wraps = 0;
    for (uint32_t t = 0; t < 40; ++t) {
        make_packet(p, sizeof p, t, 0);
        if (s.record(p, sizeof p)) { first_wrap = true; ++wraps; }
    }
    CHECK(first_wrap);
    CHECK_EQ(wraps, 1);                 // reported once, not on every overwrite
    CHECK(s.used() <= 512u);
    CHECK_EQ(s.newest(), 39u);
    CHECK(s.oldest() > 0u);             // the earliest are gone
    CHECK_EQ(s.count(), 512u / 52u);    // as many as fit
}

TEST(packet_store, delete_up_to_removes_only_older_packets) {
    PacketStore<4096> s;
    uint8_t p[30];
    for (uint32_t t = 0; t < 10; ++t) { make_packet(p, sizeof p, t, 0); s.record(p, sizeof p); }
    s.delete_up_to(6);
    CHECK_EQ(s.count(), 4u);
    CHECK_EQ(s.oldest(), 6u);
}

TEST(packet_store, a_disabled_store_records_nothing) {
    PacketStore<1024> s;
    s.set_enabled(false);
    uint8_t p[30];
    make_packet(p, sizeof p, 1, 0);
    s.record(p, sizeof p);
    CHECK_EQ(s.count(), 0u);
}

TEST(schedule, releases_the_earliest_due_activity_first) {
    TimeSchedule s;
    const uint8_t a[8] = {1}, b[8] = {2}, c[8] = {3};
    CHECK(s.insert(30.0, c, 8));
    CHECK(s.insert(10.0, a, 8));
    CHECK(s.insert(20.0, b, 8));
    CHECK(s.due(5.0) == nullptr);
    TimeSchedule::Activity* x = s.due(25.0);
    CHECK(x != nullptr && x->packet[0] == 1);
    s.release(x);
    x = s.due(25.0);
    CHECK(x != nullptr && x->packet[0] == 2);
    s.release(x);
    CHECK(s.due(25.0) == nullptr);      // the 30 s one is not yet due
    CHECK_EQ(s.pending(), 1u);
}

TEST(schedule, refuses_when_full) {
    TimeSchedule s;
    const uint8_t a[8] = {};
    for (size_t i = 0; i < TimeSchedule::kSlots; ++i) { CHECK(s.insert(1.0, a, 8)); }
    CHECK(!s.insert(1.0, a, 8));
}
