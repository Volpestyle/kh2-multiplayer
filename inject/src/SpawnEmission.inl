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
    uintptr_t baileyMission{};
    routespawn::BaileyCreeperLease baileyLease{};
};
std::vector<LocalEmissionBirth> g_localEmissionBirths;

// Read-only scope: never refresh session state while a caller holds native rows.
bool BaileyMissionCurrent(bool terminal,uintptr_t* mission,const char** refusal) {
    if(mission)*mission=0;
    if(refusal)*refusal="ok";
    const auto check=[refusal](bool value,const char* reason){if(!value && refusal)*refusal=reason;return value;};
    namespace mr=missionmirror::rules;
    RoomTransition location{};uintptr_t base=0;std::uint32_t flags=0,result=0;
    char path[mr::kPathBytes]{},file[mr::kFileBytes]{};
    const auto load=warp::LoadSerial(),transition=warp::TransitionSerial();
    if(!check(g_mirrorRequested,"mirror") || !check(spawncontroller::IsDiagnosticGameThread(),"owner-thread") ||
       !check(g_bridge.IsOpen(),"bridge") || !check(!g_recordBindingRequested,"legacy-binding") ||
       !check(!g_recordAuthorityRequested,"legacy-authority") || !check(g_resyncWriteFence==ResyncWriteFence::None,"resync-fence") ||
       !check(g_activationGeneration!=0,"activation") || !check(g_bridge.SessionGeneration()==g_activationGeneration,"generation") ||
       !check(g_orderedDeliverySerial!=0,"ordered") || !check(g_orderedDeliverySerial==g_bridge.DeliverySerial(),"delivery") ||
       !check(RuntimeWriterLive(),"writer") || !check(g_inst.live,"instance") || !check(SafeNativeGameplay(),"gameplay") ||
       !check(g_seenLoad==load,"seen-load") || !check(g_seenTransition==transition,"seen-transition") ||
       !check(ReadLocationChecked(location),"location-read") || !check(ReadNative(g_exeBase+mr::kMissionPtr,base),"mission-read") ||
       !check(base!=0,"mission-null") || !check(CopyNative(base+mr::kPath,path,sizeof(path)),"path-read") ||
       !check(mr::FileOf(path,sizeof(path),file),"file") || !check(ReadNative(base+mr::kFlags,flags),"flags-read") ||
       !check(ReadNative(base+mr::kResult,result),"result-read"))return false;
    const std::array<std::uint8_t,10> now{location.worldId,location.roomId,location.door,0,
        static_cast<std::uint8_t>(location.mapProgram),static_cast<std::uint8_t>(location.mapProgram>>8),
        static_cast<std::uint8_t>(location.battleProgram),static_cast<std::uint8_t>(location.battleProgram>>8),
        static_cast<std::uint8_t>(location.eventProgram),static_cast<std::uint8_t>(location.eventProgram>>8)};
    uintptr_t after=0;std::uint32_t flagsAfter=0,resultAfter=0;
    if(!check(routespawn::BaileyCreeperMission(now,file,true,flags,result,terminal),"mission-scope") ||
       !check(ReadNative(g_exeBase+mr::kMissionPtr,after),"mission-bookend-read") || !check(after==base,"mission-bookend") ||
       !check(ReadNative(base+mr::kFlags,flagsAfter),"flags-bookend-read") || !check(flagsAfter==flags,"flags-bookend") ||
       !check(ReadNative(base+mr::kResult,resultAfter),"result-bookend-read") || !check(resultAfter==result,"result-bookend") ||
       !check(load==warp::LoadSerial(),"load-bookend") || !check(transition==warp::TransitionSerial(),"transition-bookend"))return false;
    if(mission)*mission=base;return true;
}
bool BaileyCreeperHostMotion(uintptr_t actor) {
    spawncontroller::EmissionBirth birth{};
    return g_role==Role::Host && BaileyMissionCurrent() && spawncontroller::CaptureBaileyCreeperBirth(actor,birth);
}

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
void ObserveBaileyCreeperBirths(const NativeCensus& census) {
    for(auto& t:g_localEmissionBirths)if(t.baileyMission && !t.retired) {
        uintptr_t mission=0;
        bool current=BaileyMissionCurrent(t.adopted,&mission) && mission==t.baileyMission &&
            CensusMatchesInstance(census) && EmissionPermitCurrent(t.permit);
        bool found=false;
        if(current)for(const auto& native:census.enemies)
            if(EmissionBirthMatches(t.birth,native)){found=true;break;}
        if(!found && current && !t.adopted)
            found=InitializingEmissionBirth(t.birth,census,t.warmup);
        current=current && found && spawncontroller::BaileyCreeperBirthCurrent(t.birth);
        if(!t.baileyLease.Observe(g_hitTraceFrame,current)) {
            t.retired=true;
            if(g_log)g_log("[bailey317] retired frame=%u netId=%u seq=%llu actor=%llX",g_hitTraceFrame,t.permit.netId,
                static_cast<unsigned long long>(t.permit.sequence),static_cast<unsigned long long>(t.birth.actor));
        }
    }
}
int BaileyCreeperTicketId(const NativeEnemy& native,bool terminal) {
    int id=0;uintptr_t mission=0;
    if(native.objectId!=317 || !BaileyMissionCurrent(terminal,&mission))return 0;
    for(auto& t:g_localEmissionBirths)if(t.baileyMission && t.birth.actor==native.actor && !t.retired) {
        const bool current=t.baileyMission==mission && EmissionPermitCurrent(t.permit) &&
            EmissionBirthMatches(t.birth,native) && spawncontroller::BaileyCreeperBirthCurrent(t.birth);
        if(!t.baileyLease.Observe(g_hitTraceFrame,current)) {t.retired=true;continue;}
        if(id){t.retired=true;for(auto& other:g_localEmissionBirths)if(other.birth.actor==native.actor)other.retired=true;return 0;}
        id=t.permit.netId;
    }
    return id;
}
bool BaileyCreeperAuthority(uintptr_t actor,bool terminal) {
    if(g_role!=Role::Client || !BaileyMissionCurrent(terminal))return false;
    const auto found=g_inst.byActor.find(actor);
    if(found==g_inst.byActor.end() || found->second>=g_inst.spawns.size())return false;
    const auto& s=g_inst.spawns[found->second];
    if(!s.present || s.objectId!=317 || s.netId<=0 || s.netId>0xFFFF || s.ordinaryBinding.bound!=s.netId)return false;
    const auto retire=[actor] {
        for(auto& ticket:g_localEmissionBirths)if(ticket.baileyMission && ticket.birth.actor==actor) {
            ticket.retired=true;ticket.baileyLease.retired=true;
        }
        return false;
    };
    const auto host=g_host.enemies.find(static_cast<std::uint16_t>(s.netId));
    if(host==g_host.enemies.end() || host->second.objectId!=317 || host->second.removed)return retire();
    if(host->second.dead!=terminal)return false; // living callbacks cannot consume the pending terminal handoff
    try {
        const auto census=CaptureNativeCensus(actor);
        if(!CensusMatchesInstance(census) || !census.watchedActorPresent)return retire();
        const auto* native=FindNativeEnemy(census,s);ordinarybinding::Identity roots{};
        if(!native || !ReadOrdinaryIdentity(*native,roots) || roots!=s.ordinaryBinding.identity)return retire();
        return BaileyCreeperTicketId(*native,terminal)==s.netId;
    } catch(...) {return retire();}
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
