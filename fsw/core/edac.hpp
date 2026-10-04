// ============================================================================
//  fsw/core/edac.hpp -- error detection and correction for memory.
//
//  In orbit, a charged particle passing through a memory cell can deposit
//  enough charge to flip the bit it holds: a SINGLE-EVENT UPSET. Nothing is
//  damaged -- write the cell again and it is fine -- but until then it holds
//  the wrong value. Over a South Atlantic Anomaly pass a large memory can
//  take several a minute.
//
//  The defence is a code: store a few extra check bits with every word, so a
//  flipped bit can be found and put back. This is the code used in nearly
//  every space-grade memory controller, the extended Hamming code (72, 64):
//  eight check bits per 64-bit word, which can
//
//      CORRECT any single flipped bit in the 72, and
//      DETECT  any two flipped bits (SEC-DED: single error correct, double
//              error detect) -- without being able to say which two.
//
//  HOW. Number the 72 bit positions 0..71. Positions 1, 2, 4, ..., 64 hold
//  check bits; the other 64 positions from 3 upwards hold the data. Check
//  bit 2^k is chosen so that, over every SET bit in the word, the positions
//  XOR to zero. Flip any one bit at position p and that XOR becomes p: the
//  SYNDROME names the broken bit directly. Position 0 holds one more bit,
//  the overall parity, which is what tells one flip (parity wrong) from two
//  (parity right, syndrome non-zero).
//
//  SCRUBBING. Correction happens when a word is read, but the stored word
//  stays wrong. A second particle hitting the same word then makes two
//  flips, which cannot be corrected. So a SCRUBBER walks the memory in the
//  background, reading and rewriting every word, to clear single flips
//  before a second one can join them. EdacArray::scrub does one word.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

namespace fsw::core {

enum class EdacResult : uint8_t { Clean, Corrected, Uncorrectable };

namespace edac_detail {

// Codeword position of data bit i: 3, 5, 6, 7, 9, ... (skipping powers of two).
struct Layout {
    uint8_t pos_of[64]{};
    int8_t  data_at[72]{};
    constexpr Layout() {
        for (int p = 0; p < 72; ++p) { data_at[p] = -1; }
        int i = 0;
        for (int p = 3; p < 72 && i < 64; ++p) {
            if ((p & (p - 1)) == 0) { continue; }     // a power of two: a check bit
            pos_of[i] = static_cast<uint8_t>(p);
            data_at[p] = static_cast<int8_t>(i);
            ++i;
        }
    }
};
inline constexpr Layout kLayout{};

constexpr uint8_t parity64(uint64_t v) {
    v ^= v >> 32; v ^= v >> 16; v ^= v >> 8; v ^= v >> 4; v ^= v >> 2; v ^= v >> 1;
    return static_cast<uint8_t>(v & 1u);
}

// XOR of the positions of every set data bit.
constexpr uint8_t position_xor(uint64_t data) {
    uint8_t x = 0;
    for (int i = 0; i < 64; ++i) {
        if ((data >> i) & 1u) { x = static_cast<uint8_t>(x ^ kLayout.pos_of[i]); }
    }
    return x;
}

}  // namespace edac_detail

// Check byte: bits 0..6 are the check bits for positions 1, 2, 4, ..., 64;
// bit 7 is the overall parity of all 72 bits.
constexpr uint8_t edac_encode(uint64_t data) {
    const uint8_t c = edac_detail::position_xor(data);      // 7 bits
    const uint8_t all = static_cast<uint8_t>(edac_detail::parity64(data) ^
                                             edac_detail::parity64(c));
    return static_cast<uint8_t>(c | (all << 7));
}

// Check, and correct in place if possible.
constexpr EdacResult edac_decode(uint64_t& data, uint8_t& check) {
    const uint8_t syndrome = static_cast<uint8_t>(edac_detail::position_xor(data) ^ (check & 0x7Fu));
    const bool parity_wrong = (edac_detail::parity64(data) ^ edac_detail::parity64(check)) != 0;
    if (syndrome == 0 && !parity_wrong) { return EdacResult::Clean; }
    if (!parity_wrong) { return EdacResult::Uncorrectable; }   // two flips
    if (syndrome == 0) {                                       // the parity bit itself
        check = static_cast<uint8_t>(check ^ 0x80u);
    } else if ((syndrome & (syndrome - 1)) == 0) {             // a check bit
        check = static_cast<uint8_t>(check ^ syndrome);
    } else if (syndrome < 72 && edac_detail::kLayout.data_at[syndrome] >= 0) {
        data ^= uint64_t{1} << edac_detail::kLayout.data_at[syndrome];
    } else {
        return EdacResult::Uncorrectable;                      // odd number of flips, >= 3
    }
    return EdacResult::Corrected;
}

// N protected 64-bit words. What a radiation-tolerant memory looks like to
// the software that uses it.
template <size_t N>
class EdacArray {
 public:
    static constexpr size_t kBits = N * 72;

    void write(size_t i, uint64_t v) {
        words_[i].data = v;
        words_[i].check = edac_encode(v);
    }

    // Read with correction. The stored word is NOT repaired -- that is the
    // scrubber's job -- exactly as a hardware EDAC controller behaves.
    EdacResult read(size_t i, uint64_t& out) const {
        uint64_t d = words_[i].data;
        uint8_t  c = words_[i].check;
        const EdacResult r = edac_decode(d, c);
        out = d;
        return r;
    }

    // Read, correct and write back one word.
    EdacResult scrub(size_t i) {
        const EdacResult r = edac_decode(words_[i].data, words_[i].check);
        if (r == EdacResult::Corrected) { ++corrected_; }
        if (r == EdacResult::Uncorrectable) { ++uncorrectable_; }
        return r;
    }

    // What a particle does. Bit b of the whole array, data and check bits
    // alike: the check bits are just as exposed as the data.
    void flip(size_t b) {
        b %= kBits;
        Word& w = words_[b / 72];
        const size_t k = b % 72;
        if (k < 64) { w.data ^= uint64_t{1} << k; }
        else        { w.check = static_cast<uint8_t>(w.check ^ (1u << (k - 64))); }
    }

    uint32_t corrected()     const { return corrected_; }
    uint32_t uncorrectable() const { return uncorrectable_; }

 private:
    struct Word {
        uint64_t data  = 0;
        uint8_t  check = 0;      // edac_encode(0) == 0, so zeroed memory is valid
    };
    Word     words_[N]{};
    uint32_t corrected_     = 0;
    uint32_t uncorrectable_ = 0;
};

}  // namespace fsw::core
