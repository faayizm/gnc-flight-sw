// ============================================================================
//  fsw/core/crc.hpp -- CCSDS CRC-16 packet error control.
//
//  CCSDS 133.0-B and ECSS-E-ST-70-41C both specify the same check: the CRC-16
//  known elsewhere as CRC-16/CCITT-FALSE.
//
//      polynomial   x^16 + x^12 + x^5 + 1   (0x1021)
//      seed         0xFFFF
//      reflection   none, on input or output
//      final XOR    none
//
//  It is computed over every octet of the packet that precedes the two-byte
//  packet error control field, and appended big-endian. A receiver may either
//  recompute over the body and compare, or run the CRC over the whole packet
//  including the trailing CRC and check the result is zero -- that identity is
//  a property of this construction and both spellings appear below.
//
//  Bitwise rather than table-driven: 256 entries of ROM is real money on a
//  flight processor, and at the packet rates involved this costs nothing.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

namespace fsw::core {

inline constexpr uint16_t kCrcPolynomial = 0x1021;
inline constexpr uint16_t kCrcSeed       = 0xFFFF;

// Fold one octet into a running CRC. Exposed so a caller can checksum a packet
// that is being assembled in pieces without first joining it in a buffer.
constexpr uint16_t crc16_update(uint16_t crc, uint8_t byte) {
    crc ^= static_cast<uint16_t>(static_cast<uint16_t>(byte) << 8);
    for (int bit = 0; bit < 8; ++bit) {
        if (crc & 0x8000u) {
            crc = static_cast<uint16_t>((crc << 1) ^ kCrcPolynomial);
        } else {
            crc = static_cast<uint16_t>(crc << 1);
        }
    }
    return crc;
}

// CRC over a contiguous block, starting from the standard seed.
constexpr uint16_t crc16(const uint8_t* data, size_t length,
                         uint16_t seed = kCrcSeed) {
    uint16_t crc = seed;
    for (size_t i = 0; i < length; ++i) {
        crc = crc16_update(crc, data[i]);
    }
    return crc;
}

// Verify a received packet whose last two octets are the CRC. Running the CRC
// across the body *and* its own check field yields zero when intact, which
// avoids having to slice the buffer.
constexpr bool crc16_check(const uint8_t* packet, size_t length) {
    return length >= 2 && crc16(packet, length) == 0;
}

}  // namespace fsw::core
