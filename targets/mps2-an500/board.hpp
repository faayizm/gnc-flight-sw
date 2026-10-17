// ============================================================================
//  targets/mps2-an500/board.hpp -- the facts about this board.
//
//  Arm's MPS2 FPGA board with the AN500 image: a Cortex-M7 with a
//  double-precision FPU, the CMSDK peripherals, on a 25 MHz system clock.
//  Addresses and interrupt numbers are as QEMU 8.2 models it (hw/arm/mps2.c,
//  checked with `info mtree` and by running).
// ============================================================================
#pragma once

#include <cstdint>

namespace board {

constexpr uint32_t kSysClkHz = 25000000;   // core, SysTick, and peripheral clock

// The three serial ports, which QEMU's -serial options connect in order.
constexpr uintptr_t kUart0 = 0x40004000;   // console
constexpr uintptr_t kUart1 = 0x40005000;   // TT&C: the radio, i.e. the ground
constexpr uintptr_t kUart2 = 0x40006000;   // the simulator's sensors and actuators

// Each UART has an RX and a TX interrupt, numbered from 0 like this.
constexpr uint32_t kUart0RxIrq = 0, kUart0TxIrq = 1;
constexpr uint32_t kUart1RxIrq = 2, kUart1TxIrq = 3;
constexpr uint32_t kUart2RxIrq = 4, kUart2TxIrq = 5;
constexpr uint32_t kIrqCount   = 32;

// 25 MHz / 16, the fastest divider the UART accepts. QEMU does not pace the
// bytes at any baud rate at all; a real board would.
constexpr uint32_t kUartBaudDiv = 16;

constexpr uintptr_t kWatchdog = 0x40008000;   // CMSDK APB watchdog, its interrupt is the NMI

}  // namespace board

// Interrupt handlers, defined in main.cpp, placed in the vector table by
// startup.cpp.
extern "C" {
void uart0_rx_irq();
void uart0_tx_irq();
void uart1_rx_irq();
void uart1_tx_irq();
void uart2_rx_irq();
void uart2_tx_irq();
}
