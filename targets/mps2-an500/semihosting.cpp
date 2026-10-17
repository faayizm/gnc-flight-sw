// ============================================================================
//  targets/mps2-an500/semihosting.cpp
// ============================================================================
#include "semihosting.hpp"

#include <cstdint>

namespace semihosting {

namespace {

constexpr uint32_t kSysGetCmdline = 0x15;
constexpr uint32_t kSysExitExtended = 0x20;
constexpr uint32_t kAdpStoppedApplicationExit = 0x20026;

int32_t call(uint32_t op, const void* arg) {
    register uint32_t r0 __asm("r0") = op;
    register const void* r1 __asm("r1") = arg;
    __asm volatile("bkpt 0xAB" : "+r"(r0) : "r"(r1) : "memory");
    return static_cast<int32_t>(r0);
}

}  // namespace

bool command_line(char* buf, size_t size) {
    uint32_t block[2] = {reinterpret_cast<uint32_t>(buf), static_cast<uint32_t>(size)};
    return call(kSysGetCmdline, block) == 0;
}

void exit(int status) {
    const uint32_t block[2] = {kAdpStoppedApplicationExit, static_cast<uint32_t>(status)};
    call(kSysExitExtended, block);
    for (;;) {}
}

}  // namespace semihosting

// With no semihosting host, `bkpt` escalates to a HardFault. If the fault is
// exactly a semihosting call, step over it and return -1 in r0, as a host
// would for a failed call. Anything else is a real fault: stop.
extern "C" __attribute__((naked)) void HardFault_Handler() {
    __asm volatile(
        "tst   lr, #4          \n"   // which stack holds the exception frame?
        "ite   eq              \n"
        "mrseq r0, msp         \n"
        "mrsne r0, psp         \n"
        "ldr   r1, [r0, #24]   \n"   // stacked pc
        "ldrh  r2, [r1]        \n"   // the instruction there
        "movw  r3, #0xBEAB     \n"   // bkpt 0xAB
        "cmp   r2, r3          \n"
        "bne   1f              \n"
        "adds  r1, r1, #2      \n"   // resume after it...
        "str   r1, [r0, #24]   \n"
        "movs  r2, #0          \n"
        "subs  r2, r2, #1      \n"
        "str   r2, [r0, #0]    \n"   // ...with r0 = -1
        "ldr   r1, =0xE000ED2C \n"   // HFSR: clear DEBUGEVT
        "ldr   r2, =0x80000000 \n"
        "str   r2, [r1]        \n"
        "bx    lr              \n"
        "1:                    \n"
        "b     1b              \n");
}
