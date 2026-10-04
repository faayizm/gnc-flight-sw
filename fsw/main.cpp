// ============================================================================
//  fsw/main.cpp -- the composition root.
//
//  This is the ONLY file that knows both what the applications are and what
//  hardware they are running on. Everything else is written against interfaces.
//  Porting this flight software to a different platform means writing new
//  adapters under platform/ and a new main; not one line of core/ or apps/
//  changes.
//
//  The startup sequence is fixed and deliberate:
//
//    1. Construct the platform    -- clock, link, storage, watchdog
//    2. Construct the core        -- bus, event log, parameters, scheduler
//    3. Construct the applications
//    4. Load parameters, falling back to defaults if they cannot be trusted
//    5. Register every task, in the order they will run
//    6. Arm the watchdog
//    7. Run the loop
//
//  Note that ALL objects have static storage duration. There is no `new`
//  anywhere in this program, on any path. The worst-case memory footprint is
//  fixed at link time and can be read out of the binary.
// ============================================================================

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>

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
#include "generated/dictionary.hpp"
#include "platform/posix/posix_clock.hpp"
#include "platform/posix/posix_file_storage.hpp"
#include "platform/posix/posix_watchdog.hpp"
#include "platform/posix/tcp_server_link.hpp"

namespace {

// Set by the signal handler so the main loop can shut down between ticks
// rather than in the middle of one. volatile sig_atomic_t is the only type a
// signal handler may portably touch.
volatile std::sig_atomic_t g_stop = 0;

void on_signal(int) { g_stop = 1; }

struct Options {
    uint16_t    ttc_port   = 50001;
    uint16_t    sim_port   = 50000;
    double      time_scale = 1.0;
    const char* nvm_path   = "hypersat_nvm.bin";
    uint32_t    max_ticks  = 0;   // 0 = run until interrupted
    bool        verbose    = false;
    uint32_t    watchdog_ms = 5000;
};

void print_usage(const char* argv0) {
    std::printf(
        "HYPERSAT flight software (software-in-the-loop build)\n"
        "\n"
        "usage: %s [options]\n"
        "  --ttc-port N     TCP port the ground connects to      (default 50001)\n"
        "  --sim-port N     TCP port the simulator connects to   (default 50000)\n"
        "  --time-scale F   simulation speed, 1.0 = real time    (default 1.0)\n"
        "  --nvm PATH       non-volatile storage file            (default hypersat_nvm.bin)\n"
        "  --max-ticks N    stop after N scheduler ticks, for tests\n"
        "  --watchdog-ms N  host watchdog: reset if the loop stalls N ms (default 5000, 0 = off)\n"
        "  --verbose        print a status line once per second\n"
        "  --help           this message\n",
        argv0);
}

bool parse_args(int argc, char** argv, Options& opt) {
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        const bool has_value = (i + 1 < argc);

        if (std::strcmp(a, "--help") == 0) { print_usage(argv[0]); return false; }
        else if (std::strcmp(a, "--verbose") == 0) { opt.verbose = true; }
        else if (std::strcmp(a, "--ttc-port") == 0 && has_value) {
            opt.ttc_port = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(a, "--sim-port") == 0 && has_value) {
            opt.sim_port = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(a, "--time-scale") == 0 && has_value) {
            opt.time_scale = std::atof(argv[++i]);
        } else if (std::strcmp(a, "--nvm") == 0 && has_value) {
            opt.nvm_path = argv[++i];
        } else if (std::strcmp(a, "--watchdog-ms") == 0 && has_value) {
            opt.watchdog_ms = static_cast<uint32_t>(std::atol(argv[++i]));
        } else if (std::strcmp(a, "--max-ticks") == 0 && has_value) {
            opt.max_ticks = static_cast<uint32_t>(std::atol(argv[++i]));
        } else {
            std::fprintf(stderr, "unknown or incomplete option: %s\n", a);
            print_usage(argv[0]);
            return false;
        }
    }
    return true;
}

// Non-volatile storage map.
constexpr size_t kParamBlock = 0;     // the parameter table, as ParamStore::save writes it
constexpr size_t kBootBlock  = 1;     // the boot record: magic, boot count, CRC

fsw::platform::PosixFileStorage* g_storage = nullptr;
fsw::core::ParamStore*           g_params  = nullptr;

void save_params_if_dirty() {
    if (!g_params->dirty()) { return; }
    size_t  written = 0;
    uint8_t block[fsw::platform::PosixFileStorage::kBlockSize];
    std::memset(block, 0, sizeof block);
    if (fsw::core::is_ok(g_params->save(block, sizeof block, written)) &&
        fsw::core::is_ok(g_storage->write(kParamBlock, block, sizeof block))) {
        g_params->clear_dirty();
    }
}

// FDIR's recovery for a parameter word damaged beyond repair.
bool reload_params(void*) {
    uint8_t block[fsw::platform::PosixFileStorage::kBlockSize];
    return fsw::core::is_ok(g_storage->read(kParamBlock, block, sizeof block)) &&
           fsw::core::is_ok(g_params->load(block, sizeof block));
}

// Count this boot. The count survives every kind of reset, so a rising number
// on the ground is the clearest sign something keeps going wrong.
uint16_t count_boot(fsw::platform::PosixFileStorage& storage) {
    uint8_t b[fsw::platform::PosixFileStorage::kBlockSize];
    std::memset(b, 0, sizeof b);
    uint16_t count = 0;
    if (fsw::core::is_ok(storage.read(kBootBlock, b, sizeof b)) && b[0] == 'B' && b[1] == 'R' &&
        fsw::core::crc16_check(b, 6)) {
        count = static_cast<uint16_t>((b[2] << 8) | b[3]);
    }
    ++count;
    std::memset(b, 0, sizeof b);
    b[0] = 'B'; b[1] = 'R';
    b[2] = static_cast<uint8_t>(count >> 8);
    b[3] = static_cast<uint8_t>(count & 0xFF);
    const uint16_t crc = fsw::core::crc16(b, 4);
    b[4] = static_cast<uint8_t>(crc >> 8);
    b[5] = static_cast<uint8_t>(crc & 0xFF);
    storage.write(kBootBlock, b, sizeof b);
    return count;
}

// Bridges a scheduler overrun into an event report. The scheduler cannot call
// the event log directly without core/ acquiring a dependency it does not need.
void on_scheduler_overrun(void* context, const char* task_name, uint32_t used_us) {
    (void)task_name;
    auto* events = static_cast<fsw::core::EventLog*>(context);
    events->raise(fsw::dict::EventId::SCHED_OVERRUN, used_us);
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!parse_args(argc, argv, opt)) { return 0; }

    std::signal(SIGINT,  on_signal);
    std::signal(SIGTERM, on_signal);

    // ---- 1. platform -------------------------------------------------------
    static fsw::platform::PosixClock       clock(opt.time_scale);
    static fsw::platform::TcpServerLink    link(opt.ttc_port);
    static fsw::platform::TcpServerLink    sim_link(opt.sim_port);
    static fsw::platform::PosixFileStorage storage(opt.nvm_path);
    static fsw::platform::PosixWatchdog    watchdog(clock);

    if (!fsw::core::is_ok(link.open())) {
        std::fprintf(stderr, "fatal: cannot listen on TCP port %u "
                             "(already in use?)\n", opt.ttc_port);
        return 1;
    }
    if (!fsw::core::is_ok(sim_link.open())) {
        std::fprintf(stderr, "fatal: cannot listen on TCP port %u "
                             "(already in use?)\n", opt.sim_port);
        return 1;
    }
    if (!fsw::core::is_ok(storage.open())) {
        std::fprintf(stderr, "fatal: cannot open non-volatile storage '%s'\n",
                     opt.nvm_path);
        return 1;
    }

    g_storage = &storage;

    // The hosted stand-in for a reset-cause register: a marker file the
    // watchdog leaves behind when it fires (platform/posix/posix_watchdog.hpp).
    static char marker[1024];
    std::snprintf(marker, sizeof marker, "%s.reset", opt.nvm_path);
    const fsw::dict::ResetCause cause = fsw::platform::PosixWatchdog::consume_reset_marker(marker)
                                            ? fsw::dict::ResetCause::WATCHDOG
                                            : fsw::dict::ResetCause::POWER_ON;
    const uint16_t boots = count_boot(storage);

    // ---- 2. core -----------------------------------------------------------
    static fsw::core::Bus        bus;
    static fsw::core::EventLog   events;
    static fsw::core::ParamStore params;
    g_params = &params;
    static fsw::core::Scheduler  scheduler(clock);

    scheduler.set_overrun_handler(&on_scheduler_overrun, &events);

    // ---- 3. applications ---------------------------------------------------
    static fsw::ttc::TtcApp ttc(link, clock, bus, events, params, scheduler);
    if (!fsw::core::is_ok(ttc.init())) {
        std::fprintf(stderr, "fatal: TT&C application failed to initialise\n");
        return 1;
    }
    ttc.set_boot_info(boots, cause);

    // Order of construction is order of bus subscription, which is order of
    // delivery: on each sensor sample ADCS runs first, then EPS, then the
    // I/O app sends the reply built from both.
    static fsw::adcs::AdcsApp       adcs(bus, events, params);
    static fsw::eps::EpsApp         eps(bus, events, params);
    static fsw::io::SimIoApp        io(sim_link, clock, bus, events);
    static fsw::modemgr::ModeManager modes(clock, bus, events, params);
    // Last: FDIR judges each sample after ADCS, EPS and I/O have answered it.
    static fsw::fdir::FdirApp       fdir(bus, events);
    fdir.protect(params, modes.mode_store());
    fdir.set_param_reload(&reload_params, nullptr);
    io.set_upset_handler([](void* ctx, uint8_t target, uint32_t bit) {
        static_cast<fsw::fdir::FdirApp*>(ctx)->upset(target, bit);
    }, &fdir);
    if (!fsw::core::is_ok(adcs.init()) || !fsw::core::is_ok(eps.init()) ||
        !fsw::core::is_ok(io.init()) || !fsw::core::is_ok(modes.init()) ||
        !fsw::core::is_ok(fdir.init())) {
        std::fprintf(stderr, "fatal: an application failed to initialise\n");
        return 1;
    }

    // ---- 4. parameters -----------------------------------------------------
    // Defaults first, unconditionally. Whatever happens next, the spacecraft is
    // already running on values known to be within their declared limits.
    params.reset_to_defaults();

    uint8_t nvm_block[fsw::platform::PosixFileStorage::kBlockSize];
    if (fsw::core::is_ok(storage.read(kParamBlock, nvm_block, sizeof nvm_block))) {
        const fsw::core::Status s = params.load(nvm_block, sizeof nvm_block);
        if (!fsw::core::is_ok(s)) {
            // Expected on a first run, when the block is still all zeroes.
            // Also what happens after corruption -- and the response is the
            // same either way: keep the defaults, and say so.
            std::fprintf(stderr, "note: stored parameters not usable (%s), "
                                 "running on compiled-in defaults\n",
                         fsw::core::to_string(s));
        }
    }

    // ---- 5. tasks ----------------------------------------------------------
    // Divider is in base ticks: 1 = 50 Hz, 5 = 10 Hz, 50 = 1 Hz.
    // Offsets stagger the slower groups so they never land on the same tick.
    scheduler.add_task("ttc_rx",  &fsw::ttc::TtcApp::task_receive,   &ttc, 1);
    scheduler.add_task("ttc_tm",  &fsw::ttc::TtcApp::task_telemetry, &ttc, 5, 1);
    scheduler.add_task("ttc_dl",  &fsw::ttc::TtcApp::task_downlink,  &ttc, 1);

    scheduler.add_task("io",      &fsw::io::SimIoApp::task_run,      &io, 1);
    scheduler.add_task("modes",   &fsw::modemgr::ModeManager::task_run, &modes, 5, 3);

    // ---- 6. watchdog -------------------------------------------------------
    // Three tick periods. Long enough to tolerate one bad tick, short enough
    // that a genuinely wedged loop is caught in under a tenth of a second.
    watchdog.enable(3 * 1000 / fsw::core::Scheduler::kBaseRateHz);
    if (opt.watchdog_ms > 0) { watchdog.arm_host(opt.watchdog_ms, marker); }

    // ---- 7. run ------------------------------------------------------------
    std::printf("HYPERSAT flight software up.\n");
    std::printf("  TT&C link   : TCP 127.0.0.1:%u (waiting for the ground)\n", opt.ttc_port);
    std::printf("  sim bridge  : TCP 127.0.0.1:%u (waiting for the simulator)\n", opt.sim_port);
    std::printf("  base rate   : %u Hz\n", fsw::core::Scheduler::kBaseRateHz);
    std::printf("  time scale  : %.2fx\n", opt.time_scale);
    std::printf("  tasks       : %zu registered\n", scheduler.tasks().size());
    std::printf("  parameters  : %zu\n", fsw::dict::kParamCount);
    std::printf("  boot        : #%u, after %s\n", boots, fsw::dict::to_string(cause));
    std::fflush(stdout);

    events.raise(fsw::dict::EventId::BOOT_COMPLETE, static_cast<uint32_t>(cause));
    scheduler.start();

    uint32_t last_report_s = 0;
    uint32_t last_save_s = 0;
    while (g_stop == 0) {
        scheduler.run_tick_realtime();
        // The ONE place the watchdog is serviced -- unless the ground has
        // asked to prove that it works (ST[17,128]).
        if (!ttc.watchdog_test()) { watchdog.kick(); }

        if (opt.max_ticks > 0 && scheduler.tick_count() >= opt.max_ticks) { break; }

        // Parameters reach non-volatile storage within a second of changing:
        // a reset does not run the shutdown code below.
        if (scheduler.uptime_s() != last_save_s) {
            last_save_s = scheduler.uptime_s();
            save_params_if_dirty();
        }

        if (opt.verbose && scheduler.uptime_s() != last_report_s) {
            last_report_s = scheduler.uptime_s();
            std::printf("t=%5us  link=%s  tc=%u/%u  tm=%u  load=%u%%  overruns=%u\n",
                        last_report_s,
                        link.connected() ? "UP  " : "DOWN",
                        ttc.tc_received(), ttc.tc_rejected(), ttc.tm_sent(),
                        scheduler.load_percent(), scheduler.overrun_count());
            std::fflush(stdout);
        }
    }

    // Persist parameters on the way out, as well as once a second above.
    save_params_if_dirty();

    std::printf("\nshutting down after %u ticks (%u s)\n"
                "  telecommands : %u accepted, %u rejected\n"
                "  telemetry    : %u packets\n"
                "  overruns     : %u\n"
                "  watchdog     : longest gap %u ms, %u notional resets\n",
                scheduler.tick_count(), scheduler.uptime_s(),
                ttc.tc_received(), ttc.tc_rejected(), ttc.tm_sent(),
                scheduler.overrun_count(),
                watchdog.longest_gap_ms(), watchdog.reset_count());
    return 0;
}
