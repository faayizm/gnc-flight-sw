// ============================================================================
//  fsw/core/param_store.cpp -- persistence for the parameter table.
//  See param_store.hpp for why the CRC is not optional.
// ============================================================================
#include "core/param_store.hpp"

#include "core/bytes.hpp"

namespace fsw::core {

Status ParamStore::save(uint8_t* buffer, size_t capacity, size_t& written) const {
    written = 0;
    if (capacity < kSerialisedBytes) { return Status::NoSpace; }

    ByteWriter w(buffer, capacity);
    w.write_uint16(kMagic);
    w.write_uint16(static_cast<uint16_t>(dict::kParamCount));
    for (size_t i = 0; i < dict::kParamCount; ++i) {
        w.write_float64(values_[i]);
    }
    if (!w.ok()) { return Status::NoSpace; }

    // CRC covers the magic, the count and every value, so a truncated or
    // partially rewritten block fails the check rather than loading garbage.
    const uint16_t crc = crc16(buffer, w.size());
    w.write_uint16(crc);
    if (!w.ok()) { return Status::NoSpace; }

    written = w.size();
    return Status::Ok;
}

Status ParamStore::load(const uint8_t* buffer, size_t length) {
    // Every failure path below leaves the caller's values untouched, so a
    // caller that has already applied reset_to_defaults() ends up running on
    // known-good numbers no matter how badly the stored block is damaged.
    if (buffer == nullptr || length < kSerialisedBytes) { return Status::Invalid; }

    // Running the CRC across the block including its own check field yields
    // zero when intact. Checked before anything is interpreted.
    if (!crc16_check(buffer, kSerialisedBytes)) { return Status::IoError; }

    ByteReader r(buffer, kSerialisedBytes);
    uint16_t magic = 0;
    uint16_t count = 0;
    if (!r.read_uint16(magic) || !r.read_uint16(count)) { return Status::Invalid; }
    if (magic != kMagic) { return Status::Invalid; }

    // A count mismatch means the dictionary changed since this block was
    // written -- a software upload, most likely. Refusing is the safe answer:
    // values would otherwise be silently reassigned to different parameters.
    if (count != dict::kParamCount) { return Status::Invalid; }

    // Read into a scratch array first, so a value that fails its range check
    // cannot leave the table half-updated.
    double staged[dict::kParamCount];
    for (size_t i = 0; i < dict::kParamCount; ++i) {
        if (!r.read_float64(staged[i])) { return Status::Invalid; }
    }

    // Re-validate against the CURRENT limits. A stored value can fall out of
    // range when the dictionary tightens a bound, and it must not be trusted
    // merely because it was once written successfully.
    for (size_t i = 0; i < dict::kParamCount; ++i) {
        const dict::ParamInfo& info = dict::kParams[i];
        const double v = staged[i];
        if (!(v == v) || v < info.min_value || v > info.max_value) {
            return Status::OutOfRange;
        }
    }

    for (size_t i = 0; i < dict::kParamCount; ++i) {
        values_[i] = quantise(dict::kParams[i].type, staged[i]);
    }
    dirty_ = false;
    return Status::Ok;
}

}  // namespace fsw::core
