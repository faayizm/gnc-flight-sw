// ============================================================================
//  Tests for the CCSDS CRC-16.
//
//  The check value below is the one every implementation of CRC-16/CCITT-FALSE
//  agrees on: the CRC of the nine ASCII characters "123456789" is 0x29B1. If
//  this test passes, this implementation will interoperate with any ground
//  system, which is the only thing that actually matters about a CRC.
// ============================================================================
#include "core/crc.hpp"
#include "framework.hpp"

using fsw::core::crc16;
using fsw::core::crc16_check;

TEST(crc, matches_the_standard_check_value) {
    const uint8_t data[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    CHECK_EQ(crc16(data, sizeof data), static_cast<uint16_t>(0x29B1));
}

TEST(crc, empty_input_returns_the_seed) {
    CHECK_EQ(crc16(nullptr, 0), static_cast<uint16_t>(0xFFFF));
}

TEST(crc, appending_the_crc_makes_the_whole_block_check_to_zero) {
    // This identity is what lets a receiver verify a packet without slicing
    // the check field off first, and it is used by parse_tc().
    uint8_t packet[8] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02};
    const uint16_t crc = crc16(packet, 6);
    packet[6] = static_cast<uint8_t>(crc >> 8);
    packet[7] = static_cast<uint8_t>(crc & 0xFF);

    CHECK(crc16_check(packet, sizeof packet));
    CHECK_EQ(crc16(packet, sizeof packet), static_cast<uint16_t>(0));
}

TEST(crc, detects_a_single_flipped_bit) {
    uint8_t packet[8] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
    const uint16_t crc = crc16(packet, 6);
    packet[6] = static_cast<uint8_t>(crc >> 8);
    packet[7] = static_cast<uint8_t>(crc & 0xFF);
    CHECK(crc16_check(packet, sizeof packet));

    packet[3] ^= 0x01;   // one bit, the hardest case to catch
    CHECK(!crc16_check(packet, sizeof packet));
}

TEST(crc, detects_corruption_of_the_check_field_itself) {
    uint8_t packet[8] = {1, 2, 3, 4, 5, 6};
    const uint16_t crc = crc16(packet, 6);
    packet[6] = static_cast<uint8_t>(crc >> 8);
    packet[7] = static_cast<uint8_t>(crc & 0xFF);

    packet[7] ^= 0x80;
    CHECK(!crc16_check(packet, sizeof packet));
}

TEST(crc, incremental_update_matches_a_single_pass) {
    const uint8_t data[] = {0x00, 0xFF, 0x55, 0xAA, 0x12};
    uint16_t incremental = fsw::core::kCrcSeed;
    for (uint8_t b : data) {
        incremental = fsw::core::crc16_update(incremental, b);
    }
    CHECK_EQ(incremental, crc16(data, sizeof data));
}

TEST(crc, a_block_shorter_than_the_check_field_cannot_be_valid) {
    const uint8_t one[1] = {0x00};
    CHECK(!crc16_check(one, 1));
}
