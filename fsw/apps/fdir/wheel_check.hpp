// ============================================================================
//  fsw/apps/fdir/wheel_check.hpp -- is each reaction wheel doing what it is told?
//
//  A wheel motor's job is to change the wheel's angular momentum at the rate
//  it is commanded. The tachometer measures the momentum. So the check is a
//  bookkeeping identity, per axis, over a window of time T:
//
//      measured change   h(t + T) - h(t)
//      commanded change  sum of (torque command x sample period) over T
//
//  For a healthy wheel the two agree to within the friction the drive does
//  not quite compensate. A dead drive delivers no torque at all, and its
//  wheel slows on bearing friction instead: the two disagree, quickly and by
//  a lot.
//
//  WHY A WINDOW, NOT EACH SAMPLE. The tachometer is quantised: its momentum
//  reading moves in steps of 1e-6 N*m*s, so a single 0.1 s difference has an
//  uncertainty of 1e-5 N*m -- larger than the friction this check needs to
//  see. Over a window the steps telescope: only the first and last readings'
//  rounding matter, whatever the window's length. Five seconds makes the
//  quantisation negligible, and still answers within ten.
//
//  PERSISTENCE. One bad window is a suspicion; `fail_windows` in a row is a
//  verdict. Some disagreement is legitimate (a wheel at its momentum limit
//  cannot accelerate, and the drive clips at its torque limit), and those
//  windows are skipped rather than counted either way.
//
//  Pure arithmetic, no bus, so it is tested against hand-made data.
// ============================================================================
#pragma once

#include <cmath>
#include <cstdint>

namespace fsw::fdir {

struct WheelCheckConfig {
    double window_s      = 5.0;
    double threshold_nms = 1.0e-5;   // disagreement allowed over one window
    double max_h_nms     = 0.03;     // wheel momentum limit, from the datasheet
    double max_torque_nm = 2.0e-3;   // drive torque limit, from the datasheet
    int    fail_windows  = 2;        // bad windows in a row for a verdict
};

class WheelCheck {
 public:
    // The torque each wheel was commanded for the period starting now, and
    // whether the drives were powered to deliver it.
    void commanded(const double torque[3], bool powered) {
        for (int i = 0; i < 3; ++i) { cmd_[i] = torque[i]; }
        powered_ = powered;
    }

    // One tachometer sample. Returns the axes currently judged FAILED (bit i =
    // axis i): `fail_windows` bad windows in a row, with no good one since.
    // `ignore` masks axes not to judge.
    uint8_t sample(double t, const double h[3], bool valid, uint8_t ignore,
                   const WheelCheckConfig& c) {
        if (!valid || !have_ || !powered_) {
            // A gap in the evidence: start a fresh window from here.
            start(t, h, valid);
            return failed_mask(ignore, c);
        }
        const double dt = t - last_t_;
        for (int i = 0; i < 3; ++i) {
            commanded_[i] += cmd_[i] * dt;
            if (std::fabs(h[i]) > 0.95 * c.max_h_nms ||
                std::fabs(cmd_[i]) > 0.999 * c.max_torque_nm) {
                excused_[i] = true;   // at a limit: disagreement proves nothing
            }
        }
        last_t_ = t;

        if (t - window_t0_ >= c.window_s - 1e-9) {
            for (int i = 0; i < 3; ++i) {
                residual_[i] = (h[i] - h0_[i]) - commanded_[i];
                if ((ignore >> i) & 1u) { bad_[i] = 0; continue; }
                if (excused_[i]) { continue; }
                if (std::fabs(residual_[i]) > c.threshold_nms) {
                    ++bad_[i];
                    good_[i] = 0;
                } else {
                    bad_[i] = 0;
                    ++good_[i];
                }
            }
            start(t, h, true);
        }
        return failed_mask(ignore, c);
    }

    // Forget every verdict in progress, e.g. after the drives were power-cycled.
    void reset() {
        have_ = false;
        for (int i = 0; i < 3; ++i) { bad_[i] = 0; good_[i] = 0; }
    }

    // Consecutive passing windows since the last reset, per axis.
    uint32_t good_windows(int axis) const { return good_[axis]; }
    // The disagreement over the last complete window, N*m*s, for telemetry.
    double residual(int axis) const { return residual_[axis]; }

 private:
    uint8_t failed_mask(uint8_t ignore, const WheelCheckConfig& c) const {
        uint8_t m = 0;
        for (int i = 0; i < 3; ++i) {
            if (!((ignore >> i) & 1u) && bad_[i] >= c.fail_windows) { m |= static_cast<uint8_t>(1u << i); }
        }
        return m;
    }

    void start(double t, const double h[3], bool valid) {
        have_ = valid;
        window_t0_ = last_t_ = t;
        for (int i = 0; i < 3; ++i) {
            h0_[i] = h[i];
            commanded_[i] = 0.0;
            excused_[i] = false;
        }
    }

    bool     have_    = false;
    bool     powered_ = false;
    double   window_t0_ = 0.0;
    double   last_t_    = 0.0;
    double   cmd_[3]{};
    double   h0_[3]{};
    double   commanded_[3]{};
    double   residual_[3]{};
    bool     excused_[3]{};
    int      bad_[3]{};
    uint32_t good_[3]{};
};

}  // namespace fsw::fdir
