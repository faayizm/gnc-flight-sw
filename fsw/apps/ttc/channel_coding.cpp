// ============================================================================
//  fsw/apps/ttc/channel_coding.cpp
//
//  Every table here is computed at compile time from the definitions in the
//  standards, so there is no 256-entry block of magic numbers to mistype.
// ============================================================================
#include "apps/ttc/channel_coding.hpp"

namespace fsw::ttc::coding {

namespace {

// ---- GF(256) and the RS generator, conventional basis ---------------------
struct RsTables {
    uint8_t alpha_to[256] = {};   // alpha^i
    uint8_t index_of[256] = {};   // log_alpha(x); 255 stands for log(0)
    uint8_t genpoly[kRsParity + 1] = {};   // generator coefficients, index form
    uint8_t tal[256] = {};        // conventional -> dual basis
    uint8_t tal1[256] = {};       // dual basis -> conventional

    constexpr RsTables() {
        constexpr unsigned kGfPoly = 0x187, kFcr = 112, kPrim = 11;
        constexpr uint8_t  kA0 = 255;

        unsigned sr = 1;
        index_of[0] = kA0;
        alpha_to[kA0] = 0;
        for (unsigned i = 0; i < 255; ++i) {
            index_of[sr] = static_cast<uint8_t>(i);
            alpha_to[i] = static_cast<uint8_t>(sr);
            sr <<= 1;
            if (sr & 0x100) { sr ^= kGfPoly; }
            sr &= 0xFF;
        }

        uint8_t g[kRsParity + 1] = {};
        g[0] = 1;
        unsigned root = kFcr * kPrim;
        for (unsigned i = 0; i < kRsParity; ++i, root += kPrim) {
            g[i + 1] = 1;
            for (unsigned j = i; j > 0; --j) {
                g[j] = (g[j] != 0)
                    ? static_cast<uint8_t>(g[j - 1] ^ alpha_to[(index_of[g[j]] + root) % 255])
                    : g[j - 1];
            }
            g[0] = alpha_to[(index_of[g[0]] + root) % 255];
        }
        for (unsigned i = 0; i <= kRsParity; ++i) { genpoly[i] = index_of[g[i]]; }

        // Berlekamp's dual basis: CCSDS 131.0-B, annex F. Each row of the
        // transformation matrix, most significant first.
        constexpr uint8_t kT[8] = {0x8d, 0xef, 0xec, 0x86, 0xfa, 0x99, 0xaf, 0x7b};
        for (unsigned i = 0; i < 256; ++i) {
            uint8_t v = 0;
            for (unsigned k = 0; k < 8; ++k) {
                if (i & (1u << k)) { v = static_cast<uint8_t>(v ^ kT[7 - k]); }
            }
            tal[i] = v;
            tal1[v] = static_cast<uint8_t>(i);
        }
    }
};

constexpr RsTables kRs{};

// ---- Pseudo-randomiser sequence -------------------------------------------
struct RandomiserTable {
    uint8_t seq[kRsN] = {};
    constexpr RandomiserTable() {
        unsigned reg = 0xFF;   // bit 7 is the oldest stage, and the output
        for (size_t i = 0; i < kRsN; ++i) {
            unsigned byte = 0;
            for (int b = 0; b < 8; ++b) {
                const unsigned out = (reg >> 7) & 1u;
                // taps on stages 0, 3, 5, 7 counted from the output
                const unsigned fb = ((reg >> 7) ^ (reg >> 4) ^ (reg >> 2) ^ reg) & 1u;
                byte = (byte << 1) | out;
                reg = ((reg << 1) | fb) & 0xFFu;
            }
            seq[i] = static_cast<uint8_t>(byte);
        }
    }
};

constexpr RandomiserTable kRand{};

// ---- BCH(63,56): g(x) = x^7 + x^6 + x^2 + 1 -------------------------------
constexpr unsigned kBchGen = 0xC5;   // x^7 is implicit: 1100 0101 -> x^6+x^2+1 plus x^7

// Remainder of the 56 information bits times x^7, divided by g(x).
constexpr uint8_t bch_remainder(const uint8_t* info) {
    unsigned rem = 0;
    for (size_t i = 0; i < kCodeblockInfo; ++i) {
        for (int b = 7; b >= 0; --b) {
            const unsigned in = (static_cast<unsigned>(info[i]) >> b) & 1u;
            const unsigned top = ((rem >> 6) & 1u) ^ in;
            rem = (rem << 1) & 0x7Fu;
            if (top) { rem ^= (kBchGen & 0x7Fu); }
        }
    }
    return static_cast<uint8_t>(rem);
}

// Syndrome of a single-bit error at each of the 63 codeword bit positions,
// built by encoding it. 255 marks "no single-bit error has this syndrome".
struct BchSyndromes {
    uint8_t position_of[128] = {};
    constexpr BchSyndromes() {
        for (unsigned s = 0; s < 128; ++s) { position_of[s] = 255; }
        for (unsigned pos = 0; pos < 63; ++pos) {
            uint8_t cb[8] = {};
            cb[pos / 8] = static_cast<uint8_t>(0x80u >> (pos % 8));
            // Syndrome of the error pattern: parity of the info bits XOR the
            // received parity bits (no complement -- it cancels).
            const unsigned syn = (bch_remainder(cb) ^ (cb[7] >> 1)) & 0x7Fu;
            position_of[syn] = static_cast<uint8_t>(pos);
        }
    }
};

constexpr BchSyndromes kBch{};

}  // namespace

void rs_encode(const uint8_t* data, uint8_t* parity) {
    // The LFSR form of systematic encoding, in the conventional basis; data
    // in and parity out are converted to and from the dual basis.
    uint8_t p[kRsParity] = {};
    for (size_t i = 0; i < kRsK; ++i) {
        const uint8_t feedback = kRs.index_of[kRs.tal1[data[i]] ^ p[0]];
        if (feedback != 255) {
            for (size_t j = 1; j < kRsParity; ++j) {
                p[j] = static_cast<uint8_t>(
                    p[j] ^ kRs.alpha_to[(feedback + kRs.genpoly[kRsParity - j]) % 255]);
            }
        }
        for (size_t j = 0; j + 1 < kRsParity; ++j) { p[j] = p[j + 1]; }
        p[kRsParity - 1] = (feedback != 255)
            ? kRs.alpha_to[(feedback + kRs.genpoly[0]) % 255] : 0;
    }
    for (size_t j = 0; j < kRsParity; ++j) { parity[j] = kRs.tal[p[j]]; }
}

void randomise(uint8_t* data, size_t n) {
    for (size_t i = 0; i < n; ++i) { data[i] = static_cast<uint8_t>(data[i] ^ kRand.seq[i % kRsN]); }
}

uint8_t bch_parity(const uint8_t* info7) {
    return static_cast<uint8_t>(((~bch_remainder(info7)) & 0x7Fu) << 1);
}

BchResult bch_decode(uint8_t* cb) {
    const unsigned received = (static_cast<unsigned>(cb[7]) >> 1) & 0x7Fu;
    const unsigned expected = (~static_cast<unsigned>(bch_remainder(cb))) & 0x7Fu;
    const unsigned syn = received ^ expected;
    if (syn == 0) { return BchResult::Ok; }
    const uint8_t pos = kBch.position_of[syn];
    if (pos == 255) { return BchResult::Uncorrectable; }
    cb[pos / 8] = static_cast<uint8_t>(cb[pos / 8] ^ (0x80u >> (pos % 8)));
    return BchResult::Corrected;
}

}  // namespace fsw::ttc::coding
