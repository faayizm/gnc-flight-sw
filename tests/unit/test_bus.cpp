// ============================================================================
//  Tests for the software bus.
// ============================================================================
#include "core/bus.hpp"
#include "framework.hpp"

using fsw::core::Bus;
using fsw::core::Status;
using fsw::core::Topic;

namespace {

struct Receiver {
    uint32_t calls = 0;
    uint32_t last_value = 0;
    size_t   last_length = 0;

    static void handle(void* ctx, Topic, const uint8_t* data, size_t length) {
        auto* self = static_cast<Receiver*>(ctx);
        ++self->calls;
        self->last_length = length;
        if (length >= sizeof(uint32_t)) {
            self->last_value = *reinterpret_cast<const uint32_t*>(data);
        }
    }
};

}  // namespace

TEST(bus, a_subscriber_receives_what_is_published_to_its_topic) {
    Bus bus;
    Receiver r;
    CHECK(fsw::core::is_ok(bus.subscribe(Topic::AdcsHk, &Receiver::handle, &r)));

    const uint32_t value = 0xABCD1234;
    bus.publish(Topic::AdcsHk, reinterpret_cast<const uint8_t*>(&value), sizeof value);

    CHECK_EQ(r.calls, 1u);
    CHECK_EQ(r.last_value, 0xABCD1234u);
    CHECK_EQ(r.last_length, sizeof(uint32_t));
}

TEST(bus, a_subscriber_hears_nothing_from_other_topics) {
    Bus bus;
    Receiver r;
    bus.subscribe(Topic::AdcsHk, &Receiver::handle, &r);

    const uint32_t value = 1;
    bus.publish(Topic::EpsHk, reinterpret_cast<const uint8_t*>(&value), sizeof value);
    CHECK_EQ(r.calls, 0u);
}

TEST(bus, every_subscriber_to_a_topic_is_delivered_to) {
    Bus bus;
    Receiver a, b, c;
    bus.subscribe(Topic::ModeChanged, &Receiver::handle, &a);
    bus.subscribe(Topic::ModeChanged, &Receiver::handle, &b);
    bus.subscribe(Topic::SysHk,       &Receiver::handle, &c);

    const uint32_t value = 7;
    bus.publish(Topic::ModeChanged, reinterpret_cast<const uint8_t*>(&value), sizeof value);

    CHECK_EQ(a.calls, 1u);
    CHECK_EQ(b.calls, 1u);
    CHECK_EQ(c.calls, 0u);
    CHECK_EQ(bus.delivered_count(), 2u);
}

TEST(bus, publishing_to_a_topic_nobody_listens_to_is_not_an_error) {
    // Telemetry is published whether or not the downlink is up. Treating an
    // unheard message as a failure would make every caller handle a condition
    // that is entirely normal.
    Bus bus;
    CHECK(fsw::core::is_ok(bus.publish(Topic::SensorData, nullptr, 0)));
    CHECK_EQ(bus.published_count(), 1u);
    CHECK_EQ(bus.delivered_count(), 0u);
}

TEST(bus, delivery_is_synchronous) {
    // The subscriber has already run by the time publish() returns. Everything
    // in the dispatch design depends on this.
    Bus bus;
    Receiver r;
    bus.subscribe(Topic::SysHk, &Receiver::handle, &r);

    const uint32_t value = 42;
    bus.publish(Topic::SysHk, reinterpret_cast<const uint8_t*>(&value), sizeof value);
    CHECK_EQ(r.calls, 1u);   // not "eventually" -- now
}

TEST(bus, invalid_subscriptions_are_refused) {
    Bus bus;
    Receiver r;
    CHECK(bus.subscribe(Topic::SysHk, nullptr, &r) == Status::Invalid);
    CHECK(bus.subscribe(Topic::kTopicCount, &Receiver::handle, &r) == Status::Invalid);
}

TEST(bus, the_subscription_table_is_bounded) {
    Bus bus;
    Receiver r;
    for (size_t i = 0; i < Bus::kMaxSubscriptions; ++i) {
        CHECK(fsw::core::is_ok(bus.subscribe(Topic::SysHk, &Receiver::handle, &r)));
    }
    CHECK(bus.subscribe(Topic::SysHk, &Receiver::handle, &r) == Status::NoSpace);
}
