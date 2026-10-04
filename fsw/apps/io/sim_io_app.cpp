// ============================================================================
//  fsw/apps/io/sim_io_app.cpp
// ============================================================================
#include "apps/io/sim_io_app.hpp"

#include <cstring>

namespace fsw::io {

core::Status SimIoApp::init() {
    core::Status s = bus_.subscribe(core::Topic::ActuatorCommand, &SimIoApp::on_actuators, this);
    if (!core::is_ok(s)) { return s; }
    return bus_.subscribe(core::Topic::PowerStatus, &SimIoApp::on_power, this);
}

void SimIoApp::on_actuators(void* ctx, core::Topic, const uint8_t* data, size_t length) {
    if (length == sizeof(msg::ActuatorCommand)) {
        std::memcpy(&static_cast<SimIoApp*>(ctx)->cmd_, data, length);
    }
}

void SimIoApp::on_power(void* ctx, core::Topic, const uint8_t* data, size_t length) {
    if (length == sizeof(msg::PowerStatus)) {
        std::memcpy(&static_cast<SimIoApp*>(ctx)->power_, data, length);
    }
}

void SimIoApp::task_run(void* context) { static_cast<SimIoApp*>(context)->run(); }

void SimIoApp::run() {
    msg::SensorFrame s;
    if (bridge_.poll(s)) {
        last_rx_s_ = clock_.mission_time_s();
        ever_had_data_ = true;
        if (!sensors_ok_) {
            sensors_ok_ = true;
            if (samples_ > 0) { events_.raise(dict::EventId::SENSOR_RESTORED); }
        }
        ++samples_;
        last_ = s;

        // Anything that does not answer this sample commands nothing.
        cmd_ = msg::ActuatorCommand{};
        bus_.publish_object(core::Topic::SensorData, s);

        ActuatorFrame reply;
        reply.seq              = s.seq;
        reply.dipole_a_m2      = cmd_.dipole_a_m2;
        reply.wheel_torque_nm  = cmd_.wheel_torque_nm;
        reply.commanded        = cmd_.mtq_commanded;
        reply.wheels_commanded = cmd_.wheels_commanded;
        reply.rails            = power_.rails;
        reply.rails_commanded  = power_.valid;
        bridge_.send(reply);
        return;
    }

    if (ever_had_data_ && sensors_ok_ && clock_.mission_time_s() - last_rx_s_ > kSensorTimeoutS) {
        sensors_ok_ = false;
        events_.raise(dict::EventId::SENSOR_TIMEOUT);
        // Same time as the last real sample, every flag clear: consumers
        // discard their history rather than differentiate across the gap.
        msg::SensorFrame dead;
        dead.seq = last_.seq;
        dead.time_s = last_.time_s;
        bus_.publish_object(core::Topic::SensorData, dead);
    }
}

}  // namespace fsw::io
