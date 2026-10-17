// ============================================================================
//  fsw/platform/freertos/cmsdk_watchdog.cpp
// ============================================================================
#include "platform/freertos/cmsdk_watchdog.hpp"

#include "platform/freertos/cortex_m.hpp"

namespace fsw::platform {

namespace {

constexpr uintptr_t kLoad     = 0x000;
constexpr uintptr_t kControl  = 0x008;
constexpr uintptr_t kIntClear = 0x00C;
constexpr uintptr_t kLock     = 0xC00;

constexpr uint32_t kControlIntEn = 1u << 0;
constexpr uint32_t kControlResEn = 1u << 1;
constexpr uint32_t kUnlockKey    = 0x1ACCE551u;   // any other value locks it again

}  // namespace

void CmsdkWatchdog::enable(uint32_t timeout_ms) {
    // Two counter periods from the last kick to the reset.
    const uint64_t load = static_cast<uint64_t>(clock_hz_) * timeout_ms / 2000u;
    cm::reg<uint32_t>(base_ + kLock)    = kUnlockKey;
    cm::reg<uint32_t>(base_ + kLoad)    = static_cast<uint32_t>(load > 0xFFFFFFFFu ? 0xFFFFFFFFu : load);
    cm::reg<uint32_t>(base_ + kControl) = kControlIntEn | kControlResEn;
    cm::reg<uint32_t>(base_ + kLock)    = 0;   // locked: a stray write cannot disarm it
}

void CmsdkWatchdog::kick() {
    cm::reg<uint32_t>(base_ + kLock)     = kUnlockKey;
    cm::reg<uint32_t>(base_ + kIntClear) = 1;
    cm::reg<uint32_t>(base_ + kLock)     = 0;
}

}  // namespace fsw::platform
