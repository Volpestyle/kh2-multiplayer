// Game-thread outbox and one-use local birth proofs. Retained native addresses
// are comparison roots only; every use requires a fresh complete census.
struct EmissionWarmup {
    std::uint32_t firstFrame{},seenFrame{};
    bool Current(std::uint32_t frame,bool initializing) const {
        return frame>=seenFrame && frame-seenFrame<=1 &&
            (!initializing || (frame>=firstFrame && frame-firstFrame<32));
    }
};
struct PendingSpawnEmission {
    SpawnEmission value;
    spawncontroller::EmissionBirth birth;
    std::uint32_t generation = 0, transition = 0, load = 0;
    EmissionWarmup warmup{};
    bool retired{};
};
std::vector<PendingSpawnEmission> g_emissionOutbox;
SpawnEmissionLedger g_emissionLedger;
std::uint64_t g_emissionSequence = 0;
unsigned g_emissionDeniedLogs = 0;
struct LocalEmissionBirth {
    spawncontroller::EmissionBirth birth;
    spawncontroller::EmissionPermit permit;
    std::uint32_t seenFrame{};
    bool adopted{},retired{};
    EmissionWarmup warmup{};
};
std::vector<LocalEmissionBirth> g_localEmissionBirths;

bool EmissionPermitCurrent(const spawncontroller::EmissionPermit& p) {
    RoomTransition room;
    // Do not refresh/retire session state while a census caller holds row refs.
    return p.generation && g_role==Role::Client && g_activationGeneration==p.generation &&
        g_bridge.SessionGeneration()==p.generation && p.load==warp::LoadSerial() &&
        p.transition==warp::TransitionSerial() && p.hostConnection==g_bridge.ConnectionId(0) &&
        p.hostDelivery==g_bridge.PeerDeliverySerial(0) && p.writerDelivery==g_bridge.DeliverySerial() &&
        p.writerDelivery==g_orderedDeliverySerial && RuntimeWriterLive() && g_host.arrived &&
        g_host.epoch==p.epoch && g_lastRoomPacket && g_lastRoomPacket->epoch==p.epoch &&
        ReadLocationChecked(room) && SameLocation(room,*g_lastRoomPacket);
}
bool EmissionBirthMatches(spawncontroller::EmissionBirth& b,const NativeEnemy& native) {
    if(!b.available || b.helper || b.actor!=native.actor || b.objentry!=native.objentry ||
       !native.status || (b.status && b.status!=native.status) || b.objectId!=native.objectId || b.objectType!=native.objectType)return false;
    // Native provenance exists on wrapper return; combat status can be attached
    // by later initialization. Complete it once from a fresh census only after
    // revalidating every captured immutable root/handle and indexed definition.
    auto complete=b;complete.status=native.status;
    if(!spawncontroller::EmissionBirthCurrent(complete))return false;
    b.status=complete.status;return true;
}
// A complete combat census excludes actors whose native status is still null.
// Keep only the witnessed, still-linked pre-status birth for a bounded warmup;
// this observation never supplies a host identity or HP/death authority.
bool InitializingEmissionBirth(const spawncontroller::EmissionBirth& b,const NativeCensus& census,EmissionWarmup& warmup) {
    if(b.status || !warmup.Current(g_hitTraceFrame,true) || !CensusMatchesInstance(census))return false;
    const auto membership=CaptureNativeCensus(b.actor);
    if(!CensusMatchesInstance(membership) || !membership.watchedActorPresent ||
       !spawncontroller::EmissionBirthCurrent(b))return false;
    warmup.seenFrame=g_hitTraceFrame;return true;
}
int LocalEmissionBirthId(const NativeEnemy& native) {
    int result=0;
    for(auto& t:g_localEmissionBirths)if(t.birth.actor==native.actor && !t.adopted && !t.retired) {
        if(!EmissionPermitCurrent(t.permit) || (!t.birth.status &&
           (g_hitTraceFrame<t.warmup.firstFrame || g_hitTraceFrame-t.warmup.firstFrame>=32)) || !EmissionBirthMatches(t.birth,native) ||
           g_hitTraceFrame<t.seenFrame || g_hitTraceFrame-t.seenFrame>1) {t.retired=true;continue;}
        t.seenFrame=g_hitTraceFrame;
        if(result){for(auto& other:g_localEmissionBirths)if(other.birth.actor==native.actor)other.retired=true;return 0;}
        result=t.permit.netId;
    }
    return result;
}
void RetireAbsentEmissionBirths(const NativeCensus& census) {
    for(auto& t:g_localEmissionBirths)if(!t.adopted && !t.retired) {
        bool found=false;for(const auto& n:census.enemies)if(EmissionBirthMatches(t.birth,n)){found=true;break;}
        if(!found && EmissionPermitCurrent(t.permit) && g_hitTraceFrame>=t.seenFrame &&
           g_hitTraceFrame-t.seenFrame<=1 && InitializingEmissionBirth(t.birth,census,t.warmup)) {
            t.seenFrame=g_hitTraceFrame;continue;
        }
        if(!found || !EmissionPermitCurrent(t.permit))t.retired=true;
    }
}
void AdoptEmissionBirth(uintptr_t actor,int id) {
    for(auto& t:g_localEmissionBirths)if(t.birth.actor==actor && !t.adopted && !t.retired) {
        t.adopted=true;
        if(g_log)g_log("[spawn-birth] adopted frame=%u netId=%d expected=%u seq=%llu actor=%llX objectId=%u terminal=%u",
            g_hitTraceFrame,id,t.permit.netId,static_cast<unsigned long long>(t.permit.sequence),static_cast<unsigned long long>(actor),
            t.birth.objectId,g_host.enemies.contains(static_cast<std::uint16_t>(id)) && g_host.enemies.at(static_cast<std::uint16_t>(id)).dead ? 1U:0U);
    }
}

void ResetSpawnEmissions() {
    g_emissionOutbox.clear(); g_emissionLedger.Clear();g_localEmissionBirths.clear();
    g_emissionSequence = 0; g_emissionDeniedLogs = 0;
}
void FlushSpawnEmissions(const NativeCensus& census) {
    RoomTransition room;
    if (!ActivationContext(Role::Host, room)) return;
    const auto observedGeneration=WorldSessionGeneration();
    if(!observedGeneration || !CensusMatchesInstance(census))return;
    // Observe every queued birth before FIFO publication: an initializing head
    // must not create an unobserved gap for later births from the same tick.
    for(auto& p:g_emissionOutbox)if(!p.birth.helper && !p.retired) {
        if(p.generation!=observedGeneration || p.transition!=warp::TransitionSerial() ||
           p.load!=warp::LoadSerial() || !SameLocation(p.value.location,room) ||
           !p.warmup.Current(g_hitTraceFrame,!p.birth.status)) {
            p.retired=true;continue;
        }
        bool ready=false;
        for(const auto& n:census.enemies)if(EmissionBirthMatches(p.birth,n)){ready=true;break;}
        if(ready)p.warmup.seenFrame=g_hitTraceFrame;
        else if(!InitializingEmissionBirth(p.birth,census,p.warmup))p.retired=true;
    }
    while (!g_emissionOutbox.empty()) {
        // Session refresh may clear the outbox; never hold or erase a row across it.
        const auto generation = WorldSessionGeneration();
        if (g_emissionOutbox.empty()) return;
        auto pending = g_emissionOutbox.front();
        if (pending.generation != generation || pending.transition != warp::TransitionSerial() ||
            pending.load != warp::LoadSerial() || !SameLocation(pending.value.location, room)) {
            g_emissionOutbox.erase(g_emissionOutbox.begin()); continue;
        }
        if(pending.retired) {
            if(g_log)g_log("[spawn-birth] host-refused reason=birth-membership-or-roots seq=%llu",static_cast<unsigned long long>(pending.value.sequence));
            g_emissionOutbox.erase(g_emissionOutbox.begin());continue;
        }
        if(!pending.birth.helper) {
            if(!pending.birth.status)return; // checked pending initialization; no identity yet
            const auto found=g_inst.byActor.find(pending.birth.actor);
            const NativeEnemy* native=nullptr;
            for(const auto& n:census.enemies)if(EmissionBirthMatches(pending.birth,n)){native=&n;break;}
            if(!CensusMatchesInstance(census) || !native || found==g_inst.byActor.end() || found->second>=g_inst.spawns.size()) {
                if(g_log)g_log("[spawn-birth] host-refused reason=birth-no-longer-current seq=%llu",static_cast<unsigned long long>(pending.value.sequence));
                g_emissionOutbox.erase(g_emissionOutbox.begin());continue;
            }
            // Persist one-way status completion even if publication is retried.
            g_emissionOutbox.front().birth=pending.birth;
            const auto& spawn=g_inst.spawns[found->second];
            if(!spawn.announced || !spawn.present || spawn.spawnIndex+1>0xFFFF ||
               spawn.actor!=native->actor || spawn.objentry!=native->objentry || spawn.status!=native->status || spawn.objectId!=native->objectId) {
                g_emissionOutbox.erase(g_emissionOutbox.begin());continue;
            }
            pending.value.netId=static_cast<std::uint16_t>(spawn.spawnIndex+1);
        }
        pending.value.location = room;
        ProducerWorldContext context;
        if (!CaptureWorldContext(context) || !SendCapturedWorld(encode(pending.value), context)) return;
        if(g_emissionOutbox.empty() || g_emissionOutbox.front().value.sequence!=pending.value.sequence ||
           g_emissionOutbox.front().generation!=pending.generation)return;
        if (g_log) g_log("[spawn-emission] sent epoch=%u seq=%llu key=%u header=%u record=%u netId=%u point=(%.1f,%.1f,%.1f,%.1f)",
            room.epoch, static_cast<unsigned long long>(pending.value.sequence), pending.value.controllerKey,
            pending.value.headerId, pending.value.recordIndex, pending.value.netId, pending.value.point[0], pending.value.point[1],
            pending.value.point[2], pending.value.point[3]);
        g_emissionOutbox.erase(g_emissionOutbox.begin());
    }
}
void ReceiveSpawnEmission(const WorldScope& scope, const SpawnEmission& value) {
    // The target room packet precedes its emissions on the reliable channel.
    // Do not require native arrival: that discarded the BC intro's early adds.
    if (CurrentRole() != Role::Client || !g_lastRoomPacket ||
        !scope.sourceConnectionId || scope.sourceConnectionId != g_bridge.ConnectionId(0) ||
        scope.sourceDeliverySerial != g_bridge.PeerDeliverySerial(0) || !scope.hostSourceSerial ||
        !sameActivationLocation(value.location, *g_lastRoomPacket)) return;
    if (g_emissionLedger.Add(value) && g_log)
        g_log("[spawn-emission] received epoch=%u seq=%llu key=%u header=%u record=%u netId=%u",
            value.location.epoch, static_cast<unsigned long long>(value.sequence), value.controllerKey, value.headerId, value.recordIndex,value.netId);
}
