// ============================================================================
//  fsw/platform/posix/tcp_server_link.hpp -- ILink over a TCP socket.
//
//  The flight software LISTENS and the ground connects to it. That is the
//  reverse of the physical situation, and it is deliberate: the spacecraft is
//  the long-lived process, the ground tool comes and goes, and a ground system
//  that can reconnect at will is far easier to work with than one that has to
//  be running before the spacecraft boots.
//
//  Everything here is non-blocking. A blocking accept() or recv() anywhere in
//  this class would stall the scheduler, and the first thing that would break
//  is the control loop -- exactly the failure mode the rate-group design
//  exists to prevent.
//
//  One peer at a time. A second connection attempt while one is open is
//  refused, because two ground systems commanding the same spacecraft
//  simultaneously is not a situation worth supporting quietly.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

#include "hal/link.hpp"

namespace fsw::platform {

class TcpServerLink final : public hal::ILink {
 public:
    static constexpr size_t kTxQueueBytes = 65536;

    explicit TcpServerLink(uint16_t port);
    ~TcpServerLink() override;

    // Bind and listen. Returns IoError if the port cannot be taken.
    core::Status open();

    void         poll() override;
    bool         connected() const override;
    core::Status send(const uint8_t* data, size_t length) override;
    size_t       receive(uint8_t* dst, size_t max_length) override;
    void         disconnect() override;

    uint16_t port() const { return port_; }

    // Counts a caller can report in housekeeping.
    uint32_t tx_dropped() const { return tx_dropped_; }
    uint64_t bytes_sent() const { return bytes_sent_; }
    uint64_t bytes_received() const { return bytes_received_; }

 private:
    void accept_pending();
    void flush_tx();
    void close_peer();

    uint16_t port_;
    int      listen_fd_ = -1;
    int      peer_fd_   = -1;

    // Outbound bytes that the socket would not take yet. Bounded: when it
    // fills, new sends are refused with NoSpace and counted, rather than the
    // buffer growing without limit until the process dies.
    uint8_t  tx_queue_[kTxQueueBytes]{};
    size_t   tx_head_ = 0;
    size_t   tx_size_ = 0;

    uint32_t tx_dropped_     = 0;
    uint64_t bytes_sent_     = 0;
    uint64_t bytes_received_ = 0;
};

}  // namespace fsw::platform
