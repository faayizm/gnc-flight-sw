// ============================================================================
//  fsw/apps/ttc/pus.cpp -- PUS secondary headers, TC validation, TM assembly.
//  See pus.hpp for the standard's structure and which services are present.
// ============================================================================
#include "apps/ttc/pus.hpp"

namespace fsw::ttc {

// ---------------------------------------------------------------------------
// Secondary headers
// ---------------------------------------------------------------------------

bool PusTmHeader::encode(core::ByteWriter& w) const {
    const uint8_t byte0 = static_cast<uint8_t>(((version & 0x0F) << 4) |
                                               (time_status & 0x0F));
    return w.write_uint8(byte0) &&
           w.write_uint8(service) &&
           w.write_uint8(subtype) &&
           w.write_uint16(message_count) &&
           w.write_uint16(destination) &&
           w.write_uint32(time.coarse) &&
           w.write_uint16(time.fine);
}

bool PusTmHeader::decode(core::ByteReader& r) {
    uint8_t byte0 = 0;
    if (!r.read_uint8(byte0)) { return false; }
    version     = static_cast<uint8_t>((byte0 >> 4) & 0x0F);
    time_status = static_cast<uint8_t>(byte0 & 0x0F);
    return r.read_uint8(service) &&
           r.read_uint8(subtype) &&
           r.read_uint16(message_count) &&
           r.read_uint16(destination) &&
           r.read_uint32(time.coarse) &&
           r.read_uint16(time.fine);
}

bool PusTcHeader::encode(core::ByteWriter& w) const {
    const uint8_t byte0 = static_cast<uint8_t>(((version & 0x0F) << 4) |
                                               (ack_flags & 0x0F));
    return w.write_uint8(byte0) &&
           w.write_uint8(service) &&
           w.write_uint8(subtype) &&
           w.write_uint16(source_id);
}

bool PusTcHeader::decode(core::ByteReader& r) {
    uint8_t byte0 = 0;
    if (!r.read_uint8(byte0)) { return false; }
    version   = static_cast<uint8_t>((byte0 >> 4) & 0x0F);
    ack_flags = static_cast<uint8_t>(byte0 & 0x0F);
    return r.read_uint8(service) &&
           r.read_uint8(subtype) &&
           r.read_uint16(source_id);
}

// ---------------------------------------------------------------------------
// Telecommand validation
//
// The order of these checks is deliberate and is itself a piece of the design:
// integrity first, then structure, then meaning. A packet whose CRC fails is
// not interpreted AT ALL -- its service and subtype fields are not to be
// trusted, so it is never dispatched anywhere, only counted and reported.
// ---------------------------------------------------------------------------

core::Status parse_tc(const uint8_t* data, size_t length,
                      ReceivedTc& out, core::FailureCode& failure) {
    failure = core::FailureCode::Ok;

    // 1. Is there even enough here to be a telecommand?
    const size_t minimum = kSpacePacketHeaderBytes + kPusTcHeaderBytes + kCrcBytes;
    if (data == nullptr || length < minimum) {
        failure = core::FailureCode::BadLength;
        return core::Status::Invalid;
    }
    if (length > kMaxPacketBytes) {
        failure = core::FailureCode::BadLength;
        return core::Status::Invalid;
    }

    // 2. Integrity, before interpreting a single field. CRC over the whole
    //    packet including its own check field must come out zero.
    if (!core::crc16_check(data, length)) {
        failure = core::FailureCode::BadCrc;
        return core::Status::Invalid;
    }

    // 3. Structure.
    core::ByteReader r(data, length);
    if (!out.primary.decode(r)) {
        failure = core::FailureCode::BadLength;
        return core::Status::Invalid;
    }
    if (out.primary.total_size() != length) {
        // The declared length disagrees with what actually arrived. The CRC
        // passed, so this is not corruption -- it is a malformed sender.
        failure = core::FailureCode::BadLength;
        return core::Status::Invalid;
    }
    if (out.primary.type != PacketType::Telecommand || !out.primary.secondary_hdr) {
        failure = core::FailureCode::BadLength;
        return core::Status::Invalid;
    }
    if (!out.secondary.decode(r)) {
        failure = core::FailureCode::BadLength;
        return core::Status::Invalid;
    }
    if (out.secondary.version != kPusVersion) {
        failure = core::FailureCode::UnknownService;
        return core::Status::Invalid;
    }

    // 4. What is left, minus the trailing CRC, is the argument block.
    const size_t consumed = kSpacePacketHeaderBytes + kPusTcHeaderBytes;
    if (length < consumed + kCrcBytes) {
        failure = core::FailureCode::BadLength;
        return core::Status::Invalid;
    }
    out.args_size = length - consumed - kCrcBytes;
    out.args      = (out.args_size > 0) ? (data + consumed) : nullptr;
    return core::Status::Ok;
}

// ---------------------------------------------------------------------------
// Telemetry assembly
// ---------------------------------------------------------------------------

bool TmBuilder::begin(uint16_t apid, uint16_t sequence_count, Service service,
                      uint8_t subtype, uint16_t message_count,
                      const core::CucTime& time) {
    writer_.reset();
    started_ = false;

    SpacePacketHeader primary;
    primary.version        = 0;
    primary.type           = PacketType::Telemetry;
    primary.secondary_hdr  = true;
    primary.apid           = apid;
    primary.sequence_flags = kSeqFlagsUnsegmented;
    primary.sequence_count = sequence_count;
    primary.data_length    = 0;   // provisional, back-patched by finish()

    // Write the first four header octets, then remember where the length field
    // lands so finish() can correct it without re-encoding anything.
    const uint16_t word0 =
        static_cast<uint16_t>((0u << 13) | (0u << 12) | (1u << 11) | (apid & kApidMask));
    const uint16_t word1 =
        static_cast<uint16_t>((static_cast<uint16_t>(kSeqFlagsUnsegmented) << 14) |
                              (sequence_count & kSeqCountMask));
    if (!writer_.write_uint16(word0) || !writer_.write_uint16(word1)) { return false; }

    length_field_ = writer_.reserve(2);
    if (length_field_ == nullptr) { return false; }

    PusTmHeader secondary;
    secondary.service       = static_cast<uint8_t>(service);
    secondary.subtype       = subtype;
    secondary.message_count = message_count;
    secondary.destination   = 0;
    secondary.time          = time;
    // Time reference status 0 means "not synchronised with a ground clock".
    // Phase 4 sets this once ST[09] time correlation is implemented; reporting
    // it honestly matters, because a ground system must know whether a
    // timestamp can be trusted for correlation.
    secondary.time_status   = 0;
    if (!secondary.encode(writer_)) { return false; }

    started_ = true;
    return true;
}

size_t TmBuilder::finish() {
    if (!started_ || !writer_.ok() || length_field_ == nullptr) { return 0; }

    // Data field is everything after the 6-octet primary header, plus the CRC
    // that is about to be appended.
    const size_t data_field = writer_.size() - kSpacePacketHeaderBytes + kCrcBytes;
    if (data_field == 0 || data_field > 65536) { return 0; }

    const uint16_t length_value = static_cast<uint16_t>(data_field - 1);
    length_field_[0] = static_cast<uint8_t>(length_value >> 8);
    length_field_[1] = static_cast<uint8_t>(length_value & 0xFF);

    const uint16_t crc = core::crc16(writer_.data(), writer_.size());
    if (!writer_.write_uint16(crc)) { return 0; }

    started_ = false;
    return writer_.size();
}

}  // namespace fsw::ttc
