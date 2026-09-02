// ============================================================================
//  fsw/core/bytes.hpp -- big-endian serialisation primitives.
//
//  Everything that crosses a spacecraft interface is big-endian ("network
//  order"), because that is what CCSDS 133.0-B specifies. These two classes
//  are the ONLY place in the flight software that is allowed to know that.
//
//  Flight rules honoured here:
//    * no dynamic allocation           -- the caller owns the buffer
//    * no exceptions                   -- every operation returns bool
//    * no undefined behaviour on error -- a failed write poisons the writer,
//                                         so a caller may chain a dozen writes
//                                         and check ok() exactly once
//    * no type punning through casts   -- floats go through memcpy, which is
//                                         the only portable spelling
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace fsw::core {

// ---------------------------------------------------------------------------
// ByteWriter -- append big-endian values into a caller-owned buffer.
//
// Once any write overflows the buffer the writer becomes "poisoned": every
// later write is a no-op and ok() stays false. That lets serialisation code
// read as a flat list of writes with a single check at the end, instead of
// branching after every field.
// ---------------------------------------------------------------------------
class ByteWriter {
 public:
    ByteWriter(uint8_t* data, size_t capacity)
        : data_(data), capacity_(capacity), pos_(0), ok_(data != nullptr) {}

    bool write_uint8(uint8_t v)   { return raw(&v, 1); }
    bool write_int8(int8_t v)     { return write_uint8(static_cast<uint8_t>(v)); }

    bool write_uint16(uint16_t v) {
        const uint8_t b[2] = { static_cast<uint8_t>(v >> 8),
                               static_cast<uint8_t>(v) };
        return raw(b, 2);
    }
    bool write_int16(int16_t v)   { return write_uint16(static_cast<uint16_t>(v)); }

    bool write_uint32(uint32_t v) {
        const uint8_t b[4] = { static_cast<uint8_t>(v >> 24),
                               static_cast<uint8_t>(v >> 16),
                               static_cast<uint8_t>(v >> 8),
                               static_cast<uint8_t>(v) };
        return raw(b, 4);
    }
    bool write_int32(int32_t v)   { return write_uint32(static_cast<uint32_t>(v)); }

    bool write_uint64(uint64_t v) {
        const uint8_t b[8] = { static_cast<uint8_t>(v >> 56),
                               static_cast<uint8_t>(v >> 48),
                               static_cast<uint8_t>(v >> 40),
                               static_cast<uint8_t>(v >> 32),
                               static_cast<uint8_t>(v >> 24),
                               static_cast<uint8_t>(v >> 16),
                               static_cast<uint8_t>(v >> 8),
                               static_cast<uint8_t>(v) };
        return raw(b, 8);
    }
    bool write_int64(int64_t v)   { return write_uint64(static_cast<uint64_t>(v)); }

    // IEEE 754 binary32 / binary64 in big-endian order. memcpy is the portable
    // way to reinterpret the bits; every compiler folds it away.
    bool write_float32(float v) {
        uint32_t bits;
        std::memcpy(&bits, &v, sizeof bits);
        return write_uint32(bits);
    }
    bool write_float64(double v) {
        uint64_t bits;
        std::memcpy(&bits, &v, sizeof bits);
        return write_uint64(bits);
    }

    bool write_bytes(const uint8_t* src, size_t n) { return raw(src, n); }

    // Reserve n bytes and hand back where they landed, so a length or CRC
    // field can be written now and back-patched once the body is known.
    uint8_t* reserve(size_t n) {
        if (!ok_ || pos_ + n > capacity_) { ok_ = false; return nullptr; }
        uint8_t* at = data_ + pos_;
        pos_ += n;
        return at;
    }

    size_t         size()     const { return pos_; }
    size_t         capacity() const { return capacity_; }
    size_t         remaining() const { return ok_ ? capacity_ - pos_ : 0; }
    bool           ok()       const { return ok_; }
    const uint8_t* data()     const { return data_; }
    uint8_t*       data()           { return data_; }

    void reset() { pos_ = 0; ok_ = (data_ != nullptr); }

 private:
    bool raw(const uint8_t* src, size_t n) {
        if (!ok_ || pos_ + n > capacity_) { ok_ = false; return false; }
        std::memcpy(data_ + pos_, src, n);
        pos_ += n;
        return true;
    }

    uint8_t* data_;
    size_t   capacity_;
    size_t   pos_;
    bool     ok_;
};

// ---------------------------------------------------------------------------
// ByteReader -- pull big-endian values out of a caller-owned buffer.
//
// Same poisoning contract as ByteWriter: a read past the end leaves the output
// untouched, clears ok(), and every later read is a no-op.
// ---------------------------------------------------------------------------
class ByteReader {
 public:
    ByteReader(const uint8_t* data, size_t size)
        : data_(data), size_(size), pos_(0), ok_(data != nullptr) {}

    bool read_uint8(uint8_t& out) { return raw(&out, 1); }
    bool read_int8(int8_t& out) {
        uint8_t v;
        if (!read_uint8(v)) { return false; }
        out = static_cast<int8_t>(v);
        return true;
    }

    bool read_uint16(uint16_t& out) {
        uint8_t b[2];
        if (!raw(b, 2)) { return false; }
        out = static_cast<uint16_t>((static_cast<uint16_t>(b[0]) << 8) | b[1]);
        return true;
    }
    bool read_int16(int16_t& out) {
        uint16_t v;
        if (!read_uint16(v)) { return false; }
        out = static_cast<int16_t>(v);
        return true;
    }

    bool read_uint32(uint32_t& out) {
        uint8_t b[4];
        if (!raw(b, 4)) { return false; }
        out = (static_cast<uint32_t>(b[0]) << 24) | (static_cast<uint32_t>(b[1]) << 16) |
              (static_cast<uint32_t>(b[2]) << 8)  |  static_cast<uint32_t>(b[3]);
        return true;
    }
    bool read_int32(int32_t& out) {
        uint32_t v;
        if (!read_uint32(v)) { return false; }
        out = static_cast<int32_t>(v);
        return true;
    }

    bool read_uint64(uint64_t& out) {
        uint8_t b[8];
        if (!raw(b, 8)) { return false; }
        out = 0;
        for (int i = 0; i < 8; ++i) {
            out = (out << 8) | static_cast<uint64_t>(b[i]);
        }
        return true;
    }
    bool read_int64(int64_t& out) {
        uint64_t v;
        if (!read_uint64(v)) { return false; }
        out = static_cast<int64_t>(v);
        return true;
    }

    bool read_float32(float& out) {
        uint32_t bits;
        if (!read_uint32(bits)) { return false; }
        std::memcpy(&out, &bits, sizeof out);
        return true;
    }
    bool read_float64(double& out) {
        uint64_t bits;
        if (!read_uint64(bits)) { return false; }
        std::memcpy(&out, &bits, sizeof out);
        return true;
    }

    bool read_bytes(uint8_t* dst, size_t n) { return raw(dst, n); }

    // Borrow n bytes in place without copying, for pass-through payloads.
    const uint8_t* take(size_t n) {
        if (!ok_ || pos_ + n > size_) { ok_ = false; return nullptr; }
        const uint8_t* at = data_ + pos_;
        pos_ += n;
        return at;
    }

    bool skip(size_t n) { return take(n) != nullptr; }

    size_t         offset()    const { return pos_; }
    size_t         remaining() const { return ok_ ? size_ - pos_ : 0; }
    bool           ok()        const { return ok_; }
    bool           exhausted() const { return pos_ >= size_; }
    const uint8_t* data()      const { return data_; }
    size_t         size()      const { return size_; }

 private:
    bool raw(uint8_t* dst, size_t n) {
        if (!ok_ || pos_ + n > size_) { ok_ = false; return false; }
        std::memcpy(dst, data_ + pos_, n);
        pos_ += n;
        return true;
    }

    const uint8_t* data_;
    size_t         size_;
    size_t         pos_;
    bool           ok_;
};

}  // namespace fsw::core
