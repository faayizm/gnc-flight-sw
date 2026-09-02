// ============================================================================
//  fsw/apps/ttc/space_packet.cpp -- bit packing for the CCSDS primary header.
//  See space_packet.hpp for the field layout and the "minus one" trap.
// ============================================================================
#include "apps/ttc/space_packet.hpp"

namespace fsw::ttc {

bool SpacePacketHeader::encode(core::ByteWriter& w) const {
    // First 16 bits: version (3) | type (1) | secondary header flag (1) | APID (11)
    const uint16_t word0 =
        static_cast<uint16_t>((static_cast<uint16_t>(version & 0x7) << 13) |
                              (static_cast<uint16_t>(type) << 12) |
                              (static_cast<uint16_t>(secondary_hdr ? 1 : 0) << 11) |
                              (apid & kApidMask));

    // Next 16 bits: sequence flags (2) | sequence count (14)
    const uint16_t word1 =
        static_cast<uint16_t>((static_cast<uint16_t>(sequence_flags & 0x3) << 14) |
                              (sequence_count & kSeqCountMask));

    return w.write_uint16(word0) &&
           w.write_uint16(word1) &&
           w.write_uint16(data_length);
}

bool SpacePacketHeader::decode(core::ByteReader& r) {
    uint16_t word0 = 0;
    uint16_t word1 = 0;
    if (!r.read_uint16(word0) || !r.read_uint16(word1) || !r.read_uint16(data_length)) {
        return false;
    }

    version        = static_cast<uint8_t>((word0 >> 13) & 0x7);
    type           = static_cast<PacketType>((word0 >> 12) & 0x1);
    secondary_hdr  = ((word0 >> 11) & 0x1) != 0;
    apid           = static_cast<uint16_t>(word0 & kApidMask);
    sequence_flags = static_cast<uint8_t>((word1 >> 14) & 0x3);
    sequence_count = static_cast<uint16_t>(word1 & kSeqCountMask);
    return true;
}

}  // namespace fsw::ttc
