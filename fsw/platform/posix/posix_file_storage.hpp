// ============================================================================
//  fsw/platform/posix/posix_file_storage.hpp -- IStorage backed by a file.
//
//  Stands in for the non-volatile memory that holds the parameter table across
//  a reset. A single file of block_count * block_size bytes, written in place.
//
//  It is NOT a faithful model of flash: there is no erase-before-write, no
//  wear levelling, no partial-page hazard. It is a faithful model of the one
//  property that matters for the software above it -- that data written before
//  a restart is there afterwards, and that anything else must be caught by the
//  CRC the caller is required to keep.
//
//  Phase 6 replaces this with a version that can be told to corrupt a block on
//  demand, which is how the parameter store's recovery path gets tested.
// ============================================================================
#pragma once

#include <cstdio>
#include <string>

#include "hal/storage.hpp"

namespace fsw::platform {

class PosixFileStorage final : public hal::IStorage {
 public:
    static constexpr size_t kBlockSize  = 512;
    static constexpr size_t kBlockCount = 16;

    explicit PosixFileStorage(std::string path) : path_(std::move(path)) {}
    ~PosixFileStorage() override;

    // Open the backing file, creating and zero-filling it if it does not exist.
    core::Status open();

    size_t block_size()  const override { return kBlockSize; }
    size_t block_count() const override { return kBlockCount; }

    core::Status read(size_t block, uint8_t* dst, size_t length) override;
    core::Status write(size_t block, const uint8_t* src, size_t length) override;

 private:
    std::string path_;
    std::FILE*  file_ = nullptr;
};

}  // namespace fsw::platform
