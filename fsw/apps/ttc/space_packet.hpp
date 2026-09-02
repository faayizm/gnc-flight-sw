// ============================================================================
//  fsw/apps/ttc/space_packet.hpp -- CCSDS 133.0-B Space Packet Protocol.
//
//  The Space Packet is the universal container of spaceflight. Six bytes of
//  primary header, then a data field. Everything else -- PUS services, file
//  transfer, payload data -- rides inside one.
//
//      byte 0        byte 1        byte 2        byte 3
//     +-------------+-------------+-------------+-------------+
//     |VVV T S AAAAA|AAAAAAAA     |FF SSSSSS    |SSSSSSSS     |
//     +-------------+-------------+-------------+-------------+
//      VVV  packet version number, always 000
//      T    packet type: 0 = telemetry (down), 1 = telecommand (up)
//      S    secondary header flag, 1 for every PUS packet
//      A..  APID, 11 bits, identifies the application process
//      FF   sequence flags, 11 = unsegmented (the only value used here)
//      S..  packet sequence count, 14 bits, wraps at 16384
//
//      bytes 4-5: packet data length, MINUS ONE
//
//  The "minus one" is the classic trap. The field holds (octets in the data
//  field - 1), so a data field of 1 octet encodes as 0, and a packet can never
//  have an empty data field. Total packet size is therefore
//  6 + (length_field + 1). Every off-by-one in a ground system starts here,
//  which is why the arithmetic exists in exactly one place: kHeaderBytes and
//  total_size() below.
//
//  The sequence count is per-APID and wraps. It is the ground's only means of
//  detecting a lost packet, so it must increment on every packet emitted by an
//  application process and must not be reset except at boot.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/bytes.hpp"

namespace fsw::ttc {

inline constexpr size_t   kSpacePacketHeaderBytes = 6;
inline constexpr uint16_t kApidMask               = 0x07FF;   // 11 bits
inline constexpr uint16_t kSeqCountMask           = 0x3FFF;   // 14 bits
inline constexpr uint8_t  kSeqFlagsUnsegmented    = 0x3;
inline constexpr size_t   kMaxPacketBytes         = 1024;     // our own bound

enum class PacketType : uint8_t {
    Telemetry   = 0,
    Telecommand = 1,
};

struct SpacePacketHeader {
    uint8_t    version        = 0;
    PacketType type           = PacketType::Telemetry;
    bool       secondary_hdr  = true;
    uint16_t   apid           = 0;
    uint8_t    sequence_flags = kSeqFlagsUnsegmented;
    uint16_t   sequence_count = 0;
    uint16_t   data_length    = 0;   // as transmitted: octets in data field, minus one

    // Octets in the data field, i.e. everything after the primary header.
    uint16_t data_field_bytes() const {
        return static_cast<uint16_t>(data_length + 1);
    }

    // Total octets on the wire, header included.
    size_t total_size() const {
        return kSpacePacketHeaderBytes + data_field_bytes();
    }

    // Set the length field from a data field size in octets. Rejects zero,
    // which the encoding cannot represent.
    bool set_data_field_bytes(size_t octets) {
        if (octets == 0 || octets > 65536) { return false; }
        data_length = static_cast<uint16_t>(octets - 1);
        return true;
    }

    bool encode(core::ByteWriter& w) const;
    bool decode(core::ByteReader& r);
};

// Per-APID sequence counter. One instance per application process that emits
// packets; sharing a counter between APIDs would make gap detection useless.
class SequenceCounter {
 public:
    uint16_t next() {
        const uint16_t current = count_;
        count_ = static_cast<uint16_t>((count_ + 1) & kSeqCountMask);
        return current;
    }
    uint16_t peek() const { return count_; }

 private:
    uint16_t count_ = 0;
};

}  // namespace fsw::ttc
