// ============================================================================
//  Tests for the PUS layer: secondary headers, TC validation, TM assembly.
//
//  The validation tests matter most. Everything that arrives at a spacecraft's
//  uplink is untrusted input, and parse_tc() is the only thing standing between
//  a corrupted radio frame and the command dispatcher.
// ============================================================================
#include "apps/ttc/pus.hpp"
#include "framework.hpp"

using namespace fsw::ttc;
using fsw::core::ByteReader;
using fsw::core::ByteWriter;
using fsw::core::FailureCode;
using fsw::core::crc16;

namespace {

// Build a syntactically valid telecommand. Individual tests then damage one
// specific thing, which keeps each test about exactly one failure mode.
size_t build_tc(uint8_t* buf, size_t cap, uint8_t service, uint8_t subtype,
                const uint8_t* args, size_t args_len,
                uint16_t apid = 0x00A, uint16_t seq = 7,
                uint8_t ack = kAckAcceptance | kAckCompletion) {
    ByteWriter w(buf, cap);

    const size_t data_field = kPusTcHeaderBytes + args_len + kCrcBytes;

    SpacePacketHeader primary;
    primary.type           = PacketType::Telecommand;
    primary.secondary_hdr  = true;
    primary.apid           = apid;
    primary.sequence_count = seq;
    primary.set_data_field_bytes(data_field);
    primary.encode(w);

    PusTcHeader secondary;
    secondary.ack_flags = ack;
    secondary.service   = service;
    secondary.subtype   = subtype;
    secondary.source_id = 0x1234;
    secondary.encode(w);

    if (args_len > 0) { w.write_bytes(args, args_len); }

    w.write_uint16(crc16(w.data(), w.size()));
    return w.ok() ? w.size() : 0;
}

}  // namespace

TEST(pus, tc_secondary_header_packs_version_and_ack_into_one_octet) {
    uint8_t buf[16]{};
    ByteWriter w(buf, sizeof buf);
    PusTcHeader h;
    h.ack_flags = kAckAcceptance | kAckCompletion;   // 0x9
    h.service   = 17;
    h.subtype   = 1;
    h.source_id = 0xBEEF;
    CHECK(h.encode(w));
    CHECK_EQ(w.size(), kPusTcHeaderBytes);
    CHECK_EQ(buf[0], static_cast<uint8_t>(0x29));    // version 2, ack 9
    CHECK_EQ(buf[1], static_cast<uint8_t>(17));
    CHECK_EQ(buf[2], static_cast<uint8_t>(1));
}

TEST(pus, tm_secondary_header_is_thirteen_octets_and_round_trips) {
    uint8_t buf[32]{};
    ByteWriter w(buf, sizeof buf);
    PusTmHeader out_h;
    out_h.service       = 3;
    out_h.subtype       = 25;
    out_h.message_count = 4242;
    out_h.destination   = 0x0001;
    out_h.time.coarse   = 800000000u;
    out_h.time.fine     = 32768;   // exactly half a second
    CHECK(out_h.encode(w));
    CHECK_EQ(w.size(), kPusTmHeaderBytes);

    ByteReader r(buf, w.size());
    PusTmHeader in_h;
    CHECK(in_h.decode(r));
    CHECK_EQ(in_h.version, static_cast<uint8_t>(kPusVersion));
    CHECK_EQ(in_h.service, static_cast<uint8_t>(3));
    CHECK_EQ(in_h.subtype, static_cast<uint8_t>(25));
    CHECK_EQ(in_h.message_count, static_cast<uint16_t>(4242));
    CHECK_EQ(in_h.time.coarse, 800000000u);
    CHECK_EQ(in_h.time.fine, static_cast<uint16_t>(32768));
    CHECK_NEAR(in_h.time.to_seconds(), 800000000.5, 1e-6);
}

TEST(pus, ack_flags_are_read_back_individually) {
    PusTcHeader h;
    h.ack_flags = kAckAcceptance | kAckCompletion;
    CHECK(h.wants(kAckAcceptance));
    CHECK(h.wants(kAckCompletion));
    CHECK(!h.wants(kAckStart));
    CHECK(!h.wants(kAckProgress));
}

TEST(pus, a_well_formed_telecommand_is_accepted) {
    uint8_t buf[64]{};
    const uint8_t args[2] = {0x00, 0x05};
    const size_t len = build_tc(buf, sizeof buf, 20, 1, args, sizeof args);
    CHECK(len > 0);

    ReceivedTc tc;
    FailureCode failure = FailureCode::BadCrc;
    CHECK(fsw::core::is_ok(parse_tc(buf, len, tc, failure)));
    CHECK(failure == FailureCode::Ok);
    CHECK_EQ(tc.secondary.service, static_cast<uint8_t>(20));
    CHECK_EQ(tc.secondary.subtype, static_cast<uint8_t>(1));
    CHECK_EQ(tc.args_size, static_cast<size_t>(2));
    CHECK_EQ(tc.args[1], static_cast<uint8_t>(0x05));
    CHECK_EQ(tc.primary.sequence_count, static_cast<uint16_t>(7));
}

TEST(pus, a_command_with_no_arguments_is_accepted) {
    uint8_t buf[64]{};
    const size_t len = build_tc(buf, sizeof buf, 17, 1, nullptr, 0);
    CHECK(len > 0);

    ReceivedTc tc;
    FailureCode failure = FailureCode::BadCrc;
    CHECK(fsw::core::is_ok(parse_tc(buf, len, tc, failure)));
    CHECK_EQ(tc.args_size, static_cast<size_t>(0));
    CHECK(tc.args == nullptr);
}

TEST(pus, a_corrupted_telecommand_is_rejected_for_crc_before_anything_else) {
    uint8_t buf[64]{};
    const uint8_t args[2] = {0x00, 0x05};
    const size_t len = build_tc(buf, sizeof buf, 20, 1, args, sizeof args);

    buf[8] ^= 0x01;   // flip a bit inside the secondary header

    ReceivedTc tc;
    FailureCode failure = FailureCode::Ok;
    CHECK(!fsw::core::is_ok(parse_tc(buf, len, tc, failure)));
    // Specifically BAD_CRC, not BAD_LENGTH or UNKNOWN_SERVICE: the fields were
    // never interpreted, because they could not be trusted.
    CHECK(failure == FailureCode::BadCrc);
}

TEST(pus, a_truncated_telecommand_is_rejected) {
    uint8_t buf[64]{};
    const size_t len = build_tc(buf, sizeof buf, 17, 1, nullptr, 0);

    ReceivedTc tc;
    FailureCode failure = FailureCode::Ok;
    CHECK(!fsw::core::is_ok(parse_tc(buf, len - 1, tc, failure)));
    CHECK(failure != FailureCode::Ok);
}

TEST(pus, a_declared_length_that_disagrees_with_reality_is_rejected) {
    uint8_t buf[64]{};
    const uint8_t args[2] = {1, 2};
    size_t len = build_tc(buf, sizeof buf, 20, 1, args, sizeof args);

    // Claim one octet more than arrived, then repair the CRC so that the ONLY
    // remaining fault is the length disagreement.
    buf[5] = static_cast<uint8_t>(buf[5] + 1);
    const uint16_t crc = crc16(buf, len - 2);
    buf[len - 2] = static_cast<uint8_t>(crc >> 8);
    buf[len - 1] = static_cast<uint8_t>(crc & 0xFF);

    ReceivedTc tc;
    FailureCode failure = FailureCode::Ok;
    CHECK(!fsw::core::is_ok(parse_tc(buf, len, tc, failure)));
    CHECK(failure == FailureCode::BadLength);
}

TEST(pus, a_telemetry_packet_arriving_on_the_uplink_is_rejected) {
    uint8_t buf[64]{};
    size_t len = build_tc(buf, sizeof buf, 17, 1, nullptr, 0);

    buf[0] = static_cast<uint8_t>(buf[0] & ~0x10);   // clear the type bit
    const uint16_t crc = crc16(buf, len - 2);
    buf[len - 2] = static_cast<uint8_t>(crc >> 8);
    buf[len - 1] = static_cast<uint8_t>(crc & 0xFF);

    ReceivedTc tc;
    FailureCode failure = FailureCode::Ok;
    CHECK(!fsw::core::is_ok(parse_tc(buf, len, tc, failure)));
}

TEST(pus, a_null_or_tiny_buffer_is_rejected_without_reading_it) {
    ReceivedTc tc;
    FailureCode failure = FailureCode::Ok;
    CHECK(!fsw::core::is_ok(parse_tc(nullptr, 100, tc, failure)));
    CHECK(failure == FailureCode::BadLength);

    const uint8_t tiny[4] = {0, 0, 0, 0};
    CHECK(!fsw::core::is_ok(parse_tc(tiny, sizeof tiny, tc, failure)));
    CHECK(failure == FailureCode::BadLength);
}

TEST(pus, tm_builder_produces_a_packet_that_validates_end_to_end) {
    uint8_t buf[128]{};
    TmBuilder b(buf, sizeof buf);

    fsw::core::CucTime t;
    t.coarse = 1000;
    t.fine   = 0;
    CHECK(b.begin(0x001, 5, Service::Housekeeping, 25, 3, t));
    b.payload().write_uint8(1);          // structure id
    b.payload().write_uint32(0xCAFE);    // one field
    const size_t len = b.finish();
    CHECK(len > 0);

    // The CRC the builder appended must check out.
    CHECK(fsw::core::crc16_check(buf, len));

    // And the length field must describe exactly what was produced.
    ByteReader r(buf, len);
    SpacePacketHeader primary;
    CHECK(primary.decode(r));
    CHECK_EQ(primary.total_size(), len);
    CHECK_EQ(primary.apid, static_cast<uint16_t>(0x001));
    CHECK_EQ(primary.sequence_count, static_cast<uint16_t>(5));
    CHECK(primary.type == PacketType::Telemetry);
    CHECK(primary.secondary_hdr);

    PusTmHeader secondary;
    CHECK(secondary.decode(r));
    CHECK_EQ(secondary.service, static_cast<uint8_t>(3));
    CHECK_EQ(secondary.subtype, static_cast<uint8_t>(25));
    CHECK_EQ(secondary.message_count, static_cast<uint16_t>(3));
    CHECK_EQ(secondary.time.coarse, 1000u);
}

TEST(pus, tm_builder_refuses_to_overflow_its_buffer) {
    uint8_t small[20]{};
    TmBuilder b(small, sizeof small);
    fsw::core::CucTime t;
    CHECK(b.begin(0x001, 0, Service::Test, 2, 0, t));

    // Headers alone are 18 octets; this payload cannot fit alongside the CRC.
    for (int i = 0; i < 32; ++i) { b.payload().write_uint32(i); }
    CHECK_EQ(b.finish(), static_cast<size_t>(0));
}

TEST(pus, an_empty_test_report_is_still_a_legal_packet) {
    // ST[17,2] carries no source data. The data field is therefore just the
    // secondary header plus the CRC, which is the smallest packet this
    // spacecraft ever emits -- a good exercise of the minus-one encoding.
    uint8_t buf[64]{};
    TmBuilder b(buf, sizeof buf);
    fsw::core::CucTime t;
    CHECK(b.begin(0x001, 0, Service::Test, 2, 0, t));
    const size_t len = b.finish();

    CHECK_EQ(len, kSpacePacketHeaderBytes + kPusTmHeaderBytes + kCrcBytes);
    CHECK(fsw::core::crc16_check(buf, len));

    ByteReader r(buf, len);
    SpacePacketHeader primary;
    CHECK(primary.decode(r));
    CHECK_EQ(primary.total_size(), len);
}
