// ============================================================================
//  Tests for the big-endian serialisation primitives.
//
//  Byte order is checked explicitly against literal bytes rather than by
//  round-tripping, because a reader and writer that are both wrong in the same
//  way would round-trip perfectly and still be unable to talk to any real
//  ground system.
// ============================================================================
#include "core/bytes.hpp"
#include "framework.hpp"

using fsw::core::ByteReader;
using fsw::core::ByteWriter;

TEST(bytes, integers_are_written_most_significant_byte_first) {
    uint8_t buf[8]{};
    ByteWriter w(buf, sizeof buf);
    CHECK(w.write_uint32(0x12345678u));
    CHECK_EQ(w.size(), static_cast<size_t>(4));
    CHECK_EQ(buf[0], static_cast<uint8_t>(0x12));
    CHECK_EQ(buf[1], static_cast<uint8_t>(0x34));
    CHECK_EQ(buf[2], static_cast<uint8_t>(0x56));
    CHECK_EQ(buf[3], static_cast<uint8_t>(0x78));
}

TEST(bytes, sixteen_and_sixty_four_bit_values_too) {
    uint8_t buf[16]{};
    ByteWriter w(buf, sizeof buf);
    w.write_uint16(0xABCDu);
    w.write_uint64(0x0102030405060708ull);
    CHECK_EQ(buf[0], static_cast<uint8_t>(0xAB));
    CHECK_EQ(buf[1], static_cast<uint8_t>(0xCD));
    CHECK_EQ(buf[2], static_cast<uint8_t>(0x01));
    CHECK_EQ(buf[9], static_cast<uint8_t>(0x08));
}

TEST(bytes, every_type_round_trips) {
    uint8_t buf[64]{};
    ByteWriter w(buf, sizeof buf);
    w.write_uint8(0xFE);
    w.write_int8(-3);
    w.write_uint16(65000);
    w.write_int16(-30000);
    w.write_uint32(4000000000u);
    w.write_int32(-2000000000);
    w.write_uint64(18000000000000000000ull);
    w.write_int64(-9000000000000000000ll);
    w.write_float32(3.5f);
    w.write_float64(-1.0e-9);
    CHECK(w.ok());

    ByteReader r(buf, w.size());
    uint8_t  u8 = 0;  int8_t  i8 = 0;
    uint16_t u16 = 0; int16_t i16 = 0;
    uint32_t u32 = 0; int32_t i32 = 0;
    uint64_t u64 = 0; int64_t i64 = 0;
    float    f32 = 0.0f; double f64 = 0.0;
    CHECK(r.read_uint8(u8));   CHECK_EQ(u8, static_cast<uint8_t>(0xFE));
    CHECK(r.read_int8(i8));    CHECK_EQ(i8, static_cast<int8_t>(-3));
    CHECK(r.read_uint16(u16)); CHECK_EQ(u16, static_cast<uint16_t>(65000));
    CHECK(r.read_int16(i16));  CHECK_EQ(i16, static_cast<int16_t>(-30000));
    CHECK(r.read_uint32(u32)); CHECK_EQ(u32, 4000000000u);
    CHECK(r.read_int32(i32));  CHECK_EQ(i32, -2000000000);
    CHECK(r.read_uint64(u64)); CHECK_EQ(u64, 18000000000000000000ull);
    CHECK(r.read_int64(i64));  CHECK_EQ(i64, -9000000000000000000ll);
    CHECK(r.read_float32(f32)); CHECK_NEAR(f32, 3.5, 1e-9);
    CHECK(r.read_float64(f64)); CHECK_NEAR(f64, -1.0e-9, 1e-18);
    CHECK(r.exhausted());
}

TEST(bytes, a_writer_that_overflows_stays_poisoned) {
    uint8_t buf[4]{};
    ByteWriter w(buf, sizeof buf);
    CHECK(w.write_uint32(1));
    CHECK(w.ok());

    CHECK(!w.write_uint8(2));   // no room
    CHECK(!w.ok());
    // The poisoning contract: a later write that WOULD have fit must still
    // fail, so a caller can check ok() once at the end instead of every time.
    CHECK(!w.write_uint8(3));
    CHECK_EQ(w.size(), static_cast<size_t>(4));
}

TEST(bytes, a_reader_past_the_end_leaves_the_output_untouched) {
    const uint8_t buf[2] = {0xAA, 0xBB};
    ByteReader r(buf, sizeof buf);

    uint32_t value = 0x5A5A5A5Au;
    CHECK(!r.read_uint32(value));
    CHECK_EQ(value, 0x5A5A5A5Au);   // not partially overwritten
    CHECK(!r.ok());
}

TEST(bytes, reserve_hands_back_a_patchable_pointer) {
    uint8_t buf[8]{};
    ByteWriter w(buf, sizeof buf);
    uint8_t* slot = w.reserve(2);
    CHECK(slot != nullptr);
    w.write_uint16(0x1111);

    slot[0] = 0xDE;
    slot[1] = 0xAD;
    CHECK_EQ(buf[0], static_cast<uint8_t>(0xDE));
    CHECK_EQ(buf[1], static_cast<uint8_t>(0xAD));
    CHECK_EQ(w.size(), static_cast<size_t>(4));
}

TEST(bytes, take_borrows_without_copying) {
    const uint8_t buf[4] = {1, 2, 3, 4};
    ByteReader r(buf, sizeof buf);
    r.skip(1);
    const uint8_t* at = r.take(2);
    CHECK(at != nullptr);
    CHECK_EQ(at[0], static_cast<uint8_t>(2));
    CHECK_EQ(r.remaining(), static_cast<size_t>(1));
    CHECK(r.take(99) == nullptr);
}
