// ============================================================================
//  targets/mps2-an500/startup.cpp -- from reset to board_main().
//
//  What a hosted program gets from the operating system and the C runtime,
//  done here by hand, in this order:
//
//    1. The hardware loads the stack pointer and the reset vector from the
//       first two words of the vector table, at address 0.
//    2. Reset_Handler switches the FPU on (before any code that might use a
//       floating-point register), copies .data from where the image stores it
//       to RAM, zeroes .bss, runs the C++ static constructors, and calls
//       board_main(), this board's main.
//
//  Plus the vector table itself, and the handful of C runtime hooks that the
//  C and C++ libraries expect to find, each of which is a decision about
//  what this program must never do (allocate, exit).
// ============================================================================
#include <cstddef>
#include <cstdint>

#include "board.hpp"
#include "platform/freertos/cortex_m.hpp"

extern "C" {

// From the linker script.
extern uint32_t __main_stack_top;
extern uint32_t __data_start, __data_end, __data_load;
extern uint32_t __bss_start, __bss_end;
extern void (*__init_array_start[])();
extern void (*__init_array_end[])();

// main.cpp's entry point. Not main(): C++ forbids calling main, even here.
[[noreturn]] void board_main();

// FreeRTOS's handlers (portable/GCC/ARM_CM4F/port.c).
void vPortSVCHandler();
void xPortPendSVHandler();
void xPortSysTickHandler();

[[noreturn]] void Reset_Handler() {
    namespace cm = fsw::platform::cm;
    cm::scb_cpacr() = cm::scb_cpacr() | (0xFu << 20);   // CP10, CP11: full access
    __asm volatile("dsb\n isb" ::: "memory");

    // Plain loops, not memcpy/memset: the C library is not set up yet, and
    // the compiler must not turn these back into calls to it.
    const uint32_t* src = &__data_load;
    for (uint32_t* dst = &__data_start; dst < &__data_end;) { *dst++ = *src++; }
    for (uint32_t* dst = &__bss_start; dst < &__bss_end;) { *dst++ = 0; }

    for (auto* ctor = __init_array_start; ctor < __init_array_end; ++ctor) { (*ctor)(); }

    board_main();
}

[[noreturn]] void Default_Handler() {
    for (;;) {}
}

// A fault: stop here, where a debugger can see why. The watchdog, which
// nothing is kicking any more, resets the board shortly after.
[[noreturn]] void Fault_Handler() {
    for (;;) {}
}

void HardFault_Handler();
void NMI_Handler();

// ---- the vector table -------------------------------------------------------

using Vector = void (*)();

__attribute__((section(".isr_vector"), used))
const Vector g_vectors[16 + board::kIrqCount] = {
    reinterpret_cast<Vector>(&__main_stack_top),
    Reset_Handler,
    NMI_Handler,
    HardFault_Handler,
    Fault_Handler,          // MemManage
    Fault_Handler,          // BusFault
    Fault_Handler,          // UsageFault
    nullptr, nullptr, nullptr, nullptr,
    vPortSVCHandler,        // SVCall: FreeRTOS starts the first task with it
    Default_Handler,        // DebugMon
    nullptr,
    xPortPendSVHandler,     // PendSV: FreeRTOS switches tasks in it
    xPortSysTickHandler,    // SysTick: the RTOS tick
    // External interrupts, numbered from 0.
    uart0_rx_irq, uart0_tx_irq,
    uart1_rx_irq, uart1_tx_irq,
    uart2_rx_irq, uart2_tx_irq,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler,
};

// ---- C runtime hooks ---------------------------------------------------------
//
// Each of these is here because something in the C or C++ library refers to
// it. None of them may allocate: there is no heap, and the linker proves it,
// because nothing below provides _sbrk -- anything that calls malloc fails to
// link.

// A static object with a destructor registers it to run at exit. This
// program never exits, so the registration is accepted and forgotten.
// (Otherwise the C library's atexit table would be linked, which grows by
// calling malloc.)
int __cxa_atexit(void (*)(void*), void*, void*) { return 0; }
void* __dso_handle = nullptr;

// A pure virtual function called during construction or destruction.
[[noreturn]] void __cxa_pure_virtual() { for (;;) {} }

}  // extern "C"

// Every class with a virtual destructor has a "deleting destructor" in its
// vtable, for `delete p` through a base pointer. Nothing here ever deletes
// anything, but the vtable still names the function. Defined here, it can
// never be reached; left to the library, it would bring in free() and malloc().
void operator delete(void*) noexcept { for (;;) {} }
void operator delete(void*, std::size_t) noexcept { for (;;) {} }
