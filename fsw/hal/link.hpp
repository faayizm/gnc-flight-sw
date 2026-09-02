// ============================================================================
//  fsw/hal/link.hpp -- the communications port.
//
//  An ILink is a bidirectional, unreliable, packet-oriented byte pipe. It is
//  deliberately the thinnest possible abstraction, because the things it will
//  stand in for are wildly different: a TCP socket in the software-in-the-loop
//  build, a UART to a radio on a CubeSat, a SpaceWire link on a larger bus.
//
//  Two rules that matter more than the interface itself:
//
//    NON-BLOCKING. receive() returns immediately with whatever is there, or
//    zero. A control loop that blocks on a radio is a control loop that stops
//    controlling when the radio misbehaves.
//
//    NO OWNERSHIP OF FRAMING. The link moves bytes. Deciding where a packet
//    starts and ends is the transport layer's job, in apps/ttc. Mixing the two
//    is what makes flight software impossible to port.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/status.hpp"

namespace fsw::hal {

class ILink {
 public:
    virtual ~ILink() = default;

    // Pump the underlying transport: accept connections, drain socket buffers,
    // service DMA. Called once per scheduler tick from the highest rate group.
    virtual void poll() = 0;

    // True when a peer is attached and traffic can flow.
    virtual bool connected() const = 0;

    // Queue bytes for transmission. Returns NoSpace rather than blocking when
    // the outbound buffer is full -- the caller decides what to drop.
    virtual core::Status send(const uint8_t* data, size_t length) = 0;

    // Copy up to max_length received bytes into dst. Returns how many were
    // taken, which may be zero. Never blocks.
    virtual size_t receive(uint8_t* dst, size_t max_length) = 0;

    // Drop the peer. Used by fault injection and by autonomy on link timeout.
    virtual void disconnect() = 0;
};

}  // namespace fsw::hal
