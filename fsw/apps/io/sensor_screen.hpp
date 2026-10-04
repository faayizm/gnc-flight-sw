// ============================================================================
//  fsw/apps/io/sensor_screen.hpp -- refuse to believe a sensor that is lying.
//
//  A sensor's own "valid" flag means only that its electronics think they
//  produced a reading. Two common failures never clear it:
//
//    OUT OF RANGE  a converter stuck at full scale, a broken wire floating to
//                  a rail, a corrupted register: a value that is physically
//                  impossible. Earth's field in low orbit is 20 to 60
//                  microtesla, so a magnetometer reading two millitesla is
//                  wrong whatever its flag says.
//
//    FROZEN        the sensor keeps repeating one value. Every real sensor
//                  has noise, so a reading that does not change at all, sample
//                  after sample, is not a quiet sensor -- it is a dead one.
//                  This check only works because the noise is there; it is
//                  one of the few places where noise is useful.
//
//  The screen sits at the hardware boundary, between the bridge and the rest
//  of the flight software. A sensor it distrusts has its valid flag cleared,
//  so everything downstream simply sees "no data" -- the estimator, the
//  controller and the mode manager already know what to do about that. This
//  is ISOLATION: the fault stops at the boundary instead of propagating.
//
//  RECOVERY. A screened-out sensor comes back only after a run of good
//  samples, so a sensor that flickers between broken and working is not
//  readmitted on its first good reading.
//
//  Pure logic, no bus and no clock, so it is tested sample by sample.
// ============================================================================
#pragma once

#include <cmath>
#include <cstdint>

#include "apps/messages.hpp"
#include "generated/dictionary.hpp"

namespace fsw::io {

struct ScreenChange {
    dict::SensorId    sensor;
    dict::SensorFault fault;      // NONE means "recovered"
};

class SensorScreen {
 public:
    static constexpr int kSensors = 5;

    // Screen one frame in place. Returns how many changes were written to
    // `changes` (at most one per sensor).
    int screen(msg::SensorFrame& f, ScreenChange* changes) {
        int n = 0;
        const float* mag  = &f.mag_t.x;
        const float* gyro = &f.gyro_rps.x;
        const float* sun  = &f.sun_b.x;
        double gps[6];
        for (int i = 0; i < 3; ++i) { gps[i] = f.gps_pos[i]; gps[3 + i] = f.gps_vel[i]; }

        n += check(Slot::Mag,  f.mag_valid,  mag,  3, mag_ok(f),  changes + n);
        n += check(Slot::Gyro, f.gyro_valid, gyro, 3, gyro_ok(f), changes + n);
        n += check(Slot::Sun,  f.sun_valid,  sun,  3, sun_ok(f),  changes + n);
        n += check(Slot::Star, f.star_valid, f.star_q, 4, star_ok(f), changes + n);
        n += check(Slot::Gps,  f.gps_valid,  gps,  6, gps_ok(f),  changes + n);
        return n;
    }

    bool distrusted(dict::SensorId s) const { return state_[static_cast<int>(s)].faulty; }

    // Samples in a row that must repeat exactly before a sensor is "frozen",
    // and good samples in a row before a screened-out sensor is readmitted.
    // Counted in valid readings, so a sensor that reports slowly (GPS, once a
    // second) is judged on as many readings as a fast one.
    static constexpr uint16_t kFrozenRepeats = 10;
    static constexpr uint16_t kRecoverGood   = 20;

    // Physical limits. Generous: they catch the impossible, not the unusual.
    static constexpr double kMagMinT   = 5.0e-6;
    static constexpr double kMagMaxT   = 1.0e-4;
    static constexpr double kGyroMaxRps = 1.0;          // 57 deg/s
    static constexpr double kRMinM = 6.5e6, kRMaxM = 8.0e6;
    static constexpr double kVMinMps = 6.0e3, kVMaxMps = 9.0e3;

 private:
    enum class Slot : int { Mag = 0, Gyro = 1, Sun = 2, Star = 3, Gps = 4 };

    struct State {
        double   last[6]{};
        bool     have_last = false;
        uint16_t repeats   = 0;     // consecutive readings identical to the one before
        uint16_t good      = 0;     // consecutive readings that passed every check
        bool     faulty    = false;
        dict::SensorFault fault = dict::SensorFault::NONE;
    };

    template <typename T>
    int check(Slot slot, bool& valid, const T* v, int count, bool in_range, ScreenChange* out) {
        State& s = state_[static_cast<int>(slot)];
        // No reading is not a bad reading, and not a good one either. The
        // history is kept: a star tracker reports twice a second and GPS once,
        // so most samples carry no reading from them at all.
        if (!valid) { return 0; }

        bool same = s.have_last;
        for (int i = 0; i < count; ++i) {
            if (static_cast<double>(v[i]) != s.last[i]) { same = false; }
            s.last[i] = static_cast<double>(v[i]);
        }
        s.have_last = true;
        s.repeats = same ? static_cast<uint16_t>(s.repeats + 1) : 0;

        dict::SensorFault now = dict::SensorFault::NONE;
        if (!in_range) {
            now = dict::SensorFault::RANGE;
        } else if (s.repeats + 1 >= kFrozenRepeats) {
            now = dict::SensorFault::FROZEN;
        }

        int changed = 0;
        if (now != dict::SensorFault::NONE) {
            s.good = 0;
            if (!s.faulty || s.fault != now) {
                s.faulty = true;
                s.fault = now;
                *out = ScreenChange{static_cast<dict::SensorId>(slot), now};
                changed = 1;
            }
        } else if (s.faulty) {
            // A frozen sensor must be seen to MOVE again; a single changed
            // reading after a long freeze is not enough.
            if (++s.good >= kRecoverGood) {
                s.faulty = false;
                s.fault = dict::SensorFault::NONE;
                s.good = 0;
                *out = ScreenChange{static_cast<dict::SensorId>(slot), dict::SensorFault::NONE};
                changed = 1;
            }
        }
        if (s.faulty) { valid = false; }
        return changed;
    }

    static bool finite3(const msg::Vec3f& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }
    static double norm3(const msg::Vec3f& v) {
        const double x = static_cast<double>(v.x), y = static_cast<double>(v.y),
                     z = static_cast<double>(v.z);
        return std::sqrt(x * x + y * y + z * z);
    }
    static bool mag_ok(const msg::SensorFrame& f) {
        if (!finite3(f.mag_t)) { return false; }
        const double b = norm3(f.mag_t);
        return b >= kMagMinT && b <= kMagMaxT;
    }
    static bool gyro_ok(const msg::SensorFrame& f) {
        return finite3(f.gyro_rps) && std::fabs(static_cast<double>(f.gyro_rps.x)) < kGyroMaxRps &&
               std::fabs(static_cast<double>(f.gyro_rps.y)) < kGyroMaxRps &&
               std::fabs(static_cast<double>(f.gyro_rps.z)) < kGyroMaxRps;
    }
    static bool sun_ok(const msg::SensorFrame& f) {
        const double n = norm3(f.sun_b);
        return finite3(f.sun_b) && n > 0.9 && n < 1.1;
    }
    static bool star_ok(const msg::SensorFrame& f) {
        double n2 = 0.0;
        for (float c : f.star_q) {
            if (!std::isfinite(c)) { return false; }
            n2 += static_cast<double>(c) * static_cast<double>(c);
        }
        return n2 > 0.98 && n2 < 1.02;
    }
    static bool gps_ok(const msg::SensorFrame& f) {
        double r2 = 0.0, v2 = 0.0;
        for (int i = 0; i < 3; ++i) {
            if (!std::isfinite(f.gps_pos[i]) || !std::isfinite(f.gps_vel[i])) { return false; }
            r2 += f.gps_pos[i] * f.gps_pos[i];
            v2 += f.gps_vel[i] * f.gps_vel[i];
        }
        const double r = std::sqrt(r2), v = std::sqrt(v2);
        return r > kRMinM && r < kRMaxM && v > kVMinMps && v < kVMaxMps;
    }

    State state_[kSensors];
};

}  // namespace fsw::io
