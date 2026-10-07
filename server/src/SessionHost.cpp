#ifndef _CRT_RAND_S
#define _CRT_RAND_S
#endif
#include "kh2coop/SessionHost.hpp"
#include "kh2coop/Revive.hpp"
#include "kh2coop/ProgressMirror.hpp"
#include "kh2coop/ResyncEvidence.hpp"
#include "kh2coop/AppliedStateHash.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cerrno>
#include <iomanip>
#include <optional>
#include <sstream>
#if defined(__linux__)
#include <sys/random.h>
#endif

namespace kh2coop {

void SessionHost::sendBinding(PeerState& ps) {
    auto* host=hostPeer();if(!host||ps.status!=PeerStatus::Verified)return;
    sendTo(ps.transportPeer,encode(WorldBinding{session_.sessionId,host->connectionId,ps.connectionId,
        static_cast<std::uint8_t>(ps.assignedSlot),ps.deliverySerial}),true);
}
void SessionHost::finishResync(ResyncResultReason reason,const std::string& error) {
    if(!resyncPlan_)return;
    ResyncResult result;result.key=resyncPlan_->request.key;result.reason=reason;result.targetCount=resyncPlan_->targetCount;
    for(std::size_t i=0;i<result.targetCount;++i){auto& t=result.targets[i];t.target=resyncPlan_->targets[i];t.error=error;
        if(resyncAcks_[i]){t.status=resyncAcks_[i]->status;t.appliedCut=resyncAcks_[i]->snapshotCut;t.fingerprint=resyncAcks_[i]->observedFingerprint;}
        if(reason!=ResyncResultReason::Converged)for(auto& ps:peers_)if(ps.connectionId==t.target.connectionId)ps.worldQuarantined=true;
    }
    resyncPlan_.reset();resyncAssembler_.Reset();resyncBegin_.reset();resyncSnapshot_.reset();
    for(auto& q:resyncContinuation_)q.clear();
    resyncContinuationBytes_={};resyncAcks_={};
    lastResyncResult_=result;
    // Immutable original target denominator survives disconnect and failures.
    for(auto& ps:peers_)if(ps.status==PeerStatus::Verified){bool selected=ps.connectionId==result.key.hostConnectionId;for(std::size_t i=0;i<result.targetCount;++i)selected=selected||ps.connectionId==result.targets[i].target.connectionId;if(selected)sendTo(ps.transportPeer,encode(result),true);}
    log(formatResyncResultEvidence(result,"relay"));
    if(callbacks_.onResyncFinalized)callbacks_.onResyncFinalized(result);
}
void SessionHost::pumpResync(std::uint64_t nowMs) {
    if(resyncPlan_&&nowMs>=resyncDeadline_)finishResync(ResyncResultReason::Deadline,"fixed transaction deadline");
}
void SessionHost::refreshResyncCache(const ResyncSnapshot& s,const std::vector<WorldEnvelope>& continuation,std::uint64_t cut) {
    room_=s.room;hold_=s.hold;manifest_={s.room.epoch,true,{}};enemyHp_.clear();deadEnemies_.clear();
    for(const auto& e:s.enemies){manifest_.entries.push_back(e.identity);enemyHp_[e.identity.netId]={e.identity.netId,e.hp,e.maxHp};if(e.life==ResyncLife::ObservedDeadHistory)deadEnemies_.insert(e.identity.netId);}
    lastEnemyHpSequence_=(std::max)(lastEnemyHpSequence_,s.hpSequence);
    progress_.clear();progressVersion_=s.progress.version;
    for(const auto& span:s.progress.spans)for(std::size_t i=0;i<span.bytes.size();++i)progress_[span.offset+static_cast<std::uint32_t>(i)]=span.bytes[i];
    // These records have already passed host, epoch, framing and HP ordering
    // admission. Reapply once without a second publication or sequence mint.
    for(const auto& e:continuation)if(e.scope.hostSourceSerial>cut){
        const std::uint8_t* p;std::size_t n;const auto type=decodePacketHeader(e.packet.data(),e.packet.size(),p,n);ByteReader r(p,n);
        if(type==PacketType::RoomTransition){RoomTransition m;read(r,m);clearWorldState();room_=m;}
        else if(type==PacketType::EventHold){EventHold m;read(r,m);hold_=m;}
        else if(type==PacketType::EnemyManifest){EnemyManifest m;read(r,m);if(m.replace){manifest_=m;enemyHp_.clear();deadEnemies_.clear();}else manifest_.entries.insert(manifest_.entries.end(),m.entries.begin(),m.entries.end());}
        else if(type==PacketType::EnemyHp){EnemyHp m;read(r,m);for(const auto& hp:m.entries)enemyHp_[hp.netId]=hp;}
        else if(type==PacketType::EnemyDeath){EnemyDeath m;read(r,m);deadEnemies_.insert(m.netId);}
        else if(type==PacketType::ProgressUpdate){ProgressUpdate m;read(r,m);if(m.full)progress_.clear();for(const auto& span:m.spans)for(std::size_t i=0;i<span.bytes.size();++i)progress_[span.offset+static_cast<std::uint32_t>(i)]=span.bytes[i];progressVersion_=m.version;}
    }
}
void SessionHost::publishResyncPlan() {
    if(!resyncPlan_)return;
    invalidateParty(PartyApplyReason::RoomChanged);
    const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if(static_cast<std::uint64_t>(now)>=resyncDeadline_){finishResync(ResyncResultReason::Deadline,"fixed transaction deadline");return;}
    resyncPlan_->remainingMs=static_cast<std::uint32_t>(resyncDeadline_-static_cast<std::uint64_t>(now));
    for(auto& peer:peers_)if(peer.status==PeerStatus::Verified){
        bool target=false;for(std::size_t i=0;i<resyncPlan_->targetCount;++i)target=target||resyncPlan_->targets[i].connectionId==peer.connectionId;
        if(target){sendBinding(peer);auto plan=*resyncPlan_;plan.stage=ResyncPlanStage::Fenced;if(!sendTo(peer.transportPeer,encode(plan),true)){finishResync(ResyncResultReason::Overflow,"plan submission failed");return;}}
    }
    if(auto* host=hostPeer()){auto plan=*resyncPlan_;plan.stage=ResyncPlanStage::CaptureRequested;if(!sendTo(host->transportPeer,encode(plan),true))finishResync(ResyncResultReason::Overflow,"capture request submission failed");}
}
bool SessionHost::resyncMaterialDifference(PacketType type,const std::vector<std::uint8_t>& bytes) const {
    if(!resyncSnapshot_)return true;
    try {
        const auto& s=*resyncSnapshot_;const std::uint8_t* p;std::size_t n;decodePacketHeader(bytes.data(),bytes.size(),p,n);ByteReader r(p,n);
        if(type==PacketType::RoomTransition){RoomTransition m;read(r,m);return !sameResyncRoom(m,s.room);}
        if(type==PacketType::EventHold){EventHold m;read(r,m);return m.epoch!=s.hold.epoch||m.active!=s.hold.active||m.eventProgram!=s.hold.eventProgram;}
        if(type==PacketType::EnemyHp){EnemyHp m;read(r,m);for(const auto& e:m.entries){auto i=std::find_if(s.enemies.begin(),s.enemies.end(),[&](const auto& v){return v.identity.netId==e.netId;});if(i==s.enemies.end()||i->hp!=e.hp||i->maxHp!=e.maxHp)return true;}return false;}
        if(type==PacketType::EnemyDeath){EnemyDeath m;read(r,m);auto i=std::find_if(s.enemies.begin(),s.enemies.end(),[&](const auto& v){return v.identity.netId==m.netId;});return i==s.enemies.end()||i->life!=ResyncLife::ObservedDeadHistory;}
        if(type==PacketType::EnemyManifest){EnemyManifest m;read(r,m);if(m.replace&&m.entries.size()!=s.enemies.size())return true;for(const auto& e:m.entries){auto i=std::find_if(s.enemies.begin(),s.enemies.end(),[&](const auto& v){return v.identity.netId==e.netId;});if(i==s.enemies.end())return true;const auto& a=i->identity;if(a.objectId!=e.objectId||a.spawnIndex!=e.spawnIndex||a.battleProgram!=e.battleProgram||a.spawnPosition.x!=e.spawnPosition.x||a.spawnPosition.y!=e.spawnPosition.y||a.spawnPosition.z!=e.spawnPosition.z)return true;}return false;}
        if(type==PacketType::ProgressUpdate){ProgressUpdate m;read(r,m);std::map<std::uint32_t,std::uint8_t> expected,actual;for(const auto& span:s.progress.spans)for(std::size_t i=0;i<span.bytes.size();++i)expected[span.offset+static_cast<std::uint32_t>(i)]=span.bytes[i];for(const auto& span:m.spans)for(std::size_t i=0;i<span.bytes.size();++i)actual[span.offset+static_cast<std::uint32_t>(i)]=span.bytes[i];if(m.full&&actual!=expected)return true;for(auto [o,v]:actual){auto i=expected.find(o);if(i==expected.end()||i->second!=v)return true;}return false;}
    }catch(const std::exception&){return true;}
    return true;
}
bool SessionHost::receiveResync(PeerState& ps,PacketType type,ByteReader& r) {
    const auto now=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    pumpResync(now);
    if(type==PacketType::ResyncRequest) {
        ResyncRequest request;read(r,request);
        if(!fromHost(ps)||!room_||request.key.sessionId!=session_.sessionId||request.key.hostConnectionId!=ps.connectionId||!sameResyncRoom(request.room,*room_)){++rejectedWorld_;return false;}
        for(std::size_t i=0;i<3;++i){auto* peer=peerBySlot(static_cast<SlotType>(i));if(request.connections[i]!=(peer?peer->connectionId:0)){++rejectedWorld_;return false;}}
        if(resyncPlan_){if(encode(request)==encode(resyncPlan_->request))return true;
            ResyncResult busy;busy.key=request.key;busy.reason=ResyncResultReason::Busy;
            for(std::size_t slot=1;slot<3;++slot)if(request.targetMask&(1u<<slot)){const auto* target=peerBySlot(static_cast<SlotType>(slot));if(target){auto& t=busy.targets[busy.targetCount++];t.target={static_cast<std::uint8_t>(slot),target->connectionId,target->deliverySerial};t.error="another transaction is active";}}
            lastResyncResult_=busy;sendTo(ps.transportPeer,encode(busy),true);if(callbacks_.onResyncFinalized)callbacks_.onResyncFinalized(busy);++rejectedWorld_;return false;}
        if(request.key.requestId<=lastResyncRequestId_){++rejectedWorld_;return false;}
        ResyncPlan plan;plan.request=request;plan.remainingMs=RESYNC_TIMEOUT_MS;
        for(std::size_t slot=1;slot<3;++slot)if(request.targetMask&(1u<<slot)){
            auto it=std::find_if(peers_.begin(),peers_.end(),[&](const auto& p){return p.status==PeerStatus::Verified&&static_cast<std::size_t>(p.assignedSlot)==slot;});
            if(it==peers_.end()||it->deliverySerial==UINT64_MAX){++rejectedWorld_;return false;}
            plan.targets[plan.targetCount++]={static_cast<std::uint8_t>(slot),it->connectionId,it->deliverySerial+1};
        }
        if(simulationActive_){ResyncResult unavailable;unavailable.key=request.key;unavailable.reason=ResyncResultReason::CaptureUnavailable;unavailable.targetCount=plan.targetCount;for(std::size_t i=0;i<plan.targetCount;++i){unavailable.targets[i].target=plan.targets[i];--unavailable.targets[i].target.deliverySerial;unavailable.targets[i].error="legacy simulation is active";}lastResyncRequestId_=request.key.requestId;lastResyncResult_=unavailable;sendTo(ps.transportPeer,encode(unavailable),true);if(callbacks_.onResyncFinalized)callbacks_.onResyncFinalized(unavailable);return false;}
        lastResyncRequestId_=request.key.requestId;resyncPlan_=plan;resyncDeadline_=now+RESYNC_TIMEOUT_MS;resyncAcks_={};resyncMaterialChanged_=false;resyncBegin_.reset();resyncSnapshot_.reset();resyncAssembler_.Reset();
        for(std::size_t i=0;i<plan.targetCount;++i)for(auto& peer:peers_)if(peer.connectionId==plan.targets[i].connectionId){peer.deliverySerial=plan.targets[i].deliverySerial;peer.worldQuarantined=true;peer.hasHash=false;pendingActivation_.erase(peer.transportPeer);}
        publishResyncPlan();return true;
    }
    if(!resyncPlan_){++rejectedWorld_;return false;}
    if(type==PacketType::ResyncResult){ResyncResult result;read(r,result);if(!fromHost(ps)||result.key!=resyncPlan_->request.key||result.reason==ResyncResultReason::Converged){++rejectedWorld_;return false;}finishResync(result.reason,"host reported capture/producer failure");return true;}
    if(type==PacketType::ResyncBegin){
        ResyncBegin b;read(r,b);
        if(!fromHost(ps)||b.key!=resyncPlan_->request.key||b.phase!=resyncPlan_->phase||b.targets!=resyncPlan_->targets||b.targetCount!=resyncPlan_->targetCount||!sameResyncRoom(b.room,resyncPlan_->request.room)){++rejectedWorld_;return false;}
        if(resyncBegin_){
            if(b.phase==resyncBegin_->phase){
                if(encode(b)==encode(*resyncBegin_))return true;
                finishResync(ResyncResultReason::InvalidSnapshot,"snapshot is immutable within a phase");return false;
            }
            if(b.snapshotCut<=resyncBegin_->snapshotCut){finishResync(ResyncResultReason::InvalidSnapshot,"checkpoint requires a fresh source cut");return false;}
        }
        if(!resyncAssembler_.Begin(b)){finishResync(ResyncResultReason::InvalidSnapshot,"invalid begin");return false;}
        resyncBegin_=b;return true;
    }
    if(type==PacketType::ResyncPart){ResyncPart p;read(r,p);if(!fromHost(ps)||p.key!=resyncPlan_->request.key){++rejectedWorld_;return false;}if(!resyncAssembler_.Part(p)){finishResync(ResyncResultReason::InvalidSnapshot,"invalid part");return false;}return true;}
    if(type==PacketType::ResyncEnd){ResyncEnd e;read(r,e);if(!fromHost(ps)||e.key!=resyncPlan_->request.key){++rejectedWorld_;return false;}auto snapshot=resyncAssembler_.End(e);if(!snapshot||!resyncBegin_){finishResync(ResyncResultReason::InvalidSnapshot,"invalid complete snapshot");return false;}resyncSnapshot_=*snapshot;resyncMaterialChanged_=false;
        // Queued post-cut material was admitted before the complete capture arrived.
        for(const auto& queue:resyncContinuation_)for(const auto& item:queue)if(item.scope.hostSourceSerial>resyncBegin_->snapshotCut&&resyncMaterialDifference(static_cast<PacketType>(item.packet.front()),item.packet))resyncMaterialChanged_=true;
        if(resyncPlan_->phase==ResyncPhase::Checkpoint&&resyncMaterialChanged_){finishResync(ResyncResultReason::NativeFailed,"checkpoint already changed");return false;}
        refreshResyncCache(*snapshot,resyncContinuation_[0],resyncBegin_->snapshotCut);
        hostSourceCutFloor_=(std::max)(hostSourceCutFloor_,resyncBegin_->snapshotCut);
        lastHostSourceSerial_=(std::max)(lastHostSourceSerial_,resyncBegin_->snapshotCut);
        auto bytes=encodeResyncSnapshot(*snapshot);const auto begin=*resyncBegin_;
        for(std::size_t i=0;resyncPlan_&&i<resyncPlan_->targetCount;++i){auto* target=const_cast<PeerState*>(peerBySlot(static_cast<SlotType>(resyncPlan_->targets[i].slot)));if(!target||target->connectionId!=resyncPlan_->targets[i].connectionId){finishResync(ResyncResultReason::TargetChanged,"target missing");return false;}
            bool sent=sendTo(target->transportPeer,encode(begin),true);
            for(std::size_t off=0;sent&&off<bytes.size();off+=RESYNC_MAX_PART_BYTES){const auto end=(std::min)(bytes.size(),off+RESYNC_MAX_PART_BYTES);sent=sendTo(target->transportPeer,encode(ResyncPart{begin.key,begin.phase,begin.snapshotCut,static_cast<std::uint32_t>(off),{bytes.begin()+off,bytes.begin()+end}}),true);}
            sent=sent&&sendTo(target->transportPeer,encode(e),true);if(!sent){finishResync(ResyncResultReason::Overflow,"snapshot forwarding failed");return false;}
            target->hostSourceFloor=begin.snapshotCut;target->worldQuarantined=false;
            for(const auto& item:resyncContinuation_[i])if(item.scope.hostSourceSerial>begin.snapshotCut&&!sendTo(target->transportPeer,encode(item),true)){finishResync(ResyncResultReason::Overflow,"continuation forwarding failed");return false;}
            resyncContinuation_[i].clear();resyncContinuationBytes_[i]=0;
        }
        return true;
    }
    if(type==PacketType::ResyncAck){ResyncAck a;read(r,a);if(fromHost(ps)||a.key!=resyncPlan_->request.key||a.target.connectionId!=ps.connectionId||a.target.deliverySerial!=ps.deliverySerial||a.target.slot!=static_cast<std::uint8_t>(ps.assignedSlot)||a.phase!=resyncPlan_->phase){++rejectedWorld_;return false;}
        std::size_t index=0;for(;index<resyncPlan_->targetCount;++index)if(resyncPlan_->targets[index]==a.target)break;if(index==resyncPlan_->targetCount){++rejectedWorld_;return false;}
        if(a.status==ResyncAckStatus::Unavailable||a.status==ResyncAckStatus::Failed){resyncAcks_[index]=a;finishResync(a.status==ResyncAckStatus::Unavailable?ResyncResultReason::NativeUnavailable:ResyncResultReason::NativeFailed,a.error);return true;}
        if(!resyncSnapshot_||!resyncBegin_||a.snapshotCut!=resyncBegin_->snapshotCut||a.snapshotSha256!=resyncBegin_->sha256){++rejectedWorld_;return false;}
        if(a.status!=ResyncAckStatus::Converged){if(!resyncAcks_[index]||static_cast<unsigned>(a.status)>static_cast<unsigned>(resyncAcks_[index]->status))resyncAcks_[index]=a;return true;}
        if(a.observedFingerprint!=resyncSnapshot_->nativeFingerprint||!sameResyncRoom(a.observedRoom,resyncSnapshot_->room)||a.enemyCount!=resyncSnapshot_->livingCount||a.deadCount!=resyncSnapshot_->deadCount||a.checksMask!=ResyncChecksComplete||!a.observationFrame1||a.observationFrame2<=a.observationFrame1||!a.loadAfter||(a.phase==ResyncPhase::Bootstrap&&a.loadBefore==a.loadAfter)){finishResync(ResyncResultReason::NativeFailed,"invalid native convergence witness");return false;}
        resyncAcks_[index]=a;
        if(resyncMaterialChanged_){resyncPlan_->phase=ResyncPhase::Checkpoint;resyncSnapshot_.reset();resyncAssembler_.Reset();resyncAcks_={};resyncMaterialChanged_=false;for(auto& p:peers_)for(std::size_t i=0;i<resyncPlan_->targetCount;++i)if(p.connectionId==resyncPlan_->targets[i].connectionId)p.worldQuarantined=true;publishResyncPlan();return true;}
        for(std::size_t i=0;i<resyncPlan_->targetCount;++i){
            if(!resyncAcks_[i]||resyncAcks_[i]->status!=ResyncAckStatus::Converged)return true;
            const auto& witness=*resyncAcks_[i];
            if(witness.key!=resyncPlan_->request.key || witness.phase!=resyncPlan_->phase ||
               witness.target!=resyncPlan_->targets[i] || witness.snapshotCut!=resyncBegin_->snapshotCut ||
               witness.snapshotSha256!=resyncBegin_->sha256 || witness.observedFingerprint!=resyncSnapshot_->nativeFingerprint){
                finishResync(ResyncResultReason::NativeFailed,"mixed native convergence witnesses");return false;
            }
        }
        finishResync(ResyncResultReason::Converged,"");return true;
    }
    ++rejectedWorld_;return false;
}

namespace {

constexpr std::uint32_t disconnectCode(DisconnectReason reason) {
    return static_cast<std::uint32_t>(reason);
}

// OS entropy, never a clock/configuration-derived identity. No weak fallback:
// admission fails if entropy is unavailable. The opaque wire token has 128 bits.
std::optional<std::string> mintWorldIncarnation() {
    std::array<unsigned char, 16> bytes {};
#if defined(_WIN32)
    static_assert(sizeof(unsigned int) == 4);
    for (std::size_t i = 0; i < bytes.size(); i += 4) {
        unsigned int value = 0;
        if (rand_s(&value) != 0) return std::nullopt;
        for (std::size_t j = 0; j < 4; ++j)
            bytes[i + j] = static_cast<unsigned char>(value >> (j * 8));
    }
#elif defined(__linux__)
    std::size_t filled = 0;
    while (filled < bytes.size()) {
        const auto count = getrandom(bytes.data() + filled, bytes.size() - filled, 0);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return std::nullopt;
        filled += static_cast<std::size_t>(count);
    }
#elif defined(__APPLE__)
    arc4random_buf(bytes.data(), bytes.size());
#else
    return std::nullopt;
#endif
    if (std::all_of(bytes.begin(), bytes.end(), [](unsigned char b) { return b == 0; }))
        return std::nullopt;
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (const auto byte : bytes) out << std::setw(2) << static_cast<unsigned>(byte);
    return out.str();
}

std::uint64_t currentTimeMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

bool isValidSlot(SlotType slot) {
    switch (slot) {
        case SlotType::Player:
        case SlotType::Friend1:
        case SlotType::Friend2:
            return true;
    }

    return false;
}

bool sameActivationRoom(const RoomTransition& a, const RoomTransition& b) {
    return a.epoch == b.epoch && a.worldId == b.worldId && a.roomId == b.roomId &&
           a.door == b.door && a.mapProgram == b.mapProgram &&
           a.battleProgram == b.battleProgram && a.eventProgram == b.eventProgram;
}

bool sameActivationRequest(const ActivationRequest& a, const ActivationRequest& b) {
    return sameActivationRoom(a.location, b.location) && a.incarnation == b.incarnation &&
           a.requestSeq == b.requestSeq && a.requesterSlot == b.requesterSlot;
}

bool isValidRuntimeMode(RuntimeMode mode) {
    switch (mode) {
        case RuntimeMode::CampaignCoop:
        case RuntimeMode::PublicRealm:
            return true;
    }

    return false;
}

const char* runtimeModeName(RuntimeMode mode) {
    switch (mode) {
        case RuntimeMode::CampaignCoop:
            return "campaign_coop";
        case RuntimeMode::PublicRealm:
            return "public_realm";
    }

    return "unknown";
}

} // namespace

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

SessionHost::SessionHost(const SessionConfig& config, SessionCallbacks callbacks,
                         std::unique_ptr<Transport> transport)
    : config_(config), callbacks_(std::move(callbacks)),
      transport_(transport ? std::move(transport) : makeEnetTransport()) {
    const auto* hashOptIn = std::getenv("KH2COOP_CAUSAL_DIAGNOSTICS");
    if (!hashOptIn || std::string(hashOptIn) != "1") callbacks_.onHashDiagnostic = {};
    session_.gameBuild = config_.gameBuild;
    session_.modHash = config_.modHash;
    desyncCapture_ = std::make_unique<DesyncCapture>(config_.desyncOutputRoot);
}

SessionHost::~SessionHost() { stop(); }

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool SessionHost::start() {
    if (running_) return true;
    if (config_.protocolVersion != PROTOCOL_VERSION) {
        log("Unsupported configured protocol version: " + std::to_string(config_.protocolVersion) +
            " (this binary requires " + std::to_string(PROTOCOL_VERSION) + ")");
        return false;
    }

    const auto opened = transport_->listen(config_.bindAddress, config_.port, config_.maxPeers, 3);
    if (opened == TransportOpenResult::InvalidAddress) {
        log("Invalid bind address: " + config_.bindAddress);
        return false;
    }
    if (opened != TransportOpenResult::Ok) {
        log("Failed to create ENet host on port " + std::to_string(config_.port));
        return false;
    }

    running_ = true;
    log("Session host listening on port " + std::to_string(config_.port) +
        " (proto=" + std::to_string(config_.protocolVersion) +
        " mode=" + runtimeModeName(config_.runtimeMode) +
        " build=" + config_.gameBuild +
        " content=" + config_.contentHash +
        " mod=" + config_.modHash + ")");
    return true;
}

void SessionHost::tick(std::uint32_t timeoutMs) {
    if (!running_ || !transport_->isOpen()) return;
    pumpDesyncCapture();
    pumpResync(currentTimeMs());

    TransportEvent event;
    while (transport_->service(event, timeoutMs) > 0) {
        switch (event.type) {
            case TransportEventType::Connect:
                onConnect(event.peer);
                break;
            case TransportEventType::Disconnect:
                onDisconnect(event.peer);
                break;
            case TransportEventType::Receive:
                onReceive(event.peer, event.packet.data, event.packet.size);
                event.packet.reset();
                break;
            case TransportEventType::None:
                break;
        }
        // After first event, poll remaining without blocking.
        timeoutMs = 0;
    }

    expireStalePeers(currentTimeMs());
    pumpDesyncCapture();
}

void SessionHost::stop() {
    if (!running_) return;
    finishResync(ResyncResultReason::Cancelled,"relay stopping");
    desyncCapture_->Interrupt("relay stopping");
    pumpDesyncCapture();
    sealHashDiagnostics("shutdown");
    running_ = false;

    // Disconnect all peers gracefully.
    for (auto& ps : peers_) {
        if (ps.transportPeer) {
            transport_->disconnect(ps.transportPeer, disconnectCode(DisconnectReason::RelayStopping));
        }
    }

    // Flush disconnects.
    if (transport_->isOpen()) {
        TransportEvent event;
        while (transport_->service(event, 100) > 0) {
            if (event.type == TransportEventType::Receive) {
                event.packet.reset();
            }
        }
        transport_->close();
    }

    peers_.clear();
    session_.actors.clear();
    session_.sessionId.clear();
    lastEnemyHpSequence_ = 0;
    room_.reset();
    clearWorldState();
    progress_.clear();
    progressVersion_ = 0;
    pendingActivation_.clear();
    log("Session host stopped.");
    desyncCapture_->Shutdown();
    pumpDesyncCapture();
}

// ---------------------------------------------------------------------------
// Outbound broadcasting
// ---------------------------------------------------------------------------

void SessionHost::invalidateParty(PartyApplyReason reason) {
    partyLayout_.reset();
    const auto* host=hostPeer();
    if(!host || !room_ || !room_->epoch)return;
    if(partyHostConnection_!=host->connectionId){partyHostConnection_=host->connectionId;partyVersion_=0;}
    broadcastToVerified(encode(PartyReapply{*room_,partyVersion_,reason}),true);
}

void SessionHost::broadcastSessionState() {
    auto pkt = encode(session_);
    broadcastToVerified(pkt, true /* reliable */);
    for(auto& ps:peers_)if(ps.status==PeerStatus::Verified)sendBinding(ps);
    invalidateParty(PartyApplyReason::RosterChanged);
}

void SessionHost::broadcastActorSnapshots(
    const std::vector<ActorSnapshot>& snapshots) {
    for (const auto& snap : snapshots) {
        if(simulationSourceSerial_==UINT64_MAX)continue;
        simulationActive_=true;++simulationSourceSerial_;
        if(resyncPlan_)finishResync(ResyncResultReason::CaptureUnavailable,"legacy simulation is active");
        auto pkt = encode(snap);
        broadcastToVerified(pkt, false /* unreliable */);
    }
}

void SessionHost::broadcastEnemySnapshots(
    const std::vector<EnemySnapshot>& snapshots) {
    for (const auto& snap : snapshots) {
        if(simulationSourceSerial_==UINT64_MAX)continue;
        simulationActive_=true;++simulationSourceSerial_;
        if(resyncPlan_)finishResync(ResyncResultReason::CaptureUnavailable,"legacy simulation is active");
        auto pkt = encode(snap);
        broadcastToVerified(pkt, false /* unreliable */);
    }
}

void SessionHost::broadcastEvent(const EventMessage& event) {
    if(simulationSourceSerial_==UINT64_MAX)return;
    simulationActive_=true;++simulationSourceSerial_;
    if(resyncPlan_)finishResync(ResyncResultReason::CaptureUnavailable,"legacy simulation is active");
    auto pkt = encode(event);
    broadcastToVerified(pkt, true /* reliable */);
}

// ---------------------------------------------------------------------------
// State queries
// ---------------------------------------------------------------------------

std::size_t SessionHost::verifiedPeerCount() const {
    return std::count_if(peers_.begin(), peers_.end(), [](const PeerState& ps) {
        return ps.status == PeerStatus::Verified;
    });
}

std::optional<SlotType> SessionHost::firstFreeSlot() const {
    for (auto slot : {SlotType::Player, SlotType::Friend1, SlotType::Friend2}) {
        if (!isSlotTaken(slot)) {
            return slot;
        }
    }

    return std::nullopt;
}

// ---------------------------------------------------------------------------
// ENet event handlers
// ---------------------------------------------------------------------------

void SessionHost::onConnect(TransportPeer* peer) {
    if (peers_.size() >= config_.maxPeers) {
        log("Rejecting connection: lobby full.");
        transport_->disconnect(peer, disconnectCode(DisconnectReason::LobbyFull));
        return;
    }

    // Create a temporary peer id from the address until the client sends its
    // real identity in the version handshake.
    PeerState ps;
    ps.transportPeer = peer;
    ps.peerId = transport_->pendingPeerLabel(peer);
    ps.status = PeerStatus::PendingVersion;
    ps.lastHeartbeatMs = currentTimeMs();
    peers_.push_back(std::move(ps));

    log("Peer connected: " + peers_.back().peerId + " (pending version check)");
}

void SessionHost::onDisconnect(TransportPeer* peer) {
    auto* ps = findPeer(peer);
    if (ps) {
        std::string id = ps->peerId;
        removePeer(peer);
        log("Peer disconnected: " + id);
        if (callbacks_.onPeerLeft) callbacks_.onPeerLeft(id);

        rebuildSessionActors();
        broadcastSessionState();
    }
}

void SessionHost::onReceive(TransportPeer* peer, const std::uint8_t* data,
                            std::size_t size, const WorldScope* admittedScope) {
    auto* ps = findPeer(peer);
    if (!ps) return;
    const bool diagnostic = size && (data[0]==static_cast<std::uint8_t>(PacketType::DesyncArtifactChunk) ||
                                      data[0]==static_cast<std::uint8_t>(PacketType::DesyncCaptureDone));

    try {
        const std::uint8_t* payload = nullptr;
        std::size_t payloadSize = 0;
        auto type = decodePacketHeader(data, size, payload, payloadSize);
        ByteReader reader(payload, payloadSize);
        ps->lastHeartbeatMs = currentTimeMs();

        if(type==PacketType::LocalResyncCommand || type==PacketType::NativeResyncSnapshot)return;
        if(type==PacketType::WorldEnvelope) {
            WorldEnvelope envelope;read(reader,envelope);const auto& scope=envelope.scope;
            if(ps->status!=PeerStatus::Verified || scope.sessionId!=session_.sessionId ||
               scope.kind!=WorldSourceKind::Native || scope.sourceConnectionId!=ps->connectionId || scope.sourceDeliverySerial!=ps->deliverySerial ||
               scope.targetConnectionId || scope.targetDeliverySerial || (fromHost(*ps)?!scope.hostSourceSerial:scope.hostSourceSerial!=0)) {++rejectedWorld_;return;}
            const auto inner=static_cast<PacketType>(envelope.packet.front());
            if(ps->worldQuarantined&&!isEphemeralWorldPacket(inner)){++rejectedWorld_;return;}
            if(fromHost(*ps)&&scope.hostSourceSerial<=(std::max)(hostSourceCutFloor_,resyncBegin_?resyncBegin_->snapshotCut:0)){++rejectedWorld_;return;}
            const auto previous=forwardingScope_;forwardingScope_=scope;
            onReceive(peer,envelope.packet.data(),envelope.packet.size(),&scope);
            forwardingScope_=previous;return;
        }
        if(isScopedWorldPacket(type)&&!admittedScope){++rejectedWorld_;return;}
        if(type==PacketType::ResyncRequest || (type>=PacketType::ResyncBegin&&type<=PacketType::ResyncResult)) {
            if(ps->status!=PeerStatus::Verified)return;
            receiveResync(*ps,type,reader);return;
        }
        switch (type) {
            case PacketType::ClientHello: {
                if (ps->status != PeerStatus::PendingVersion) {
                    rejectPeer(peer, ps->peerId, "Repeated ClientHello is not permitted", 1);
                    return;
                }
                // Dedicated handshake packet (B2: replaces SessionState-as-hello).
                ClientHello hello;
                read(reader, hello);

                if (!config_.authenticatedHostIdentity.empty()) {
                    const auto identity = transport_->authenticatedIdentity(peer);
                    if (identity.empty() || hello.peerId != identity ||
                        (identity == config_.authenticatedHostIdentity) !=
                            (hello.requestedSlot == static_cast<std::uint8_t>(SlotType::Player))) {
                        rejectPeer(peer, ps->peerId, "Authenticated Steam identity/host slot mismatch", 1);
                        return;
                    }
                }

                if (hello.protocolVersion != config_.protocolVersion) {
                    const std::string reason =
                        "Protocol mismatch: client=" +
                        std::to_string(hello.protocolVersion) +
                        " server=" + std::to_string(config_.protocolVersion);
                    rejectPeer(peer, ps->peerId, reason, 1);
                    return;
                }

                if (!isValidRuntimeMode(hello.requestedMode)) {
                    const std::string reason = "Invalid requested mode";
                    rejectPeer(peer, ps->peerId, reason, 1);
                    return;
                }

                if (hello.requestedMode != config_.runtimeMode) {
                    const std::string reason =
                        "Mode mismatch: client=" +
                        std::string(runtimeModeName(hello.requestedMode)) +
                        " server=" + runtimeModeName(config_.runtimeMode);
                    rejectPeer(peer, ps->peerId, reason, 1);
                    return;
                }

                if (hello.gameBuild != config_.gameBuild ||
                    hello.contentHash != config_.contentHash ||
                    hello.modHash != config_.modHash) {
                    std::string reason =
                        "Version mismatch: build=" + hello.gameBuild +
                        " content=" + hello.contentHash +
                        " mod=" + hello.modHash +
                        " (relay expects build=" + config_.gameBuild +
                        " content=" + config_.contentHash +
                        " mod=" + config_.modHash + ")";
                    rejectPeer(peer, ps->peerId, reason, 1);
                    return;
                }

                // Resolve requested slot — 0xFF means no preference.
                std::optional<SlotType> requestedSlot;
                if (hello.requestedSlot != 0xFF) {
                    const auto requested =
                        static_cast<SlotType>(hello.requestedSlot);
                    if (!isValidSlot(requested)) {
                        const std::string reason = "Invalid requested slot";
                        rejectPeer(peer, ps->peerId, reason, 2);
                        return;
                    }
                    requestedSlot = requested;
                } else {
                    requestedSlot = firstFreeSlot();
                    if (!requestedSlot.has_value()) {
                        const std::string reason = "No free slots available";
                        rejectPeer(peer, ps->peerId, reason, static_cast<std::uint8_t>(DisconnectReason::LobbyFull));
                        return;
                    }
                }

                if (!config_.authenticatedHostIdentity.empty() &&
                    (transport_->authenticatedIdentity(peer) == config_.authenticatedHostIdentity) !=
                        (*requestedSlot == SlotType::Player)) {
                    rejectPeer(peer, ps->peerId, "Steam host slot is reserved for local authenticated owner", 1);
                    return;
                }
                if (isSlotTaken(*requestedSlot)) {
                    const std::string reason =
                        "Requested slot " +
                        std::to_string(static_cast<int>(*requestedSlot)) +
                        " is already taken";
                    rejectPeer(peer, ps->peerId, reason, static_cast<std::uint8_t>(DisconnectReason::SlotOccupied));
                    return;
                }

                // Version OK — assign the validated requested slot.
                if (nextConnectionId_ == 0) {
                    rejectPeer(peer, ps->peerId, "Connection identity exhausted", 1);
                    return;
                }
                if (*requestedSlot == SlotType::Player) {
                    const auto incarnation = mintWorldIncarnation();
                    if (!incarnation) {
                        rejectPeer(peer, ps->peerId, "World incarnation entropy unavailable", 1);
                        return;
                    }
                    session_.sessionId = *incarnation;
                    lastEnemyHpSequence_ = 0;lastHostSourceSerial_=0;hostSourceCutFloor_=0;lastResyncRequestId_=0;simulationActive_=false;simulationSourceSerial_=0;
                    log("New world incarnation " + session_.sessionId + " (label=" + config_.sessionId + ")");
                }
                ps->gameBuild = hello.gameBuild;
                ps->modHash = hello.modHash;
                if (!hello.peerId.empty()) {
                    ps->peerId = hello.peerId;
                }

                ps->status = PeerStatus::Verified;
                ps->assignedSlot = *requestedSlot;
                ps->connectionId = nextConnectionId_++;

                log("Peer verified: " + ps->peerId + " -> slot " +
                    std::to_string(static_cast<int>(ps->assignedSlot)));

                rebuildSessionActors();

                if (callbacks_.onPeerJoined)
                    callbacks_.onPeerJoined(ps->peerId, ps->assignedSlot);

                // Send full session state to everyone.
                broadcastSessionState();
                if (!fromHost(*ps)) sendWorldStateTo(peer);
                break;
            }

            case PacketType::SessionState: {
                rejectPeer(peer, ps->peerId, "Legacy handshake unsupported; ClientHello required", 1);
                return;
            }

            case PacketType::InputFrame: {
                if (ps->status != PeerStatus::Verified) {
                    log("Ignoring input from unverified peer " + ps->peerId);
                    return;
                }
                InputFrame input;
                read(reader, input);
                ps->lastInput = input;

                if (callbacks_.onInputReceived)
                    callbacks_.onInputReceived(ps->peerId, input);
                break;
            }

            case PacketType::Heartbeat: {
                break;
            }

            case PacketType::RoomTransition:
            case PacketType::EventHold:
            case PacketType::EnemyManifest:
            case PacketType::EnemyHp:
            case PacketType::EnemyDeath:
            case PacketType::ProgressUpdate: {
                // The generic header decoder permits trailing frame bytes for
                // legacy packet types; cached state requires the exact frame.
                if (size != payloadSize + 3) { ++rejectedWorld_; return; }
                if (ps->status != PeerStatus::Verified || !fromHost(*ps)) {
                    ++rejectedWorld_;
                    log("Dropping world message from non-host " + ps->peerId);
                    return;
                }
                const std::vector<std::uint8_t> packet(data, data + size);
                bool reliable = true;
                if (type == PacketType::RoomTransition) {
                    RoomTransition m;
                    read(reader, m);
                    if (!reader.atEnd()) { ++rejectedWorld_; return; }
                    if(resyncPlan_&&!sameResyncRoom(m,resyncPlan_->request.room))finishResync(ResyncResultReason::RoomChanged,"host room changed");
                    clearWorldState();
                    room_ = m;
                    log("Host transition epoch " + std::to_string(m.epoch) +
                        " -> world " + std::to_string(m.worldId) + " room " +
                        std::to_string(m.roomId));
                } else if (type == PacketType::EventHold) {
                    EventHold m;
                    read(reader, m);
                    if (!reader.atEnd() || !room_ || m.epoch == 0 || m.epoch != room_->epoch) {
                        ++rejectedWorld_;
                        return;
                    }
                    hold_ = m;
                } else if (type == PacketType::EnemyManifest) {
                    EnemyManifest m;
                    read(reader, m);
                    if (!reader.atEnd() || !room_ || m.epoch == 0 || m.epoch != room_->epoch) {
                        ++rejectedWorld_;
                        return;
                    }
                    if (m.replace || m.epoch != manifest_.epoch) {
                        manifest_ = m;
                        enemyHp_.clear();
                        deadEnemies_.clear();
                    } else {
                        manifest_.entries.insert(manifest_.entries.end(),
                                                 m.entries.begin(), m.entries.end());
                    }
                } else if (type == PacketType::EnemyHp) {
                    EnemyHp m;
                    read(reader, m);
                    // Decode the entire record before touching the cache. HP is
                    // normally unreliable and can arrive after its room ended;
                    // never relabel that old payload with manifest_.epoch later.
                    if (!reader.atEnd() || !room_ || m.epoch == 0 ||
                        m.epoch != room_->epoch || m.epoch != manifest_.epoch ||
                        m.sequence <= lastEnemyHpSequence_) {
                        ++rejectedWorld_;
                        return;
                    }
                    // Preserve last-entry-wins and same-epoch unknown IDs: HP
                    // can precede a reliable appended manifest on another channel.
                    lastEnemyHpSequence_ = m.sequence;
                    for (const auto& e : m.entries) enemyHp_[e.netId] = e;
                    reliable = false; // periodic absolute values
                } else if (type == PacketType::ProgressUpdate) {
                    ProgressUpdate m;
                    read(reader, m);
                    if (!reader.atEnd()) { ++rejectedWorld_; return; }
                    if (m.full) progress_.clear();
                    for (const auto& span : m.spans) {
                        for (std::size_t i = 0; i < span.bytes.size(); ++i) {
                            progress_[static_cast<std::uint32_t>(span.offset + i)] =
                                span.bytes[i];
                        }
                    }
                    progressVersion_ = m.version;
                } else {
                    EnemyDeath m;
                    read(reader, m);
                    if (!reader.atEnd() || !room_ || m.epoch == 0 ||
                        m.epoch != room_->epoch || m.epoch != manifest_.epoch) {
                        ++rejectedWorld_;
                        return;
                    }
                    deadEnemies_.insert(m.netId);
                }
                if(admittedScope)lastHostSourceSerial_=(std::max)(lastHostSourceSerial_,admittedScope->hostSourceSerial);
                if(resyncSnapshot_&&admittedScope&&admittedScope->hostSourceSerial>resyncBegin_->snapshotCut&&resyncMaterialDifference(type,packet)) {
                    resyncMaterialChanged_=true;
                    if(resyncPlan_->phase==ResyncPhase::Checkpoint)finishResync(ResyncResultReason::NativeFailed,"material change after checkpoint cut");
                }
                forwardToOthers(peer, packet, reliable);
                if(type==PacketType::RoomTransition)invalidateParty(PartyApplyReason::RoomChanged);
                break;
            }

            case PacketType::ActivationRequest: {
                if (ps->status != PeerStatus::Verified || fromHost(*ps) || !room_) {
                    ++rejectedWorld_;
                    return;
                }
                validateActivationPacket(std::vector<std::uint8_t>(data, data + size));
                ActivationRequest request;
                read(reader, request);
                if (!sameActivationRoom(request.location, *room_)) {
                    ++rejectedWorld_;
                    return;
                }
                auto* host = hostPeer();
                if (!host) return;
                request.requesterSlot = static_cast<std::uint8_t>(ps->assignedSlot);
                const auto now = currentTimeMs();
                auto& pending = pendingActivation_[peer];
                std::erase_if(pending, [now](const PendingActivation& entry) {
                    return now - entry.receivedMs >= ACTIVATION_LEASE_MS;
                });
                if (std::any_of(pending.begin(), pending.end(), [&](const PendingActivation& entry) {
                        return sameActivationRequest(entry.request, request);
                    })) return; // duplicates never renew a routing lease
                if (pending.size() == ACTIVATION_MAX_OUTSTANDING) pending.erase(pending.begin());
                pending.push_back({request, now});
                sendTo(host->transportPeer, encode(request), false);
                break;
            }

            case PacketType::HostActivationPoint: {
                if (ps->status != PeerStatus::Verified || !fromHost(*ps) || !room_) {
                    ++rejectedWorld_;
                    return;
                }
                validateActivationPacket(std::vector<std::uint8_t>(data, data + size));
                HostActivationPoint point;
                read(reader, point);
                if (!sameActivationRoom(point.request.location, *room_)) {
                    ++rejectedWorld_;
                    return;
                }
                auto target = std::find_if(peers_.begin(), peers_.end(), [&](const PeerState& other) {
                    return other.status == PeerStatus::Verified && !fromHost(other) &&
                           static_cast<std::uint8_t>(other.assignedSlot) == point.request.requesterSlot;
                });
                if (target == peers_.end()) { ++rejectedWorld_; return; }
                auto pending = pendingActivation_.find(target->transportPeer);
                if (pending == pendingActivation_.end()) { ++rejectedWorld_; return; }
                const auto now = currentTimeMs();
                std::erase_if(pending->second, [now](const PendingActivation& entry) {
                    return now - entry.receivedMs >= ACTIVATION_LEASE_MS;
                });
                const auto match = std::find_if(pending->second.begin(), pending->second.end(),
                    [&](const PendingActivation& entry) {
                        return sameActivationRequest(entry.request, point.request);
                    });
                if (match == pending->second.end()) { ++rejectedWorld_; return; }
                pending->second.erase(match); // a response can be routed only once
                sendTo(target->transportPeer, encode(point), false);
                break;
            }

            case PacketType::PartyLayout: {
                PartyLayout layout;read(reader,layout);
                std::array<std::uint64_t,3> roster{};
                for(const auto& p:peers_)if(p.status==PeerStatus::Verified)roster[static_cast<std::uint8_t>(p.assignedSlot)]=p.connectionId;
                if(!fromHost(*ps) || !admittedScope || !room_ || resyncPlan_ ||
                   !sameResyncRoom(layout.location,*room_) || !validPartyLayout(layout,roster)) {++rejectedWorld_;return;}
                if(partyHostConnection_!=ps->connectionId){partyHostConnection_=ps->connectionId;partyVersion_=0;}
                if(layout.version<=partyVersion_){++rejectedWorld_;return;}
                partyVersion_=layout.version;partyLayout_=layout;
                lastHostSourceSerial_=(std::max)(lastHostSourceSerial_,admittedScope->hostSourceSerial);
                broadcastToVerified(encode(layout),true);
                log("PartyLayout version="+std::to_string(layout.version)+" rule="+std::to_string(static_cast<unsigned>(layout.rule))+
                    " benchedMask="+std::to_string(partyBenchedMask(layout)));
                break;
            }
            case PacketType::PartyReapply: {
                PartyReapply request;read(reader,request);
                if(!fromHost(*ps) || !admittedScope || !room_ || resyncPlan_ ||
                   !sameResyncRoom(request.location,*room_) || request.afterVersion!=partyVersion_ ||
                   request.reason!=PartyApplyReason::StoryForced){++rejectedWorld_;return;}
                invalidateParty(PartyApplyReason::StoryForced);
                break;
            }

            case PacketType::ReviveRequest: {
                ReviveRequest request; read(reader,request);
                if (ps->status != PeerStatus::Verified || ps->worldQuarantined || !admittedScope ||
                    !room_ || !room_->epoch || !sameResyncRoom(request.location,*room_) ||
                    (hold_ && hold_->active) || !request.seq || request.seq <= ps->lastReviveSeq ||
                    request.requesterConnectionId != ps->connectionId || request.targetSlot >= 3 ||
                    request.targetSlot == static_cast<std::uint8_t>(ps->assignedSlot)) { ++rejectedWorld_; return; }
                // Consume authenticated sequence even on gameplay refusal; it
                // cannot become valid later after a peer moves or becomes downed.
                ps->lastReviveSeq = request.seq;
                auto target = std::find_if(peers_.begin(),peers_.end(),[&](const auto& p) {
                    return static_cast<std::uint8_t>(p.assignedSlot)==request.targetSlot &&
                        p.status==PeerStatus::Verified && p.connectionId==request.targetConnectionId;
                });
                const auto now = currentTimeMs();
                const auto arrived = [&](const PeerState& p) {
                    return !p.worldQuarantined && p.ackArrived && p.ackEpoch==room_->epoch &&
                        p.ackWorldId==room_->worldId && p.ackRoomId==room_->roomId &&
                        p.reviveAvatarMs && now>=p.reviveAvatarMs && now-p.reviveAvatarMs<=REVIVE_AVATAR_MAX_AGE_MS &&
                        p.reviveAvatar.downedDelivery==p.deliverySerial &&
                        p.reviveAvatar.downedEpoch==room_->epoch && p.reviveAvatar.worldId==room_->worldId &&
                        p.reviveAvatar.roomId==room_->roomId && !(p.reviveAvatar.flags & AvatarInCutscene);
                };
                if (target==peers_.end() || !arrived(*ps) || !arrived(*target) ||
                    (ps->reviveAvatar.flags & AvatarDowned) || ps->reviveAvatar.hp<=0 ||
                    !(target->reviveAvatar.flags & AvatarDowned) || target->reviveAvatar.hp!=0 ||
                    !request.targetEpisode || request.targetEpisode!=target->reviveAvatar.downedEpisode ||
                    request.targetEpisode<=target->revivedEpisode || !reviveInRange(ps->reviveAvatar,target->reviveAvatar)) {
                    ++rejectedWorld_; return;
                }
                target->revivedEpisode=request.targetEpisode; // reserve before send, no second teammate application
                request.requesterSlot=static_cast<std::uint8_t>(ps->assignedSlot);
                sendTo(target->transportPeer,encode(request),true);
                log("ReviveRequest forwarded requester="+std::to_string(request.requesterSlot)+
                    " target="+std::to_string(request.targetSlot)+" episode="+std::to_string(request.targetEpisode));
                break;
            }

            case PacketType::HitClaim: {
                if (ps->status != PeerStatus::Verified || fromHost(*ps)) {
                    // The host applies its own hits natively.
                    ++rejectedWorld_;
                    return;
                }
                HitClaim claim;
                read(reader, claim);
                // A delayed claim cannot target an enemy identity from a new
                // room. Epoch zero is unset, even if both sides report it.
                if (!room_ || room_->epoch == 0 || claim.epoch != room_->epoch) {
                    ++rejectedWorld_;
                    return;
                }
                const auto matchingIdCount = std::count_if(manifest_.entries.begin(), manifest_.entries.end(),
                    [&](const EnemyManifestEntry& entry) { return entry.netId == claim.netId; });
                const auto target = std::find_if(manifest_.entries.begin(), manifest_.entries.end(),
                    [&](const EnemyManifestEntry& entry) {
                        return entry.netId == claim.netId && entry.objectId == claim.objectId;
                    });
                if (claim.requesterConnectionId == 0 || claim.requesterConnectionId != ps->connectionId ||
                    claim.seq == 0 || claim.seq <= ps->lastHitClaimSeq || claim.netId == 0 ||
                    claim.objectId == 0 || manifest_.epoch != room_->epoch || matchingIdCount != 1 ||
                    target == manifest_.entries.end() || deadEnemies_.count(claim.netId) != 0 ||
                    claim.damage <= 0 || !std::isfinite(claim.attackerPosition.x) ||
                    !std::isfinite(claim.attackerPosition.y) || !std::isfinite(claim.attackerPosition.z)) {
                    ++rejectedWorld_;
                    return;
                }
                auto* host = hostPeer();
                if (!host) { ++rejectedWorld_; return; }
                claim.attackerSlot = ps->assignedSlot; // never trust the claim
                // Consume before forwarding. Room changes never reset this watermark.
                ps->lastHitClaimSeq = claim.seq;
                sendTo(host->transportPeer, encode(claim), true);
                break;
            }

            case PacketType::TransitionAck: {
                if (ps->status != PeerStatus::Verified) return;
                TransitionAck ack;
                read(reader, ack);
                ps->ackEpoch = ack.epoch;
                ps->ackWorldId = ack.worldId;
                ps->ackRoomId = ack.roomId;
                ps->ackArrived = ack.arrived;
                std::ostringstream ackLog;
                ackLog << "TransitionAck slot=" << static_cast<int>(ps->assignedSlot)
                       << " epoch=" << ack.epoch << " room=" << std::hex
                       << std::uppercase << std::setfill('0') << std::setw(2) << ack.worldId
                       << "/" << std::setw(2) << ack.roomId << std::dec
                       << " arrived=" << (ack.arrived ? 1 : 0);
                log(ackLog.str());
                if (room_ && ack.epoch == room_->epoch &&
                    (!ack.arrived || ack.worldId != room_->worldId ||
                     ack.roomId != room_->roomId)) {
                    log("Divergence: " + ps->peerId + " reports world " +
                        std::to_string(ack.worldId) + " room " +
                        std::to_string(ack.roomId) + " for epoch " +
                        std::to_string(ack.epoch));
                }
                break;
            }

            case PacketType::StateHash: {
                if (ps->status != PeerStatus::Verified) return;
                StateHash hash;
                read(reader, hash);
                if(!reader.atEnd() || size!=payloadSize+3)return;
                ps->lastHash=hash;
                ps->hasHash = true;
                if (ps->hashReceiptSeq != UINT64_MAX) ++ps->hashReceiptSeq;
                ps->hashReceiptMs = currentTimeMs();
                if (callbacks_.onHashDiagnostic) {
                    ps->diagnosticHashScopeAvailable = admittedScope != nullptr;
                    // Only already-admitted scalar provenance: no diagnostic allocation
                    // may throw out of this receive branch before its real comparison.
                    ps->diagnosticHashScope.sourceConnectionId = admittedScope ? admittedScope->sourceConnectionId : 0;
                    ps->diagnosticHashScope.sourceDeliverySerial = admittedScope ? admittedScope->sourceDeliverySerial : 0;
                    ps->diagnosticHashScope.hostSourceSerial = admittedScope ? admittedScope->hostSourceSerial : 0;
                    ps->diagnosticHashScope.kind = admittedScope ? admittedScope->kind : WorldSourceKind::Native;
                    const auto before = hashDiagnostics_.highWater;
                    hashDiagnostic("receive", *ps, nullptr);
                    ps->diagnosticHashReceive = hashDiagnostics_.highWater > before ? hashDiagnostics_.highWater : 0;
                }
                if (fromHost(*ps)) {
                    for (auto& other : peers_) {
                        if (!fromHost(other) && other.hasHash) compareWithHost(other);
                    }
                } else {
                    compareWithHost(*ps);
                }
                break;
            }
            case PacketType::DesyncArtifactChunk: {
                if(ps->status!=PeerStatus::Verified) return;
                desyncCapture_->Pump(currentTimeMs());
                DesyncArtifactChunk chunk;read(reader,chunk);
                desyncCapture_->Chunk(ps->connectionId,chunk);
                break;
            }
            case PacketType::DesyncCaptureDone: {
                if(ps->status!=PeerStatus::Verified) return;
                desyncCapture_->Pump(currentTimeMs());
                DesyncCaptureDone done;read(reader,done);
                desyncCapture_->Done(ps->connectionId,done);
                break;
            }

            case PacketType::ClockPing: {
                ClockPing ping;
                read(reader, ping);
                sendTo(peer, encode(ClockPong {ping.clientSendMs, currentTimeMs()}),
                       false);
                break;
            }

            case PacketType::AvatarState: {
                if (ps->status != PeerStatus::Verified) {
                    log("Ignoring avatar from unverified peer " + ps->peerId);
                    return;
                }
                AvatarState avatar;
                read(reader, avatar);
                // The owner is whoever sent it, never what the packet claims.
                avatar.ownerSlot = ps->assignedSlot;
                if (!reader.atEnd()) return;
                // Existing relay behavior stays intact. Only revive authority
                // requires an advancing sequence and finite, valid health.
                if (avatar.seq>ps->reviveAvatarSeq) {
                    ps->reviveAvatarSeq=avatar.seq;
                    ps->reviveAvatarMs=0;
                    if (revivePosition(avatar) && avatar.maxHp>0 && avatar.hp>=0 && avatar.hp<=avatar.maxHp) {
                        ps->reviveAvatar=avatar; ps->reviveAvatarMs=currentTimeMs();
                    }
                }
                const auto relay = encode(AvatarRelay {ps->connectionId, avatar});
                for (auto& other : peers_) {
                    if (other.transportPeer != peer &&
                        other.status == PeerStatus::Verified && other.transportPeer) {
                        sendTo(other.transportPeer, relay, false);
                    }
                }
                ++relayedAvatars_;
                break;
            }

            default:
                log("Unknown packet type from " + ps->peerId + ": " +
                    std::to_string(static_cast<int>(type)));
                break;
        }
    } catch (const std::exception& ex) {
        if(diagnostic && ps->status==PeerStatus::Verified)desyncCapture_->Reject(ps->connectionId,"malformed diagnostic packet");
        if(resyncPlan_&&fromHost(*ps)&&size&&data[0]>=static_cast<std::uint8_t>(PacketType::ResyncBegin)&&data[0]<=static_cast<std::uint8_t>(PacketType::ResyncEnd))
            finishResync(ResyncResultReason::InvalidSnapshot,"malformed host snapshot record");
        log("Packet decode error from " + ps->peerId + ": " + ex.what());
    }
}

// ---------------------------------------------------------------------------
// World sync helpers
// ---------------------------------------------------------------------------

bool SessionHost::fromHost(const PeerState& ps) const {
    return ps.status == PeerStatus::Verified && ps.assignedSlot == SlotType::Player;
}

PeerState* SessionHost::hostPeer() {
    for (auto& ps : peers_) {
        if (fromHost(ps)) return &ps;
    }
    return nullptr;
}

const PeerState* SessionHost::peerBySlot(SlotType slot) const {
    for (const auto& ps : peers_) {
        if (ps.status == PeerStatus::Verified && ps.assignedSlot == slot) return &ps;
    }
    return nullptr;
}

void SessionHost::forwardToOthers(TransportPeer* sender,
                                  const std::vector<std::uint8_t>& packet,
                                  bool reliable) {
    for (auto& other : peers_) {
        if (other.transportPeer != sender && other.status == PeerStatus::Verified &&
            other.transportPeer) {
            if(!sendTo(other.transportPeer,packet,reliable)&&resyncPlan_&&isMaterialWorldPacket(static_cast<PacketType>(packet.front()))) {finishResync(ResyncResultReason::Overflow,"material forwarding failed");return;}
        }
    }
}

// Catch a late joiner up: room, hold, enemy set, HP, deaths.
void SessionHost::sendWorldStateTo(TransportPeer* peer) try {
    if (!progress_.empty()) {
        // Coalesce the merged byte map into spans, then chunk under the
        // packet size limit; the first chunk replaces the joiner's state.
        std::vector<ProgressSpan> spans;
        for (const auto& [offset, value] : progress_) {
            if (!spans.empty() &&
                spans.back().offset + spans.back().bytes.size() == offset) {
                spans.back().bytes.push_back(value);
            } else {
                spans.push_back({offset, {value}});
            }
        }
        for (const auto& u : splitProgressUpdate(progressVersion_, true, spans)) {
            sendTo(peer, encode(u), true, true);
        }
    }
    if (!room_) return;
    sendTo(peer, encode(*room_), true, true);
    if (hold_ && hold_->epoch == room_->epoch) sendTo(peer, encode(*hold_), true, true);
    // A received complete-empty replacement is state too. Default/absent or
    // append-only empty packets must not manufacture a complete baseline.
    if (!manifest_.entries.empty() || (manifest_.replace && manifest_.epoch != 0 &&
                                       manifest_.epoch == room_->epoch)) {
        EnemyManifest m = manifest_;
        m.replace = true;
        sendTo(peer, encode(m), true, true);
    }
    if (!enemyHp_.empty()) {
        EnemyHp hp;
        hp.epoch = manifest_.epoch;
        hp.sequence = lastEnemyHpSequence_;
        for (const auto& [id, e] : enemyHp_) hp.entries.push_back(e);
        sendTo(peer, encode(hp), true, true);
    }
    for (auto id : deadEnemies_) {
        sendTo(peer, encode(EnemyDeath {manifest_.epoch, id}), true, true);
    }
} catch (...) {
    // Covers cached body construction/encoding and the nested scope/envelope send.
    // Preserve the original exception path, but never attest complete coverage.
    if (callbacks_.onCausalDiagnostic) cacheDiagnostics_.gap();
    throw;
}

// A client disagreeing with the host on the same epoch for two consecutive
// comparisons is reported once per distinct set of fields (transient
// mismatches during a load don't fire). Different epochs aren't compared:
// the client may simply still be loading.
void SessionHost::hashDiagnostic(const char* action, const PeerState& client, const PeerState* host,
                                 std::uint8_t fields, std::uint64_t previousCompare, std::uint64_t compare) {
    hashDiagnostics_.emit(callbacks_.onHashDiagnostic,"relay-hash",[&](auto& out) {
        const auto& c=client.lastHash;
        out<<" action="<<action<<" session="<<session_.sessionId
           <<" slot="<<unsigned(client.assignedSlot)<<" connection="<<client.connectionId
           <<" delivery="<<client.deliverySerial<<" receiveSeq="<<client.hashReceiptSeq
           <<" receiveSeqAvailable="<<(client.hashReceiptSeq != UINT64_MAX)
           <<" scopeAvailable="<<client.diagnosticHashScopeAvailable
           <<" sourceConnection="<<client.diagnosticHashScope.sourceConnectionId
           <<" sourceDelivery="<<client.diagnosticHashScope.sourceDeliverySerial
           <<" hostSource="<<client.diagnosticHashScope.hostSourceSerial
           <<" sourceKind="<<unsigned(client.diagnosticHashScope.kind)
           <<" receiveRecord="<<(std::string(action)=="receive"?hashDiagnostics_.highWater:client.diagnosticHashReceive)
           <<" epoch="<<c.epoch<<" world="<<c.worldId<<" room="<<c.roomId
           <<" enemiesHash="<<c.enemiesHash<<" progressHash="<<c.progressHash
           <<" latestReceived=1 hostAvailable="<<(host!=nullptr)
           <<" hostConnection="<<(host?host->connectionId:0)<<" hostDelivery="<<(host?host->deliverySerial:0)
           <<" hostReceiveSeq="<<(host?host->hashReceiptSeq:0)<<" hostReceiveRecord="<<(host?host->diagnosticHashReceive:0)
           <<" hostScopeAvailable="<<(host && host->diagnosticHashScopeAvailable)
           <<" hostSourceConnection="<<(host?host->diagnosticHashScope.sourceConnectionId:0)
           <<" hostSourceDelivery="<<(host?host->diagnosticHashScope.sourceDeliverySerial:0)
           <<" hostHostSource="<<(host?host->diagnosticHashScope.hostSourceSerial:0)
           <<" hostEpoch="<<(host?host->lastHash.epoch:0)<<" hostWorld="<<(host?host->lastHash.worldId:0)
           <<" hostRoom="<<(host?host->lastHash.roomId:0)<<" hostEnemiesHash="<<(host?host->lastHash.enemiesHash:0)
           <<" hostProgressHash="<<(host?host->lastHash.progressHash:0)<<" fields="<<unsigned(fields)
           <<" streakBefore="<<client.mismatchStreak<<" previousCompare="<<previousCompare<<" compare="<<compare
           <<" nativeComplete="<<client.lastHash.nativeCensusComplete<<" nativeLiving="<<client.lastHash.nativeLivingCount
           <<" nativeCombat="<<client.lastHash.nativeCombatCount
           <<" hostNativeComplete="<<(host?host->lastHash.nativeCensusComplete:false)
           <<" hostNativeLiving="<<(host?host->lastHash.nativeLivingCount:0)
           <<" hostNativeCombat="<<(host?host->lastHash.nativeCombatCount:0)<<" coarseHintOnly=1";
    });
}

void SessionHost::compareWithHost(PeerState& client) {
    auto* host = hostPeer();
    if (!host || !host->hasHash || !client.hasHash) return;
    const auto& h = host->lastHash;
    const auto& c = client.lastHash;
    if (h.epoch != c.epoch) return;
    // A new host hash cannot turn one cached client observation into two
    // mismatch witnesses. This counter is independent of opt-in diagnostics.
    if (!client.hashReceiptSeq || client.comparedHashReceiptSeq == client.hashReceiptSeq) return;
    client.comparedHashReceiptSeq = client.hashReceiptSeq;
    if (client.comparedHashEpoch != c.epoch) {
        client.comparedHashEpoch = c.epoch;
        client.mismatchStreak = 0;
        client.reportedFields = 0;
        client.mismatchFields = 0;
        client.diagnosticPreviousCompare = 0;
    }
    if(desyncComparisonSeq_!=UINT64_MAX)++desyncComparisonSeq_;
    std::uint8_t fields = 0;
    if (h.worldId != c.worldId || h.roomId != c.roomId) fields |= DesyncRoom;
    if (h.enemiesHash != c.enemiesHash) fields |= DesyncEnemies;
    if (h.progressHash != c.progressHash) fields |= DesyncProgress;
    // The implemented native replay covers a complete absent living pack.
    // HP lag, incomplete observations and partial/extra populations remain
    // coarse diagnostics; they cannot request a Bootstrap room reload.
    if ((fields & DesyncEnemies) && !(fields & DesyncRoom) &&
        h.nativeCensusComplete && c.nativeCensusComplete &&
        h.nativeLivingCount > 0 && c.nativeLivingCount == 0 &&
        h.nativeCombatCount >= h.nativeLivingCount && c.nativeCombatCount == 0 &&
        c.enemiesHash == hashAppliedEnemies({})) fields |= DesyncMissingEnemies;
    if (client.mismatchFields != fields) {
        client.mismatchFields = fields;
        client.mismatchStreak = 0;
        client.diagnosticPreviousCompare = 0;
    }
    const auto previousCompare = client.diagnosticPreviousCompare;
    std::uint64_t comparisonRecord = 0;
    if (callbacks_.onHashDiagnostic) {
        const auto before = hashDiagnostics_.highWater;
        hashDiagnostic("compare",client,host,fields,previousCompare);
        comparisonRecord = hashDiagnostics_.highWater > before ? hashDiagnostics_.highWater : 0;
        client.diagnosticPreviousCompare = fields ? comparisonRecord : 0;
    }
    if (fields == 0) {
        client.mismatchStreak = 0;
        client.reportedFields = 0;
        return;
    }
    if (++client.mismatchStreak < 2 || fields == client.reportedFields) return;
    client.reportedFields = fields;
    ++desyncNotices_;
    log("Desync: " + client.peerId + " fields=" + std::to_string(fields) +
        " epoch=" + std::to_string(h.epoch));
    hashDiagnostic("notice",client,host,fields,previousCompare,comparisonRecord);
    broadcastToVerified(encode(DesyncNotice {client.assignedSlot, h.epoch, fields}), true);
    sealHashDiagnostics("notice");
    if(!config_.desyncOutputRoot.empty() && nextDesyncReportId_) {
        DesyncCaptureRequest request;
        request.key={session_.sessionId,nextDesyncReportId_};
        if(nextDesyncReportId_==UINT64_MAX)nextDesyncReportId_=0;else ++nextDesyncReportId_;
        for(const auto& peer:peers_)if(peer.status==PeerStatus::Verified)
            request.connections[static_cast<std::size_t>(peer.assignedSlot)]=peer.connectionId;
        request.epoch=h.epoch;request.divergedSlot=static_cast<std::uint8_t>(client.assignedSlot);request.fields=fields;
        request.hostHash=h;request.clientHash=c;request.hostReceiptSeq=host->hashReceiptSeq;request.clientReceiptSeq=client.hashReceiptSeq;
        request.hostReceiptMs=host->hashReceiptMs;request.clientReceiptMs=client.hashReceiptMs;request.comparisonSeq=desyncComparisonSeq_;
        request.triggerMs=currentTimeMs();request.deadlineMs=request.triggerMs+DESYNC_DEADLINE_MS;
        if(!desyncCapture_->Trigger(request,desyncRelayLog_,request.triggerMs,desyncRelayLogBytes_)) {
            const auto& stats=desyncCapture_->Stats();
            log("Desync capture coalesced/suppressed cadence="+std::to_string(stats.suppressedCadence)+
                " quota="+std::to_string(stats.suppressedQuota)+" extra="+std::to_string(stats.extraTriggers)+
                " overflow="+std::to_string(stats.extraTriggerOverflow)+
                " summaryLostTriggers="+std::to_string(stats.summaryLostTriggers));
        }
    }
}

void SessionHost::pumpDesyncCapture() {
    desyncCapture_->Pump(currentTimeMs());
    if(auto result=desyncCapture_->TakeSuppressionResult()) {
        lastDesyncSuppression_=std::move(*result);
        log("Desync suppression summary session="+lastDesyncSuppression_->sessionId+
            " revision="+std::to_string(lastDesyncSuppression_->revision)+
            " written="+(lastDesyncSuppression_->written?"1":"0")+
            " storageErrors="+std::to_string(desyncCapture_->Stats().summaryStorageErrors)+
            " error="+lastDesyncSuppression_->error);
    }
    if(auto request=desyncCapture_->TakeRequest()) {
        for(const auto& peer:peers_)if(peer.status==PeerStatus::Verified &&
            request->connections[static_cast<std::size_t>(peer.assignedSlot)]==peer.connectionId)
            {
                if(transport_->stats(peer.transportPeer).channelCount<3)desyncCapture_->Reject(peer.connectionId,"diagnostic channel unavailable");
                else sendTo(peer.transportPeer,encode(*request),true);
            }
    }
    if(auto result=desyncCapture_->TakeFinalized()) {
        lastDesyncCapture_=std::move(*result);
        log("Desync capture finalized report="+std::to_string(lastDesyncCapture_->key.reportId)+
            " status="+std::to_string(static_cast<unsigned>(lastDesyncCapture_->status))+
            " manifestWritten="+(lastDesyncCapture_->manifestWritten?"1":"0")+" error="+lastDesyncCapture_->error);
        if(callbacks_.onDesyncCaptureFinalized)callbacks_.onDesyncCaptureFinalized(*lastDesyncCapture_);
    }
}

void SessionHost::clearWorldState() {
    partyLayout_.reset();
    pendingActivation_.clear();
    for (auto& p:peers_) p.reviveAvatarMs=0;
    hold_.reset();
    manifest_ = {};
    enemyHp_.clear();
    deadEnemies_.clear();
}

// ---------------------------------------------------------------------------
// Peer helpers
// ---------------------------------------------------------------------------

PeerState* SessionHost::findPeer(TransportPeer* peer) {
    for (auto& ps : peers_) {
        if (ps.transportPeer == peer) return &ps;
    }
    return nullptr;
}

PeerState* SessionHost::findPeerById(const std::string& peerId) {
    for (auto& ps : peers_) {
        if (ps.peerId == peerId) return &ps;
    }
    return nullptr;
}

bool SessionHost::isSlotTaken(SlotType slot) const {
    return std::any_of(peers_.begin(), peers_.end(), [slot](const PeerState& ps) {
        return ps.status == PeerStatus::Verified && ps.assignedSlot == slot;
    });
}

void SessionHost::rebuildSessionActors() {
    session_.actors.clear();
    for (const auto& peer : peers_) {
        if (peer.status != PeerStatus::Verified) {
            continue;
        }

        SessionActor actor;
        actor.actorId = static_cast<std::uint32_t>(peer.assignedSlot);
        actor.slot = peer.assignedSlot;
        actor.ownerPeerId = peer.peerId;
        actor.connectionId = peer.connectionId;
        session_.actors.push_back(std::move(actor));
    }
}

void SessionHost::expireStalePeers(std::uint64_t nowMs) {
    struct ExpiredPeer {
        TransportPeer* peer{nullptr};
        std::string peerId;
        PeerStatus status{PeerStatus::PendingVersion};
        std::string reason;
    };

    std::vector<ExpiredPeer> expired;
    expired.reserve(peers_.size());

    for (const auto& peer : peers_) {
        const auto timeoutMs =
            peer.status == PeerStatus::Verified ? config_.heartbeatTimeoutMs
                                                : config_.pendingPeerTimeoutMs;
        if (timeoutMs == 0 || peer.lastHeartbeatMs == 0) {
            continue;
        }

        if (nowMs - peer.lastHeartbeatMs < timeoutMs) {
            continue;
        }

        ExpiredPeer expiredPeer;
        expiredPeer.peer = peer.transportPeer;
        expiredPeer.peerId = peer.peerId;
        expiredPeer.status = peer.status;
        expiredPeer.reason = peer.status == PeerStatus::Verified
                                 ? "Heartbeat timed out"
                                 : "Handshake timed out";
        expired.push_back(std::move(expiredPeer));
    }

    bool removedVerifiedPeer = false;

    for (const auto& expiredPeer : expired) {
        // A preceding host expiry may already have ended this peer's session.
        // Do not disconnect or notify entries from that stale snapshot twice.
        if (!findPeer(expiredPeer.peer)) continue;
        if (expiredPeer.peer) {
            transport_->disconnect(expiredPeer.peer, disconnectCode(
                expiredPeer.status == PeerStatus::Verified ? DisconnectReason::PeerIdleTimeout
                                                          : DisconnectReason::HandshakeTimeout));
        }

        removePeer(expiredPeer.peer);

        if (expiredPeer.status == PeerStatus::Verified) {
            removedVerifiedPeer = true;
            log("Peer timed out: " + expiredPeer.peerId);
            if (callbacks_.onPeerLeft) {
                callbacks_.onPeerLeft(expiredPeer.peerId);
            }
            continue;
        }

        log("Rejecting " + expiredPeer.peerId + ": " + expiredPeer.reason);
        if (callbacks_.onPeerRejected) {
            callbacks_.onPeerRejected(expiredPeer.peerId, expiredPeer.reason);
        }
    }

    if (removedVerifiedPeer) {
        rebuildSessionActors();
        broadcastSessionState();
    }
}

void SessionHost::removePeer(TransportPeer* peer) {
    if(auto* ps=findPeer(peer);ps&&resyncPlan_) {
        if(fromHost(*ps))finishResync(ResyncResultReason::HostChanged,"host retired");
        else for(std::size_t i=0;resyncPlan_&&i<resyncPlan_->targetCount;++i)if(resyncPlan_->targets[i].connectionId==ps->connectionId){finishResync(ResyncResultReason::TargetChanged,"target retired");break;}
    }
    if(auto* ps=findPeer(peer)) {
        if(fromHost(*ps))desyncCapture_->Interrupt("host session ended");
        else desyncCapture_->PeerLeft(ps->connectionId);
    }
    pendingActivation_.erase(peer);
    if (auto* ps = findPeer(peer); ps && fromHost(*ps)) {
        session_.sessionId.clear();
        lastEnemyHpSequence_ = 0;
        room_.reset();
        clearWorldState();
        progress_.clear();
        if (config_.runtimeMode == RuntimeMode::CampaignCoop) {
            // The Player owns the co-op world: losing that connection ends the
            // session, without migrating ownership or stopping the relay.
            // Detach all membership before callbacks or later ENet events can
            // observe it. Keep the old entries alive locally during teardown.
            auto departed = std::move(peers_);
            peers_.clear();
            session_.actors.clear();
            progressVersion_ = 0;
            for (const auto& remaining : departed) {
                if (remaining.transportPeer == peer) continue;
                if (remaining.transportPeer)
                    transport_->disconnect(remaining.transportPeer, disconnectCode(DisconnectReason::HostSessionEnded));
            }
            for (const auto& remaining : departed) {
                if (remaining.transportPeer == peer) continue;
                log("Session ended after host loss: " + remaining.peerId);
                if (callbacks_.onPeerLeft) callbacks_.onPeerLeft(remaining.peerId);
            }
            // The caller reports the original host's departure exactly once.
            return;
        }
    }
    peers_.erase(
        std::remove_if(peers_.begin(), peers_.end(),
                        [peer](const PeerState& ps) { return ps.transportPeer == peer; }),
        peers_.end());
}

// ---------------------------------------------------------------------------
// Send helpers
// ---------------------------------------------------------------------------

bool SessionHost::sendTo(TransportPeer* peer,const std::vector<std::uint8_t>& packet,bool reliable,bool cached) {
    std::optional<WorldScope> emittedScope;
    const auto finish=[&](bool result,const char* disposition,const std::vector<std::uint8_t>* wire=nullptr) {
        if(cached)cacheDiagnostics_.emit(callbacks_.onCausalDiagnostic,"relay-cache-delivery",[&](std::ostream& out){
            const auto* target=findPeer(peer);const auto* host=hostPeer();
            out<<" origin=sendWorldStateTo disposition="<<disposition<<" return="<<result
               <<" session="<<session_.sessionId<<" host="<<(host?host->connectionId:0)
               <<" target="<<(target?target->connectionId:0)<<" targetDelivery="<<(target?target->deliverySerial:0)
               <<" targetSlot="<<(target?unsigned(target->assignedSlot):255)
               <<" scopeAvailable="<<emittedScope.has_value()<<" source="<<(emittedScope?emittedScope->sourceConnectionId:0)
               <<" scopeSession="<<(emittedScope?emittedScope->sessionId:"-")
               <<" scopeTarget="<<(emittedScope?emittedScope->targetConnectionId:0)<<" scopeTargetDelivery="<<(emittedScope?emittedScope->targetDeliverySerial:0)
               <<" scopeKind="<<(emittedScope?unsigned(emittedScope->kind):0)
               <<" sourceDelivery="<<(emittedScope?emittedScope->sourceDeliverySerial:0)
               <<" hostSource="<<(emittedScope?emittedScope->hostSourceSerial:0)
               <<" packetType="<<(packet.empty()?0:unsigned(packet.front()))<<" bytes="<<packet.size()
               <<" payloadSHA="<<causalSha(packet)<<" wireAvailable="<<(wire!=nullptr)
               <<" wireSHA="<<(wire?causalSha(*wire):"-")<<" progressVersion="<<progressVersion_;
            causalRoom(out,room_?&*room_:nullptr);
        });
        return result;
    };
    if(!peer||packet.empty())return finish(false,"invalid-input");
    const auto type=static_cast<PacketType>(packet.front());
    if(isScopedWorldPacket(type)) {
        auto* target=findPeer(peer);auto* host=hostPeer();
        if(!target||target->status!=PeerStatus::Verified||!host)return finish(false,"context-unavailable");
        WorldScope scope=forwardingScope_.value_or(WorldScope{session_.sessionId,host->connectionId,host->deliverySerial,lastHostSourceSerial_,0,0});
        const bool simulation=type==PacketType::ActorSnapshot||type==PacketType::EnemySnapshot||type==PacketType::EventMessage;
        if(simulation){scope.kind=WorldSourceKind::Simulation;scope.hostSourceSerial=simulationSourceSerial_;}
        if(type==PacketType::DesyncNotice || type==PacketType::PartyReapply){scope={session_.sessionId,host->connectionId,host->deliverySerial,lastHostSourceSerial_,0,0};scope.kind=WorldSourceKind::Relay;}
        scope.targetConnectionId=target->connectionId;scope.targetDeliverySerial=target->deliverySerial;
        if(cached && callbacks_.onCausalDiagnostic){try{emittedScope=scope;}catch(...){cacheDiagnostics_.gap();}}
        if(scope.sourceConnectionId==host->connectionId && scope.kind==WorldSourceKind::Native && type!=PacketType::DesyncNotice && scope.hostSourceSerial<=target->hostSourceFloor)return finish(true,"source-floor-suppressed");
        WorldEnvelope env{scope,packet};
        if(resyncPlan_&&scope.sourceConnectionId==host->connectionId&&isMaterialWorldPacket(type)) {
            reliable=true;
            for(std::size_t i=0;i<resyncPlan_->targetCount;++i)if(resyncPlan_->targets[i].connectionId==target->connectionId&&target->worldQuarantined) {
                if(resyncContinuation_[i].size()>=RESYNC_MAX_CONTINUATION_RECORDS || resyncContinuationBytes_[i]+packet.size()>RESYNC_MAX_CONTINUATION_BYTES){finishResync(ResyncResultReason::Overflow,"continuation bound");return finish(false,"continuation-overflow");}
                resyncContinuation_[i].push_back(std::move(env));resyncContinuationBytes_[i]+=packet.size();return finish(true,"continuation-buffered");
            }
        }
        if(target->worldQuarantined&&!isEphemeralWorldPacket(type))return finish(true,"quarantine-suppressed");
        const auto wire=encode(env);const bool sent=sendTo(peer,wire,reliable);
        return finish(sent,sent?"enet-submitted":"enet-submission-failed",&wire);
    }
    const bool diagnostic=isDesyncDiagnosticPacket(type);
    if(diagnostic)reliable=true;
    return transport_->send(peer, packet.data(), packet.size(), diagnostic ? 2 : (reliable ? 0 : 1), reliable);
}

void SessionHost::broadcastToVerified(
    const std::vector<std::uint8_t>& packet, bool reliable) {
    for (auto& ps : peers_) {
        if (ps.status == PeerStatus::Verified && ps.transportPeer) {
            sendTo(ps.transportPeer, packet, reliable);
        }
    }
}

void SessionHost::rejectPeer(TransportPeer* peer, const std::string& peerId,
                             const std::string& reason, std::uint8_t code) {
    // A refused handshake cannot become verified through a second packet
    // already queued before the delayed ENet disconnect is observed.
    if (auto* ps = findPeer(peer); ps && ps->status == PeerStatus::PendingVersion)
        ps->status = PeerStatus::Disconnected;
    log("Rejecting " + peerId + ": " + reason);
    if (callbacks_.onPeerRejected) callbacks_.onPeerRejected(peerId, reason);
    HelloReject reject;
    reject.code = code;
    reject.reason = reason;
    sendTo(peer, encode(reject), true);
    // disconnect_later lets the reject packet go out first.
    transport_->disconnectLater(peer, code);
}

void SessionHost::log(const std::string& msg) {
    desyncRelayLogBytes_ += msg.size()+15; // '[SessionHost] ' plus newline
    desyncRelayLog_ += "[SessionHost] "+msg+"\n";
    if(desyncRelayLog_.size()>DESYNC_LOG_BYTES)desyncRelayLog_.erase(0,desyncRelayLog_.size()-DESYNC_LOG_BYTES);
    if (callbacks_.onLog) {
        callbacks_.onLog("[SessionHost] " + msg);
    }
}

} // namespace kh2coop
