// ============================================================================
//  fsw/core/time.hpp -- the spacecraft's notion of time.
//
//  Two different clocks live here and must never be confused:
//
//    Monotonic  A tick count since boot. Never jumps, never runs backwards,
//               unaffected by time correlation from the ground. EVERY control
//               law, timeout and rate group uses this, because a corrected
//               clock that steps backwards would otherwise stall the scheduler
//               or produce a negative dt in a filter.
//
//    Mission    Seconds since the mission epoch, steerable from the ground.
//               Used only for timestamping telemetry and for time-tagged
//               command execution (PUS ST[11], a later phase).
//
//  Mission time is downlinked as CCSDS Unsegmented Code: 4 octets of coarse
//  seconds followed by 2 octets of fine time in units of 1/65536 s, which is
//  a resolution of about 15 microseconds. That is the "CUC 4,2" format named
//  in dictionary/mission.yaml.
// ============================================================================
#pragma once

#include <cstdint>

namespace fsw::core {

// A span of time. Stored in microseconds in a signed 64-bit integer, which
// covers roughly +/- 292,000 years -- no wrap to reason about, and signed so
// that a difference of two instants is naturally representable.
class Duration {
 public:
    constexpr Duration() = default;
    static constexpr Duration micros(int64_t v)  { return Duration(v); }
    static constexpr Duration millis(int64_t v)  { return Duration(v * 1000); }
    static constexpr Duration seconds(int64_t v) { return Duration(v * 1000000); }
    static constexpr Duration hz(int32_t rate)   { return Duration(1000000 / rate); }

    constexpr int64_t to_micros()  const { return us_; }
    constexpr int64_t to_millis()  const { return us_ / 1000; }
    constexpr int64_t to_seconds() const { return us_ / 1000000; }
    constexpr double  to_double_seconds() const { return static_cast<double>(us_) * 1e-6; }

    constexpr Duration operator+(Duration o) const { return Duration(us_ + o.us_); }
    constexpr Duration operator-(Duration o) const { return Duration(us_ - o.us_); }
    constexpr Duration operator*(int64_t k)  const { return Duration(us_ * k); }
    constexpr bool operator<(Duration o)  const { return us_ < o.us_; }
    constexpr bool operator<=(Duration o) const { return us_ <= o.us_; }
    constexpr bool operator>(Duration o)  const { return us_ > o.us_; }
    constexpr bool operator>=(Duration o) const { return us_ >= o.us_; }
    constexpr bool operator==(Duration o) const { return us_ == o.us_; }

 private:
    explicit constexpr Duration(int64_t us) : us_(us) {}
    int64_t us_ = 0;
};

// A point on the monotonic clock. Deliberately a distinct type from Duration
// so that "instant plus instant", which is meaningless, will not compile.
class Instant {
 public:
    constexpr Instant() = default;
    static constexpr Instant from_micros(int64_t v) { return Instant(v); }
    constexpr int64_t to_micros() const { return us_; }

    constexpr Duration operator-(Instant o) const {
        return Duration::micros(us_ - o.us_);
    }
    constexpr Instant operator+(Duration d) const {
        return Instant(us_ + d.to_micros());
    }
    constexpr bool operator<(Instant o)  const { return us_ < o.us_; }
    constexpr bool operator>=(Instant o) const { return us_ >= o.us_; }

 private:
    explicit constexpr Instant(int64_t us) : us_(us) {}
    int64_t us_ = 0;
};

// Mission elapsed time in the CCSDS Unsegmented Code wire format.
struct CucTime {
    uint32_t coarse = 0;  // whole seconds since the mission epoch
    uint16_t fine   = 0;  // fractional seconds in units of 1/65536 s

    // Build from seconds since epoch expressed as a double. Used when the
    // ground uplinks a time correlation, and by the simulator bridge.
    static CucTime from_seconds(double seconds) {
        CucTime t;
        if (seconds < 0.0) { return t; }
        const double whole = static_cast<double>(static_cast<uint64_t>(seconds));
        t.coarse = static_cast<uint32_t>(whole);
        double frac = seconds - whole;
        if (frac < 0.0) { frac = 0.0; }
        if (frac > 0.999985) { frac = 0.999985; }
        t.fine = static_cast<uint16_t>(frac * 65536.0);
        return t;
    }

    double to_seconds() const {
        return static_cast<double>(coarse) +
               static_cast<double>(fine) / 65536.0;
    }
};

}  // namespace fsw::core
