// ============================================================================
//  fsw/main.cpp -- the spacecraft on a laptop.
//
//  This is the POSIX platform's main: it builds the laptop's version of the
//  four ports (TCP sockets for the two links, a file for non-volatile
//  storage, the POSIX clock, scaled for running faster than real time) and
//  hands them to fsw::Spacecraft, which is the same on every platform. The
//  board's main, targets/mps2-an500/main.cpp, does the same with UARTs, RAM
//  and FreeRTOS.
//
//  The startup sequence is fixed and deliberate:
//
//    1. Construct the platform    -- clock, links, storage, watchdog
//    2. Find out why we started   -- the reset cause
//    3. Boot the spacecraft       -- count the boot, wire the applications,
//                                    load parameters, register the tasks
//    4. Arm the watchdog
//    5. Run the loop
//
//  Note that ALL objects have static storage duration. There is no `new`
//  anywhere in this program, on any path. The worst-case memory footprint is
//  fixed at link time and can be read out of the binary.
// ============================================================================

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>

#include "generated/dictionary.hpp"
#include "platform/posix/posix_clock.hpp"
#include "platform/posix/posix_file_storage.hpp"
#include "platform/posix/posix_watchdog.hpp"
#include "platform/posix/tcp_server_link.hpp"
#include "spacecraft.hpp"

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

    // ---- 2. why we started -------------------------------------------------
    // The hosted stand-in for a reset-cause register: a marker file the
    // watchdog leaves behind when it fires (platform/posix/posix_watchdog.hpp).
    static char marker[1024];
    std::snprintf(marker, sizeof marker, "%s.reset", opt.nvm_path);
    const fsw::dict::ResetCause cause = fsw::platform::PosixWatchdog::consume_reset_marker(marker)
                                            ? fsw::dict::ResetCause::WATCHDOG
                                            : fsw::dict::ResetCause::POWER_ON;

    // ---- 3. the spacecraft -------------------------------------------------
    static fsw::Spacecraft sc(clock, link, sim_link, storage);
    if (!fsw::core::is_ok(sc.boot(cause))) {
        std::fprintf(stderr, "fatal: %s\n", sc.failure());
        return 1;
    }
    if (sc.on_default_params()) {
        // Expected on a first run, when the block is still all zeroes.
        std::fprintf(stderr, "note: stored parameters not usable (%s), "
                             "running on compiled-in defaults\n",
                     fsw::core::to_string(sc.param_load_status()));
    }

    // ---- 4. watchdog -------------------------------------------------------
    // Three tick periods. Long enough to tolerate one bad tick, short enough
    // that a genuinely wedged loop is caught in under a tenth of a second.
    watchdog.enable(3 * 1000 / fsw::core::Scheduler::kBaseRateHz);
    if (opt.watchdog_ms > 0) { watchdog.arm_host(opt.watchdog_ms, marker); }

    // ---- 5. run ------------------------------------------------------------
    fsw::core::Scheduler& scheduler = sc.scheduler();
    std::printf("HYPERSAT flight software up.\n");
    std::printf("  TT&C link   : TCP 127.0.0.1:%u (waiting for the ground)\n", opt.ttc_port);
    std::printf("  sim bridge  : TCP 127.0.0.1:%u (waiting for the simulator)\n", opt.sim_port);
    std::printf("  base rate   : %u Hz\n", fsw::core::Scheduler::kBaseRateHz);
    std::printf("  time scale  : %.2fx\n", opt.time_scale);
    std::printf("  tasks       : %zu registered\n", scheduler.tasks().size());
    std::printf("  parameters  : %zu\n", fsw::dict::kParamCount);
    std::printf("  boot        : #%u, after %s\n", sc.boot_count(), fsw::dict::to_string(cause));
    std::fflush(stdout);

    sc.start();

    uint32_t last_report_s = 0;
    uint32_t last_save_s = 0;
    while (g_stop == 0) {
        scheduler.run_tick_realtime();
        // The ONE place the watchdog is serviced -- unless the ground has
        // asked to prove that it works (ST[17,128]).
        if (!sc.watchdog_test()) { watchdog.kick(); }

        if (opt.max_ticks > 0 && scheduler.tick_count() >= opt.max_ticks) { break; }

        // Parameters reach non-volatile storage within a second of changing:
        // a reset does not run the shutdown code below.
        if (scheduler.uptime_s() != last_save_s) {
            last_save_s = scheduler.uptime_s();
            sc.save_params_if_dirty();
        }

        if (opt.verbose && scheduler.uptime_s() != last_report_s) {
            last_report_s = scheduler.uptime_s();
            std::printf("t=%5us  link=%s  tc=%u/%u  tm=%u  load=%u%%  overruns=%u\n",
                        last_report_s,
                        link.connected() ? "UP  " : "DOWN",
                        sc.ttc().tc_received(), sc.ttc().tc_rejected(), sc.ttc().tm_sent(),
                        scheduler.load_percent(), scheduler.overrun_count());
            std::fflush(stdout);
        }
    }

    // Persist parameters on the way out, as well as once a second above.
    sc.save_params_if_dirty();

    std::printf("\nshutting down after %u ticks (%u s)\n"
                "  telecommands : %u accepted, %u rejected\n"
                "  telemetry    : %u packets\n"
                "  overruns     : %u\n"
                "  watchdog     : longest gap %u ms, %u notional resets\n",
                scheduler.tick_count(), scheduler.uptime_s(),
                sc.ttc().tc_received(), sc.ttc().tc_rejected(), sc.ttc().tm_sent(),
                scheduler.overrun_count(),
                watchdog.longest_gap_ms(), watchdog.reset_count());
    return 0;
}
