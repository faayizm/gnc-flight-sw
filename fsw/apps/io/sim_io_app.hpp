// ============================================================================
//  fsw/apps/io/sim_io_app.hpp -- the hardware boundary, in one place.
//
//  On a real spacecraft this would be the device drivers: SPI to the gyro,
//  I2C to the EPS board, a UART to the star tracker. Here it is the simulator
//  bridge. Either way, it is the only application that talks to hardware:
//
//    sensor frame arrives  -->  publish Topic::SensorData
//                                 (ADCS and EPS run, synchronously, and
//                                  publish their commands back on the bus)
//                          -->  reply with one actuator frame: ADCS's
//                               dipole and wheel torque, EPS's rail switches
//
//  Because the bus is synchronous, every consumer of a sample has run and
//  answered by the time publish() returns -- which is what keeps the
//  simulator's lockstep, and the run's determinism, intact.
//
//  FAILURE. If the sensor stream stops, every actuator is commanded to zero
//  and an event is raised. A controller that keeps applying its last demand to
//  a satellite it can no longer observe is how a small fault becomes a large
//  one. Consumers are told too: they receive one frame with every validity
//  flag clear.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

#include "apps/io/sim_bridge.hpp"
#include "apps/messages.hpp"
#include "core/bus.hpp"
#include "core/event_log.hpp"
#include "hal/clock.hpp"

namespace fsw::io {

class SimIoApp {
 public:
    SimIoApp(hal::ILink& link, hal::IClock& clock, core::Bus& bus, core::EventLog& events)
        : bridge_(link), clock_(clock), bus_(bus), events_(events) {}

    core::Status init();

    // 50 Hz, so the reply to the simulator waits at most one base tick.
    static void task_run(void* context);

    bool     sensors_ok() const { return sensors_ok_; }
    uint32_t samples()    const { return samples_; }

 private:
    static void on_actuators(void* ctx, core::Topic, const uint8_t* data, size_t length);
    static void on_power(void* ctx, core::Topic, const uint8_t* data, size_t length);
    void run();

    static constexpr double kSensorTimeoutS = 2.0;

    SimBridge       bridge_;
    hal::IClock&    clock_;
    core::Bus&      bus_;
    core::EventLog& events_;

    msg::ActuatorCommand cmd_{};
    msg::PowerStatus     power_{};
    msg::SensorFrame     last_{};

    bool     sensors_ok_    = false;
    bool     ever_had_data_ = false;
    double   last_rx_s_     = 0.0;
    uint32_t samples_       = 0;
};

}  // namespace fsw::io
