// ============================================================================
//  fsw/platform/freertos/cmsdk_uart_link.hpp -- ILink over an Arm CMSDK UART.
//
//  The CMSDK APB UART is the serial port in Arm's reference designs, the MPS2
//  boards among them. It holds ONE byte each way. At any real baud rate the
//  50 Hz scheduler cannot keep up with that one byte by polling, so both
//  directions run on interrupts, through a ring buffer each:
//
//    receive   the RX interrupt moves each byte into rx_; receive() takes
//              them out from the task
//    send      send() queues into tx_ and, if the transmitter is idle, writes
//              the first byte; each TX interrupt ("that byte has gone")
//              writes the next, until the queue is empty
//
//  Each ring has one writer and one reader, one in an interrupt and one in
//  the task, so the indices need no lock -- only the moment send() decides
//  whether to start the transmitter, which is done with interrupts masked.
//
//  A UART has no notion of a peer, so connected() is always true: the radio
//  on the end of it is either receiving or not, and the spacecraft cannot
//  tell. That is the physical truth the TCP link's "connected" only imitates.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

#include "hal/link.hpp"

namespace fsw::platform {

class CmsdkUartLink final : public hal::ILink {
 public:
    static constexpr size_t kRxBytes = 4096;
    static constexpr size_t kTxBytes = 16384;
    static_assert((kRxBytes & (kRxBytes - 1)) == 0 && (kTxBytes & (kTxBytes - 1)) == 0,
                  "free-running 32-bit indices need power-of-two buffers");

    // `base` is the UART's register block; `rx_irq` and `tx_irq` its two
    // interrupt lines on the NVIC. `bauddiv` divides the peripheral clock.
    CmsdkUartLink(uintptr_t base, uint32_t rx_irq, uint32_t tx_irq, uint32_t bauddiv)
        : base_(base), rx_irq_(rx_irq), tx_irq_(tx_irq), bauddiv_(bauddiv) {}

    // Program the UART and enable its interrupts.
    void open();

    void         poll() override {}
    bool         connected() const override { return true; }
    core::Status send(const uint8_t* data, size_t length) override;
    size_t       receive(uint8_t* dst, size_t max_length) override;
    void         disconnect() override {}

    // Called from the board's interrupt vectors.
    void on_rx_interrupt();
    void on_tx_interrupt();

    uint32_t rx_overflows() const { return rx_overflows_; }
    uint32_t tx_dropped() const { return tx_dropped_; }

 private:
    uintptr_t base_;
    uint32_t  rx_irq_;
    uint32_t  tx_irq_;
    uint32_t  bauddiv_;

    // Free-running indices; the slot is index % size. Written by one side
    // only, read by the other.
    uint8_t           rx_[kRxBytes]{};
    volatile uint32_t rx_head_ = 0;   // written by the RX interrupt
    volatile uint32_t rx_tail_ = 0;   // written by receive()

    uint8_t           tx_[kTxBytes]{};
    volatile uint32_t tx_head_ = 0;   // written by send()
    volatile uint32_t tx_tail_ = 0;   // written by the TX interrupt
    volatile bool     tx_busy_ = false;

    volatile uint32_t rx_overflows_ = 0;
    uint32_t          tx_dropped_   = 0;
};

}  // namespace fsw::platform
