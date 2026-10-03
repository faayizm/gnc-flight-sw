// ============================================================================
//  Tests for the space link: channel coding, TM framing, CLTU decoding and
//  FARM-1. The Reed-Solomon vectors come from Phil Karn's libfec
//  (encode_rs_ccsds), the reference implementation most CCSDS ground stations
//  descend from; matching it byte for byte is the evidence that the dual-basis
//  handling is right.
// ============================================================================
#include <cstring>

#include "apps/ttc/channel_coding.hpp"
#include "apps/ttc/tc_receiver.hpp"
#include "apps/ttc/tm_framer.hpp"
#include "core/crc.hpp"
#include "framework.hpp"

using namespace fsw::ttc;

namespace {

void hex(const char* s, uint8_t* out) {
    auto nib = [](char c) { return static_cast<uint8_t>(c <= '9' ? c - '0' : c - 'a' + 10); };
    for (size_t i = 0; s[2 * i] != 0; ++i) { out[i] = static_cast<uint8_t>((nib(s[2 * i]) << 4) | nib(s[2 * i + 1])); }
}

struct Sink {
    int    frames = 0;
    size_t last_len = 0;
    uint8_t last[1100]{};
    static void on(void* c, const uint8_t* d, size_t n) {
        auto* s = static_cast<Sink*>(c);
        ++s->frames;
        s->last_len = n;
        std::memcpy(s->last, d, n);
    }
};

size_t make_tc_frame(uint8_t* f, bool bypass, bool control, uint8_t ns, const uint8_t* data, size_t n) {
    const size_t len = kTcFrameHeaderBytes + n + 2;
    const uint16_t scid = fsw::dict::link::kScid;
    f[0] = static_cast<uint8_t>((bypass ? 0x20 : 0) | (control ? 0x10 : 0) | ((scid >> 8) & 0x03));
    f[1] = static_cast<uint8_t>(scid & 0xFF);
    f[2] = static_cast<uint8_t>((fsw::dict::link::kTcVc << 2) | (((len - 1) >> 8) & 0x03));
    f[3] = static_cast<uint8_t>((len - 1) & 0xFF);
    f[4] = ns;
    std::memcpy(f + 5, data, n);
    const uint16_t crc = fsw::core::crc16(f, len - 2);
    f[len - 2] = static_cast<uint8_t>(crc >> 8);
    f[len - 1] = static_cast<uint8_t>(crc & 0xFF);
    return len;
}

}  // namespace

TEST(coding, reed_solomon_matches_libfec_for_a_counting_pattern) {
    uint8_t data[223];
    for (size_t i = 0; i < 223; ++i) { data[i] = static_cast<uint8_t>(i); }
    uint8_t parity[32], expect[32];
    coding::rs_encode(data, parity);
    hex("4ffb92dd557ec67f27fb8982cf58f8fd028ad117fcef6b2793d0418826578651", expect);
    CHECK(std::memcmp(parity, expect, 32) == 0);
}

TEST(coding, reed_solomon_matches_libfec_for_pseudo_random_data) {
    uint8_t data[223];
    uint32_t s = 12345;
    for (size_t i = 0; i < 223; ++i) { s = s * 1103515245u + 12345u; data[i] = static_cast<uint8_t>(s >> 16); }
    uint8_t parity[32], expect[32];
    coding::rs_encode(data, parity);
    hex("a52b7b78c3a32a8315f0c1bd3b4aeb32e68728b5944bcc51fd18dad18e19ea4e", expect);
    CHECK(std::memcmp(parity, expect, 32) == 0);
}

TEST(coding, randomiser_sequence_starts_as_the_standard_says) {
    uint8_t z[8] = {};
    coding::randomise(z, 8);
    const uint8_t expect[8] = {0xFF, 0x48, 0x0E, 0xC0, 0x9A, 0x0D, 0x70, 0xBC};
    CHECK(std::memcmp(z, expect, 8) == 0);
}

TEST(coding, bch_corrects_any_single_bit_and_flags_double_errors) {
    uint8_t info[7] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE};
    uint8_t good[8];
    std::memcpy(good, info, 7);
    good[7] = coding::bch_parity(info);
    for (int bit = 0; bit < 63; ++bit) {
        uint8_t cb[8];
        std::memcpy(cb, good, 8);
        cb[bit / 8] = static_cast<uint8_t>(cb[bit / 8] ^ (0x80 >> (bit % 8)));
        CHECK(coding::bch_decode(cb) == coding::BchResult::Corrected);
        CHECK(std::memcmp(cb, good, 8) == 0);
    }
    uint8_t cb[8];
    std::memcpy(cb, good, 8);
    cb[0] ^= 0x81;   // two bits
    CHECK(coding::bch_decode(cb) == coding::BchResult::Uncorrectable);
}

TEST(tm_framer, a_cadu_has_the_asm_and_a_frame_that_decodes) {
    TmFramer f;
    uint8_t pkt[20];
    for (size_t i = 0; i < 20; ++i) { pkt[i] = static_cast<uint8_t>(i); }
    pkt[0] = 0x08; pkt[1] = 0x01;                // TM, APID 1
    pkt[4] = 0; pkt[5] = 20 - 7;
    CHECK(f.realtime().enqueue(pkt, sizeof pkt));
    uint8_t cadu[coding::kCaduBytes];
    // Not full yet and not old: nothing until the flush time.
    CHECK_EQ(f.next_cadu(0, 0x01000000u, false, cadu), 0u);
    const size_t n = f.next_cadu(TmVirtualChannel::kFlushTicks, 0x01000000u, false, cadu);
    CHECK_EQ(n, coding::kCaduBytes);
    CHECK(std::memcmp(cadu, coding::kAsm, 4) == 0);

    uint8_t* frame = cadu + 4;
    coding::randomise(frame, coding::kRsN);           // de-randomise
    uint8_t parity[32];
    coding::rs_encode(frame, parity);
    CHECK(std::memcmp(parity, frame + 223, 32) == 0); // codeword is intact
    const uint16_t scid = static_cast<uint16_t>(((frame[0] & 0x3F) << 4) | (frame[1] >> 4));
    CHECK_EQ(scid, fsw::dict::link::kScid);
    const uint16_t fhp = static_cast<uint16_t>(((frame[4] & 0x07) << 8) | frame[5]);
    CHECK_EQ(fhp, 0u);                                // our packet starts the data field
    CHECK(std::memcmp(frame + 6, pkt, 20) == 0);
    CHECK_EQ(frame[6 + 20], 0x07);                    // then an idle packet (APID 0x7FF)
}

TEST(tm_framer, a_long_packet_spans_frames_and_the_pointer_says_so) {
    TmFramer f;
    uint8_t big[300] = {};
    big[0] = 0x08; big[1] = 0x01; big[4] = (300 - 7) >> 8; big[5] = (300 - 7) & 0xFF;
    uint8_t small[10] = {0x08, 0x02, 0xC0, 0, 0, 3};
    f.realtime().enqueue(big, sizeof big);
    f.realtime().enqueue(small, sizeof small);
    uint8_t c1[coding::kCaduBytes], c2[coding::kCaduBytes];
    f.next_cadu(0, 0, false, c1);
    f.next_cadu(TmVirtualChannel::kFlushTicks, 0, false, c2);
    coding::randomise(c2 + 4, coding::kRsN);
    const uint16_t fhp = static_cast<uint16_t>(((c2[8] & 0x07) << 8) | c2[9]);
    CHECK_EQ(fhp, 300u - kTmDataBytes);   // the small packet starts after the big one's tail
}

TEST(tc_receiver, a_clean_cltu_delivers_its_frame) {
    Sink s;
    TcReceiver rx(&Sink::on, &s);
    const uint8_t payload[9] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    uint8_t frame[64], cltu[128];
    const size_t fl = make_tc_frame(frame, false, false, 0, payload, sizeof payload);
    const size_t cl = TcReceiver::encode_cltu(frame, fl, cltu, sizeof cltu);
    const uint8_t junk[5] = {0xEB, 0x00, 0x13, 0xEB, 0x91};   // garbage first: the hunt must cope
    rx.push(junk, sizeof junk);
    rx.push(cltu, cl);
    CHECK_EQ(s.frames, 1);
    CHECK_EQ(s.last_len, sizeof payload);
    CHECK_EQ(rx.farm().vr(), 1);
}

TEST(tc_receiver, a_flipped_bit_on_the_uplink_is_corrected) {
    Sink s;
    TcReceiver rx(&Sink::on, &s);
    const uint8_t payload[20] = {9, 9, 9};
    uint8_t frame[64], cltu[128];
    const size_t cl = TcReceiver::encode_cltu(frame, make_tc_frame(frame, false, false, 0, payload, 20), cltu, sizeof cltu);
    cltu[5] ^= 0x10;
    cltu[13] ^= 0x01;
    rx.push(cltu, cl);
    CHECK_EQ(s.frames, 1);
    CHECK_EQ(rx.bits_corrected(), 2u);
}

TEST(farm1, frames_are_accepted_only_in_sequence) {
    Farm1 f;
    CHECK(f.on_ad(0) == Farm1::Verdict::Accept);
    CHECK(f.on_ad(2) == Farm1::Verdict::Discard);   // frame 1 went missing
    CHECK(f.retransmit());
    CHECK_EQ(f.vr(), 1);
    CHECK(f.on_ad(1) == Farm1::Verdict::Accept);    // the retransmission
    CHECK(!f.retransmit());
    CHECK(f.on_ad(1) == Farm1::Verdict::Discard);   // a duplicate: ignored, no lockout
    CHECK(!f.lockout());
    CHECK(f.on_ad(100) == Farm1::Verdict::Discard); // far outside the window
    CHECK(f.lockout());
    CHECK(f.on_ad(2) == Farm1::Verdict::Discard);   // locked out: nothing gets in
    const uint8_t unlock[1] = {0x00};
    CHECK(f.on_bc(unlock, 1));
    CHECK(!f.lockout());
    const uint8_t set_vr[3] = {0x82, 0x00, 50};
    CHECK(f.on_bc(set_vr, 3));
    CHECK_EQ(f.vr(), 50);
    CHECK_EQ(f.clcw() & 0xFFu, 50u);
    CHECK_EQ((f.clcw() >> 24) & 0x3u, 1u);          // COP-1 in effect
}
