// ============================================================================
//  fsw/apps/ttc/pus.hpp -- ECSS-E-ST-70-41C, the Packet Utilisation Standard.
//
//  CCSDS says how to wrap bytes in a packet. PUS says what those bytes MEAN.
//  It is the European standard for the application layer of a spacecraft, and
//  its value is that a ground system which speaks PUS can operate a spacecraft
//  it has never seen before: housekeeping, events, parameters, memory dumps
//  and time-tagged commanding all work the same way on every mission.
//
//  Services implemented in this phase:
//
//    ST[01]  Request verification -- did my telecommand arrive, and did it
//            work? Four reports: acceptance ok/failed, completion ok/failed.
//            Without this the ground is commanding blind.
//    ST[03]  Housekeeping -- periodic parameter reports, the bulk of routine
//            telemetry.
//    ST[05]  Event reporting -- "something happened", with a severity.
//    ST[17]  Test -- a connection check that proves the whole chain works
//            end to end without changing any spacecraft state.
//    ST[20]  Parameter management -- read and write on-board parameters.
//
//  Later phases add ST[11] time-based scheduling, ST[12] on-board monitoring,
//  ST[15] storage and retrieval, ST[08] function management.
//
//  SECONDARY HEADERS
//
//    TM (13 octets): 1 + 1 + 1 + 2 + 2 + 6
//      PUS version (4 bits, = 2) | time reference status (4 bits)   1 octet
//      service type                                                 1 octet
//      message subtype                                              1 octet
//      message type counter                                         2 octets
//      destination id                                               2 octets
//      time, CUC 4.2                                                6 octets
//
//    TC (5 octets):
//      PUS version (4 bits, = 2) | acknowledgement flags (4 bits)
//      service type (1) | message subtype (1) | source id (2)
//
//  The acknowledgement flags let the ground ask for exactly the verification
//  reports it wants, per command: bit 0 acceptance, bit 1 start, bit 2
//  progress, bit 3 completion. Honouring them rather than always reporting is
//  what keeps the downlink budget under control during a busy pass.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

#include "apps/ttc/space_packet.hpp"
#include "core/bytes.hpp"
#include "core/crc.hpp"
#include "core/status.hpp"
#include "core/time.hpp"

namespace fsw::ttc {

inline constexpr uint8_t kPusVersion       = 2;   // PUS-C
inline constexpr size_t  kPusTmHeaderBytes = 13;
inline constexpr size_t  kPusTcHeaderBytes = 5;
inline constexpr size_t  kCrcBytes         = 2;

// Acknowledgement flags in the TC secondary header.
enum AckFlags : uint8_t {
    kAckNone       = 0x0,
    kAckAcceptance = 0x1,
    kAckStart      = 0x2,
    kAckProgress   = 0x4,
    kAckCompletion = 0x8,
};

// PUS service numbers used in this build.
enum class Service : uint8_t {
    Verification = 1,
    Housekeeping = 3,
    Event        = 5,
    Function     = 8,
    Test         = 17,
    Parameter    = 20,
};

struct PusTmHeader {
    uint8_t        version      = kPusVersion;
    uint8_t        time_status  = 0;   // 0 = time not yet correlated with ground
    uint8_t        service      = 0;
    uint8_t        subtype      = 0;
    uint16_t       message_count = 0;  // per (APID, service, subtype)
    uint16_t       destination  = 0;
    core::CucTime  time{};

    bool encode(core::ByteWriter& w) const;
    bool decode(core::ByteReader& r);
};

struct PusTcHeader {
    uint8_t  version   = kPusVersion;
    uint8_t  ack_flags = kAckAcceptance | kAckCompletion;
    uint8_t  service   = 0;
    uint8_t  subtype   = 0;
    uint16_t source_id = 0;

    bool encode(core::ByteWriter& w) const;
    bool decode(core::ByteReader& r);

    bool wants(AckFlags flag) const { return (ack_flags & flag) != 0; }
};

// ---------------------------------------------------------------------------
// A telecommand as received and validated. Holds a borrowed view of the
// argument bytes; valid only while the receive buffer that produced it lives.
// ---------------------------------------------------------------------------
struct ReceivedTc {
    SpacePacketHeader primary;
    PusTcHeader       secondary;
    const uint8_t*    args      = nullptr;
    size_t            args_size = 0;
};

// Parse and fully validate an uplinked telecommand. Every rejection reason
// maps onto the failure code the ground will be told in ST[1,2], so there is
// no way to reject a command without being able to say why.
core::Status parse_tc(const uint8_t* data, size_t length,
                      ReceivedTc& out, core::FailureCode& failure);

// ---------------------------------------------------------------------------
// TmBuilder -- assembles one downlink packet into a caller-owned buffer.
//
// Usage is always the same three steps: begin, write the source data, finish.
// finish() back-patches the CCSDS length field and appends the CRC, so no
// caller ever has to compute either -- which is precisely why those two
// perennial sources of bugs do not appear anywhere else in this codebase.
// ---------------------------------------------------------------------------
class TmBuilder {
 public:
    TmBuilder(uint8_t* buffer, size_t capacity) : writer_(buffer, capacity) {}

    // Start a packet. Reserves space for both headers; the length field is
    // written provisionally and corrected in finish().
    bool begin(uint16_t apid, uint16_t sequence_count, Service service,
               uint8_t subtype, uint16_t message_count, const core::CucTime& time);

    core::ByteWriter& payload() { return writer_; }

    // Complete the packet. Returns the total size on the wire, or 0 on
    // overflow. After this call the buffer holds a valid, CRC-protected packet.
    size_t finish();

    const uint8_t* data() const { return writer_.data(); }

 private:
    core::ByteWriter writer_;
    uint8_t*         length_field_ = nullptr;  // where to back-patch
    bool             started_      = false;
};

}  // namespace fsw::ttc
