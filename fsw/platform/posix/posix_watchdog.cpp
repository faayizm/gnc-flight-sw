// ============================================================================
//  fsw/platform/posix/posix_watchdog.cpp
// ============================================================================
#include "platform/posix/posix_watchdog.hpp"

#include <fcntl.h>
#include <signal.h>
#include <sys/time.h>
#include <unistd.h>

#include <cstring>

namespace fsw::platform {

namespace {

const char* g_marker = nullptr;

void rearm(uint32_t ms) {
    itimerval t{};
    t.it_value.tv_sec  = static_cast<time_t>(ms / 1000u);
    t.it_value.tv_usec = static_cast<suseconds_t>((ms % 1000u) * 1000u);
    setitimer(ITIMER_REAL, &t, nullptr);
}

// Only async-signal-safe calls in here: open, write, close, _exit.
extern "C" void on_watchdog_expiry(int) {
    if (g_marker != nullptr) {
        const int fd = ::open(g_marker, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            const char w = 'W';
            const ssize_t n = ::write(fd, &w, 1);
            (void)n;
            ::close(fd);
        }
    }
    ::_exit(PosixWatchdog::kResetExitCode);
}

}  // namespace

void PosixWatchdog::arm_host(uint32_t timeout_ms, const char* marker_path) {
    g_marker = marker_path;
    host_timeout_ms_ = timeout_ms;
    struct sigaction sa{};
    sa.sa_handler = &on_watchdog_expiry;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, nullptr);
    rearm(timeout_ms);
}

bool PosixWatchdog::consume_reset_marker(const char* marker_path) {
    if (::access(marker_path, F_OK) != 0) { return false; }
    ::unlink(marker_path);
    return true;
}

void PosixWatchdog::enable(uint32_t timeout_ms) {
    enabled_    = true;
    timeout_ms_ = timeout_ms;
    last_kick_  = clock_.now();
}

void PosixWatchdog::kick() {
    if (host_timeout_ms_ > 0) { rearm(host_timeout_ms_); }
    if (!enabled_) { return; }

    const core::Instant now = clock_.now();
    const auto gap_ms = static_cast<uint32_t>((now - last_kick_).to_millis());

    if (gap_ms > longest_gap_ms_) { longest_gap_ms_ = gap_ms; }
    if (timeout_ms_ > 0 && gap_ms > timeout_ms_) {
        // On real hardware the processor would be resetting right now.
        ++would_have_reset_;
    }
    last_kick_ = now;
}

}  // namespace fsw::platform
