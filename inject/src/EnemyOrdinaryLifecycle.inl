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
void ResetOrdinaryLifecycle() {
    g_ordinaryBirths.clear();g_ordinaryHostHistory.Reset();g_ordinaryClientHistory.Reset();
    g_ordinarySequence=0;g_ordinaryGeneration=g_ordinaryLoad=g_ordinaryTransition=0;g_ordinarySource.reset();
}
bool OrdinaryLifecycleRoom() {
    return g_inst.live && g_inst.world==5 && g_inst.room==6 && g_inst.door==0 &&
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
bool PublishOrdinaryLifecycle(const NativeCensus& census) {
    if(!OrdinaryLifecycleRoom())return true;
    if(g_populationRequested || g_role!=Role::Host || !CensusMatchesInstance(census) || !SafeNativeGameplay())return false;
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
                if(!combatcausal::engine.RetireMember(CausalScope(),b.occurrence.netId,b.occurrence.birthSequence,
                    b.occurrence.terminalSequence,b.target,bytes.data(),static_cast<unsigned>(bytes.size()),combatcausal::Now()))return false;
            for(auto& s:g_inst.spawns)if(s.spawnIndex+1==b.occurrence.netId)s.lifecycleRetired=true;
        }
        g_ordinaryHostHistory=std::move(history);g_ordinarySequence=sequence;
        OrdinaryCutReceipt(cut,"host-published");
    }
    g_ordinaryBirths=std::move(births);g_ordinaryGeneration=generation;g_ordinaryLoad=census.load;g_ordinaryTransition=census.transition;
    return true;
}
void ReceiveOrdinaryLifecycle(const PopulationCut& cut,const WorldScope& source) {
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
    const auto before=CausalScope();const auto census=CaptureNativeCensus();
    if(!CensusMatchesInstance(census) || !SafeNativeGameplay())return;
    RecordCatalog catalogBefore,catalogAfter;
    if(!FreshRecordCatalog(census,catalogBefore))return;
    struct Retiring {std::size_t index;const PopulationOccurrence* row;combatcausal::Target target;};
    std::vector<Retiring> retiring;
    for(const auto& row:cut.entries)if(row.removed) {
        const auto* prior=g_ordinaryClientHistory.Current()?ordinarylifecycle::Find(*g_ordinaryClientHistory.Current(),row.netId):nullptr;
        if(prior && !prior->terminal)for(auto& spawn:g_inst.spawns)if(spawn.ordinaryBinding.highWater==row.netId) {
            NativeEnemy native;bool enemy=false;
            if(!ReadNativeEnemy(spawn.actor,native,enemy) || !enemy)return;
            const auto target=OrdinaryHistoricalTarget(spawn,native,row.netId);
            if(!combatcausal::SameTarget(target,target))return;
            unsigned matches=0;
            for(std::uint32_t i=0;i<catalogBefore.entryCount;++i) {
                const auto& entry=catalogBefore.entries[i];
                if(population_cut_detail::sameDefinition(entry.content,cut.definitions[row.definition])) {
                    if(entry.controller!=spawn.controller || entry.spawnArray+64*row.record!=spawn.record)return;
                    ++matches;
                }
            }
            if(matches!=1)return;
            retiring.push_back({static_cast<std::size_t>(&spawn-g_inst.spawns.data()),&row,target});
        }
    }
    if(!FreshRecordCatalog(census,catalogAfter) || !SameRecordCatalogSample(catalogBefore,catalogAfter) ||
       !combatcausal::SameScope(before,CausalScope()) || !CensusMatchesInstance(census))return;
    for(const auto& pending:retiring) {
        const auto& row=*pending.row;
        if(combatcausal::Requested() && !combatcausal::engine.RetireMember(before,row.netId,row.birthSequence,
            row.terminalSequence,pending.target,bytes.data(),static_cast<unsigned>(bytes.size()),combatcausal::Now()))return;
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
