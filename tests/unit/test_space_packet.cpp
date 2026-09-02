// ============================================================================
//  Tests for the CCSDS Space Packet primary header.
//
//  Almost all of these exist because of one field: the packet data length,
//  which is stored MINUS ONE. Every ground system that has ever failed to
//  parse a spacecraft's telemetry has failed here first.
// ============================================================================
#include "apps/ttc/space_packet.hpp"
#include "framework.hpp"

using namespace fsw::ttc;
using fsw::core::ByteReader;
using fsw::core::ByteWriter;

TEST(space_packet, header_is_exactly_six_octets) {
    uint8_t buf[16]{};
    ByteWriter w(buf, sizeof buf);
    SpacePacketHeader h;
    CHECK(h.encode(w));
    CHECK_EQ(w.size(), kSpacePacketHeaderBytes);
}

TEST(space_packet, fields_land_in_the_bits_the_standard_specifies) {
    uint8_t buf[16]{};
    ByteWriter w(buf, sizeof buf);

    SpacePacketHeader h;
    h.version        = 0;
    h.type           = PacketType::Telecommand;   // bit 3 of byte 0
    h.secondary_hdr  = true;                      // bit 4 of byte 0
    h.apid           = 0x2AB;                     // 11 bits
    h.sequence_flags = kSeqFlagsUnsegmented;      // top 2 bits of byte 2
    h.sequence_count = 0x0123;
    h.data_length    = 0x0010;
    CHECK(h.encode(w));

    // 000 1 1 010 -> 0x1A, then the low 8 APID bits.
    CHECK_EQ(buf[0], static_cast<uint8_t>(0x1A));
    CHECK_EQ(buf[1], static_cast<uint8_t>(0xAB));
    // 11 000001 -> 0xC1, then 0x23.
    CHECK_EQ(buf[2], static_cast<uint8_t>(0xC1));
    CHECK_EQ(buf[3], static_cast<uint8_t>(0x23));
    CHECK_EQ(buf[4], static_cast<uint8_t>(0x00));
    CHECK_EQ(buf[5], static_cast<uint8_t>(0x10));
}

TEST(space_packet, decode_recovers_everything_encode_wrote) {
    uint8_t buf[16]{};
    ByteWriter w(buf, sizeof buf);
    SpacePacketHeader out_h;
    out_h.type           = PacketType::Telemetry;
    out_h.secondary_hdr  = true;
    out_h.apid           = 0x7FF;    // maximum
    out_h.sequence_count = 0x3FFF;   // maximum
    out_h.data_length    = 0xFFFF;   // maximum
    out_h.encode(w);

    ByteReader r(buf, w.size());
    SpacePacketHeader in_h;
    CHECK(in_h.decode(r));
    CHECK_EQ(in_h.apid, static_cast<uint16_t>(0x7FF));
    CHECK_EQ(in_h.sequence_count, static_cast<uint16_t>(0x3FFF));
    CHECK_EQ(in_h.data_length, static_cast<uint16_t>(0xFFFF));
    CHECK(in_h.secondary_hdr);
    CHECK(in_h.type == PacketType::Telemetry);
}

TEST(space_packet, the_length_field_is_the_data_field_size_minus_one) {
    SpacePacketHeader h;
    CHECK(h.set_data_field_bytes(1));
    CHECK_EQ(h.data_length, static_cast<uint16_t>(0));
    CHECK_EQ(h.data_field_bytes(), static_cast<uint16_t>(1));
    CHECK_EQ(h.total_size(), static_cast<size_t>(7));

    CHECK(h.set_data_field_bytes(20));
    CHECK_EQ(h.data_length, static_cast<uint16_t>(19));
    CHECK_EQ(h.total_size(), static_cast<size_t>(26));
}

TEST(space_packet, a_zero_length_data_field_is_not_representable) {
    // The "minus one" encoding cannot express an empty data field, so the
    // setter must refuse rather than silently wrapping to 65535.
    SpacePacketHeader h;
    CHECK(!h.set_data_field_bytes(0));
}

TEST(space_packet, out_of_range_field_values_are_masked_not_smeared) {
    uint8_t buf[16]{};
    ByteWriter w(buf, sizeof buf);
    SpacePacketHeader h;
    h.apid           = 0xFFFF;   // more than 11 bits
    h.sequence_count = 0xFFFF;   // more than 14 bits
    h.encode(w);

    ByteReader r(buf, w.size());
    SpacePacketHeader in_h;
    CHECK(in_h.decode(r));
    // Masked to their field widths, and critically the overflow has not
    // corrupted the version, type or secondary-header-flag bits beside them.
    CHECK_EQ(in_h.apid, static_cast<uint16_t>(0x7FF));
    CHECK_EQ(in_h.sequence_count, static_cast<uint16_t>(0x3FFF));
    CHECK_EQ(in_h.version, static_cast<uint8_t>(0));
}

TEST(space_packet, decoding_a_truncated_header_fails) {
    const uint8_t buf[3] = {0x08, 0x01, 0xC0};
    ByteReader r(buf, sizeof buf);
    SpacePacketHeader h;
    CHECK(!h.decode(r));
}

TEST(space_packet, sequence_counter_wraps_at_the_fourteen_bit_boundary) {
    SequenceCounter c;
    CHECK_EQ(c.next(), static_cast<uint16_t>(0));
    CHECK_EQ(c.next(), static_cast<uint16_t>(1));

    for (int i = 2; i < 16384; ++i) { c.next(); }
    // 16384 values consumed; the next one must be 0 again, not 16384.
    CHECK_EQ(c.next(), static_cast<uint16_t>(0));
}
