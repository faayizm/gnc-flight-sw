// ============================================================================
//  fsw/apps/ttc/ttc_app.cpp -- see ttc_app.hpp for the application's role.
// ============================================================================
#include "apps/ttc/ttc_app.hpp"

#include <cstring>

namespace fsw::ttc {

namespace {
// Subtypes of the ST[01] verification service.
constexpr uint8_t kVerifAcceptSuccess   = 1;
constexpr uint8_t kVerifAcceptFailure   = 2;
constexpr uint8_t kVerifCompleteSuccess = 7;
constexpr uint8_t kVerifCompleteFailure = 8;
}  // namespace

TtcApp::TtcApp(hal::ILink& link, hal::IClock& clock, core::Bus& bus,
               core::EventLog& events, core::ParamStore& params,
               const core::Scheduler& scheduler)
    : link_(link), clock_(clock), bus_(bus), events_(events),
      params_(params), scheduler_(scheduler) {}

core::Status TtcApp::init() {
    // Every housekeeping structure in the dictionary starts enabled. An
    // operator can silence any of them with ST[3,6] during a busy pass.
    const dict::HkSid sids[] = {
        dict::HkSid::SYS_HK,
        dict::HkSid::ADCS_HK,
        dict::HkSid::EPS_HK,
    };
    for (dict::HkSid sid : sids) {
        HkState s;
        s.sid      = sid;
        s.enabled  = true;
        s.next_due = 0.0;
        if (!hk_.push_back(s)) { return core::Status::NoSpace; }
    }

    events_.set_sink(&TtcApp::event_sink, this);
    events_.set_time_source(&TtcApp::mission_time, this);

    core::Status s = bus_.subscribe(core::Topic::AdcsHk, &TtcApp::on_adcs_hk, this);
    if (!core::is_ok(s)) { return s; }
    s = bus_.subscribe(core::Topic::EpsHk, &TtcApp::on_eps_hk, this);
    if (!core::is_ok(s)) { return s; }

    return core::Status::Ok;
}

double TtcApp::mission_time(void* context) {
    return static_cast<TtcApp*>(context)->clock_.mission_time_s();
}

// ---------------------------------------------------------------------------
// Uplink
// ---------------------------------------------------------------------------

void TtcApp::task_receive(void* context) {
    static_cast<TtcApp*>(context)->pump_link();
}

void TtcApp::pump_link() {
    link_.poll();

    // Report link transitions as events. The ground needs to know when the
    // spacecraft believes contact was made or lost, independently of what the
    // ground station itself observed -- the two disagreeing is diagnostic.
    const bool connected = link_.connected();
    if (connected != was_connected_) {
        events_.raise(connected ? dict::EventId::LINK_CONNECTED
                                : dict::EventId::LINK_LOST);
        was_connected_ = connected;
        if (!connected) {
            // Drop any half-received packet. Resuming a partial packet across
            // a reconnection would splice two unrelated byte streams together.
            rx_used_ = 0;
        }
    }
    if (!connected) { return; }

    const size_t taken = link_.receive(rx_buffer_ + rx_used_,
                                       kRxBufferBytes - rx_used_);
    rx_used_ += taken;
    drain_rx_buffer();
}

void TtcApp::drain_rx_buffer() {
    // Extract as many whole Space Packets as the buffer currently holds.
    //
    // TCP is a byte stream with no packet boundaries, so framing comes from
    // the CCSDS length field itself. That works only while the stream stays in
    // sync. A real radio link does not rely on this: TM/TC transfer frames
    // carry an attached sync marker precisely so a receiver can regain framing
    // after a burst of noise. Adding that is Phase 4; until then a corrupted
    // length field would desynchronise the stream, and the recovery below is
    // the honest, limited mitigation -- discard one octet and try again.
    size_t consumed = 0;

    while (rx_used_ - consumed >= kSpacePacketHeaderBytes) {
        const uint8_t* at        = rx_buffer_ + consumed;
        const size_t   available = rx_used_ - consumed;

        core::ByteReader   header_reader(at, available);
        SpacePacketHeader  header;
        if (!header.decode(header_reader)) { break; }

        const size_t total = header.total_size();
        if (total > kMaxPacketBytes) {
            // Not a packet we could ever accept. Skip a single octet rather
            // than the claimed length, because the length itself is suspect.
            ++consumed;
            ++tc_rejected_;
            events_.raise(dict::EventId::TC_REJECTED,
                          static_cast<uint32_t>(core::FailureCode::BadLength));
            continue;
        }
        if (available < total) { break; }   // wait for the rest to arrive

        ReceivedTc        tc;
        core::FailureCode failure = core::FailureCode::Ok;
        const core::Status status = parse_tc(at, total, tc, failure);

        if (core::is_ok(status)) {
            ++tc_received_;
            handle_tc(tc);
        } else {
            ++tc_rejected_;
            events_.raise(dict::EventId::TC_REJECTED,
                          static_cast<uint32_t>(failure));
            // A packet that failed its CRC cannot be answered with a
            // verification report: its APID and sequence count are exactly the
            // fields we would have to quote back, and they are not trustworthy.
            // The event above is the only honest notification.
        }
        consumed += total;
    }

    // Shuffle whatever is left to the front. At most one partial packet, so
    // this moves a few hundred bytes at worst.
    if (consumed > 0) {
        const size_t leftover = rx_used_ - consumed;
        if (leftover > 0) {
            std::memmove(rx_buffer_, rx_buffer_ + consumed, leftover);
        }
        rx_used_ = leftover;
    } else if (rx_used_ == kRxBufferBytes) {
        // Full of something that never resolves into a packet. Reset rather
        // than wedge the uplink forever.
        rx_used_ = 0;
        events_.raise(dict::EventId::TC_REJECTED,
                      static_cast<uint32_t>(core::FailureCode::BadLength));
    }
}

void TtcApp::handle_tc(const ReceivedTc& tc) {
    // Acceptance: the packet is well formed and addresses a service we know.
    const cmd::CommandInfo* info = cmd::find_command(tc.secondary.service,
                                                      tc.secondary.subtype);
    if (info == nullptr) {
        if (tc.secondary.wants(kAckAcceptance)) {
            send_verification(tc, kVerifAcceptFailure, core::FailureCode::UnknownService);
        }
        ++tc_rejected_;
        events_.raise(dict::EventId::TC_REJECTED,
                      static_cast<uint32_t>(core::FailureCode::UnknownService));
        return;
    }
    if (tc.args_size != info->arg_bytes) {
        if (tc.secondary.wants(kAckAcceptance)) {
            send_verification(tc, kVerifAcceptFailure, core::FailureCode::BadLength);
        }
        ++tc_rejected_;
        events_.raise(dict::EventId::TC_REJECTED,
                      static_cast<uint32_t>(core::FailureCode::BadLength));
        return;
    }

    if (tc.secondary.wants(kAckAcceptance)) {
        send_verification(tc, kVerifAcceptSuccess, core::FailureCode::Ok);
    }

    // Execution.
    core::FailureCode result = core::FailureCode::UnknownService;
    switch (static_cast<Service>(tc.secondary.service)) {
        case Service::Test:         result = svc_test(tc);         break;
        case Service::Housekeeping: result = svc_housekeeping(tc); break;
        case Service::Parameter:    result = svc_parameter(tc);    break;
        case Service::Function:     result = svc_function(tc);     break;
        default:                    result = core::FailureCode::UnknownService; break;
    }

    if (tc.secondary.wants(kAckCompletion)) {
        send_verification(tc,
                          result == core::FailureCode::Ok ? kVerifCompleteSuccess
                                                          : kVerifCompleteFailure,
                          result);
    }
}

// ---- PUS service handlers -------------------------------------------------

core::FailureCode TtcApp::svc_test(const ReceivedTc& tc) {
    // ST[17,1] connection test. Changes no state whatsoever, which is what
    // makes it safe to send at any time, in any mode, as a first action of a
    // pass to prove the uplink, the flight software and the downlink all work.
    if (tc.secondary.subtype != cmd::TestConnectionArgs::kSubtype) {
        return core::FailureCode::UnknownService;
    }
    send_test_report();
    return core::FailureCode::Ok;
}

core::FailureCode TtcApp::svc_housekeeping(const ReceivedTc& tc) {
    core::ByteReader r(tc.args, tc.args_size);

    const bool enable = (tc.secondary.subtype == cmd::EnableHkArgs::kSubtype);
    if (!enable && tc.secondary.subtype != cmd::DisableHkArgs::kSubtype) {
        return core::FailureCode::UnknownService;
    }

    uint8_t sid_value = 0;
    if (!r.read_uint8(sid_value)) { return core::FailureCode::BadLength; }

    for (size_t i = 0; i < hk_.size(); ++i) {
        if (static_cast<uint8_t>(hk_[i].sid) == sid_value) {
            hk_[i].enabled = enable;
            events_.raise(enable ? dict::EventId::HK_ENABLED
                                 : dict::EventId::HK_DISABLED,
                          sid_value);
            return core::FailureCode::Ok;
        }
    }
    return core::FailureCode::IllegalArg;
}

core::FailureCode TtcApp::svc_parameter(const ReceivedTc& tc) {
    core::ByteReader r(tc.args, tc.args_size);

    uint16_t id_value = 0;
    if (!r.read_uint16(id_value)) { return core::FailureCode::BadLength; }
    const auto id = static_cast<dict::ParamId>(id_value);

    if (tc.secondary.subtype == cmd::ReportParamArgs::kSubtype) {
        double value = 0.0;
        if (!core::is_ok(params_.get(id, value))) { return core::FailureCode::IllegalArg; }
        send_param_report(id, value);
        return core::FailureCode::Ok;
    }

    if (tc.secondary.subtype == cmd::SetParamArgs::kSubtype) {
        double value = 0.0;
        if (!r.read_float64(value)) { return core::FailureCode::BadLength; }

        const core::Status s = params_.set(id, value);
        if (!core::is_ok(s)) {
            // Range violations are reported, never clamped. Silently accepting
            // a nearby value would leave the ground believing it had set
            // something it had not.
            return core::to_failure(s);
        }
        events_.raise(dict::EventId::PARAM_SET, id_value);
        return core::FailureCode::Ok;
    }

    return core::FailureCode::UnknownService;
}

core::FailureCode TtcApp::svc_function(const ReceivedTc& tc) {
    if (tc.secondary.subtype == cmd::SetModeArgs::kSubtype) {
        cmd::SetModeArgs args;
        core::ByteReader r(tc.args, tc.args_size);
        if (!args.deserialize(r)) { return core::FailureCode::BadLength; }

        // Publish the request and let the mode manager arbitrate. TT&C has no
        // business deciding whether a mode change is safe -- it only carries
        // the request. The refusal, if any, arrives back as an event.
        bus_.publish(mode_topic_, &args.mode, sizeof(args.mode));
        return core::FailureCode::Ok;
    }

    if (tc.secondary.subtype == cmd::ResetCountersArgs::kSubtype) {
        tc_received_ = 0;
        tc_rejected_ = 0;
        tm_sent_     = 0;
        return core::FailureCode::Ok;
    }

    return core::FailureCode::UnknownService;
}

// ---------------------------------------------------------------------------
// Downlink
// ---------------------------------------------------------------------------

uint16_t TtcApp::next_message_count(uint8_t service, uint8_t subtype) {
    for (size_t i = 0; i < msg_counters_.size(); ++i) {
        if (msg_counters_[i].service == service && msg_counters_[i].subtype == subtype) {
            return msg_counters_[i].count++;
        }
    }
    MsgCounter c{service, subtype, 1};
    msg_counters_.push_back(c);
    return 0;
}

bool TtcApp::send_packet(size_t length) {
    if (length == 0) { return false; }
    if (!link_.connected()) { return false; }
    if (!core::is_ok(link_.send(tx_scratch_, length))) { return false; }
    ++tm_sent_;
    return true;
}

void TtcApp::send_verification(const ReceivedTc& tc, uint8_t subtype,
                               core::FailureCode failure) {
    TmBuilder b(tx_scratch_, sizeof tx_scratch_);
    if (!b.begin(dict::apid_value(dict::Apid::TTC), seq_ttc_.next(),
                 Service::Verification, subtype,
                 next_message_count(1, subtype), now_cuc())) {
        return;
    }
    // Quote back exactly which telecommand this refers to. APID plus sequence
    // count is the only unambiguous identifier the ground has.
    b.payload().write_uint16(tc.primary.apid);
    b.payload().write_uint16(tc.primary.sequence_count);
    if (subtype == kVerifAcceptFailure || subtype == kVerifCompleteFailure) {
        b.payload().write_uint16(static_cast<uint16_t>(failure));
    }
    send_packet(b.finish());
}

void TtcApp::send_test_report() {
    TmBuilder b(tx_scratch_, sizeof tx_scratch_);
    if (!b.begin(dict::apid_value(dict::Apid::TTC), seq_ttc_.next(),
                 Service::Test, 2, next_message_count(17, 2), now_cuc())) {
        return;
    }
    // ST[17,2] carries no source data at all: its existence is the message.
    send_packet(b.finish());
}

void TtcApp::send_param_report(dict::ParamId id, double value) {
    TmBuilder b(tx_scratch_, sizeof tx_scratch_);
    if (!b.begin(dict::apid_value(dict::Apid::TTC), seq_ttc_.next(),
                 Service::Parameter, 2, next_message_count(20, 2), now_cuc())) {
        return;
    }
    b.payload().write_uint16(static_cast<uint16_t>(id));
    b.payload().write_float64(value);
    send_packet(b.finish());
}

void TtcApp::event_sink(void* context, const core::EventRecord& record) {
    auto* self = static_cast<TtcApp*>(context);

    // The subtype IS the severity, which is what lets a ground system filter
    // on urgency without knowing a single thing about this mission's events.
    const uint8_t subtype = static_cast<uint8_t>(record.severity);

    TmBuilder b(self->tx_scratch_, sizeof self->tx_scratch_);
    if (!b.begin(dict::apid_value(dict::Apid::TTC), self->seq_ttc_.next(),
                 Service::Event, subtype,
                 self->next_message_count(5, subtype), record.time)) {
        return;
    }
    b.payload().write_uint16(static_cast<uint16_t>(record.id));
    b.payload().write_uint32(record.aux);
    self->send_packet(b.finish());
}

void TtcApp::send_hk(dict::HkSid sid) {
    uint16_t         apid = dict::apid_value(dict::Apid::TTC);
    SequenceCounter* seq  = &seq_ttc_;

    switch (sid) {
        case dict::HkSid::SYS_HK:
            apid = dict::apid_value(tlm::SysHk::kApid);
            seq  = &seq_ttc_;
            break;
        case dict::HkSid::ADCS_HK:
            apid = dict::apid_value(tlm::AdcsHk::kApid);
            seq  = &seq_adcs_;
            break;
        case dict::HkSid::EPS_HK:
            apid = dict::apid_value(tlm::EpsHk::kApid);
            seq  = &seq_eps_;
            break;
    }

    TmBuilder b(tx_scratch_, sizeof tx_scratch_);
    if (!b.begin(apid, seq->next(), Service::Housekeeping, 25,
                 next_message_count(3, 25), now_cuc())) {
        return;
    }
    // ST[3,25] source data begins with the structure identifier, which is how
    // the ground knows which of the dictionary's layouts follows.
    b.payload().write_uint8(static_cast<uint8_t>(sid));

    switch (sid) {
        case dict::HkSid::SYS_HK: {
            // Assembled here rather than published by someone else, because
            // these are facts about the flight software itself.
            tlm::SysHk hk;
            hk.uptime_s       = scheduler_.uptime_s();
            hk.tick_count     = scheduler_.tick_count();
            hk.mode           = static_cast<uint8_t>(dict::SystemMode::BOOT);
            hk.boot_count     = 0;
            hk.cpu_load_pct   = scheduler_.load_percent();
            hk.sched_overruns = static_cast<uint16_t>(scheduler_.overrun_count());
            hk.tc_received    = tc_received_;
            hk.tc_rejected    = tc_rejected_;
            hk.tm_sent        = tm_sent_;
            hk.link_up        = link_.connected() ? 1 : 0;
            hk.events_logged  = events_.raised_count();
            hk.last_event_id  = events_.last_id();
            hk.serialize(b.payload());
            break;
        }
        case dict::HkSid::ADCS_HK: adcs_hk_.serialize(b.payload()); break;
        case dict::HkSid::EPS_HK:  eps_hk_.serialize(b.payload());  break;
    }

    send_packet(b.finish());
}

void TtcApp::task_telemetry(void* context) {
    auto* self = static_cast<TtcApp*>(context);
    const double now = self->clock_.mission_time_s();

    // Each structure has its own period, taken from a parameter so an operator
    // can slow telemetry down over a congested link without a software change.
    const dict::ParamId period_param[] = {
        dict::ParamId::SYS_HK_PERIOD_MS,
        dict::ParamId::ADCS_HK_PERIOD_MS,
        dict::ParamId::EPS_HK_PERIOD_MS,
    };

    for (size_t i = 0; i < self->hk_.size(); ++i) {
        HkState& state = self->hk_[i];
        if (!state.enabled) { continue; }

        const double period_s =
            static_cast<double>(self->params_.get_u32(period_param[i])) / 1000.0;

        // A shortened period must take effect NOW, not after the interval that
        // was already running finishes. An operator who asks for faster
        // telemetry during a pass has a reason, and making them wait up to a
        // full old period -- possibly a minute -- would be surprising and
        // useless. Lengthening a period needs no equivalent handling: the next
        // report simply comes later, which is what was asked for.
        const double soonest = now + period_s;
        if (state.next_due > soonest) { state.next_due = soonest; }

        if (now < state.next_due) { continue; }

        self->send_hk(state.sid);

        // Advance from "now" rather than from the previous deadline. Catching
        // up on missed reports after a long gap would dump a burst of stale
        // housekeeping into a fresh contact, which is never what is wanted.
        state.next_due = now + period_s;
    }
}

// ---- bus subscriptions ----------------------------------------------------

void TtcApp::on_adcs_hk(void* context, core::Topic, const uint8_t* data, size_t length) {
    auto* self = static_cast<TtcApp*>(context);
    if (length == sizeof(tlm::AdcsHk)) {
        std::memcpy(&self->adcs_hk_, data, sizeof(tlm::AdcsHk));
    }
}

void TtcApp::on_eps_hk(void* context, core::Topic, const uint8_t* data, size_t length) {
    auto* self = static_cast<TtcApp*>(context);
    if (length == sizeof(tlm::EpsHk)) {
        std::memcpy(&self->eps_hk_, data, sizeof(tlm::EpsHk));
    }
}

}  // namespace fsw::ttc
