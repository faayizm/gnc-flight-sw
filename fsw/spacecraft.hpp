// ============================================================================
//  fsw/spacecraft.hpp -- the spacecraft, assembled from whatever it runs on.
//
//  Everything that makes this flight software THIS spacecraft -- which
//  applications exist, in what order they hear the bus, which tasks run at
//  what rate, how the parameters and the boot count are kept -- lives here.
//  What it runs on arrives as four interfaces (hal/), and nothing else:
//
//      a clock     the time, and a way to wait for it
//      two links   one to the ground, one to the sensors and actuators
//      storage     a few blocks that survive a reset
//
//  So there is exactly one definition of the spacecraft, and each platform's
//  main only builds the four pieces, hands them over, and turns the handle:
//
//      fsw/main.cpp                    a laptop: TCP sockets, a file, POSIX time
//      targets/mps2-an500/main.cpp     a Cortex-M7 under FreeRTOS: UARTs, RAM
//                                      that survives a reset, the RTOS tick
//
//  The two mains differ in about everything except this file, which is the
//  point. The watchdog is not here: when to service it, and what happens when
//  it fires, are the platform's business (hal/watchdog.hpp explains why it is
//  kicked from exactly one place in the main loop).
//
//  Construct it once, with static storage duration: it holds every
//  application, the 4 MiB packet store included, so its size is the flight
//  software's whole memory footprint, known at link time.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

#include "apps/adcs/adcs_app.hpp"
#include "apps/eps/eps_app.hpp"
#include "apps/fdir/fdir_app.hpp"
#include "apps/io/sim_io_app.hpp"
#include "apps/modemgr/mode_manager.hpp"
#include "apps/ttc/ttc_app.hpp"
#include "core/bus.hpp"
#include "core/event_log.hpp"
#include "core/param_store.hpp"
#include "core/scheduler.hpp"
#include "hal/clock.hpp"
#include "hal/link.hpp"
#include "hal/storage.hpp"

namespace fsw {

class Spacecraft {
 public:
    Spacecraft(hal::IClock& clock, hal::ILink& ttc_link, hal::ILink& sim_link, hal::IStorage& storage);

    // Bring everything up: count this boot, wire and initialise every
    // application, load the stored parameters (or keep the defaults), and
    // register every task. On failure, `failure()` says what failed.
    core::Status boot(dict::ResetCause cause);
    const char* failure() const { return failure_; }

    // Raise BOOT_COMPLETE and start the scheduler. Then call
    // scheduler().run_tick_realtime() for ever.
    void start();

    // Persist the parameter table if it has changed. Call about once a second
    // from the main loop: a reset does not run any shutdown code.
    void save_params_if_dirty();

    // True when the stored parameters could not be used at boot, and the
    // compiled-in defaults are flying instead.
    bool on_default_params() const { return !core::is_ok(param_load_); }
    core::Status param_load_status() const { return param_load_; }

    // ST[17,128] has asked the main loop to stop servicing the watchdog.
    bool watchdog_test() const { return ttc_.watchdog_test(); }

    uint16_t                boot_count() const { return boot_count_; }
    core::Scheduler&        scheduler()        { return scheduler_; }
    const ttc::TtcApp&      ttc() const        { return ttc_; }
    core::EventLog&         events()           { return events_; }

    // Non-volatile storage map.
    static constexpr size_t kParamBlock = 0;   // the parameter table, as ParamStore::save writes it
    static constexpr size_t kBootBlock  = 1;   // the boot record: magic, boot count, CRC
    static constexpr size_t kBlockBytes = 512; // the largest block this code will handle

 private:
    uint16_t count_boot();
    bool     reload_params();
    static bool reload_thunk(void* ctx) { return static_cast<Spacecraft*>(ctx)->reload_params(); }
    static void on_overrun(void* ctx, const char* task, uint32_t used_us);

    hal::IStorage& storage_;

    core::Bus        bus_;
    core::EventLog   events_;
    core::ParamStore params_;
    core::Scheduler  scheduler_;

    // Declaration order is construction order. Initialisation order -- which
    // is bus subscription order, which is delivery order -- is set in boot().
    ttc::TtcApp           ttc_;
    adcs::AdcsApp         adcs_;
    eps::EpsApp           eps_;
    io::SimIoApp          io_;
    modemgr::ModeManager  modes_;
    fdir::FdirApp         fdir_;

    dict::ResetCause cause_      = dict::ResetCause::POWER_ON;
    uint16_t         boot_count_ = 0;
    core::Status     param_load_ = core::Status::Ok;
    const char*      failure_    = "";
};

}  // namespace fsw
