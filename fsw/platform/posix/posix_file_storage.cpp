// ============================================================================
//  fsw/platform/posix/posix_file_storage.cpp
// ============================================================================
#include "platform/posix/posix_file_storage.hpp"

#include <cstring>

namespace fsw::platform {

PosixFileStorage::~PosixFileStorage() {
    if (file_ != nullptr) { std::fclose(file_); }
}

core::Status PosixFileStorage::open() {
    // "r+b" first: if the file exists, keep what is in it. Only when that
    // fails do we create a fresh, zero-filled one -- opening with "w+b"
    // unconditionally would erase the stored parameters on every boot.
    file_ = std::fopen(path_.c_str(), "r+b");
    if (file_ == nullptr) {
        file_ = std::fopen(path_.c_str(), "w+b");
        if (file_ == nullptr) { return core::Status::IoError; }

        uint8_t zeros[kBlockSize];
        std::memset(zeros, 0, sizeof zeros);
        for (size_t i = 0; i < kBlockCount; ++i) {
            if (std::fwrite(zeros, 1, kBlockSize, file_) != kBlockSize) {
                return core::Status::IoError;
            }
        }
        std::fflush(file_);
    }
    return core::Status::Ok;
}

core::Status PosixFileStorage::read(size_t block, uint8_t* dst, size_t length) {
    if (file_ == nullptr) { return core::Status::Unavailable; }
    if (block >= kBlockCount || dst == nullptr || length != kBlockSize) {
        return core::Status::Invalid;
    }
    if (std::fseek(file_, static_cast<long>(block * kBlockSize), SEEK_SET) != 0) {
        return core::Status::IoError;
    }
    if (std::fread(dst, 1, length, file_) != length) { return core::Status::IoError; }
    return core::Status::Ok;
}

core::Status PosixFileStorage::write(size_t block, const uint8_t* src, size_t length) {
    if (file_ == nullptr) { return core::Status::Unavailable; }
    if (block >= kBlockCount || src == nullptr || length != kBlockSize) {
        return core::Status::Invalid;
    }
    if (std::fseek(file_, static_cast<long>(block * kBlockSize), SEEK_SET) != 0) {
        return core::Status::IoError;
    }
    if (std::fwrite(src, 1, length, file_) != length) { return core::Status::IoError; }

    // Flush immediately. A parameter change that survives only until the
    // process happens to exit cleanly is not persistence.
    std::fflush(file_);
    return core::Status::Ok;
}

}  // namespace fsw::platform
