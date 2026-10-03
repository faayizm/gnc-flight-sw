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
        // Drop any half-received CLTU. Resuming one across a reconnection
        // would splice two unrelated byte streams together.
        if (!connected) { tc_rx_.reset(); }
    }
    if (!connected) { return; }

    // Everything framing-related lives in TcReceiver: start-sequence hunt,
    // BCH, frame checks, FARM-1. What comes out the other end is the data
    // field of each accepted frame.
    for (;;) {
        const size_t taken = link_.receive(rx_chunk_, sizeof rx_chunk_);
        if (taken == 0) { break; }
        tc_rx_.push(rx_chunk_, taken);
    }
}

void TtcApp::on_frame_data(void* context, const uint8_t* data, size_t length) {
    static_cast<TtcApp*>(context)->accept_packets(data, length);
}

void TtcApp::accept_packets(const uint8_t* data, size_t length) {
    // A frame's data field holds whole telecommand packets. The frame's own
    // CRC has already vouched for these bytes, so a length field that does not
    // fit means the ground built a bad frame, not that noise struck.
    size_t pos = 0;
    while (length - pos >= kSpacePacketHeaderBytes) {
        core::ByteReader  hr(data + pos, length - pos);
        SpacePacketHeader header;
        if (!header.decode(hr)) { break; }
        const size_t total = header.total_size();
        if (total > length - pos || total > kMaxPacketBytes) {
            ++tc_rejected_;
            events_.raise(dict::EventId::TC_REJECTED,
                          static_cast<uint32_t>(core::FailureCode::BadLength));
            return;
        }

        ReceivedTc        tc;
        core::FailureCode failure = core::FailureCode::Ok;
        if (core::is_ok(parse_tc(data + pos, total, tc, failure))) {
            ++tc_received_;
            handle_tc(tc);
        } else {
            ++tc_rejected_;
            // A packet that failed its own CRC cannot be answered with a
            // verification report: its APID and sequence count are exactly
            // the fields we would have to quote back. The event is the only
            // honest notification.
            events_.raise(dict::EventId::TC_REJECTED, static_cast<uint32_t>(failure));
        }
        pos += total;
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
    if (!framer_.realtime().enqueue(tx_scratch_, length)) { return false; }
    ++tm_sent_;
    return true;
}

void TtcApp::task_downlink(void* context) {
    auto* self = static_cast<TtcApp*>(context);
    if (!self->link_.connected()) { return; }
    const uint32_t tick = self->scheduler_.tick_count();

    // Up to two frames per 20 ms tick: 100 frames/s, about 207 kbit/s of
    // coded downlink -- an S-band CubeSat radio. An idle frame goes out at
    // least every half second, because the ground's COP-1 lives on the CLCW
    // it carries.
    for (int i = 0; i < kFramesPerTick; ++i) {
        const bool idle_due = (tick - self->last_frame_tick_) >= kIdleFrameTicks;
        const size_t n = self->framer_.next_cadu(tick, self->tc_rx_.farm().clcw(), idle_due, self->cadu_);
        if (n == 0) { break; }
        if (!core::is_ok(self->link_.send(self->cadu_, n))) { break; }
        self->last_frame_tick_ = tick;
    }
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
            hk.tm_frames_sent = framer_.frames_sent();
            hk.tc_frames_ok   = tc_rx_.frames_accepted();
            hk.tc_frames_bad  = tc_rx_.frames_rejected();
            hk.cltu_corrected = tc_rx_.bits_corrected();
            hk.farm_vr        = tc_rx_.farm().vr();
            hk.farm_lockout   = tc_rx_.farm().lockout() ? 1 : 0;
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
