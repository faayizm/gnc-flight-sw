// ============================================================================
//  fsw/apps/ttc/channel_coding.hpp -- what makes a byte stream survive a radio.
//
//  A transfer frame on its own is fragile: one flipped bit and its CRC fails,
//  one lost byte and the receiver no longer knows where the next frame
//  starts. The coding layer fixes both, in three independent pieces:
//
//    REED-SOLOMON (CCSDS 131.0-B, downlink). Each 223-byte TM frame gets 32
//    parity bytes; the ground can correct any 16 corrupted bytes in the 255.
//    Parameters: GF(256) with field polynomial x^8+x^7+x^2+x+1 (0x187),
//    generator roots alpha^(11 j) for j = 112..143, and symbols carried in
//    Berlekamp's dual basis. The dual basis is the detail implementations get
//    wrong; the encoder here is checked byte for byte against vectors from
//    Phil Karn's libfec, the reference most ground stations descend from.
//
//    PSEUDO-RANDOMISER (CCSDS 131.0-B, downlink). XORs the coded frame with a
//    fixed 255-byte sequence from h(x) = x^8+x^7+x^5+x^3+1. A frame of zeros
//    would otherwise be a long run of one symbol, and the ground receiver's
//    bit synchroniser needs transitions to stay locked.
//
//    BCH(63,56) (CCSDS 231.0-B, uplink). Every 7 bytes of a telecommand frame
//    carry 1 byte of parity. A single bit error per 8-byte codeblock is
//    corrected; anything worse ends the CLTU and the frame is discarded --
//    COP-1 then retransmits it.
//
//  The attached sync marker (0x1ACFFC1D before every downlink codeblock, and
//  the 0xEB90 start sequence before every uplink CLTU) is what lets a
//  receiver find a frame boundary in a stream it joined at an arbitrary point.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

namespace fsw::ttc::coding {

// ---- Reed-Solomon (255,223), CCSDS dual basis ------------------------------
constexpr size_t kRsN       = 255;
constexpr size_t kRsK       = 223;
constexpr size_t kRsParity  = 32;

// Compute the 32 parity bytes for 223 data bytes (both in dual basis).
void rs_encode(const uint8_t* data, uint8_t* parity);

// ---- Pseudo-randomiser -----------------------------------------------------
// XOR `n` bytes in place with the CCSDS sequence, starting from its first byte.
void randomise(uint8_t* data, size_t n);

// ---- Attached sync marker ---------------------------------------------------
constexpr uint8_t kAsm[4] = {0x1A, 0xCF, 0xFC, 0x1D};
constexpr size_t  kCaduBytes = 4 + kRsN;

// ---- BCH(63,56) codeblocks for CLTUs ---------------------------------------
constexpr uint8_t kCltuStart[2] = {0xEB, 0x90};
constexpr uint8_t kCltuTail[8]  = {0xC5, 0xC5, 0xC5, 0xC5, 0xC5, 0xC5, 0xC5, 0x79};
constexpr size_t  kCodeblockInfo = 7;
constexpr size_t  kCodeblockBytes = 8;

// Parity byte for 7 information bytes: 7 complemented BCH parity bits,
// followed by a filler bit of zero.
uint8_t bch_parity(const uint8_t* info7);

enum class BchResult { Ok, Corrected, Uncorrectable };

// Check (and if possible correct, in place) one 8-byte codeblock.
BchResult bch_decode(uint8_t* codeblock8);

}  // namespace fsw::ttc::coding
