// ============================================================================
//  fsw/apps/ttc/tm_framer.cpp
// ============================================================================
#include "apps/ttc/tm_framer.hpp"

#include <cstring>

namespace fsw::ttc {

namespace {
constexpr uint16_t kIdleApid = 0x7FF;
constexpr uint8_t  kIdleFill = 0x55;
}

bool TmVirtualChannel::enqueue(const uint8_t* packet, size_t length) {
    if (length == 0 || length > kMaxPacketBytes || used_ + length + 2 > kFifoBytes) {
        ++dropped_;
        return false;
    }
    size_t tail = (head_ + used_) % kFifoBytes;
    const uint8_t prefix[2] = {static_cast<uint8_t>(length >> 8), static_cast<uint8_t>(length & 0xFF)};
    for (size_t i = 0; i < 2; ++i) { fifo_[tail] = prefix[i]; tail = (tail + 1) % kFifoBytes; }
    for (size_t i = 0; i < length; ++i) { fifo_[tail] = packet[i]; tail = (tail + 1) % kFifoBytes; }
    used_ += length + 2;
    return true;
}

bool TmVirtualChannel::pop_packet() {
    if (used_ < 2) { return false; }
    const size_t length = (static_cast<size_t>(fifo_[head_]) << 8) | fifo_[(head_ + 1) % kFifoBytes];
    head_ = (head_ + 2) % kFifoBytes;
    for (size_t i = 0; i < length; ++i) { carry_[i] = fifo_[(head_ + i) % kFifoBytes]; }
    head_ = (head_ + length) % kFifoBytes;
    used_ -= length + 2;
    carry_len_ = length;
    carry_pos_ = 0;
    return true;
}

void TmVirtualChannel::make_idle_packet(size_t length) {
    // CCSDS idle packet: APID all ones, any content. Length is at least 7,
    // the smallest legal packet; if that overruns this frame, the remainder
    // simply continues into the next one like any other spanning packet.
    if (length < 7) { length = 7; }
    const size_t data_len_minus_one = length - kSpacePacketHeaderBytes - 1;
    carry_[0] = static_cast<uint8_t>(kIdleApid >> 8);   // version 0, type TM, no sec hdr
    carry_[1] = static_cast<uint8_t>(kIdleApid & 0xFF);
    carry_[2] = 0xC0;                                    // unsegmented, count 0
    carry_[3] = 0x00;
    carry_[4] = static_cast<uint8_t>(data_len_minus_one >> 8);
    carry_[5] = static_cast<uint8_t>(data_len_minus_one & 0xFF);
    std::memset(carry_ + kSpacePacketHeaderBytes, kIdleFill, length - kSpacePacketHeaderBytes);
    carry_len_ = length;
    carry_pos_ = 0;
}

bool TmVirtualChannel::ready(uint32_t tick) const {
    const size_t pending = pending_bytes();
    if (pending == 0) { return false; }
    return expedite_ || pending >= kTmDataBytes || (tick - oldest_tick_) >= kFlushTicks;
}

uint16_t TmVirtualChannel::fill(uint8_t* data, bool flush) {
    expedite_ = false;
    uint16_t fhp = kFhpNoPacket;
    size_t pos = 0;
    while (pos < kTmDataBytes) {
        if (carry_pos_ < carry_len_) {
            size_t n = carry_len_ - carry_pos_;
            if (n > kTmDataBytes - pos) { n = kTmDataBytes - pos; }
            std::memcpy(data + pos, carry_ + carry_pos_, n);
            carry_pos_ += n;
            pos += n;
            continue;
        }
        // A new packet starts at `pos`.
        if (!pop_packet()) {
            if (!flush) { break; }
            make_idle_packet(kTmDataBytes - pos);
        }
        if (fhp == kFhpNoPacket) { fhp = static_cast<uint16_t>(pos); }
    }
    return fhp;
}

void TmFramer::write_header(uint8_t* f, uint8_t vcid, uint8_t mc, uint8_t vc, uint16_t fhp) {
    const uint16_t scid = dict::link::kScid;
    // version 00, SCID (10), VCID (3), OCF flag 1
    f[0] = static_cast<uint8_t>((scid >> 4) & 0x3F);
    f[1] = static_cast<uint8_t>(((scid & 0x0F) << 4) | ((vcid & 0x07) << 1) | 0x01);
    f[2] = mc;
    f[3] = vc;
    // no secondary header, sync flag 0, order 0, segment length id 11, FHP
    f[4] = static_cast<uint8_t>(0x18 | ((fhp >> 8) & 0x07));
    f[5] = static_cast<uint8_t>(fhp & 0xFF);
}

size_t TmFramer::next_cadu(uint32_t tick, uint32_t clcw, bool idle, uint8_t* out) {
    realtime_.note_tick(tick);
    playback_.note_tick(tick);

    TmVirtualChannel* vc = nullptr;
    if (realtime_.ready(tick)) {
        vc = &realtime_;
    } else if (playback_.pending_bytes() > 0) {
        vc = &playback_;    // playback is never latency-critical: send it whenever there is room
    } else if (!idle) {
        return 0;
    }

    uint8_t* frame = out + sizeof coding::kAsm;
    if (vc != nullptr) {
        const uint16_t fhp = vc->fill(frame + kTmHeaderBytes, true);
        write_header(frame, vc->vcid(), mc_count_++, vc->next_count(), fhp);
    } else {
        std::memset(frame + kTmHeaderBytes, kIdleFill, kTmDataBytes);
        write_header(frame, static_cast<uint8_t>(dict::link::kVcIdle), mc_count_++,
                     idle_vc_count_++, kFhpIdleOnly);
    }
    uint8_t* ocf = frame + kTmHeaderBytes + kTmDataBytes;
    ocf[0] = static_cast<uint8_t>(clcw >> 24);
    ocf[1] = static_cast<uint8_t>(clcw >> 16);
    ocf[2] = static_cast<uint8_t>(clcw >> 8);
    ocf[3] = static_cast<uint8_t>(clcw);

    coding::rs_encode(frame, frame + kTmFrameBytes);
    coding::randomise(frame, coding::kRsN);
    std::memcpy(out, coding::kAsm, sizeof coding::kAsm);
    ++frames_sent_;
    return coding::kCaduBytes;
}

}  // namespace fsw::ttc
