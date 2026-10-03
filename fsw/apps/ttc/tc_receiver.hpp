// ============================================================================
//  fsw/apps/ttc/tc_receiver.hpp -- the uplink, from raw bytes to telecommands.
//
//  Three layers, each discarding what it cannot trust:
//
//    CLTU (CCSDS 231.0-B). Hunt for the 0xEB90 start sequence, then take
//    8-byte BCH codeblocks, correcting single-bit errors, until the tail
//    sequence -- or until a codeblock is beyond repair, which ends the CLTU.
//    The hunt is what gives the uplink framing recovery: after any amount of
//    garbage, the next start sequence is a clean place to begin again.
//
//    TC TRANSFER FRAME (CCSDS 232.0-B). Five-byte header, data, CRC-16.
//    Wrong spacecraft, wrong virtual channel, bad length or bad CRC: dropped
//    without a word, because none of its fields can be trusted to say who to
//    complain to.
//
//    FARM-1 (CCSDS 232.1-B, the receiving half of COP-1). Sequence-controlled
//    (type AD) frames are accepted strictly in order: N(S) must equal the
//    expected V(R). A frame from the future means one went missing, so it is
//    discarded and the retransmit flag raised; a frame from the past is a
//    duplicate and is discarded quietly; anything far outside the window
//    locks the FARM out until the ground sends Unlock. The ground learns all
//    of this from the CLCW in every downlinked frame, and its FOP-1 repairs
//    the sequence by retransmitting. Bypass (type BD) frames skip all of this,
//    for when the sequence itself is broken.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

#include "apps/ttc/channel_coding.hpp"
#include "generated/dictionary.hpp"

namespace fsw::ttc {

constexpr size_t kTcFrameHeaderBytes = 5;
constexpr size_t kTcMaxFrameBytes    = dict::link::kTcMaxFrameBytes;

class Farm1 {
 public:
    enum class Verdict { Accept, Discard };

    // Type-AD frame with sequence number ns.
    Verdict on_ad(uint8_t ns);
    // Type-BD frame (always accepted).
    void on_bd() { ++farm_b_; }
    // Type-BC control command. Returns false if it is not one FARM-1 knows.
    bool on_bc(const uint8_t* data, size_t length);

    // The Communications Link Control Word, carried in every TM frame's OCF.
    uint32_t clcw() const;

    uint8_t vr()        const { return vr_; }
    bool    lockout()   const { return lockout_; }
    bool    retransmit() const { return retransmit_; }
    uint32_t lockouts() const { return lockouts_; }

 private:
    static constexpr uint8_t kWindow = static_cast<uint8_t>(dict::link::kFarmWindow);

    uint8_t  vr_ = 0;
    bool     lockout_ = false;
    bool     retransmit_ = false;
    bool     wait_ = false;     // never set: frames are processed immediately
    uint8_t  farm_b_ = 0;
    uint32_t lockouts_ = 0;
};

class TcReceiver {
 public:
    // Called once per accepted frame with the frame's data field: the
    // telecommand packet(s) it carries.
    using PacketSink = void (*)(void* context, const uint8_t* data, size_t length);

    TcReceiver(PacketSink sink, void* context) : sink_(sink), context_(context) {}

    // Feed raw uplink bytes, in any chunking.
    void push(const uint8_t* bytes, size_t n);

    // Forget any partial CLTU (the link dropped).
    void reset() { state_ = State::Hunt; hunt_ = 0; cb_used_ = 0; frame_used_ = 0; }

    const Farm1& farm() const { return farm_; }

    // Statistics for housekeeping.
    uint32_t cltus()             const { return cltus_; }
    uint32_t bits_corrected()    const { return bits_corrected_; }
    uint32_t frames_accepted()   const { return frames_accepted_; }
    uint32_t frames_rejected()   const { return frames_rejected_; }

    // Exposed for tests and the ground model: CLTU-encode a frame.
    static size_t encode_cltu(const uint8_t* frame, size_t length, uint8_t* out, size_t capacity);

 private:
    enum class State { Hunt, Codeblocks };

    void end_cltu();
    void process_frame(const uint8_t* f, size_t length);

    PacketSink sink_;
    void*      context_;
    Farm1      farm_;

    State   state_ = State::Hunt;
    uint8_t hunt_ = 0;                        // bytes of the start sequence matched
    uint8_t cb_[coding::kCodeblockBytes]{};
    size_t  cb_used_ = 0;
    uint8_t frame_[kTcMaxFrameBytes + coding::kCodeblockInfo]{};
    size_t  frame_used_ = 0;

    uint32_t cltus_ = 0;
    uint32_t bits_corrected_ = 0;
    uint32_t frames_accepted_ = 0;
    uint32_t frames_rejected_ = 0;
};

}  // namespace fsw::ttc
