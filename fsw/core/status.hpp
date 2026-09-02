// ============================================================================
//  fsw/core/status.hpp -- outcome codes shared across the flight software.
//
//  Flight code does not throw. Anything that can fail returns one of these,
//  and anything that fails visibly to the ground is reported with a
//  FailureCode inside a PUS ST[1,2] or ST[1,8] verification report.
// ============================================================================
#pragma once

#include <cstdint>

namespace fsw::core {

// Internal result of an operation. Not downlinked directly.
enum class Status : uint8_t {
    Ok = 0,
    Invalid,       // arguments made no sense
    OutOfRange,    // a value fell outside its declared limits
    NotFound,      // no such identifier
    NoSpace,       // a fixed-capacity container or buffer was full
    Unavailable,   // the subsystem exists but cannot serve the request now
    Timeout,       // an expected response never arrived
    IoError,       // a hardware or link operation failed
    Refused,       // understood, permitted, but declined by policy
};

constexpr bool is_ok(Status s) { return s == Status::Ok; }

constexpr const char* to_string(Status s) {
    switch (s) {
        case Status::Ok:          return "OK";
        case Status::Invalid:     return "INVALID";
        case Status::OutOfRange:  return "OUT_OF_RANGE";
        case Status::NotFound:    return "NOT_FOUND";
        case Status::NoSpace:     return "NO_SPACE";
        case Status::Unavailable: return "UNAVAILABLE";
        case Status::Timeout:     return "TIMEOUT";
        case Status::IoError:     return "IO_ERROR";
        case Status::Refused:     return "REFUSED";
    }
    return "UNKNOWN";
}

// Ground-visible failure reason, carried in PUS ST[1,2] and ST[1,8].
// The numbering is part of the interface: see docs/ICD.md. Never renumber.
enum class FailureCode : uint16_t {
    Ok             = 0,
    BadCrc         = 1,  // packet error control did not match
    BadLength      = 2,  // packet too short, or argument block the wrong size
    UnknownService = 3,  // no handler for this (service, subtype)
    IllegalArg     = 4,  // an argument was outside its permitted range
    Unavailable    = 5,  // the addressed function is not currently available
    Refused        = 6,  // understood and legal, but declined (e.g. a mode change)
};

constexpr const char* to_string(FailureCode c) {
    switch (c) {
        case FailureCode::Ok:             return "OK";
        case FailureCode::BadCrc:         return "BAD_CRC";
        case FailureCode::BadLength:      return "BAD_LENGTH";
        case FailureCode::UnknownService: return "UNKNOWN_SERVICE";
        case FailureCode::IllegalArg:     return "ILLEGAL_ARG";
        case FailureCode::Unavailable:    return "UNAVAILABLE";
        case FailureCode::Refused:        return "REFUSED";
    }
    return "UNKNOWN";
}

// Map an internal Status onto the code the ground will see.
constexpr FailureCode to_failure(Status s) {
    switch (s) {
        case Status::Ok:          return FailureCode::Ok;
        case Status::Invalid:     return FailureCode::IllegalArg;
        case Status::OutOfRange:  return FailureCode::IllegalArg;
        case Status::NotFound:    return FailureCode::UnknownService;
        case Status::NoSpace:     return FailureCode::Unavailable;
        case Status::Unavailable: return FailureCode::Unavailable;
        case Status::Timeout:     return FailureCode::Unavailable;
        case Status::IoError:     return FailureCode::Unavailable;
        case Status::Refused:     return FailureCode::Refused;
    }
    return FailureCode::Unavailable;
}

}  // namespace fsw::core
