// ============================================================================
//  fsw/apps/fdir/fdir_app.hpp -- fault detection, isolation and recovery.
//
//  Everywhere else in this flight software, each application handles its own
//  small faults where they happen: the I/O application refuses a sensor that
//  is lying (io/sensor_screen.hpp), ADCS throws away an estimator that has
//  lost track, EPS sheds load when the battery runs down. Those are local
//  reflexes, and they are right to be local.
//
//  This application is for faults that need a view across applications, and
//  a decision that is bigger than any one of them:
//
//    ON-BOARD MONITORING (PUS ST[12]). Limits on housekeeping values, as
//    declared in the dictionary, checked every time a subsystem publishes
//    its housekeeping. See monitoring.hpp.
//
//    REACTION WHEELS. Is each wheel delivering the torque ADCS asked for?
//    Answering it needs ADCS's commands, the tachometers from the I/O
//    application and the power switches' state from EPS. Acting on it needs
//    EPS (to power-cycle the drives), ADCS (to stop using a wheel) and the
//    mode manager (to give up on pointing). See wheel_check.hpp for the test
//    and wheel_ladder.hpp for the response.
//
//  ON THE BUS
//    in   SensorData, ActuatorCommand, PowerStatus, WheelRestore,
//         AdcsHk, EpsHk (monitored), MonitorControl
//    out  WheelHealth (adcs, modemgr), FdirRails (eps), FdirHk and
//         MonitorReport (ttc)
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

#include "apps/fdir/monitoring.hpp"
#include "apps/fdir/wheel_check.hpp"
#include "apps/fdir/wheel_ladder.hpp"
#include "apps/messages.hpp"
#include "core/bus.hpp"
#include "core/event_log.hpp"
#include "generated/telemetry.hpp"

namespace fsw::fdir {

class FdirApp {
 public:
    FdirApp(core::Bus& bus, core::EventLog& events) : bus_(bus), events_(events) {}

    core::Status init();

    const WheelLadder& ladder() const { return ladder_; }
    const WheelCheck&  check()  const { return check_; }
    const tlm::FdirHk& hk()     const { return hk_; }
    const Monitoring&  monitoring() const { return monitoring_; }

    // One sensor sample, after ADCS and EPS have answered it. Public so unit
    // tests can drive it without a bus.
    void step(const msg::SensorFrame& s);

 private:
    static void on_sensor(void* ctx, core::Topic, const uint8_t* data, size_t length);
    static void on_actuators(void* ctx, core::Topic, const uint8_t* data, size_t length);
    static void on_power(void* ctx, core::Topic, const uint8_t* data, size_t length);
    static void on_restore(void* ctx, core::Topic, const uint8_t* data, size_t length);
    static void on_adcs_hk(void* ctx, core::Topic, const uint8_t* data, size_t length);
    static void on_eps_hk(void* ctx, core::Topic, const uint8_t* data, size_t length);
    static void on_monitor_control(void* ctx, core::Topic, const uint8_t* data, size_t length);

    void monitor(dict::HkSid sid, const void* hk);

    void publish_health();

    core::Bus&      bus_;
    core::EventLog& events_;

    WheelCheck       check_;
    WheelCheckConfig check_cfg_;
    WheelLadder      ladder_;
    Monitoring       monitoring_;

    msg::ActuatorCommand cmd_{};
    msg::PowerStatus     power_{};
    msg::WheelHealth     health_{};
    msg::FdirRails       rails_{};
    tlm::FdirHk          hk_{};
};

}  // namespace fsw::fdir
