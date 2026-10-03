// ============================================================================
//  fsw/apps/ttc/tm_framer.hpp -- TM Space Data Link Protocol (CCSDS 132.0-B).
//
//  Packets go in; fixed-length transfer frames come out, each Reed-Solomon
//  coded, randomised and preceded by the attached sync marker (a "CADU").
//
//  FRAME LAYOUT (223 bytes, one RS codeword):
//
//      +--------------- primary header (6) ---------------+
//      | ver 2 | SCID 10 | VCID 3 | OCF 1 | MC count 8 | VC count 8 |
//      | sec hdr 1 | sync 1 | order 1 | seg len id 2 | first header ptr 11 |
//      +--------------------------------------------------+
//      | data field (213): packets, back to back, spanning frames          |
//      +--------------------------------------------------+
//      | operational control field (4): the CLCW, COP-1's report          |
//      +--------------------------------------------------+
//
//  PACKETS SPAN FRAMES. A packet starts wherever the previous one ended, and
//  carries on into the next frame of the same virtual channel if it does not
//  fit. The first header pointer says where the first packet *starting* in
//  this frame begins -- which is how a receiver that lost the previous frame
//  finds its footing again in this one. 0x7FF means "no packet starts here";
//  0x7FE means "idle data only".
//
//  VIRTUAL CHANNELS separate traffic that must not wait behind each other:
//  live telemetry on one, playback from storage on another, idle frames on a
//  third. Each has its own frame counter, so the ground can tell which
//  stream lost a frame.
//
//  LATENCY. A frame is sent when its data field is full, or when the oldest
//  byte in it has waited `kFlushTicks` -- then the rest is filled with an idle
//  packet. A frame-filling rule alone would hold a lone event report until
//  enough other telemetry arrived to push it out.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

#include "apps/ttc/channel_coding.hpp"
#include "apps/ttc/space_packet.hpp"
#include "generated/dictionary.hpp"

namespace fsw::ttc {

constexpr size_t kTmFrameBytes     = dict::link::kTmFrameBytes;
constexpr size_t kTmHeaderBytes    = 6;
constexpr size_t kTmOcfBytes       = 4;
constexpr size_t kTmDataBytes      = kTmFrameBytes - kTmHeaderBytes - kTmOcfBytes;
constexpr uint16_t kFhpNoPacket    = 0x7FF;
constexpr uint16_t kFhpIdleOnly    = 0x7FE;
static_assert(kTmFrameBytes == coding::kRsK, "one TM frame per RS codeword");

class TmVirtualChannel {
 public:
    static constexpr size_t kFifoBytes = 16384;

    explicit TmVirtualChannel(uint8_t vcid) : vcid_(vcid) {}

    // Queue one whole packet. False (and counted) if it does not fit: a full
    // FIFO means the downlink is slower than telemetry is being produced, and
    // dropping new packets is safer than corrupting ones already queued.
    bool enqueue(const uint8_t* packet, size_t length);

    // True when a frame should go out now: a full data field is waiting, or
    // something has waited longer than the flush limit.
    bool ready(uint32_t tick) const;

    // Fill a frame's data field. Returns the first header pointer.
    uint16_t fill(uint8_t* data_field, bool flush);

    uint8_t  vcid()          const { return vcid_; }
    uint8_t  next_count()          { return vc_count_++; }
    size_t   pending_bytes() const { return used_ + (carry_len_ - carry_pos_); }
    uint32_t dropped()       const { return dropped_; }

    void note_tick(uint32_t tick) { if (pending_bytes() == 0) { oldest_tick_ = tick; } }

    // Send whatever is queued in the next frame, without waiting for the
    // flush time. Used for time reports, whose value to the ground depends on
    // how quickly they arrive.
    void expedite() { expedite_ = true; }

    static constexpr uint32_t kFlushTicks = 5;   // 100 ms at 50 Hz

 private:
    bool pop_packet();                   // move the next whole packet into carry_
    void make_idle_packet(size_t length);

    uint8_t  vcid_;
    uint8_t  vc_count_ = 0;

    // Ring of queued packets, each stored with a 2-byte length prefix.
    uint8_t  fifo_[kFifoBytes]{};
    size_t   head_ = 0;
    size_t   used_ = 0;

    // The packet currently being cut into frames.
    uint8_t  carry_[kMaxPacketBytes + kTmDataBytes]{};
    size_t   carry_len_ = 0;
    size_t   carry_pos_ = 0;

    uint32_t oldest_tick_ = 0;
    uint32_t dropped_ = 0;
    bool     expedite_ = false;
};

class TmFramer {
 public:
    TmFramer()
        : realtime_(static_cast<uint8_t>(dict::link::kVcRealtime)),
          playback_(static_cast<uint8_t>(dict::link::kVcPlayback)) {}

    TmVirtualChannel& realtime() { return realtime_; }
    TmVirtualChannel& playback() { return playback_; }

    // Build the next CADU to transmit, if any is due. Realtime telemetry goes
    // first; playback uses the capacity it leaves. `idle` forces an idle frame
    // when nothing else is due, so the CLCW keeps flowing to the ground even
    // when the spacecraft has nothing to say. Returns the CADU length or 0.
    size_t next_cadu(uint32_t tick, uint32_t clcw, bool idle, uint8_t* out);

    uint32_t frames_sent() const { return frames_sent_; }

    // Exposed for tests: build the plain frame (no coding).
    static void write_header(uint8_t* frame, uint8_t vcid, uint8_t mc, uint8_t vc, uint16_t fhp);

 private:
    TmVirtualChannel realtime_;
    TmVirtualChannel playback_;
    uint8_t  mc_count_ = 0;
    uint8_t  idle_vc_count_ = 0;
    uint32_t frames_sent_ = 0;
};

}  // namespace fsw::ttc
