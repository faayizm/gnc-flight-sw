// ============================================================================
//  fsw/hal/storage.hpp -- non-volatile storage port.
//
//  Backs the parameter table across a reset, and later the mass memory used by
//  PUS ST[15] storage and retrieval. Modelled as a flat array of fixed-size
//  blocks, because that is the greatest common divisor of the media this will
//  actually run on: NOR flash sectors, an FRAM, a file in the SIL build.
//
//  Every read is checked. Storage that has been sitting in a radiation
//  environment is not to be trusted, so the caller is expected to keep a CRC
//  alongside anything it stores and to have a defined answer for what to do
//  when that CRC fails -- normally "fall back to the compiled-in default and
//  raise an event", never "carry on with the corrupted value".
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/status.hpp"

namespace fsw::hal {

class IStorage {
 public:
    virtual ~IStorage() = default;

    virtual size_t block_size()  const = 0;
    virtual size_t block_count() const = 0;

    // Read one whole block. length must equal block_size().
    virtual core::Status read(size_t block, uint8_t* dst, size_t length) = 0;

    // Write one whole block. Implementations must make this as close to atomic
    // as the medium allows: a reset midway must not leave a half-written block
    // that still passes its CRC.
    virtual core::Status write(size_t block, const uint8_t* src, size_t length) = 0;
};

}  // namespace fsw::hal
