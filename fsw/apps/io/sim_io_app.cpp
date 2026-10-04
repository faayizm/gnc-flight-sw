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
        last_rx_ = clock_.host_now();
        ever_had_data_ = true;
        if (!sensors_ok_) {
            sensors_ok_ = true;
            if (samples_ > 0) { events_.raise(dict::EventId::SENSOR_RESTORED); }
        }
        // A hole in the sensors' own timestamps. The host-clock watchdog
        // below cannot see a short one when the simulation runs fast; the
        // timestamps always can, deterministically, once data resumes.
        if (samples_ > 0 && s.time_s - last_.time_s > kGapS) {
            const double gap_ms = (s.time_s - last_.time_s) * 1000.0;
            events_.raise(dict::EventId::SENSOR_GAP,
                          static_cast<uint32_t>(gap_ms < 4.0e9 ? gap_ms : 4.0e9));
        }
        ++samples_;

        // The particle arrives with the sample, before anyone reads memory.
        if (bridge_.seu().target != 0xFF && upset_fn_ != nullptr) {
            upset_fn_(upset_ctx_, bridge_.seu().target, bridge_.seu().bit);
        }

        // Distrust before anything else sees the data.
        ScreenChange changes[SensorScreen::kSensors];
        const int n = screen_.screen(s, changes);
        for (int i = 0; i < n; ++i) {
            const auto id = static_cast<uint32_t>(changes[i].sensor);
            if (changes[i].fault == dict::SensorFault::NONE) {
                events_.raise(dict::EventId::SENSOR_READMITTED, id);
            } else {
                events_.raise(dict::EventId::SENSOR_REJECTED,
                              (id << 8) | static_cast<uint32_t>(changes[i].fault));
            }
        }
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

    if (ever_had_data_ && sensors_ok_ && clock_.host_now() - last_rx_ > kSensorTimeout) {
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
