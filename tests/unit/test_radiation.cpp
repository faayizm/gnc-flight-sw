// ============================================================================
//  Tests for the radiation defences: EDAC (core/edac.hpp) and triple modular
//  redundancy (core/tmr.hpp). Both claims are small enough to check
//  exhaustively, so they are: every single flip is corrected and every
//  double flip detected, for several data patterns.
// ============================================================================
#include "core/edac.hpp"
#include "core/param_store.hpp"
#include "core/tmr.hpp"
#include "framework.hpp"

using namespace fsw;
using core::EdacResult;

namespace {
constexpr uint64_t kPatterns[] = {0, ~uint64_t{0}, 0x0123456789ABCDEFull, 0x4059000000000000ull /* 100.0 */};

void flip72(uint64_t& d, uint8_t& c, int bit) {
    if (bit < 64) { d ^= uint64_t{1} << bit; } else { c = static_cast<uint8_t>(c ^ (1u << (bit - 64))); }
}
}  // namespace

TEST(edac, zeroed_memory_is_a_valid_codeword) {
    CHECK_EQ(core::edac_encode(0), 0);
}

TEST(edac, every_single_flip_is_corrected) {
    for (uint64_t v : kPatterns) {
        for (int bit = 0; bit < 72; ++bit) {
            uint64_t d = v;
            uint8_t c = core::edac_encode(v);
            flip72(d, c, bit);
            CHECK(core::edac_decode(d, c) == EdacResult::Corrected);
            CHECK(d == v);
            CHECK(c == core::edac_encode(v));
        }
    }
}

TEST(edac, every_double_flip_is_detected_and_never_miscorrected) {
    int pairs = 0;
    for (uint64_t v : kPatterns) {
        for (int a = 0; a < 72; ++a) {
            for (int b = a + 1; b < 72; ++b) {
                uint64_t d = v;
                uint8_t c = core::edac_encode(v);
                flip72(d, c, a);
                flip72(d, c, b);
                CHECK(core::edac_decode(d, c) == EdacResult::Uncorrectable);
                ++pairs;
            }
        }
    }
    CHECK_EQ(pairs, 4 * 2556);
}

TEST(edac, reads_correct_but_only_the_scrubber_repairs) {
    core::EdacArray<4> m;
    m.write(2, 0xDEADBEEFull);
    m.flip(2 * 72 + 5);
    uint64_t v = 0;
    CHECK(m.read(2, v) == EdacResult::Corrected);
    CHECK(v == 0xDEADBEEFull);
    CHECK(m.read(2, v) == EdacResult::Corrected);   // still wrong in memory
    CHECK(m.scrub(2) == EdacResult::Corrected);
    CHECK(m.read(2, v) == EdacResult::Clean);       // now repaired
    CHECK_EQ(m.corrected(), 1u);
}

TEST(edac, a_second_upset_before_scrubbing_is_one_too_many) {
    core::EdacArray<1> m;
    m.write(0, 42);
    m.flip(3);
    m.flip(40);
    uint64_t v = 0;
    CHECK(m.read(0, v) == EdacResult::Uncorrectable);
    CHECK(m.scrub(0) == EdacResult::Uncorrectable);
    CHECK_EQ(m.uncorrectable(), 1u);
}

TEST(tmr, one_corrupt_copy_is_outvoted_and_repaired) {
    core::Tmr<uint8_t> t(4);
    t.flip(8 + 1);                                  // second copy: 4 -> 6
    CHECK_EQ(t.get(), 4);
    CHECK_EQ(t.repairs(), 1u);
    CHECK_EQ(t.get(), 4);
    CHECK_EQ(t.repairs(), 1u);                      // repaired: no second repair
}

TEST(tmr, every_single_flip_in_any_copy_is_survived) {
    for (size_t b = 0; b < core::Tmr<uint16_t>::kBits; ++b) {
        core::Tmr<uint16_t> t(0x1234);
        t.flip(b);
        CHECK_EQ(t.get(), 0x1234);
    }
}

TEST(params, an_upset_parameter_reads_correctly_and_the_scrubber_repairs_it) {
    core::ParamStore p;
    p.reset_to_defaults();
    CHECK(core::is_ok(p.set(dict::ParamId::BDOT_GAIN, 123456.0)));
    p.flip(8 * 72 + 61);                            // parameter 9's word, a high exponent bit
    CHECK_NEAR(p.get_f64(dict::ParamId::BDOT_GAIN), 123456.0, 0.0);
    const core::ScrubReport r = p.scrub();
    CHECK_EQ(r.corrected, 1u);
    CHECK_EQ(r.uncorrectable, 0u);
    CHECK_EQ(p.scrub().corrected, 0u);
}

TEST(params, a_parameter_beyond_repair_falls_back_to_its_default_and_says_which) {
    core::ParamStore p;
    p.reset_to_defaults();
    CHECK(core::is_ok(p.set(dict::ParamId::BDOT_GAIN, 123456.0)));
    p.flip(8 * 72 + 61);
    p.flip(8 * 72 + 2);
    CHECK_NEAR(p.get_f64(dict::ParamId::BDOT_GAIN), 300000.0, 0.0);   // never the garbage
    const core::ScrubReport r = p.scrub();
    CHECK_EQ(r.uncorrectable, 1u);
    CHECK(r.first_bad == dict::ParamId::BDOT_GAIN);
    CHECK_NEAR(p.get_f64(dict::ParamId::BDOT_GAIN), 300000.0, 0.0);   // rewritten with the default
    CHECK_EQ(p.scrub().uncorrectable, 0u);
}
