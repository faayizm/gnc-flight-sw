// ============================================================================
//  targets/mps2-an500/main.cpp -- the spacecraft on a Cortex-M7.
//
//  This board's main, the counterpart of fsw/main.cpp on the laptop. It
//  builds the board's version of the four ports and hands them to the same
//  fsw::Spacecraft:
//
//      the clock     FreeRTOS's tick, refined by SysTick   (FreeRtosClock)
//      two links     UART1 to the ground, UART2 to the simulator
//      storage       RAM that survives a reset              (RamStorage)
//      watchdog      the CMSDK watchdog, which really resets the board
//
//  The startup sequence is the laptop's, step for step:
//
//    1. Construct the platform    -- here, at namespace scope: the startup
//                                    code runs the constructors before main
//    2. Find out why we started   -- a word in RAM that survives the reset
//    3. Boot the spacecraft
//    4. Arm the watchdog
//    5. Run the loop              -- in one FreeRTOS task, the only one
//
//  Under QEMU the options arrive by semihosting, in the laptop's syntax:
//      --time-scale F  --watchdog-ms N  --max-ticks N  --verbose
// ============================================================================

#include <cstdint>
#include <cstring>

#include "FreeRTOS.h"
#include "task.h"

#include "board.hpp"
#include "console.hpp"
#include "generated/dictionary.hpp"
#include "platform/freertos/cmsdk_uart_link.hpp"
#include "platform/freertos/cmsdk_watchdog.hpp"
#include "platform/freertos/freertos_clock.hpp"
#include "platform/freertos/ram_storage.hpp"
#include "semihosting.hpp"
#include "spacecraft.hpp"

namespace {

using fsw::platform::CmsdkUartLink;
using fsw::platform::CmsdkWatchdog;
using fsw::platform::FreeRtosClock;
using fsw::platform::RamStorage;

// ---- RAM that survives a reset ------------------------------------------------
//
// At an address the linker script sets aside and nothing loads or zeroes. At
// power-on it holds whatever the RAM powered up with (zeroes, under QEMU), so
// `magic` tells a power-on from a reset; `watchdog_fired` is written by the
// NMI, the watchdog's last warning, just before it resets the board.
struct Retained {
    uint32_t magic;
    uint32_t watchdog_fired;
    uint32_t watchdog_resets;
    uint8_t  nvm[RamStorage::kBytes];
};
constexpr uint32_t kRetainedMagic = 0x48595052;   // "HYPR"
constexpr uint32_t kWatchdogFired = 0x57444F47;   // "WDOG"

extern "C" uint32_t __retained_start[];
Retained& g_retained = *reinterpret_cast<Retained*>(__retained_start);
static_assert(sizeof(Retained) <= 64 * 1024, "larger than the NOINIT region in mps2_an500.ld");

// ---- 1. platform --------------------------------------------------------------
FreeRtosClock    g_clock(board::kSysClkHz);
CmsdkUartLink    g_ttc_link(board::kUart1, board::kUart1RxIrq, board::kUart1TxIrq, board::kUartBaudDiv);
CmsdkUartLink    g_sim_link(board::kUart2, board::kUart2RxIrq, board::kUart2TxIrq, board::kUartBaudDiv);
RamStorage       g_storage(g_retained.nvm);
fsw::Spacecraft  g_spacecraft(g_clock, g_ttc_link, g_sim_link, g_storage);

struct Options {
    uint32_t time_scale_centi = 100;   // 1.00x; hundredths, see parse_centi
    uint32_t watchdog_ms      = 1000;
    uint32_t max_ticks        = 0;     // 0 = run for ever
    bool     verbose          = false;
};

// "12.5" -> 1250. By hand: atof is strtod underneath, and in newlib that
// brings in malloc, abort and file I/O -- ten system calls this board does
// not have.
uint32_t parse_centi(const char* s) {
    uint32_t whole = 0, frac = 0, frac_digits = 0;
    for (; *s >= '0' && *s <= '9'; ++s) { whole = whole * 10 + static_cast<uint32_t>(*s - '0'); }
    if (*s == '.') {
        for (++s; *s >= '0' && *s <= '9'; ++s) {
            if (frac_digits < 2) {
                frac = frac * 10 + static_cast<uint32_t>(*s - '0');
                ++frac_digits;
            }
        }
    }
    for (; frac_digits < 2; ++frac_digits) { frac *= 10; }
    return whole * 100 + frac;
}

uint32_t parse_u(const char* s) { return parse_centi(s) / 100; }

// The semihosting command line is one string, program name first.
Options parse_options() {
    Options opt;
    static char line[256];
    if (!semihosting::command_line(line, sizeof line)) { return opt; }
    const char* argv[16];
    int argc = 0;
    for (char* p = line; *p != '\0' && argc < 16;) {
        while (*p == ' ') { *p++ = '\0'; }
        if (*p == '\0') { break; }
        argv[argc++] = p;
        while (*p != '\0' && *p != ' ') { ++p; }
    }
    for (int i = 1; i < argc; ++i) {
        const bool has_value = i + 1 < argc;
        if (std::strcmp(argv[i], "--verbose") == 0) {
            opt.verbose = true;
        } else if (std::strcmp(argv[i], "--time-scale") == 0 && has_value) {
            opt.time_scale_centi = parse_centi(argv[++i]);
        } else if (std::strcmp(argv[i], "--watchdog-ms") == 0 && has_value) {
            opt.watchdog_ms = parse_u(argv[++i]);
        } else if (std::strcmp(argv[i], "--max-ticks") == 0 && has_value) {
            opt.max_ticks = parse_u(argv[++i]);
        } else {
            console::put("ignoring unknown or incomplete option: ");
            console::put(argv[i]);
            console::put("\n");
        }
    }
    return opt;
}

// ---- 2. why we started ----------------------------------------------------------
fsw::dict::ResetCause reset_cause() {
    if (g_retained.magic != kRetainedMagic) {
        // Power-on: the retained RAM is whatever it powered up as. Start it
        // afresh; the storage blocks fail their CRCs, as a blank store does.
        std::memset(&g_retained, 0, sizeof g_retained);
        g_retained.magic = kRetainedMagic;
        return fsw::dict::ResetCause::POWER_ON;
    }
    const bool watchdog = g_retained.watchdog_fired == kWatchdogFired;
    g_retained.watchdog_fired = 0;
    if (watchdog) { ++g_retained.watchdog_resets; }
    // The board has no other reset source than the watchdog and the power.
    return watchdog ? fsw::dict::ResetCause::WATCHDOG : fsw::dict::ResetCause::POWER_ON;
}

void report_tasks(fsw::core::Scheduler& scheduler) {
    console::put("  task         runs    max_us   mean_us\n");
    for (const auto& t : scheduler.tasks()) {
        console::put("  ");
        console::put(t.name);
        for (size_t n = std::strlen(t.name); n < 8; ++n) { console::put(" "); }
        console::put_u(t.runs, 9);
        console::put_u(t.max_us, 10);
        console::put_u(t.runs > 0 ? static_cast<uint32_t>(t.total_us / t.runs) : 0, 10);
        console::put("\n");
    }
}

// ---- 3-5. the flight software task ------------------------------------------------
void flight_task(void*) {
    const Options opt = parse_options();
    g_clock.set_time_scale(static_cast<double>(opt.time_scale_centi) / 100.0);
    g_ttc_link.open();
    g_sim_link.open();

    const fsw::dict::ResetCause cause = reset_cause();

    if (!fsw::core::is_ok(g_spacecraft.boot(cause))) {
        console::put("fatal: ");
        console::put(g_spacecraft.failure());
        console::put("\n");
        for (;;) { vTaskDelay(1000); }   // the watchdog is not armed; stay down
    }

    static CmsdkWatchdog watchdog(board::kWatchdog, board::kSysClkHz,
                                  static_cast<uint16_t>(g_retained.watchdog_resets));
    if (opt.watchdog_ms > 0) { watchdog.enable(opt.watchdog_ms); }

    fsw::core::Scheduler& scheduler = g_spacecraft.scheduler();
    console::put("HYPERSAT flight software up (Cortex-M7, FreeRTOS " tskKERNEL_VERSION_NUMBER ").\n");
    console::put("  TT&C link   : UART1\n");
    console::put("  sim bridge  : UART2\n");
    console::put("  base rate   : "); console::put_u(fsw::core::Scheduler::kBaseRateHz); console::put(" Hz\n");
    console::put("  time scale  : "); console::put_centi(opt.time_scale_centi); console::put("x\n");
    console::put("  tasks       : "); console::put_u(static_cast<uint32_t>(scheduler.tasks().size())); console::put(" registered\n");
    console::put("  parameters  : "); console::put_u(static_cast<uint32_t>(fsw::dict::kParamCount)); console::put("\n");
    console::put("  boot        : #"); console::put_u(g_spacecraft.boot_count());
    console::put(", after "); console::put(fsw::dict::to_string(cause)); console::put("\n");
    console::put("  watchdog    : ");
    if (opt.watchdog_ms > 0) { console::put_u(opt.watchdog_ms); console::put(" ms, "); }
    else { console::put("off, "); }
    console::put_u(watchdog.reset_count()); console::put(" resets so far\n");

    g_spacecraft.start();

    uint32_t last_report_s = 0;
    uint32_t last_save_s = 0;
    for (;;) {
        scheduler.run_tick_realtime();
        // The ONE place the watchdog is serviced -- unless the ground has
        // asked to prove that it works (ST[17,128]).
        if (!g_spacecraft.watchdog_test()) { watchdog.kick(); }

        if (opt.max_ticks > 0 && scheduler.tick_count() >= opt.max_ticks) { break; }

        if (scheduler.uptime_s() != last_save_s) {
            last_save_s = scheduler.uptime_s();
            g_spacecraft.save_params_if_dirty();
        }

        if (opt.verbose && scheduler.uptime_s() != last_report_s) {
            last_report_s = scheduler.uptime_s();
            const auto& ttc = g_spacecraft.ttc();
            console::put("t="); console::put_u(last_report_s);
            console::put("s  tc="); console::put_u(ttc.tc_received());
            console::put("/"); console::put_u(ttc.tc_rejected());
            console::put("  tm="); console::put_u(ttc.tm_sent());
            console::put("  load="); console::put_u(scheduler.load_percent());
            console::put("%  overruns="); console::put_u(scheduler.overrun_count());
            console::put("\n");
        }
    }

    g_spacecraft.save_params_if_dirty();
    console::put("\nstopped after "); console::put_u(scheduler.tick_count());
    console::put(" ticks, "); console::put_u(scheduler.overrun_count()); console::put(" overruns\n");
    report_tasks(scheduler);
    console::put("stack     : ");
    console::put_u(static_cast<uint32_t>(uxTaskGetStackHighWaterMark(nullptr)) * 4u);
    console::put(" bytes never used\n");
    semihosting::exit(0);
}

constexpr uint32_t kFlightStackWords = 8192;   // 32 KiB
StackType_t  g_flight_stack[kFlightStackWords];
StaticTask_t g_flight_tcb;

}  // namespace

// ---- interrupts ---------------------------------------------------------------------

extern "C" {

void uart0_rx_irq() {}
void uart0_tx_irq() {}
void uart1_rx_irq() { g_ttc_link.on_rx_interrupt(); }
void uart1_tx_irq() { g_ttc_link.on_tx_interrupt(); }
void uart2_rx_irq() { g_sim_link.on_rx_interrupt(); }
void uart2_tx_irq() { g_sim_link.on_tx_interrupt(); }

// The watchdog's first expiry. Leave a note for the next boot and wait for
// its second, which resets the board. The interrupt is deliberately not
// cleared: clearing it would be a kick.
[[noreturn]] void NMI_Handler() {
    g_retained.watchdog_fired = kWatchdogFired;
    for (;;) {}
}

// Nothing to do until the next interrupt: the tick, or a byte on a UART.
// Wait for it asleep, as a real board would to save power. (Under QEMU
// -icount, this is also what lets virtual time pass without instructions.)
void vApplicationIdleHook() {
    __asm volatile("wfi");
}

void vApplicationStackOverflowHook(TaskHandle_t, char*) {
    __asm volatile("cpsid i");
    for (;;) {}   // the watchdog resets the board
}

}  // extern "C"

// Called by Reset_Handler (startup.cpp) once RAM is set up and the static
// constructors have run.
extern "C" [[noreturn]] void board_main() {
    console::init();
    xTaskCreateStatic(flight_task, "fsw", kFlightStackWords, nullptr, 2,
                      g_flight_stack, &g_flight_tcb);
    vTaskStartScheduler();
    for (;;) {}   // only reached if the scheduler could not start
}
