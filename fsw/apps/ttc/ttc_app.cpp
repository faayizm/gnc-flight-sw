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
        dict::HkSid::FDIR_HK,
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
    s = bus_.subscribe(core::Topic::FdirHk, &TtcApp::on_fdir_hk, this);
    if (!core::is_ok(s)) { return s; }
    s = bus_.subscribe(core::Topic::MonitorReport, &TtcApp::on_monitor_report, this);
    if (!core::is_ok(s)) { return s; }
    for (bool& on : action_enabled_) { on = true; }
    s = bus_.subscribe(core::Topic::ModeChanged, &TtcApp::on_mode, this);
    if (!core::is_ok(s)) { return s; }
    s = bus_.subscribe(core::Topic::PowerStatus, &TtcApp::on_power, this);
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
    auto* self = static_cast<TtcApp*>(context);
    self->run_event_actions();
    self->pump_link();
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

    // The ground was heard. EPS keeps the transmitter on for a while after
    // this even when shedding load; the mode manager resets its contact timer.
    if (tc_rx_.frames_accepted() != frames_heard_) {
        frames_heard_ = tc_rx_.frames_accepted();
        bus_.publish(core::Topic::UplinkActivity, nullptr, 0);
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
    if (!info->variable && tc.args_size != info->arg_bytes) {
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
        case Service::Time:         result = svc_time(tc);         break;
        case Service::Scheduling:   result = svc_scheduling(tc);   break;
        case Service::Storage:      result = svc_storage(tc);      break;
        case Service::Monitoring:   result = svc_monitoring(tc);   break;
        case Service::EventAction:  result = svc_event_action(tc); break;
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

    if (tc.secondary.subtype == cmd::SwitchRailArgs::kSubtype) {
        cmd::SwitchRailArgs args;
        core::ByteReader r(tc.args, tc.args_size);
        if (!args.deserialize(r)) { return core::FailureCode::BadLength; }
        if (args.rail > static_cast<uint8_t>(dict::PowerRail::SURVIVAL_HEATERS) || args.on > 1) {
            return core::FailureCode::IllegalArg;
        }
        // Like a mode request: carried to EPS, which owns the switches.
        const msg::RailRequest req{args.rail, args.on != 0};
        bus_.publish_object(core::Topic::RailRequest, req);
        return core::FailureCode::Ok;
    }

    if (tc.secondary.subtype == cmd::RestoreWheelsArgs::kSubtype) {
        cmd::RestoreWheelsArgs args;
        core::ByteReader r(tc.args, tc.args_size);
        if (!args.deserialize(r)) { return core::FailureCode::BadLength; }
        if (args.mask == 0 || args.mask > 7) { return core::FailureCode::IllegalArg; }
        // FDIR owns the wheels' health; the ground can only ask.
        bus_.publish(core::Topic::WheelRestore, &args.mask, sizeof(args.mask));
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

// ---- ST[12] on-board monitoring --------------------------------------------

core::FailureCode TtcApp::svc_monitoring(const ReceivedTc& tc) {
    core::ByteReader r(tc.args, tc.args_size);
    cmd::EnableMonitorArgs a;                     // both subtypes carry one id
    if (!a.deserialize(r)) { return core::FailureCode::BadLength; }
    bool known = false;
    for (const tlm::MonitorDef& d : tlm::kMonitors) { known = known || d.id == a.monitor_id; }
    if (!known) { return core::FailureCode::IllegalArg; }
    if (tc.secondary.subtype != cmd::EnableMonitorArgs::kSubtype &&
        tc.secondary.subtype != cmd::DisableMonitorArgs::kSubtype) {
        return core::FailureCode::UnknownService;
    }
    // FDIR owns the monitors; TT&C only carries the request.
    const msg::MonitorControl c{a.monitor_id, tc.secondary.subtype == cmd::EnableMonitorArgs::kSubtype};
    bus_.publish_object(core::Topic::MonitorControl, c);
    return core::FailureCode::Ok;
}

void TtcApp::on_monitor_report(void* context, core::Topic, const uint8_t* data, size_t length) {
    if (length != sizeof(msg::MonitorReport)) { return; }
    auto* self = static_cast<TtcApp*>(context);
    msg::MonitorReport m;
    std::memcpy(&m, data, sizeof m);
    TmBuilder b(self->tx_scratch_, sizeof self->tx_scratch_);
    if (!b.begin(dict::apid_value(dict::Apid::FDIR), self->seq_fdir_.next(), Service::Monitoring, 12,
                 self->next_message_count(12, 12), self->now_cuc(), self->time_status_)) {
        return;
    }
    b.payload().write_uint8(m.id);
    b.payload().write_uint8(m.from);
    b.payload().write_uint8(m.to);
    b.payload().write_float64(m.value);
    b.payload().write_float64(m.limit);
    self->send_packet(b.finish());
}

// ---- ST[19] event-action ------------------------------------------------

core::FailureCode TtcApp::svc_event_action(const ReceivedTc& tc) {
    core::ByteReader r(tc.args, tc.args_size);
    cmd::EnableEventActionArgs a;                 // both subtypes carry one event id
    if (!a.deserialize(r)) { return core::FailureCode::BadLength; }
    if (tc.secondary.subtype != cmd::EnableEventActionArgs::kSubtype &&
        tc.secondary.subtype != cmd::DisableEventActionArgs::kSubtype) {
        return core::FailureCode::UnknownService;
    }
    bool found = false;
    for (size_t i = 0; i < dict::kEventActionCount; ++i) {
        if (static_cast<uint16_t>(dict::kEventActions[i].event) == a.event_id) {
            action_enabled_[i] = tc.secondary.subtype == cmd::EnableEventActionArgs::kSubtype;
            found = true;
        }
    }
    return found ? core::FailureCode::Ok : core::FailureCode::IllegalArg;
}

void TtcApp::run_event_actions() {
    // Deferred from the event sink to here, the start of the next tick: an
    // action can raise events of its own, and must not do so from inside the
    // sink that is reporting the event that triggered it. Under lockstep the
    // next tick always comes before the next sensor sample, so the action
    // lands at the same point of the flight every time.
    while (!pending_actions_.empty()) {
        const size_t i = pending_actions_[0];
        pending_actions_.erase(0);

        const dict::EventActionDef& def = dict::kEventActions[i];
        events_.raise(dict::EventId::EVENT_ACTION, static_cast<uint32_t>(def.event));
        ReceivedTc tc;
        tc.primary.apid       = dict::apid_value(dict::Apid::TTC);
        tc.secondary.ack_flags = kAckNone;        // nobody on the ground is waiting for a reply
        tc.secondary.service  = def.service;
        tc.secondary.subtype  = def.subtype;
        tc.secondary.source_id = 0;
        tc.args      = def.args;
        tc.args_size = def.arg_bytes;
        ++tc_received_;
        handle_tc(tc);
    }
}

// ---- ST[9] time management ---------------------------------------------

core::FailureCode TtcApp::svc_time(const ReceivedTc& tc) {
    core::ByteReader r(tc.args, tc.args_size);
    if (tc.secondary.subtype == cmd::SetTimeReportRateArgs::kSubtype) {
        cmd::SetTimeReportRateArgs a;
        if (!a.deserialize(r)) { return core::FailureCode::BadLength; }
        if (a.rate_exp > 16 && a.rate_exp != 255) { return core::FailureCode::IllegalArg; }
        time_rate_exp_ = a.rate_exp;
        next_time_report_ = 0.0;            // first report straight away
        return core::FailureCode::Ok;
    }
    if (tc.secondary.subtype == cmd::AdjustTimeArgs::kSubtype) {
        cmd::AdjustTimeArgs a;
        if (!a.deserialize(r)) { return core::FailureCode::BadLength; }
        if (!(a.delta_s == a.delta_s)) { return core::FailureCode::IllegalArg; }
        const double t = clock_.mission_time_s() + a.delta_s;
        if (t < 0.0 || t > 4.0e9) { return core::FailureCode::IllegalArg; }   // CUC coarse is 32 bits
        clock_.set_mission_time_s(t);
        time_status_ = 1;
        // Everything already scheduled in mission time stays where it is in
        // mission time; periodic reports re-anchor on their next run.
        // Whole seconds, two's complement: covers +/-68 years, which a first
        // correction from a clock that booted at the mission epoch needs.
        const double secs = a.delta_s > 2.0e9 ? 2.0e9 : (a.delta_s < -2.0e9 ? -2.0e9 : a.delta_s);
        events_.raise(dict::EventId::TIME_ADJUSTED,
                      static_cast<uint32_t>(static_cast<int32_t>(secs)));
        return core::FailureCode::Ok;
    }
    return core::FailureCode::UnknownService;
}

void TtcApp::send_time_report() {
    TmBuilder b(tx_scratch_, sizeof tx_scratch_);
    const core::CucTime t = now_cuc();
    if (!b.begin(dict::apid_value(dict::Apid::TTC), seq_ttc_.next(), Service::Time, 2,
                 next_message_count(9, 2), t, time_status_)) {
        return;
    }
    b.payload().write_uint8(time_rate_exp_);
    b.payload().write_uint32(t.coarse);
    b.payload().write_uint16(t.fine);
    if (send_packet(b.finish())) { framer_.realtime().expedite(); }
}

// ---- ST[11] time-based scheduling --------------------------------------

core::FailureCode TtcApp::svc_scheduling(const ReceivedTc& tc) {
    switch (tc.secondary.subtype) {
        case cmd::EnableScheduleArgs::kSubtype:  schedule_.set_enabled(true);  return core::FailureCode::Ok;
        case cmd::DisableScheduleArgs::kSubtype: schedule_.set_enabled(false); return core::FailureCode::Ok;
        case cmd::ResetScheduleArgs::kSubtype:   schedule_.reset();            return core::FailureCode::Ok;
        case cmd::InsertActivitiesArgs::kSubtype: break;
        default: return core::FailureCode::UnknownService;
    }

    // Two passes over the request: validate everything, then insert
    // everything. Nothing is stored unless all of it can be.
    const double now = clock_.mission_time_s();
    for (int pass = 0; pass < 2; ++pass) {
        core::ByteReader r(tc.args, tc.args_size);
        uint8_t count = 0;
        if (!r.read_uint8(count) || count == 0) { return core::FailureCode::BadLength; }
        if (pass == 0 && count > schedule_.free_slots()) { return core::FailureCode::Unavailable; }
        for (uint8_t i = 0; i < count; ++i) {
            uint32_t coarse = 0;
            uint16_t fine = 0;
            if (!r.read_uint32(coarse) || !r.read_uint16(fine)) { return core::FailureCode::BadLength; }
            const uint8_t* at = r.take(0);
            if (at == nullptr || r.remaining() < kSpacePacketHeaderBytes) { return core::FailureCode::BadLength; }
            core::ByteReader hr(at, r.remaining());
            SpacePacketHeader h;
            if (!h.decode(hr)) { return core::FailureCode::BadLength; }
            const size_t total = h.total_size();
            if (total > r.remaining() || total > TimeSchedule::kMaxTcBytes) { return core::FailureCode::BadLength; }

            const double release = core::CucTime{coarse, fine}.to_seconds();
            if (pass == 0) {
                ReceivedTc inner;
                core::FailureCode why = core::FailureCode::Ok;
                if (!core::is_ok(parse_tc(at, total, inner, why))) { return why; }
                if (cmd::find_command(inner.secondary.service, inner.secondary.subtype) == nullptr) {
                    return core::FailureCode::IllegalArg;
                }
                if (release < now) { return core::FailureCode::IllegalArg; }   // already in the past
            } else {
                schedule_.insert(release, at, total);
            }
            r.skip(total);
        }
        if (pass == 0 && !r.exhausted()) { return core::FailureCode::BadLength; }
    }
    return core::FailureCode::Ok;
}

void TtcApp::release_scheduled() {
    if (!schedule_.enabled()) { return; }
    const double now = clock_.mission_time_s();
    // Several may fall due in one 100 ms tick; each runs, oldest first.
    for (TimeSchedule::Activity* a = schedule_.due(now); a != nullptr; a = schedule_.due(now)) {
        const size_t n = a->length;
        std::memcpy(release_buf_, a->packet, n);
        schedule_.release(a);
        const uint16_t seq = static_cast<uint16_t>(((release_buf_[2] & 0x3F) << 8) | release_buf_[3]);
        events_.raise(dict::EventId::SCHED_RELEASED, seq);
        execute_packet(release_buf_, n);
    }
}

void TtcApp::execute_packet(const uint8_t* packet, size_t length) {
    ReceivedTc tc;
    core::FailureCode failure = core::FailureCode::Ok;
    if (core::is_ok(parse_tc(packet, length, tc, failure))) {
        ++tc_received_;
        handle_tc(tc);
    } else {
        ++tc_rejected_;
        events_.raise(dict::EventId::TC_REJECTED, static_cast<uint32_t>(failure));
    }
}

// ---- ST[15] on-board storage and retrieval ------------------------------

core::FailureCode TtcApp::svc_storage(const ReceivedTc& tc) {
    core::ByteReader r(tc.args, tc.args_size);
    uint8_t id = 0;
    if (!r.read_uint8(id)) { return core::FailureCode::BadLength; }
    if (id != kStoreId) { return core::FailureCode::IllegalArg; }

    switch (tc.secondary.subtype) {
        case cmd::EnableStorageArgs::kSubtype:  store_.set_enabled(true);  return core::FailureCode::Ok;
        case cmd::DisableStorageArgs::kSubtype: store_.set_enabled(false); return core::FailureCode::Ok;
        case cmd::RetrieveByTimeArgs::kSubtype: {
            uint32_t from = 0, to = 0;
            if (!r.read_uint32(from) || !r.read_uint32(to)) { return core::FailureCode::BadLength; }
            if (to < from) { return core::FailureCode::IllegalArg; }
            if (store_.retrieving()) { return core::FailureCode::Unavailable; }
            const uint32_t n = store_.start_retrieval(from, to);
            playback_sent_ = 0;
            events_.raise(dict::EventId::PLAYBACK_STARTED, n);
            return core::FailureCode::Ok;
        }
        case cmd::DeleteStoreUpToArgs::kSubtype: {
            uint32_t to = 0;
            if (!r.read_uint32(to)) { return core::FailureCode::BadLength; }
            if (store_.retrieving()) { return core::FailureCode::Unavailable; }
            store_.delete_up_to(to);
            return core::FailureCode::Ok;
        }
        case cmd::ReportStoreSummaryArgs::kSubtype:
            send_store_summary();
            return core::FailureCode::Ok;
        default:
            return core::FailureCode::UnknownService;
    }
}

void TtcApp::send_store_summary() {
    TmBuilder b(tx_scratch_, sizeof tx_scratch_);
    if (!b.begin(dict::apid_value(dict::Apid::TTC), seq_ttc_.next(), Service::Storage, 13,
                 next_message_count(15, 13), now_cuc(), time_status_)) {
        return;
    }
    b.payload().write_uint8(kStoreId);
    b.payload().write_uint32(store_.oldest());
    b.payload().write_uint32(store_.newest());
    b.payload().write_uint32(store_.count());
    b.payload().write_uint8(store_.used_pct());
    send_packet(b.finish());
}

void TtcApp::pump_playback() {
    // Keep the playback channel topped up without flooding it: live
    // telemetry always has priority in the framer, and a bounded backlog here
    // means a retrieval never starves the store of the CPU either.
    while (store_.retrieving() && framer_.playback().pending_bytes() < 4096) {
        const size_t n = store_.next(playback_buf_, sizeof playback_buf_);
        if (n == 0) {
            events_.raise(dict::EventId::PLAYBACK_DONE, playback_sent_);
            return;
        }
        if (framer_.playback().enqueue(playback_buf_, n)) { ++playback_sent_; }
    }
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
    // Everything the spacecraft says is written down, contact or not. The
    // store cannot raise its own event from here -- that would rebuild a
    // packet in tx_scratch_ while this one is still in it -- so it is flagged
    // and raised from the next telemetry task.
    if (store_.record(tx_scratch_, length)) { wrap_event_pending_ = true; }
    if (!link_.connected() || !tx_on_) { return false; }
    if (!framer_.realtime().enqueue(tx_scratch_, length)) { return false; }
    ++tm_sent_;
    return true;
}

void TtcApp::task_downlink(void* context) {
    auto* self = static_cast<TtcApp*>(context);
    // No transmitter, no downlink: the store keeps recording regardless.
    if (!self->link_.connected() || !self->tx_on_) { return; }
    const uint32_t tick = self->scheduler_.tick_count();
    self->pump_playback();

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
                 next_message_count(1, subtype), now_cuc(), time_status_)) {
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
                 Service::Test, 2, next_message_count(17, 2), now_cuc(), time_status_)) {
        return;
    }
    // ST[17,2] carries no source data at all: its existence is the message.
    send_packet(b.finish());
}

void TtcApp::send_param_report(dict::ParamId id, double value) {
    TmBuilder b(tx_scratch_, sizeof tx_scratch_);
    if (!b.begin(dict::apid_value(dict::Apid::TTC), seq_ttc_.next(),
                 Service::Parameter, 2, next_message_count(20, 2), now_cuc(), time_status_)) {
        return;
    }
    b.payload().write_uint16(static_cast<uint16_t>(id));
    b.payload().write_float64(value);
    send_packet(b.finish());
}

void TtcApp::event_sink(void* context, const core::EventRecord& record) {
    auto* self = static_cast<TtcApp*>(context);

    for (size_t i = 0; i < dict::kEventActionCount; ++i) {
        if (dict::kEventActions[i].event == record.id && self->action_enabled_[i]) {
            self->pending_actions_.push_back(i);   // full queue: the action is dropped, the event is not
        }
    }

    // The subtype IS the severity, which is what lets a ground system filter
    // on urgency without knowing a single thing about this mission's events.
    const uint8_t subtype = static_cast<uint8_t>(record.severity);

    TmBuilder b(self->tx_scratch_, sizeof self->tx_scratch_);
    if (!b.begin(dict::apid_value(dict::Apid::TTC), self->seq_ttc_.next(),
                 Service::Event, subtype,
                 self->next_message_count(5, subtype), record.time,
                 self->time_status_)) {
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
        case dict::HkSid::FDIR_HK:
            apid = dict::apid_value(tlm::FdirHk::kApid);
            seq  = &seq_fdir_;
            break;
    }

    TmBuilder b(tx_scratch_, sizeof tx_scratch_);
    if (!b.begin(apid, seq->next(), Service::Housekeeping, 25,
                 next_message_count(3, 25), now_cuc(), time_status_)) {
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
            hk.mode           = mode_;
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
            hk.time_status    = time_status_;
            hk.sched_pending  = static_cast<uint16_t>(schedule_.pending());
            hk.sched_enabled  = schedule_.enabled() ? 1 : 0;
            hk.store_packets  = store_.count();
            hk.store_used_pct = store_.used_pct();
            hk.serialize(b.payload());
            break;
        }
        case dict::HkSid::ADCS_HK: adcs_hk_.serialize(b.payload()); break;
        case dict::HkSid::EPS_HK:  eps_hk_.serialize(b.payload());  break;
        case dict::HkSid::FDIR_HK: fdir_hk_.serialize(b.payload()); break;
    }

    send_packet(b.finish());
}

void TtcApp::task_telemetry(void* context) {
    auto* self = static_cast<TtcApp*>(context);
    const double now = self->clock_.mission_time_s();

    if (self->wrap_event_pending_) {
        self->wrap_event_pending_ = false;
        self->events_.raise(dict::EventId::STORE_WRAPPED);
    }
    self->release_scheduled();

    if (self->time_rate_exp_ != 255) {
        const double period = static_cast<double>(1u << (self->time_rate_exp_ > 16 ? 16 : self->time_rate_exp_));
        if (self->next_time_report_ > now + period) { self->next_time_report_ = now + period; }
        if (now >= self->next_time_report_) {
            self->send_time_report();
            self->next_time_report_ = now + period;
        }
    }

    // Each structure has its own period, taken from a parameter so an operator
    // can slow telemetry down over a congested link without a software change.
    const dict::ParamId period_param[] = {
        dict::ParamId::SYS_HK_PERIOD_MS,
        dict::ParamId::ADCS_HK_PERIOD_MS,
        dict::ParamId::EPS_HK_PERIOD_MS,
        dict::ParamId::FDIR_HK_PERIOD_MS,
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

void TtcApp::on_mode(void* context, core::Topic, const uint8_t* data, size_t length) {
    if (length == sizeof(msg::ModeChange)) {
        msg::ModeChange m;
        std::memcpy(&m, data, sizeof m);
        static_cast<TtcApp*>(context)->mode_ = m.to;
    }
}

void TtcApp::on_power(void* context, core::Topic, const uint8_t* data, size_t length) {
    if (length == sizeof(msg::PowerStatus)) {
        msg::PowerStatus p;
        std::memcpy(&p, data, sizeof p);
        const auto tx = static_cast<uint16_t>(1u << static_cast<unsigned>(dict::PowerRail::TX));
        static_cast<TtcApp*>(context)->tx_on_ = !p.valid || (p.rails & tx) != 0;
    }
}

void TtcApp::on_fdir_hk(void* context, core::Topic, const uint8_t* data, size_t length) {
    auto* self = static_cast<TtcApp*>(context);
    if (length == sizeof(tlm::FdirHk)) {
        std::memcpy(&self->fdir_hk_, data, sizeof(tlm::FdirHk));
    }
}

void TtcApp::on_eps_hk(void* context, core::Topic, const uint8_t* data, size_t length) {
    auto* self = static_cast<TtcApp*>(context);
    if (length == sizeof(tlm::EpsHk)) {
        std::memcpy(&self->eps_hk_, data, sizeof(tlm::EpsHk));
    }
}

}  // namespace fsw::ttc
