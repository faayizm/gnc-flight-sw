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
    double      time_scale = 1.0;
    const char* nvm_path   = "hypersat_nvm.bin";
    uint32_t    max_ticks  = 0;   // 0 = run until interrupted
    bool        verbose    = false;
};

void print_usage(const char* argv0) {
    std::printf(
        "HYPERSAT flight software (software-in-the-loop build)\n"
        "\n"
        "usage: %s [options]\n"
        "  --ttc-port N     TCP port the ground connects to      (default 50001)\n"
        "  --time-scale F   simulation speed, 1.0 = real time    (default 1.0)\n"
        "  --nvm PATH       non-volatile storage file            (default hypersat_nvm.bin)\n"
        "  --max-ticks N    stop after N scheduler ticks, for tests\n"
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
        } else if (std::strcmp(a, "--time-scale") == 0 && has_value) {
            opt.time_scale = std::atof(argv[++i]);
        } else if (std::strcmp(a, "--nvm") == 0 && has_value) {
            opt.nvm_path = argv[++i];
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

// Block in non-volatile storage holding the parameter table. Block 0 is used
// because nothing else claims it yet; a later phase will define a proper map.
constexpr size_t kParamBlock = 0;

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
    static fsw::platform::PosixFileStorage storage(opt.nvm_path);
    static fsw::platform::PosixWatchdog    watchdog(clock);

    if (!fsw::core::is_ok(link.open())) {
        std::fprintf(stderr, "fatal: cannot listen on TCP port %u "
                             "(already in use?)\n", opt.ttc_port);
        return 1;
    }
    if (!fsw::core::is_ok(storage.open())) {
        std::fprintf(stderr, "fatal: cannot open non-volatile storage '%s'\n",
                     opt.nvm_path);
        return 1;
    }

    // ---- 2. core -----------------------------------------------------------
    static fsw::core::Bus        bus;
    static fsw::core::EventLog   events;
    static fsw::core::ParamStore params;
    static fsw::core::Scheduler  scheduler(clock);

    scheduler.set_overrun_handler(&on_scheduler_overrun, &events);

    // ---- 3. applications ---------------------------------------------------
    static fsw::ttc::TtcApp ttc(link, clock, bus, events, params, scheduler);
    if (!fsw::core::is_ok(ttc.init())) {
        std::fprintf(stderr, "fatal: TT&C application failed to initialise\n");
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

    // ---- 6. watchdog -------------------------------------------------------
    // Three tick periods. Long enough to tolerate one bad tick, short enough
    // that a genuinely wedged loop is caught in under a tenth of a second.
    watchdog.enable(3 * 1000 / fsw::core::Scheduler::kBaseRateHz);

    // ---- 7. run ------------------------------------------------------------
    std::printf("HYPERSAT flight software up.\n");
    std::printf("  TT&C link   : TCP 127.0.0.1:%u (waiting for the ground)\n", opt.ttc_port);
    std::printf("  base rate   : %u Hz\n", fsw::core::Scheduler::kBaseRateHz);
    std::printf("  time scale  : %.2fx\n", opt.time_scale);
    std::printf("  tasks       : %zu registered\n", scheduler.tasks().size());
    std::printf("  parameters  : %zu\n", fsw::dict::kParamCount);
    std::fflush(stdout);

    events.raise(fsw::dict::EventId::BOOT_COMPLETE);
    scheduler.start();

    uint32_t last_report_s = 0;
    while (g_stop == 0) {
        scheduler.run_tick_realtime();
        watchdog.kick();

        if (opt.max_ticks > 0 && scheduler.tick_count() >= opt.max_ticks) { break; }

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

    // Persist parameters on the way out. In flight this would also happen
    // periodically, because an unplanned reset does not run this code.
    if (params.dirty()) {
        size_t  written = 0;
        uint8_t out_block[fsw::platform::PosixFileStorage::kBlockSize];
        std::memset(out_block, 0, sizeof out_block);
        if (fsw::core::is_ok(params.save(out_block, sizeof out_block, written))) {
            storage.write(kParamBlock, out_block, sizeof out_block);
        }
    }

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
