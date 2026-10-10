// ============================================================================
//  fsw/spacecraft.cpp -- see spacecraft.hpp.
// ============================================================================
#include "spacecraft.hpp"

#include <cstring>

#include "core/crc.hpp"

namespace fsw {

Spacecraft::Spacecraft(hal::IClock& clock, hal::ILink& ttc_link, hal::ILink& sim_link,
                       hal::IStorage& storage)
    : storage_(storage),
      scheduler_(clock),
      ttc_(ttc_link, clock, bus_, events_, params_, scheduler_),
      adcs_(bus_, events_, params_),
      eps_(bus_, events_, params_),
      io_(sim_link, clock, bus_, events_),
      modes_(clock, bus_, events_, params_),
      fdir_(bus_, events_) {}

void Spacecraft::on_overrun(void* ctx, const char* task, uint32_t used_us) {
    // The scheduler cannot raise events itself without core/ depending on the
    // dictionary's event list; this is the bridge.
    (void)task;
    static_cast<Spacecraft*>(ctx)->events_.raise(dict::EventId::SCHED_OVERRUN, used_us);
}

// Count this boot. The count survives every kind of reset, so a rising number
// on the ground is the clearest sign something keeps going wrong.
uint16_t Spacecraft::count_boot() {
    uint8_t b[kBlockBytes];
    std::memset(b, 0, sizeof b);
    const size_t n = storage_.block_size();
    uint16_t count = 0;
    if (core::is_ok(storage_.read(kBootBlock, b, n)) && b[0] == 'B' && b[1] == 'R' &&
        core::crc16_check(b, 6)) {
        count = static_cast<uint16_t>((b[2] << 8) | b[3]);
    }
    ++count;
    std::memset(b, 0, sizeof b);
    b[0] = 'B'; b[1] = 'R';
    b[2] = static_cast<uint8_t>(count >> 8);
    b[3] = static_cast<uint8_t>(count & 0xFF);
    const uint16_t crc = core::crc16(b, 4);
    b[4] = static_cast<uint8_t>(crc >> 8);
    b[5] = static_cast<uint8_t>(crc & 0xFF);
    storage_.write(kBootBlock, b, n);
    return count;
}

// FDIR's recovery for a parameter word damaged beyond repair.
bool Spacecraft::reload_params() {
    uint8_t b[kBlockBytes];
    const size_t n = storage_.block_size();
    return core::is_ok(storage_.read(kParamBlock, b, n)) && core::is_ok(params_.load(b, n));
}

void Spacecraft::save_params_if_dirty() {
    if (!params_.dirty()) { return; }
    size_t  written = 0;
    uint8_t b[kBlockBytes];
    std::memset(b, 0, sizeof b);
    const size_t n = storage_.block_size();
    if (core::is_ok(params_.save(b, n, written)) && core::is_ok(storage_.write(kParamBlock, b, n))) {
        params_.clear_dirty();
    }
}

core::Status Spacecraft::boot(dict::ResetCause cause) {
    if (storage_.block_size() > kBlockBytes || storage_.block_count() <= kBootBlock) {
        failure_ = "non-volatile storage has an unexpected block layout";
        return core::Status::Invalid;
    }
    cause_ = cause;
    boot_count_ = count_boot();

    scheduler_.set_overrun_handler(&Spacecraft::on_overrun, this);

    if (!core::is_ok(ttc_.init())) {
        failure_ = "TT&C application failed to initialise";
        return core::Status::Invalid;
    }
    ttc_.set_boot_info(boot_count_, cause);

    // Order of initialisation is order of bus subscription, which is order of
    // delivery: on each sensor sample ADCS runs first, then EPS, then the I/O
    // app sends the reply built from both; FDIR judges the sample last.
    fdir_.protect(params_, modes_.mode_store());
    fdir_.set_param_reload(&Spacecraft::reload_thunk, this);
    io_.set_upset_handler([](void* ctx, uint8_t target, uint32_t bit) {
        static_cast<fdir::FdirApp*>(ctx)->upset(target, bit);
    }, &fdir_);
    if (!core::is_ok(adcs_.init()) || !core::is_ok(eps_.init()) || !core::is_ok(io_.init()) ||
        !core::is_ok(modes_.init()) || !core::is_ok(fdir_.init())) {
        failure_ = "an application failed to initialise";
        return core::Status::Invalid;
    }

    // Defaults first, unconditionally. Whatever happens next, the spacecraft
    // is already running on values known to be within their declared limits.
    params_.reset_to_defaults();
    uint8_t b[kBlockBytes];
    const size_t n = storage_.block_size();
    // Expected on a first boot, when the block is still blank. Also what
    // happens after corruption -- and the response is the same either way:
    // keep the defaults.
    param_load_ = storage_.read(kParamBlock, b, n);
    if (core::is_ok(param_load_)) { param_load_ = params_.load(b, n); }

    // Divider is in base ticks: 1 = 50 Hz, 5 = 10 Hz, 50 = 1 Hz. Offsets
    // stagger the slower groups so they never land on the same tick.
    const core::Status s[] = {
        scheduler_.add_task("ttc_rx",  &ttc::TtcApp::task_receive,   &ttc_, 1),
        scheduler_.add_task("ttc_tm",  &ttc::TtcApp::task_telemetry, &ttc_, 5, 1),
        scheduler_.add_task("ttc_dl",  &ttc::TtcApp::task_downlink,  &ttc_, 1),
        scheduler_.add_task("io",      &io::SimIoApp::task_run,      &io_, 1),
        scheduler_.add_task("modes",   &modemgr::ModeManager::task_run, &modes_, 5, 3),
    };
    for (core::Status st : s) {
        if (!core::is_ok(st)) {
            failure_ = "a task could not be registered";
            return st;
        }
    }
    return core::Status::Ok;
}

void Spacecraft::start() {
    events_.raise(dict::EventId::BOOT_COMPLETE, static_cast<uint32_t>(cause_));
    scheduler_.start();
}

}  // namespace fsw
