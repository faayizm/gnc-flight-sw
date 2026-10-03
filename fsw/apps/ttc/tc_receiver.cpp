// ============================================================================
//  fsw/apps/ttc/tc_receiver.cpp
// ============================================================================
#include "apps/ttc/tc_receiver.hpp"

#include <cstring>

#include "core/crc.hpp"

namespace fsw::ttc {

// ---- FARM-1 ----------------------------------------------------------------

Farm1::Verdict Farm1::on_ad(uint8_t ns) {
    if (lockout_) { return Verdict::Discard; }
    if (ns == vr_) {
        ++vr_;
        retransmit_ = false;
        return Verdict::Accept;
    }
    // Modulo-256 distance ahead of the expected number.
    const auto ahead = static_cast<uint8_t>(ns - vr_);
    const uint8_t half = kWindow / 2;
    if (ahead > 0 && ahead <= half) {
        retransmit_ = true;           // a gap: ask for everything from V(R)
    } else if (static_cast<uint8_t>(vr_ - ns) <= half) {
        // Already received: a retransmission that crossed our CLCW. Ignore.
    } else {
        lockout_ = true;              // nowhere near the window: stop and wait for the ground
        ++lockouts_;
    }
    return Verdict::Discard;
}

bool Farm1::on_bc(const uint8_t* d, size_t n) {
    if (n == 1 && d[0] == 0x00) {                       // Unlock
        lockout_ = false;
        wait_ = false;
        retransmit_ = false;
    } else if (n == 3 && d[0] == 0x82 && d[1] == 0x00) { // Set V(R)
        if (lockout_) { return true; }                  // accepted, but no effect while locked out
        vr_ = d[2];
        wait_ = false;
        retransmit_ = false;
    } else {
        return false;
    }
    ++farm_b_;
    return true;
}

uint32_t Farm1::clcw() const {
    // type 0, version 00, status 000, COP in effect 01, VCID (6), spare 00 |
    // no RF 0, no bit lock 0, lockout, wait, retransmit, FARM-B count (2), spare 0 | V(R)
    uint32_t w = 0;
    w |= 1u << 24;                                         // COP-1 in effect
    w |= static_cast<uint32_t>(dict::link::kTcVc & 0x3F) << 18;
    w |= (lockout_ ? 1u : 0u) << 13;
    w |= (wait_ ? 1u : 0u) << 12;
    w |= (retransmit_ ? 1u : 0u) << 11;
    w |= static_cast<uint32_t>(farm_b_ & 0x3) << 9;
    w |= vr_;
    return w;
}

// ---- CLTU decoding ---------------------------------------------------------

void TcReceiver::push(const uint8_t* bytes, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        const uint8_t b = bytes[i];
        if (state_ == State::Hunt) {
            if (b == coding::kCltuStart[hunt_]) {
                if (++hunt_ == sizeof coding::kCltuStart) {
                    state_ = State::Codeblocks;
                    hunt_ = 0;
                    cb_used_ = 0;
                    frame_used_ = 0;
                    ++cltus_;
                }
            } else {
                hunt_ = (b == coding::kCltuStart[0]) ? 1 : 0;
            }
            continue;
        }

        cb_[cb_used_++] = b;
        if (cb_used_ < coding::kCodeblockBytes) { continue; }
        cb_used_ = 0;

        if (std::memcmp(cb_, coding::kCltuTail, sizeof coding::kCltuTail) == 0) {
            end_cltu();
            continue;
        }
        const coding::BchResult r = coding::bch_decode(cb_);
        if (r == coding::BchResult::Uncorrectable) {
            // The CLTU ends here. Whatever was decoded so far is still offered
            // to the frame layer, whose length and CRC checks decide its fate.
            end_cltu();
            continue;
        }
        if (r == coding::BchResult::Corrected) { ++bits_corrected_; }
        if (frame_used_ + coding::kCodeblockInfo <= sizeof frame_) {
            std::memcpy(frame_ + frame_used_, cb_, coding::kCodeblockInfo);
            frame_used_ += coding::kCodeblockInfo;
        } else {
            end_cltu();     // longer than any frame we accept: cannot be valid
        }
    }
}

void TcReceiver::end_cltu() {
    // A CLTU may carry several frames back to back, then fill bytes.
    size_t pos = 0;
    while (frame_used_ - pos >= kTcFrameHeaderBytes + 2) {
        const uint8_t* f = frame_ + pos;
        const size_t length = (((static_cast<size_t>(f[2]) & 0x03) << 8) | f[3]) + 1;
        if (length < kTcFrameHeaderBytes + 2 || length > kTcMaxFrameBytes || pos + length > frame_used_) {
            if (pos == 0) { ++frames_rejected_; }
            break;
        }
        process_frame(f, length);
        pos += length;
    }
    state_ = State::Hunt;
    hunt_ = 0;
    frame_used_ = 0;
}

void TcReceiver::process_frame(const uint8_t* f, size_t length) {
    const uint8_t  version = static_cast<uint8_t>(f[0] >> 6);
    const bool     bypass  = (f[0] & 0x20) != 0;
    const bool     control = (f[0] & 0x10) != 0;
    const uint16_t scid    = static_cast<uint16_t>(((f[0] & 0x03) << 8) | f[1]);
    const uint8_t  vcid    = static_cast<uint8_t>(f[2] >> 2);
    const uint8_t  ns      = f[4];

    if (version != 0 || scid != dict::link::kScid || vcid != dict::link::kTcVc ||
        !core::crc16_check(f, length)) {
        ++frames_rejected_;
        return;
    }
    const uint8_t* data = f + kTcFrameHeaderBytes;
    const size_t   data_len = length - kTcFrameHeaderBytes - 2;

    if (control) {
        if (bypass && farm_.on_bc(data, data_len)) { ++frames_accepted_; } else { ++frames_rejected_; }
        return;
    }
    if (bypass) {
        farm_.on_bd();
    } else if (farm_.on_ad(ns) != Farm1::Verdict::Accept) {
        ++frames_rejected_;
        return;
    }
    ++frames_accepted_;
    sink_(context_, data, data_len);
}

size_t TcReceiver::encode_cltu(const uint8_t* frame, size_t length, uint8_t* out, size_t cap) {
    const size_t blocks = (length + coding::kCodeblockInfo - 1) / coding::kCodeblockInfo;
    const size_t total = 2 + blocks * coding::kCodeblockBytes + sizeof coding::kCltuTail;
    if (total > cap) { return 0; }
    size_t o = 0;
    out[o++] = coding::kCltuStart[0];
    out[o++] = coding::kCltuStart[1];
    for (size_t b = 0; b < blocks; ++b) {
        uint8_t info[coding::kCodeblockInfo];
        for (size_t i = 0; i < coding::kCodeblockInfo; ++i) {
            const size_t k = b * coding::kCodeblockInfo + i;
            info[i] = (k < length) ? frame[k] : 0x55;    // fill pattern
        }
        std::memcpy(out + o, info, coding::kCodeblockInfo);
        out[o + coding::kCodeblockInfo] = coding::bch_parity(info);
        o += coding::kCodeblockBytes;
    }
    std::memcpy(out + o, coding::kCltuTail, sizeof coding::kCltuTail);
    return o + sizeof coding::kCltuTail;
}

}  // namespace fsw::ttc
