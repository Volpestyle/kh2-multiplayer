// Included inside ScriptMirror after ScriptActorOwner. VUH-2913: Axel II's collision-offered reaction commands
// (AxelReaction.hpp, docs/AXEL_REACTIONS.md). The host mints one owner per offer while host Axel II is live; each
// seat's press, the host's own included, acts only on a live window of its own seat; sysobj_player inside Axel's
// script resolves to the seat that ran the RC. A friend's RC runs on the host with that seat's actor, streams to the
// friend through a ScriptActorOwner role (SmOwnerBeginAxelRole), and the safe abort (R::AxelActAdmitted) refuses Axel's
// attach or capture onto anyone but the bound executor's resolving actor. Native state is only read here, except the
// original callback and the native traps the hooks own.
struct SmAxelState {
    // host
    uintptr_t actor{};
    std::uint32_t handle{},epoch{};
    std::uint16_t netId{};
    R::AxelWindows windows{};
    R::AxelExecutor executor{};
    uintptr_t executorActor{};
    std::uint32_t executorHandle{};
    bool unresolvedLogged{}; // once per executor
    bool chainUnread{};      // logged on the change
    int heldSent{-1};        // the friend role's last attachment record: -1 none, 0 detach, 1 attach
    R::AxelCameraCustody cameraCustody{}; // R::AxelCameraStepFor: whose this RC's camera sequence is
    bool inFriendCallback{}; // a friend's completion is running the native callback (SmAxelComplete)
    bool watchPending{};     // Burst's 1/207, waiting for the callback's end (SmAxelFlushWatch)
    R::AxelCameraStep watchStep{};
    std::uint64_t watchArgs[1]{};
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
    g_axel.executor={};g_axel.executorActor=0;g_axel.executorHandle=0;g_axel.unresolvedLogged=false;g_axel.chainUnread=false;
    g_axel.heldSent=-1;g_axel.cameraCustody=R::AxelCameraCustody::Undecided;
}
// Client: the host's offers and identities belong to the room and session they came in; ScriptMirror's room, load,
// transition and session resets end them.
static void SmAxelClientReset() {
    g_axel.offers.Reset();g_axel.identities={};
}
static void SmAxelHostReset() {
    if(!g_axel.actor && !g_axel.executor.command)return;
    SmAxelEndExecutor("host-axel-gone");
    g_axel.windows.Reset();g_axel.actor=0;g_axel.handle=0;g_axel.netId=0;g_axel.epoch=0;
}
static bool SmAxelExecutorBound(const enemysync::DownedScope& scope) {
    return g_axel.executor.command && R::AxelExecutorLive(g_axel.executor,scope.epoch,g_frameCounter);
}
// The native side of R::AxelReadMasks, R::AxelReadActor, R::AxelReadContact and R::AxelReadChain.
struct SmAxelReader {
    template<class T> bool operator()(uintptr_t address,T& value) const { return ReadHitTrace(address,value); }
};
static uintptr_t SmAxelDecode(std::uint32_t handle) noexcept { return SmDecode(handle); }
static R::AxelActorState SmAxelExecutorActor() {
    return R::AxelReadActor(SmAxelReader{},SmAxelDecode,g_axel.executorActor,g_axel.executorHandle);
}
// 1/3 inside Axel's own script (R::AxelPlayerRouteFor): Executor fills handle with the executor's own actor.
static R::AxelPlayerRoute SmAxelPlayerRoute(uintptr_t source,const enemysync::DownedScope& scope,std::uint32_t& handle) {
    handle=0;
    if(!g_sm.hostActive || !source || source!=g_axel.actor)return R::AxelPlayerRoute::Choice;
    const auto state=SmAxelExecutorActor();
    const auto route=R::AxelPlayerRouteFor(SmAxelExecutorBound(scope),state==R::AxelActorState::Resolves);
    if(route==R::AxelPlayerRoute::Executor)handle=g_axel.executorHandle;
    if(route==R::AxelPlayerRoute::Unresolved && !g_axel.unresolvedLogged && SmAxelLogBudget()) {
        g_axel.unresolvedLogged=true;
        Log("[axel-rc] player unresolved command=%u owner=%u actor=%s: 1/3 keeps the native result",
            g_axel.executor.command,g_axel.executor.slot,state==R::AxelActorState::Unknown?"unread":"gone");
    }
    return route;
}
// The Axel part of 1/3 (SmOwnerPlayer), with the native result already in args[0]: true once it has decided.
static bool SmAxelPlayer(std::uint64_t* args,uintptr_t source,const enemysync::DownedScope& scope) {
    std::uint32_t handle=0;
    const auto route=SmAxelPlayerRoute(source,scope,handle);
    if(route==R::AxelPlayerRoute::Executor)args[0]=R::Tagged(R::kTagAddress,handle);
    return route!=R::AxelPlayerRoute::Choice;
}
// Host: whether this actor is a player's, whatever its targeting eligibility. A downed or otherwise ineligible host
// leaves the candidate table yet stays 1/3's native result (Ione's 75233daf), so the canonical player and the party
// puppets' actors count as well as the candidates.
static bool SmAxelPlayerActor(uintptr_t actor) {
    uintptr_t canonical=0;
    return actor && (SmOwnerActorSeat(actor)>=0 || actor==g_soraActor ||
        (ReadHitTrace(g_exeBase+enemytarget::RVA_NATIVE_PLAYER,canonical) && canonical==actor) ||
        actor==PuppetTarget(0) || actor==PuppetTarget(1));
}
// The safe abort (R::AxelActAdmitted) for host Axel acting on the player argument. A player argument that does not
// decode counts as a player: the native trap would fault on it (42C576, 42D187).
static R::AxelAct SmAxelActOn(uintptr_t axel,std::uint64_t player,const char* trap) {
    if(!g_sm.hostActive || !axel || !SmAxelHostCurrent() || axel!=g_axel.actor)return R::AxelAct::Native;
    std::uint32_t ignored=0;
    const auto target=SmActorOf(player,ignored);
    enemysync::DownedScope scope;
    const bool bound=SmOwnerScope(scope) && SmAxelExecutorBound(scope);
    const auto state=bound?SmAxelExecutorActor():R::AxelActorState::Gone;
    const bool executor=target && target==g_axel.executorActor;
    const auto act=R::AxelActAdmitted(true,!target || executor || SmAxelPlayerActor(target),bound,state,executor);
    if(act==R::AxelAct::Refused && SmAxelLogBudget())
        Log("[axel-rc] %s refused command=%u owner=%u target=%s executor=%s",trap,g_axel.executor.command,
            g_axel.executor.slot,!target?"undecoded":executor?"executor":"other-seat",
            !bound?"unbound":state==R::AxelActorState::Unknown?"unread":state==R::AxelActorState::Gone?"gone":"resolves");
    return act;
}
// 1/18 attach and 1/29 capture, on the trap's copied arguments: both take Axel first (the attach's child, the
// capture's source) and the player second (the parent, the captured actor). Refused: the hook skips the native call.
static R::AxelAct SmAxelAct(const std::uint64_t* args,const char* trap) {
    if(!g_sm.hostActive || !args || !SmAxelHostCurrent())return R::AxelAct::Native; // nothing read elsewhere
    std::uint32_t ignored=0;
    return SmAxelActOn(SmActorOf(args[0],ignored),args[1],trap);
}
// 1/101 obj_motion_is_capture in Axel's own script (idle_after_rc), on its copied argument: the player, 1/3's result.
// Refused: the hook answers "not captured" without reading, so the wait ends.
static R::AxelAct SmAxelCaptureQuery(const std::uint64_t* args) {
    if(!g_sm.hostActive || !args || !SmAxelHostCurrent())return R::AxelAct::Native; // nothing read elsewhere
    std::uint16_t netId=0;std::uint32_t objectId=0;
    return SmAxelActOn(SmOwnerVmSource(netId,objectId),args[0],"capture-query");
}
// R::AxelRoleHeld for a ScriptActorOwner role on the host: an Axel II role with no thread, still the bound
// executor's seat and actor, whose actor no coroutine holds.
static bool SmAxelRoleHeld(std::uint32_t objectId,std::uint8_t slot,uintptr_t actor,uintptr_t roleThread) {
    if(!g_sm.hostActive || objectId!=R::kAxelIIObjectId || roleThread || !actor)return false;
    enemysync::DownedScope scope;uintptr_t thread=0;
    const bool read=ReadHitTrace(actor+0xD48,thread);
    return SmOwnerScope(scope) && R::AxelRoleHeld(SmAxelExecutorBound(scope),g_axel.executor.slot,g_axel.executorActor,
        slot,actor,read,thread);
}
// The Axel role's own records in ScriptActorOwner's stream, built only here so that what they carry is what the wire
// rules admit (R::ValidCall, validScriptCallEntry; tests/AxelReactionAdapterTest.cpp runs them through both).
static R::Call SmAxelHoldRecord(const ScriptActorRole& role,bool held) {
    auto call=SmOwnerRecord(role,held?13:14);
    if(held){call.values[0]=R::Tagged(R::kTagInt,R::kAxelAttachFlags);call.values[1]=R::Tagged(R::kTagInt,R::kAxelAttachBone);}
    return call;
}
static R::Call SmAxelCameraRecord(const ScriptActorRole& role,std::uint32_t fourcc) {
    auto call=SmOwnerRecord(role,5);
    call.values[0]=R::Tagged(R::kTagInt,fourcc);call.values[1]=R::Tagged(R::kTagInt,R::kAxelCameraNone);
    return call;
}
static R::Call SmAxelCameraOpRecord(const ScriptActorRole& role,R::AxelCameraKind kind,float argument) {
    auto call=SmOwnerRecord(role,17);
    const auto k=static_cast<std::uint32_t>(kind);
    std::uint32_t bits=0;std::memcpy(&bits,&argument,sizeof(bits));
    call.values[0]=R::Tagged(R::kTagInt,k);call.values[1]=k<=2?R::Tagged(R::kTagInt,0):R::Tagged(R::kTagFloat,bits);
    return call;
}
// Host: the friend executor's current owner role on its actor, or none: its role with no thread while held
// (SmOwnerBeginAxelRole, SmAxelRoleHeld), or, with anyThread, Burst's coroutine role from its own 1/112 hook too.
static const ScriptActorRole* SmAxelFriendRole(std::uint8_t slot,uintptr_t actor,bool anyThread=false) {
    enemysync::DownedScope scope;
    if(!slot || slot>=3 || !actor || !SmOwnerScope(scope))return nullptr;
    const auto& role=g_smRoles[slot];
    if(!role.resource || role.objectId!=R::kAxelIIObjectId || role.actor!=actor || !SmOwnerCurrent(role,scope))return nullptr;
    if(role.thread)return anyThread?&role:nullptr;
    return SmAxelRoleHeld(R::kAxelIIObjectId,slot,actor,0)?&role:nullptr;
}
static bool SmAxelEmitHold(std::uint8_t slot,uintptr_t actor,bool held) {
    const auto* role=SmAxelFriendRole(slot,actor);
    return role && SmEmit(SmAxelHoldRecord(*role,held));
}
static bool SmAxelEmitCamera(std::uint8_t slot,uintptr_t actor,std::uint32_t fourcc) {
    const auto* role=SmAxelFriendRole(slot,actor);
    return role && SmEmit(SmAxelCameraRecord(*role,fourcc));
}
static bool SmAxelEmitCameraOp(std::uint8_t slot,uintptr_t actor,R::AxelCameraKind kind,float argument) {
    const auto* role=SmAxelFriendRole(slot,actor,true);
    return role && SmEmit(SmAxelCameraOpRecord(*role,kind,argument));
}
// Host: whether Axel is attached to this actor with his own pair (R::AxelAttachPair): 1 held, 0 not, -1 unread.
static int SmAxelHeldBy(uintptr_t actor) {
    if(!SmAxelHostCurrent() || !actor)return -1;
    std::uint32_t parent=0;std::uint16_t flags=0,bone=0;
    if(!ReadHitTrace(g_axel.actor+R::kAxelParent,parent))return -1;
    if(!parent || SmDecode(parent)!=actor)return 0;
    if(!ReadHitTrace(g_axel.actor+0x6A4,flags) || !ReadHitTrace(g_axel.actor+0x6A6,bone))return -1;
    return R::AxelAttachPair(flags,bone)?1:0;
}
// Host: a friend executor's hold travels to its owner as its role's attachment record, on each change. An unread
// parent sends nothing.
static void SmAxelSyncHeld() {
    if(!g_axel.executor.command || !g_axel.executor.slot || g_axel.executor.slot>=3 || !g_axel.executorActor)return;
    const int held=SmAxelHeldBy(g_axel.executorActor);
    if(held<0 || held==g_axel.heldSent)return;
    if(SmAxelEmitHold(g_axel.executor.slot,g_axel.executorActor,held==1))g_axel.heldSent=held;
}
// Client: Axel II's pair is the reverse of Thorn's: the Axel clone (the role's source) is the child of the friend's own
// player (the role's actor) at bone 1, flags 16386, with no callback. It is this role's only with its receipt (Ione's
// 75233daf, as SmOwnerHasAttachment requires role.attached): a matching pair it never made is another activation's.
static bool SmAxelCloneHeld(const ScriptActorRole& role) {
    std::uint32_t parent=0;std::uint16_t flags=0,bone=0;uintptr_t callback=0;
    return role.axelHeld && role.actor && role.source && ReadHitTrace(role.source+0x6A0,parent) && parent &&
        SmDecode(parent)==role.actor && ReadHitTrace(role.source+0x6A4,flags) && ReadHitTrace(role.source+0x6A6,bone) &&
        R::AxelAttachPair(flags,bone) && ReadHitTrace(role.source+0x6B0,callback) && !callback;
}
// Client op 13/14 and cleanup: attach or detach our own hold of the Axel clone. It never replaces or removes another
// attachment.
static bool SmAxelApplyHold(ScriptActorRole& role,bool hold) noexcept {
    if(!SmOwnerReady() || g_sm.hostActive || role.objectId!=R::kAxelIIObjectId || !SmOwnerActorCurrent(role) ||
       !role.actor || !role.source)return false;
    const auto child=SmOwnHandle(role.source),parent=SmOwnHandle(role.actor);
    if(!child || !parent)return false;
    std::uint32_t now=0;
    if(!ReadHitTrace(role.source+0x6A0,now))return false;
    if(hold && now)return SmAxelCloneHeld(role); // idempotent for our own hold only
    if(!hold && !now){role.axelHeld=false;return true;}
    if(!hold && !SmAxelCloneHeld(role))return false;
    __try {
        if(hold) {
            role.axelHeld=true; // reserve cleanup authority before entry: a fault can leave a native attachment
            std::uint64_t args[]{R::Tagged(R::kTagAddress,child),R::Tagged(R::kTagAddress,parent),
                R::Tagged(R::kTagInt,R::kAxelAttachFlags),R::Tagged(R::kTagInt,R::kAxelAttachBone),
                R::Tagged(R::kTagInt,0),R::Tagged(R::kTagInt,0)};
            reinterpret_cast<TrapFn>(g_smOwnerTraps[4].original)(args);
            return SmAxelCloneHeld(role);
        }
        std::uint64_t args[]{R::Tagged(R::kTagAddress,child)};
        reinterpret_cast<TrapFn>(g_smOwnerTraps[5].original)(args);
        if(!ReadHitTrace(role.source+0x6A0,now) || now)return false;
        role.axelHeld=false;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){++g_sm.faults;return false;}
}
// R::AxelCameraStepFor on the host for a camera op whose object is this actor.
static R::AxelCameraStep SmAxelCameraStep(uintptr_t object) {
    const bool friendExecutor=g_axel.executor.command && g_axel.executor.slot && g_axel.executor.slot<3 && g_axel.executorActor;
    return R::AxelCameraStepFor(object && object==g_axel.actor,friendExecutor?g_axel.cameraCustody:R::AxelCameraCustody::Undecided,
        friendExecutor);
}
// After an Offer or Withhold step's send. False only for an Offer that did not go: the host plays it, and the rest
// of the sequence, natively.
static bool SmAxelCameraSent(R::AxelCameraStep step,bool sent,const char* op,std::uint32_t detail) {
    if(step==R::AxelCameraStep::Offer) {
        g_axel.cameraCustody=sent?R::AxelCameraCustody::Owner:R::AxelCameraCustody::Host;
        if(!sent) {
            if(SmAxelLogBudget())Log("[axel-rc] camera native %s command=%u owner=%u: not sent, the host keeps the sequence",
                op,g_axel.executor.command,g_axel.executor.slot);
            return false;
        }
    }
    if(SmAxelLogBudget())
        Log("[axel-rc] camera %s %s command=%u owner=%u detail=%X",sent?"forwarded":"dropped",op,g_axel.executor.command,
            g_axel.executor.slot,detail);
    return true;
}
// A camera name argument as the fourcc 3DB2F0 packs, if it is printable.
static bool SmAxelCameraName(std::uint64_t arg,std::uint32_t& fourcc) {
    char text[4]{};fourcc=0;
    const auto name=R::TagOf(arg)==R::kTagAddress?SmDecode(R::BitsOf(arg)):0;
    if(!name || !ReadHitTraceMemory(name,text,sizeof(text)))return false;
    for(unsigned i=0;i<4 && text[i];++i) {
        if(text[i]<0x20 || text[i]>0x7E)return false;
        fourcc|=static_cast<std::uint32_t>(static_cast<unsigned char>(text[i]))<<(8*i);
    }
    return fourcc!=0;
}
// Host 1/164 on its copied arguments ([0] the object, [8] the camera name). True when the hook must skip the native
// start. An offered start whose name is not a printable fourcc, or that cannot be sent, stays native.
static bool SmAxelCamera(const std::uint64_t* args) {
    if(!g_sm.hostActive || !args || !SmAxelHostCurrent())return false;
    std::uint32_t ignored=0,fourcc=0;
    const auto step=SmAxelCameraStep(SmActorOf(args[0],ignored));
    if(step==R::AxelCameraStep::Native)return false;
    const bool sent=SmAxelCameraName(args[1],fourcc) && SmAxelEmitCamera(g_axel.executor.slot,g_axel.executorActor,fourcc);
    return SmAxelCameraSent(step,sent,"start",fourcc);
}
// Host 1/207 camera_watch on its copied argument ([0] the object): Burst's L531 watches Axel. True: the hook skips
// the native watch. L531 calls it before its 1/112 hooks the player (Kit, e4a7861e), so inside a friend's callback
// the role it goes on may not exist yet; it then waits for the callback's end (SmAxelFlushWatch).
static bool SmAxelCameraWatch(const std::uint64_t* args) {
    if(!g_sm.hostActive || !args || !SmAxelHostCurrent())return false;
    std::uint32_t ignored=0;
    const auto step=SmAxelCameraStep(SmActorOf(args[0],ignored));
    if(step==R::AxelCameraStep::Native)return false;
    const auto slot=g_axel.executor.slot;
    if(!SmAxelFriendRole(slot,g_axel.executorActor,true) && g_axel.inFriendCallback && !g_axel.watchPending) {
        g_axel.watchPending=true;g_axel.watchStep=step;g_axel.watchArgs[0]=args[0];return true;
    }
    return SmAxelCameraSent(step,SmAxelEmitCameraOp(slot,g_axel.executorActor,R::AxelCameraKind::Watch,0),"watch",0);
}
// After the friend's callback: the waiting watch goes on its role (Burst's hook role), or, if this RC's sequence is
// not already the owner's and it cannot be sent, plays natively now.
static void SmAxelFlushWatch() {
    if(!g_axel.watchPending)return;
    g_axel.watchPending=false;
    using Custody=R::AxelCameraCustody;
    const auto step=g_axel.cameraCustody==Custody::Owner?R::AxelCameraStep::Withhold:
        g_axel.cameraCustody==Custody::Host?R::AxelCameraStep::Native:g_axel.watchStep;
    const bool sent=step!=R::AxelCameraStep::Native &&
        SmAxelEmitCameraOp(g_axel.executor.slot,g_axel.executorActor,R::AxelCameraKind::Watch,0);
    if(step==R::AxelCameraStep::Native || !SmAxelCameraSent(step,sent,"watch",0))SmOwnerCameraWatchNative(g_axel.watchArgs);
}
static void SmAxelExecutorTick(const enemysync::DownedScope& scope) {
    if(!g_axel.executor.command)return;
    if(!SmAxelExecutorBound(scope)){SmAxelEndExecutor(g_axel.executor.epoch!=scope.epoch?"epoch":"lease");return;}
    // An unread identity leaves the capture unknown, so it never releases the RC (R::AxelReadContact).
    const auto contact=R::AxelReadContact(SmAxelReader{},SmAxelDecode,g_axel.actor,g_axel.executorActor,SmAxelExecutorActor());
    const bool was=g_axel.executor.engaged;
    if(!R::AxelExecutorContinues(g_axel.executor,contact)){SmAxelEndExecutor("released");return;}
    if(!was && g_axel.executor.engaged && SmAxelLogBudget())
        Log("[axel-rc] executor engaged command=%u owner=%u attached=%u captured=%u",g_axel.executor.command,
            g_axel.executor.slot,contact.attached?1u:0u,contact.captured?1u:0u);
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
    if(!R::AxelHostFrameAdmitted(g_sm.hostActive,scope.localSlot,scope.worldId,scope.roomId,scope.battleProgram)) {
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
    SmAxelSyncHeld();
    R::AxelGroupMasks masks{};float xyz[3]{},yaw=0;
    const auto entity=actor+offsets::actor::ENTITY_TRANSFORM;
    R::AxelFrameFacts facts{};
    facts.observed=R::AxelReadMasks(SmAxelReader{},actor,masks) &&
        ReadHitTraceMemory(entity+offsets::entity::POS_X,xyz,sizeof(xyz)) && ReadHitTrace(entity+offsets::entity::ROT_Y,yaw);
    const auto seats=R::AxelOfferSeats(SmOwnerLivingSeats(scope),SmOwnerAxelAbortReady()); // unarmed: host only
    if(facts.observed) {
        const R::ReactionPoint origin{xyz[0],xyz[1],xyz[2]};
        R::AxelExecutorFacts chain{};
        if(SmAxelExecutorBound(scope) && g_axel.executorActor) {
            chain.current=true;chain.command=g_axel.executor.command;chain.slot=g_axel.executor.slot;
        }
        facts.known[1]=R::AxelReadChain(SmAxelReader{},SmAxelDecode,g_axel.executorActor,g_axel.executorHandle,chain);
        if(!facts.known[1]!=g_axel.chainUnread) {
            g_axel.chainUnread=!facts.known[1];
            if(SmAxelLogBudget())
                Log("[axel-rc] chain %s command=%u owner=%u frame=%u",g_axel.chainUnread?"unread: Clear Light withheld":"read",
                    g_axel.executor.command,g_axel.executor.slot,g_frameCounter);
        }
        const bool overtaker=R::AxelGroupActive(masks,R::kOvertakerGroup);
        const bool burst=R::AxelGroupActive(masks,R::kBurstFrontierGroupA) || R::AxelGroupActive(masks,R::kBurstFrontierGroupB);
        const int chainOwner=R::AxelChainOwner(chain);
        facts.active={overtaker,chainOwner>=0,burst};
        facts.winners={overtaker?R::AxelFamilyWinner(R::kOvertakerSpheres,masks,origin,yaw,seats):-1,chainOwner,
            burst?R::AxelFamilyWinner(R::kBurstFrontierSpheres,masks,origin,yaw,seats):-1};
    } else if(SmAxelLogBudget()) {
        Log("[axel-rc] observation failed source=%u frame=%u: open windows withheld",netId,g_frameCounter);
    }
    std::array<R::ReactionWindow,3> previous{};
    for(unsigned i=0;i<3;++i)if(const auto* w=g_axel.windows.Find(R::kAxelCommands[i]))previous[i]=*w;
    const unsigned opened=g_axel.windows.Frame(facts,netId,scope.epoch,g_frameCounter,seats);
    for(unsigned i=0;i<3;++i) {
        const auto command=R::kAxelCommands[i];
        const auto* now=g_axel.windows.Find(command);
        if((opened>>i)&1u) {
            const bool sent=SmAxelEmit(*now);
            if(SmAxelLogBudget())
                Log("[axel-rc] offer source=%u resource=%u occurrence=%llu command=%u owner=%u epoch=%u sent=%u",
                    netId,R::AxelResource(command),now->occurrence,command,now->slot,scope.epoch,sent?1u:0u);
        } else if(previous[i].occurrence && !now && SmAxelLogBudget()) {
            Log("[axel-rc] offer end source=%u occurrence=%llu command=%u owner=%u consumed=%u frames=%u",
                netId,previous[i].occurrence,command,previous[i].slot,previous[i].consumed?1u:0u,
                g_frameCounter-previous[i].openedFrame);
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
    std::uint64_t session=0;
    if(!enemysync::SessionIdentity(session) || !session)return false;
    const bool applied=g_axel.offers.Apply(occurrence,command,static_cast<std::uint8_t>(slot),scope.localSlot,
        identity.connection,c.netId,scope.epoch,g_frameCounter,session);
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
    const bool armed=SmOwnerAxelAbortReady();
    if(!D::Check(g_smReactionCallback!=nullptr,"callback-ready",why) || !D::Check(SmAxelHostCurrent(),"host-axel",why) ||
       !D::Check(g_axel.windows.ObservationCurrent(g_frameCounter),"observation-current",why) ||
       !D::Check(SmOwnerScope(scope),"owner-scope",why) || !D::Check(scope.localSlot==0,"local-host-seat",why) ||
       !D::Check(R::AxelResourceFor(claim.resource,claim.objectId,command) && command==claim.command,"axel-resource",why) ||
       !D::Check(claim.netId==g_axel.netId,"axel-source",why) ||
       !D::Check(R::AxelExecutionAdmitted(claim.ownerSlot,armed),"owner-seat",why))return refused();
    auto* window=g_axel.windows.Find(command);
    if(!window){why="window-missing";return refused();}
    const auto seats=SmOwnerLivingSeats(scope);
    const auto before=*window;
    const R::ReactionCompletion completion{claim.occurrence,claim.requesterConnectionId,claim.epoch,claim.netId,
        claim.command,claim.ownerSlot};
    if(!R::AxelConsume(*window,completion,scope.epoch,g_frameCounter,seats,armed)) {
        why=SmRcConsumeReason(before,completion,scope.epoch,g_frameCounter,seats);return refused();
    }
    const auto player=SmOwnerSeatActor(claim.ownerSlot);
    const auto playerHandle=SmOwnHandle(player);
    if(!D::Check(playerHandle!=0,"seat-actor",why))return refused(); // remains consumed
    SmAxelEndExecutor("superseded");
    g_axel.executor={command,claim.netId,claim.ownerSlot,claim.requesterConnectionId,scope.epoch,g_frameCounter,false};
    g_axel.executorActor=player;g_axel.executorHandle=playerHandle;
    // A friend's RC is replayed on its own player through an owner role, begun before the callback so the callback's
    // own capture reaches it (R::AxelRoleAtCompletion). The host's own RC plays on the host's game and needs none.
    if(claim.ownerSlot!=0 && R::AxelRoleAtCompletion(command) &&
       !D::Check(SmOwnerBeginAxelRole(claim.ownerSlot,player,g_axel.actor,g_axel.netId,scope),"owner-role",why)) {
        SmAxelEndExecutor("owner-role");return refused(); // remains consumed
    }
    const std::uint32_t fallback[5]{g_axel.handle,0,0,UINT32_MAX,0};
    const auto receiver=g_axel.actor;
    if(SmAxelLogBudget())
        Log("[axel-rc] complete source=%u resource=%u occurrence=%llu command=%u owner=%u host-press=%u",
            claim.netId,claim.resource,claim.occurrence,command,claim.ownerSlot,nativeTarget?1u:0u);
    g_axel.inFriendCallback=claim.ownerSlot!=0;
    g_smReactionCallback(receiver,player,command,nativeTarget?nativeTarget:fallback);
    g_axel.inFriendCallback=false;
    SmAxelFlushWatch();
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
