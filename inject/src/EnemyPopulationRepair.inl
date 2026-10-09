// Owner-thread population authority. Included after the checked native catalog helpers.
// Historical host certificates contain no client pointer; exact terminal tickets authorize native death only.
struct PopulationBirthCertificate {
    PopulationOccurrence occurrence;
    NativeRecordContentDefinition definition;
    std::array<uintptr_t,5> roots{}; // host-local announced incarnation, never transported
};
struct PopulationDisposalTicket {
    std::array<uintptr_t,5> roots{};
    std::uint16_t netId{};
    std::uint64_t cut{},manifest{},terminalSequence{};
    std::uint32_t generation{},load{},transition{},authorizedFrame{},dispatchedFrame{};
    std::uint64_t delivery{};
    NativeRecordLocation location{};
    bool dispatched{};
    std::uint64_t id{},birthSequence{};
    WorldScope source{};
    std::uint32_t epoch{};
    NativeRecordContentDefinition definition;
    std::uint16_t ordinal{};
    const char* lastOutcome="";
    const char* lastReason="";
    std::uint32_t outcomeFirst{},outcomeLast{};
    std::uint64_t outcomeRepeats{};
    float admissionBefore{};
    std::uint32_t controllerCountBefore{};
    bool budgetBeforeAvailable{},countBeforeAvailable{};
};
std::vector<PopulationBirthCertificate> g_populationBirths;
std::uint64_t g_populationSourceSequence=0,g_populationPublishedSequence=0,g_populationClientFloor=0;
std::uint32_t g_populationHostLoad=0,g_populationHostTransition=0,g_populationHostGeneration=0;
std::array<float,4> g_populationObservedPoint{};
std::uint32_t g_populationPointFrame=0,g_populationPointLoad=0,g_populationPointTransition=0;
NativeRecordLocation g_populationPointLocation{};
bool g_populationPointAvailable=false;
std::optional<PopulationCut> g_populationCut;
std::optional<WorldScope> g_populationCutScope;
std::vector<PopulationDisposalTicket> g_populationDisposals;
// Process-lifetime poison, deliberately independent of ticket/cut/session cancellation.
// Native load/transition and all five roots identify the supported local incarnation.
struct PopulationDeathAttempt {
    std::array<uintptr_t,5> roots{};
    std::uint32_t load{},transition{},frame{};
};
std::vector<PopulationDeathAttempt> g_populationDeathAttempts;
bool g_populationConsumerActive=false;
std::uint64_t g_populationTicketSequence=0,g_populationOutcomeSequence=0,g_populationOutcomeLoss=0;
constexpr std::uint64_t POPULATION_OUTCOME_CAP=8192;
void CancelPopulationDisposals(const char* reason,std::uint32_t frame);
void SealPopulationDisposals(const char* reason,std::uint32_t frame);
std::vector<std::size_t> TrackSpawns(const NativeCensus& census);
std::uint32_t g_populationInstallFrame=0,g_populationInstallLoad=0,g_populationInstallTransition=0;
std::uint32_t g_populationInstallGeneration=0,g_populationLeaseTicks=0;
std::uint64_t g_populationInstallManifest=0;
bool g_populationInstalled=false;
struct PopulationLeasePoint {
    uintptr_t controller{};
    NativeRecordContentDefinition definition;
    std::array<float,4> point{};
};
std::vector<PopulationLeasePoint> g_populationLeasePoints;

NativeRecordLocation PopulationLocation(const RoomTransition& room) {
    return {room.worldId,room.roomId,room.door,room.mapProgram,room.battleProgram,room.eventProgram};
}
void ResetPopulationRepair() {
    g_populationBirths.clear();g_populationPublishedSequence=0;
    g_populationHostLoad=0;g_populationHostTransition=0;g_populationHostGeneration=0;
    g_populationPointAvailable=false;
    g_populationCut.reset();g_populationCutScope.reset();g_populationClientFloor=0;
    CancelPopulationDisposals("reset",g_mirrorFrame);g_populationLeasePoints.clear();g_populationInstalled=false;
}
bool PopulationNextSequence(std::uint64_t& value) {
    if(g_populationSourceSequence==UINT64_MAX)return false;
    value=++g_populationSourceSequence;return true;
}
void ObservePopulationActivation(const std::array<float,4>& point) {
    if(!g_populationRequested || point[3]!=1.0f)return;
    RoomTransition room;
    if(!ReadLocationChecked(room))return;
    g_populationObservedPoint=point;g_populationPointFrame=g_mirrorFrame;
    g_populationPointLoad=warp::LoadSerial();g_populationPointTransition=warp::TransitionSerial();
    g_populationPointLocation=PopulationLocation(room);
    g_populationPointAvailable=true;
}
std::array<uintptr_t,5> PopulationSpawnRoots(const Spawn& spawn) {
    return {spawn.actor,spawn.objentry,spawn.status,spawn.controller,spawn.record};
}
bool PopulationBirthScopeCurrent(const NativeCensus& census,std::uint32_t expectedGeneration) {
    RoomTransition room;
    return spawncontroller::IsDiagnosticGameThread() && g_role==Role::Host && CurrentRole()==Role::Host &&
        g_inst.live && SafeNativeGameplay() && CensusMatchesInstance(census) &&
        expectedGeneration==WorldSessionGeneration() && census.load==warp::LoadSerial() &&
        census.transition==warp::TransitionSerial() && ReadLocationChecked(room) &&
        PopulationLocation(room)==PopulationLocation(census.location);
}
bool PopulationBirthRootsCurrent(const Spawn& spawn,const std::array<uintptr_t,5>& roots) {
    if(!spawn.announced || !spawn.identityRead || PopulationSpawnRoots(spawn)!=roots ||
        std::any_of(roots.begin(),roots.end(),[](auto root){return root==0;}))return false;
    const auto current=ReadPopulationIdentity(roots[0]);
    uintptr_t controller=0,record=0;
    return ReadNative(roots[0]+0x9E8,controller) && ReadNative(roots[0]+0x9F0,record) &&
        controller==roots[3] && record==roots[4] && current.actor==roots[0] &&
        current.objentry==roots[1] && current.status==roots[2] &&
        spawn.announced && spawn.identityRead && PopulationSpawnRoots(spawn)==roots;
}
void CapturePopulationBirths(const NativeCensus& census) {
    const auto birthGeneration=WorldSessionGeneration();
    if(!g_populationRequested || !PopulationBirthScopeCurrent(census,birthGeneration))return;
    if(g_populationHostLoad!=census.load || g_populationHostTransition!=census.transition ||
        g_populationHostGeneration!=WorldSessionGeneration()) {
        g_populationBirths.clear();g_populationPublishedSequence=0;
        g_populationHostLoad=census.load;g_populationHostTransition=census.transition;
        g_populationHostGeneration=WorldSessionGeneration();
    }
    RecordCatalog before;
    if(!FreshRecordCatalog(census,before))return;
    for(const auto& spawn:g_inst.spawns) {
        const auto id=static_cast<std::uint16_t>(spawn.spawnIndex+1);
        if(std::any_of(g_populationBirths.begin(),g_populationBirths.end(),[&](const auto& c){return c.occurrence.netId==id;}))continue;
        const auto* native=FindNativeEnemy(census,spawn);
        const auto roots=PopulationSpawnRoots(spawn);
        if(!spawn.present || spawn.deathSent || !native || native->hp<=0 || native->objectId!=spawn.objectId ||
            !spawn.populationPointCaptured || !PopulationBirthRootsCurrent(spawn,roots))continue;
        spawncontroller::NativeRecordMembership member;
        if(!RecordMember(before,census,spawn.actor,member) || member.actor!=roots[0] ||
            member.controller[0]!=roots[3] || member.controller[1]!=roots[3] ||
            member.record[0]!=roots[4] || member.record[1]!=roots[4] || member.tableIndex>=before.entryCount ||
            member.recordIndex>=before.entries[member.tableIndex].content.records.size())continue;
        const auto& definition=before.entries[member.tableIndex].content;
        if(RecordObjectId(definition.records[member.recordIndex])!=spawn.objectId)continue;
        unsigned aliases=0;
        for(std::uint32_t i=0;i<before.entryCount;++i)
            if(population_cut_detail::sameDefinition(before.entries[i].content,definition))++aliases;
        if(aliases!=1)continue;
        RecordCatalog after;
        if(!FreshRecordCatalog(census,after) || !SameRecordCatalogSample(before,after))return;
        spawncontroller::NativeRecordMembership finalMember;
        if(!RecordMember(after,census,roots[0],finalMember) || finalMember.actor!=roots[0] ||
            finalMember.tableIndex>=after.entryCount || finalMember.tableIndex!=member.tableIndex ||
            finalMember.recordIndex!=member.recordIndex ||
            finalMember.controller[0]!=roots[3] || finalMember.controller[1]!=roots[3] ||
            finalMember.record[0]!=roots[4] || finalMember.record[1]!=roots[4] ||
            !population_cut_detail::sameDefinition(definition,after.entries[finalMember.tableIndex].content))continue;
        if(!PopulationBirthRootsCurrent(spawn,roots) || spawn.deathSent ||
            native->objectId!=spawn.objectId || !PopulationBirthScopeCurrent(census,birthGeneration))continue;
        if(g_populationBirths.size()>=POPULATION_MAX_ENTRIES)return;
        PopulationBirthCertificate certificate;
        certificate.definition=definition;certificate.roots=roots;
        auto& occurrence=certificate.occurrence;
        occurrence.netId=id;occurrence.objectId=spawn.objectId;occurrence.record=member.recordIndex;
        if(!PopulationNextSequence(occurrence.birthSequence))return;
        occurrence.occurrence=occurrence.birthSequence;
        occurrence.activationPoint=spawn.populationBirthPoint;
        g_populationBirths.push_back(std::move(certificate));
    }
}
void PublishPopulationCut(const NativeCensus& census) {
    if(!g_populationRequested || !g_manifestSent || !PopulationBirthScopeCurrent(census,g_populationHostGeneration) ||
        census.load!=g_populationHostLoad || census.transition!=g_populationHostTransition)return;
    PopulationCut cut;cut.epoch=g_epoch;cut.hostLoad=census.load;cut.hostTransition=census.transition;
    cut.location=PopulationLocation(census.location);
    for(const auto& spawn:g_inst.spawns) {
        if(!spawn.announced)return;
        auto found=std::find_if(g_populationBirths.begin(),g_populationBirths.end(),[&](const auto& c){return c.occurrence.netId==spawn.spawnIndex+1;});
        if(found==g_populationBirths.end())return; // unavailable is never a guessed tombstone
        if(!spawn.identityRead || PopulationSpawnRoots(spawn)!=found->roots ||
            spawn.objectId!=found->occurrence.objectId)return;
        // A living publication still belongs to this announced five-root incarnation.
        if(!spawn.deathSent && (!spawn.present || !PopulationBirthRootsCurrent(spawn,found->roots)))return;
        if(spawn.deathSent && !found->occurrence.terminal) {
            if(!PopulationNextSequence(found->occurrence.terminalSequence))return;
            found->occurrence.terminal=true;
        }
        auto entry=found->occurrence;
        auto definition=std::find_if(cut.definitions.begin(),cut.definitions.end(),[&](const auto& d){return population_cut_detail::sameDefinition(d,found->definition);});
        if(definition==cut.definitions.end()) {
            entry.definition=static_cast<std::uint16_t>(cut.definitions.size());cut.definitions.push_back(found->definition);
        } else entry.definition=static_cast<std::uint16_t>(definition-cut.definitions.begin());
        cut.entries.push_back(entry);
    }
    cut.sequence=g_populationSourceSequence;
    if(cut.sequence==g_populationPublishedSequence)return;
    try {const auto bytes=encode(cut);
        if(PopulationBirthScopeCurrent(census,g_populationHostGeneration) && Send(bytes))g_populationPublishedSequence=cut.sequence;}
    catch(const std::exception&) { /* unsupported/bounded cut cannot create authority */ }
}
bool PopulationCutCoversHost(const PopulationCut& cut) {
    if(!g_host.manifestComplete || cut.epoch!=g_host.epoch || cut.entries.size()!=g_host.enemies.size())return false;
    for(const auto& e:cut.entries) {
        const auto h=g_host.enemies.find(e.netId);
        if(h==g_host.enemies.end() || h->second.objectId!=e.objectId || h->second.dead!=e.terminal)return false;
    }
    return true;
}
void ReceivePopulationCut(const PopulationCut& cut,const WorldScope& scope) {
    if(!g_populationRequested || CurrentRole()!=Role::Client || cut.sequence<=g_populationClientFloor ||
        !g_lastRoomPacket || cut.location!=PopulationLocation(*g_lastRoomPacket) ||
        !PopulationCutCoversHost(cut))return;
    g_populationCut=cut;g_populationCutScope=scope;g_populationClientFloor=cut.sequence;
    g_populationInstalled=false;CancelPopulationDisposals("new-cut",g_mirrorFrame);g_populationLeasePoints.clear();
}
bool PopulationCutCurrent() {
    // Role/generation reads may retire the session and clear authority containers.
    const auto role=CurrentRole();const auto currentGeneration=WorldSessionGeneration();
    RoomTransition room;
    return g_populationRequested && g_populationCut && g_populationCutScope &&
        g_populationInstalled && g_role==Role::Client && g_resyncWriteFence==ResyncWriteFence::None &&
        role==Role::Client && currentGeneration!=0 && g_inst.live && SafeNativeGameplay() &&
        ReadLocationChecked(room) && g_host.arrived && warp::HostTransitionArrived(g_host.epoch) &&
        g_populationCut->location==PopulationLocation(room) && g_populationCut->epoch==g_host.epoch &&
        g_populationInstallLoad==warp::LoadSerial() && g_populationInstallTransition==warp::TransitionSerial() &&
        g_populationInstallGeneration==currentGeneration &&
        g_populationInstallManifest==g_clientManifestRevision &&
        g_populationCutScope->sourceConnectionId==g_bridge.ConnectionId(0) &&
        g_populationCutScope->sourceDeliverySerial==g_bridge.PeerDeliverySerial(0) &&
        g_populationCutScope->targetConnectionId==g_bridge.ConnectionId(g_bridge.LocalSlot()) &&
        g_populationCutScope->targetDeliverySerial==g_bridge.DeliverySerial() &&
        PopulationCutCoversHost(*g_populationCut);
}
std::string PopulationClientScopeJson() {
    std::ostringstream s;s<<"{\"session\":"<<PopulationJsonString(g_populationCutScope->sessionId)
        <<",\"generation\":"<<g_populationInstallGeneration<<",\"delivery\":"<<g_bridge.DeliverySerial()
        <<",\"epoch\":"<<g_populationCut->epoch<<",\"load\":"<<g_populationInstallLoad
        <<",\"transition\":"<<g_populationInstallTransition<<",\"location\":"<<PopulationLocationJson(g_populationCut->location)<<'}';return s.str();
}
const PopulationOccurrence* PopulationTerminalFor(const NativeRecordContentDefinition& definition,std::uint16_t record) {
    if(!g_populationCut)return nullptr;
    const PopulationOccurrence* newest=nullptr;
    for(const auto& e:g_populationCut->entries) {
        if(e.record!=record || !population_cut_detail::sameDefinition(g_populationCut->definitions[e.definition],definition))continue;
        if(!e.terminal)return nullptr;
        if(!newest || e.birthSequence>newest->birthSequence)newest=&e;
    }
    return newest;
}
struct PopulationCheck {
    const char* reason="ok";
    std::uint64_t expected{},observed{};
    bool available=true;
    explicit operator bool() const {return std::strcmp(reason,"ok")==0;}
};
PopulationCheck PopulationTicketScopeCheck(const PopulationDisposalTicket& t) {
    if(!spawncontroller::IsDiagnosticGameThread())return {"owner-thread",1,0};
    if(!g_populationRequested || g_role!=Role::Client || CurrentRole()!=Role::Client)return {"feature-role",1,0};
    const auto currentGeneration=WorldSessionGeneration();
    if(!g_populationCut || !g_populationCutScope || !g_populationInstalled)return {"installed-cut",1,0};
    if(t.generation!=currentGeneration)return {"generation",t.generation,currentGeneration};
    if(t.delivery!=g_bridge.DeliverySerial())return {"delivery",t.delivery,g_bridge.DeliverySerial()};
    if(t.source.sourceConnectionId!=g_bridge.ConnectionId(0))return {"host-connection",t.source.sourceConnectionId,g_bridge.ConnectionId(0)};
    if(t.source.sourceDeliverySerial!=g_bridge.PeerDeliverySerial(0))return {"host-delivery",t.source.sourceDeliverySerial,g_bridge.PeerDeliverySerial(0)};
    if(t.source.targetConnectionId!=g_bridge.ConnectionId(g_bridge.LocalSlot()))return {"self-connection",t.source.targetConnectionId,g_bridge.ConnectionId(g_bridge.LocalSlot())};
    if(t.source.sessionId!=g_populationCutScope->sessionId)return {"session",1,0};
    if(t.cut!=g_populationCut->sequence)return {"cut",t.cut,g_populationCut->sequence};
    if(t.manifest!=g_clientManifestRevision)return {"manifest",t.manifest,g_clientManifestRevision};
    if(t.epoch!=g_host.epoch)return {"epoch",t.epoch,g_host.epoch};
    if(t.load!=warp::LoadSerial())return {"load",t.load,warp::LoadSerial()};
    if(t.transition!=warp::TransitionSerial())return {"transition",t.transition,warp::TransitionSerial()};
    if(g_resyncWriteFence!=ResyncWriteFence::None)return {"resync-fence",0,1};
    if(!SafeNativeGameplay())return {"safe-gameplay",1,0};
    RoomTransition room;
    if(!ReadLocationChecked(room))return {"full-location-read",0,0,false};
    if(t.location!=PopulationLocation(room) || t.location!=g_populationCut->location)return {"full-location",1,0};
    if(!PopulationCutCurrent())return {"cut-current",1,0};
    const auto* terminal=PopulationTerminalFor(t.definition,t.ordinal);
    if(!terminal || terminal->netId!=t.netId || terminal->birthSequence!=t.birthSequence ||
       terminal->terminalSequence!=t.terminalSequence)
        return {"terminal-occurrence",t.terminalSequence,terminal?terminal->terminalSequence:0};
    const auto tracked=g_inst.byActor.find(t.roots[0]);
    if(tracked==g_inst.byActor.end() || tracked->second>=g_inst.spawns.size())return {"tracked-actor",1,0};
    const auto& spawn=g_inst.spawns[tracked->second];
    if(!spawn.present || !spawn.identityRead || spawn.netId>0)return {"unbound-present",0,static_cast<std::uint64_t>(spawn.netId)};
    if(PopulationSpawnRoots(spawn)!=t.roots)return {"tracked-five-roots",1,0};
    return {};
}
PopulationCheck PopulationTicketCheck(PopulationDisposalTicket t) {
    const auto scope=PopulationTicketScopeCheck(t);
    if(!scope)return scope;
    const auto census=CaptureNativeCensus(t.roots[0]);
    if(!CensusMatchesInstance(census))return {"census-scope",1,0};
    if(!census.watchedActorPresent)return {"actor-presence",1,0};
    const auto identity=ReadPopulationIdentity(t.roots[0]);
    if(identity.actor!=t.roots[0])return {"actor",t.roots[0],identity.actor};
    if(identity.objentry!=t.roots[1])return {"objentry",t.roots[1],identity.objentry};
    if(identity.status!=t.roots[2])return {"status",t.roots[2],identity.status};
    const auto local=g_inst.byActor.find(t.roots[0]);
    if(local==g_inst.byActor.end() || local->second>=g_inst.spawns.size())return {"tracked-actor",1,0};
    const auto& spawn=g_inst.spawns[local->second];
    if(!spawn.present || !spawn.identityRead || spawn.netId>0)return {"unbound-present",0,static_cast<std::uint64_t>(spawn.netId)};
    if(PopulationSpawnRoots(spawn)!=t.roots)return {"tracked-five-roots",1,0};
    RecordCatalog catalog;spawncontroller::NativeRecordMembership member;
    if(!FreshRecordCatalog(census,catalog))return {"catalog",0,0,false};
    if(!RecordMember(catalog,census,t.roots[0],member))return {"membership",0,0,false};
    if(member.controller[0]!=t.roots[3] || member.controller[1]!=t.roots[3])return {"member-controller",t.roots[3],member.controller[1]};
    if(member.record[0]!=t.roots[4] || member.record[1]!=t.roots[4])return {"member-record",t.roots[4],member.record[1]};
    if(member.tableIndex>=catalog.entryCount || member.recordIndex!=t.ordinal)return {"member-ordinal",t.ordinal,member.recordIndex};
    if(!population_cut_detail::sameDefinition(catalog.entries[member.tableIndex].content,t.definition))return {"definition",1,0};
    const auto* terminal=PopulationTerminalFor(t.definition,t.ordinal);
    if(!terminal || terminal->netId!=t.netId || terminal->birthSequence!=t.birthSequence || terminal->terminalSequence!=t.terminalSequence)return {"terminal-occurrence",t.terminalSequence,terminal?terminal->terminalSequence:0};
    const auto* native=FindNativeEnemy(census,spawn);
    if(!native || native->objectId!=terminal->objectId)return {"native-object",terminal->objectId,native?native->objectId:0};
    unsigned aliases=0;
    for(std::uint32_t i=0;i<catalog.entryCount;++i)
        if(population_cut_detail::sameDefinition(catalog.entries[i].content,t.definition))++aliases;
    if(aliases!=1)return {"definition-alias",1,aliases};
    RecordCatalog after;spawncontroller::NativeRecordMembership finalMember;
    if(!FreshRecordCatalog(census,after) || !SameRecordCatalogSample(catalog,after))return {"catalog-bookend",1,0};
    if(!RecordMember(after,census,t.roots[0],finalMember) || finalMember.tableIndex!=member.tableIndex ||
       finalMember.recordIndex!=t.ordinal || finalMember.controller[0]!=t.roots[3] || finalMember.controller[1]!=t.roots[3] ||
       finalMember.record[0]!=t.roots[4] || finalMember.record[1]!=t.roots[4])return {"membership-bookend",1,0};
    const auto final=ReadPopulationIdentity(t.roots[0]);
    if(final.actor!=t.roots[0])return {"final-actor",t.roots[0],final.actor};
    if(final.objentry!=t.roots[1])return {"final-objentry",t.roots[1],final.objentry};
    if(final.status!=t.roots[2])return {"final-status",t.roots[2],final.status};
    uintptr_t controller=0,record=0;
    if(!ReadNative(t.roots[0]+0x9E8,controller))return {"final-controller-read",t.roots[3],0,false};
    if(controller!=t.roots[3])return {"final-controller",t.roots[3],controller};
    if(!ReadNative(t.roots[0]+0x9F0,record))return {"final-record-read",t.roots[4],0,false};
    if(record!=t.roots[4])return {"final-record",t.roots[4],record};
    if(!CensusMatchesInstance(census))return {"final-census-scope",1,0};
    return PopulationTicketScopeCheck(t);
}
bool PopulationTicketFresh(const PopulationDisposalTicket& ticket) {return static_cast<bool>(PopulationTicketCheck(ticket));}
void PopulationOutcome(PopulationDisposalTicket& t,const char* action,PopulationCheck check,std::uint32_t frame) {
    if(std::strcmp(t.lastOutcome,action)==0 && std::strcmp(t.lastReason,check.reason)==0) {
        ++t.outcomeRepeats;t.outcomeLast=frame;
        const auto live=std::find_if(g_populationDisposals.begin(),g_populationDisposals.end(),[&](const auto& q){return q.id==t.id;});
        if(live!=g_populationDisposals.end() && &*live!=&t)*live=t;
        return;
    }
    if(g_populationOutcomeSequence>=POPULATION_OUTCOME_CAP || !g_log) {
        ++g_populationOutcomeLoss;
        if(g_log)g_log("[enemy-pop] receipt-loss disposal-outcome-cap loss=%llu",static_cast<unsigned long long>(g_populationOutcomeLoss));
        return;
    }
    g_log("[enemy-pop] disposal-outcome-json {\"schema\":1,\"seq\":%llu,\"ticket\":%llu,\"action\":\"%s\",\"reason\":\"%s\",\"expected\":%llu,\"observed\":%llu,\"available\":%s,\"frame\":%u,\"netId\":%u,\"cut\":%llu,\"deathSequence\":%llu,\"load\":%u,\"transition\":%u,\"roots\":[%llu,%llu,%llu,%llu,%llu],\"priorFirst\":%u,\"priorLast\":%u,\"priorRepeats\":%llu}",
        static_cast<unsigned long long>(++g_populationOutcomeSequence),static_cast<unsigned long long>(t.id),action,check.reason,
        static_cast<unsigned long long>(check.expected),static_cast<unsigned long long>(check.observed),check.available?"true":"false",frame,t.netId,
        static_cast<unsigned long long>(t.cut),static_cast<unsigned long long>(t.terminalSequence),t.load,t.transition,
        static_cast<unsigned long long>(t.roots[0]),static_cast<unsigned long long>(t.roots[1]),static_cast<unsigned long long>(t.roots[2]),static_cast<unsigned long long>(t.roots[3]),static_cast<unsigned long long>(t.roots[4]),
        t.outcomeFirst,t.outcomeLast,static_cast<unsigned long long>(t.outcomeRepeats));
    t.lastOutcome=action;t.lastReason=check.reason;t.outcomeFirst=t.outcomeLast=frame;t.outcomeRepeats=0;
    const auto live=std::find_if(g_populationDisposals.begin(),g_populationDisposals.end(),[&](const auto& q){return q.id==t.id;});
    if(live!=g_populationDisposals.end() && &*live!=&t)*live=t;
}
void CancelPopulationDisposals(const char* reason,std::uint32_t frame) {
    for(auto& t:g_populationDisposals)PopulationOutcome(t,"cancelled",{reason,1,0},frame);
    g_populationDisposals.clear();
}
void SealPopulationDisposals(const char* reason,std::uint32_t frame) {
    CancelPopulationDisposals(reason,frame);
    if(g_log)g_log("[enemy-pop] disposal-outcome-seal seq=%llu tickets=%llu loss=%llu attempts=%zu",
        static_cast<unsigned long long>(g_populationOutcomeSequence),static_cast<unsigned long long>(g_populationTicketSequence),
        static_cast<unsigned long long>(g_populationOutcomeLoss),g_populationDeathAttempts.size());
}
void LogPopulationDisposal(const char* action,const PopulationDisposalTicket& ticket,std::uint32_t frame) {
    if(!g_log || !g_populationCut || !g_populationCutScope)return;
    auto entry=std::find_if(g_populationCut->entries.begin(),g_populationCut->entries.end(),[&](const auto& e){return e.netId==ticket.netId;});
    if(entry==g_populationCut->entries.end())return;
    std::ostringstream s;s<<"{\"schema\":1,\"action\":"<<PopulationJsonString(action)<<",\"scope\":"<<PopulationClientScopeJson()
        <<",\"netId\":"<<ticket.netId<<",\"roots\":[";
    for(std::size_t i=0;i<ticket.roots.size();++i){if(i)s<<',';s<<ticket.roots[i];}
    s<<"],\"cut\":"<<ticket.cut<<",\"deathSequence\":"<<ticket.terminalSequence<<",\"frame\":"<<frame
        <<",\"membership\":"<<PopulationMembershipJson(g_populationCut->definitions[entry->definition],entry->record);
    float admissionAfter=0.0f;std::uint32_t countAfter=0;
    const bool budgetAfterAvailable=ReadNative(g_exeBase+RVA_ADMISSION_USED,admissionAfter) && std::isfinite(admissionAfter) && admissionAfter>=0;
    bool countAfterAvailable=false;
    const auto countCensus=CaptureNativeCensus();RecordCatalog currentControllers;
    if(CensusMatchesInstance(countCensus) && FreshRecordCatalog(countCensus,currentControllers)) {
        unsigned matches=0;
        for(std::uint32_t i=0;i<currentControllers.entryCount;++i)
            if(currentControllers.entries[i].controller==ticket.roots[3] &&
               population_cut_detail::sameDefinition(currentControllers.entries[i].content,ticket.definition))++matches;
        RecordCatalog countBookend;
        if(matches==1 && ReadNative(ticket.roots[3]+0x24,countAfter) &&
           FreshRecordCatalog(countCensus,countBookend) && SameRecordCatalogSample(currentControllers,countBookend))countAfterAvailable=true;
    }
    s<<",\"nativeBookkeeping\":{\"budgetAvailable\":"<<((ticket.budgetBeforeAvailable && budgetAfterAvailable)?"true":"false")
        <<",\"usedBefore\":"<<(ticket.budgetBeforeAvailable?ticket.admissionBefore:0.0f)
        <<",\"usedAfter\":"<<(budgetAfterAvailable?admissionAfter:0.0f)
        <<",\"countAvailable\":"<<((ticket.countBeforeAvailable && countAfterAvailable)?"true":"false")
        <<",\"countBefore\":"<<ticket.controllerCountBefore<<",\"countAfter\":"<<countAfter<<"}}";
    // Native logger has a finite line buffer; never publish a truncated authority assertion.
    const auto text=s.str();if(text.size()>1500) {g_log("[enemy-pop] receipt-loss oversized-native-disposal");return;}
    g_log("[enemy-pop] stale-native-json %s",text.c_str());
}
void TickPopulationRepair(std::uint32_t frame,const NativeCensus& census) {
    if(!g_populationRequested || g_role!=Role::Client || !g_populationCut || !g_populationCutScope ||
        !CensusMatchesInstance(census) || !PopulationCutCoversHost(*g_populationCut) ||
        g_resyncWriteFence!=ResyncWriteFence::None || g_populationCut->location!=PopulationLocation(census.location)) {
        CancelPopulationDisposals("repair-scope-unavailable",frame);g_populationLeasePoints.clear();return;
    }
    if(!g_populationInstalled) {
        g_populationInstallLoad=census.load;g_populationInstallTransition=census.transition;
        g_populationInstallGeneration=WorldSessionGeneration();g_populationInstallManifest=g_clientManifestRevision;
        g_populationInstallFrame=frame;g_populationLeaseTicks=0;g_populationInstalled=true;
        if(!PopulationCutCurrent()) {g_populationInstalled=false;return;}
        if(g_log)g_log("[enemy-pop] population-cut-installed-json {\"schema\":1,\"scope\":%s,\"cut\":%llu,\"sequence\":%llu,\"frame\":%u}",
            PopulationClientScopeJson().c_str(),static_cast<unsigned long long>(g_populationCut->sequence),
            static_cast<unsigned long long>(g_populationCut->sequence),frame);
    }
    if(!PopulationCutCurrent()) {CancelPopulationDisposals("repair-scope-unavailable",frame);g_populationLeasePoints.clear();return;}
    // Authority queries can retire/reset the live vector: keep no iterator across them.
    const auto pending=g_populationDisposals;
    for(auto ticket:pending) {
        bool retire=false;
        if(!CensusHasActor(census,ticket.roots[0])) {
            const auto absence=CaptureNativeCensus(ticket.roots[0]);
            if(ticket.dispatched && frame>ticket.dispatchedFrame && CensusMatchesInstance(absence) &&
                !absence.watchedActorPresent && PopulationCutCurrent()) {
                PopulationOutcome(ticket,"disposed",{},frame);LogPopulationDisposal("disposed",ticket,frame);
            } else PopulationOutcome(ticket,"cancelled",{"absence-without-confirmed-call-scope",1,0},frame);
            retire=true;
        } else if(const auto check=PopulationTicketCheck(ticket); !check) {
            PopulationOutcome(ticket,"refused",check,frame);retire=true;
        }
        if(retire)g_populationDisposals.erase(std::remove_if(g_populationDisposals.begin(),g_populationDisposals.end(),
            [&](const auto& live){return live.id==ticket.id;}),g_populationDisposals.end());
    }
    if(!PopulationCutCurrent())return;
    RecordCatalog catalog;
    if(!FreshRecordCatalog(census,catalog)) {CancelPopulationDisposals("repair-scope-unavailable",frame);g_populationLeasePoints.clear();return;}
    const auto unboundCandidates=g_inst.spawns;
    for(const auto& spawn:unboundCandidates) {
        if(!spawn.present || spawn.netId>0 || !spawn.identityRead ||
            std::any_of(g_populationDisposals.begin(),g_populationDisposals.end(),[&](const auto& t){return t.roots[0]==spawn.actor;}))continue;
        spawncontroller::NativeRecordMembership member;
        if(!RecordMember(catalog,census,spawn.actor,member) || member.tableIndex>=catalog.entryCount)continue;
        const auto* selected=PopulationTerminalFor(catalog.entries[member.tableIndex].content,member.recordIndex);
        if(!selected || !g_populationCut || !g_populationCutScope)continue;
        const auto terminal=*selected; // no authority-owned reference across generation retirement

        PopulationDisposalTicket ticket;
        ticket.roots={spawn.actor,spawn.objentry,spawn.status,member.controller[1],member.record[1]};
        ticket.netId=terminal.netId;ticket.terminalSequence=terminal.terminalSequence;
        ticket.cut=g_populationCut->sequence;ticket.manifest=g_clientManifestRevision;
        ticket.delivery=g_bridge.DeliverySerial();
        ticket.load=census.load;ticket.transition=census.transition;ticket.location=g_populationCut->location;
        ticket.authorizedFrame=frame;ticket.id=++g_populationTicketSequence;
        ticket.birthSequence=terminal.birthSequence;ticket.source=*g_populationCutScope;ticket.epoch=g_host.epoch;
        ticket.definition=catalog.entries[member.tableIndex].content;ticket.ordinal=member.recordIndex;
        ticket.generation=WorldSessionGeneration();
        ticket.budgetBeforeAvailable=ReadNative(g_exeBase+RVA_ADMISSION_USED,ticket.admissionBefore) &&
            std::isfinite(ticket.admissionBefore) && ticket.admissionBefore>=0;
        ticket.countBeforeAvailable=ReadNative(ticket.roots[3]+0x24,ticket.controllerCountBefore);
        const auto check=PopulationTicketCheck(ticket);
        PopulationOutcome(ticket,"produced",{},frame);
        if(g_populationDisposals.size()<POPULATION_MAX_ENTRIES && check) {
            g_populationDisposals.push_back(ticket);LogPopulationDisposal("authorized",ticket,frame);
        } else PopulationOutcome(ticket,"refused",check?PopulationCheck{"ticket-cap",POPULATION_MAX_ENTRIES,g_populationDisposals.size()}:check,frame);
    }
    g_populationLeasePoints.clear();
    if(!PopulationCutCurrent() || g_populationLeaseTicks>=600)return;
    ++g_populationLeaseTicks;
    for(const auto& occurrence:g_populationCut->entries) {
        if(occurrence.terminal)continue;
        if(std::any_of(g_inst.spawns.begin(),g_inst.spawns.end(),[&](const auto& s){return s.present && s.netId==occurrence.netId;}))continue;
        const auto& definition=g_populationCut->definitions[occurrence.definition];
        const spawncontroller::NativeRecordCatalogEntry* match=nullptr;unsigned matches=0;
        for(std::uint32_t i=0;i<catalog.entryCount;++i)
            if(population_cut_detail::sameDefinition(catalog.entries[i].content,definition)){match=&catalog.entries[i];++matches;}
        if(matches==1 && match && g_populationLeasePoints.size()<POPULATION_MAX_ENTRIES)
            g_populationLeasePoints.push_back({match->controller,definition,occurrence.activationPoint});
    }
}
bool PopulationStaleAuthorized(uintptr_t actor) {
    const auto ticket=std::find_if(g_populationDisposals.begin(),g_populationDisposals.end(),[&](const auto& t){return t.roots[0]==actor;});
    return ticket!=g_populationDisposals.end() && !ticket->dispatched && PopulationTicketFresh(*ticket);
}
bool CopyPopulationActivation(float* point,uintptr_t controller) {
    if(!point || g_populationLeasePoints.empty() || g_populationLeaseTicks>=600 || !PopulationCutCurrent() ||
        !spawncontroller::IsDiagnosticGameThread() || g_nativeResync)return false;
    // Exactly one bounded historical input per 30 resolved frames, never a record/controller write.
    const auto selected=g_populationLeasePoints[((g_populationLeaseTicks-1)/30)%g_populationLeasePoints.size()];
    if(selected.controller!=controller)return false;
    const auto census=CaptureNativeCensus();RecordCatalog catalog;
    if(!CensusMatchesInstance(census) || !FreshRecordCatalog(census,catalog))return false;
    unsigned matches=0;
    for(std::uint32_t i=0;i<catalog.entryCount;++i)
        if(catalog.entries[i].controller==controller && population_cut_detail::sameDefinition(catalog.entries[i].content,selected.definition))++matches;
    if(matches!=1 || !PopulationCutCurrent())return false;
    std::copy(selected.point.begin(),selected.point.end(),point);return true;
}

bool PopulationAttempted(const PopulationDisposalTicket& t) {
    return std::any_of(g_populationDeathAttempts.begin(),g_populationDeathAttempts.end(),[&](const auto& a) {
        return a.roots==t.roots && a.load==t.load && a.transition==t.transition;
    });
}
// Returns false after any native call (or failed recapture): caller must stop this frame.
struct PopulationDeathResult {bool called{},returned{};PopulationCheck check;};
bool PopulationDeathHelperReady();
// The production definition is a POD-only SEH leaf, reached only after final proof.
bool InvokePopulationNativeDeath(uintptr_t actor,int hp);
PopulationDeathResult CallPopulationNativeDeath(PopulationDisposalTicket ticket,const NativeEnemy& expected) {
    if(!g_populationConsumerActive || !PopulationDeathHelperReady())return {false,false,{"native-helper-unqualified",1,0}};
    const auto check=PopulationTicketCheck(ticket);
    if(!check)return {false,false,check};
    NativeEnemy final;bool enemy=false;
    if(!ReadNativeEnemy(expected.actor,final,enemy) || !enemy || !SameNativeIdentity(expected,final))
        return {false,false,{"leaf-native-identity",1,0}};
    if(final.hp<=0 || final.hp!=expected.hp)return {false,false,{"leaf-hp",static_cast<std::uint64_t>(expected.hp),static_cast<std::uint64_t>(final.hp)}};
    const auto last=PopulationTicketCheck(ticket);
    if(!last)return {false,false,last};
    std::int32_t hp=0;
    if(!ReadNative(ticket.roots[2],hp) || hp!=expected.hp || hp<=0)
        return {false,false,{"final-hp",static_cast<std::uint64_t>(expected.hp),static_cast<std::uint64_t>(hp)}};
    const auto dispatchCensus=CaptureNativeCensus(ticket.roots[0]);
    if(!CensusMatchesInstance(dispatchCensus) || !dispatchCensus.watchedActorPresent)
        return {false,false,{"dispatch-census",1,0}};
    NativeEnemy dispatchEnemy;bool dispatchCombat=false;
    if(!ReadNativeEnemy(expected.actor,dispatchEnemy,dispatchCombat) || !dispatchCombat ||
       !SameNativeIdentity(expected,dispatchEnemy) || dispatchEnemy.hp!=hp)
        return {false,false,{"dispatch-native-identity-hp",1,0}};
    // Post-HP five-root and full-scope bookend immediately precedes dispatch.
    const auto identity=ReadPopulationIdentity(ticket.roots[0]);
    if(identity.actor!=ticket.roots[0])return {false,false,{"dispatch-actor",ticket.roots[0],identity.actor}};
    if(identity.objentry!=ticket.roots[1])return {false,false,{"dispatch-objentry",ticket.roots[1],identity.objentry}};
    if(identity.status!=ticket.roots[2])return {false,false,{"dispatch-status",ticket.roots[2],identity.status}};
    uintptr_t controller=0,record=0;
    if(!ReadNative(ticket.roots[0]+0x9E8,controller) || controller!=ticket.roots[3])return {false,false,{"dispatch-controller",ticket.roots[3],controller}};
    if(!ReadNative(ticket.roots[0]+0x9F0,record) || record!=ticket.roots[4])return {false,false,{"dispatch-record",ticket.roots[4],record}};
    const auto authority=PopulationTicketScopeCheck(ticket);
    if(!authority)return {false,false,authority};
    const bool returned=InvokePopulationNativeDeath(expected.actor,hp);
    return {true,returned,returned?PopulationCheck{}:PopulationCheck{"native-call-fault",0,0,false}};
}
bool ConsumePopulationDisposals(std::uint32_t frame,NativeCensus& census) {
    if(g_populationConsumerActive) {
        for(auto& t:g_populationDisposals)PopulationOutcome(t,"refused",{"reentrant-consumer",0,1},frame);
        return false;
    }
    if(!spawncontroller::IsDiagnosticGameThread()) {
        for(auto& t:g_populationDisposals)PopulationOutcome(t,"refused",{"owner-thread",1,0},frame);
        return false;
    }
    g_populationConsumerActive=true;
    struct Guard {~Guard(){g_populationConsumerActive=false;}} guard;
    const auto tickets=g_populationDisposals; // callbacks/role retirement may clear the live vector
    for(auto ticket:tickets) {
        const auto check=PopulationTicketCheck(ticket);
        if(!check) {PopulationOutcome(ticket,"refused",check,frame);continue;}
        if(PopulationAttempted(ticket)) {
            const auto attempt=std::find_if(g_populationDeathAttempts.begin(),g_populationDeathAttempts.end(),[&](const auto& a){return a.roots==ticket.roots && a.load==ticket.load && a.transition==ticket.transition;});
            ticket.dispatched=true;ticket.dispatchedFrame=attempt->frame;
            PopulationOutcome(ticket,"waiting-native-removal",{"one-shot-poisoned",1,1},frame);continue;
        }
        if(!PopulationDeathHelperReady()) {PopulationOutcome(ticket,"refused",{"native-helper-unqualified",1,0},frame);continue;}
        const auto found=g_inst.byActor.find(ticket.roots[0]);
        const auto fresh=CaptureNativeCensus(ticket.roots[0]);
        if(!CensusMatchesInstance(fresh) || found==g_inst.byActor.end() || found->second>=g_inst.spawns.size()) {
            PopulationOutcome(ticket,"refused",{"pre-call-census",1,0},frame);continue;
        }
        const auto* sampled=FindNativeEnemy(fresh,g_inst.spawns[found->second]);
        if(!sampled) {PopulationOutcome(ticket,"refused",{"pre-call-actor",1,0},frame);continue;}
        const auto expected=*sampled;
        if(expected.hp<=0) {PopulationOutcome(ticket,"waiting-native-removal",{"native-hp-nonpositive",0,0},frame);continue;}
        // Reserve process-lifetime poison only when all preparatory checks passed.
        // Failed leaf checks do not dispatch; a fault/unknown native result never retries.
        if(g_populationDeathAttempts.size()>=POPULATION_MAX_ENTRIES*2) {
            PopulationOutcome(ticket,"refused",{"attempt-journal-cap",POPULATION_MAX_ENTRIES*2,g_populationDeathAttempts.size()},frame);continue;
        }
        const auto final=PopulationTicketCheck(ticket);
        if(!final) {PopulationOutcome(ticket,"refused",final,frame);continue;}
        PopulationOutcome(ticket,"attempted",{},frame);
        g_populationDeathAttempts.push_back({ticket.roots,ticket.load,ticket.transition,frame});
        ticket.dispatchedFrame=frame;
        const auto result=CallPopulationNativeDeath(ticket,expected);
        if(!result.called) {
            g_populationDeathAttempts.pop_back();
            PopulationOutcome(ticket,"refused",result.check,frame);continue;
        }
        ticket.dispatched=true;
        PopulationOutcome(ticket,result.returned?"call-returned":"call-fault",result.check,frame);
        // No pre-call census/spawn pointer may survive a native mutation or fault.
        census=CaptureNativeCensus();
        if(!CensusMatchesInstance(census) || !PopulationCutCurrent()) {
            PopulationOutcome(ticket,"refused",{"post-call-census-scope",1,0},frame);return false;
        }
        TrackSpawns(census);
        return false; // next registered owner frame rebuilds bindings/admission/hash inputs
    }
    return true;
}
