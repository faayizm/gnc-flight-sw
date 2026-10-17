// ============================================================================
//  fsw/platform/freertos/cmsdk_uart_link.cpp
// ============================================================================
#include "platform/freertos/cmsdk_uart_link.hpp"

#include "platform/freertos/cortex_m.hpp"

namespace fsw::platform {

namespace {

// CMSDK APB UART register map.
constexpr uintptr_t kData     = 0x00;
constexpr uintptr_t kState    = 0x04;
constexpr uintptr_t kCtrl     = 0x08;
constexpr uintptr_t kIntClear = 0x0C;
constexpr uintptr_t kBaudDiv  = 0x10;

constexpr uint32_t kStateTxFull = 1u << 0;
constexpr uint32_t kStateRxFull = 1u << 1;
constexpr uint32_t kCtrlTxEn    = 1u << 0;
constexpr uint32_t kCtrlRxEn    = 1u << 1;
constexpr uint32_t kCtrlTxInt   = 1u << 2;
constexpr uint32_t kCtrlRxInt   = 1u << 3;
constexpr uint32_t kIntTx       = 1u << 0;
constexpr uint32_t kIntRx       = 1u << 1;

}  // namespace

void CmsdkUartLink::open() {
    cm::reg<uint32_t>(base_ + kBaudDiv) = bauddiv_;
    cm::reg<uint32_t>(base_ + kCtrl) = kCtrlTxEn | kCtrlRxEn | kCtrlTxInt | kCtrlRxInt;
    // Neither handler calls the RTOS, so they may sit above FreeRTOS's
    // critical sections and are never held off by them.
    cm::nvic_set_priority(rx_irq_, 0);
    cm::nvic_set_priority(tx_irq_, 0);
    cm::nvic_enable(rx_irq_);
    cm::nvic_enable(tx_irq_);
}

void CmsdkUartLink::on_rx_interrupt() {
    cm::reg<uint32_t>(base_ + kIntClear) = kIntRx;
    while ((cm::reg<uint32_t>(base_ + kState) & kStateRxFull) != 0) {
        const auto byte = static_cast<uint8_t>(cm::reg<uint32_t>(base_ + kData));
        const uint32_t head = rx_head_;
        if (head - rx_tail_ >= kRxBytes) {
            // The task has fallen a whole buffer behind. Drop the byte and
            // count it: the framing above resynchronises on the next marker.
            rx_overflows_ = rx_overflows_ + 1;
            continue;
        }
        rx_[head % kRxBytes] = byte;
        cm::dmb();
        rx_head_ = head + 1;
    }
}

size_t CmsdkUartLink::receive(uint8_t* dst, size_t max_length) {
    size_t n = 0;
    uint32_t tail = rx_tail_;
    const uint32_t head = rx_head_;
    cm::dmb();
    while (n < max_length && tail != head) {
        dst[n++] = rx_[tail % kRxBytes];
        ++tail;
    }
    cm::dmb();
    rx_tail_ = tail;
    return n;
}

core::Status CmsdkUartLink::send(const uint8_t* data, size_t length) {
    const uint32_t head = tx_head_;
    if (length > kTxBytes - (head - tx_tail_)) {
        ++tx_dropped_;
        return core::Status::NoSpace;
    }
    for (size_t i = 0; i < length; ++i) { tx_[(head + i) % kTxBytes] = data[i]; }
    cm::dmb();
    tx_head_ = head + static_cast<uint32_t>(length);

    // Start the transmitter if it is idle; once started, it keeps itself
    // going from the TX interrupt until the queue is empty.
    const uint32_t saved = cm::irq_save();
    if (!tx_busy_) {
        tx_busy_ = true;
        const uint32_t tail = tx_tail_;
        cm::reg<uint32_t>(base_ + kData) = tx_[tail % kTxBytes];
        tx_tail_ = tail + 1;
    }
    cm::irq_restore(saved);
    return core::Status::Ok;
}

void CmsdkUartLink::on_tx_interrupt() {
    cm::reg<uint32_t>(base_ + kIntClear) = kIntTx;
    if ((cm::reg<uint32_t>(base_ + kState) & kStateTxFull) != 0) { return; }
    const uint32_t tail = tx_tail_;
    if (tail == tx_head_) {
        tx_busy_ = false;
        return;
    }
    cm::reg<uint32_t>(base_ + kData) = tx_[tail % kTxBytes];
    tx_tail_ = tail + 1;
}

}  // namespace fsw::platform
