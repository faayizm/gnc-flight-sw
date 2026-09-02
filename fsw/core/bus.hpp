// ============================================================================
//  fsw/core/bus.hpp -- the software bus.
//
//  Applications must not call each other directly. ADCS should not know that
//  TT&C exists; the mode manager should not hold a pointer to EPS. They publish
//  to named topics and subscribe to the topics they care about. That is what
//  makes an application removable, testable in isolation, and replaceable by a
//  stub in a scenario.
//
//  DISPATCH IS SYNCHRONOUS. publish() calls every subscriber before returning.
//  No queues, no worker threads, no deferred delivery.
//
//    + execution order is fully determined by registration order
//    + there is no queue to overflow and no message to silently drop
//    + a stack trace during debugging shows the whole causal chain
//    - a slow subscriber directly delays its publisher
//
//  The last point is the price, and it is acceptable only because everything
//  runs in one thread inside a rate group with a measured time budget. If a
//  subscriber ever needs to do something slow, it must record the request and
//  do the work in its own task, not block the publisher.
//
//  Payloads are copied into the subscriber's world by the subscriber; the
//  pointer handed to a handler is valid only for the duration of that call.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/static_vector.hpp"
#include "core/status.hpp"

namespace fsw::core {

// Every topic on the spacecraft. Adding one is a deliberate, reviewable act.
enum class Topic : uint16_t {
    SysHk = 1,        // core system housekeeping, published by the TT&C app
    AdcsHk,           // attitude housekeeping, published by ADCS
    EpsHk,            // power housekeeping, published by EPS
    ModeRequest,      // a request to change spacecraft mode, from ground or autonomy
    ModeChanged,      // announcement that a transition has actually happened
    SensorData,       // decoded sensor set from the simulator bridge
    ActuatorCommand,  // torque and dipole demands heading for the actuators
    kTopicCount
};

constexpr const char* to_string(Topic t) {
    switch (t) {
        case Topic::SysHk:           return "SYS_HK";
        case Topic::AdcsHk:          return "ADCS_HK";
        case Topic::EpsHk:           return "EPS_HK";
        case Topic::ModeRequest:     return "MODE_REQUEST";
        case Topic::ModeChanged:     return "MODE_CHANGED";
        case Topic::SensorData:      return "SENSOR_DATA";
        case Topic::ActuatorCommand: return "ACTUATOR_COMMAND";
        case Topic::kTopicCount:     return "INVALID";
    }
    return "UNKNOWN";
}

class Bus {
 public:
    using Handler = void (*)(void* context, Topic topic,
                             const uint8_t* data, size_t length);

    static constexpr size_t kMaxSubscriptions = 32;

    // All subscriptions are made during initialisation. Nothing subscribes or
    // unsubscribes in flight, so the delivery set for a topic is fixed and can
    // be reviewed by reading the initialisation code in one place.
    Status subscribe(Topic topic, Handler handler, void* context) {
        if (handler == nullptr) { return Status::Invalid; }
        if (topic >= Topic::kTopicCount) { return Status::Invalid; }
        if (subs_.full()) { return Status::NoSpace; }
        subs_.push_back(Subscription{topic, handler, context});
        return Status::Ok;
    }

    // Deliver to every subscriber, in registration order. Returns Ok even when
    // nobody is listening: publishing into the void is normal and not an error
    // (telemetry is published whether or not the downlink is up).
    Status publish(Topic topic, const uint8_t* data, size_t length) {
        if (topic >= Topic::kTopicCount) { return Status::Invalid; }
        ++published_;
        for (size_t i = 0; i < subs_.size(); ++i) {
            if (subs_[i].topic == topic) {
                subs_[i].handler(subs_[i].context, topic, data, length);
                ++delivered_;
            }
        }
        return Status::Ok;
    }

    // Convenience for the common case of publishing a whole POD structure.
    template <typename T>
    Status publish_object(Topic topic, const T& object) {
        return publish(topic, reinterpret_cast<const uint8_t*>(&object), sizeof(T));
    }

    uint32_t published_count() const { return published_; }
    uint32_t delivered_count() const { return delivered_; }
    size_t   subscription_count() const { return subs_.size(); }

 private:
    struct Subscription {
        Topic   topic;
        Handler handler;
        void*   context;
    };

    StaticVector<Subscription, kMaxSubscriptions> subs_;
    uint32_t published_ = 0;
    uint32_t delivered_ = 0;
};

}  // namespace fsw::core
