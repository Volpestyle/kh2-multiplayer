// Included inside ScriptMirror after ScriptActorOwner. VUH-2913: Axel II's collision-offered reaction commands
// (AxelReaction.hpp, docs/AXEL_REACTIONS.md). The host mints one owner per offer while host Axel II is live; each
// seat's press, the host's own included, acts only on a live window of its own seat; sysobj_player inside Axel's
// script resolves to the seat that ran the RC. Native state is only read here, except the original callback.
struct SmAxelState {
    // host
    uintptr_t actor{};
    std::uint32_t handle{},epoch{};
    std::uint16_t netId{};
    R::AxelWindows windows{};
    R::AxelExecutor executor{};
    uintptr_t executorActor{};
    std::uint32_t executorHandle{};
    // client
    R::AxelOffers offers{};
    struct Identity { std::uint8_t slot{0xFF};std::uint64_t connection{};std::uint32_t epoch{}; };
    std::array<Identity,3> identities{};
    unsigned logs{};
};
static SmAxelState g_axel{};
static constexpr unsigned kSmAxelLogs=256;
static bool SmAxelLogBudget() noexcept { if(g_axel.logs>=kSmAxelLogs)return false;++g_axel.logs;return true; }

static bool SmAxelReceiver(uintptr_t actor) noexcept {
    uintptr_t entry=0;std::uint32_t objectId=0;
    return actor && ReadHitTrace(actor+offsets::actor::OBJENTRY_PTR,entry) && entry &&
        ReadHitTrace(entry+offsets::objentry::OBJECT_ID,objectId) && objectId==R::kAxelIIObjectId;
}
static bool SmAxelHostCurrent() noexcept {
    return g_sm.hostActive && g_axel.actor && g_axel.handle && SmOwnHandle(g_axel.actor)==g_axel.handle;
}
static void SmAxelEndExecutor(const char* why) {
    if(g_axel.executor.command && SmAxelLogBudget())
        Log("[axel-rc] executor end command=%u owner=%u frames=%u engaged=%u reason=%s",g_axel.executor.command,
            g_axel.executor.slot,g_frameCounter-g_axel.executor.admittedFrame,g_axel.executor.engaged?1u:0u,why);
    g_axel.executor={};g_axel.executorActor=0;g_axel.executorHandle=0;
}
static void SmAxelHostReset() {
    if(!g_axel.actor && !g_axel.executor.command)return;
    SmAxelEndExecutor("host-axel-gone");
    g_axel.windows.Reset();g_axel.actor=0;g_axel.handle=0;g_axel.netId=0;g_axel.epoch=0;
}
static bool SmAxelExecutorCurrent(const enemysync::DownedScope& scope) {
    const auto& e=g_axel.executor;
    return e.slot<3 && R::AxelExecutorLive(e,scope.epoch,scope.connections[e.slot],g_frameCounter) &&
        g_axel.executorActor && SmOwnHandle(g_axel.executorActor)==g_axel.executorHandle;
}
// 1/3 inside Axel's own script: the seat that ran the current RC, else -1 (the caller keeps its own choice).
static int SmAxelExecutorSlot(uintptr_t source) {
    if(!g_sm.hostActive || !source || source!=g_axel.actor || !g_axel.executor.command)return -1;
    enemysync::DownedScope scope;
    return SmOwnerScope(scope) && SmAxelExecutorCurrent(scope)?static_cast<int>(g_axel.executor.slot):-1;
}
static void SmAxelExecutorTick(const enemysync::DownedScope& scope) {
    if(!g_axel.executor.command)return;
    if(!SmAxelExecutorCurrent(scope)){SmAxelEndExecutor("lease-or-seat");return;}
    std::uint32_t parent=0;uintptr_t capture=0;
    const bool attached=ReadHitTrace(g_axel.actor+0x6A0,parent) && parent && SmDecode(parent)==g_axel.executorActor;
    const bool captured=ReadHitTrace(g_axel.executorActor+0x2B0,capture) && capture==g_axel.actor;
    const bool was=g_axel.executor.engaged;
    if(!R::AxelExecutorContinues(g_axel.executor,attached,captured)){SmAxelEndExecutor("released");return;}
    if(!was && g_axel.executor.engaged && SmAxelLogBudget())
        Log("[axel-rc] executor engaged command=%u owner=%u attached=%u captured=%u",g_axel.executor.command,
            g_axel.executor.slot,attached?1u:0u,captured?1u:0u);
}
static bool SmAxelMasks(R::AxelGroupMasks& m) noexcept {
    m={};uintptr_t collision=0;
    if(!ReadHitTrace(g_axel.actor+0x5A0,m.motionOn) || !ReadHitTrace(g_axel.actor+0x5A4,m.motionOff) ||
       !ReadHitTrace(g_axel.actor+0x5A8,m.scriptOn) || !ReadHitTrace(g_axel.actor+0x5AC,m.scriptOff) ||
       !ReadHitTrace(g_axel.actor+0xAB8,collision))return false;
    m.collision=collision && ReadHitTrace(collision+4,m.defaults);
    return true;
}
static bool SmAxelEmit(const R::ReactionWindow& w) {
    R::Call identity{};identity.netId=g_axel.netId;identity.objectId=R::kAxelIIObjectId;
    identity.effect=R::AxelResource(w.command);identity.trap=static_cast<std::uint8_t>(R::FindTrap(255,12));
    identity.values[0]=R::Tagged(R::kTagInt,w.slot);
    identity.values[1]=R::Tagged(R::kTagInt,static_cast<std::uint32_t>(w.connection));
    identity.values[2]=R::Tagged(R::kTagInt,static_cast<std::uint32_t>(w.connection>>32));
    R::Call offer=identity;offer.trap=static_cast<std::uint8_t>(R::FindTrap(255,1));
    offer.values[0]=R::Tagged(R::kTagInt,static_cast<std::uint32_t>(w.occurrence));
    offer.values[1]=R::Tagged(R::kTagInt,static_cast<std::uint32_t>(w.occurrence>>32));
    offer.values[2]=R::Tagged(R::kTagInt,w.command);offer.values[3]=R::Tagged(R::kTagInt,w.slot);
    return SmEmit(identity) && SmEmit(offer);
}
// Host, once per frame from SmTickReactions. Three compares leave every other room.
static void SmAxelFrame(const enemysync::DownedScope& scope) {
    if(!g_sm.hostActive || !R::AxelEncounter(scope.worldId,scope.roomId,scope.battleProgram) || scope.localSlot!=0) {
        SmAxelHostReset();return;
    }
    std::uint16_t netId=0;
    const auto actor=enemysync::HostLiveEnemyActor(R::kAxelIIObjectId,netId);
    const auto handle=actor?SmOwnHandle(actor):0;
    if(!handle){SmAxelHostReset();return;}
    if(actor!=g_axel.actor || handle!=g_axel.handle || netId!=g_axel.netId || scope.epoch!=g_axel.epoch) {
        SmAxelHostReset();
        g_axel.actor=actor;g_axel.handle=handle;g_axel.netId=netId;g_axel.epoch=scope.epoch;
    }
    SmAxelExecutorTick(scope);
    R::AxelGroupMasks masks{};float xyz[3]{},yaw=0;
    const auto entity=actor+offsets::actor::ENTITY_TRANSFORM;
    if(!SmAxelMasks(masks) || !ReadHitTraceMemory(entity+offsets::entity::POS_X,xyz,sizeof(xyz)) ||
       !ReadHitTrace(entity+offsets::entity::ROT_Y,yaw))return;
    const auto seats=SmOwnerLivingSeats(scope);
    const R::ReactionPoint origin{xyz[0],xyz[1],xyz[2]};
    R::AxelExecutorFacts chain{};
    if(g_axel.executor.command && SmAxelExecutorCurrent(scope)) {
        chain.current=true;chain.command=g_axel.executor.command;chain.slot=g_axel.executor.slot;
        if(!ReadHitTrace(g_axel.executorActor+0x638,chain.followup) || !ReadHitTrace(g_axel.executorActor+0x18C,chain.flags))
            chain.current=false;
    }
    const bool overtaker=R::AxelGroupActive(masks,R::kOvertakerGroup);
    const bool burst=R::AxelGroupActive(masks,R::kBurstFrontierGroupA) || R::AxelGroupActive(masks,R::kBurstFrontierGroupB);
    const int chainOwner=R::AxelChainOwner(chain);
    const bool active[3]{overtaker,chainOwner>=0,burst};
    const int winners[3]{overtaker?R::AxelFamilyWinner(R::kOvertakerSpheres,origin,yaw,seats):-1,chainOwner,
        burst?R::AxelFamilyWinner(R::kBurstFrontierSpheres,origin,yaw,seats):-1};
    for(unsigned i=0;i<3;++i) {
        const auto command=R::kAxelCommands[i];
        const auto* before=g_axel.windows.Find(command);
        const auto previous=before?*before:R::ReactionWindow{};
        const auto* opened=g_axel.windows.Tick(i,active[i],winners[i],netId,scope.epoch,g_frameCounter,seats);
        if(opened) {
            const bool sent=SmAxelEmit(*opened);
            if(SmAxelLogBudget())
                Log("[axel-rc] offer source=%u resource=%u occurrence=%llu command=%u owner=%u epoch=%u sent=%u",
                    netId,R::AxelResource(command),opened->occurrence,command,opened->slot,scope.epoch,sent?1u:0u);
        } else if(previous.occurrence && !g_axel.windows.Find(command) && SmAxelLogBudget()) {
            Log("[axel-rc] offer end source=%u occurrence=%llu command=%u owner=%u consumed=%u frames=%u",
                netId,previous.occurrence,command,previous.slot,previous.consumed?1u:0u,g_frameCounter-previous.openedFrame);
        }
    }
}

// Client: identity then offer, as the host's attack bindings are announced.
static bool SmAxelApplyIdentity(const R::Call& c,std::uint16_t command,uintptr_t actor) {
    enemysync::DownedScope scope;
    if(g_sm.hostActive || !SmOwnerScope(scope) || !actor || actor!=SmDrivenActor(c.netId,c.objectId))return false;
    const auto slot=R::BitsOf(c.values[0]);
    const auto connection=static_cast<std::uint64_t>(R::BitsOf(c.values[1])) |
        (static_cast<std::uint64_t>(R::BitsOf(c.values[2]))<<32);
    if(slot>=3 || !connection || scope.connections[slot]!=connection)return false;
    g_axel.identities[static_cast<unsigned>(R::AxelCommandIndex(command))]={static_cast<std::uint8_t>(slot),connection,scope.epoch};
    return true;
}
static bool SmAxelApplyOffer(const R::Call& c,std::uint16_t command,uintptr_t actor) {
    enemysync::DownedScope scope;
    if(g_sm.hostActive || !SmOwnerScope(scope) || !actor || actor!=SmDrivenActor(c.netId,c.objectId))return false;
    const auto occurrence=static_cast<std::uint64_t>(R::BitsOf(c.values[0])) |
        (static_cast<std::uint64_t>(R::BitsOf(c.values[1]))<<32);
    const auto slot=R::BitsOf(c.values[3]);
    const auto& identity=g_axel.identities[static_cast<unsigned>(R::AxelCommandIndex(command))];
    if(R::BitsOf(c.values[2])!=command || slot>=3 || identity.slot!=slot || identity.epoch!=scope.epoch ||
       scope.connections[slot]!=identity.connection)return false;
    const bool applied=g_axel.offers.Apply(occurrence,command,static_cast<std::uint8_t>(slot),scope.localSlot,
        identity.connection,c.netId,scope.epoch,g_frameCounter);
    if(applied && SmAxelLogBudget())
        Log("[axel-rc] client offer source=%u occurrence=%llu command=%u owner=%u mine=%u epoch=%u",c.netId,occurrence,
            command,slot,slot==scope.localSlot?1u:0u,scope.epoch);
    return applied;
}

// Family 3 of the native RC proof: this seat's live window for the command on the Axel actor the native target
// names ({Axel, 0, collision object whose +0x118 is Axel, -1}, built by 3BEE00).
static bool SmAxelProof(std::uint16_t command,const std::array<std::uint32_t,4>& target,const enemysync::DownedScope& scope,
    std::uint64_t connection,uintptr_t& source,uintptr_t& native,R::NativeReactionIdentity& id) noexcept {
    if(R::AxelCommandIndex(command)<0 || !target[0] || !target[2] ||
       !R::AxelEncounter(scope.worldId,scope.roomId,scope.battleProgram))return false;
    const auto part=SmDecode(target[2]);std::uint32_t owner=0;
    if(!part || !ReadHitTrace(part+0x118,owner) || owner!=target[0])return false;
    uintptr_t actor=0;std::uint16_t netId=0;const R::ReactionWindow* w=nullptr;
    if(g_sm.hostActive) {
        if(!SmAxelHostCurrent() || g_axel.handle!=target[0])return false;
        actor=g_axel.actor;netId=g_axel.netId;w=g_axel.windows.Find(command);
    } else {
        w=g_axel.offers.Find(command);
        if(!w)return false;
        actor=SmDrivenActor(w->source,R::kAxelIIObjectId);netId=w->source;
        if(!actor || SmOwnHandle(actor)!=target[0])return false;
    }
    if(!w || !R::ReactionLive(*w,scope.epoch,g_frameCounter) || w->command!=command || w->slot!=scope.localSlot ||
       w->connection!=connection || w->source!=netId)return false;
    source=actor;native=part;
    id.family=3;id.resource=R::AxelResource(command);id.incarnation=w->occurrence;id.occurrence=w->occurrence;
    id.source=netId;id.objectId=R::kAxelIIObjectId;
    return true;
}
static bool SmAxelKnownSource(std::uint32_t handle) noexcept {
    return handle && SmAxelHostCurrent() && g_axel.handle==handle;
}

// Host: consume one window, bind the executor, then run the original callback with that seat's actor.
static bool SmAxelComplete(const enemysync::ScriptReactionCompletionRecord& claim,const std::uint32_t* nativeTarget) {
    const char* why="unknown";
    const auto refused=[&]() {
        if(SmAxelLogBudget())Log("[axel-rc] complete refused occurrence=%llu command=%u owner=%u reason=%s",
            claim.occurrence,claim.command,claim.ownerSlot,why);
        return false;
    };
    std::uint16_t command=0;enemysync::DownedScope scope;
    if(!D::Check(g_smReactionCallback!=nullptr,"callback-ready",why) || !D::Check(SmAxelHostCurrent(),"host-axel",why) ||
       !D::Check(SmOwnerScope(scope),"owner-scope",why) || !D::Check(scope.localSlot==0,"local-host-seat",why) ||
       !D::Check(R::AxelResourceFor(claim.resource,claim.objectId,command) && command==claim.command,"axel-resource",why) ||
       !D::Check(claim.netId==g_axel.netId,"axel-source",why))return refused();
    auto* window=g_axel.windows.Find(command);
    if(!window){why="window-missing";return refused();}
    const auto seats=SmOwnerLivingSeats(scope);
    const auto before=*window;
    const R::ReactionCompletion completion{claim.occurrence,claim.requesterConnectionId,claim.epoch,claim.netId,
        claim.command,claim.ownerSlot};
    if(!R::ConsumeReaction(*window,completion,scope.epoch,g_frameCounter,seats)) {
        why=SmRcConsumeReason(before,completion,scope.epoch,g_frameCounter,seats);return refused();
    }
    const auto player=SmOwnerSeatActor(claim.ownerSlot);
    const auto playerHandle=SmOwnHandle(player);
    if(!D::Check(playerHandle!=0,"seat-actor",why))return refused(); // remains consumed
    SmAxelEndExecutor("superseded");
    g_axel.executor={command,claim.netId,claim.ownerSlot,claim.requesterConnectionId,scope.epoch,g_frameCounter,false};
    g_axel.executorActor=player;g_axel.executorHandle=playerHandle;
    const std::uint32_t fallback[5]{g_axel.handle,0,0,UINT32_MAX,0};
    const auto receiver=g_axel.actor;
    if(SmAxelLogBudget())
        Log("[axel-rc] complete source=%u resource=%u occurrence=%llu command=%u owner=%u host-press=%u",
            claim.netId,claim.resource,claim.occurrence,command,claim.ownerSlot,nativeTarget?1u:0u);
    g_smReactionCallback(receiver,player,command,nativeTarget?nativeTarget:fallback);
    return true;
}
// Either seat's native callback for Axel II. Handled (true) whenever the receiver is Axel II and the command is
// one of his: it runs only for the seat that owns the live window, and never with another seat's window.
static bool SmAxelReactionCallback(uintptr_t receiver,uintptr_t player,std::uint32_t command,const std::uint32_t* target) {
    const int index=R::AxelCommandIndex(command);
    if(index<0 || !SmAxelReceiver(receiver))return false;
    enemysync::DownedScope scope;
    const char* why="unknown";
    const auto refuse=[&]() {
        if(SmAxelLogBudget())Log("[axel-rc] press refused command=%u seat=%u reason=%s",command,
            scope.localSlot<3?scope.localSlot:0xFFu,why);
        return true;
    };
    if(!D::Check(SmOwnerScope(scope),"owner-scope",why) ||
       !D::Check(R::AxelEncounter(scope.worldId,scope.roomId,scope.battleProgram),"encounter",why) ||
       !D::Check(player==g_soraActor,"canonical-player",why))return refuse();
    const auto cmd=static_cast<std::uint16_t>(command);
    R::ReactionWindow* window=nullptr;
    if(g_sm.hostActive) {
        if(!D::Check(SmAxelHostCurrent() && receiver==g_axel.actor,"host-axel",why))return refuse();
        window=g_axel.windows.Find(cmd);
    } else {
        window=g_axel.offers.Find(cmd);
        if(!D::Check(window && SmDrivenActor(window->source,R::kAxelIIObjectId)==receiver,"client-axel",why))return refuse();
    }
    const auto connection=scope.connections[scope.localSlot];
    const auto decision=R::AxelPressDecision(window,cmd,scope.localSlot,connection,scope.epoch,g_frameCounter,
        downedspike::LogicallyDowned());
    if(decision==R::AxelPress::Refused) {
        why=!window?"no-window":window->slot!=scope.localSlot?"other-seat-window":"window-not-live";return refuse();
    }
    const enemysync::ScriptReactionCompletionRecord claim{window->occurrence,connection,R::AxelResource(cmd),
        R::kAxelIIObjectId,scope.epoch,window->source,cmd,scope.localSlot};
    if(decision==R::AxelPress::Execute){(void)SmAxelComplete(claim,target);return true;}
    window->consumed=true;
    const bool queued=enemysync::SendScriptReactionCompletion(claim);
    if(SmAxelLogBudget())
        Log("[axel-rc] local completion source=%u resource=%u occurrence=%llu command=%u owner=%u queued=%u",
            claim.netId,claim.resource,claim.occurrence,cmd,claim.ownerSlot,queued?1u:0u);
    return true;
}
