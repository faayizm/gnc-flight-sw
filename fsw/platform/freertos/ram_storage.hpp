// ============================================================================
//  fsw/platform/freertos/ram_storage.hpp -- IStorage in RAM that survives a
//  reset.
//
//  The board has no flash this software may write, so the "non-volatile"
//  blocks live in a region of RAM nothing touches (NOINIT in the board's
//  linker script): neither loaded from the image nor zeroed. A reset --
//  the watchdog's, say -- leaves it as it was; switching the board off does
//  not. That is exactly the property the parameter table and the boot count
//  rely on between resets, and honestly less than flash would give.
//
//  The region is just bytes, garbage at power-on. That is fine: everything
//  stored in it carries a CRC, and the code above already treats a block that
//  fails its CRC as "never written" (hal/storage.hpp).
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "hal/storage.hpp"

namespace fsw::platform {

class RamStorage final : public hal::IStorage {
 public:
    static constexpr size_t kBlockSize  = 512;
    static constexpr size_t kBlockCount = 16;
    static constexpr size_t kBytes      = kBlockSize * kBlockCount;

    // `region` is kBytes of RAM that survives a reset.
    explicit RamStorage(uint8_t* region) : mem_(region) {}

    size_t block_size()  const override { return kBlockSize; }
    size_t block_count() const override { return kBlockCount; }

    core::Status read(size_t block, uint8_t* dst, size_t length) override {
        if (block >= kBlockCount || length != kBlockSize) { return core::Status::Invalid; }
        std::memcpy(dst, mem_ + block * kBlockSize, kBlockSize);
        return core::Status::Ok;
    }

    core::Status write(size_t block, const uint8_t* src, size_t length) override {
        if (block >= kBlockCount || length != kBlockSize) { return core::Status::Invalid; }
        std::memcpy(mem_ + block * kBlockSize, src, kBlockSize);
        return core::Status::Ok;
    }

 private:
    uint8_t* mem_;
};

}  // namespace fsw::platform
