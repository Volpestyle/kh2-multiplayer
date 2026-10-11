// BC ordinary membership only. Reuses PopulationCut content/occurrence proofs;
// deliberately separate from population repair and native death authorization.
struct OrdinaryBirth {
    PopulationOccurrence occurrence;
    NativeRecordContentDefinition definition;
    combatcausal::Target target;
    std::uint32_t lastSeenFrame{};
};
std::vector<OrdinaryBirth> g_ordinaryBirths;
ordinarylifecycle::History g_ordinaryHostHistory,g_ordinaryClientHistory;
std::uint64_t g_ordinarySequence=0;
std::uint32_t g_ordinaryGeneration=0,g_ordinaryLoad=0,g_ordinaryTransition=0;
std::optional<WorldScope> g_ordinarySource;
// Transport authentication is retained separately from native admission. Native
// load initialization clears bindings, but must not discard an in-flight bootstrap.
struct PendingOrdinaryCut {
    PopulationCut cut;
    WorldScope source;
    std::uint32_t generation{},load{},transition{};
    std::uint64_t receivedMs{};
    bool arrived{};
};
std::vector<PendingOrdinaryCut> g_pendingOrdinaryCuts;
void ClearPendingOrdinaryCuts() {g_pendingOrdinaryCuts.clear();}
// Original cut/connection/native roots survive later replacement births; never re-anchor.
struct OrdinaryRetirement {
    Spawn original;
    PopulationOccurrence occurrence;
    NativeRecordContentDefinition definition;
    WorldScope source;
    std::uint32_t generation{},load{},transition{},frame{},attemptFrame{};
    bool attempted{},failed{},completed{};
    const char* outcome="queued";
};
std::vector<OrdinaryRetirement> g_ordinaryRetirements;
// Client-first native removal is not an authoritative retirement. Keep the last
// adjacent, fully checked living binding until its original host cut arrives.
struct OrdinaryPresence {
    OrdinaryRetirement anchor;
    combatcausal::Target target;
    std::uint32_t seen{},absent{},checked{};
    bool pending{},failed{},finished{};
};
std::vector<OrdinaryPresence> g_ordinaryPresence;
bool RequestOrdinaryNativeLeave(const Spawn& original,int hp);
using OrdinaryLeaveFn=bool(*)(const Spawn&,int);
OrdinaryLeaveFn g_ordinaryLeave=RequestOrdinaryNativeLeave;
void ResetOrdinaryLifecycle() {
    g_ordinaryBirths.clear();g_ordinaryRetirements.clear();g_ordinaryPresence.clear();g_ordinaryHostHistory.Reset();g_ordinaryClientHistory.Reset();
    g_ordinarySequence=0;g_ordinaryGeneration=g_ordinaryLoad=g_ordinaryTransition=0;g_ordinarySource.reset();
}
bool OrdinaryLifecycleRoom() {
    return !g_populationRequested && g_inst.live && g_inst.world==5 && g_inst.room==6 && g_inst.door==0 &&
        g_inst.map==1 && g_inst.btl==1 && g_inst.evt==0;
}
const PopulationCut* OrdinaryCut() {
    const auto& c=g_role==Role::Host?g_ordinaryHostHistory.Current():g_ordinaryClientHistory.Current();
    return c?&*c:nullptr;
}
bool OrdinaryCutCurrent() {
    if(!OrdinaryLifecycleRoom() || !OrdinaryCut() || !g_ordinaryGeneration ||
       WorldSessionGeneration()!=g_ordinaryGeneration || warp::LoadSerial()!=g_ordinaryLoad ||
       warp::TransitionSerial()!=g_ordinaryTransition || !SafeNativeGameplay())return false;
    if(g_role==Role::Host)return OrdinaryCut()->epoch==g_epoch;
    return g_role==Role::Client && g_ordinarySource && OrdinaryCut()->epoch==g_host.epoch &&
        g_ordinarySource->sourceConnectionId==g_bridge.ConnectionId(0) &&
        g_ordinarySource->sourceDeliverySerial==g_bridge.PeerDeliverySerial(0) &&
        g_ordinarySource->targetConnectionId==g_bridge.ConnectionId(g_bridge.LocalSlot()) &&
        g_ordinarySource->targetDeliverySerial==g_bridge.DeliverySerial();
}
// Retain the exact raw cut alongside source/causal receipts. Bounded cut codec;
// no repaired/synthetic death, and no native call on a retired occurrence.
void OrdinaryCutReceipt(const PopulationCut& cut,const char* action) {
    if(g_log)g_log("[ordinary-lifecycle] action=%s cut=%llu epoch=%u hostLoad=%u hostTransition=%u rows=%zu killCredit=0 nativeWrite=0",
        action,(unsigned long long)cut.sequence,cut.epoch,cut.hostLoad,cut.hostTransition,cut.entries.size());
}
combatcausal::Target OrdinaryHistoricalTarget(const Spawn& spawn,const NativeEnemy& native,unsigned id) {
    ordinarybinding::Identity identity;
    if(!ReadOrdinaryIdentity(native,identity) || identity.actor!=spawn.actor || identity.objentry!=spawn.objentry ||
       identity.status!=spawn.status || identity.controller!=spawn.controller || identity.record!=spawn.record)return {};
    return {identity.actor,identity.objentry,identity.status,identity.controller,identity.record,id,native.objectId,native.objectType,native.hp,native.maxHp};
}
bool OrdinaryPresenceCurrent(const OrdinaryPresence& presence) {
    const auto& anchor=presence.anchor;
    if(presence.failed || presence.finished || !OrdinaryCutCurrent() || g_role!=Role::Client ||
       !g_ordinarySource || anchor.generation!=WorldSessionGeneration() || anchor.load!=warp::LoadSerial() ||
       anchor.transition!=warp::TransitionSerial() || anchor.source.sessionId!=g_ordinarySource->sessionId ||
       anchor.source.sourceConnectionId!=g_ordinarySource->sourceConnectionId ||
       anchor.source.sourceDeliverySerial!=g_ordinarySource->sourceDeliverySerial ||
       anchor.source.targetConnectionId!=g_ordinarySource->targetConnectionId ||
       anchor.source.targetDeliverySerial!=g_ordinarySource->targetDeliverySerial ||
       anchor.source.hostSourceSerial>g_ordinarySource->hostSourceSerial || g_resyncWriteFence!=ResyncWriteFence::None)return false;
    if(presence.target.netId!=anchor.occurrence.netId ||
       PopulationSpawnRoots(anchor.original)!=std::array<uintptr_t,5>{presence.target.actor,presence.target.objentry,
           presence.target.status,presence.target.controller,presence.target.record})return false;
    unsigned originals=0;
    for(const auto& tracked:g_inst.spawns)if(tracked.actor==presence.target.actor &&
       tracked.ordinaryBinding.highWater==static_cast<int>(presence.target.netId) &&
       PopulationSpawnRoots(tracked)==PopulationSpawnRoots(anchor.original))++originals;
    if(originals!=1)return false;
    const auto* row=ordinarylifecycle::Find(*OrdinaryCut(),anchor.occurrence.netId);
    return row && !row->terminal && row->birthSequence==anchor.occurrence.birthSequence &&
        row->record==anchor.occurrence.record &&
        population_cut_detail::sameDefinition(OrdinaryCut()->definitions[row->definition],anchor.definition);
}
bool OrdinaryPresenceRecord(const OrdinaryPresence& presence,const RecordCatalog& catalog) {
    unsigned matches=0;
    for(std::uint32_t index=0;index<catalog.entryCount;++index) {
        const auto& entry=catalog.entries[index];
        if(population_cut_detail::sameDefinition(entry.content,presence.anchor.definition)) {
            if(entry.controller!=presence.target.controller ||
               entry.spawnArray+64*presence.anchor.occurrence.record!=presence.target.record)return false;
            ++matches;
        }
    }
    return matches==1;
}
void OrdinaryPresenceFailure(OrdinaryPresence& presence,const char* reason) {
    if(presence.failed)return;
    presence.failed=true;InvalidateClientClaims("ordinary-native-leave-unproved");
    if(g_log)g_log("[ordinary-lifecycle] native-pending netId=%u actor=%llX frame=%u outcome=%s killCredit=0 nativeWrite=0",
        presence.target.netId,(unsigned long long)presence.target.actor,g_hitTraceFrame,reason);
}
// Runs with the observer OFF too. No cached address is dereferenced after absence.
bool RefreshOrdinaryPresence(const NativeCensus& census) {
    if(g_role!=Role::Client || !OrdinaryLifecycleRoom() || !OrdinaryCut())return true;
    RecordCatalog first,last;
    if(!OrdinaryCutCurrent() || !CensusMatchesInstance(census) || !FreshRecordCatalog(census,first))return false;
    bool valid=true;
    for(auto& presence:g_ordinaryPresence) {
        if(presence.finished)continue;
        const auto death=g_host.enemies.find(static_cast<std::uint16_t>(presence.target.netId));
        if(!presence.pending && !presence.failed && death!=g_host.enemies.end() && death->second.dead) {
            // The existing authenticated death/native-zero path owns this body;
            // do not reinterpret it as a living membership leave.
            presence.finished=true;continue;
        }
        if(!OrdinaryPresenceCurrent(presence) || !OrdinaryPresenceRecord(presence,first)) {
            OrdinaryPresenceFailure(presence,"refuse-original-scope-or-record");valid=false;continue;
        }
        const auto found=std::find_if(census.enemies.begin(),census.enemies.end(),[&](const auto& native){return native.actor==presence.target.actor;});
        if(found==census.enemies.end()) {
            if(presence.pending && presence.checked!=g_hitTraceFrame &&
               (presence.checked==UINT32_MAX || presence.checked+1!=g_hitTraceFrame)) {
                OrdinaryPresenceFailure(presence,"refuse-pending-census-gap");valid=false;continue;
            }
            if(!presence.pending) {
                if(presence.seen==UINT32_MAX || presence.seen+1!=g_hitTraceFrame || presence.target.hp<=0) {
                    OrdinaryPresenceFailure(presence,"refuse-nonadjacent-native-leave");valid=false;continue;
                }
                presence.pending=true;presence.absent=g_hitTraceFrame;
                InvalidateClientClaims("ordinary-native-leave-awaiting-cut");
                if(g_log)g_log("[ordinary-lifecycle] native-pending netId=%u actor=%llX frame=%u lastSeen=%u hp=%d outcome=awaiting-authoritative-cut killCredit=0 nativeWrite=0",
                    presence.target.netId,(unsigned long long)presence.target.actor,g_hitTraceFrame,presence.seen,presence.target.hp);
            }
            presence.checked=g_hitTraceFrame;
            const auto repeated=CaptureNativeCensus(presence.target.actor);
            if(g_hitTraceFrame<presence.absent || g_hitTraceFrame-presence.absent>120 ||
               !CensusMatchesInstance(repeated) || repeated.watchedActorPresent || !OrdinaryPresenceCurrent(presence)) {
                OrdinaryPresenceFailure(presence,"refuse-pending-absence-or-timeout");valid=false;
            }
        } else {
            if(presence.pending) {OrdinaryPresenceFailure(presence,"refuse-reappeared-or-reused-actor");valid=false;continue;}
            const auto target=OrdinaryHistoricalTarget(presence.anchor.original,*found,presence.target.netId);
            if(!combatcausal::SameTarget(target,presence.target) || target.hp<=0) {
                OrdinaryPresenceFailure(presence,"refuse-changed-living-roots");valid=false;continue;
            }
            presence.target=target;presence.seen=g_hitTraceFrame;
        }
    }
    // Establish presence only from an already earned, live native binding.
    for(const auto& spawn:g_inst.spawns)if(spawn.netId>0 && spawn.present && !spawn.lifecycleRetired) {
        const auto* row=ordinarylifecycle::Find(*OrdinaryCut(),static_cast<unsigned>(spawn.netId));
        const auto exists=std::find_if(g_ordinaryPresence.begin(),g_ordinaryPresence.end(),[&](const auto& presence){return presence.target.netId==static_cast<unsigned>(spawn.netId);});
        const auto death=g_host.enemies.find(static_cast<std::uint16_t>(spawn.netId));
        if(exists!=g_ordinaryPresence.end() || !row || row->terminal || spawn.ordinaryBinding.highWater!=spawn.netId ||
           (death!=g_host.enemies.end() && death->second.dead))continue;
        const auto* native=FindNativeEnemy(census,spawn);if(!native || native->hp<=0)continue;
        OrdinaryPresence presence;
        presence.anchor={spawn,*row,OrdinaryCut()->definitions[row->definition],*g_ordinarySource,
            WorldSessionGeneration(),census.load,census.transition,g_hitTraceFrame};
        presence.target=OrdinaryHistoricalTarget(spawn,*native,row->netId);presence.seen=g_hitTraceFrame;
        if(!combatcausal::SameTarget(presence.target,presence.target) || !OrdinaryPresenceRecord(presence,first) ||
           g_ordinaryPresence.size()==POPULATION_MAX_ENTRIES) {
            for(auto& original:g_ordinaryPresence)if(!original.finished)OrdinaryPresenceFailure(original,"refuse-presence-establishment");
            return false;
        }
        g_ordinaryPresence.push_back(std::move(presence));
    }
    const auto repeated=CaptureNativeCensus();
    bool bookend=FreshRecordCatalog(census,last) && SameRecordCatalogSample(first,last) &&
        CensusMatchesInstance(census) && CensusMatchesInstance(repeated) && OrdinaryCutCurrent();
    for(const auto& presence:g_ordinaryPresence)if(!presence.finished && !presence.failed) {
        const auto found=std::find_if(repeated.enemies.begin(),repeated.enemies.end(),[&](const auto& native){return native.actor==presence.target.actor;});
        if(presence.pending)bookend=bookend && found==repeated.enemies.end();
        else if(found==repeated.enemies.end())bookend=false;
        else {
            const auto target=OrdinaryHistoricalTarget(presence.anchor.original,*found,presence.target.netId);
            bookend=bookend && combatcausal::SameTarget(target,presence.target) && target.hp==presence.target.hp;
        }
    }
    if(!bookend)for(auto& presence:g_ordinaryPresence)if(!presence.finished)
        OrdinaryPresenceFailure(presence,"refuse-presence-bookend");
    return valid && bookend;
}
// Only an unbound, independently typed replacement of the pending record may
// be omitted from the diagnostic population. It receives no ID or HP authority.
bool OrdinaryPendingReplacement(const NativeCensus& census,const NativeEnemy& native) {
    if(native.objectId!=302 || native.objectType!=4 || native.hp<=0 || census.enemies.size()>5 || !OrdinaryCutCurrent())return false;
    ordinarybinding::Identity identity;RecordCatalog first,last;spawncontroller::NativeRecordMembership member;
    if(!ReadOrdinaryIdentity(native,identity) || !FreshRecordCatalog(census,first) ||
       !RecordMember(first,census,native.actor,member) || member.issues)return false;
    unsigned localAliases=0;
    for(const auto& candidate:census.enemies) {
        ordinarybinding::Identity checked;
        if(!ReadOrdinaryIdentity(candidate,checked))return false;
        if(checked.controller==identity.controller && checked.record==identity.record)++localAliases;
    }
    if(localAliases!=1)return false;
    unsigned matches=0;
    for(const auto& presence:g_ordinaryPresence)if(presence.pending && OrdinaryPresenceCurrent(presence) &&
       native.actor!=presence.target.actor && !CensusHasActor(census,presence.target.actor) &&
       identity.controller==presence.target.controller && identity.record==presence.target.record &&
       member.recordIndex==presence.anchor.occurrence.record && OrdinaryPresenceRecord(presence,first))++matches;
    return matches==1 && FreshRecordCatalog(census,last) && SameRecordCatalogSample(first,last) && CensusMatchesInstance(census);
}
bool PublishOrdinaryLifecycle(const NativeCensus& census) {
    if(!OrdinaryLifecycleRoom())return true;
    if(g_role!=Role::Host || !CensusMatchesInstance(census) || !SafeNativeGameplay())return false;
    const auto generation=WorldSessionGeneration();
    if(!generation || (g_ordinaryGeneration && (generation!=g_ordinaryGeneration ||
        census.load!=g_ordinaryLoad || census.transition!=g_ordinaryTransition)))return false;
    // Wait for the initial complete five-Shadow manifest; no partial certificate
    // may block native activation or manufacture an initial source admission.
    if(!g_ordinaryHostHistory.Current() && (census.enemies.size()!=5 ||
       std::any_of(g_inst.spawns.begin(),g_inst.spawns.end(),[](const auto& s){return s.present&&!s.announced;})))return true;
    RecordCatalog before,after;
    if(!FreshRecordCatalog(census,before))return false;
    auto births=g_ordinaryBirths;
    auto sequence=g_ordinarySequence;
    const auto next=[&](std::uint64_t& output){if(sequence==UINT64_MAX)return false;output=++sequence;return true;};
    for(const auto& spawn:g_inst.spawns) {
        const auto id=static_cast<std::uint16_t>(spawn.spawnIndex+1);
        auto old=std::find_if(births.begin(),births.end(),[&](const auto& b){return b.occurrence.netId==id;});
        if(old!=births.end()) {
            if(old->occurrence.terminal)continue;
            if(PopulationSpawnRoots(spawn)!=std::array<uintptr_t,5>{old->target.actor,old->target.objentry,old->target.status,old->target.controller,old->target.record})return false;
            if(spawn.present) {
                if(!PopulationBirthRootsCurrent(spawn,PopulationSpawnRoots(spawn)))return false;
                const auto* native=FindNativeEnemy(census,spawn);if(!native)return false;
                old->lastSeenFrame=g_hitTraceFrame;old->target.hp=native->hp;
                if(spawn.deathSent && native->hp==0) {
                    if(!next(old->occurrence.terminalSequence))return false;old->occurrence.terminal=true;
                }
            } else if(spawn.deathSent && old->target.hp==0 && spawn.lastHp<=0) {
                // The ordinary native-death receipt already owns this terminal.
                // Do not reinterpret a legitimately dead body's later absence
                // as a living membership removal.
                if(!next(old->occurrence.terminalSequence))return false;
                old->occurrence.terminal=true;
            } else {
                // Full current census + immediately previous authenticated living
                // five-root member + still-readable original roots/HP/content.
                // This proves membership removal, NOT the script/cull trigger.
                if(old->lastSeenFrame+1!=g_hitTraceFrame || old->target.hp<=0 || spawn.lastHp!=old->target.hp ||
                   std::any_of(census.enemies.begin(),census.enemies.end(),[&](const auto& n){return n.actor==old->target.actor;}))return false;
                NativeEnemy original;bool enemy=false;
                if(!ReadNativeEnemy(spawn.actor,original,enemy) || !enemy || original.objentry!=old->target.objentry ||
                   original.status!=old->target.status || original.hp!=old->target.hp || original.maxHp!=old->target.maxHp ||
                   !PopulationBirthRootsCurrent(spawn,PopulationSpawnRoots(spawn)))return false;
                unsigned definitions=0;
                for(std::uint32_t i=0;i<before.entryCount;++i)if(population_cut_detail::sameDefinition(before.entries[i].content,old->definition)) {
                    const auto& entry=before.entries[i];
                    if(entry.controller!=old->target.controller || entry.spawnArray+64*old->occurrence.record!=old->target.record)return false;
                    ++definitions;
                }
                if(definitions!=1 || !next(old->occurrence.terminalSequence))return false;
                old->occurrence.terminal=true;old->occurrence.removed=true;
            }
            continue;
        }
        if(!spawn.announced)continue; // manifest ordering; post-manifest call captures this birth
        if(!spawn.present || spawn.lastHp<=0 || spawn.objectId!=302 || births.size()==POPULATION_MAX_ENTRIES)return false;
        const auto roots=PopulationSpawnRoots(spawn);
        if(!PopulationBirthRootsCurrent(spawn,roots))return false;
        for(const auto& b:births)if(b.target.actor==spawn.actor)return false; // no reused address authority
        const auto* native=FindNativeEnemy(census,spawn);if(!native)return false;
        spawncontroller::NativeRecordMembership member;
        if(!RecordMember(before,census,spawn.actor,member) || member.issues ||
           member.controller[0]!=roots[3] || member.controller[1]!=roots[3] ||
           member.record[0]!=roots[4] || member.record[1]!=roots[4])return false;
        OrdinaryBirth birth;
        birth.definition=before.entries[member.tableIndex].content;
        unsigned aliases=0;for(std::uint32_t i=0;i<before.entryCount;++i)
            if(population_cut_detail::sameDefinition(before.entries[i].content,birth.definition))++aliases;
        if(aliases!=1)return false;
        birth.target={roots[0],roots[1],roots[2],roots[3],roots[4],id,302,4,native->hp,native->maxHp};
        birth.lastSeenFrame=g_hitTraceFrame;auto& occurrence=birth.occurrence;
        occurrence.netId=id;occurrence.objectId=302;occurrence.record=member.recordIndex;
        occurrence.activationPoint={spawn.spawnPos.x,spawn.spawnPos.y,spawn.spawnPos.z,1};
        if(!next(occurrence.birthSequence))return false;occurrence.occurrence=occurrence.birthSequence;
        births.push_back(std::move(birth));
    }
    if(!FreshRecordCatalog(census,after) || !SameRecordCatalogSample(before,after) ||
       generation!=WorldSessionGeneration() || !CensusMatchesInstance(census))return false;
    const auto repeated=CaptureNativeCensus();
    if(!CensusMatchesInstance(repeated) || repeated.nodeCount!=census.nodeCount || repeated.enemies.size()!=census.enemies.size())return false;
    for(const auto& native:census.enemies) {
        const auto found=std::find_if(repeated.enemies.begin(),repeated.enemies.end(),[&](const auto& n){return n.actor==native.actor;});
        if(found==repeated.enemies.end() || !SameNativeIdentity(native,*found) || native.hp!=found->hp || native.maxHp!=found->maxHp)return false;
    }
    if(births.empty())return true; // before first manifest
    PopulationCut cut;cut.membershipOnly=true;cut.epoch=g_epoch;cut.hostLoad=census.load;cut.hostTransition=census.transition;
    cut.sequence=sequence;cut.location=PopulationLocation(census.location);
    for(const auto& b:births) {
        auto row=b.occurrence;auto found=std::find_if(cut.definitions.begin(),cut.definitions.end(),[&](const auto& d){return population_cut_detail::sameDefinition(d,b.definition);});
        if(found==cut.definitions.end()){row.definition=static_cast<std::uint16_t>(cut.definitions.size());cut.definitions.push_back(b.definition);}
        else row.definition=static_cast<std::uint16_t>(found-cut.definitions.begin());
        cut.entries.push_back(row);
    }
    if(!g_ordinaryHostHistory.Current() || sequence!=g_ordinarySequence) {
        auto history=g_ordinaryHostHistory;
        if(!history.Admit(cut))return false;
        const auto bytes=encode(cut);ProducerWorldContext source;
        if(!CaptureWorldContext(source) || !WorldContextCurrent(source) || !SendCapturedWorld(bytes,source))return false;
        for(const auto& b:births)if(b.occurrence.removed) {
            const auto prior=std::find_if(g_ordinaryBirths.begin(),g_ordinaryBirths.end(),[&](const auto& a){return a.occurrence.netId==b.occurrence.netId;});
            if(prior!=g_ordinaryBirths.end() && !prior->occurrence.terminal && combatcausal::Requested())
                (void)combatcausal::engine.RetireMember(CausalScope(),b.occurrence.netId,b.occurrence.birthSequence,
                    b.occurrence.terminalSequence,b.target,bytes.data(),static_cast<unsigned>(bytes.size()),combatcausal::Now());
            for(auto& s:g_inst.spawns)if(s.spawnIndex+1==b.occurrence.netId)s.lifecycleRetired=true;
        }
        g_ordinaryHostHistory=std::move(history);g_ordinarySequence=sequence;
        OrdinaryCutReceipt(cut,"host-published");
    }
    g_ordinaryBirths=std::move(births);g_ordinaryGeneration=generation;g_ordinaryLoad=census.load;g_ordinaryTransition=census.transition;
    return true;
}
void TryReceiveOrdinaryLifecycle(const PopulationCut& cut,const WorldScope& source) {
    if(!OrdinaryLifecycleRoom() || CurrentRole()!=Role::Client || cut.epoch!=g_host.epoch ||
       source.kind!=WorldSourceKind::Native || !source.hostSourceSerial ||
       source.sourceConnectionId!=g_bridge.ConnectionId(0) || source.sourceDeliverySerial!=g_bridge.PeerDeliverySerial(0) ||
       source.targetConnectionId!=g_bridge.ConnectionId(g_bridge.LocalSlot()) || source.targetDeliverySerial!=g_bridge.DeliverySerial())return;
    if(g_ordinaryClientHistory.Current() && (!OrdinaryCutCurrent() || !g_ordinarySource ||
       g_ordinarySource->sessionId!=source.sessionId || source.hostSourceSerial<=g_ordinarySource->hostSourceSerial))return;
    auto history=g_ordinaryClientHistory;
    if(!history.Admit(cut)){OrdinaryCutReceipt(cut,"client-refused-history");return;}
    for(const auto& row:cut.entries) {
        const auto host=g_host.enemies.find(row.netId);
        if(host==g_host.enemies.end() || host->second.objectId!=row.objectId)return;
    }
    const auto bytes=encode(cut);
    // Product bookends must exist with the optional observer OFF as well.
    // CausalScope deliberately returns unavailable when it is not requested.
    ClientClaimScope productBefore,productAfter;
    if(!ReadClientClaimScope(productBefore) || !g_host.ackSent)return;
    const auto salt=g_bridge.SpawnPickSalt();
    std::array<std::uint64_t,3> deliveries{};
    for(unsigned i=0;i<3;++i)deliveries[i]=g_bridge.PeerDeliverySerial(static_cast<std::uint8_t>(i));
    const auto before=CausalScope();const auto census=CaptureNativeCensus();
    if(!CensusMatchesInstance(census) || !SafeNativeGameplay())return;
    if(!RefreshOrdinaryPresence(census)){OrdinaryCutReceipt(cut,"client-refused-native-history");return;}
    RecordCatalog catalogBefore,catalogAfter;
    if(!FreshRecordCatalog(census,catalogBefore))return;
    struct Retiring {std::size_t index;const PopulationOccurrence* row;combatcausal::Target target;bool absent;};
    std::vector<Retiring> retiring;
    for(const auto& row:cut.entries)if(row.removed) {
        const auto* prior=g_ordinaryClientHistory.Current()?ordinarylifecycle::Find(*g_ordinaryClientHistory.Current(),row.netId):nullptr;
        if(prior && !prior->terminal)for(auto& spawn:g_inst.spawns)if(spawn.ordinaryBinding.highWater==row.netId) {
            const bool absent=!CensusHasActor(census,spawn.actor);
            combatcausal::Target target;
            if(absent) {
                const auto original=std::find_if(g_ordinaryPresence.begin(),g_ordinaryPresence.end(),[&](const auto& presence) {
                    return presence.target.netId==row.netId && presence.pending && OrdinaryPresenceCurrent(presence) &&
                        PopulationSpawnRoots(presence.anchor.original)==PopulationSpawnRoots(spawn) &&
                        presence.anchor.occurrence.birthSequence==row.birthSequence &&
                        presence.anchor.occurrence.record==row.record &&
                        population_cut_detail::sameDefinition(presence.anchor.definition,cut.definitions[row.definition]);
                });
                if(original==g_ordinaryPresence.end()){OrdinaryCutReceipt(cut,"client-refused-absence-without-binding");return;}
                target=original->target; // last checked positive HP, not a read of a disposed actor
            } else {
                const auto* native=FindNativeEnemy(census,spawn);
                if(!native){OrdinaryCutReceipt(cut,"client-refused-old-body");return;}
                target=OrdinaryHistoricalTarget(spawn,*native,row.netId);
            }
            if(!combatcausal::SameTarget(target,target) || target.hp<=0){OrdinaryCutReceipt(cut,"client-refused-old-identity");return;}
            unsigned matches=0;
            for(std::uint32_t i=0;i<catalogBefore.entryCount;++i) {
                const auto& entry=catalogBefore.entries[i];
                if(population_cut_detail::sameDefinition(entry.content,cut.definitions[row.definition])) {
                    if(entry.controller!=spawn.controller || entry.spawnArray+64*row.record!=spawn.record)return;
                    ++matches;
                }
            }
            if(matches!=1)return;
            retiring.push_back({static_cast<std::size_t>(&spawn-g_inst.spawns.data()),&row,target,absent});
        }
    }
    if(!FreshRecordCatalog(census,catalogAfter) || !SameRecordCatalogSample(catalogBefore,catalogAfter) ||
       !ReadClientClaimScope(productAfter) || !SameClientClaimScope(productBefore,productAfter) ||
       !g_host.ackSent || !salt || salt!=g_bridge.SpawnPickSalt() || !CensusMatchesInstance(census))return;
    for(unsigned i=0;i<3;++i)if(deliveries[i]!=g_bridge.PeerDeliverySerial(static_cast<std::uint8_t>(i)))return;
    for(const auto& pending:retiring)if(pending.absent) {
        const auto bookend=CaptureNativeCensus(pending.target.actor);
        if(!CensusMatchesInstance(bookend) || bookend.watchedActorPresent || !OrdinaryCutCurrent()) {
            OrdinaryCutReceipt(cut,"client-refused-absence-bookend");return;
        }
    }
    if(g_ordinaryRetirements.size()+retiring.size()>POPULATION_MAX_ENTRIES)return;
    for(const auto& pending:retiring) {
        const auto& row=*pending.row;
        // Observer loss still hard-refuses the diagnostic, never the already
        // authenticated product membership transition or subsequent HP cache.
        if(combatcausal::Requested())(void)combatcausal::engine.RetireMember(before,row.netId,row.birthSequence,
            row.terminalSequence,pending.target,bytes.data(),static_cast<unsigned>(bytes.size()),combatcausal::Now());
    }
    // Reserve bounded tickets before committing any source membership mutation.
    for(const auto& pending:retiring) {
        const auto& row=*pending.row;
        g_ordinaryRetirements.push_back({g_inst.spawns[pending.index],row,cut.definitions[row.definition],source,
            WorldSessionGeneration(),census.load,census.transition,g_hitTraceFrame});
        if(pending.absent) {
            auto& ticket=g_ordinaryRetirements.back();ticket.completed=true;ticket.outcome="complete-native-absence-before-cut";
            if(g_log)g_log("[ordinary-lifecycle] native-retire netId=%u actor=%llX frame=%u outcome=%s killCredit=0 nativeWrite=0",
                row.netId,(unsigned long long)ticket.original.actor,g_hitTraceFrame,ticket.outcome);
        }
        for(auto& presence:g_ordinaryPresence)if(presence.target.netId==row.netId)presence.finished=true;
    }
    for(const auto& pending:retiring) {
        auto& spawn=g_inst.spawns[pending.index];spawn.ordinaryBinding.removed=true;
        spawn.ordinaryBinding.bound=0;spawn.netId=-1;spawn.lifecycleRetired=true;
    }
    for(const auto& row:cut.entries)if(row.removed){g_host.enemies.at(row.netId).removed=true;g_mirror.Erase(row.netId);}
    g_ordinaryClientHistory=std::move(history);g_ordinarySource=source;
    g_ordinaryGeneration=WorldSessionGeneration();g_ordinaryLoad=warp::LoadSerial();g_ordinaryTransition=warp::TransitionSerial();
    AdvanceClientManifestRevision();InvalidateClientClaims("ordinary-membership-retired");
    OrdinaryCutReceipt(cut,"client-admitted");
}
bool PendingOrdinarySourceCurrent(const PendingOrdinaryCut& pending) {
    return g_role==Role::Client && pending.generation==WorldSessionGeneration() &&
        pending.cut.epoch==g_host.epoch && g_lastRoomPacket &&
        pending.cut.location==PopulationLocation(*g_lastRoomPacket) &&
        pending.source.sourceConnectionId==g_bridge.ConnectionId(0) &&
        pending.source.sourceDeliverySerial==g_bridge.PeerDeliverySerial(0) &&
        pending.source.targetConnectionId==g_bridge.ConnectionId(g_bridge.LocalSlot()) &&
        pending.source.targetDeliverySerial==g_bridge.DeliverySerial() &&
        (!pending.arrived || (pending.load==warp::LoadSerial() && pending.transition==warp::TransitionSerial())) &&
        GetTickCount64()>=pending.receivedMs && GetTickCount64()-pending.receivedMs<=30000;
}
void RetryOrdinaryLifecycle() {
    while(!g_pendingOrdinaryCuts.empty()) {
        // Role/scope readers may retire session state. Never retain a reference
        // across those readers or the native admission bookends.
        const auto pending=g_pendingOrdinaryCuts.front();
        if(!PendingOrdinarySourceCurrent(pending)) {
            OrdinaryCutReceipt(pending.cut,"client-pending-expired-or-superseded");
            ClearPendingOrdinaryCuts();return;
        }
        TryReceiveOrdinaryLifecycle(pending.cut,pending.source);
        if(g_pendingOrdinaryCuts.empty())return;
        if(!g_ordinaryClientHistory.Current() || !g_ordinarySource ||
           g_ordinaryClientHistory.Current()->sequence!=pending.cut.sequence ||
           g_ordinarySource->hostSourceSerial!=pending.source.hostSourceSerial)return;
        g_pendingOrdinaryCuts.erase(g_pendingOrdinaryCuts.begin());
    }
}
void ReceiveOrdinaryLifecycle(const PopulationCut& cut,const WorldScope& source) {
    const auto role=CurrentRole();const auto generation=WorldSessionGeneration();
    if(g_populationRequested || role!=Role::Client || !generation || !cut.membershipOnly ||
       cut.epoch!=g_host.epoch || !g_lastRoomPacket || cut.location!=PopulationLocation(*g_lastRoomPacket) ||
       source.sessionId.empty() || source.kind!=WorldSourceKind::Native || !source.hostSourceSerial ||
       source.sourceConnectionId!=g_bridge.ConnectionId(0) || source.sourceDeliverySerial!=g_bridge.PeerDeliverySerial(0) ||
       source.targetConnectionId!=g_bridge.ConnectionId(g_bridge.LocalSlot()) || source.targetDeliverySerial!=g_bridge.DeliverySerial())return;
    try {ValidatePopulationCut(cut);}catch(const std::exception&) {return;}
    if(g_ordinarySource && (g_ordinarySource->sessionId!=source.sessionId ||
       source.hostSourceSerial<=g_ordinarySource->hostSourceSerial))return;
    // Validate monotonic cumulative history before reserving bounded storage.
    auto history=g_ordinaryClientHistory;
    for(const auto& queued:g_pendingOrdinaryCuts) {
        if(queued.source.sessionId!=source.sessionId || queued.generation!=generation ||
           queued.source.sourceConnectionId!=source.sourceConnectionId ||
           queued.source.sourceDeliverySerial!=source.sourceDeliverySerial ||
           queued.source.targetConnectionId!=source.targetConnectionId ||
           queued.source.targetDeliverySerial!=source.targetDeliverySerial)return;
        if(!history.Admit(queued.cut))return;
    }
    if(!history.Admit(cut))return; // duplicates do not renew the original deadline
    if(!g_pendingOrdinaryCuts.empty() && source.hostSourceSerial<=g_pendingOrdinaryCuts.back().source.hostSourceSerial)return;
    if(g_pendingOrdinaryCuts.size()>=16) {
        OrdinaryCutReceipt(cut,"client-pending-capacity-refused");return;
    }
    g_pendingOrdinaryCuts.push_back({cut,source,generation,warp::LoadSerial(),warp::TransitionSerial(),GetTickCount64(),g_host.arrived});
    OrdinaryCutReceipt(cut,"client-pending-authenticated");
    RetryOrdinaryLifecycle();
}
// A request is not completion. Only a later complete, current census can prove
// absence; until then the original living body blocks replacement/release.
void OrdinaryRetirementOutcome(OrdinaryRetirement& ticket,const char* outcome) {
    if(std::strcmp(ticket.outcome,outcome)==0)return;
    ticket.outcome=outcome;
    if(g_log)g_log("[ordinary-lifecycle] native-retire netId=%u birth=%llu retire=%llu actor=%llX frame=%u outcome=%s killCredit=0 roots=%llX/%llX/%llX/%llX generation=%u load=%u transition=%u source=%llu/%llu/%llu target=%llu/%llu",
        ticket.occurrence.netId,(unsigned long long)ticket.occurrence.birthSequence,
        (unsigned long long)ticket.occurrence.terminalSequence,(unsigned long long)ticket.original.actor,g_hitTraceFrame,outcome,
        (unsigned long long)ticket.original.objentry,(unsigned long long)ticket.original.status,
        (unsigned long long)ticket.original.controller,(unsigned long long)ticket.original.record,
        ticket.generation,ticket.load,ticket.transition,(unsigned long long)ticket.source.sourceConnectionId,
        (unsigned long long)ticket.source.sourceDeliverySerial,(unsigned long long)ticket.source.hostSourceSerial,
        (unsigned long long)ticket.source.targetConnectionId,(unsigned long long)ticket.source.targetDeliverySerial);
}
bool OrdinaryRetirementCurrent(const OrdinaryRetirement& ticket) {
    if(!OrdinaryCutCurrent() || g_role!=Role::Client || ticket.generation!=WorldSessionGeneration() ||
       ticket.load!=warp::LoadSerial() || ticket.transition!=warp::TransitionSerial() ||
       !g_ordinarySource || ticket.source.sessionId!=g_ordinarySource->sessionId ||
       ticket.source.sourceConnectionId!=g_ordinarySource->sourceConnectionId ||
       ticket.source.sourceDeliverySerial!=g_ordinarySource->sourceDeliverySerial ||
       ticket.source.targetConnectionId!=g_ordinarySource->targetConnectionId ||
       ticket.source.targetDeliverySerial!=g_ordinarySource->targetDeliverySerial ||
       ticket.source.hostSourceSerial>g_ordinarySource->hostSourceSerial || g_resyncWriteFence!=ResyncWriteFence::None)return false;
    const auto* row=ordinarylifecycle::Find(*OrdinaryCut(),ticket.occurrence.netId);
    return row && row->removed && row->terminal && row->birthSequence==ticket.occurrence.birthSequence &&
        row->terminalSequence==ticket.occurrence.terminalSequence && row->record==ticket.occurrence.record &&
        population_cut_detail::sameDefinition(OrdinaryCut()->definitions[row->definition],ticket.definition);
}
bool OrdinaryRetirementBody(const OrdinaryRetirement& ticket,const NativeCensus& census,NativeEnemy& native) {
    if(!CensusMatchesInstance(census) || !OrdinaryRetirementCurrent(ticket))return false;
    const auto it=g_inst.byActor.find(ticket.original.actor);
    if(it==g_inst.byActor.end() || it->second>=g_inst.spawns.size())return false;
    const auto& tracked=g_inst.spawns[it->second];
    if(!tracked.present || !tracked.lifecycleRetired || !tracked.ordinaryBinding.removed || tracked.netId>0 ||
       tracked.ordinaryBinding.highWater!=ticket.occurrence.netId ||
       PopulationSpawnRoots(tracked)!=PopulationSpawnRoots(ticket.original))return false;
    const auto* found=FindNativeEnemy(census,tracked);
    if(!found || found->hp<=0 || found->objectId!=302)return false;
    native=*found;
    const auto target=OrdinaryHistoricalTarget(ticket.original,native,ticket.occurrence.netId);
    if(!combatcausal::SameTarget(target,target))return false;
    RecordCatalog first,last;spawncontroller::NativeRecordMembership member;
    if(!FreshRecordCatalog(census,first) || !RecordMember(first,census,native.actor,member) || member.issues ||
       member.tableIndex>=first.entryCount || member.recordIndex!=ticket.occurrence.record ||
       member.controller[0]!=ticket.original.controller || member.controller[1]!=ticket.original.controller ||
       member.record[0]!=ticket.original.record || member.record[1]!=ticket.original.record ||
       !population_cut_detail::sameDefinition(first.entries[member.tableIndex].content,ticket.definition))return false;
    unsigned aliases=0;
    for(std::uint32_t entry=0;entry<first.entryCount;++entry)
        if(population_cut_detail::sameDefinition(first.entries[entry].content,ticket.definition))++aliases;
    return aliases==1 && FreshRecordCatalog(census,last) && SameRecordCatalogSample(first,last) &&
        CensusMatchesInstance(census) && OrdinaryRetirementCurrent(ticket);
}
bool ConsumeOrdinaryRetirements(std::uint32_t frame,NativeCensus& census) {
    for(auto& ticket:g_ordinaryRetirements) {
        if(ticket.failed)return false; // poisoned original retirement holds all downstream release
        if(ticket.completed)continue;
        if(!OrdinaryRetirementCurrent(ticket)) {
            ticket.failed=true;OrdinaryRetirementOutcome(ticket,"refuse-original-scope");return false;
        }
        if(!CensusMatchesInstance(census))return false;
        if(frame<ticket.frame || frame-ticket.frame>120) {
            ticket.failed=true;OrdinaryRetirementOutcome(ticket,"refuse-native-removal-timeout");return false;
        }
        if(!CensusHasActor(census,ticket.original.actor)) {
            const auto bookend=CaptureNativeCensus(ticket.original.actor);
            if(!CensusMatchesInstance(bookend) || bookend.watchedActorPresent || !OrdinaryRetirementCurrent(ticket))return false;
            ticket.completed=true;OrdinaryRetirementOutcome(ticket,"complete-native-absence");continue;
        }
        NativeEnemy expected;
        if(!OrdinaryRetirementBody(ticket,census,expected)) {
            ticket.failed=true;OrdinaryRetirementOutcome(ticket,"refuse-original-body");return false;
        }
        if(ticket.attempted) {OrdinaryRetirementOutcome(ticket,"waiting-native-absence");continue;}
        if(!spawncontroller::IsDiagnosticGameThread()) {
            OrdinaryRetirementOutcome(ticket,"refuse-owner-thread");continue;
        }
        // Independent final complete census/record/source proof immediately before
        // the native leave operation. Poison once, including fault/unknown results.
        const auto finalCensus=CaptureNativeCensus(ticket.original.actor);NativeEnemy finalNative;
        if(!OrdinaryRetirementBody(ticket,finalCensus,finalNative) || !SameNativeIdentity(expected,finalNative) ||
           finalNative.hp!=expected.hp) {ticket.failed=true;OrdinaryRetirementOutcome(ticket,"refuse-dispatch-bookend");return false;}
        ticket.attempted=true;ticket.attemptFrame=frame;
        auto dispatched=ticket; // native code must not leave a reference into resettable state
        const bool returned=g_ordinaryLeave(dispatched.original,expected.hp);
        const auto live=std::find_if(g_ordinaryRetirements.begin(),g_ordinaryRetirements.end(),[&](const auto& row) {
            return row.occurrence.netId==dispatched.occurrence.netId &&
                row.occurrence.birthSequence==dispatched.occurrence.birthSequence &&
                row.source.sessionId==dispatched.source.sessionId && PopulationSpawnRoots(row.original)==PopulationSpawnRoots(dispatched.original);
        });
        if(live!=g_ordinaryRetirements.end()) {
            live->failed=!returned;
            OrdinaryRetirementOutcome(*live,returned?"requested-native-leave":"refuse-native-leaf-unavailable-or-fault");
        } else OrdinaryRetirementOutcome(dispatched,"refuse-state-retired-during-leaf");
        // No pre-call census may authorize a store, hash, claim or release.
        census=CaptureNativeCensus();return false;
    }
    return true;
}
// Source certificate matches local record CONTENT, not native pointers or point.
int OrdinaryCertifiedId(const NativeCensus& census,const NativeEnemy& native,const RecordCatalog& catalog) {
    if(!OrdinaryCutCurrent())return 0;
    spawncontroller::NativeRecordMembership member;
    if(!RecordMember(catalog,census,native.actor,member) || member.issues)return 0;
    const auto& cut=*OrdinaryCut();int selected=0;
    for(const auto& row:cut.entries) {
        if(row.removed || row.objectId!=native.objectId || row.record!=member.recordIndex ||
           !population_cut_detail::sameDefinition(cut.definitions[row.definition],catalog.entries[member.tableIndex].content))continue;
        // A previously bound local body cannot be reused for this or another ID.
        for(const auto& old:g_inst.spawns)if(old.ordinaryBinding.highWater==row.netId && old.actor!=native.actor)return 0;
        // A poisoned/unknown request never manufactures completion, even if a
        // later unrelated removal eventually makes the old body disappear.
        for(const auto& pending:g_ordinaryRetirements)
            if(!pending.completed && pending.original.controller==member.controller[0] && pending.original.record==member.record[0])return 0;
        // Retired live bodies must disappear natively before a replacement binds.
        for(const auto& old:g_inst.spawns)if(old.lifecycleRetired && old.present && old.controller==member.controller[0] && old.record==member.record[0])return 0;
        if(selected)return 0;selected=row.netId;
    }
    return selected;
}
void BindOrdinaryCausalBirths(const NativeCensus& census) {
    if(!combatcausal::Requested() || !combatcausal::engine.Admitted() || !OrdinaryCutCurrent())return;
    const auto& cut=*OrdinaryCut();const auto bytes=encode(cut);
    for(const auto& native:census.enemies) {
        const auto it=g_inst.byActor.find(native.actor);if(it==g_inst.byActor.end())continue;
        const auto& spawn=g_inst.spawns[it->second];
        const auto id=g_role==Role::Host?unsigned(spawn.spawnIndex+1):unsigned(spawn.netId>0?spawn.netId:0);
        const auto* row=ordinarylifecycle::Find(cut,id);
        if(row && !row->terminal) {
            const auto scope=CausalScope();const auto target=CausalTarget(spawn,native,id);
            if(!combatcausal::engine.BirthBound(scope,target,row->birthSequence))
                combatcausal::engine.BindBirth(scope,target,row->birthSequence,bytes.data(),static_cast<unsigned>(bytes.size()),combatcausal::Now());
        }
    }
}
