// ============================================================================
//  fsw/core/param_store.hpp -- the on-board parameter table.
//
//  Parameters are the tuning knobs the ground can turn without a software
//  upload: control gains, mode thresholds, housekeeping periods. Getting this
//  mechanism right is worth real effort, because in flight it is often the
//  only way to change behaviour, and a parameter set to a nonsensical value is
//  a very effective way to lose a spacecraft.
//
//  Three protections, all enforced here rather than in each caller:
//
//    RANGE CHECKING. Every parameter declares min and max in the dictionary.
//    A write outside that range is rejected with ILLEGAL_ARG and the old value
//    is kept. There is no path by which an out-of-range value reaches flight
//    code.
//
//    CRC ON NON-VOLATILE STORAGE. The saved table carries a CRC. If it fails
//    on load -- corrupted by radiation, or by a reset midway through a write --
//    the compiled-in defaults are used and an event is raised. The system never
//    boots on a value it cannot vouch for.
//
//    EDAC IN MEMORY. A gain that a particle has silently changed is as bad as
//    one an operator mistyped, and there is no check on the way in to catch
//    it. Every value lives in an EDAC-protected word (core/edac.hpp): a single
//    flipped bit is corrected on every read, and scrub() repairs it in place.
//    A word damaged beyond repair reads back as the parameter's DEFAULT --
//    never as the garbage -- and scrub() reports which one, so the owner can
//    reload the stored table.
//
//    ONE STORAGE TYPE. Values are held as double and converted on access.
//    A double represents every integer up to 2^53 exactly, which covers every
//    parameter type in the dictionary except a 64-bit integer above that
//    magnitude; the generator would need extending before such a parameter
//    could be declared. The simplification is deliberate: it means ST[20]
//    transports one representation, and there is no union to get wrong.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "core/crc.hpp"
#include "core/edac.hpp"
#include "core/status.hpp"
#include "generated/dictionary.hpp"

namespace fsw::core {

// What one scrubbing pass found.
struct ScrubReport {
    uint32_t      corrected     = 0;
    uint32_t      uncorrectable = 0;
    dict::ParamId first_bad     = static_cast<dict::ParamId>(0);
};

class ParamStore {
 public:
    // Load the compiled-in defaults from the generated table. Always safe,
    // always the fallback when stored values cannot be trusted.
    void reset_to_defaults() {
        for (size_t i = 0; i < dict::kParamCount; ++i) {
            store(i, dict::kParams[i].default_value);
        }
        dirty_ = false;
    }

    Status get(dict::ParamId id, double& out) const {
        const size_t index = index_of(id);
        if (index >= dict::kParamCount) { return Status::NotFound; }
        out = value(index);
        return Status::Ok;
    }

    // Typed convenience accessors. These deliberately have no error return:
    // an unknown identifier is a programming mistake, not a runtime condition,
    // and returning the default is the safe behaviour if one ever slips through.
    double   get_f64(dict::ParamId id) const {
        const size_t i = index_of(id);
        return (i < dict::kParamCount) ? value(i) : 0.0;
    }
    float    get_f32(dict::ParamId id) const { return static_cast<float>(get_f64(id)); }
    uint32_t get_u32(dict::ParamId id) const { return static_cast<uint32_t>(get_f64(id)); }

    // Range-checked write. This is the only way a value ever changes.
    Status set(dict::ParamId id, double value) {
        const size_t index = index_of(id);
        if (index >= dict::kParamCount) { return Status::NotFound; }

        const dict::ParamInfo& info = dict::kParams[index];
        if (value < info.min_value || value > info.max_value) {
            return Status::OutOfRange;
        }
        // Reject NaN, which compares false against every bound and would
        // otherwise slip through the check above.
        if (!(value == value)) { return Status::Invalid; }

        store(index, quantise(info.type, value));
        dirty_ = true;
        return Status::Ok;
    }

    // ---- radiation -------------------------------------------------------

    // One pass over the whole table: correct what can be corrected, and put a
    // word that cannot be back to its default so it stops failing.
    ScrubReport scrub() {
        ScrubReport r;
        for (size_t i = 0; i < dict::kParamCount; ++i) {
            const EdacResult e = values_.scrub(i);
            if (e == EdacResult::Corrected) { ++r.corrected; }
            if (e == EdacResult::Uncorrectable) {
                if (r.uncorrectable++ == 0) { r.first_bad = dict::kParams[i].id; }
                store(i, dict::kParams[i].default_value);
            }
        }
        return r;
    }

    // Where a particle strikes. Exposed for software-in-the-loop radiation
    // testing only; nothing in flight code calls it.
    static constexpr size_t kBits = EdacArray<dict::kParamCount>::kBits;
    void flip(size_t bit) { values_.flip(bit); }

    uint32_t edac_corrected()     const { return values_.corrected(); }
    uint32_t edac_uncorrectable() const { return values_.uncorrectable(); }

    // ---- non-volatile storage ------------------------------------------
    // Serialised layout: a 16-bit magic, a 16-bit parameter count, the values
    // as big-endian doubles, then a CRC-16 over everything preceding it.

    static constexpr uint16_t kMagic = 0x5041;  // 'PA'
    static constexpr size_t   kSerialisedBytes = 2 + 2 + dict::kParamCount * 8 + 2;

    Status save(uint8_t* buffer, size_t capacity, size_t& written) const;
    Status load(const uint8_t* buffer, size_t length);

    bool   dirty() const { return dirty_; }
    void   clear_dirty()  { dirty_ = false; }

 private:
    // Round-trip the value through its declared type, so that reading back a
    // parameter declared uint32 never returns 999.7 just because the ground
    // sent that. What the ground reads back is what the flight code will use.
    static double quantise(dict::ParamType type, double value) {
        switch (type) {
            case dict::ParamType::U8:  return static_cast<double>(static_cast<uint8_t>(value));
            case dict::ParamType::I8:  return static_cast<double>(static_cast<int8_t>(value));
            case dict::ParamType::U16: return static_cast<double>(static_cast<uint16_t>(value));
            case dict::ParamType::I16: return static_cast<double>(static_cast<int16_t>(value));
            case dict::ParamType::U32: return static_cast<double>(static_cast<uint32_t>(value));
            case dict::ParamType::I32: return static_cast<double>(static_cast<int32_t>(value));
            case dict::ParamType::U64: return static_cast<double>(static_cast<uint64_t>(value));
            case dict::ParamType::I64: return static_cast<double>(static_cast<int64_t>(value));
            case dict::ParamType::F32: return static_cast<double>(static_cast<float>(value));
            case dict::ParamType::F64: return value;
        }
        return value;
    }

    static size_t index_of(dict::ParamId id) {
        for (size_t i = 0; i < dict::kParamCount; ++i) {
            if (dict::kParams[i].id == id) { return i; }
        }
        return dict::kParamCount;  // sentinel meaning "not found"
    }

    // Doubles travel through the EDAC words as their bit patterns.
    double value(size_t i) const {
        uint64_t bits = 0;
        if (values_.read(i, bits) == EdacResult::Uncorrectable) {
            return dict::kParams[i].default_value;
        }
        double v = 0.0;
        std::memcpy(&v, &bits, sizeof v);
        return v;
    }
    void store(size_t i, double v) {
        uint64_t bits = 0;
        std::memcpy(&bits, &v, sizeof bits);
        values_.write(i, bits);
    }

    EdacArray<dict::kParamCount> values_;
    bool   dirty_ = false;
};

}  // namespace fsw::core
