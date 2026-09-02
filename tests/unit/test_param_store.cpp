// ============================================================================
//  Tests for the on-board parameter table.
//
//  The recovery behaviour is the point of these tests. A parameter store that
//  works when everything is fine is easy; one that does the right thing when
//  its non-volatile copy has been corrupted is what keeps a spacecraft alive.
// ============================================================================
#include "core/param_store.hpp"

#include "core/bytes.hpp"
#include "core/crc.hpp"
#include "framework.hpp"

using fsw::core::ParamStore;
using fsw::core::Status;
using fsw::dict::ParamId;

TEST(params, defaults_come_from_the_dictionary) {
    ParamStore p;
    p.reset_to_defaults();
    CHECK_EQ(p.get_u32(ParamId::SYS_HK_PERIOD_MS), 1000u);
    CHECK_NEAR(p.get_f32(ParamId::DETUMBLE_RATE_DPS), 2.0, 1e-6);
    CHECK(!p.dirty());
}

TEST(params, a_value_inside_its_limits_is_accepted) {
    ParamStore p;
    p.reset_to_defaults();
    CHECK(fsw::core::is_ok(p.set(ParamId::SYS_HK_PERIOD_MS, 500)));
    CHECK_EQ(p.get_u32(ParamId::SYS_HK_PERIOD_MS), 500u);
    CHECK(p.dirty());
}

TEST(params, a_value_outside_its_limits_is_rejected_and_the_old_one_kept) {
    ParamStore p;
    p.reset_to_defaults();

    // The dictionary declares SYS_HK_PERIOD_MS as 100..60000.
    CHECK(p.set(ParamId::SYS_HK_PERIOD_MS, 99) == Status::OutOfRange);
    CHECK(p.set(ParamId::SYS_HK_PERIOD_MS, 60001) == Status::OutOfRange);
    // Not clamped to the nearest legal value -- unchanged.
    CHECK_EQ(p.get_u32(ParamId::SYS_HK_PERIOD_MS), 1000u);
}

TEST(params, nan_is_rejected) {
    // NaN compares false against both bounds, so a naive range check lets it
    // straight through and it then poisons every calculation downstream.
    ParamStore p;
    p.reset_to_defaults();
    const double nan_value = std::nan("");
    CHECK(p.set(ParamId::DETUMBLE_RATE_DPS, nan_value) == Status::Invalid);
    CHECK_NEAR(p.get_f32(ParamId::DETUMBLE_RATE_DPS), 2.0, 1e-6);
}

TEST(params, an_unknown_identifier_is_not_found) {
    ParamStore p;
    p.reset_to_defaults();
    double out = 0.0;
    CHECK(p.get(static_cast<ParamId>(9999), out) == Status::NotFound);
    CHECK(p.set(static_cast<ParamId>(9999), 1.0) == Status::NotFound);
}

TEST(params, values_are_quantised_to_their_declared_type) {
    ParamStore p;
    p.reset_to_defaults();
    // A uint32 parameter given a fractional value must read back as the
    // integer flight code will actually use, not as 1234.7.
    CHECK(fsw::core::is_ok(p.set(ParamId::SYS_HK_PERIOD_MS, 1234.7)));
    double out = 0.0;
    p.get(ParamId::SYS_HK_PERIOD_MS, out);
    CHECK_NEAR(out, 1234.0, 1e-9);
}

TEST(params, save_and_load_round_trip) {
    ParamStore saved;
    saved.reset_to_defaults();
    saved.set(ParamId::SYS_HK_PERIOD_MS, 250);
    saved.set(ParamId::BATT_LOW_SOC_PCT, 45.5);

    uint8_t block[512]{};
    size_t written = 0;
    CHECK(fsw::core::is_ok(saved.save(block, sizeof block, written)));
    CHECK_EQ(written, ParamStore::kSerialisedBytes);

    ParamStore loaded;
    loaded.reset_to_defaults();
    CHECK(fsw::core::is_ok(loaded.load(block, sizeof block)));
    CHECK_EQ(loaded.get_u32(ParamId::SYS_HK_PERIOD_MS), 250u);
    CHECK_NEAR(loaded.get_f32(ParamId::BATT_LOW_SOC_PCT), 45.5, 1e-4);
    // A successful load is not a pending change.
    CHECK(!loaded.dirty());
}

TEST(params, a_corrupted_block_is_refused_and_defaults_survive) {
    ParamStore saved;
    saved.reset_to_defaults();
    saved.set(ParamId::SYS_HK_PERIOD_MS, 250);

    uint8_t block[512]{};
    size_t written = 0;
    saved.save(block, sizeof block, written);

    block[10] ^= 0xFF;   // a radiation upset, or a reset mid-write

    ParamStore loaded;
    loaded.reset_to_defaults();
    CHECK(loaded.load(block, sizeof block) == Status::IoError);
    // The critical property: the store is still running on known-good values.
    CHECK_EQ(loaded.get_u32(ParamId::SYS_HK_PERIOD_MS), 1000u);
}

TEST(params, an_all_zero_block_is_refused) {
    // What a freshly formatted non-volatile memory looks like on first boot.
    uint8_t block[512]{};
    ParamStore p;
    p.reset_to_defaults();
    CHECK(!fsw::core::is_ok(p.load(block, sizeof block)));
    CHECK_EQ(p.get_u32(ParamId::SYS_HK_PERIOD_MS), 1000u);
}

TEST(params, a_stored_value_outside_the_current_limits_is_refused) {
    // Simulates a software upload that tightened a bound: the stored value was
    // legal when written and is not any more, so it must not be trusted.
    ParamStore saved;
    saved.reset_to_defaults();

    uint8_t block[512]{};
    size_t written = 0;
    saved.save(block, sizeof block, written);

    // Overwrite the first value with something wildly out of range, then
    // repair the CRC so integrity passes and only the range check can object.
    fsw::core::ByteWriter w(block + 4, sizeof block - 4);
    w.write_float64(1.0e12);
    const uint16_t crc = fsw::core::crc16(block, ParamStore::kSerialisedBytes - 2);
    block[ParamStore::kSerialisedBytes - 2] = static_cast<uint8_t>(crc >> 8);
    block[ParamStore::kSerialisedBytes - 1] = static_cast<uint8_t>(crc & 0xFF);

    ParamStore loaded;
    loaded.reset_to_defaults();
    CHECK(loaded.load(block, sizeof block) == Status::OutOfRange);
    CHECK_EQ(loaded.get_u32(ParamId::SYS_HK_PERIOD_MS), 1000u);
}

TEST(params, saving_into_too_small_a_buffer_fails_cleanly) {
    ParamStore p;
    p.reset_to_defaults();
    uint8_t tiny[8]{};
    size_t written = 12345;
    CHECK(p.save(tiny, sizeof tiny, written) == Status::NoSpace);
    CHECK_EQ(written, static_cast<size_t>(0));
}
