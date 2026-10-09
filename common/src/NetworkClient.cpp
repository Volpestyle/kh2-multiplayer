#include "kh2coop/NetworkClient.hpp"
#include "kh2coop/Revive.hpp"

#include <chrono>
#include <utility>
#include <algorithm>

namespace kh2coop {

bool NetworkClient::validWorldContext(const ProducerWorldContext& c) const {
    return hasCurrentWorldBinding() && c.generation && c.deliverySerial==worldBinding_->deliverySerial &&
        (avatarLocalSlot_!=0 || c.hostSourceSerial!=0);
}
ProducerWorldContext NetworkClient::makeTestingWorldContext() {
    ProducerWorldContext c{1,deliverySerial(),0};
    if(avatarLocalSlot_==0 && testingSourceSerial_!=UINT64_MAX)c.hostSourceSerial=++testingSourceSerial_;
    return c;
}
bool NetworkClient::sendNativeWorld(const std::vector<std::uint8_t>& bytes,const ProducerWorldContext& c,bool reliable) {
    if(!validWorldContext(c)||bytes.empty())return false;
    const auto type=static_cast<PacketType>(bytes.front());
    if(!isScopedWorldPacket(type)||(worldQuarantined_&&!isEphemeralWorldPacket(type)))return false;
    try {
        validateScopedWorldPacket(bytes);
        if(type==PacketType::RoomTransition && avatarLocalSlot_==0){const std::uint8_t* p;std::size_t n;decodePacketHeader(bytes.data(),bytes.size(),p,n);ByteReader r(p,n);RoomTransition room;readRoomTransitionPacket(r,room);if(!r.atEnd())return false;hostRoom_=room;}
        WorldEnvelope e{{avatarSessionId_,worldBinding_->selfConnectionId,c.deliverySerial,c.hostSourceSerial,0,0},bytes};
        const bool material=resyncPlan_&&avatarLocalSlot_==0&&isMaterialWorldPacket(type);
        const bool sent=sendPacket(encode(e),reliable||material);
        if(!sent&&material)abortResync(ResyncResultReason::Overflow,"required material submission failed");
        return sent;
    }catch(const std::exception&){return false;}
}
std::optional<HostResyncContext> NetworkClient::hostResyncContext() const {
    if (!worldReady() || avatarLocalSlot_ != 0 || !hostRoom_) return std::nullopt;
    HostResyncContext context;
    context.binding = *worldBinding_;
    context.room = *hostRoom_;
    for (std::size_t i = 0; i < context.connections.size(); ++i) context.connections[i] = avatarConnections_[i];
    return context;
}
void NetworkClient::recordResyncRequest(ResyncRequestOrigin origin,std::uint8_t mask,
    const ResyncRequest* request,const char* disposition,const RequestSubmissionObservation& observation) noexcept {
    requestDiagnostics_.emit(callbacks_.onCausalDiagnostic,"resync-request",[&](std::ostream& out){
        out<<" origin="<<causalOrigin(origin)<<" disposition="<<disposition<<" mask="<<unsigned(mask)
           <<" keyAvailable="<<(request!=nullptr)<<" requestSession="<<(request?request->key.sessionId:"-")
           <<" requestHost="<<(request?request->key.hostConnectionId:0)<<" request="<<(request?request->key.requestId:0)
           <<" session="<<(avatarSessionId_.empty()?"-":avatarSessionId_)<<" host="<<avatarConnections_[0]
           <<" self="<<(avatarLocalSlot_<3?avatarConnections_[avatarLocalSlot_]:0)<<" slot="<<unsigned(avatarLocalSlot_)
           <<" delivery="<<deliverySerial()<<" roster0="<<(request?request->connections[0]:avatarConnections_[0])
           <<" roster1="<<(request?request->connections[1]:avatarConnections_[1])
           <<" roster2="<<(request?request->connections[2]:avatarConnections_[2])
           <<" deadlineAvailable="<<observation.deadlineAvailable<<" startedMs="<<observation.startedMs
           <<" originalDeadlineMs="<<observation.deadlineMs;
        causalRoom(out,request?&request->room:(hostRoom_?&*hostRoom_:nullptr));
    });
}
void NetworkClient::recordResyncCallerRejection(ResyncRequestOrigin origin,std::uint8_t mask) noexcept {
    recordResyncRequest(origin,mask,nullptr,"caller-rejected",{});
}
bool NetworkClient::requestWorldResync(std::uint8_t mask, ResyncRequest* generated, ResyncRequestOrigin origin) try {
    if (generated) *generated = {};
    if(!worldReady()||avatarLocalSlot_!=0||!hostRoom_||!nextResyncRequest_||resyncPlan_||requestedResync_){
        recordResyncRequest(origin,mask,nullptr,"generator-rejected",{});return false;
    }
    ResyncRequest r;r.key={avatarSessionId_,avatarConnections_[0],nextResyncRequest_};
    nextResyncRequest_=nextResyncRequest_==UINT64_MAX?0:nextResyncRequest_+1;
    r.room=*hostRoom_;r.targetMask=mask;for(std::size_t i=0;i<3;++i)r.connections[i]=avatarConnections_[i];
    // Expose only the actual generated immutable request, including failed submission.
    if (generated) *generated = r;
    RequestSubmissionObservation observation;
    const bool sent=sendResyncRequestObserved(r,observation);
    recordResyncRequest(origin,mask,&r,sent?"submitted":"submission-failed",observation);
    return sent;
} catch (...) {
    // Escaping construction/allocation failures have no complete outcome row.
    if (callbacks_.onCausalDiagnostic) requestDiagnostics_.gap();
    throw;
}
bool NetworkClient::sendResyncCapture(const ResyncBegin& begin,const ResyncSnapshot& snapshot,const ProducerWorldContext& c) {
    if(!validWorldContext(c)||avatarLocalSlot_!=0||!resyncPlan_||begin.key!=resyncPlan_->request.key||
       begin.phase!=resyncPlan_->phase||begin.targets!=resyncPlan_->targets||begin.targetCount!=resyncPlan_->targetCount||begin.snapshotCut!=c.hostSourceSerial)return false;
    try {
        const auto bytes=encodeResyncSnapshot(snapshot);
        if(bytes.size()!=begin.totalBytes||desyncSha256(bytes)!=begin.sha256)return false;
        if(!sendPacket(encode(begin),true)){abortResync(ResyncResultReason::Overflow,"snapshot begin submission failed");return false;}
        for(std::size_t offset=0;offset<bytes.size();offset+=RESYNC_MAX_PART_BYTES){
            const auto end=(std::min)(bytes.size(),offset+RESYNC_MAX_PART_BYTES);
            ResyncPart p{begin.key,begin.phase,begin.snapshotCut,static_cast<std::uint32_t>(offset),{bytes.begin()+offset,bytes.begin()+end}};
            if(!sendPacket(encode(p),true)){abortResync(ResyncResultReason::Overflow,"snapshot submission failed");return false;}
        }
        const bool sent=sendPacket(encode(ResyncEnd{begin.key,begin.phase,begin.snapshotCut,begin.totalBytes,begin.sha256}),true);
        if(!sent)abortResync(ResyncResultReason::Overflow,"snapshot end submission failed");
        return sent;
    }catch(const std::exception&){return false;}
}
bool NetworkClient::sendResyncAck(const ResyncAck& a,const ProducerWorldContext& c) {
    if(!validWorldContext(c)||!resyncPlan_||a.key!=resyncPlan_->request.key||a.phase!=resyncPlan_->phase||
       a.target.slot!=avatarLocalSlot_||a.target.connectionId!=worldBinding_->selfConnectionId||a.target.deliverySerial!=c.deliverySerial)return false;
    try{const bool sent=sendPacket(encode(a),true);if(a.status==ResyncAckStatus::Failed||a.status==ResyncAckStatus::Unavailable)worldQuarantined_=true;return sent;}catch(const std::exception&){return false;}
}
bool NetworkClient::sendResyncFailure(const ResyncResult& r,const ProducerWorldContext& c) {
    if(!validWorldContext(c)||avatarLocalSlot_!=0||!resyncPlan_||r.key!=resyncPlan_->request.key||r.reason==ResyncResultReason::Converged)return false;
    try{return sendPacket(encode(r),true);}catch(const std::exception&){return false;}
}
void NetworkClient::abortResync(ResyncResultReason why,const std::string& error) {
    if(why==ResyncResultReason::Converged)why=ResyncResultReason::NativeFailed;
    if(!resyncPlan_){
        if(!requestedResync_)return;
        ResyncResult result;result.key=requestedResync_->key;result.reason=why;
        for(std::size_t slot=1;slot<3;++slot)if(requestedResync_->targetMask&(1u<<slot)){auto& t=result.targets[result.targetCount++];t.target={static_cast<std::uint8_t>(slot),requestedResync_->connections[slot],remoteDeliverySerials_[slot]};t.error=error;}
        lastResyncPlanId_=(std::max)(lastResyncPlanId_,requestedResync_->key.requestId);
        requestedResync_.reset();sendPacket(encode(result),true);
        if(callbacks_.onResyncResult)callbacks_.onResyncResult(result);
        return;
    }
    ResyncResult result;result.key=resyncPlan_->request.key;result.reason=why;result.targetCount=resyncPlan_->targetCount;
    for(std::size_t i=0;i<result.targetCount;++i){result.targets[i].target=resyncPlan_->targets[i];result.targets[i].error=error;}
    if(avatarLocalSlot_==0)sendPacket(encode(result),true);
    else {ResyncAck a;a.key=result.key;a.phase=resyncPlan_->phase;a.status=ResyncAckStatus::Failed;a.error=error;for(std::size_t i=0;i<result.targetCount;++i)if(result.targets[i].target.slot==avatarLocalSlot_)a.target=result.targets[i].target;sendPacket(encode(a),true);worldQuarantined_=true;}
    resyncPlan_.reset();resyncAssembler_.Reset();
    if(callbacks_.onResyncResult)callbacks_.onResyncResult(result);
}

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

NetworkClient::NetworkClient(const std::string& hostAddress, std::uint16_t port,
                             const std::string& gameBuild,
                             const std::string& modHash,
                             const std::string& peerId,
                             std::optional<SlotType> requestedSlot,
                             ClientCallbacks callbacks,
                             RuntimeMode requestedMode,
                             std::string contentHash,
                             std::uint16_t protocolVersion,
                             std::string peerName,
                             std::unique_ptr<Transport> transport)
    : hostAddress_(hostAddress),
      port_(port),
      gameBuild_(gameBuild),
      modHash_(modHash),
      contentHash_(std::move(contentHash)),
      peerId_(peerId),
      peerName_(std::move(peerName)),
      requestedSlot_(requestedSlot),
      requestedMode_(requestedMode),
      protocolVersion_(protocolVersion),
      callbacks_(std::move(callbacks)),
      transport_(transport ? std::move(transport) : makeEnetTransport()) {}

NetworkClient::~NetworkClient() { disconnect(); }

// ---------------------------------------------------------------------------
// Connect
// ---------------------------------------------------------------------------

bool NetworkClient::connect() {
    if (connected_) return true;
    // Also dispose an ENet host retained after a remote disconnect or an
    // unfinished connection attempt. No old conditioned packet may cross this.
    disconnect();

    if (!transport_->createClient(1, 3)) {
        log("Failed to create ENet client host.");
        return false;
    }
    bool resolved = false;
    transportPeer_ = transport_->connect(hostAddress_, port_, 3, resolved);
    if (!resolved) {
        log("Failed to resolve host address: " + hostAddress_);
        transport_->close();
        return false;
    }
    if (!transportPeer_) {
        log("Failed to initiate connection to " + hostAddress_ + ":" + std::to_string(port_));
        transport_->close();
        return false;
    }

    attemptActive_ = true;

    log("Connecting to " + hostAddress_ + ":" + std::to_string(port_) + "...");
    return true;
}

// ---------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------

void NetworkClient::tick(std::uint32_t timeoutMs) {
    if (!transport_->isOpen()) return;
    // Retire the local transaction before ENet or conditioned callbacks can
    // publish a delayed snapshot. onReceive repeats this after blocking waits.
    if((resyncPlan_||requestedResync_) && localTimeMs()>=resyncDeadline_)
        abortResync(ResyncResultReason::Deadline,"local resync deadline");
    if (!transport_->isOpen()) return;

    if (connected_) {
        // Fast pings until the estimate settles, then a slow refresh.
        const std::uint64_t interval = clockSamples_ < 5 ? 100 : 2000;
        if (localTimeMs() - lastPingMs_ >= interval) sendClockPing();
    }

    TransportEvent event;
    const auto generation = transportGeneration_;
    while (transport_->isOpen() && transport_->service(event, timeoutMs) > 0) {
        switch (event.type) {
            case TransportEventType::Connect:
                onConnect();
                break;
            case TransportEventType::Disconnect:
                onDisconnect(event.data);
                break;
            case TransportEventType::Receive:
                if (inbound_.conditions().active()) {
                    // Both gameplay channel 0 and diagnostic channel 2 are reliable.
                    inbound_.enqueue(
                        localTimeMs(),
                        std::vector<std::uint8_t>(
                            event.packet.data,
                            event.packet.data + event.packet.size),
                        event.packet.reliable);
                } else {
                    onReceive(event.packet.data, event.packet.size,
                              event.packet.reliable);
                }
                event.packet.reset();
                break;
            case TransportEventType::None:
                break;
        }
        // A callback may disconnect/reconnect, replacing the host and queues.
        // onConnect itself establishes a new generation too.
        if (generation != transportGeneration_) break;
        timeoutMs = 0;
    }

    flushConditioned();
    dispatchPendingDesync();
    if((resyncPlan_||requestedResync_) && localTimeMs()>=resyncDeadline_) abortResync(ResyncResultReason::Deadline,"local resync deadline");
}

void NetworkClient::flushConditioned() {
    const auto now = localTimeMs();
    const auto generation = transportGeneration_;
    for (auto& pkt : outbound_.popDue(now)) {
        if (generation != transportGeneration_) return;
        if(!sendNow(pkt.bytes,pkt.reliable) && !pkt.bytes.empty()) {
            const auto type=static_cast<PacketType>(pkt.bytes.front());
            if(isDesyncDiagnosticPacket(type))log("Delayed diagnostic submission failed or its authority expired");
            if((resyncPlan_||requestedResync_) && (type==PacketType::ResyncRequest || type==PacketType::WorldEnvelope || (type>=PacketType::ResyncBegin&&type<=PacketType::ResyncEnd)))
                abortResync(ResyncResultReason::Overflow,"delayed authoritative submission failed");
        }
    }
    for (auto& pkt : inbound_.popDue(now)) {
        if (generation != transportGeneration_) return;
        onReceive(pkt.bytes.data(), pkt.bytes.size(), pkt.reliable);
    }
}

// ---------------------------------------------------------------------------
// Outbound
// ---------------------------------------------------------------------------

void NetworkClient::sendInput(const InputFrame& input) {
    if (!ready()) return;
    auto pkt = encode(input);
    sendPacket(pkt, false /* unreliable */);
}

void NetworkClient::sendHeartbeat() {
    if (!connected_) return;
    // Minimal heartbeat: just the framed header with empty payload.
    auto pkt = encodePacket(PacketType::Heartbeat, {});
    sendPacket(pkt, false);
}

void NetworkClient::sendAvatar(AvatarState avatar) {
    if (!ready()) return;
    if (avatar.seq == 0) {
        // Sequence zero is unavailable; do not wrap into an old sequence in
        // the same relay connection. A reconnect starts a new identity.
        if (avatarSeq_ == UINT32_MAX) return;
        avatar.seq = ++avatarSeq_;
    }
    if (avatar.serverTimeMs == 0) avatar.serverTimeMs = estimatedServerTimeMs();
    reviveLocal_ = avatar;
    reviveLocalMs_ = localTimeMs();
    sendPacket(encode(avatar, PacketType::AvatarState), false);
}

void NetworkClient::sendRoomTransition(const RoomTransition& m) {
    if (ready()) sendNativeWorld(encode(m), makeTestingWorldContext(), true);
}

void NetworkClient::sendEventHold(const EventHold& m) {
    if (ready()) sendNativeWorld(encode(m), makeTestingWorldContext(), true);
}

void NetworkClient::sendEnemyManifest(const EnemyManifest& m) {
    if (ready()) sendNativeWorld(encode(m), makeTestingWorldContext(), true);
}

void NetworkClient::sendEnemyHp(const EnemyHp& m) {
    if (ready()) sendNativeWorld(encode(m), makeTestingWorldContext(), false);
}

void NetworkClient::sendEnemyDeath(const EnemyDeath& m) {
    if (ready()) sendNativeWorld(encode(m), makeTestingWorldContext(), true);
}

bool NetworkClient::sendPartyLayout(const PartyLayout& m) {
    if(avatarLocalSlot_!=0)return false;
    try { return sendNativeWorld(encode(m),makeTestingWorldContext(),true); }
    catch(const std::exception&) { return false; }
}
bool NetworkClient::sendPartyIntent(const PartyIntent& m) {
    if(avatarLocalSlot_!=0)return false;
    try { return sendNativeWorld(encode(m),makeTestingWorldContext(),true); }
    catch(const std::exception&) { return false; }
}
bool NetworkClient::requestPartyReapply(const PartyReapply& m) {
    if(avatarLocalSlot_!=0 || m.reason!=PartyApplyReason::StoryForced)return false;
    try { return sendNativeWorld(encode(m),makeTestingWorldContext(),true); }
    catch(const std::exception&) { return false; }
}
void NetworkClient::sendReviveRequest(const ReviveRequest& m) {
    sendNativeWorld(encode(m), makeTestingWorldContext(), true);
}
void NetworkClient::sendRemoteHit(const RemoteHit& m) {
    if (ready() && avatarLocalSlot_ == 0) sendNativeWorld(encode(m), makeTestingWorldContext(), true);
}
void NetworkClient::sendTargetAuthority(const TargetAuthority& m) {
    if (ready() && avatarLocalSlot_ == 0) sendNativeWorld(encode(m), makeTestingWorldContext(), true);
}
void NetworkClient::sendHitClaim(const HitClaim& m) {
    if (ready()) sendNativeWorld(encode(m), makeTestingWorldContext(), true);
}

void NetworkClient::sendTransitionAck(const TransitionAck& m) {
    if (ready()) sendNativeWorld(encode(m), makeTestingWorldContext(), true);
}

void NetworkClient::sendProgressUpdate(const ProgressUpdate& m) {
    if (ready()) sendNativeWorld(encode(m), makeTestingWorldContext(), true);
}

void NetworkClient::sendRawPacket(const std::vector<std::uint8_t>& packet,
                                  bool reliable) {
    if(!ready()||packet.empty())return;
    const auto type=static_cast<PacketType>(packet.front());
    if(isScopedWorldPacket(type)||type==PacketType::ResyncRequest||
       (type>=PacketType::WorldBinding&&type<=PacketType::ResyncResult)||packet.front()>=0xF0)return;
    sendPacket(packet,reliable);
}

void NetworkClient::sendStateHash(const StateHash& m) {
    if (ready()) sendNativeWorld(encode(m), makeTestingWorldContext(), false);
}

bool NetworkClient::sendResyncRequest(const ResyncRequest& m) try {
    RequestSubmissionObservation observation;
    const bool sent=sendResyncRequestObserved(m,observation);
    recordResyncRequest(ResyncRequestOrigin::DirectRequest,m.targetMask,&m,sent?"submitted":"submission-failed",observation);
    return sent;
} catch (...) {
    // Escaping construction/allocation failures have no complete outcome row.
    if (callbacks_.onCausalDiagnostic) requestDiagnostics_.gap();
    throw;
}
bool NetworkClient::sendResyncRequestObserved(const ResyncRequest& m, RequestSubmissionObservation& observation) {
    if(!worldReady() || avatarLocalSlot_!=0 || m.key.sessionId!=avatarSessionId_ ||
       m.key.hostConnectionId!=avatarConnections_[0] || !hostRoom_ || !sameResyncRoom(m.room,*hostRoom_)) return false;
    if(resyncPlan_ && m.key!=resyncPlan_->request.key)return false;
    if(requestedResync_ && m.key!=requestedResync_->key)return false;
    for(std::size_t i=0;i<3;++i)if(m.connections[i]!=avatarConnections_[i])return false;
    try {
        const auto packet=encode(m);
        const bool initial=!resyncPlan_&&!requestedResync_;
        if(initial){requestedResync_=m;const auto started=localTimeMs();resyncDeadline_=started+RESYNC_TIMEOUT_MS;
            observation={true,started,resyncDeadline_};
            if(callbacks_.onCausalDiagnostic){originalDiagnosticRequestId_=m.key.requestId;originalDiagnosticDeadline_=observation;}
        } else if(callbacks_.onCausalDiagnostic && m.key.requestId==originalDiagnosticRequestId_)observation=originalDiagnosticDeadline_;
        const bool sent=sendPacket(packet,true);
        if(!sent&&initial)requestedResync_.reset();
        return sent;
    }catch(const std::exception&){return false;}
}

bool NetworkClient::admitDesync(const DesyncKey& key, std::uint64_t connectionId) const {
    if (!ready() || rejection_ || !desyncRequest_ || avatarLocalSlot_ >= 3 ||
        key != desyncRequest_->key || key.sessionId != avatarSessionId_ ||
        connectionId != avatarConnections_[avatarLocalSlot_] || localTimeMs() >= desyncDeadlineMs_) return false;
    for (std::size_t i=0;i<3;++i) if (desyncRequest_->connections[i] != avatarConnections_[i]) return false;
    return true;
}
void NetworkClient::dispatchPendingDesync() {
    if(!pendingDesyncRequest_)return;
    const auto now=localTimeMs();
    if(now>=pendingDesyncDeadlineMs_) {
        log("Pending desync capture expired before matching roster admission");
        pendingDesyncRequest_.reset();return;
    }
    const auto& request=*pendingDesyncRequest_;
    if(!ready() || rejection_ || request.key.sessionId!=avatarSessionId_)return;
    for(std::size_t i=0;i<3;++i)if(request.connections[i]!=avatarConnections_[i])return;
    if(request.key.reportId<=lastDesyncReportId_){pendingDesyncRequest_.reset();return;}
    auto admitted=request;
    admitted.remainingMs=static_cast<std::uint32_t>(pendingDesyncDeadlineMs_-now);
    desyncDeadlineMs_=pendingDesyncDeadlineMs_;
    desyncRequest_=admitted;lastDesyncReportId_=admitted.key.reportId;
    pendingDesyncRequest_.reset();
    if(callbacks_.onDesyncCaptureRequest)callbacks_.onDesyncCaptureRequest(admitted);
}
bool NetworkClient::sendDesyncArtifactChunk(const DesyncArtifactChunk& m) {
    if (!admitDesync(m.key,m.connectionId)) return false;
    try { return sendPacket(encode(m),true); } catch(const std::exception&) { return false; }
}
bool NetworkClient::sendDesyncCaptureDone(const DesyncCaptureDone& m) {
    if (!admitDesync(m.key,m.connectionId)) return false;
    try { return sendPacket(encode(m),true); } catch(const std::exception&) { return false; }
}

void NetworkClient::sendActivationRequest(const ActivationRequest& m) {
    if (ready()) sendNativeWorld(encode(m), makeTestingWorldContext(), false);
}

void NetworkClient::sendHostActivationPoint(const HostActivationPoint& m) {
    if (ready()) sendNativeWorld(encode(m), makeTestingWorldContext(), false);
}

void NetworkClient::sendClockPing() {
    if (!connected_) return;
    lastPingMs_ = localTimeMs();
    sendPacket(encode(ClockPing {lastPingMs_}), false);
}

std::uint64_t NetworkClient::localTimeMs() const {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    return static_cast<std::uint64_t>(ms + clockSkewMs_);
}

std::uint64_t NetworkClient::estimatedServerTimeMs() const {
    return static_cast<std::uint64_t>(
        static_cast<std::int64_t>(localTimeMs()) + clockOffsetMs_);
}

void NetworkClient::onClockPong(const ClockPong& pong) {
    const auto now = localTimeMs();
    if (pong.clientSendMs > now) return; // stale or bogus
    const auto rtt = static_cast<std::uint32_t>(now - pong.clientSendMs);
    lastRttMs_ = rtt;
    // The lowest-RTT sample has the least queuing asymmetry, so trust it.
    if (clockSamples_ == 0 || rtt <= bestRttMs_) {
        bestRttMs_ = rtt;
        clockOffsetMs_ = static_cast<std::int64_t>(pong.serverMs) +
                         static_cast<std::int64_t>(rtt / 2) -
                         static_cast<std::int64_t>(now);
    }
    ++clockSamples_;
}

NetworkClient::LinkStats NetworkClient::linkStats() const {
    LinkStats s;
    if (!connected_ || !transportPeer_) return s;
    s.valid = true;
    const auto stats = transport_->stats(transportPeer_);
    s.rttMs = stats.rttMs;
    s.rttVarMs = stats.rttVarianceMs;
    s.lossPermille = stats.lossPermille;
    s.appRttMs = lastRttMs_;
    for (const auto& w : avatarLoss_) {
        if (w.lastLossPermille == kNoAvatarLoss) continue;
        if (s.avatarLossPermille == kNoAvatarLoss || w.lastLossPermille > s.avatarLossPermille)
            s.avatarLossPermille = w.lastLossPermille;
    }
    return s;
}

void NetworkClient::noteAvatarSeq(std::uint8_t ownerSlot, std::uint32_t seq) {
    if (ownerSlot >= 3 || seq == 0) return;
    auto& w = avatarLoss_[ownerSlot];
    constexpr std::uint32_t kWindow = 300;
    if (!w.started) { // identity changes reset the whole window explicitly
        w = AvatarLossWindow {true, seq, seq, 1, w.lastLossPermille};
        return;
    }
    if (seq < w.firstSeq) return; // late packet from a closed window
    ++w.received;
    if (seq > w.maxSeq) w.maxSeq = seq;
    const std::uint32_t expected = w.maxSeq - w.firstSeq + 1;
    if (expected >= kWindow) {
        const std::uint32_t got = w.received < expected ? w.received : expected;
        w.lastLossPermille =
            static_cast<std::uint32_t>((static_cast<std::uint64_t>(expected - got) * 1000u) / expected);
        w.firstSeq = w.maxSeq + 1;
        w.received = 0;
    }
}

void NetworkClient::resetTransportState() {
    worldBinding_.reset();resyncPlan_.reset();requestedResync_.reset();hostRoom_.reset();resyncAssembler_.Reset();remoteDeliverySerials_={1,1,1};deliveryConnections_={};
    worldQuarantined_=false;worldSourceFloor_=0;testingSourceSerial_=0;resyncDeadline_=0;lastResyncPlanId_=0;completedResyncBegin_.reset();

    enemyHpSessionId_.clear();
    enemyHpHostConnectionId_ = 0;
    enemyHpSequence_ = 0;
    enemyMotionSequence_ = 0;
    enemyHpRoomEpoch_ = 0;
    desyncRequest_.reset();
    desyncDeadlineMs_ = 0;
    lastDesyncReportId_ = 0;
    lastObservedDesyncReportId_ = 0;
    pendingDesyncRequest_.reset();
    pendingDesyncDeadlineMs_ = 0;
    ++transportGeneration_;
    inbound_.reset();
    outbound_.reset();
    avatarRosterValid_ = false;
    avatarSessionId_.clear();
    avatarLocalSlot_ = 0xFF;
    for (std::size_t i = 0; i < 3; ++i) {
        avatarConnections_[i] = 0;
        avatarLastSeq_[i] = 0;
        avatarLoss_[i] = {};
    }
    avatarSeq_ = 0;
    partyLayout_.reset(); partyVersion_=0;
    partyIntents_.clear(); partyIntentVersion_=0;
    reviveLocal_ = {}; reviveLocalMs_ = 0; receivedReviveEpisode_ = 0;
    receivedRemoteHitHost_ = 0; receivedRemoteHitSeq_ = 0;
    receivedTargetAuthorityHost_ = 0; receivedTargetAuthoritySeq_ = 0;
    clockOffsetMs_ = 0;
    bestRttMs_ = 0;
    lastRttMs_ = 0;
    clockSamples_ = 0;
    lastPingMs_ = 0;
    hadReady_ = false;
    rejection_.reset();
}

bool NetworkClient::SetResumePin(std::optional<SessionResumePin> pin) {
    if (connected_ || attemptActive_) return false;
    if (pin && (pin->sessionId.empty() || pin->hostPeerId.empty() ||
        pin->hostConnectionId == 0 || pin->localPeerId != peerId_ ||
        (pin->localSlot != SlotType::Friend1 && pin->localSlot != SlotType::Friend2) ||
        (requestedSlot_ && *requestedSlot_ != pin->localSlot))) return false;
    resumePin_ = std::move(pin);
    return true;
}

bool NetworkClient::matchesResumePin(const SessionState& session) const {
    if (!resumePin_) return true;
    const auto& pin = *resumePin_;
    if (session.sessionId != pin.sessionId) return false;
    bool host = false, self = false;
    for (const auto& actor : session.actors) {
        if (actor.slot == SlotType::Player)
            host = actor.ownerPeerId == pin.hostPeerId && actor.connectionId == pin.hostConnectionId;
        if (actor.slot == pin.localSlot)
            self = actor.ownerPeerId == pin.localPeerId && actor.connectionId != 0;
    }
    return host && self;
}

void NetworkClient::closeChangedSession() {
    if (transportPeer_) transport_->disconnect(transportPeer_, static_cast<std::uint32_t>(DisconnectReason::SessionChanged));
    onDisconnect(static_cast<std::uint32_t>(DisconnectReason::SessionChanged), true);
}

bool NetworkClient::updateAvatarRoster(const SessionState& session) {
    std::uint64_t connections[3]{};
    std::uint8_t localSlot = 0xFF;
    bool valid = !session.sessionId.empty() && session.gameBuild == gameBuild_ &&
                 session.modHash == modHash_ && session.actors.size() <= 3;
    for (const auto& actor : session.actors) {
        const auto slot = static_cast<std::uint8_t>(actor.slot);
        if (slot >= 3 || actor.connectionId == 0 || connections[slot] != 0) {
            valid = false;
            continue;
        }
        for (const auto connection : connections)
            if (connection == actor.connectionId) valid = false;
        connections[slot] = actor.connectionId;
        if (actor.ownerPeerId == peerId_) {
            if (localSlot != 0xFF || (requestedSlot_ && *requestedSlot_ != actor.slot))
                valid = false;
            localSlot = slot;
        }
    }
    valid = valid && connections[0] != 0 && localSlot < 3;
    if (valid && (enemyHpSessionId_ != session.sessionId ||
                  enemyHpHostConnectionId_ != connections[0])) {
        worldBinding_.reset();resyncPlan_.reset();requestedResync_.reset();resyncAssembler_.Reset();hostRoom_.reset();worldSourceFloor_=0;worldQuarantined_=false;lastResyncPlanId_=0;
        enemyHpSessionId_ = session.sessionId;
        enemyHpHostConnectionId_ = connections[0];
        enemyHpSequence_ = 0;
        enemyMotionSequence_ = 0;
        enemyHpRoomEpoch_ = 0;
    }
    const bool namespaceChanged = !valid || !avatarRosterValid_ ||
        avatarSessionId_ != session.sessionId || avatarLocalSlot_ != localSlot ||
        avatarConnections_[0] != connections[0] ||
        (localSlot < 3 && avatarConnections_[localSlot] != connections[localSlot]);
    if(!valid || (avatarRosterValid_ && namespaceChanged)) {
        if(pendingDesyncRequest_)log("Pending desync capture retired with roster authority");
        pendingDesyncRequest_.reset();desyncRequest_.reset();
    }
    if (namespaceChanged) { partyLayout_.reset(); partyVersion_=0; partyIntents_.clear(); partyIntentVersion_=0; }
    if (namespaceChanged) { reviveLocal_ = {}; reviveLocalMs_ = 0; }
    if (namespaceChanged) outbound_.reset(); // retire already-conditioned authoritative sends
    for (std::size_t i = 0; i < 3; ++i) {
        if (namespaceChanged || avatarConnections_[i] != connections[i]) {
            partyLayout_.reset();
            partyIntents_.clear(); // roster-pinned; the version floor stays with the host connection
            avatarLastSeq_[i] = 0;
            avatarLoss_[i] = {};

        }
        if(valid&&deliveryConnections_[i]!=connections[i]){deliveryConnections_[i]=connections[i];remoteDeliverySerials_[i]=1;}
        avatarConnections_[i] = valid ? connections[i] : 0;
    }
    avatarRosterValid_ = valid;
    avatarLocalSlot_ = valid ? localSlot : 0xFF;
    avatarSessionId_ = valid ? session.sessionId : std::string{};
    return valid;
}

bool NetworkClient::admitAvatar(const AvatarRelay& relay) {
    const auto slot = static_cast<std::uint8_t>(relay.avatar.ownerSlot);
    if (!connected_ || !avatarRosterValid_ || slot >= 3 || slot == avatarLocalSlot_ ||
        relay.ownerConnectionId == 0 || avatarConnections_[slot] != relay.ownerConnectionId ||
        relay.avatar.seq == 0 || relay.avatar.seq <= avatarLastSeq_[slot])
        return false;
    // Nonwrapping sequence within one authenticated relay connection. This is
    // admission, not interpolation: a replacement connection may restart at 1.
    avatarLastSeq_[slot] = relay.avatar.seq;
    return true;
}

void NetworkClient::setLinkConditions(const LinkConditions& outbound,
                                      const LinkConditions& inbound) {
    outbound_.configure(outbound);
    inbound_.configure(inbound);
}

// ---------------------------------------------------------------------------
// Disconnect
// ---------------------------------------------------------------------------

void NetworkClient::disconnect() {
    attemptActive_ = false;
    connected_ = false;
    resetTransportState();
    if (transportPeer_) {
        transport_->disconnect(transportPeer_, 0);
        // Flush.
        if (transport_->isOpen()) {
            TransportEvent event;
            while (transport_->service(event, 100) > 0) {
                if (event.type == TransportEventType::Receive)
                    event.packet.reset();
            }
        }
        transportPeer_ = nullptr;
    }
    if (transport_->isOpen()) {
        transport_->close();
    }
    connected_ = false;
}

// ---------------------------------------------------------------------------
// ENet event handlers
// ---------------------------------------------------------------------------

void NetworkClient::onConnect() {
    resetTransportState();
    connected_ = true;
    log("Connected to host. Sending ClientHello handshake...");

    // Send a dedicated ClientHello packet for the version handshake.
    // This replaces the old SessionState-as-hello pattern (B2 cleanup).
    ClientHello hello;
    hello.protocolVersion = protocolVersion_;
    hello.gameBuild = gameBuild_;
    hello.contentHash = contentHash_;
    hello.modHash = modHash_;
    hello.peerId = peerId_;
    hello.peerName = peerName_.empty() ? peerId_ : peerName_;
    hello.requestedMode = requestedMode_;
    hello.requestedSlot = resumePin_ ? static_cast<std::uint8_t>(resumePin_->localSlot) : requestedSlot_.has_value()
                              ? static_cast<std::uint8_t>(*requestedSlot_)
                              : 0xFF;
    auto pkt = encode(hello);
    sendPacket(pkt, true /* reliable */);

    if (callbacks_.onConnected) callbacks_.onConnected();
}

void NetworkClient::onDisconnect(std::uint32_t code, bool local) {
    if (!attemptActive_) return;
    ClientCloseInfo info;
    info.rawCode = code;
    info.reason = static_cast<DisconnectReason>(rejection_ ? rejection_->code : code);
    if (rejection_ && rejection_->code == 0) info.reason = DisconnectReason::AdmissionRejected;
    info.rejection = rejection_;
    info.hadReady = hadReady_;
    info.local = local;
    attemptActive_ = false;
    connected_ = false;
    transportPeer_ = nullptr;
    resetTransportState();
    log(code == 0 ? std::string("Disconnected from host.")
                  : "Disconnected from host (code " + std::to_string(code) + ").");
    if (callbacks_.onDisconnected) callbacks_.onDisconnected();
    if (callbacks_.onClosed) callbacks_.onClosed(info);
}

void NetworkClient::onReceive(const std::uint8_t* data, std::size_t size, bool reliable, const WorldScope* admittedScope,
                              const std::uint8_t* admittedWire, std::size_t admittedWireSize) {
    if (!connected_) return;
    // A malformed SessionState must invalidate consumers too, even when its
    // frame/header fails before the switch below. Never forward malformed data.
    const bool sessionPacket = size > 0 &&
        data[0] == static_cast<std::uint8_t>(PacketType::SessionState);
    bool sessionNotified = false;
    try {
        const std::uint8_t* payload = nullptr;
        std::size_t payloadSize = 0;
        auto type = decodePacketHeader(data, size, payload, payloadSize);
        ByteReader reader(payload, payloadSize);

        if((resyncPlan_||requestedResync_) && localTimeMs()>=resyncDeadline_) {
            const auto generation=transportGeneration_;
            abortResync(ResyncResultReason::Deadline,"local resync deadline before delivery");
            if(generation!=transportGeneration_ || !connected_)return;
            // Do not let a phase/End/result delivered at expiry revive or
            // certify the retired transaction, including conditioner delivery.
            if(type>=PacketType::ResyncPlan && type<=PacketType::ResyncResult)return;
        }

        // No authoritative delivery before a valid roster and resume pin.
        // Clock/heartbeat transport liveness remains independent of readiness.
        if ((!ready() || rejection_) && type != PacketType::SessionState &&
            type != PacketType::ClockPong && type != PacketType::HelloReject &&
            !(type==PacketType::DesyncCaptureRequest && !rejection_)) return;
        if (rejection_ && type == PacketType::SessionState) return;

        if (isScopedWorldPacket(type) && !admittedScope) return; // no unscoped wire bypass
        if (type==PacketType::WorldEnvelope) {
            WorldEnvelope m;read(reader,m);
            if(!hasCurrentWorldBinding() || m.scope.sessionId!=avatarSessionId_ ||
               m.scope.targetConnectionId!=worldBinding_->selfConnectionId ||
               m.scope.targetDeliverySerial!=worldBinding_->deliverySerial) return;
            bool source=false;for(std::size_t i=0;i<3;++i)if(avatarConnections_[i]&&avatarConnections_[i]==m.scope.sourceConnectionId&&m.scope.sourceDeliverySerial==remoteDeliverySerials_[i])source=true;
            const auto inner=static_cast<PacketType>(m.packet.front());
            // VUH-1504 hop log (diagnostic only, bounded): envelope-level drops of a ReviveRequest.
            static unsigned reviveEnvelopeLogs = 0;
            const auto reviveDrop = [&](const char* why) {
                if (inner == PacketType::ReviveRequest && reviveEnvelopeLogs < 32) { ++reviveEnvelopeLogs; log(std::string("[revive-hop] envelope drop reason=") + why); }
            };
            if(!source){reviveDrop("source-not-roster-or-delivery");return;}
            if(m.scope.sourceConnectionId==avatarConnections_[0] && m.scope.kind==WorldSourceKind::Native && inner!=PacketType::DesyncNotice &&
               (!m.scope.hostSourceSerial || m.scope.hostSourceSerial<=worldSourceFloor_)){reviveDrop("host-source-floor");return;}
            if(worldQuarantined_ && !isEphemeralWorldPacket(inner)){reviveDrop("quarantined");return;}
            onReceive(m.packet.data(),m.packet.size(),reliable,&m.scope,data,size);return;
        }
        if (isEphemeralWorldPacket(type))
            validateActivationPacket(std::vector<std::uint8_t>(data, data + size));

        // Admit before BOTH raw bridge delivery and typed callbacks. Parsing a
        // prefix must never advance the sequence floor or publish stale bytes.
        std::optional<RoomTransition> admittedRoom;
        std::optional<EnemyHp> admittedHp;
        if (type == PacketType::RoomTransition) {
            RoomTransition room;
            readRoomTransitionPacket(reader, room);
            if (!reader.atEnd() || size != payloadSize + 3)
                throw std::runtime_error("RoomTransition: wrong frame/payload length");
            partyLayout_.reset();
            enemyHpRoomEpoch_ = room.epoch;
            hostRoom_=room;
            admittedRoom = room;
        } else if (type == PacketType::EnemyHp) {
            EnemyHp hp;
            read(reader, hp);
            if (!hp.epoch || hp.epoch != enemyHpRoomEpoch_ ||
                hp.sequence < enemyHpSequence_ ||
                (hp.sequence == enemyHpSequence_ && !reliable)) return;
            // Equal reliable packets are trusted relay cache unions. They can
            // restore state after a targeted resend without minting a sequence.
            enemyHpSequence_ = hp.sequence;
            admittedHp = std::move(hp);
        } else if (type == PacketType::EnemyMotion) {
            // VUH-1515: periodic host pose/motion. Current room epoch and a
            // strictly increasing sequence; never cached, never replayed.
            EnemyMotion motion;
            read(reader, motion);
            if (!reader.atEnd() || size != payloadSize + 3 || !motion.epoch ||
                motion.epoch != enemyHpRoomEpoch_ || motion.sequence <= enemyMotionSequence_) return;
            enemyMotionSequence_ = motion.sequence;
        }

        if(type==PacketType::PopulationCut) {
            PopulationCut cut;read(reader,cut);
            if(!admittedScope || admittedScope->kind!=WorldSourceKind::Native ||
               admittedScope->sourceConnectionId!=avatarConnections_[0] || !hostRoom_ ||
               cut.epoch!=hostRoom_->epoch || cut.location!=NativeRecordLocation{hostRoom_->worldId,hostRoom_->roomId,
                   hostRoom_->door,hostRoom_->mapProgram,hostRoom_->battleProgram,hostRoom_->eventProgram})return;
            reader=ByteReader(payload,payloadSize);
        }

        if(type==PacketType::PartyLayout) {
            PartyLayout m;read(reader,m);
            std::array<std::uint64_t,3> roster{avatarConnections_[0],avatarConnections_[1],avatarConnections_[2]};
            if(!admittedScope || admittedScope->kind!=WorldSourceKind::Native ||
               admittedScope->sourceConnectionId!=avatarConnections_[0] || !hostRoom_ ||
               !sameResyncRoom(m.location,*hostRoom_) || !validPartyLayout(m,roster) || m.version<=partyVersion_)return;
            partyVersion_=m.version;partyLayout_=m;reader=ByteReader(payload,payloadSize);
        } else if(type==PacketType::PartyIntent) {
            // VUH-1786: room-independent, so no hostRoom_ match; never cleared by a room transition.
            PartyIntent m;read(reader,m);
            std::array<std::uint64_t,3> roster{avatarConnections_[0],avatarConnections_[1],avatarConnections_[2]};
            if(!reader.atEnd() || !admittedScope || admittedScope->kind!=WorldSourceKind::Native ||
               admittedScope->sourceConnectionId!=avatarConnections_[0] || !validPartyIntent(m,roster) ||
               m.version<=partyIntentVersion_)return;
            auto same=std::find_if(partyIntents_.begin(),partyIntents_.end(),[&](const PartyIntent& x){return x.target==m.target;});
            if(same==partyIntents_.end() && partyIntents_.size()>=8)return; // store full: refused, not forwarded
            partyIntentVersion_=m.version;
            if(same!=partyIntents_.end())*same=m;
            else partyIntents_.push_back(m);
            reader=ByteReader(payload,payloadSize);
        } else if(type==PacketType::PartyReapply) {
            PartyReapply m;read(reader,m);
            if(!admittedScope || admittedScope->kind!=WorldSourceKind::Relay ||
               admittedScope->sourceConnectionId!=avatarConnections_[0] || !hostRoom_ ||
               !sameResyncRoom(m.location,*hostRoom_) || m.afterVersion<partyVersion_)return;
            partyVersion_=m.afterVersion;partyLayout_.reset();reader=ByteReader(payload,payloadSize);
        }
        if (type == PacketType::ReviveRequest) {
            ReviveRequest revive; read(reader, revive);
            const auto now = localTimeMs();
            if (!admittedScope || !hostRoom_ || !sameResyncRoom(revive.location,*hostRoom_) ||
                revive.targetSlot != avatarLocalSlot_ || revive.requesterSlot >= 3 ||
                revive.requesterSlot == avatarLocalSlot_ || !revive.seq ||
                !revive.requesterConnectionId || revive.requesterConnectionId != avatarConnections_[revive.requesterSlot] ||
                admittedScope->sourceConnectionId != revive.requesterConnectionId ||
                !revive.targetConnectionId || revive.targetConnectionId != avatarConnections_[avatarLocalSlot_] ||
                !reviveLocalMs_ || now < reviveLocalMs_ || now-reviveLocalMs_ > REVIVE_AVATAR_MAX_AGE_MS ||
                !(reviveLocal_.flags & AvatarDowned) || (reviveLocal_.flags & AvatarInCutscene) ||
                reviveLocal_.hp != 0 || reviveLocal_.downedDelivery != deliverySerial() || reviveLocal_.downedEpoch != revive.location.epoch ||
                reviveLocal_.worldId != revive.location.worldId || reviveLocal_.roomId != revive.location.roomId ||
                !revive.targetEpisode || revive.targetEpisode != reviveLocal_.downedEpisode ||
                revive.targetEpisode <= receivedReviveEpisode_) {
                // VUH-1504 hop log (diagnostic only, bounded): name the first failing check.
                static unsigned reviveDropLogs = 0;
                if (reviveDropLogs < 32) {
                    ++reviveDropLogs;
                    const char* why = !admittedScope ? "no-scope" : !hostRoom_ ? "no-host-room" :
                        !sameResyncRoom(revive.location,*hostRoom_) ? "room-mismatch" :
                        revive.targetSlot != avatarLocalSlot_ ? "not-our-slot" :
                        revive.requesterSlot >= 3 || revive.requesterSlot == avatarLocalSlot_ ? "requester-slot" :
                        !revive.seq ? "sequence" :
                        !revive.requesterConnectionId || revive.requesterConnectionId != avatarConnections_[revive.requesterSlot] ||
                            admittedScope->sourceConnectionId != revive.requesterConnectionId ? "requester-connection" :
                        !revive.targetConnectionId || revive.targetConnectionId != avatarConnections_[avatarLocalSlot_] ? "target-connection" :
                        !reviveLocalMs_ || now < reviveLocalMs_ || now-reviveLocalMs_ > REVIVE_AVATAR_MAX_AGE_MS ? "local-avatar-stale" :
                        !(reviveLocal_.flags & AvatarDowned) ? "local-not-downed" :
                        (reviveLocal_.flags & AvatarInCutscene) ? "local-in-cutscene" :
                        reviveLocal_.hp != 0 ? "local-hp-nonzero" :
                        reviveLocal_.downedDelivery != deliverySerial() ? "local-delivery" :
                        reviveLocal_.downedEpoch != revive.location.epoch ? "local-epoch" :
                        reviveLocal_.worldId != revive.location.worldId || reviveLocal_.roomId != revive.location.roomId ? "local-room" :
                        !revive.targetEpisode || revive.targetEpisode != reviveLocal_.downedEpisode ? "episode-mismatch" : "episode-already-received";
                    log(std::string("[revive-hop] target drop reason=") + why + " hp=" + std::to_string(reviveLocal_.hp) +
                        " flags=" + std::to_string(reviveLocal_.flags) + " episode=" + std::to_string(revive.targetEpisode) +
                        " localEpisode=" + std::to_string(reviveLocal_.downedEpisode));
                }
                return;
            }
            receivedReviveEpisode_ = revive.targetEpisode;
            {
                static unsigned reviveDeliverLogs = 0;
                if (reviveDeliverLogs < 32) { ++reviveDeliverLogs; log("[revive-hop] target deliver episode=" + std::to_string(revive.targetEpisode) + " seq=" + std::to_string(revive.seq)); }
            }
            reader = ByteReader(payload,payloadSize);
        }

        if (type == PacketType::RemoteHit) {
            RemoteHit hit; read(reader, hit);
            const auto host = avatarConnections_[0];
            const auto floor = host == receivedRemoteHitHost_ ? receivedRemoteHitSeq_ : 0;
            const char* why = !admittedScope || admittedScope->kind != WorldSourceKind::Native ? "no-native-scope" :
                !host || admittedScope->sourceConnectionId != host || hit.hostConnectionId != host ? "host-connection" :
                !hostRoom_ || !sameResyncRoom(hit.location,*hostRoom_) ? "room-mismatch" :
                avatarLocalSlot_ == 0 || avatarLocalSlot_ >= 3 || hit.targetSlot != avatarLocalSlot_ ? "not-our-slot" :
                !hit.targetConnectionId || hit.targetConnectionId != avatarConnections_[avatarLocalSlot_] ? "target-connection" :
                !hit.seq || hit.seq <= floor ? "sequence" :
                hit.damage < 1 || hit.damage > REMOTE_HIT_MAX_DAMAGE ? "damage" : nullptr;
            static unsigned remoteHitLogs = 0;
            if (why) {
                // VUH-1515 (diagnostic only, bounded): name the first failing check.
                if (remoteHitLogs < 32) { ++remoteHitLogs; log(std::string("[remote-hit] target drop reason=") + why + " seq=" + std::to_string(hit.seq)); }
                return;
            }
            receivedRemoteHitHost_ = host; receivedRemoteHitSeq_ = hit.seq;
            if (remoteHitLogs < 32) {
                ++remoteHitLogs;
                log("[remote-hit] target deliver seq=" + std::to_string(hit.seq) + " netId=" + std::to_string(hit.netId) +
                    " damage=" + std::to_string(hit.damage));
            }
            reader = ByteReader(payload,payloadSize);
        }
        if (type == PacketType::TargetAuthority) {
            TargetAuthority authority; read(reader, authority);
            const auto host = avatarConnections_[0];
            const auto floor = host == receivedTargetAuthorityHost_ ? receivedTargetAuthoritySeq_ : 0;
            const char* why = !admittedScope || admittedScope->kind != WorldSourceKind::Native ? "no-native-scope" :
                !host || admittedScope->sourceConnectionId != host || authority.hostConnectionId != host ? "host-connection" :
                !hostRoom_ || !sameResyncRoom(authority.location,*hostRoom_) ? "room-mismatch" :
                !authority.seq || authority.seq <= floor ? "sequence" : nullptr;
            static unsigned targetAuthorityLogs = 0;
            if (why) {
                // VUH-1515 (diagnostic only, bounded): name the first failing check.
                if (targetAuthorityLogs < 32) { ++targetAuthorityLogs; log(std::string("[target-authority] drop reason=") + why + " seq=" + std::to_string(authority.seq)); }
                return;
            }
            receivedTargetAuthorityHost_ = host; receivedTargetAuthoritySeq_ = authority.seq;
            if (targetAuthorityLogs < 32) {
                ++targetAuthorityLogs;
                log("[target-authority] deliver seq=" + std::to_string(authority.seq) + " slotMask=" +
                    std::to_string(authority.slotMask) + " mode=" + std::to_string(authority.mode));
            }
            reader = ByteReader(payload,payloadSize);
        }

        // Actual accepted inner body and original received envelope, before bridge callbacks.
        // Diagnostic failure must neither fabricate a delivery nor change admission.
        if (admittedScope && callbacks_.onCausalDiagnostic) {
            worldAdmissionDiagnostics_.emit(callbacks_.onCausalDiagnostic,"world-envelope-admission",[&](auto& out) {
                out << " action=admitted session=" << avatarSessionId_
                    << " host=" << avatarConnections_[0] << " self=" << (worldBinding_?worldBinding_->selfConnectionId:0)
                    << " slot=" << unsigned(avatarLocalSlot_) << " delivery=" << (worldBinding_?worldBinding_->deliverySerial:0)
                    << " transportGeneration=" << transportGeneration_
                    << " scopeSession=" << admittedScope->sessionId << " source=" << admittedScope->sourceConnectionId
                    << " sourceDelivery=" << admittedScope->sourceDeliverySerial << " hostSource=" << admittedScope->hostSourceSerial
                    << " scopeKind=" << unsigned(admittedScope->kind) << " target=" << admittedScope->targetConnectionId
                    << " targetDelivery=" << admittedScope->targetDeliverySerial << " packetType=" << unsigned(type)
                    << " bytes=" << size << " payloadSHA=" << causalSha(std::vector<std::uint8_t>(data,data+size))
                    << " wireAvailable=" << (admittedWire!=nullptr && admittedWireSize!=0)
                    << " wireBytes=" << admittedWireSize
                    << " wireSHA=" << (admittedWire && admittedWireSize ? causalSha(std::vector<std::uint8_t>(admittedWire,admittedWire+admittedWireSize)) : "-")
                    << " reliable=" << reliable << " callbackDelivered=unproven nativeConsumed=unproven";
            });
        }
        if(admittedScope && callbacks_.onWorldEnvelope) {
            const auto generation=transportGeneration_;
            callbacks_.onWorldEnvelope(WorldEnvelope{*admittedScope,{data,data+size}});
            if(generation!=transportGeneration_)return;
        }
        if (isWorldPacket(type) && callbacks_.onWorldPacket) {
            const auto generation = transportGeneration_;
            callbacks_.onWorldPacket(std::vector<std::uint8_t>(data, data + size));
            if (generation != transportGeneration_) return;
        }

        switch (type) {
            case PacketType::WorldBinding: {
                WorldBinding m;read(reader,m);
                if(m.sessionId!=avatarSessionId_ || m.hostConnectionId!=avatarConnections_[0] ||
                   m.selfSlot!=avatarLocalSlot_ || m.selfConnectionId!=avatarConnections_[avatarLocalSlot_] ||
                   (worldBinding_ && m.deliverySerial<worldBinding_->deliverySerial))return;
                const bool changed=!worldBinding_ || m.deliverySerial!=worldBinding_->deliverySerial;
                worldBinding_=m;
                if(changed && m.deliverySerial>1)worldQuarantined_=true;
                if(callbacks_.onWorldBinding)callbacks_.onWorldBinding(m);
                break;
            }
            case PacketType::ResyncPlan: {
                ResyncPlan m;read(reader,m);
                if(!hasCurrentWorldBinding() || m.request.key.sessionId!=avatarSessionId_ || m.request.key.hostConnectionId!=avatarConnections_[0])return;
                bool selected=avatarLocalSlot_==0;
                for(std::size_t i=0;i<m.targetCount;++i)if(m.targets[i].slot==avatarLocalSlot_ && m.targets[i].connectionId==worldBinding_->selfConnectionId && m.targets[i].deliverySerial==worldBinding_->deliverySerial)selected=true;
                if(!selected)return;
                if(!resyncPlan_&&m.request.key.requestId<=lastResyncPlanId_)return;
                if(resyncPlan_ && m.request.key==resyncPlan_->request.key &&
                   (m.targets!=resyncPlan_->targets || m.targetCount!=resyncPlan_->targetCount || encode(m.request)!=encode(resyncPlan_->request))) {
                    abortResync(ResyncResultReason::InvalidSnapshot,"conflicting immutable plan");return;
                }
                if(resyncPlan_&&m.request.key==resyncPlan_->request.key&&m.phase==resyncPlan_->phase){
                    if(m.targets!=resyncPlan_->targets||m.targetCount!=resyncPlan_->targetCount||encode(m.request)!=encode(resyncPlan_->request)){abortResync(ResyncResultReason::InvalidSnapshot,"conflicting plan");return;}
                    resyncDeadline_=(std::min)(resyncDeadline_,localTimeMs()+m.remainingMs);return;
                }
                if(resyncPlan_ && (m.request.key!=resyncPlan_->request.key || static_cast<unsigned>(m.phase)<static_cast<unsigned>(resyncPlan_->phase)))return;
                if(!resyncPlan_&&!requestedResync_)resyncDeadline_=localTimeMs()+m.remainingMs;
                else resyncDeadline_=(std::min)(resyncDeadline_,localTimeMs()+m.remainingMs);
                if(!resyncPlan_ || m.phase!=resyncPlan_->phase)resyncAssembler_.Reset();
                for(std::size_t i=0;i<m.targetCount;++i)remoteDeliverySerials_[m.targets[i].slot]=m.targets[i].deliverySerial;
                lastResyncPlanId_=m.request.key.requestId;
                requestedResync_.reset();
                partyLayout_.reset(); // only an admitted plan retires authority
                resyncPlan_=m;
                if(avatarLocalSlot_!=0)worldQuarantined_=true;
                if(localTimeMs()>=resyncDeadline_){abortResync(ResyncResultReason::Deadline,"local resync deadline before phase delivery");return;}
                if(callbacks_.onResyncPlan)callbacks_.onResyncPlan(m);
                break;
            }
            case PacketType::ResyncBegin: {
                ResyncBegin m;read(reader,m);
                if(!resyncPlan_ || m.key!=resyncPlan_->request.key || m.phase!=resyncPlan_->phase || m.targets!=resyncPlan_->targets || m.targetCount!=resyncPlan_->targetCount || !sameResyncRoom(m.room,resyncPlan_->request.room))return;
                if(completedResyncBegin_ && m.key==completedResyncBegin_->key && m.phase==completedResyncBegin_->phase){
                    if(encode(m)!=encode(*completedResyncBegin_))abortResync(ResyncResultReason::InvalidSnapshot,"snapshot is immutable within a phase");
                    return;
                }
                if(!resyncAssembler_.Begin(m))abortResync(ResyncResultReason::InvalidSnapshot,"invalid begin");
                break;
            }
            case PacketType::ResyncPart: {
                ResyncPart m;read(reader,m);if(!resyncPlan_ || m.key!=resyncPlan_->request.key)return;
                if(completedResyncBegin_ && m.key==completedResyncBegin_->key && m.phase==completedResyncBegin_->phase)return;
                if(!resyncAssembler_.Part(m))abortResync(ResyncResultReason::InvalidSnapshot,"invalid part");
                break;
            }
            case PacketType::ResyncEnd: {
                ResyncEnd m;read(reader,m);if(!resyncPlan_ || m.key!=resyncPlan_->request.key)return;
                if(completedResyncBegin_ && m.key==completedResyncBegin_->key && m.phase==completedResyncBegin_->phase)return;
                const auto begin=resyncAssembler_.Header();auto snapshot=resyncAssembler_.End(m);
                if(!begin || !snapshot){abortResync(ResyncResultReason::InvalidSnapshot,"invalid end");break;}
                if(localTimeMs()>=resyncDeadline_){abortResync(ResyncResultReason::Deadline,"local resync deadline before snapshot delivery");return;}
                worldSourceFloor_=begin->snapshotCut;enemyHpRoomEpoch_=snapshot->room.epoch;hostRoom_=snapshot->room;
                enemyHpSequence_=(std::max)(enemyHpSequence_,snapshot->hpSequence);
                worldQuarantined_=false;
                completedResyncBegin_=*begin;
                if(callbacks_.onResyncSnapshot)callbacks_.onResyncSnapshot(*begin,*snapshot);
                break;
            }
            case PacketType::ResyncResult: {
                ResyncResult m;read(reader,m);
                if(!resyncPlan_){if(requestedResync_&&m.key==requestedResync_->key){requestedResync_.reset();if(callbacks_.onResyncResult)callbacks_.onResyncResult(m);}return;}
                if(m.key!=resyncPlan_->request.key)return;
                if(m.reason!=ResyncResultReason::Converged && avatarLocalSlot_!=0)worldQuarantined_=true;
                resyncPlan_.reset();resyncAssembler_.Reset();
                if(callbacks_.onResyncResult)callbacks_.onResyncResult(m);
                break;
            }
            case PacketType::SessionState: {
                SessionState ss;
                read(reader, ss);
                if (!reader.atEnd() || size != payloadSize + 3)
                    throw std::runtime_error("SessionState: trailing bytes");
                const bool valid = updateAvatarRoster(ss);
                if (resumePin_ && (!valid || !matchesResumePin(ss))) {
                    sessionNotified = true;
                    closeChangedSession();
                    return;
                }
                if (valid) hadReady_ = true;
                sessionNotified = true;
                if (callbacks_.onSessionState)
                    callbacks_.onSessionState(valid ? ss : SessionState{});
                if (!valid) log("Invalid SessionState: avatar roster cleared.");
                break;
            }
            case PacketType::ActorSnapshot: {
                ActorSnapshot snap;
                read(reader, snap);
                if (callbacks_.onActorSnapshot) callbacks_.onActorSnapshot(snap);
                break;
            }
            case PacketType::EnemySnapshot: {
                EnemySnapshot snap;
                read(reader, snap);
                if (callbacks_.onEnemySnapshot) callbacks_.onEnemySnapshot(snap);
                break;
            }
            case PacketType::EventMessage: {
                EventMessage evt;
                read(reader, evt);
                if (callbacks_.onEvent) callbacks_.onEvent(evt);
                break;
            }
            case PacketType::AvatarRelay: {
                AvatarRelay relay;
                read(reader, relay);
                if (!admitAvatar(relay)) break;
                noteAvatarSeq(static_cast<std::uint8_t>(relay.avatar.ownerSlot), relay.avatar.seq);
                if (callbacks_.onAvatarState) callbacks_.onAvatarState(relay);
                break;
            }
            case PacketType::RoomTransition: {
                if (callbacks_.onRoomTransition) callbacks_.onRoomTransition(*admittedRoom);
                break;
            }
            case PacketType::ActivationRequest: {
                ActivationRequest m;
                read(reader, m);
                if (callbacks_.onActivationRequest) callbacks_.onActivationRequest(m);
                break;
            }
            case PacketType::HostActivationPoint: {
                HostActivationPoint m;
                read(reader, m);
                if (callbacks_.onHostActivationPoint) callbacks_.onHostActivationPoint(m);
                break;
            }
            case PacketType::EventHold: {
                EventHold m;
                read(reader, m);
                if (callbacks_.onEventHold) callbacks_.onEventHold(m);
                break;
            }
            case PacketType::EnemyManifest: {
                EnemyManifest m;
                read(reader, m);
                if (callbacks_.onEnemyManifest) callbacks_.onEnemyManifest(m);
                break;
            }
            case PacketType::EnemyHp: {
                if (callbacks_.onEnemyHp) callbacks_.onEnemyHp(*admittedHp);
                break;
            }
            case PacketType::EnemyDeath: {
                EnemyDeath m;
                read(reader, m);
                if (callbacks_.onEnemyDeath) callbacks_.onEnemyDeath(m);
                break;
            }
            case PacketType::EnemyMotion:
                break; // raw world delivery only (VUH-1515)
            case PacketType::PartyLayout: {
                PartyLayout m;read(reader,m);if(callbacks_.onPartyLayout)callbacks_.onPartyLayout(m);break;
            }
            case PacketType::PartyReapply: {
                PartyReapply m;read(reader,m);if(callbacks_.onPartyReapply)callbacks_.onPartyReapply(m);break;
            }
            case PacketType::PartyIntent: {
                PartyIntent m;read(reader,m);if(callbacks_.onPartyIntent)callbacks_.onPartyIntent(m);break;
            }
            case PacketType::ReviveRequest: {
                ReviveRequest m; read(reader,m);
                if (callbacks_.onReviveRequest) callbacks_.onReviveRequest(m);
                break;
            }
            case PacketType::RemoteHit: {
                RemoteHit m; read(reader,m);
                if (callbacks_.onRemoteHit) callbacks_.onRemoteHit(m);
                break;
            }
            case PacketType::TargetAuthority: {
                TargetAuthority m; read(reader,m);
                if (callbacks_.onTargetAuthority) callbacks_.onTargetAuthority(m);
                break;
            }
            case PacketType::HitClaim: {
                HitClaim m;
                read(reader, m);
                if (callbacks_.onHitClaim) callbacks_.onHitClaim(m);
                break;
            }
            case PacketType::ProgressUpdate: {
                ProgressUpdate m;
                read(reader, m);
                if (callbacks_.onProgressUpdate) callbacks_.onProgressUpdate(m);
                break;
            }
            case PacketType::DesyncNotice: {
                DesyncNotice m;
                read(reader, m);
                if (callbacks_.onDesyncNotice) callbacks_.onDesyncNotice(m);
                break;
            }
            case PacketType::DesyncCaptureRequest: {
                DesyncCaptureRequest m;
                read(reader,m);
                if(m.key.reportId<=lastObservedDesyncReportId_)break;
                lastObservedDesyncReportId_=m.key.reportId;
                const auto generation=transportGeneration_;
                dispatchPendingDesync();
                if(generation!=transportGeneration_)break;
                if(pendingDesyncRequest_) {
                    log("Pending desync capture already occupied; duplicate/new request cannot extend its deadline");
                    break;
                }
                pendingDesyncDeadlineMs_=localTimeMs()+m.remainingMs;
                pendingDesyncRequest_=std::move(m);
                dispatchPendingDesync();
                break;
            }
            case PacketType::ClockPong: {
                ClockPong pong;
                read(reader, pong);
                onClockPong(pong);
                break;
            }
            case PacketType::HelloReject: {
                HelloReject reject;
                read(reader, reject);
                if (!rejection_) rejection_ = reject;
                pendingDesyncRequest_.reset();desyncRequest_.reset();
                avatarRosterValid_ = false;
                log("Refused by host: " + reject.reason);
                if (callbacks_.onRejected) callbacks_.onRejected(reject);
                break;
            }
            default:
                log("Unknown packet type from host: " +
                    std::to_string(static_cast<int>(type)));
                break;
        }
    } catch (const std::exception& ex) {
        if (sessionPacket && !sessionNotified) {
            updateAvatarRoster(SessionState{});
            if (resumePin_) closeChangedSession();
            else if (callbacks_.onSessionState) callbacks_.onSessionState(SessionState{});
        }
        if(resyncPlan_&&size&&data[0]>=static_cast<std::uint8_t>(PacketType::ResyncBegin)&&data[0]<=static_cast<std::uint8_t>(PacketType::ResyncEnd))
            abortResync(ResyncResultReason::InvalidSnapshot,"malformed relay snapshot record");
        log("Packet decode error: " + std::string(ex.what()));
    }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

bool NetworkClient::sendPacket(const std::vector<std::uint8_t>& packet,
                               bool reliable) {
    if(!packet.empty() && isDesyncDiagnosticPacket(static_cast<PacketType>(packet.front())))reliable=true;
    if (outbound_.conditions().active()) {
        return outbound_.enqueue(localTimeMs(), packet, reliable);
    }
    return sendNow(packet, reliable);
}

bool NetworkClient::sendNow(const std::vector<std::uint8_t>& packet,
                            bool reliable) {
    if (!connected_ || !transportPeer_ || packet.empty()) return false;
    const auto type = static_cast<PacketType>(packet.front());
    if (!ready() && type != PacketType::ClientHello && type != PacketType::Heartbeat &&
        type != PacketType::ClockPing) return false;
    // Queued capture parts and success ACKs retain their original transaction;
    // neither a new plan nor elapsed conditioner time grants fresh authority.
    if((type>=PacketType::ResyncBegin && type<=PacketType::ResyncAck) || type==PacketType::ResyncRequest) {
        try {
            const std::uint8_t* payload=nullptr;std::size_t size=0;
            decodePacketHeader(packet.data(),packet.size(),payload,size);ByteReader r(payload,size);
            if(type==PacketType::ResyncRequest) {
                ResyncRequest m;read(r,m);
                if(m.key.sessionId!=avatarSessionId_ || m.key.hostConnectionId!=avatarConnections_[0] || avatarLocalSlot_!=0)return false;
                if(localTimeMs()>=resyncDeadline_ ||
                   (!requestedResync_&&!resyncPlan_) ||
                   (requestedResync_&&m.key!=requestedResync_->key) ||
                   (resyncPlan_&&m.key!=resyncPlan_->request.key))return false;
            } else {
                ResyncKey key;ResyncPhase phase{};bool terminalFailure=false;
                if(type==PacketType::ResyncBegin){ResyncBegin m;read(r,m);key=m.key;phase=m.phase;}
                else if(type==PacketType::ResyncPart){ResyncPart m;read(r,m);key=m.key;phase=m.phase;}
                else if(type==PacketType::ResyncEnd){ResyncEnd m;read(r,m);key=m.key;phase=m.phase;}
                else {ResyncAck m;read(r,m);key=m.key;phase=m.phase;terminalFailure=m.status==ResyncAckStatus::Failed || m.status==ResyncAckStatus::Unavailable;
                    if(!worldBinding_ || m.target.slot!=avatarLocalSlot_ || m.target.connectionId!=worldBinding_->selfConnectionId || m.target.deliverySerial!=worldBinding_->deliverySerial)return false;}
                if(key.sessionId!=avatarSessionId_ || key.hostConnectionId!=avatarConnections_[0])return false;
                if(!terminalFailure && (!resyncPlan_ || key!=resyncPlan_->request.key || phase!=resyncPlan_->phase || localTimeMs()>=resyncDeadline_))return false;
            }
        } catch(const std::exception&) { return false; }
    }
    // Recheck after conditioner delay; an old collector cannot be relabeled
    // with a replacement roster/session or a later report's authority.
    if (type==PacketType::DesyncArtifactChunk || type==PacketType::DesyncCaptureDone) {
        try {
            const std::uint8_t* payload=nullptr;std::size_t size=0;
            decodePacketHeader(packet.data(),packet.size(),payload,size);ByteReader r(payload,size);
            if(type==PacketType::DesyncArtifactChunk){DesyncArtifactChunk m;read(r,m);if(!admitDesync(m.key,m.connectionId))return false;}
            else {DesyncCaptureDone m;read(r,m);if(!admitDesync(m.key,m.connectionId))return false;}
        } catch(const std::exception&) { return false; }
    }
    if(type==PacketType::WorldEnvelope) {
        try {const std::uint8_t* p;std::size_t n;decodePacketHeader(packet.data(),packet.size(),p,n);ByteReader r(p,n);WorldEnvelope m;read(r,m);
            if(!worldBinding_ || m.scope.sessionId!=avatarSessionId_ || m.scope.sourceConnectionId!=worldBinding_->selfConnectionId || m.scope.sourceDeliverySerial!=worldBinding_->deliverySerial)return false;
        }catch(const std::exception&){return false;}
    }
    return transport_->send(transportPeer_, packet.data(), packet.size(),
                            isDesyncDiagnosticPacket(type) ? 2 : (reliable ? 0 : 1), reliable);
}

void NetworkClient::log(const std::string& msg) {
    if (callbacks_.onLog) {
        callbacks_.onLog("[NetworkClient] " + msg);
    }
}

} // namespace kh2coop
