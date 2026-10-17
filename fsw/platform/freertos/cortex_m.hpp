// ============================================================================
//  fsw/platform/freertos/cortex_m.hpp -- the few Cortex-M core registers the
//  board ports touch, written out by hand.
//
//  A vendor would ship these as CMSIS headers. Writing out the dozen that are
//  used keeps the board build free of a vendor SDK, and keeps every register
//  this code depends on visible in one screen.
// ============================================================================
#pragma once

#include <cstdint>

namespace fsw::platform::cm {

template <typename T>
inline volatile T& reg(uintptr_t address) {
    return *reinterpret_cast<volatile T*>(address);
}

// SysTick: a 24-bit down-counter on the core clock, which FreeRTOS uses for
// its tick. Reading its current value gives time finer than one tick.
inline volatile uint32_t& systick_load()  { return reg<uint32_t>(0xE000E014u); }
inline volatile uint32_t& systick_value() { return reg<uint32_t>(0xE000E018u); }

// Interrupt Control and State: bit 26 says a SysTick interrupt is pending,
// i.e. the counter has wrapped but the tick count has not caught up yet.
inline volatile uint32_t& scb_icsr()      { return reg<uint32_t>(0xE000ED04u); }
constexpr uint32_t kIcsrPendSysTick = 1u << 26;

// Application Interrupt and Reset Control: writing VECTKEY | SYSRESETREQ asks
// the system for a reset.
inline volatile uint32_t& scb_aircr()     { return reg<uint32_t>(0xE000ED0Cu); }

// Coprocessor Access Control: CP10 and CP11 full access switches the FPU on.
inline volatile uint32_t& scb_cpacr()     { return reg<uint32_t>(0xE000ED88u); }

inline void nvic_enable(uint32_t irq) {
    reg<uint32_t>(0xE000E100u + 4u * (irq / 32u)) = 1u << (irq % 32u);
}

inline void nvic_set_priority(uint32_t irq, uint8_t priority) {
    reg<uint8_t>(0xE000E400u + irq) = priority;
}

// Mask every interrupt, returning what PRIMASK was so it can be restored.
inline uint32_t irq_save() {
    uint32_t primask = 0;
    __asm volatile("mrs %0, primask\n cpsid i" : "=r"(primask) : : "memory");
    return primask;
}

inline void irq_restore(uint32_t primask) {
    __asm volatile("msr primask, %0" : : "r"(primask) : "memory");
}

inline void dmb() { __asm volatile("dmb" : : : "memory"); }

}  // namespace fsw::platform::cm
