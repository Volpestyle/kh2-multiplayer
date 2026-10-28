// Included inside ScriptMirror. Normalized actor roles use the same reliable
// source-netId ScriptCall stream as effects/attacks. Native handles stay local.
struct ScriptOwnerVisual {
    uintptr_t actor{},status{},objentry{},bank{},anchor{};
    std::uint32_t handle{},objectId{},load{},transition{},motion{UINT32_MAX};
    int puppet{-1};
    bool captured{},attached{},wantAttachment{},applying{};
    std::uint32_t refusedLogged{UINT32_MAX};  // VUH-2580: the motion last logged as refused for this clone
    std::uint32_t installs{};  // VUH-2580: successful installs on this clone, logged at cleanup
};
struct ScriptActorRole {
    std::uint32_t resource{},objectId{},epoch{};
    const char* bindRefused{};  // VUH-2580: the last logged reason the clone could not be bound
    std::uint16_t netId{};
    std::uint8_t slot{0xFF};
    std::uint64_t connection{},deadline{};
    uintptr_t actor{},source{},thread{};
    uintptr_t actorStatus{},actorObjentry{},sourceStatus{},sourceObjentry{};
    std::uint32_t heartbeat{},motion{UINT32_MAX};
    std::uint32_t ownMotion{UINT32_MAX};  // VUH-2617: the own-bank motion this role installed on the local actor (a held receipt)
    std::uint32_t actorHandle{},sourceHandle{},load{},transition{};
    std::uint32_t actorObjectId{};
    enemysync::DownedScope admittedScope{};
    R::EntityCameraIdentity camera{};
    float position[4]{},motionTime{};
    bool havePosition{},haveMotion{};
    R::ReactionWindow reaction{};
    std::uint64_t commandIncarnation{};
    std::uint32_t commandFrame{},commandMode{};
    bool commandAnnounced{};
    bool captured{},attached{};
    bool axelHeld{};  // VUH-2913 (client): this role attached the Axel clone (its source) to its actor (SmAxelApplyHold)
    // A spectator's clone is a visual target, never the role's command actor.
    ScriptOwnerVisual visual{};
    std::uint64_t captureValues[2]{};
    bool haveCapture{};
    R::OwnerVisualHeartbeat visualHeartbeat{};  // VUH-2610: spectator only; reset with the role
};
static std::array<ScriptActorRole,3> g_smRoles{};

// VUH2707_TRACE_BEGIN
// These are copies of our own metadata and already-read scalar values, never
// native pointer follows. Every caller is on the game thread (including the
// ready/thread refusal at CameraCurrent, which stays before observation setup).
struct SmCameraObservation : R::CameraObservation {
    unsigned site{},nativeCalls{};
    std::uint32_t resource{},epoch{},load{},transition{},fourcc{};
    std::uint16_t netId{};std::uint8_t slot{0xFF};
    std::uint64_t connection{};uintptr_t actor{},source{},inputContext{},openedAsset{},openedAnchor{};
    std::uint64_t openedKind{};
};
static bool SmCameraTraceEnabled() {
    static const bool on=[](){char value[2]{};return GetEnvironmentVariableA("KH2COOP_SCRIPT_CAMERA_TRACE",value,sizeof(value))==1 && value[0]=='1';}();
    return on;
}
static R::CameraObservationBudget g_smCameraTraceBudget;
static void SmCameraTraceRole(SmCameraObservation& t,const ScriptActorRole& role) noexcept {
    if(!t.enabled)return;
    t.resource=role.resource;t.epoch=role.epoch;t.load=role.load;t.transition=role.transition;
    t.netId=role.netId;t.slot=role.slot;t.connection=role.connection;t.actor=role.actor;t.source=role.source;
    t.openedAsset=role.camera.asset;t.openedAnchor=role.camera.sourceAnchor;t.openedKind=role.camera.kindRecord;
}
static SmCameraObservation SmCameraTraceBegin(unsigned site,const ScriptActorRole* role) noexcept {
    SmCameraObservation t{};t.enabled=SmCameraTraceEnabled();t.site=site;
    if(role)SmCameraTraceRole(t,*role);
    return t;
}
template<class T> static T SmCameraTraceResult(SmCameraObservation& t,T result,const char* fallback="no-role-match") noexcept {
    if(!t.enabled)return result;
    const bool accepted=result!=0;
    const auto context=(t.observed&(1u<<R::CameraObservation::Context))?
        t.values[R::CameraObservation::Context]:t.inputContext;
    const auto* why=accepted?"qualified":!std::strcmp(t.reason,"complete")?fallback:t.reason;
    const R::CameraObservationKey key{t.site,t.nativeCalls,t.resource,t.connection,context,accepted,why};
    if(!g_smCameraTraceBudget.Take(true,key))return result;
    const auto& v=t.values;
    Log("[script-camera] site=%s result=%u reason=%s nativeCalls=%u role=%u source=%u seat=%u connection=%llu epoch=%u load=%u transition=%u frame=%u actor=%llX sourceActor=%llX requestedContext=%llX fourcc=%X observed=%X context=%llX asset=%llX anchor=%llX metadata=%llX kind=%llX inField=%llX menu=%llX frozen=%llX active=%llX state=%llX actorStatus=%llX sourceStatus=%llX thread=%llX vmSource=%llX residentAsset=%llX openedAsset=%llX openedAnchor=%llX openedKind=%llX (%u/64)",
        t.site==0?"replay":t.site==1?"opened":"current",accepted?1u:0u,why,t.nativeCalls,t.resource,t.netId,
        t.slot,static_cast<unsigned long long>(t.connection),t.epoch,t.load,t.transition,g_frameCounter,
        static_cast<unsigned long long>(t.actor),static_cast<unsigned long long>(t.source),
        static_cast<unsigned long long>(t.inputContext),t.fourcc,t.observed,
        static_cast<unsigned long long>(v[0]),static_cast<unsigned long long>(v[1]),static_cast<unsigned long long>(v[2]),
        static_cast<unsigned long long>(v[3]),static_cast<unsigned long long>(v[4]),static_cast<unsigned long long>(v[5]),
        static_cast<unsigned long long>(v[6]),static_cast<unsigned long long>(v[7]),static_cast<unsigned long long>(v[8]),
        static_cast<unsigned long long>(v[9]),static_cast<unsigned long long>(v[10]),static_cast<unsigned long long>(v[11]),
        static_cast<unsigned long long>(v[12]),static_cast<unsigned long long>(v[13]),static_cast<unsigned long long>(v[14]),
        static_cast<unsigned long long>(t.openedAsset),static_cast<unsigned long long>(t.openedAnchor),
        static_cast<unsigned long long>(t.openedKind),g_smCameraTraceBudget.Used());
    return result;
}
// VUH2707_TRACE_END

// Diagnostic copies of admitted metadata, never native VM workspaces or new
// ownership. A revoked cleanup association is explicitly distinct from active.
struct SmLifeSnapshot {
    ScriptActorRole role{};
    D::Record trace{};
    bool associated{},qualified{},revoked{};
    std::uint64_t captureSerial{};
    uintptr_t executing{};
    int executingRead{-1};
    const char* failure{"unassociated"};
};
static SmLifeSnapshot SmLifeCapturePrepare(std::uint64_t) noexcept;
static void SmLifeCaptureReturned(const SmLifeSnapshot&,std::uint64_t*) noexcept;
static void SmLifeCancelForCleanup(const ScriptActorRole&);
// Revocation ends command authority immediately. Native event shutdown remains
// asynchronous; retain only the exact old camera/actor cleanup receipt.
struct ScriptOwnerCleanup { ScriptActorRole role{}; };
static std::array<ScriptOwnerCleanup,3> g_smOwnerCleanup{};
static std::array<ScriptOwnerCleanup,3> g_smVisualCleanup{};
// VUH-2580: 0x42D0E0 only switches the clone to its idle state, which unwinds our borrowed
// capture (+2B0/+2B8/+18C 0x4000) over the next frames. A next role of the same owner admitted
// inside that window finds our own leftovers, not a foreign capture. Minted only after a completed
// cleanup of the released role, from the capture signature verified before the native idle; one use,
// by a strictly newer role of the same admission, before any intervening operation.
struct ScriptOwnerResidue {
    ScriptActorRole role{};
    uintptr_t bank{},anchor{};
    std::uint32_t motion{UINT32_MAX},frame{};
    std::uint64_t tick{};
};
static std::array<ScriptOwnerResidue,3> g_smVisualResidue{};
static std::array<bool,3> g_smVisualCleanupBusy{}; // a cleanup's native detach/idle and readback are running
// VUH-2611 (host): the boss script can capture a seat's actor (1/29) before it hooks that actor (1/112), when no
// role is current to attribute the capture to. The last such capture per seat, for the next hook of that actor.
// Only the executing script's own source may leave one, and only this load, transition and epoch may adopt it.
struct ScriptOwnerPendingCapture {
    uintptr_t actor{},source{};std::uint64_t values[2]{};
    std::uint32_t frame{},load{},transition{},epoch{};
};
static std::array<ScriptOwnerPendingCapture,3> g_smPendingCapture{};
static unsigned g_smPendingCaptureLogs=0;
static ScriptActorRole g_smVisualNativeReplay{}; // exact callback suppression while retiring native visuals
static unsigned g_smVisualBindLogs=0;
static unsigned g_smVisualDiagLogs=0;  // VUH-2580
static std::array<std::uint32_t,3> g_smRoleAdmissionFloor{};
static enemysync::DownedScope g_smRoleAdmissionScope{};
static bool SmOwnerSameAdmissionSession(const enemysync::DownedScope& scope) {
    return scope.context.generation==g_smRoleAdmissionScope.context.generation &&
        scope.context.deliverySerial==g_smRoleAdmissionScope.context.deliverySerial &&
        scope.connections[0]==g_smRoleAdmissionScope.connections[0];
}
static std::uint32_t g_smRoleSequence=0;
static bool g_smOwnerReplay=false;
static std::uint64_t g_smOwnerCommandSequence=0;
using OwnerCommandFn=uintptr_t(__fastcall*)(uintptr_t,int,std::uint32_t,const std::uint32_t*,std::uint32_t,int);
using ScriptDispatchFn=void(__fastcall*)(uintptr_t,std::uint64_t,const std::uint64_t*,std::uint32_t,uintptr_t);
static OwnerCommandFn g_smOwnerCommand=nullptr;
static OwnerCommandFn g_smOwnerCapturedCommand=nullptr;
static ScriptDispatchFn g_smScriptDispatch=nullptr;
static std::atomic<bool> g_smOwnerInstalled{false};
static bool g_smOwnerDispatchCreated=false,g_smOwnerCommandCreated=false,g_smOwnerCapturedCommandCreated=false;

static unsigned g_smOwnerCommandRefusals=0;
static std::atomic<unsigned> g_smOwnerCommandEntryLogs{0};
static bool SmOwnerReady() noexcept {
    return g_smOwnerInstalled.load(std::memory_order_acquire) &&
        g_smLifetimeReady && g_smLifetimeReliable.load();
}
static void __fastcall SmOwnerPlayer(std::uint64_t*);
static void __fastcall SmOwnerHook(std::uint64_t*);
static void __fastcall SmOwnerCapture(std::uint64_t*);
static void __fastcall SmOwnerCamera(std::uint64_t*);
static bool SmOwnerCameraAsset(uintptr_t,std::uint32_t,uintptr_t* = nullptr) noexcept;
static bool SmOwnerOpenedCamera(ScriptActorRole&,uintptr_t,std::uint32_t) noexcept;
static void __fastcall SmOwnerAttach(std::uint64_t*);
static void __fastcall SmOwnerDetach(std::uint64_t*);
static void __fastcall SmLifeMotionEnd(std::uint64_t*);
static void __fastcall SmLifeCaptureId(std::uint64_t*);
static bool SmOwnerPin(std::uint32_t,const std::uint8_t*,std::size_t);
struct OwnerTrapSlot {
    std::uint16_t index;
    std::uint32_t rva,flags;
    const std::uint8_t* body;
    std::size_t size;
    TrapFn detour;
    uintptr_t entry{},original{};
    bool swapped{};
};
static void __fastcall SmOwnerCameraGlobal(std::uint64_t*);
static void __fastcall SmOwnerCaptureQuery(std::uint64_t*);
static constexpr std::size_t kSmOwnerCoreTrapCount=6;
static OwnerTrapSlot g_smOwnerTraps[]{
    {3,0x42B230,0x40000000,R::kOwnerPlayer,sizeof(R::kOwnerPlayer),&SmOwnerPlayer},
    {112,0x42CA20,3,R::kOwnerHook,sizeof(R::kOwnerHook),&SmOwnerHook},
    {29,0x42D160,0x40000004,R::kOwnerCapture,sizeof(R::kOwnerCapture),&SmOwnerCapture},
    {157,0x42E070,3,R::kOwnerCamera,sizeof(R::kOwnerCamera),&SmOwnerCamera},
    {18,0x42C550,6,R::kOwnerAttach,sizeof(R::kOwnerAttach),&SmOwnerAttach},
    {102,0x42C5F0,1,R::kOwnerDetach,sizeof(R::kOwnerDetach),&SmOwnerDetach},
    {14,0x42B440,0x40000001,R::kOwnerMotionEndObservation,sizeof(R::kOwnerMotionEndObservation),&SmLifeMotionEnd},
    {238,0x42D3B0,0x40000001,R::kOwnerCaptureIdObservation,sizeof(R::kOwnerCaptureIdObservation),&SmLifeCaptureId},
};
// VUH-2913, optional, outside the core. camera_start_global: refused, Axel's RC cameras play natively as before.
// obj_motion_is_capture: refused, the safe abort is unarmed and only the host executes Axel's RCs.
static OwnerTrapSlot g_smAxelCameraSlot{164,0x42E0D0,0x2,R::kOwnerCameraGlobal,sizeof(R::kOwnerCameraGlobal),&SmOwnerCameraGlobal};
static OwnerTrapSlot g_smAxelCaptureQuerySlot{101,0x42D360,0x40000001,R::kOwnerCaptureQuery,sizeof(R::kOwnerCaptureQuery),
    &SmOwnerCaptureQuery};
// R::AxelExecutionAdmitted's abortArmed: every trap that takes 1/3's result is gated, the core's 1/18 and 1/29 and
// the optional 1/101 (Ione's 75233daf).
static bool SmOwnerAxelAbortReady() noexcept { return SmOwnerReady() && g_smAxelCaptureQuerySlot.swapped; }
static bool SmOwnerSwapOptional(OwnerTrapSlot& slot,uintptr_t table) {
    std::uint32_t flags=0;slot.entry=R::EntryRva(table,slot.index);
    slot.swapped=ReadHitTrace(slot.entry,slot.original) && slot.original==g_exeBase+slot.rva &&
        ReadHitTrace(slot.entry+8,flags) && flags==slot.flags && SmOwnerPin(slot.rva,slot.body,slot.size) &&
        InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(slot.entry),reinterpret_cast<void*>(slot.detour),
            reinterpret_cast<void*>(slot.original))==reinterpret_cast<void*>(slot.original);
    return slot.swapped;
}
static void SmOwnerRestoreSlot(OwnerTrapSlot& slot) {
    if(!slot.swapped)return;
    const auto previous=InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(slot.entry),
        reinterpret_cast<void*>(slot.original),reinterpret_cast<void*>(slot.detour));
    if(previous==reinterpret_cast<void*>(slot.detour) || previous==reinterpret_cast<void*>(slot.original))slot.swapped=false;
    else Log("[script-owner] restore slot changed index=%u",slot.index);
}
static bool SmOwnerScope(enemysync::DownedScope& scope,const char** failure=nullptr) {
    const char* why="scope-current";
    const bool current=D::Check(SmGameThread(),"scope-game-thread",why) &&
        D::Check(!warp::TransitionPending(),"scope-transition-pending",why) &&
        D::Check(enemysync::CaptureDownedScope(scope),"scope-capture-downed",why);
    if(!current && failure)*failure=why;
    return current;
}
static uintptr_t SmOwnerVmSource(std::uint16_t& netId,std::uint32_t& objectId) {
    netId=0;objectId=0;
    uintptr_t vm=0,actor=0,entry=0;
    if(!g_sm.hostActive || !SmGameThread() ||
       !ReadHitTrace(g_exeBase+0x2A250B0,vm) || !vm || !ReadHitTrace(vm+0x60,actor) || !actor ||
       !ReadHitTrace(actor+offsets::actor::OBJENTRY_PTR,entry) || !entry ||
       !ReadHitTrace(entry+offsets::objentry::OBJECT_ID,objectId))return 0;
    netId=enemysync::HostEnemyNetId(actor,objectId);
    return netId && enemymirror::FamilyAllowed(objectId)?actor:0;
}
static uintptr_t SmOwnerSeatActor(std::uint8_t slot) {
    const std::array<R::ReactionActorCandidate,3> candidates{{
        {enemytarget::g.candidateActor[0],0},
        {enemytarget::g.candidateActor[1],enemytarget::g.ownerSlot[1]},
        {enemytarget::g.candidateActor[2],enemytarget::g.ownerSlot[2]}}};
    return R::ReactionActorForSeat(slot,0,enemytarget::g.candidateActor[0],candidates);
}
static int SmOwnerActorSeat(uintptr_t actor) {
    for(unsigned i=0;i<3;++i)if(enemytarget::g.candidateActor[i]==actor && actor)
        return i?enemytarget::g.ownerSlot[i]:0;
    return -1;
}
static bool SmOwnerActorCurrent(const ScriptActorRole& role,const char** failure=nullptr) {
    uintptr_t status=0,entry=0;std::uint32_t objectId=0;const char* why="unknown";
    const auto fail=[&](){if(failure)*failure=why;return false;};
    if(!D::Check(role.source!=0,"source-zero",why) || !D::Check(role.sourceHandle!=0,"source-handle-zero",why) ||
       !D::Check(SmOwnHandle(role.source)==role.sourceHandle,"source-handle-replaced",why) ||
       !D::Check(ReadHitTrace(role.source+0x5C0,status),"source-status-unreadable",why) ||
       !D::Check(status==role.sourceStatus,"source-status-replaced",why) ||
       !D::Check(ReadHitTrace(role.source+offsets::actor::OBJENTRY_PTR,entry),"source-entry-unreadable",why) ||
       !D::Check(entry==role.sourceObjentry,"source-entry-replaced",why) || !D::Check(entry!=0,"source-entry-zero",why) ||
       !D::Check(ReadHitTrace(entry+offsets::objentry::OBJECT_ID,objectId),"source-id-unreadable",why) ||
       !D::Check(objectId==role.objectId,"source-id-replaced",why))return fail();
    if(!role.actor)return true; // metadata for a different seat never writes an actor
    if(!D::Check(role.actorHandle!=0,"actor-handle-zero",why) ||
       !D::Check(SmOwnHandle(role.actor)==role.actorHandle,"actor-handle-replaced",why) ||
       !D::Check(ReadHitTrace(role.actor+0x5C0,status),"actor-status-unreadable",why) ||
       !D::Check(status==role.actorStatus,"actor-status-replaced",why) ||
       !D::Check(ReadHitTrace(role.actor+offsets::actor::OBJENTRY_PTR,entry),"actor-entry-unreadable",why) ||
       !D::Check(entry==role.actorObjentry,"actor-entry-replaced",why) || !D::Check(entry!=0,"actor-entry-zero",why) ||
       !D::Check(ReadHitTrace(entry+offsets::objentry::OBJECT_ID,objectId),"actor-id-unreadable",why) ||
       !D::Check(objectId==role.actorObjectId,"actor-id-replaced",why))return fail();
    return true;
}
static bool SmOwnerCaptureCurrent(const ScriptActorRole& role) {
    uintptr_t capture=0;std::uint32_t captureFlags=0,actorFlags=0;
    return role.captured && role.actor && ReadHitTrace(role.actor+0x2B0,capture) && capture==role.source &&
        ReadHitTrace(role.actor+0x6C8,captureFlags) && (captureFlags&0x80) &&
        ReadHitTrace(role.actor+0x18C,actorFlags) && (actorFlags&0x4000);
}
static bool SmOwnerBindActor(ScriptActorRole& role) {
    role.sourceHandle=SmOwnHandle(role.source);
    if(!role.sourceHandle || !ReadHitTrace(role.source+0x5C0,role.sourceStatus) || !role.sourceStatus ||
       !ReadHitTrace(role.source+offsets::actor::OBJENTRY_PTR,role.sourceObjentry) || !role.sourceObjentry)return false;
    if(!role.actor)return true;
    role.actorHandle=SmOwnHandle(role.actor);
    return role.actorHandle && ReadHitTrace(role.actor+0x5C0,role.actorStatus) && role.actorStatus &&
        ReadHitTrace(role.actor+offsets::actor::OBJENTRY_PTR,role.actorObjentry) && role.actorObjentry &&
        ReadHitTrace(role.actorObjentry+offsets::objentry::OBJECT_ID,role.actorObjectId);
}
static bool SmOwnerCurrent(const ScriptActorRole& role,const enemysync::DownedScope& scope,
    const char** failure=nullptr,D::Record* trace=nullptr) {
    const char* why="role-current";
    const auto fail=[&](){if(failure)*failure=why;return false;};
    if(role.actor && role.actor==g_soraActor && downedspike::LogicallyDowned()){why="local-down";return fail();}
    uintptr_t currentThread=0;
    if(role.actor) {
        const bool read=ReadHitTrace(role.actor+0xD48,currentThread);
        if(trace){trace->observedRead=read?1:0;trace->expected=role.thread;trace->observed=currentThread;}
        if(!read || currentThread!=role.thread){why=read?"coroutine-replaced":"coroutine-unreadable";return fail();}
    }
    if(!D::Check(role.resource!=0,"role-resource",why) || !D::Check(role.slot<3,"role-slot",why) ||
       !D::Check(role.epoch==scope.epoch,"role-epoch",why) ||
       !D::Check(enemysync::ScriptCameraScopeCurrent(role.admittedScope),"admitted-scope",why) ||
       !D::Check(role.connection!=0,"role-connection-zero",why) ||
       !D::Check(scope.connections[role.slot]==role.connection,"role-connection-replaced",why) ||
       !SmOwnerActorCurrent(role,&why) ||
       !D::Check(g_sm.hostActive?enemysync::HostEnemyNetId(role.source,role.objectId)==role.netId:
            SmDrivenActor(role.netId,role.objectId)==role.source,"source-binding",why))return fail();
    return true;
}
static ScriptActorRole* SmOwnerRoleForSource(uintptr_t source) {
    uintptr_t executing=0,parent=0;
    (void)ReadHitTrace(g_exeBase+0x2A250B0,executing);
    (void)ReadHitTrace(g_exeBase+0x2A25048,parent);
    std::array<R::ActorRoleFacts,3> facts{};
    for(unsigned i=0;i<g_smRoles.size();++i) {
        const auto& role=g_smRoles[i];uintptr_t thread=0;
        facts[i]={role.source,role.thread,role.resource && role.thread && SmOwnerActorCurrent(role) &&
            ReadHitTrace(role.actor+0xD48,thread) && thread==role.thread,role.captured};
    }
    const auto index=R::SelectActorRole(source,executing,parent,facts);
    return index>=0?&g_smRoles[static_cast<unsigned>(index)]:nullptr;
}
static R::Call SmOwnerRecord(const ScriptActorRole& role,std::uint16_t operation) {
    R::Call call{};call.netId=role.netId;call.objectId=role.objectId;call.effect=role.resource;
    call.trap=static_cast<std::uint8_t>(R::FindTrap(255,operation));return call;
}
static std::uint64_t SmOwnerFloat(float value) {
    std::uint32_t bits=0;std::memcpy(&bits,&value,sizeof(bits));return R::Tagged(R::kTagFloat,bits);
}
static bool SmOwnerBegin(ScriptActorRole& role) {
    auto call=SmOwnerRecord(role,2);
    call.values[0]=R::Tagged(R::kTagInt,role.slot);
    call.values[1]=R::Tagged(R::kTagInt,static_cast<std::uint32_t>(role.connection));
    call.values[2]=R::Tagged(R::kTagInt,static_cast<std::uint32_t>(role.connection>>32));
    call.values[3]=R::Tagged(R::kTagInt,60);
    return SmEmit(call);
}
static bool SmOwnerNativePlayer(uintptr_t actor,std::uint32_t rva) noexcept {
    const auto handle=SmOwnHandle(actor);
    if(!handle)return false;
    __try {std::uint64_t args[]{R::Tagged(R::kTagAddress,handle)};
        reinterpret_cast<TrapFn>(g_exeBase+rva)(args);return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
// Only this native pair is qualified: the admitted player is attached to the
// admitted source at bone 5. Client attachments never carry VM callbacks/data.
static bool SmOwnerHasAttachment(const ScriptActorRole& role,bool noCallback) {
    std::uint32_t parent=0;std::uint16_t flags=0,bone=0;uintptr_t callback=0;
    return role.attached && role.actor && role.source &&
        ReadHitTrace(role.actor+0x6A0,parent) && parent && SmDecode(parent)==role.source &&
        ReadHitTrace(role.actor+0x6A4,flags) && flags==16386 &&
        ReadHitTrace(role.actor+0x6A6,bone) && bone==5 &&
        (!noCallback || (ReadHitTrace(role.actor+0x6B0,callback) && !callback));
}
static bool SmOwnerApplyAttachment(ScriptActorRole& role,bool attach) noexcept {
    if(!SmOwnerReady() || !SmOwnerActorCurrent(role) || !role.actor || !role.source)return false;
    const auto actor=SmOwnHandle(role.actor),source=SmOwnHandle(role.source);
    if(!actor || !source)return false;
    std::uint32_t parent=0;
    if(!ReadHitTrace(role.actor+0x6A0,parent))return false;
    if(attach && parent)return SmOwnerHasAttachment(role,true); // idempotent; never replace another attachment
    if(!attach && !parent){role.attached=false;return true;}
    if(!attach && !SmOwnerHasAttachment(role,true))return false;
    __try {
        if(attach) {
            // Reserve cleanup authority before entry: a fault can leave a native attachment.
            role.attached=true;
            std::uint64_t args[]{R::Tagged(R::kTagAddress,actor),R::Tagged(R::kTagAddress,source),
                R::Tagged(R::kTagInt,16386),R::Tagged(R::kTagInt,5),
                R::Tagged(R::kTagInt,0),R::Tagged(R::kTagInt,0)};
            reinterpret_cast<TrapFn>(g_smOwnerTraps[4].original)(args);
            return SmOwnerHasAttachment(role,true);
        }
        std::uint64_t args[]{R::Tagged(R::kTagAddress,actor)};
        reinterpret_cast<TrapFn>(g_smOwnerTraps[5].original)(args);
        if(!ReadHitTrace(role.actor+0x6A0,parent) || parent)return false;
        role.attached=false;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){++g_sm.faults;return false;}
}
static ScriptActorRole SmOwnerVisualActor(const ScriptActorRole& role) {
    auto target=role;
    target.actor=role.visual.actor;target.actorHandle=role.visual.handle;
    target.actorStatus=role.visual.status;target.actorObjentry=role.visual.objentry;
    target.actorObjectId=role.visual.objectId;target.thread=0;
    target.captured=role.visual.captured;target.attached=role.visual.attached;
    return target;
}
// Native/allocation proof only, also usable after connection/lease revocation.
// A replacement coroutine is never ours to cancel, detach or reset.
static bool SmOwnerVisualRoots(const ScriptActorRole& role) {
    const auto& visual=role.visual;uintptr_t thread=0;
    if(!SmGameThread() || warp::TransitionPending() || warp::LoadPending() ||
       visual.load!=warp::LoadSerial() || visual.transition!=warp::TransitionSerial() ||
       !visual.actor || visual.actor==g_soraActor || visual.puppet<0 || visual.puppet>1 ||
       PuppetTarget(visual.puppet)!=visual.actor || !ReadHitTrace(visual.actor+0xD48,thread) || thread)return false;
    return SmOwnerActorCurrent(SmOwnerVisualActor(role));
}
static bool SmOwnerVisualBank(const ScriptActorRole& role) {
    uintptr_t bank=0,sourceBank=0;
    return role.visual.captured && role.visual.bank && SmOwnerVisualRoots(role) &&
        ReadHitTrace(role.visual.actor+0x2B8,bank) && bank==role.visual.bank &&
        ReadHitTrace(role.source+ACTOR_MOTCTRL,sourceBank) && sourceBank==bank &&
        SmOwnerCaptureCurrent(SmOwnerVisualActor(role));
}
static bool SmOwnerVisualPose(const ScriptActorRole& role,const enemysync::DownedScope& scope,int index) {
    if(g_sm.hostActive || role.slot==scope.localSlot || index<0 || index>1 || !IsPuppetActive(index))return false;
    const auto& driven=g_puppets[index];const auto& pose=driven.pose.pose;
    return driven.pose.provenance.producer==PuppetProducer::Network &&
        driven.pose.provenance.ownerConnectionId==role.connection && pose.ownerSlot==static_cast<SlotType>(role.slot) &&
        pose.worldId==scope.worldId && pose.roomId==scope.roomId && pose.hp>0 &&
        !(pose.flags&(AvatarHeld|AvatarInCutscene|AvatarDowned)) &&
        ValidPuppetProvenance(driven.pose.provenance,role.slot,index,enemysync::CapturePuppetAuthority());
}
static bool SmOwnerVisualCurrent(const ScriptActorRole& role,const enemysync::DownedScope& scope) {
    return SmOwnerReady() && role.haveCapture && GetTickCount64()<role.deadline && SmOwnerCurrent(role,scope) &&
        SmOwnerVisualRoots(role) && SmOwnerVisualPose(role,scope,role.visual.puppet);
}
// 3C8CD0 establishes these roots for an ordinary controller-bank motion:
// +2A0=actor+80, +2B0=0 and +2B8=0. A mere missing capture bit is insufficient.
enum class ScriptOwnerMotionState { Foreign, Own, Source };
static ScriptOwnerMotionState SmOwnerMotionState(const ScriptActorRole& target) {
    uintptr_t controllerActor=0,anchor=0,capture=0,bank=0,sourceBank=0,ownBank=0;
    std::uint32_t flags=0,parent=0;
    if(!target.actor || !SmOwnerActorCurrent(target) ||
       !ReadHitTrace(target.actor+ACTOR_MOTCTRL+0x10,controllerActor) || controllerActor!=target.actor ||
       !ReadHitTrace(target.actor+0x2A0,anchor) || !ReadHitTrace(target.actor+0x2B0,capture) ||
       !ReadHitTrace(target.actor+0x2B8,bank) || !ReadHitTrace(target.actor+0x18C,flags) ||
       !ReadHitTrace(target.actor+0x6A0,parent) || (parent && !SmOwnerHasAttachment(target,true)))
        return ScriptOwnerMotionState::Foreign;
    if(!capture && !bank && !(flags&0x4000) && anchor==target.actor+0x80 &&
       ReadHitTrace(target.actor+ACTOR_MOTCTRL,ownBank) && ownBank)
        return ScriptOwnerMotionState::Own;
    auto captured=target;captured.captured=true;
    if(ReadHitTrace(target.source+ACTOR_MOTCTRL,sourceBank) && sourceBank && bank==sourceBank &&
       SmOwnerCaptureCurrent(captured))return ScriptOwnerMotionState::Source;
    return ScriptOwnerMotionState::Foreign;
}
// VUH-2617 (Quill r1 on 06ff413): rules::OwnInstallQualified from the local actor right after its own-bank install,
// with the same proven root and reads as SmOwnerMotionState except +18C, which belongs to the installed motion.
static bool SmOwnerOwnInstallReadback(const ScriptActorRole& target,std::uint32_t installed) {
    R::OwnInstallFacts f{};f.installed=installed;
    uintptr_t controllerActor=0,anchor=0,ownBank=0;std::uint32_t parent=0;
    f.rootsRead=target.actor && SmOwnerActorCurrent(target) &&
        ReadHitTrace(target.actor+ACTOR_MOTCTRL+0x10,controllerActor) && ReadHitTrace(target.actor+0x2A0,anchor) &&
        ReadHitTrace(target.actor+0x2B0,f.capture) && ReadHitTrace(target.actor+0x2B8,f.bank) &&
        ReadHitTrace(target.actor+0x6A0,parent) && ReadHitTrace(target.actor+offsets::actor::ANIM_ID,f.motionNow);
    if(!f.rootsRead)return false;
    f.controllerSelf=controllerActor==target.actor;
    f.anchorOwn=anchor==target.actor+0x80;
    f.ownBankPresent=ReadHitTrace(target.actor+ACTOR_MOTCTRL,ownBank) && ownBank;
    f.parentOk=!parent || SmOwnerHasAttachment(target,true);
    return R::OwnInstallQualified(f);
}
static bool SmOwnerLocalCurrent(const ScriptActorRole& role,const enemysync::DownedScope& scope) {
    return SmOwnerReady() && !g_sm.hostActive && role.slot==scope.localSlot && role.actor==g_soraActor &&
        GetTickCount64()<role.deadline && !downedspike::LogicallyDowned() && SmOwnerCurrent(role,scope);
}
// POD-only native leaf: both targets use the original1/29 ABI, retaining the
// authored blend argument. The caller validates lifetime again after reentry.
static bool SmOwnerNativeMotion(std::uint32_t source,std::uint32_t target,
                                R::OwnerCaptureSignature capture,bool borrowed,bool visual) noexcept {
    const bool wasOur=g_inOurAnimSet,wasPuppet=g_inPuppetAnimSet;
    g_inOurAnimSet=true;if(visual)g_inPuppetAnimSet=true;
    bool ok=false;
    __try {
        if(borrowed) {
            std::uint64_t args[]{R::Tagged(R::kTagAddress,source),R::Tagged(R::kTagAddress,target),
                capture.motion,capture.blend};
            reinterpret_cast<TrapFn>(g_smOwnerTraps[2].original)(args);
        } else {
            std::uint64_t args[]{R::Tagged(R::kTagAddress,target),capture.motion,SmOwnerFloat(0)};
            reinterpret_cast<TrapFn>(g_exeBase+0x42B320)(args);
        }
        ok=true;
    } __except(EXCEPTION_EXECUTE_HANDLER){++g_sm.faults;}
    g_inOurAnimSet=wasOur;g_inPuppetAnimSet=wasPuppet;return ok;
}
static bool SmOwnerMotionCurrent(const ScriptActorRole& role,const enemysync::DownedScope& scope,bool visual) {
    return visual?SmOwnerVisualCurrent(role,scope):SmOwnerLocalCurrent(role,scope);
}
// VUH-2580: the clone still holds exactly our borrowed capture of this source: its current source bank,
// the anchor and motion our install left, D48 empty and no parent. Partial or zero states do not qualify.
static bool SmOwnerResidueExact(const ScriptActorRole& old,uintptr_t bank,uintptr_t anchor,std::uint32_t motion) {
    const auto actor=old.visual.actor;
    uintptr_t capture=0,nowBank=0,nowAnchor=0,sourceBank=0,thread=0;std::uint32_t parent=0,current=UINT32_MAX;
    auto target=SmOwnerVisualActor(old);target.captured=true;
    return bank && anchor && SmOwnerMotionState(target)==ScriptOwnerMotionState::Source &&
        ReadHitTrace(actor+0x2B0,capture) && capture==old.source &&
        ReadHitTrace(actor+0x2B8,nowBank) && nowBank==bank &&
        ReadHitTrace(old.source+ACTOR_MOTCTRL,sourceBank) && sourceBank==bank &&
        ReadHitTrace(actor+0x2A0,nowAnchor) && nowAnchor==anchor &&
        ReadHitTrace(actor+offsets::actor::ANIM_ID,current) && current==motion &&
        ReadHitTrace(actor+0xD48,thread) && !thread && ReadHitTrace(actor+0x6A0,parent) && !parent;
}
// A script capture, attach or detach that is not our replay may write any clone: no receipt survives it.
static void SmOwnerResidueInvalidate() { for(auto& r:g_smVisualResidue)r={}; }
static bool SmOwnerCleanupNames(uintptr_t actor) {
    for(unsigned i=0;i<g_smVisualCleanup.size();++i)
        if(g_smVisualCleanupBusy[i] || (g_smVisualCleanup[i].role.resource && g_smVisualCleanup[i].role.visual.actor==actor))return true;
    for(const auto& c:g_smOwnerCleanup)if(c.role.resource && (c.role.actor==actor || c.role.visual.actor==actor))return true;
    return false;
}
static bool SmOwnerInstallMotion(ScriptActorRole& role,R::OwnerCaptureSignature capture,bool borrowed,bool visual,
                                 bool newCapture=false,const char** why=nullptr) {
    // VUH-2580: every refusal names itself for the caller's bounded log.
    const auto fail=[why](const char* reason){if(why)*why=reason;return false;};
    enemysync::DownedScope scope;
    if(!SmOwnerScope(scope))return fail("scope");
    if(!SmOwnerMotionCurrent(role,scope,visual))return fail("not-current");
    if(role.reaction.consumed && !newCapture)return fail("command-consumed");
    if(visual && role.visual.applying)return fail("applying");
    auto target=visual?SmOwnerVisualActor(role):role;
    std::uint32_t eligibility=0;
    if(SmOwnerMotionState(target)==ScriptOwnerMotionState::Foreign)return fail("foreign-state");
    if(borrowed && (!ReadHitTrace(target.actor+0x6C8,eligibility) || !(eligibility&0x80)))return fail("borrow-ineligible");
    const auto resource=role.resource,sourceHandle=role.sourceHandle,targetHandle=target.actorHandle;
    const auto source=role.source,actor=target.actor;const auto connection=role.connection;
    if(visual)role.visual.applying=true;
    const bool ok=SmOwnerNativeMotion(sourceHandle,targetHandle,capture,borrowed,visual);
    if(role.resource!=resource || role.connection!=connection || role.source!=source ||
       role.sourceHandle!=sourceHandle || (visual?role.visual.actor:role.actor)!=actor ||
       (visual?role.visual.handle:role.actorHandle)!=targetHandle)return fail("replaced-during-install");
    if(visual)role.visual.applying=false;
    if(!ok)return fail("native-install");
    if(!SmOwnerScope(scope) || !SmOwnerMotionCurrent(role,scope,visual) || (role.reaction.consumed && !newCapture))
        return fail("stale-after-install");
    target=visual?SmOwnerVisualActor(role):role;
    std::uint32_t installedMotion=UINT32_MAX;
    if(!ReadHitTrace(actor+offsets::actor::ANIM_ID,installedMotion) || installedMotion!=R::BitsOf(capture.motion))
        return fail("motion-not-installed");
    const auto state=SmOwnerMotionState(target);
    if(borrowed) {
        if(state!=ScriptOwnerMotionState::Source)return fail("not-source-bank");
        if(visual) {
            if(!ReadHitTrace(actor+0x2B8,role.visual.bank))return fail("bank-read");
            if(!ReadHitTrace(actor+0x2A0,role.visual.anchor))role.visual.anchor=0; // no receipt without it
            role.visual.captured=true;role.visual.motion=R::BitsOf(capture.motion);
        } else role.captured=true;
    } else {
        // The local actor's own install is read back for the motion just installed (its +18C is that motion's);
        // a visual clone keeps the general classifier.
        if(state!=ScriptOwnerMotionState::Own &&
           (visual || !SmOwnerOwnInstallReadback(target,R::BitsOf(capture.motion))))return fail("not-own-bank");
        if(visual){role.visual.captured=false;role.visual.bank=0;role.visual.motion=R::BitsOf(capture.motion);}
        else role.ownMotion=R::BitsOf(capture.motion);  // the release's continuity receipt for this exact install
        // The capture receipt remains for matching heartbeats and teardown;
        // its native state is now own-bank, so cleanup cannot reset it again.
    }
    if(visual)++role.visual.installs;
    return true;
}
// Mint after the released role's idle returned, from the pre-call signature: the post-idle clone must
// continue it exactly, and no deferred cleanup may still write the clone.
static void SmOwnerMintResidue(const ScriptActorRole& released,uintptr_t bank,uintptr_t anchor,std::uint32_t motion) {
    if(released.slot>=g_smVisualResidue.size() || !released.visual.captured ||
       SmOwnerCleanupNames(released.visual.actor) || !SmOwnerVisualRoots(released) ||
       !SmOwnerResidueExact(released,bank,anchor,motion))return;
    g_smVisualResidue[released.slot]={released,bank,anchor,motion,g_frameCounter,GetTickCount64()};
}
static void SmOwnerPollVisualCleanup() {
    if(!SmOwnerReady() || !SmGameThread())return;
    // VUH-2580 diagnostic: each cleanup ends with a named outcome (bounded).
    const auto outcome=[](const ScriptActorRole& r,const char* what) {
        if(g_smVisualDiagLogs<64) {
            ++g_smVisualDiagLogs;
            Log("[script-visual] cleanup role=%u actor=%llX installs=%u %s",r.resource,
                static_cast<unsigned long long>(r.visual.actor),r.visual.installs,what);
        }
    };
    for(auto& pending:g_smVisualCleanup) {
        if(!pending.role.resource)continue;
        const auto released=pending.role;
        if(released.slot<g_smVisualResidue.size())g_smVisualResidue[released.slot]={};
        if(!SmOwnerVisualRoots(released)){outcome(released,"dropped: roots");pending={};continue;}
        const auto& driver=g_puppets[released.visual.puppet];
        if(IsPuppetActive(released.visual.puppet) &&
           (driver.pose.pose.ownerSlot!=static_cast<SlotType>(released.slot) || driver.pose.provenance.ownerConnectionId!=released.connection)) {
            outcome(released,"dropped: other connection");pending={};continue; // a different connection now drives this allocation
        }
        uintptr_t context=0;std::int32_t state=0;
        if(!ReadHitTrace(g_exeBase+offsets::EVENT_CONTEXT,context) ||
           !ReadHitTrace(g_exeBase+offsets::CUTSCENE_STATE,state) || context || state)continue;
        // Only our still-installed source motion may authorize native cleanup.
        if(!SmOwnerVisualBank(released)){outcome(released,"dropped: not our bank");pending={};continue;}
        auto target=SmOwnerVisualActor(released);std::uint32_t parent=0;
        if(!ReadHitTrace(target.actor+0x6A0,parent) || (parent && !SmOwnerHasAttachment(target,true))) {
            outcome(released,"dropped: foreign parent");pending={};continue;
        }
        pending={}; // callbacks cannot find authority during detach/idle
        // VUH-2580: the slot stays reserved through native detach/idle and the receipt readback.
        g_smVisualCleanupBusy[released.slot]=true;
        if(parent) {
            g_smVisualNativeReplay=released;
            const bool detached=SmOwnerApplyAttachment(target,false);
            g_smVisualNativeReplay={};
            if(!detached){g_smVisualCleanupBusy[released.slot]=false;outcome(released,"failed: detach");continue;}
        }
        if(!SmOwnerVisualBank(released)){g_smVisualCleanupBusy[released.slot]=false;outcome(released,"dropped: bank after detach");continue;}
        // VUH-2580: the receipt's signature is the verified pre-call capture, never the post-call state.
        uintptr_t bank=0,anchor=0;std::uint32_t motion=UINT32_MAX;
        const bool signed_=ReadHitTrace(target.actor+0x2B8,bank) && bank==released.visual.bank &&
            ReadHitTrace(target.actor+0x2A0,anchor) && anchor==released.visual.anchor &&
            ReadHitTrace(target.actor+offsets::actor::ANIM_ID,motion) && motion==released.visual.motion;
        g_smVisualNativeReplay=released;
        const bool idled=SmOwnerNativePlayer(target.actor,0x42D0E0);
        g_smVisualNativeReplay={};
        g_smVisualCleanupBusy[released.slot]=false;
        outcome(released,idled?"restored own bank":"failed: idle");
        if(idled && signed_)SmOwnerMintResidue(released,bank,anchor,motion);
        if(PuppetTarget(released.visual.puppet)==target.actor)
            g_puppets[released.visual.puppet].lastAnim=-1;
    }
}
static void SmOwnerRetireVisual(ScriptActorRole& role,bool native) {
    // VUH-2580: any retire of this seat ends an older receipt; only its own completed cleanup mints anew.
    if(role.slot<g_smVisualResidue.size())g_smVisualResidue[role.slot]={};
    if(!role.visual.actor)return;
    const auto released=role;role.visual={}; // authority ends before native entry
    if(SmOwnerVisualRoots(released) &&
       g_puppets[released.visual.puppet].pose.provenance.ownerConnectionId==released.connection)
        g_puppets[released.visual.puppet].lastAnim=-1;
    if(native && released.slot<g_smVisualCleanup.size() && !g_smVisualCleanup[released.slot].role.resource)
        g_smVisualCleanup[released.slot].role=released;
    SmOwnerPollVisualCleanup();
}
// VUH-2580: the clone still holds our own capture from the completed cleanup of an older role of this
// same admission (session, host connection, epoch, room, seat connection and source), and nothing ran on
// it since but its own native update. Identity is matched before any saved pointer is read; the receipt
// is consumed before the readback, so a failure never refunds it. Visual binding only.
static bool SmOwnerClaimResidue(const ScriptActorRole& role,uintptr_t actor,int puppet) {
    if(role.slot>=g_smVisualResidue.size() || !g_smVisualResidue[role.slot].role.resource)return false;
    const auto r=g_smVisualResidue[role.slot];g_smVisualResidue[role.slot]={};
    const auto& old=r.role;const auto& a=old.admittedScope;const auto& b=role.admittedScope;
    const auto now=GetTickCount64();
    const bool same=old.resource<role.resource && old.visual.actor==actor && old.visual.puppet==puppet &&
        now>=r.tick && now-r.tick<1000 && g_frameCounter-r.frame<=2 &&
        old.slot==role.slot && old.connection==role.connection && old.epoch==role.epoch &&
        old.source==role.source && old.sourceHandle==role.sourceHandle && old.sourceStatus==role.sourceStatus &&
        old.sourceObjentry==role.sourceObjentry && old.netId==role.netId && old.objectId==role.objectId &&
        a.context.generation==b.context.generation && a.context.deliverySerial==b.context.deliverySerial &&
        a.connections==b.connections && a.epoch==b.epoch &&
        a.worldId==b.worldId && a.roomId==b.roomId && a.door==b.door && a.mapProgram==b.mapProgram &&
        a.battleProgram==b.battleProgram && a.eventProgram==b.eventProgram;
    return same && PuppetTarget(puppet)==actor && !SmOwnerCleanupNames(actor) && SmOwnerVisualRoots(old) &&
        SmOwnerResidueExact(old,r.bank,r.anchor,r.motion);
}
static bool SmOwnerBindVisual(ScriptActorRole& role,const enemysync::DownedScope& scope,uintptr_t actor) {
    if(role.visual.actor)return role.visual.actor==actor && SmOwnerVisualCurrent(role,scope);
    const char* currentReason="not-checked";
    // VUH-2580: a refused bind names itself once per role and reason.
    const auto refuse=[&](const char* why) {
        if(role.bindRefused!=why && g_smVisualDiagLogs<64) {
            ++g_smVisualDiagLogs;role.bindRefused=why;
            Log("[script-visual] bind refused role=%u seat=%u actor=%llX reason=%s currentReason=%s",role.resource,role.slot,
                static_cast<unsigned long long>(actor),why,currentReason);
        }
        return false;
    };
    if(!role.haveCapture || role.slot>=3)return false;
    if(g_smVisualCleanup[role.slot].role.resource || g_smVisualCleanupBusy[role.slot])return refuse("cleanup-pending");
    if(!SmOwnerCurrent(role,scope,&currentReason))return refuse("role-not-current");
    if(GetTickCount64()>=role.deadline)return refuse("deadline");
    if(actor==g_soraActor || !IsPlayerClassActor(actor))return false;
    const auto index=PuppetIndexFor(actor);
    if(!SmOwnerVisualPose(role,scope,index))return refuse("pose");
    uintptr_t thread=0,capture=0;std::uint32_t parent=0,motionFlags=0,typeFlags=0;
    if(!ReadHitTrace(actor+0xD48,thread) || thread)return refuse("thread");
    if(!ReadHitTrace(actor+0x2B0,capture) || !ReadHitTrace(actor+0x18C,motionFlags))return refuse("capture-read");
    const bool residue=(motionFlags&0x4000) && capture!=actor && capture==role.source &&
        SmOwnerClaimResidue(role,actor,index);
    if((motionFlags&0x4000) && capture!=actor && !residue) {
        // VUH-2580: name the actor that holds the clone's capture (its object ID), once per role.
        if(role.bindRefused!="foreign-capture" && g_smVisualDiagLogs<64) {
            uintptr_t objentry=0;std::uint32_t objectId=0;
            if(capture && ReadHitTrace(capture+offsets::actor::OBJENTRY_PTR,objentry) && objentry)
                (void)ReadHitTrace(objentry+offsets::objentry::OBJECT_ID,objectId);
            Log("[script-visual] foreign capture role=%u clone=%llX capture=%llX captureObject=%u source=%llX",role.resource,
                static_cast<unsigned long long>(actor),static_cast<unsigned long long>(capture),objectId,
                static_cast<unsigned long long>(role.source));
        }
        return refuse("foreign-capture");
    }
    if(!ReadHitTrace(actor+0x6C8,typeFlags) || !(typeFlags&0x80))return refuse("type-flags");
    if(!ReadHitTrace(actor+0x6A0,parent) || parent)return refuse("attached");
    ScriptOwnerVisual visual{};visual.actor=actor;visual.puppet=index;
    visual.handle=SmOwnHandle(actor);visual.load=warp::LoadSerial();visual.transition=warp::TransitionSerial();
    if(!visual.handle || !ReadHitTrace(actor+0x5C0,visual.status) || !visual.status ||
       !ReadHitTrace(actor+offsets::actor::OBJENTRY_PTR,visual.objentry) || !visual.objentry ||
       !ReadHitTrace(visual.objentry+offsets::objentry::OBJECT_ID,visual.objectId))return false;
    visual.wantAttachment=role.visual.wantAttachment;role.visual=visual;
    if(residue) {
        if(g_smVisualDiagLogs<64) {
            ++g_smVisualDiagLogs;
            Log("[script-visual] own residue role=%u clone=%llX capture=%llX",role.resource,
                static_cast<unsigned long long>(actor),static_cast<unsigned long long>(capture));
        }
    }
    if(g_smVisualBindLogs<32) {
        ++g_smVisualBindLogs;
        Log("[script-visual] bound role=%u seat=%u puppet=%d actor=%llX",role.resource,role.slot,index,
            static_cast<unsigned long long>(actor));
    }
    return SmOwnerVisualCurrent(role,scope);
}
static std::uint32_t SmOwnerVisualMotion(const ScriptActorRole& role) {
    return role.visualHeartbeat.Motion(role.haveMotion,role.motion,role.haveCapture,R::BitsOf(role.captureValues[0]));
}
// VUH-2580: the owner is on a motion the clone may not replay (an unqualified post-capture ID).
static bool SmOwnerVisualRefused(const ScriptActorRole& role) {
    const auto motion=SmOwnerVisualMotion(role);
    return R::OwnerMotion(motion,role.haveCapture,R::BitsOf(role.captureValues[0]),role.reaction.consumed)==
        R::OwnerMotionBank::Refuse;
}
static bool OwnsVisualPuppet(uintptr_t actor) {
    enemysync::DownedScope scope;if(!SmOwnerScope(scope))return false;
    // Reserve a checked target before generic playback can select the same
    // numeric ID from the wrong bank. Native capture itself waits for POST.
    // VUH-2580: while the owner is on a refused motion and the clone holds no borrowed bank, the ordinary
    // puppet keeps driving it; reserving it then froze the clone in its last pose.
    for(auto& role:g_smRoles)if(role.resource && role.slot!=scope.localSlot && role.haveCapture &&
        SmOwnerBindVisual(role,scope,actor) &&
        (role.visual.applying || (SmOwnerMotionState(SmOwnerVisualActor(role))!=ScriptOwnerMotionState::Foreign &&
                                  (role.visual.captured || !SmOwnerVisualRefused(role)))))return true;
    return false;
}
// Runs after the ordinary puppet's team/collision/HP and transform maintenance.
static void VisualPostUpdate(uintptr_t actor) {
    if(!SmOwnerReady())return;
    enemysync::DownedScope scope;if(!SmOwnerScope(scope))return;
    for(auto& role:g_smRoles) {
        if(!role.resource || role.slot==scope.localSlot || !role.haveCapture)continue;
        if(!SmOwnerBindVisual(role,scope,actor))continue;
        if(!SmOwnerVisualCurrent(role,scope)){SmOwnerRetireVisual(role,true);continue;}
        const auto motion=SmOwnerVisualMotion(role);
        const auto bank=R::OwnerMotion(motion,role.haveCapture,R::BitsOf(role.captureValues[0]),role.reaction.consumed);
        if(bank==R::OwnerMotionBank::Refuse) {
            // VUH-2580: say once per role and motion why the clone does not take the owner's motion.
            if(role.visual.refusedLogged!=motion && g_smVisualDiagLogs<64) {
                ++g_smVisualDiagLogs;role.visual.refusedLogged=motion;
                Log("[script-visual] refused role=%u motion=%u capture=%u consumed=%u",role.resource,motion,
                    R::BitsOf(role.captureValues[0]),role.reaction.consumed?1u:0u);
            }
            // A borrowed bank still installed would hold the clone in the owner's past motion: the native
            // cleanup returns it to its own bank and the ordinary puppet takes it back.
            if(role.visual.captured)SmOwnerRetireVisual(role,true);
            continue;
        }
        auto target=SmOwnerVisualActor(role);
        const auto state=SmOwnerMotionState(target);
        std::uint32_t current=UINT32_MAX;
        if(!ReadHitTrace(actor+offsets::actor::ANIM_ID,current))continue;
        const bool borrowed=bank==R::OwnerMotionBank::Source;
        if((borrowed && (state!=ScriptOwnerMotionState::Source || current!=motion || role.visual.motion!=motion)) ||
           (!borrowed && (state!=ScriptOwnerMotionState::Own || current!=motion))) {
            const char* why="unknown";
            if(!SmOwnerInstallMotion(role,{R::Tagged(R::kTagInt,motion),role.captureValues[1]},borrowed,true,false,&why)) {
                // VUH-2580: an install failure retired the clone's role silently.
                if(g_smVisualDiagLogs<64) {
                    ++g_smVisualDiagLogs;
                    Log("[script-visual] install failed role=%u motion=%u borrowed=%u reason=%s",role.resource,motion,
                        borrowed?1u:0u,why);
                }
                SmOwnerRetireVisual(role,true);continue;
            }
        }
        if(!SmOwnerVisualCurrent(role,scope)){SmOwnerRetireVisual(role,true);continue;}
        // VUH-2580: the owner's script-set placement belongs to its captured (borrowed) motion. Once the owner
        // is back on its own motion the ordinary puppet stream places the clone; a stale placement froze it.
        if(role.havePosition && borrowed) {
            const auto entity=actor+offsets::actor::ENTITY_TRANSFORM;
            std::memcpy(reinterpret_cast<void*>(entity+offsets::entity::POS_X),role.position,3*sizeof(float));
            *reinterpret_cast<float*>(entity+offsets::entity::ROT_Y)=role.position[3];
        }
        if(R::OwnerVisualHeartbeat::TimeApplies(role.haveMotion,role.motion,motion))
            *reinterpret_cast<float*>(actor+ACTOR_MOTCTRL+MOTCTRL_CURRENT_TIME)=role.motionTime;
        if(role.visual.wantAttachment!=role.visual.attached) {
            const auto resource=role.resource;auto target=SmOwnerVisualActor(role);
            if(SmOwnerApplyAttachment(target,role.visual.wantAttachment) && role.resource==resource && role.visual.actor==actor)
                role.visual.attached=target.attached;
        }
    }
}
static bool SmOwnerVisualCallback(uintptr_t actor) {
    if(g_smVisualNativeReplay.resource &&
       (actor==g_smVisualNativeReplay.visual.actor || actor==g_smVisualNativeReplay.source) &&
       SmOwnerVisualRoots(g_smVisualNativeReplay))return true;
    enemysync::DownedScope scope;if(!actor || !SmOwnerScope(scope))return false;
    for(const auto& role:g_smRoles)if(role.visual.actor==actor && SmOwnerVisualCurrent(role,scope) &&
        (role.visual.applying || SmOwnerVisualBank(role)))return true;
    return false;
}
// Does not depend on the revoked connection, lease or logical living state.
// Native roots and the load are the remaining cleanup authority.
static bool SmOwnerCleanupRoots(const ScriptActorRole& role) {
    return SmGameThread() && !warp::TransitionPending() && !warp::LoadPending() &&
        role.load==warp::LoadSerial() && role.transition==warp::TransitionSerial() &&
        role.actor && SmOwnerActorCurrent(role);
}
static bool SmOwnerCameraMatches(const ScriptActorRole& role,uintptr_t context) {
    if(!context || !SmOwnerCleanupRoots(role))return false;
    uintptr_t metadata=0;R::EntityCameraIdentity current{};current.context=context;
    return ReadHitTrace(context+8,current.asset) && ReadHitTrace(context+0x10,current.sourceAnchor) &&
        ReadHitTrace(context+0x48,metadata) && metadata && ReadHitTrace(metadata,current.kindRecord) &&
        R::SameEntityCamera(role.camera,current);
}
static unsigned g_smLocalReleaseLogs=0;
// VUH-2617: the friend's own actor only (client, the canonical player); the host's roles never log here. Pointer
// equality only: nothing of the role's actor or source is read to decide this.
static bool SmOwnerFriendLocal(const ScriptActorRole& role) {
    uintptr_t canonical=0;
    return !g_sm.hostActive && role.actor && role.actor==g_soraActor &&
        ReadHitTrace(g_exeBase+0x2A105D0,canonical) && canonical==role.actor;
}
// The released role's actor root as the release path proves it (game thread, room, handles), re-asked after a native
// cancel that may have changed it.
static bool SmOwnerReleaseRootCurrent(const ScriptActorRole& role) {
    return role.actor && SmGameThread() && !warp::TransitionPending() && role.load==warp::LoadSerial() &&
        role.transition==warp::TransitionSerial() && SmOwnerActorCurrent(role);
}
// `[script-owner] local release`: the gate the release stopped at and, through rules::ReadLocalHold, the friend's
// actor (+D48 coroutine, +2B0 capture, +2B8 bank against the proven source's, +6C8/+18C capture bits, +6A0, anchor,
// motion, +0xC action) read only from roots rules::ProveLocalHoldRoots proves. stage=release: as the decision found
// it; stage=post-cancel: after the original native coroutine cancel, metadata only; stage=refused: the release refused
// its root, metadata only.
static bool SmOwnerCameraSourceCurrent(const ScriptActorRole& role) noexcept;
// A root listed under its saved handle (ListedHandle): the head global, the handle resolver and the list walk
// (enemytarget::ListHandle stops at a root's predecessor), none of which reads the root.
static std::uint32_t SmListedRoot(const ScriptActorRole& role,std::uintptr_t at) {
    return R::ListedHandle(at,at==role.actor?role.actorHandle:role.sourceHandle,
        [](){uintptr_t head=0;return ReadHitTrace(g_exeBase+offsets::active_entity_list::HEAD,head)?head:uintptr_t{0};},
        [](std::uint32_t handle){return SmDecode(handle);},
        [](std::uintptr_t walked){return enemytarget::ListHandle(walked);});
}
static R::LocalHold SmReadReleaseHold(const ScriptActorRole& role,bool decisionCurrent,bool cancelled,R::LocalHoldRoots& roots) {
    roots=R::ProveLocalHoldRoots(decisionCurrent,cancelled,role.actor,role.actorHandle,role.source,
        role.sourceHandle,[&role](std::uintptr_t at){return SmListedRoot(role,at);},
        [&role](){return SmOwnerCameraSourceCurrent(role)?role.source:std::uintptr_t{0};}); // driven table only, no reads
    return R::ReadLocalHold(roots.actor,role.actor,roots.source,role.source,
        [](std::uintptr_t at,void* out,std::size_t size){return ReadHitTraceMemory(at,out,size);},
        [](std::uint32_t handle){return SmDecode(handle);});
}
static void SmLogLocalRelease(const ScriptActorRole& role,R::ReleaseCleanup outcome,const char* stage,const R::LocalHold& h) {
    if(g_smLocalReleaseLogs>=64)return;
    ++g_smLocalReleaseLogs;
    Log("[script-owner] local release stage=%s role=%u seat=%u outcome=%s actorRead=%u sourceRead=%u d48=%llX "
        "roleThread=%llX capture=%llX source=%llX bank=%llX sourceBank=%llX flags6C8=%X flags18C=%X parent=%X "
        "anchorOwn=%u motion=%u action=%X actionVt=%llX frame=%u (%u/64)",stage,role.resource,role.slot,
        R::ReleaseCleanupName(outcome),h.actorRead?1u:0u,h.sourceRead?1u:0u,
        static_cast<unsigned long long>(h.thread),static_cast<unsigned long long>(role.thread),
        static_cast<unsigned long long>(h.capture),static_cast<unsigned long long>(role.source),
        static_cast<unsigned long long>(h.bank),static_cast<unsigned long long>(h.sourceBank),h.flags6C8,h.flags18C,
        h.parent,h.actorRead && h.anchor==role.actor+0x80?1u:0u,h.motion,h.actionHandle,
        static_cast<unsigned long long>(h.actionVtable?h.actionVtable-g_exeBase:0),g_frameCounter,g_smLocalReleaseLogs);
}
// VUH-2617: a friend local-seat release that stopped at `guards`, by a role with a positive receipt that it held this
// actor (an own-bank install read back, or a borrowed capture it installed), runs the same native detach/idle pair a
// cleaned release runs, once (rules::DecideHeldRelease). Detach runs on facts from the release's own reads; the
// input-gate line is read from the same proven root before any native call. Idle runs only if detach succeeded and a
// full admission rebuilt after detach (canonical friend, session, field, list-proven root, a fresh hold read, the seat's
// slots) still holds (rules::HeldReleaseIdleAllowed). The own stand motion follows only a second full admission rebuilt
// after idle (rules::RunHeldPair); nothing of the actor is read after the stand.
static unsigned g_smHeldReleaseLogs=0;
static R::HeldReleaseFacts SmOwnerHeldFacts(const ScriptActorRole& released,bool friendLocal,R::ReleaseCleanup outcome,
                                            bool releaseStage,bool field,const R::LocalHoldRoots& roots,const R::LocalHold& h) {
    R::HeldReleaseFacts r{};
    r.friendLocal=friendLocal;r.outcome=outcome;r.releaseStage=releaseStage;r.field=field;
    enemysync::DownedScope scope;
    r.sessionCurrent=released.slot<3 && SmOwnerScope(scope) && scope.epoch==released.epoch &&
        scope.connections[released.slot]==released.connection && enemysync::ScriptCameraScopeCurrent(released.admittedScope);
    r.actorProven=roots.actor;r.actorRead=h.actorRead;r.sourceProven=roots.source && h.sourceRead;
    r.roleFree=released.slot<3 && !g_smRoles[released.slot].resource && !g_smOwnerCleanup[released.slot].role.resource;
    r.heldReceipt=released.ownMotion!=UINT32_MAX || released.captured;
    r.thread=h.thread;r.capture=h.capture;r.bank=h.bank;r.parent=h.parent;r.source=released.source;r.sourceBank=h.sourceBank;
    return r;
}
static void SmOwnerReleaseHeld(const ScriptActorRole& released,R::ReleaseCleanup outcome,const R::ReleaseCleanupFacts& f,
                               bool cancelled,const R::LocalHoldRoots& roots,const R::LocalHold& h) {
    const auto r=SmOwnerHeldFacts(released,true,outcome,f.current && !cancelled,f.field,roots,h);
    const char* why="";
    const bool release=R::DecideHeldRelease(r,why);
    if(!release && !r.heldReceipt)return;  // a role that never held this actor: nothing to say
    const auto gate=R::ReadInputGate(roots.actor && r.releaseStage,released.actor,g_exeBase+0x2A10620,
        [](std::uintptr_t at,void* out,std::size_t size){return ReadHitTraceMemory(at,out,size);},
        [](std::uint32_t handle){return SmDecode(handle);});
    const auto pair=R::RunHeldPair(release,
        [&](){return SmOwnerNativePlayer(released.actor,0x42C5F0);},
        [&](){
            // Every fact rebuilt (after detach, and again after idle); the outcome names the guards release this
            // belongs to. Quill r2 on 96d77cc: both saved roots are list/handle-proven before anything follows them.
            return R::RebuildHeld(released.actor,released.actorHandle,released.source,released.sourceHandle,
                [&](std::uintptr_t at){return SmListedRoot(released,at);},
                [&](){
                    const bool current=SmOwnerReleaseRootCurrent(released);
                    uintptr_t event=0;std::int32_t cutscene=0;
                    const bool field=current && ReadHitTrace(g_exeBase+0x2A11478,event) && !event &&
                        ReadHitTrace(g_exeBase+0xB65210,cutscene) && !cutscene;
                    R::LocalHoldRoots rootsNow{};
                    const auto now=SmReadReleaseHold(released,current,false,rootsNow);
                    return SmOwnerHeldFacts(released,SmOwnerFriendLocal(released),outcome,current,field,rootsNow,now);});},
        [&](){return SmOwnerNativePlayer(released.actor,0x42D0E0);},
        // The role's own-bank install path (0x42B320 -> the ordinary motion set), by the role's actor handle.
        [&](std::uint32_t motion){return SmOwnerNativeMotion(0,released.actorHandle,
            {R::Tagged(R::kTagInt,motion),SmOwnerFloat(0)},false,false);});
    const bool detached=pair.detached,idled=pair.idled,stood=pair.stood;
    const char* idleWhy=pair.idleWhy;
    const char* standWhy=pair.standWhy;
    if(g_smHeldReleaseLogs<32) {
        ++g_smHeldReleaseLogs;
        Log("[script-owner] local held-release %s role=%u seat=%u why=%s ownMotion=%d captured=%u detach=%u idleWhy=%s "
            "idle=%u standWhy=%s stand=%u gateRead=%u pad=%llX padEntry0=%u command=%llX commandAux=%llX fieldCommand=%u command24=%X "
            "flags9B8=%X flags120=%X motion=%u motionTime=%.1f action=%X actionVt=%llX actionTime=%.2f frame=%u (%u/32)",
            release?"ran":"kept",released.resource,released.slot,why,
            released.ownMotion==UINT32_MAX?-1:static_cast<int>(released.ownMotion),released.captured?1u:0u,
            detached?1u:0u,idleWhy,idled?1u:0u,standWhy,stood?1u:0u,gate.read?1u:0u,static_cast<unsigned long long>(gate.pad),
            gate.pad==g_exeBase+0xBF31A0?1u:0u,static_cast<unsigned long long>(gate.command),
            static_cast<unsigned long long>(gate.commandAux),gate.command==g_exeBase+0x2A10620?1u:0u,gate.command24,
            gate.flags9B8,gate.flags120,gate.motion,gate.motionTime,gate.actionHandle,
            static_cast<unsigned long long>(gate.actionVtable?gate.actionVtable-g_exeBase:0),gate.actionTime,
            g_frameCounter,g_smHeldReleaseLogs);
    }
}
// The release's native detach/idle, decided by rules::DecideReleaseCleanup from the same reads, in the same order and
// on the same branches as before (the coroutine cancel stays where it was). A friend local-seat release then logs.
static void SmOwnerCleanupPlayer(const ScriptActorRole& released) {
    if(released.axelHeld && SmOwnerReleaseRootCurrent(released)) {
        auto held=released;(void)SmAxelApplyHold(held,false); // VUH-2913: only our own hold of the Axel clone
    }
    const bool friendLocal=SmOwnerFriendLocal(released);
    R::ReleaseCleanupFacts f{};
    f.current=SmOwnerReleaseRootCurrent(released);
    auto outcome=R::ReleaseCleanup::NotCurrent;
    bool cancelled=false;
    if(f.current) {
        uintptr_t event=0;std::int32_t cutscene=0;
        f.field=ReadHitTrace(g_exeBase+0x2A11478,event) && !event &&
            ReadHitTrace(g_exeBase+0xB65210,cutscene) && !cutscene;
        uintptr_t canonical=0;
        f.local=ReadHitTrace(g_exeBase+0x2A105D0,canonical) && canonical==released.actor;
        f.clone=!f.local && released.thread && enemytarget::ListHandle(released.actor)==released.actorHandle;
        if(f.field && (f.local || f.clone)) {
            uintptr_t currentThread=0,source=0,capture=0;
            f.threadRead=ReadHitTrace(released.actor+0xD48,currentThread);
            f.threadZero=f.threadRead && !currentThread;
            if(f.threadRead && released.thread && currentThread==released.thread &&
               ReadHitTrace(currentThread+0x60,source) && source==released.source) {
                SmLifeCancelForCleanup(released); // same native call; diagnostic revoked-role context only
                f.threadOwned=true;cancelled=true;
            }
            if(f.threadZero) {
                f.captureOwned=ReadHitTrace(released.actor+0x2B0,capture) && capture==released.source;
                f.attachment=SmOwnerHasAttachment(released,released.thread==0);
            }
            if(f.threadOwned || (f.threadZero && (f.captureOwned || f.attachment))) {
                std::uint32_t parent=0;
                f.parentRead=ReadHitTrace(released.actor+0x6A0,parent);
                f.parentZero=!parent;
                if(f.parentRead && parent)f.attachment=SmOwnerHasAttachment(released,released.thread==0);
            }
        }
        outcome=R::DecideReleaseCleanup(f);
    }
    if(friendLocal) {
        R::LocalHoldRoots roots{};
        const auto h=SmReadReleaseHold(released,f.current,cancelled,roots);
        SmLogLocalRelease(released,outcome,!f.current?"refused":cancelled?"post-cancel":"release",h);
        if(outcome!=R::ReleaseCleanup::Cleaned)SmOwnerReleaseHeld(released,outcome,f,cancelled,roots,h);
    }
    if(outcome==R::ReleaseCleanup::Cleaned) {
        (void)SmOwnerNativePlayer(released.actor,0x42C5F0);
        (void)SmOwnerNativePlayer(released.actor,0x42D0E0);
    }
}
static void SmOwnerPollCleanup() {
    if(!SmGameThread())return;
    for(auto& pending:g_smOwnerCleanup) {
        if(!pending.role.resource)continue;
        const auto released=pending.role;
        if(!SmOwnerCleanupRoots(released)){pending={};continue;}
        uintptr_t event=0;std::int32_t state=0;
        if(!ReadHitTrace(g_exeBase+offsets::EVENT_CONTEXT,event) ||
           !ReadHitTrace(g_exeBase+offsets::CUTSCENE_STATE,state))continue;
        if(!event && !state) {
            pending={}; // cleanup can reenter native hooks
            SmOwnerCleanupPlayer(released);
            Log("[script-owner] camera retired source=%u role=%u seat=%u",released.netId,released.resource,released.slot);
            continue;
        }
        if(event && !SmOwnerCameraMatches(released,event)) {
            Log("[script-owner] camera retirement abandoned role=%u replacement",released.resource);
            pending={};continue;
        }
        // The admitted resident camera finishes on its authored frame. Keep
        // its receipt until native idle; never cancel a global timeline or
        // turn a paused camera clock into a wall-clock hard-free.
    }
}
// A newer native hook can continue one camera across coroutine stages. Only
// that exact same seat/source/actor/admitted scope inherits the saved identity.
struct ScriptOwnerContinuation {
    R::EntityCameraIdentity camera{};bool attached{},captured{},haveCapture{};
    bool axelHeld{}; // VUH-2913 (client): the Axel clone hold continues with its receipt
    std::uint64_t captureValues[2]{};
};
static ScriptOwnerContinuation SmOwnerContinueCamera(const ScriptActorRole& role,
    const enemysync::DownedScope& scope,uintptr_t source,uintptr_t actor,std::uint8_t slot) {
    uintptr_t event=0;
    if(!role.resource || !role.camera.context || role.slot!=slot || role.source!=source || role.actor!=actor ||
       role.epoch!=scope.epoch || role.connection!=scope.connections[slot] ||
       !enemysync::ScriptCameraScopeCurrent(role.admittedScope) ||
       (!g_sm.hostActive && GetTickCount64()>=role.deadline) ||
       (actor==g_soraActor && downedspike::LogicallyDowned()) ||
       !ReadHitTrace(g_exeBase+offsets::EVENT_CONTEXT,event) || !SmOwnerCameraMatches(role,event))return {};
    uintptr_t capture=0;
    ScriptOwnerContinuation next{role.camera,SmOwnerHasAttachment(role,!g_sm.hostActive),
        role.captured && ReadHitTrace(actor+0x2B0,capture) && capture==source};
    next.axelHeld=SmAxelCloneHeld(role);
    if(next.captured && role.haveCapture) {
        next.haveCapture=true;next.captureValues[0]=role.captureValues[0];next.captureValues[1]=role.captureValues[1];
    }
    return next;
}
static void SmOwnerRelease(ScriptActorRole& role,bool native,bool announce,bool continuing=false) {
    if(!role.resource)return;
    if(!g_sm.hostActive && role.slot<3 && SmOwnerSameAdmissionSession(role.admittedScope) &&
       role.resource>g_smRoleAdmissionFloor[role.slot])g_smRoleAdmissionFloor[role.slot]=role.resource;
    SmOwnerRetireVisual(role,native);
    const auto released=role;
    role={}; // native cleanup can reenter commands/callbacks; role is already retired
    if(announce) {
        auto end=SmOwnerRecord(released,6);
        (void)SmEmit(end); // bounded owner lease also releases a dropped end
    }
    std::array<std::uint32_t,R::ClientEffects::kCap> retire{};std::size_t count=0;
    g_sm.clientAttacks.Each([&](std::uint32_t id,const R::ClientEffect& pair) {
        if(pair.ownerRole==released.resource)retire[count++]=id;
    });
    for(std::size_t i=0;i<count;++i) {
        const auto* pair=g_sm.clientAttacks.Find(retire[i]);
        if(pair && native && SmGameThread() && released.load==warp::LoadSerial() &&
           released.transition==warp::TransitionSerial() && SmOwnerActorCurrent(released) &&
           SmAttackState(pair->handle,pair->effect,pair->part)) {
            std::uint64_t unused=0,args[4]{};
            (void)SmCallTrap(static_cast<std::size_t>(R::FindTrap(2,13)),pair->handle,args,unused);
        }
        g_sm.clientAttacks.Erase(retire[i]);
    }
    if(native && released.actor && !continuing) {
        uintptr_t event=0;
        if(released.slot<g_smOwnerCleanup.size() && released.camera.context &&
           ReadHitTrace(g_exeBase+offsets::EVENT_CONTEXT,event) && SmOwnerCameraMatches(released,event)) {
            // New admission for this seat waits for this receipt; no eviction.
            auto& pending=g_smOwnerCleanup[released.slot];
            if(!pending.role.resource) {
                pending.role=released;
                Log("[script-owner] camera retirement pending source=%u role=%u seat=%u",released.netId,released.resource,released.slot);
            }
            SmOwnerPollCleanup();
        } else SmOwnerCleanupPlayer(released);
    }
    g_sm.occurrences.Retire(released.resource,released.commandIncarnation);
    Log("[script-owner] release source=%u role=%u seat=%u native=%u",released.netId,released.resource,released.slot,native?1u:0u);
}
static void SmOwnerReset(bool native) {
    for(auto& role:g_smRoles)SmOwnerRelease(role,native,false);
    g_smPendingCapture={};
    SmOwnerResidueInvalidate();
}
static int SmOwnerNearest(uintptr_t source,const enemysync::DownedScope& scope) {
    R::ReactionPoint trigger{};float xyz[3]{};
    if(!ReadHitTraceMemory(source+offsets::actor::ENTITY_TRANSFORM+offsets::entity::POS_X,xyz,sizeof(xyz)))return -1;
    trigger={xyz[0],xyz[1],xyz[2]};
    std::array<R::ReactionSeat,3> seats{};
    for(unsigned candidate=0;candidate<3;++candidate) {
        const auto actor=enemytarget::g.candidateActor[candidate];
        const auto slot=candidate?enemytarget::g.ownerSlot[candidate]:std::uint8_t{0};
        uintptr_t status=0;
        if(!actor || slot>=3 || !scope.connections[slot] || (slot==0 && downedspike::LogicallyDowned()) ||
           !enemytarget::ReadLive(actor,status) ||
           !ReadHitTraceMemory(actor+offsets::actor::ENTITY_TRANSFORM+offsets::entity::POS_X,xyz,sizeof(xyz)))continue;
        seats[slot]={scope.connections[slot],{xyz[0],xyz[1],xyz[2]},true,true,true};
    }
    return R::ReactionWinner(trigger,R::kMaxFloat,seats);
}
static void __fastcall SmOwnerPlayer(std::uint64_t* args) {
    reinterpret_cast<TrapFn>(g_smOwnerTraps[0].original)(args);
    std::uint16_t netId=0;std::uint32_t objectId=0;
    const auto source=SmOwnerVmSource(netId,objectId);
    enemysync::DownedScope scope;
    if(!source || !SmOwnerScope(scope))return;
    // VUH-2913: inside Axel II's RC flow, the executor's own actor, not the nearest seat, and never another seat
    // in its place (R::AxelPlayerRouteFor).
    if(SmAxelPlayer(args,source,scope))return;
    auto* role=SmOwnerRoleForSource(source);
    const auto slot=role && SmOwnerCurrent(*role,scope)?role->slot:SmOwnerNearest(source,scope);
    const auto actor=slot>=0 && slot<3?SmOwnerSeatActor(static_cast<std::uint8_t>(slot)):0;
    const auto handle=SmOwnHandle(actor);
    if(handle)args[0]=R::Tagged(R::kTagAddress,handle);
}
static void __fastcall SmOwnerHook(std::uint64_t* args) {
    std::uint16_t netId=0;std::uint32_t objectId=0,ignored=0;
    const auto source=SmOwnerVmSource(netId,objectId);
    enemysync::DownedScope scope;
    const auto actor=source && SmOwnerScope(scope)?SmActorOf(args[0],ignored):0;
    const auto slot=SmOwnerActorSeat(actor);
    reinterpret_cast<TrapFn>(g_smOwnerTraps[1].original)(args); // VM context remains native
    if(!source || !actor || slot<0 || slot>=3 || g_smRoleSequence>=R::kOwnerRoleSequenceCap)return;
    // VUH-2611: every hook of this seat consumes its pending capture, adopted below or not.
    const auto pending=g_smPendingCapture[static_cast<unsigned>(slot)];g_smPendingCapture[static_cast<unsigned>(slot)]={};
    uintptr_t thread=0,vmSource=0;
    if(!ReadHitTrace(actor+0xD48,thread) || !thread || !ReadHitTrace(thread+0x60,vmSource) || vmSource!=source)return;
    if(g_smOwnerCleanup[static_cast<unsigned>(slot)].role.resource)return;
    auto& role=g_smRoles[static_cast<unsigned>(slot)];
    const auto camera=SmOwnerContinueCamera(role,scope,source,actor,static_cast<std::uint8_t>(slot));
    // No End before a continuing Begin: the owner must see an atomic supersession.
    SmOwnerRelease(role,false,!camera.camera.context);
    role={};role.camera=camera.camera;role.attached=camera.attached;role.captured=camera.captured;role.resource=0x80000000u|++g_smRoleSequence;role.netId=netId;role.objectId=objectId;
    role.haveCapture=camera.haveCapture;role.captureValues[0]=camera.captureValues[0];role.captureValues[1]=camera.captureValues[1];
    role.epoch=scope.epoch;role.slot=static_cast<std::uint8_t>(slot);role.connection=scope.connections[role.slot];
    role.actor=actor;role.source=source;role.thread=thread;role.deadline=GetTickCount64()+1000;role.admittedScope=scope;
    if(!SmOwnerBindActor(role)){role={};return;}
    role.load=warp::LoadSerial();role.transition=warp::TransitionSerial();
    (void)SmOwnerBegin(role);
    Log("[script-owner] begin source=%u role=%u seat=%u epoch=%u",netId,role.resource,role.slot,role.epoch);
    // VUH-2611: a capture the script issued just before this hook belongs to this role while it is still installed.
    if(!role.haveCapture && pending.actor) {
        const auto frames=g_frameCounter-pending.frame;
        auto captured=role;captured.captured=true;
        std::uint32_t motion=UINT32_MAX;
        const bool adopt=pending.actor==role.actor && pending.source==role.source && frames<=30 &&
            pending.load==role.load && pending.transition==role.transition && pending.epoch==role.epoch &&
            SmOwnerCaptureCurrent(captured) && ReadHitTrace(role.actor+offsets::actor::ANIM_ID,motion) &&
            motion==R::BitsOf(pending.values[0]);
        if(g_smPendingCaptureLogs<64) {
            ++g_smPendingCaptureLogs;
            Log("[script-owner] pending capture role=%u motion=%u frames=%u adopted=%u",role.resource,
                R::BitsOf(pending.values[0]),frames,adopt?1u:0u);
        }
        if(adopt) {
            role.captured=true;role.captureValues[0]=pending.values[0];role.captureValues[1]=pending.values[1];
            role.haveCapture=true;
            auto call=SmOwnerRecord(role,3);call.values[0]=pending.values[0];call.values[1]=pending.values[1];SmEmit(call);
        }
    }
}
// VUH-2913 (host): a friend's Axel RC replays on that seat's own player through an ordinary role with no thread,
// begun before the callback runs (SmAxelComplete). Its lifetime is the RC's executor (SmAxelRoleHeld), not a
// coroutine; Burst Frontier's own 1/112 hook replaces it with a coroutine role. An Overtaker followed by its Clear
// Light on the same seat and actor keeps one role.
static bool SmOwnerBeginAxelRole(std::uint8_t slot,uintptr_t actor,uintptr_t source,std::uint16_t netId,
                                 const enemysync::DownedScope& scope) {
    uintptr_t thread=0;
    if(!g_sm.hostActive || slot==0 || slot>=3 || !actor || !source || !netId || !scope.connections[slot] ||
       !ReadHitTrace(actor+0xD48,thread) || thread || g_smOwnerCleanup[slot].role.resource)return false;
    auto& role=g_smRoles[slot];
    if(role.resource && role.objectId==R::kAxelIIObjectId && !role.thread && role.actor==actor && role.source==source &&
       role.netId==netId && SmOwnerCurrent(role,scope))return true;
    if(g_smRoleSequence>=R::kOwnerRoleSequenceCap)return false;
    SmOwnerRelease(role,false,true);
    role={};role.resource=0x80000000u|++g_smRoleSequence;role.netId=netId;role.objectId=R::kAxelIIObjectId;
    role.epoch=scope.epoch;role.slot=slot;role.connection=scope.connections[slot];
    role.actor=actor;role.source=source;role.thread=0;role.deadline=GetTickCount64()+1000;role.admittedScope=scope;
    if(!SmOwnerBindActor(role)){role={};return false;}
    role.load=warp::LoadSerial();role.transition=warp::TransitionSerial();
    (void)SmOwnerBegin(role);
    Log("[script-owner] begin source=%u role=%u seat=%u epoch=%u axel=1",netId,role.resource,role.slot,role.epoch);
    return true;
}
static bool SmOwnerEmitAxelCamera(std::uint8_t slot,uintptr_t actor,std::uint32_t fourcc,std::uint32_t target) {
    enemysync::DownedScope scope;
    if(slot>=3 || !SmOwnerScope(scope))return false;
    const auto& role=g_smRoles[slot];
    if(!role.resource || role.objectId!=R::kAxelIIObjectId || role.thread || role.actor!=actor ||
       !SmOwnerCurrent(role,scope))return false;
    auto call=SmOwnerRecord(role,5);call.values[0]=R::Tagged(R::kTagInt,fourcc);call.values[1]=R::Tagged(R::kTagInt,target);
    return SmEmit(call);
}
// VUH-2913: the safe abort at 1/101 in Axel's own script (SmAxelCaptureQuery). Refused, it writes both dwords of the
// native result, int 0 ("not captured"), without resolving the argument (42D36B and 42D370 have no null check).
static void __fastcall SmOwnerCaptureQuery(std::uint64_t* args) {
    std::uint64_t before[1]{};
    if(g_sm.hostActive && SmGameThread() && SmCopyArgs(args,before,1) && SmAxelCaptureQuery(before)==R::AxelAct::Refused) {
        args[0]=R::Tagged(R::kTagInt,0);return;
    }
    reinterpret_cast<TrapFn>(g_smAxelCaptureQuerySlot.original)(args);
}
// Host only decides (SmAxelCamera); a skipped start pushes nothing (the slot has no result bit).
static void __fastcall SmOwnerCameraGlobal(std::uint64_t* args) {
    std::uint64_t before[2]{};
    if(SmGameThread() && SmCopyArgs(args,before,2) && SmAxelCamera(before))return;
    reinterpret_cast<TrapFn>(g_smAxelCameraSlot.original)(args);
}
static bool SmOwnerEmitAxelHold(std::uint8_t slot,uintptr_t actor,bool held) {
    enemysync::DownedScope scope;
    if(slot>=3 || !SmOwnerScope(scope))return false;
    const auto& role=g_smRoles[slot];
    if(!role.resource || role.objectId!=R::kAxelIIObjectId || role.thread || role.actor!=actor ||
       !SmOwnerCurrent(role,scope))return false;
    auto call=SmOwnerRecord(role,held?13:14);
    if(held){call.values[0]=R::Tagged(R::kTagInt,R::kAxelAttachFlags);call.values[1]=R::Tagged(R::kTagInt,R::kAxelAttachBone);}
    return SmEmit(call);
}
static void __fastcall SmOwnerCapture(std::uint64_t* args) {
    SmOwnerResidueInvalidate(); // VUH-2580: our replays call the original, never this detour
    std::uint64_t before[4]{};const bool copied=SmCopyArgs(args,before,4);
    // VUH-2913: the safe abort. Axel captures only the bound executor's resolving actor; refused, it returns int 0.
    if(copied && SmAxelAct(before,"capture")==R::AxelAct::Refused){args[0]=R::Tagged(R::kTagInt,0);return;}
    const auto beforeObservationError=GetLastError();
    const auto observation=SmLifeCapturePrepare(copied?before[1]:0);
    SetLastError(beforeObservationError);
    reinterpret_cast<TrapFn>(g_smOwnerTraps[2].original)(args);
    const auto nativeError=GetLastError();
    SmLifeCaptureReturned(observation,args);
    SetLastError(nativeError);
    if(!g_sm.hostActive || !copied || !SmGameThread())return;
    std::uint32_t ignored=0;const auto actor=SmActorOf(before[1],ignored);
    enemysync::DownedScope scope;if(!SmOwnerScope(scope))return;
    for(auto& role:g_smRoles)if(SmOwnerCurrent(role,scope) && role.actor==actor) {
        role.captured=true;
        role.captureValues[0]=before[2];role.captureValues[1]=before[3];role.haveCapture=true;
        auto call=SmOwnerRecord(role,3);call.values[0]=before[2];call.values[1]=before[3];SmEmit(call);return;
    }
    // VUH-2611: no current role yet; keep it for this seat's next hook of the same actor.
    const auto slot=SmOwnerActorSeat(actor);
    const auto source=SmActorOf(before[0],ignored);
    std::uint16_t vmNetId=0;std::uint32_t vmObject=0;
    if(slot>=0 && slot<3 && source && source==SmOwnerVmSource(vmNetId,vmObject) && R::TagOf(before[2])==R::kTagInt)
        g_smPendingCapture[static_cast<unsigned>(slot)]={actor,source,{before[2],before[3]},g_frameCounter,
            warp::LoadSerial(),warp::TransitionSerial(),scope.epoch};
}
static void __fastcall SmOwnerAttach(std::uint64_t* args) {
    SmOwnerResidueInvalidate();
    std::uint64_t before[6]{};const bool copied=SmCopyArgs(args,before,6);
    // VUH-2913: the safe abort. Axel attaches only to the bound executor's resolving actor; refused, nothing runs.
    const auto axel=copied?SmAxelAct(before,"attach"):R::AxelAct::Native;
    if(axel==R::AxelAct::Refused)return;
    reinterpret_cast<TrapFn>(g_smOwnerTraps[4].original)(args); // keep host callback and workspace native
    if(axel==R::AxelAct::Admitted){SmAxelSyncHeld();return;}
    if(!SmOwnerReady() || !g_sm.hostActive || !copied || !SmGameThread() ||
       R::TagOf(before[2])!=R::kTagInt || R::BitsOf(before[2])!=16386 ||
       R::TagOf(before[3])!=R::kTagInt || R::BitsOf(before[3])!=5)return;
    std::uint32_t ignored=0;const auto actor=SmActorOf(before[0],ignored);
    const auto source=SmActorOf(before[1],ignored);
    enemysync::DownedScope scope;if(!SmOwnerScope(scope))return;
    for(auto& role:g_smRoles)if(SmOwnerCurrent(role,scope) && role.actor==actor && role.source==source) {
        role.attached=true;
        if(!SmOwnerHasAttachment(role,false)){role.attached=false;return;}
        auto call=SmOwnerRecord(role,13);call.values[0]=R::Tagged(R::kTagInt,16386);
        call.values[1]=R::Tagged(R::kTagInt,5);(void)SmEmit(call);return;
    }
}
static void __fastcall SmOwnerDetach(std::uint64_t* args) {
    SmOwnerResidueInvalidate();
    std::uint64_t before=0;const bool copied=SmCopyArgs(args,&before,1);
    std::uint32_t resource=0,ignored=0;
    const auto actor=copied?SmActorOf(before,ignored):0;
    if(SmOwnerReady() && g_sm.hostActive && SmGameThread()) {
        enemysync::DownedScope scope;
        if(SmOwnerScope(scope))for(const auto& role:g_smRoles)
            if(SmOwnerCurrent(role,scope) && role.actor==actor && SmOwnerHasAttachment(role,false))resource=role.resource;
    }
    reinterpret_cast<TrapFn>(g_smOwnerTraps[5].original)(args); // host alone may run its detach BDX callback
    if(!resource)return;
    for(auto& role:g_smRoles)if(role.resource==resource) {
        std::uint32_t parent=0;if(!ReadHitTrace(role.actor+0x6A0,parent) || parent)return;
        role.attached=false;auto call=SmOwnerRecord(role,14);(void)SmEmit(call);return;
    }
}
static void __fastcall SmOwnerCamera(std::uint64_t* args) {
    if(!g_sm.hostActive || !SmGameThread()) {
        reinterpret_cast<TrapFn>(g_smOwnerTraps[3].original)(args);return;
    }
    std::uint32_t ignored=0;const auto target=SmActorOf(args[2],ignored);
    const auto source=SmActorOf(args[0],ignored);
    ScriptActorRole* selected=nullptr;
    enemysync::DownedScope scope;if(!SmOwnerScope(scope)){
        reinterpret_cast<TrapFn>(g_smOwnerTraps[3].original)(args);return;
    }
    if(g_sm.hostActive && SmGameThread())for(auto& role:g_smRoles)
        if(SmOwnerCurrent(role,scope) && role.actor==target){selected=&role;break;}
    if(!selected && target==source)selected=SmOwnerRoleForSource(source);
    if(!selected || selected->slot==0)reinterpret_cast<TrapFn>(g_smOwnerTraps[3].original)(args);
    if(!selected || R::TagOf(args[1])!=R::kTagAddress)return;
    char name[5]{};std::uint32_t fourcc=0;
    if(!ReadHitTraceMemory(SmDecode(R::BitsOf(args[1])),name,4))return;
    for(unsigned i=0;i<4;++i) {
        if(!name[i])break;
        if(name[i]<0x20 || name[i]>0x7E)return;
        fourcc|=static_cast<std::uint32_t>(static_cast<unsigned char>(name[i]))<<(8*i);
    }
    if(!fourcc)return;
    if(selected->slot==0)(void)SmOwnerOpenedCamera(*selected,source,fourcc);
    auto call=SmOwnerRecord(*selected,5);call.values[0]=R::Tagged(R::kTagInt,fourcc);
    call.values[1]=R::Tagged(R::kTagInt,target==selected->source?1u:0u);SmEmit(call);
}
static bool SmOwnerAttackSource(std::uint64_t argument,std::uint16_t& netId,std::uint32_t& objectId,std::uint8_t& slot,std::uint32_t& resource) {
    netId=0;objectId=0;slot=0xFF;resource=0;
    std::uint32_t actorId=0;const auto actor=SmActorOf(argument,actorId);
    std::uint16_t sourceId=0;std::uint32_t sourceObject=0;const auto source=SmOwnerVmSource(sourceId,sourceObject);
    enemysync::DownedScope scope;
    if(!source || !SmOwnerScope(scope))return false;
    for(const auto& role:g_smRoles)if(SmOwnerCurrent(role,scope) && role.source==source && role.actor==actor) {
        netId=role.netId;objectId=role.objectId;slot=role.slot;resource=role.resource;return true;
    }
    return false;
}
static uintptr_t SmOwnerAttackActor(std::uint32_t resource,std::uint16_t netId,std::uint32_t objectId,std::uint8_t& slot) {
    enemysync::DownedScope scope;
    if(!SmOwnerScope(scope) || downedspike::LogicallyDowned())return 0;
    for(const auto& role:g_smRoles)if(role.resource==resource && role.netId==netId && role.objectId==objectId &&
       SmOwnerCurrent(role,scope) && role.slot==scope.localSlot && role.actor==g_soraActor &&
       GetTickCount64()<role.deadline) {slot=role.slot;return role.actor;}
    return 0;
}
static bool OwnsPuppet(uintptr_t actor) {
    enemysync::DownedScope scope;
    if(!g_sm.hostActive || !SmOwnerScope(scope))return false;
    for(const auto& role:g_smRoles)if(SmOwnerCurrent(role,scope) && role.actor==actor && role.slot!=0) {
        uintptr_t thread=0; // VUH-2913: a friend's Axel role owns its puppet without a thread while its executor lasts
        return ReadHitTrace(actor+0xD48,thread) &&
            (thread?thread==role.thread:SmAxelRoleHeld(role.objectId,role.slot,role.actor,role.thread));
    }
    return false;
}
static void OwnerPostUpdate(uintptr_t actor) {
    enemysync::DownedScope scope;
    if(!SmOwnerScope(scope))return;
    for(auto& role:g_smRoles) {
        if(!SmOwnerCurrent(role,scope) || role.actor!=actor)continue;
        const auto entity=actor+offsets::actor::ENTITY_TRANSFORM;
        if(scope.localSlot==0 && g_sm.hostActive) {
            auto position=SmOwnerRecord(role,4);
            float xyz[3]{},facing=0,time=0;std::uint32_t motion=0;
            if(ReadHitTraceMemory(entity+offsets::entity::POS_X,xyz,sizeof(xyz)) &&
               ReadHitTrace(entity+offsets::entity::ROT_Y,facing)) {
                for(unsigned i=0;i<3;++i)position.values[i]=SmOwnerFloat(xyz[i]);
                position.values[3]=SmOwnerFloat(facing);SmEmit(position);
            }
            if(ReadHitTrace(actor+offsets::actor::ANIM_ID,motion) && ReadHitTrace(actor+ACTOR_MOTCTRL+MOTCTRL_CURRENT_TIME,time)) {
                auto animation=SmOwnerRecord(role,8);animation.values[0]=R::Tagged(R::kTagInt,motion);
                animation.values[1]=SmOwnerFloat(time);SmEmit(animation);
            }
        } else if(scope.localSlot==role.slot && actor==g_soraActor && GetTickCount64()<role.deadline &&
                  !downedspike::LogicallyDowned()) {
            if(role.havePosition) {
                std::memcpy(reinterpret_cast<void*>(entity+offsets::entity::POS_X),role.position,3*sizeof(float));
                *reinterpret_cast<float*>(entity+offsets::entity::ROT_Y)=role.position[3];
                std::memset(reinterpret_cast<void*>(actor+ACTOR_VELOCITY),0,3*sizeof(float));
                std::memset(reinterpret_cast<void*>(actor+ACTOR_CARRIED_DISPLACEMENT),0,3*sizeof(float));
                std::memset(reinterpret_cast<void*>(actor+ACTOR_ACCEL_BLOCK),0,ACTOR_ACCEL_BLOCK_BYTES);
            }
            if(role.haveMotion && !role.reaction.consumed) {
                const auto bank=R::OwnerMotion(role.motion,role.haveCapture,R::BitsOf(role.captureValues[0]),false);
                if(bank==R::OwnerMotionBank::Refuse)continue;
                const auto state=SmOwnerMotionState(role);
                std::uint32_t current=UINT32_MAX;
                if(!ReadHitTrace(actor+offsets::actor::ANIM_ID,current))continue;
                const bool borrowed=bank==R::OwnerMotionBank::Source;
                if((borrowed && (state!=ScriptOwnerMotionState::Source || current!=role.motion)) ||
                   (!borrowed && (state!=ScriptOwnerMotionState::Own || current!=role.motion))) {
                    if(!SmOwnerInstallMotion(role,{R::Tagged(R::kTagInt,role.motion),role.captureValues[1]},borrowed,false))continue;
                }
                if(!SmOwnerLocalCurrent(role,scope) || role.reaction.consumed)continue;
                *reinterpret_cast<float*>(actor+ACTOR_MOTCTRL+MOTCTRL_CURRENT_TIME)=role.motionTime;
            }
        }
    }
}
static std::array<R::ReactionSeat,3> SmOwnerLivingSeats(const enemysync::DownedScope&);
// Diagnostic snapshots only. No cached pointer is dereferenced by the emitter.
static void SmRcRoleSnapshot(D::Record&,const ScriptActorRole&) noexcept;
struct SmOwnerRetirementObservation {
    uintptr_t selectedActor{},secondThread{};
    std::uint64_t scopeDelivery{},scopeConnection{};
    std::uint32_t scopeGeneration{},scopeEpoch{},scopeSlot{255};
    int scopeCaptured{-1};
    int selectedRead{-1},secondRead{-1},hostActive{-1};
};
static D::Record SmOwnerRetirementSnapshot(const ScriptActorRole& role) noexcept {
    auto trace=D::Claim(role.epoch,role.resource,role.objectId,role.netId,role.reaction.command,
        role.slot,role.connection,role.reaction.occurrence);
    trace.tuple=role.resource && role.reaction.occurrence && role.reaction.command && role.connection;
    SmRcRoleSnapshot(trace,role);
    // These are cached admission values, not a new/current scope proof.
    trace.generation=role.admittedScope.context.generation;
    trace.sourceDelivery=role.admittedScope.context.deliverySerial;
    return trace;
}
static void SmOwnerRetirementLog(const char* caller,const char* reason,const D::Record& checked,
    const ScriptActorRole& current,const SmOwnerRetirementObservation& observation) noexcept {
    if(!current.resource)return; // original Release would be a no-op
    const auto disposition=SmOwnerRetirementSnapshot(current);
    const auto seen=observation; // no role/candidate references survive into Log
    const bool same=checked.resource==disposition.resource && checked.actor==disposition.actor &&
        checked.sourceActor==disposition.sourceActor && checked.thread==disposition.thread &&
        checked.requester==disposition.requester && checked.epoch==disposition.epoch && checked.slot==disposition.slot &&
        checked.load==disposition.load && checked.transition==disposition.transition;
    D::Emit(D::Stage::RoleRetirement,"retiring",reason,checked,[&](const char* base) {
        // Keep the one saturation row free of an unrelated tuple suffix.
        if(!std::strstr(base," tuple=")){Log("%s",base);return;}
        char line[4096]{};
        std::snprintf(line,sizeof(line),
            "%s caller=%s cachedIdentityMatchesDisposition=%u selectedActor=%llX selectedEvaluated=%d secondD48=%llX secondRead=%d hostActiveObserved=%d scopeGuardSucceeded=%d scopeGeneration=%u scopeDelivery=%llu scopeEpoch=%u scopeSlot=%u scopeConnection=%llu dispositionResource=%u dispositionActor=%llX dispositionSource=%llX dispositionThread=%llX dispositionConnection=%llu dispositionEpoch=%u dispositionSlot=%u dispositionLoad=%u dispositionTransition=%u dispositionFrame=%u dispositionOccurrence=%llu dispositionCommand=%u dispositionCommandFrame=%u dispositionOpenedFrame=%u dispositionDuration=%u dispositionConsumed=%d dispositionLease=%llu",
            base,caller,same?1u:0u,static_cast<unsigned long long>(seen.selectedActor),seen.selectedRead,
            static_cast<unsigned long long>(seen.secondThread),seen.secondRead,seen.hostActive,seen.scopeCaptured,seen.scopeGeneration,
            static_cast<unsigned long long>(seen.scopeDelivery),seen.scopeEpoch,seen.scopeSlot,
            static_cast<unsigned long long>(seen.scopeConnection),disposition.resource,static_cast<unsigned long long>(disposition.actor),
            static_cast<unsigned long long>(disposition.sourceActor),static_cast<unsigned long long>(disposition.thread),
            static_cast<unsigned long long>(disposition.requester),disposition.epoch,disposition.slot,disposition.load,disposition.transition,disposition.frame,
            static_cast<unsigned long long>(disposition.occurrence),disposition.command,disposition.commandFrame,
            disposition.openedFrame,disposition.duration,disposition.consumedBefore,static_cast<unsigned long long>(disposition.lease));
        Log("%s",line);
    });
}
static void SmOwnerFrame() {
    SmOwnerPollVisualCleanup();
    SmOwnerPollCleanup();
    enemysync::DownedScope scope;const char* scopeFailure="scope-current";
    if(!SmOwnerScope(scope,&scopeFailure)) {
        // Snapshot each currently nonzero role before the original group reset.
        // Later native cleanup may reenter; this records reset-entry, not a
        // claim about every role that subsequent cleanup might replace.
        SmOwnerRetirementObservation observed{};observed.scopeCaptured=0;
        for(const auto& role:g_smRoles)if(role.resource) {
            const auto checked=SmOwnerRetirementSnapshot(role);
            SmOwnerRetirementLog("scope-reset-entry",scopeFailure,checked,role,observed);
        }
        SmOwnerReset(true);return;
    }
    for(auto& role:g_smRoles) {
        if(!role.resource)continue;
        auto checked=SmOwnerRetirementSnapshot(role);SmOwnerRetirementObservation observed{};
        observed.scopeCaptured=1;observed.scopeGeneration=scope.context.generation;
        observed.scopeDelivery=scope.context.deliverySerial;observed.scopeEpoch=scope.epoch;observed.scopeSlot=scope.localSlot;
        if(role.slot<scope.connections.size())observed.scopeConnection=scope.connections[role.slot];
        const char* failure="role-current";
        bool live=SmOwnerCurrent(role,scope,&failure,&checked);
        if(role.visual.actor && !SmOwnerVisualCurrent(role,scope))SmOwnerRetireVisual(role,true);
        if(scope.localSlot==0) {
            uintptr_t thread=0;
            // These are the original lazy host predicates, in the same order.
            // Keep this SECOND D48 read separate from SmOwnerCurrent's first.
            if(live) {
                const bool active=g_sm.hostActive;observed.hostActive=active?1:0;
                if(!active){failure="host-inactive";live=false;}
                else {
                    const auto selected=SmOwnerSeatActor(role.slot);
                    observed.selectedActor=selected;observed.selectedRead=1;
                    if(selected!=role.actor){failure="host-selected-actor";live=false;}
                    else {
                        const bool read=ReadHitTrace(role.actor+0xD48,thread);
                        observed.secondRead=read?1:0;observed.secondThread=thread;
                        if(!read){failure="host-second-d48-unreadable";live=false;}
                        // VUH-2913: a friend's Axel role has no thread; it lasts while its executor does.
                        else if(!thread && !SmAxelRoleHeld(role.objectId,role.slot,role.actor,role.thread)) {
                            failure=role.objectId==R::kAxelIIObjectId && !role.thread?"axel-executor-ended":"host-second-d48-zero";
                            live=false;
                        }
                        else if(thread!=role.thread){failure="host-second-d48-replaced";live=false;}
                    }
                }
            }
            if(!live) {
                SmOwnerRetirementLog("host-frame-release",failure,checked,role,observed);
                SmOwnerRelease(role,true,true);continue;
            }
            (void)R::RefreshReaction(role.reaction,scope.epoch,g_frameCounter,SmOwnerLivingSeats(scope));
            if(g_frameCounter-role.heartbeat>=15) {
                // Arrival admission can legitimately drop an early Begin and
                // capture. Refresh current state through the existing stream;
                // receivers treat the same role/capture/attachment idempotently.
                (void)SmOwnerBegin(role);
                if(role.haveCapture) {
                    auto capture=SmOwnerRecord(role,3);
                    capture.values[0]=role.captureValues[0];capture.values[1]=role.captureValues[1];(void)SmEmit(capture);
                }
                // VUH-2913: an Axel role's attachment is Axel held by its actor (bone 1); an unread one sends nothing.
                const bool axelRole=role.objectId==R::kAxelIIObjectId;
                const int held=axelRole?SmAxelHeldBy(role.actor):SmOwnerHasAttachment(role,false)?1:0;
                if(held>=0) {
                    auto attachment=SmOwnerRecord(role,held?13:14);
                    if(held) {
                        attachment.values[0]=R::Tagged(R::kTagInt,16386);
                        attachment.values[1]=R::Tagged(R::kTagInt,axelRole?R::kAxelAttachBone:5);
                    }
                    (void)SmEmit(attachment);
                }
                auto pulse=SmOwnerRecord(role,11);pulse.values[0]=R::Tagged(R::kTagInt,g_frameCounter);
                if(SmEmit(pulse))role.heartbeat=g_frameCounter;
            }
        } else {
            live=live && D::Check(GetTickCount64()<role.deadline,"client-lease-expired",failure) &&
                (role.slot!=scope.localSlot || (D::Check(role.actor==g_soraActor,"client-canonical-actor",failure) &&
                    D::Check(!downedspike::LogicallyDowned(),"client-logical-down",failure)));
            if(!live) {
                SmOwnerRetirementLog("client-frame-release",failure,checked,role,observed);
                SmOwnerRelease(role,true,false);
            }
        }
    }
}
static bool SmOwnerReactionIdentity(const R::Call& call) {
    enemysync::DownedScope scope;if(!SmOwnerScope(scope))return false;
    for(const auto& role:g_smRoles)if(role.resource==call.effect && role.netId==call.netId && role.objectId==call.objectId) {
        const auto connection=static_cast<std::uint64_t>(R::BitsOf(call.values[1])) |
            (static_cast<std::uint64_t>(R::BitsOf(call.values[2]))<<32);
        return SmOwnerCurrent(role,scope) && R::BitsOf(call.values[0])==role.slot && connection==role.connection;
    }
    return false;
}
static bool SmOwnerReactionOffer(const R::Call& call) {
    enemysync::DownedScope scope;if(!SmOwnerScope(scope))return false;
    for(auto& role:g_smRoles)if(role.resource==call.effect && role.netId==call.netId && role.objectId==call.objectId) {
        if(!SmOwnerCurrent(role,scope) || R::BitsOf(call.values[3])!=role.slot)return false;
        const auto occurrence=static_cast<std::uint64_t>(R::BitsOf(call.values[0])) |
            (static_cast<std::uint64_t>(R::BitsOf(call.values[1]))<<32);
        const auto command=static_cast<std::uint16_t>(R::BitsOf(call.values[2]));
        if(occurrence<role.reaction.occurrence)return false;
        if(occurrence==role.reaction.occurrence)return command==role.reaction.command;
        role.reaction={occurrence,role.connection,scope.epoch,g_frameCounter,180,role.netId,command,role.slot,false};
        return true;
    }
    return false;
}
static std::array<R::ReactionSeat,3> SmOwnerLivingSeats(const enemysync::DownedScope& scope) {
    std::array<R::ReactionSeat,3> seats{};
    for(unsigned i=0;i<3;++i) {
        const auto actor=enemytarget::g.candidateActor[i];const auto slot=i?enemytarget::g.ownerSlot[i]:std::uint8_t{0};
        float xyz[3]{};
        uintptr_t status=0;
        if(!actor || slot>=3 || !scope.connections[slot] || (slot==0 && downedspike::LogicallyDowned()) ||
           !enemytarget::ReadLive(actor,status) ||
           !ReadHitTraceMemory(actor+offsets::actor::ENTITY_TRANSFORM+offsets::entity::POS_X,xyz,sizeof(xyz)))continue;
        seats[slot]={scope.connections[slot],{xyz[0],xyz[1],xyz[2]},true,true,true};
    }
    return seats;
}
static void SmRcRoleSnapshot(D::Record& trace,const ScriptActorRole& role) noexcept {
    trace.facts|=D::Native|D::Role;trace.frame=g_frameCounter;trace.load=role.load;trace.transition=role.transition;
    trace.actor=role.actor;trace.sourceActor=role.source;trace.thread=role.thread;
    trace.commandFrame=role.commandFrame;trace.slot=role.slot;trace.mode=role.commandMode;
    trace.facts|=D::Window;trace.openedFrame=role.reaction.openedFrame;trace.duration=role.reaction.duration;
    trace.consumedBefore=role.reaction.consumed?1:0;
    trace.lease=role.deadline;trace.actorStatus=role.actorStatus;trace.actorEntry=role.actorObjentry;
    trace.sourceStatus=role.sourceStatus;trace.sourceEntry=role.sourceObjentry;
    trace.actorHandle=role.actorHandle;trace.sourceHandle=role.sourceHandle;
}
#include "ScriptActorLifecycleTrace.inl"
static bool SmOwnerCompleteReaction(const enemysync::ScriptReactionCompletionRecord& claim) {
    auto trace=SmRcClaim(claim);trace.facts|=D::Native;trace.frame=g_frameCounter;
    const char* why="unknown";
    const auto refused=[&](){SmRcHop(D::Stage::HostRole,"refused",why,trace);return false;};
    enemysync::DownedScope scope;
    if(!D::Check(g_sm.hostActive,"host-active",why) || !D::Check(g_smReactionCallback!=nullptr,"callback-ready",why) ||
       !D::Check(SmOwnerScope(scope),"owner-scope",why) || !D::Check(scope.localSlot==0,"local-host-seat",why))return refused();
    for(auto& role:g_smRoles)if(role.resource==claim.resource && role.netId==claim.netId && role.objectId==claim.objectId) {
        const R::ReactionCompletion completion{claim.occurrence,claim.requesterConnectionId,claim.epoch,
            claim.netId,claim.command,claim.ownerSlot};
        const auto seats=SmOwnerLivingSeats(scope);
        SmRcRoleSnapshot(trace,role);
        if(!SmOwnerCurrent(role,scope,&why,&trace) ||
           !D::Check(g_frameCounter-role.commandFrame<=15,"command-producer-stale",why) ||
           !D::Check(R::OwnerCommandCaptureLive(role.commandMode,SmOwnerCaptureCurrent(role)),"mode-capture",why) ||
           !D::Check(SmOwnerSeatActor(role.slot)==role.actor,"selected-seat-actor",why) ||
           !D::Check(SmOwnHandle(role.source)!=0,"source-handle",why))return refused();
        const auto before=role.reaction;SmRcWindow(trace,before,seats);trace.frame=g_frameCounter;
        const bool consumed=R::ConsumeReaction(role.reaction,completion,scope.epoch,g_frameCounter,seats);
        trace.consumedAfter=role.reaction.consumed?1:0;
        if(!consumed){why=SmRcConsumeReason(before,completion,scope.epoch,trace.frame,seats);return refused();}
        const std::uint32_t target[]{SmOwnHandle(role.source),0,0,UINT32_MAX,0};
        const auto completed=claim; // Native hook may replace this role before returning.
        const auto entered=trace;
        SmRcHop(D::Stage::HostRole,"callback-enter","consumed",entered);
        g_smReactionCallback(role.source,role.actor,claim.command,target);
        SmRcHop(D::Stage::HostRole,"callback-returned","native-call-returned",entered);
        Log("[script-rc] complete source=%u resource=%u occurrence=%llu command=%u owner=%u",
            completed.netId,completed.resource,completed.occurrence,completed.command,completed.ownerSlot);
        return true;
    }
    why="role-missing";return refused();
}
static bool SmOwnerReactionCallback(uintptr_t receiver,uintptr_t player,std::uint32_t command,
                                   const D::Record* acceptedTrace=nullptr) {
    auto trace=acceptedTrace?*acceptedTrace:D::Record{};trace.command=command;trace.actor=player;trace.sourceActor=receiver;
    const char* why="unknown";
    enemysync::DownedScope scope;
    if(!SmOwnerScope(scope)){if(trace.tuple)SmRcHop(D::Stage::LocalHandler,"not-handled","owner-scope",trace);return false;}
    for(auto& role:g_smRoles)if(role.resource && role.source==receiver && role.actor==player && role.reaction.command==command) {
        trace= D::Claim(role.epoch,role.resource,role.objectId,role.netId,command,role.slot,role.connection,role.reaction.occurrence);
        SmRcRoleSnapshot(trace,role);trace.consumedBefore=role.reaction.consumed?1:0;
        if(!SmOwnerCurrent(role,scope,&why,&trace) || !D::Check(role.slot==scope.localSlot,"local-selected-seat",why) ||
           !D::Check(player==g_soraActor,"canonical-player",why) || !D::Check(!downedspike::LogicallyDowned(),"local-down",why) ||
           !D::Check(R::OwnerCommandCaptureLive(role.commandMode,SmOwnerCaptureCurrent(role)),"mode-capture",why) ||
           !D::Check(R::ReactionLive(role.reaction,scope.epoch,g_frameCounter),"local-role-window",why)) {
            SmRcHop(D::Stage::LocalHandler,"refused",why,trace);return true;
        }
        enemysync::ScriptReactionCompletionRecord claim{role.reaction.occurrence,role.connection,
            role.resource,role.objectId,role.epoch,role.netId,static_cast<std::uint16_t>(command),role.slot};
        if(scope.localSlot==0)(void)SmOwnerCompleteReaction(claim);
        else {
            role.reaction.consumed=true;trace.consumedAfter=1;
            SmRcHop(D::Stage::LocalHandler,"send-enter","role-consumed",trace);
            const bool queued=enemysync::SendScriptReactionCompletion(claim);
            SmRcHop(D::Stage::LocalHandler,queued?"ring-enqueued":"send-refused","role-send-returned",trace);
            Log("[script-rc] local completion source=%u resource=%u occurrence=%llu command=%u owner=%u queued=%u",
                claim.netId,claim.resource,claim.occurrence,claim.command,claim.ownerSlot,queued?1u:0u);
        }
        return true;
    }
    if(trace.tuple)SmRcHop(D::Stage::LocalHandler,"not-handled","role-missing",trace);return false;
}
static uintptr_t SmOwnerInvokeCommand(OwnerCommandFn original,uintptr_t controller,int kind,std::uint32_t command,
    const std::uint32_t* target,std::uint32_t flags,int priority,bool& ok) noexcept {
    uintptr_t result=0;ok=false;
    const bool wasReplaying=g_smOwnerReplay;g_smOwnerReplay=true;
    __try {result=original(controller,kind,command,target,flags,priority);ok=true;}
    __except(EXCEPTION_EXECUTE_HANDLER){++g_sm.faults;}
    g_smOwnerReplay=wasReplaying;return result;
}
static bool SmOwnerReplayCommand(std::uint32_t mode,uintptr_t controller,std::uint32_t command,const std::uint32_t* target,
    std::uint32_t flags,int priority) noexcept {
    const auto original=mode==static_cast<std::uint32_t>(R::OwnerCommandMode::CapturedMotion)?
        g_smOwnerCapturedCommand:g_smOwnerCommand;
    if(!R::ValidOwnerCommandMode(mode) || !original)return false;
    bool ok=false;(void)SmOwnerInvokeCommand(original,controller,1,command,target,flags,priority,ok);return ok;
}
// Native 3DB2F0 calls 3A6480(source->BAR, type=0x16, fourcc, occurrence=0),
// then decodes the returned entry's +8 handle. Its missing-entry path still
// creates a camera task, so require the resident asset before entering it.
static constexpr std::uint8_t kSmOwnerBarLookup[]{
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x7C,0x24,0x10,0x48,0x63,0x79,0x04,0x33,0xDB,
    0x48,0x85,0xFF,0x7E,0x36,0x44,0x8B,0xDB,0x4C,0x8D,0x51,0x14,0x0F,0x1F,0x40,0x00,
    0x83,0xFA,0xFF,0x74,0x09,0x41,0x0F,0xB7,0x42,0xFC,0x3B,0xC2,0x75,0x0F,0x45,0x39,
    0x02,0x75,0x0A,0x41,0x8B,0xC1,0x41,0xFF,0xC9,0x85,0xC0,0x74,0x1B,0xFF,0xC3,0x49,
    0xFF,0xC3,0x49,0x83,0xC2,0x10,0x4C,0x3B,0xDF,0x7C,0xD5,0x33,0xC0,0x48,0x8B,0x5C,
    0x24,0x08,0x48,0x8B,0x7C,0x24,0x10,0xC3,0x48,0x8B,0x7C,0x24,0x10,0x48,0x63,0xC3,
    0x48,0x8B,0x5C,0x24,0x08,0x48,0xFF,0xC0,0x48,0xC1,0xE0,0x04,0x48,0x03,0xC1,0xC3,
};
static bool SmOwnerCameraAsset(uintptr_t source,std::uint32_t fourcc,uintptr_t* output) noexcept {
    if(output)*output=0;
    if(!SmOwnerReady() || !source || !fourcc)return false;
    bool ended=false;
    for(unsigned i=0;i<4;++i) {
        const auto ch=static_cast<std::uint8_t>(fourcc>>(8*i));
        if(!ch)ended=true;
        else if(ended || ch<0x20 || ch>0x7E)return false;
    }
    uintptr_t bar=0;std::uint32_t header[4]{};
    if(!ReadHitTrace(source+0x928,bar) || !bar ||
       !ReadHitTraceMemory(bar,header,sizeof(header)) || header[0]!=0x01524142u ||
       !header[1] || header[1]>4096)return false;
    const auto bytes=(static_cast<uintptr_t>(header[1])+1)*16;
    if(bar>UINTPTR_MAX-bytes)return false;
    __try {
        using Lookup=uintptr_t(__fastcall*)(uintptr_t,int,std::uint32_t,int);
        const auto entry=reinterpret_cast<Lookup>(g_exeBase+0x3A6480)(bar,0x16,fourcc,0);
        if(entry<bar+16 || entry>bar+bytes-16 || (entry-bar)%16)return false;
        std::uint16_t type=0;std::uint32_t name=0,handle=0,size=0;
        if(!ReadHitTrace(entry,type) || type!=0x16 || !ReadHitTrace(entry+4,name) || name!=fourcc ||
           !ReadHitTrace(entry+8,handle) || !handle || !ReadHitTrace(entry+12,size) || !size)return false;
        const auto payload=SmDecode(handle);std::uint8_t probe=0;
        const bool valid=payload && payload<=UINTPTR_MAX-(size-1) && ReadHitTrace(payload,probe) &&
            ReadHitTrace(payload+size-1,probe);
        if(valid && output)*output=payload;
        return valid;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
// This query is called by gameplay gating. Never call SmOwnerScope, census or
// SmOwnerCurrent here: those gates query this proof and would recurse.
static bool SmOwnerCameraSourceCurrent(const ScriptActorRole& role) noexcept {
    if(g_sm.hostActive)return enemysync::HostEnemyNetId(role.source,role.objectId)==role.netId;
    // Do not use SmDrivenActor here: its field-object branch enters gameplay
    // gating. This proof is deliberately restricted to a current combat actor.
    for(const auto& driven:enemymirror::g_driven)
        if(driven.actor==role.source && driven.netId==role.netId && !driven.pose.field &&
           driven.running && g_frameCounter-driven.frame<=2)return true;
    return false;
}
static bool SmOwnerCameraCurrent(uintptr_t context,std::int32_t state) noexcept {
    if(!SmOwnerReady() || !SmGameThread() || state<0 || state>4 || !context ||
       warp::TransitionPending() || warp::LoadPending())return false;
    const ScriptActorRole* cameraTraceRole=nullptr; // VUH2707_TRACE
    if(SmCameraTraceEnabled())for(const auto& candidate:g_smRoles)if(candidate.resource && candidate.camera.context==context){cameraTraceRole=&candidate;break;} // VUH2707_TRACE
    auto cameraTrace=SmCameraTraceBegin(2,cameraTraceRole);cameraTrace.enabled=cameraTrace.enabled && cameraTraceRole;cameraTrace.inputContext=context; // VUH2707_TRACE
    std::uint8_t field=0,menu=0;std::int32_t frozen=1,currentState=0;uintptr_t active=0;
    if(!cameraTrace.Read(ReadHitTrace(g_exeBase+offsets::IN_FIELD,field),"field-read",R::CameraObservation::InField,field) || cameraTrace.Bad(!field,"not-in-field") ||
       !cameraTrace.Read(ReadHitTrace(g_exeBase+offsets::OPEN_MENU,menu),"menu-read",R::CameraObservation::Menu,menu) || cameraTrace.Bad(menu!=0xFF,"menu") ||
       !cameraTrace.Read(ReadHitTrace(g_exeBase+offsets::CONTROLLABLE,frozen),"frozen-read",R::CameraObservation::Frozen,frozen) || cameraTrace.Bad(frozen!=0,"frozen") ||
       !cameraTrace.Read(ReadHitTrace(g_exeBase+offsets::EVENT_CONTEXT,active),"active-context-read",R::CameraObservation::Active,active) || cameraTrace.Bad(active!=context,"context-replaced") ||
       !cameraTrace.Read(ReadHitTrace(g_exeBase+offsets::CUTSCENE_STATE,currentState),"state-read",R::CameraObservation::State,currentState) || cameraTrace.Bad(currentState!=state,"state-replaced"))return SmCameraTraceResult(cameraTrace,false);
    for(const auto& role:g_smRoles) {
        cameraTrace.watch=&role==cameraTraceRole; // VUH2707_TRACE
        if(cameraTrace.Bad(!role.resource,"no-resource") || cameraTrace.Bad(role.camera.context!=context,"other-context") || cameraTrace.Bad(!role.actor,"no-actor") ||
           cameraTrace.Bad(role.load!=warp::LoadSerial(),"load") || cameraTrace.Bad(role.transition!=warp::TransitionSerial(),"transition") ||
           cameraTrace.Bad(!enemysync::ScriptCameraScopeCurrent(role.admittedScope),"scope") ||
           cameraTrace.Bad(!SmOwnerActorCurrent(role),"actor-identity") ||
           cameraTrace.Bad(!SmOwnerCameraSourceCurrent(role),"source-current") ||
           cameraTrace.Bad(role.slot!=role.admittedScope.localSlot,"owner-seat") || cameraTrace.Bad(role.actor!=g_soraActor,"canonical-actor"))continue;
        if(cameraTrace.Bad(role.admittedScope.localSlot!=0 && GetTickCount64()>=role.deadline,"lease"))continue;
        uintptr_t actorStatus=0,sourceStatus=0;
        if(!cameraTrace.Read(enemytarget::ReadLive(role.actor,actorStatus),"actor-live",R::CameraObservation::ActorStatus,actorStatus) || cameraTrace.Bad(actorStatus!=role.actorStatus,"actor-status-replaced") ||
           !cameraTrace.Read(enemytarget::ReadLive(role.source,sourceStatus),"source-live",R::CameraObservation::SourceStatus,sourceStatus) || cameraTrace.Bad(sourceStatus!=role.sourceStatus,"source-status-replaced"))continue;
        uintptr_t thread=0,vmSource=0,metadata=0;R::EntityCameraIdentity current{};current.context=context;
        if(!cameraTrace.Read(ReadHitTrace(role.actor+0xD48,thread),"thread-read",R::CameraObservation::Thread,thread) || cameraTrace.Bad(thread!=role.thread,"thread-replaced") ||
           (thread && (!cameraTrace.Read(ReadHitTrace(thread+0x60,vmSource),"vm-source-read",R::CameraObservation::VmSource,vmSource) || cameraTrace.Bad(vmSource!=role.source,"vm-source-replaced"))) ||
           !cameraTrace.Read(ReadHitTrace(context+0x10,current.sourceAnchor),"anchor-read",R::CameraObservation::Anchor,current.sourceAnchor) || !cameraTrace.Read(ReadHitTrace(context+8,current.asset),"asset-read",R::CameraObservation::Asset,current.asset) ||
           !cameraTrace.Read(ReadHitTrace(context+0x48,metadata),"metadata-read",R::CameraObservation::Metadata,metadata) || cameraTrace.Bad(!metadata,"no-metadata") ||
           !cameraTrace.Read(ReadHitTrace(metadata,current.kindRecord),"kind-read",R::CameraObservation::Kind,current.kindRecord))continue;
        if(cameraTrace.Good(R::SameEntityCamera(role.camera,current),"camera-identity"))return SmCameraTraceResult(cameraTrace,true);
    }
    return SmCameraTraceResult(cameraTrace,false);
}
static bool SmOwnerOpenedCamera(ScriptActorRole& role,uintptr_t source,std::uint32_t fourcc) noexcept {
    if(!SmGameThread())return false; // also protects the shared diagnostic budget
    auto cameraTrace=SmCameraTraceBegin(1,&role);cameraTrace.nativeCalls=1;cameraTrace.fourcc=fourcc; // VUH2707_TRACE
    // The arrival proof is identity-only. Preserve the raw native field fences
    // independently, without querying gameplay before this camera is registered.
    if(cameraTrace.Bad(warp::TransitionPending(),"transition-pending") || cameraTrace.Bad(warp::LoadPending(),"load-pending") ||
       cameraTrace.Bad(role.load!=warp::LoadSerial(),"load") || cameraTrace.Bad(role.transition!=warp::TransitionSerial(),"transition") ||
       cameraTrace.Bad(role.slot!=role.admittedScope.localSlot,"owner-seat") || cameraTrace.Bad(role.actor!=g_soraActor,"canonical-actor") ||
       cameraTrace.Bad(downedspike::LogicallyDowned(),"logically-downed"))return SmCameraTraceResult(cameraTrace,false);
    std::uint8_t field=0,menu=0;std::int32_t frozen=1,state=-1;
    if(!cameraTrace.Read(ReadHitTrace(g_exeBase+offsets::IN_FIELD,field),"field-read",R::CameraObservation::InField,field) || cameraTrace.Bad(!field,"not-in-field") ||
       !cameraTrace.Read(ReadHitTrace(g_exeBase+offsets::OPEN_MENU,menu),"menu-read",R::CameraObservation::Menu,menu) || cameraTrace.Bad(menu!=0xFF,"menu") ||
       !cameraTrace.Read(ReadHitTrace(g_exeBase+offsets::CONTROLLABLE,frozen),"frozen-read",R::CameraObservation::Frozen,frozen) || cameraTrace.Bad(frozen!=0,"frozen") ||
       !cameraTrace.Read(ReadHitTrace(g_exeBase+offsets::CUTSCENE_STATE,state),"state-read",R::CameraObservation::State,state) || cameraTrace.Bad(state<0 || state>4,"state"))return SmCameraTraceResult(cameraTrace,false);
    uintptr_t context=0,metadata=0,asset=0;R::EntityCameraIdentity current{};
    if(cameraTrace.Bad(source!=role.source,"source-identity") || cameraTrace.Bad(!SmOwnerActorCurrent(role),"actor-identity") ||
       cameraTrace.Bad(!enemysync::ScriptCameraScopeCurrent(role.admittedScope),"scope") ||
       !cameraTrace.Read(SmOwnerCameraAsset(source,fourcc,&asset),"resident-asset",R::CameraObservation::ResidentAsset,asset) ||
       !cameraTrace.Read(ReadHitTrace(g_exeBase+offsets::EVENT_CONTEXT,context),"context-read",R::CameraObservation::Context,context) || cameraTrace.Bad(!context,"no-context") ||
       !cameraTrace.Read(ReadHitTrace(context+8,current.asset),"asset-read",R::CameraObservation::Asset,current.asset) || !cameraTrace.Read(ReadHitTrace(context+0x10,current.sourceAnchor),"anchor-read",R::CameraObservation::Anchor,current.sourceAnchor) ||
       !cameraTrace.Read(ReadHitTrace(context+0x48,metadata),"metadata-read",R::CameraObservation::Metadata,metadata) || cameraTrace.Bad(!metadata,"no-metadata") || !cameraTrace.Read(ReadHitTrace(metadata,current.kindRecord),"kind-read",R::CameraObservation::Kind,current.kindRecord))return SmCameraTraceResult(cameraTrace,false);
    current.context=context;
    const R::EntityCameraIdentity expected{context,source+0x80,asset,0x6C000Bull};
    if(cameraTrace.Bad(!R::SameEntityCamera(expected,current),"camera-identity"))return SmCameraTraceResult(cameraTrace,false);
    role.camera=expected;
    Log("[script-owner] camera source=%u role=%u seat=%u context=%llX kind=11",role.netId,role.resource,
        role.slot,static_cast<unsigned long long>(context));
    return SmCameraTraceResult(cameraTrace,true);
}
static int SmOwnerApply(const R::Call& call,uintptr_t source) {
    auto cameraTrace=SmCameraTraceBegin(0,nullptr);cameraTrace.enabled=cameraTrace.enabled && SmGameThread() && call.trap==R::FindTrap(255,5); // VUH2707_TRACE
    cameraTrace.resource=call.effect;cameraTrace.netId=call.netId;cameraTrace.source=source;cameraTrace.fourcc=R::BitsOf(call.values[0]); // VUH2707_TRACE
    if(cameraTrace.Bad(!SmOwnerReady(),"owner-not-ready"))return SmCameraTraceResult(cameraTrace,0);
    enemysync::DownedScope scope;if(cameraTrace.Bad(!SmOwnerScope(scope),"owner-scope"))return SmCameraTraceResult(cameraTrace,0);
    const auto operation=R::kTraps[call.trap].index;
    if(operation==2) {
        const auto slot=R::BitsOf(call.values[0]);
        const auto connection=static_cast<std::uint64_t>(R::BitsOf(call.values[1])) |
            (static_cast<std::uint64_t>(R::BitsOf(call.values[2]))<<32);
        if(!(call.effect&0x80000000u) || slot>=3 || !connection || scope.connections[slot]!=connection ||
           R::BitsOf(call.values[3])!=60)return 0;
        if(!SmOwnerSameAdmissionSession(scope)) {
            g_smRoleAdmissionFloor={};g_smRoleAdmissionScope=scope;
        }
        if(g_smOwnerCleanup[slot].role.resource)return 0;
        auto& role=g_smRoles[slot];
        if(!R::OwnerRoleBeginAllowed(call.effect,role.resource,g_smRoleAdmissionFloor[slot]))return 0;
        if(role.resource==call.effect)return SmOwnerCurrent(role,scope)?1:0;
        if(role.resource>call.effect)return 0;
        const auto camera=SmOwnerContinueCamera(role,scope,source,slot==scope.localSlot?g_soraActor:0,static_cast<std::uint8_t>(slot));
        SmOwnerRelease(role,true,false,camera.camera.context!=0);
        if(g_smOwnerCleanup[slot].role.resource)return 0;
        role={};role.camera=camera.camera;role.attached=camera.attached;role.captured=camera.captured;
        role.axelHeld=camera.axelHeld;role.haveCapture=camera.haveCapture;role.captureValues[0]=camera.captureValues[0];role.captureValues[1]=camera.captureValues[1];
        role.resource=call.effect;role.netId=call.netId;role.objectId=call.objectId;role.source=source;
        role.slot=static_cast<std::uint8_t>(slot);role.connection=connection;role.epoch=scope.epoch;
        role.actor=slot==scope.localSlot?g_soraActor:0;role.deadline=GetTickCount64()+1000;role.admittedScope=scope;
        if(!SmOwnerBindActor(role)){role={};return 0;}
        g_smRoleAdmissionFloor[slot]=call.effect;
        role.load=warp::LoadSerial();role.transition=warp::TransitionSerial();
        Log("[script-owner] admitted source=%u role=%u seat=%u epoch=%u",role.netId,role.resource,role.slot,role.epoch);
        return 1;
    }
    for(auto& role:g_smRoles)if(role.resource==call.effect && role.netId==call.netId && role.objectId==call.objectId) {
        SmCameraTraceRole(cameraTrace,role); // VUH2707_TRACE
        if(cameraTrace.Bad(!SmOwnerCurrent(role,scope),"role-current"))return SmCameraTraceResult(cameraTrace,0);
        if(operation==6){SmOwnerRelease(role,true,false);return 1;}
        role.deadline=GetTickCount64()+1000;
        if(role.slot!=scope.localSlot) {
            if(operation==3) {
                const bool changed=!role.haveCapture || role.captureValues[0]!=call.values[0] || role.captureValues[1]!=call.values[1];
                role.visualHeartbeat.Capture(role.haveCapture,R::BitsOf(role.captureValues[0]),R::BitsOf(call.values[0]));
                role.captureValues[0]=call.values[0];role.captureValues[1]=call.values[1];role.haveCapture=true;
                if(changed)role.visual.motion=UINT32_MAX;
            } else if(operation==4) {
                for(unsigned i=0;i<4;++i)role.position[i]=R::FloatOf(call.values[i]);role.havePosition=true;
            } else if(operation==8) {
                role.motion=R::BitsOf(call.values[0]);role.motionTime=R::FloatOf(call.values[1]);role.haveMotion=true;
                role.visualHeartbeat.Heartbeat(role.motion);
            } else if(operation==13)role.visual.wantAttachment=role.objectId!=R::kAxelIIObjectId; // Thorn's pair only
            else if(operation==14)role.visual.wantAttachment=false;
            return 1; // camera, commands and native attacks stay actual-owner-only
        }
        if(cameraTrace.Bad(role.actor!=g_soraActor,"canonical-actor") || cameraTrace.Bad(!SmOwnHandle(role.actor),"actor-handle") || cameraTrace.Bad(downedspike::LogicallyDowned(),"logically-downed"))return SmCameraTraceResult(cameraTrace,0);
        if(operation==3) {
            if(!source || source!=role.source || R::TagOf(call.values[0])!=R::kTagInt)return 0;
            if(R::CaptureSnapshotApplied({role.captureValues[0],role.captureValues[1]},
                {call.values[0],call.values[1]},role.haveCapture,SmOwnerCaptureCurrent(role),role.reaction.consumed,
                role.haveMotion && role.motion==0 && SmOwnerMotionState(role)==ScriptOwnerMotionState::Own))return 1;
            // A changed authored input can install the next capture without
            // reviving or rewriting the consumed occurrence's command authority.
            const bool changed=!role.haveCapture || role.captureValues[0]!=call.values[0] || role.captureValues[1]!=call.values[1];
            if(!SmOwnerInstallMotion(role,{call.values[0],call.values[1]},true,false,changed))return 0;
            role.captureValues[0]=call.values[0];role.captureValues[1]=call.values[1];role.haveCapture=true;
            return 1;
        }
        if(operation==4) {
            for(unsigned i=0;i<4;++i)role.position[i]=R::FloatOf(call.values[i]);role.havePosition=true;return 1;
        }
        if(operation==8) {
            if(R::TagOf(call.values[0])!=R::kTagInt || R::TagOf(call.values[1])!=R::kTagFloat ||
               R::BitsOf(call.values[0])>4095 || R::FloatOf(call.values[1])<0)return 0;
            role.motion=R::BitsOf(call.values[0]);role.motionTime=R::FloatOf(call.values[1]);role.haveMotion=true;return 1;
        }
        if(operation==5) {
            // VUH-2913: an Axel role's camera may also start with no target (R::kAxelCameraNone).
            const auto targetCode=R::BitsOf(call.values[1]);
            const bool axelRole=role.objectId==R::kAxelIIObjectId;
            if(cameraTrace.Bad(!source,"no-source") || cameraTrace.Bad(source!=role.source,"source-identity") ||
               cameraTrace.Bad(targetCode>(axelRole?R::kAxelCameraNone:1u),"camera-argument"))return SmCameraTraceResult(cameraTrace,0);
            char name[5]{};const auto fourcc=R::BitsOf(call.values[0]);std::memcpy(name,&fourcc,4);
            if(cameraTrace.Bad(!SmOwnerCameraAsset(source,fourcc),"resident-asset"))return SmCameraTraceResult(cameraTrace,0);
            using Camera=void(__fastcall*)(uintptr_t,const char*,uintptr_t);
            cameraTrace.nativeCalls=1; // VUH2707_TRACE
            reinterpret_cast<Camera>(g_exeBase+0x3DB2F0)(source,name,
                targetCode==R::kAxelCameraNone?uintptr_t{0}:targetCode?source:role.actor);
            return SmCameraTraceResult(cameraTrace,SmOwnerOpenedCamera(role,source,fourcc)?1:0,"registration");
        }
        if(operation==13 && role.objectId==R::kAxelIIObjectId) { // VUH-2913: Axel held by this seat's player
            if(source!=role.source || R::TagOf(call.values[0])!=R::kTagInt || R::TagOf(call.values[1])!=R::kTagInt ||
               !R::AxelAttachPair(R::BitsOf(call.values[0]),R::BitsOf(call.values[1])))return 0;
            return SmAxelApplyHold(role,true)?1:0;
        }
        if(operation==14 && role.objectId==R::kAxelIIObjectId)return SmAxelApplyHold(role,false)?1:0;
        if(operation==13) {
            if(source!=role.source || R::TagOf(call.values[0])!=R::kTagInt || R::BitsOf(call.values[0])!=16386 ||
               R::TagOf(call.values[1])!=R::kTagInt || R::BitsOf(call.values[1])!=5)return 0;
            return SmOwnerApplyAttachment(role,true)?1:0;
        }
        if(operation==14)return SmOwnerApplyAttachment(role,false)?1:0;
        if(operation==11)return 1;
        // Commands require an admitted per-window reaction as well as this role.
        if(operation==7) {
            if(!R::ReactionLive(role.reaction,scope.epoch,g_frameCounter) ||
               !R::ValidOwnerCommandMode(R::BitsOf(call.values[0])) || R::BitsOf(call.values[1])!=role.reaction.command ||
               R::BitsOf(call.values[3])>255)return 0;
            if(!R::OwnerCommandCaptureLive(R::BitsOf(call.values[0]),SmOwnerCaptureCurrent(role)))return 0;
            uintptr_t controller=0,owner=0;
            if(!ReadHitTrace(role.actor+0xDD0,controller) || !controller ||
               !ReadHitTrace(controller+0xAE0,owner) || owner!=role.actor || !SmOwnHandle(role.source))return 0;
            const std::uint32_t target[]{SmOwnHandle(role.source),0,0,UINT32_MAX,0};
            role.commandMode=R::BitsOf(call.values[0]);
            return SmOwnerReplayCommand(R::BitsOf(call.values[0]),controller,role.reaction.command,target,R::BitsOf(call.values[2]),
                static_cast<int>(R::BitsOf(call.values[3])))?1:0;
        }
    }
    return SmCameraTraceResult(cameraTrace,0);
}
static bool SmOwnerMirroredSource(uintptr_t actor) {
    if(!actor)return false;
    if(enemysync::SuppressClientDecision(actor))return true;
    uintptr_t entry=0;std::uint32_t objectId=0;
    if(!ReadHitTrace(actor+offsets::actor::OBJENTRY_PTR,entry) || !entry ||
       !ReadHitTrace(entry+offsets::objentry::OBJECT_ID,objectId))return false;
    enemymirror::Pose pose{};
    const auto gate=enemysync::MirrorPose(actor,pose);
    if(objectId==enemyclass::TwilightThorn && gate!=enemymirror::Gate::None && pose.objectId==objectId)return true;
    for(const auto& driven:enemymirror::g_driven)if(driven.actor==actor && driven.running &&
       SmDrivenActor(driven.netId,objectId)==actor)return true;
    return false;
}
static void __fastcall SmOwnerDispatch(uintptr_t vm,std::uint64_t key,const std::uint64_t* args,std::uint32_t argc,uintptr_t parent) {
    if(g_sm.requested && SmOwnerReady() && SmGameThread() && enemysync::ActivationRole()==2 && (key==6 || key==16 || key==17)) {
        uintptr_t owner=0;
        bool suppress=vm && ReadHitTrace(vm+0x60,owner) &&
            (SmOwnerMirroredSource(owner) || SmOwnerVisualCallback(owner));
        if(!suppress && argc && args) {
            std::uint64_t arg=0;std::uint32_t ignored=0;
            const auto target=SmCopyArgs(args,&arg,1)?SmActorOf(arg,ignored):0;
            if(SmOwnerVisualCallback(target))suppress=true;
            uintptr_t capture=0;
            if(target && ReadHitTrace(target+0x2B0,capture))for(const auto& role:g_smRoles)
                if(role.resource && SmOwnerActorCurrent(role) && role.actor==target && role.source==capture &&
                   GetTickCount64()<role.deadline && SmOwnerMirroredSource(capture))suppress=true;
        }
        if(suppress)return; // keep native motion/effects/hitboxes before this BDX entry
    }
    g_smScriptDispatch(vm,key,args,argc,parent);
}
static uintptr_t SmOwnerCommandRefuse(const char* reason,uintptr_t caller,std::uint32_t command,std::uint32_t source) {
    if(g_smOwnerCommandRefusals<32) {
        ++g_smOwnerCommandRefusals;
        Log("[script-rc] command refused reason=%s caller=%llX command=%u sourceHandle=%08X (%u/32)",
            reason,static_cast<unsigned long long>(caller),command,source,g_smOwnerCommandRefusals);
    }
    return 0;
}
static uintptr_t SmOwnerRouteCommand(OwnerCommandFn original,R::OwnerCommandEntry entry,uintptr_t caller,
    uintptr_t controller,int kind,std::uint32_t command,const std::uint32_t* target,std::uint32_t flags,int priority) {
    const bool nativeMotion=R::NativeCapturedCommandCaller(entry,caller);
    if(nativeMotion && g_sm.requested && g_smOwnerCommandEntryLogs.fetch_add(1,std::memory_order_relaxed)<16) {
        const bool gameThread=SmGameThread();std::uint32_t diagnosticSource=0;bool copied=false,registered=false;
        if(gameThread && target)copied=ReadHitTraceMemory(reinterpret_cast<uintptr_t>(target),&diagnosticSource,sizeof(diagnosticSource));
        if(copied && diagnosticSource) {
            for(const auto& role:g_smRoles)if(role.resource && role.sourceHandle==diagnosticSource)registered=true;
            for(const auto& cleanup:g_smOwnerCleanup)if(cleanup.role.resource && cleanup.role.sourceHandle==diagnosticSource)registered=true;
        }
        Log("[script-rc] native command entry helper=%u caller=%llX command=%u sourceHandle=%08X copied=%u registered=%u ready=%u gameThread=%u replay=%u kind=%d",
            static_cast<unsigned>(entry),static_cast<unsigned long long>(caller),command,diagnosticSource,copied?1u:0u,
            registered?1u:0u,SmOwnerReady()?1u:0u,gameThread?1u:0u,gameThread && g_smOwnerReplay?1u:0u,kind);
    }
    if(!g_sm.requested || !SmOwnerReady() || !SmGameThread() || g_smOwnerReplay || kind!=1)
        return original(controller,kind,command,target,flags,priority);
    uintptr_t actor=0;
    if(!controller || !ReadHitTrace(controller+0xAE0,actor))return original(controller,kind,command,target,flags,priority);
    std::array<std::uint32_t,5> targetCopy{};
    const bool targetRead=target && ReadHitTraceMemory(reinterpret_cast<uintptr_t>(target),targetCopy.data(),sizeof(targetCopy));
    const auto sourceHandle=targetRead?targetCopy[0]:0;
    bool registered=false;
    for(const auto& role:g_smRoles)if(role.resource && sourceHandle && role.sourceHandle==sourceHandle)registered=true;
    for(const auto& cleanup:g_smOwnerCleanup)if(cleanup.role.resource && sourceHandle && cleanup.role.sourceHandle==sourceHandle)registered=true;
    for(const auto& cleanup:g_smVisualCleanup)if(cleanup.role.resource && sourceHandle && cleanup.role.sourceHandle==sourceHandle)registered=true;
    if(g_smVisualNativeReplay.resource && sourceHandle && g_smVisualNativeReplay.sourceHandle==sourceHandle)registered=true;
    ScriptActorRole* selected=nullptr;
    auto mode=R::OwnerCommandMode::General;
    enemysync::DownedScope scope;
    if(!nativeMotion && entry==R::OwnerCommandEntry::Captured && registered)
        return SmOwnerCommandRefuse("unproven-captured-caller",caller,command,sourceHandle);
    if(nativeMotion) {
        // Native animation uses the canonical controller even for a remote
        // clone. Its pinned explicit target wins over residual VM globals.
        if(!registered)return original(controller,kind,command,target,flags,priority);
        if(!targetRead || !R::OwnerCommandRootTarget(targetCopy) || flags!=1 || priority!=100)
            return SmOwnerCommandRefuse("native-target-shape",caller,command,sourceHandle);
        uintptr_t canonical=0,canonicalController=0;
        if(!ReadHitTrace(g_exeBase+0x2A105D0,canonical) || actor!=canonical ||
           !ReadHitTrace(canonical+0xDD0,canonicalController) || controller!=canonicalController || !SmOwnerScope(scope))
            return SmOwnerCommandRefuse("native-controller-scope",caller,command,sourceHandle);
        if(scope.localSlot!=0 && command<=UINT16_MAX) {
            // A client's own host-offered window re-registers natively each
            // frame; op7 replays alone leave alternate updates empty (VUH-2617).
            std::array<R::ClientReactionRegistration,3> offered{};
            for(unsigned i=0;i<g_smRoles.size();++i) {
                const auto& role=g_smRoles[i];
                if(!role.resource)continue;
                offered[i]={role.sourceHandle,role.slot,
                    role.actor==canonical && SmOwnerCurrent(role,scope) && GetTickCount64()<role.deadline,
                    SmOwnerCaptureCurrent(role),role.reaction};
            }
            if(R::SelectClientReactionRegistration(entry,sourceHandle,static_cast<std::uint16_t>(command),scope.localSlot,
                scope.connections[scope.localSlot],scope.epoch,g_frameCounter,downedspike::LogicallyDowned(),offered)>=0)
                return original(controller,kind,command,target,flags,priority);
        }
        const auto seats=SmOwnerLivingSeats(scope);
        std::array<R::CapturedCommandRoleFacts,3> facts{};
        for(unsigned i=0;i<g_smRoles.size();++i) {
            const auto& role=g_smRoles[i];
            if(!role.resource || role.sourceHandle!=sourceHandle)continue; // no arbitrary handle decode
            const bool captureCurrent=SmOwnerCaptureCurrent(role);
            const bool fresh=role.slot<3 && seats[role.slot].connection==role.connection && seats[role.slot].living &&
                seats[role.slot].fresh && seats[role.slot].capable;
            facts[i]={role.sourceHandle,SmOwnerCurrent(role,scope) && (g_sm.hostActive || GetTickCount64()<role.deadline),
                role.captured,captureCurrent,captureCurrent,fresh};
        }
        const auto index=R::SelectCapturedCommandRole(sourceHandle,facts);
        if(index<0)return SmOwnerCommandRefuse("native-capture-owner",caller,command,sourceHandle);
        selected=&g_smRoles[static_cast<unsigned>(index)];mode=R::OwnerCommandMode::CapturedMotion;
    } else if(g_sm.hostActive) {
        std::uint16_t netId=0;std::uint32_t objectId=0;
        const auto source=SmOwnerVmSource(netId,objectId);
        if(source) {
            // The trap uses the canonical controller even in a remote coroutine.
            // Resolve VM ownership first; an ambiguous source must not fall back.
            selected=SmOwnerRoleForSource(source);
            if(!selected)return SmOwnerCommandRefuse("vm-owner",caller,command,sourceHandle);
        }
    }
    if(!nativeMotion && registered && !selected)
        return SmOwnerCommandRefuse("unproven-owned-producer",caller,command,sourceHandle);
    if(!selected)for(auto& role:g_smRoles)if(role.resource && role.actor==actor && SmOwnerActorCurrent(role)){
        selected=&role;break;
    }
    if(!selected)return original(controller,kind,command,target,flags,priority);
    auto& role=*selected;
    if(!targetRead || !R::OwnerCommandRootTarget(targetCopy) || sourceHandle!=role.sourceHandle ||
       !SmOwnerScope(scope) || !SmOwnerCurrent(role,scope) || command==0 || command>UINT16_MAX || priority<0 || priority>255)
        return SmOwnerCommandRefuse("role-target-scope",caller,command,sourceHandle);
    if(scope.localSlot!=0)return 0; // local captured motion cannot mint a window
    const auto commandMode=static_cast<std::uint32_t>(mode);
    if(!role.reaction.occurrence || !R::OwnerCommandContinues(role.reaction.command,role.commandMode,role.commandFrame,
        command,commandMode,g_frameCounter)) {
        g_sm.occurrences.Retire(role.resource,role.commandIncarnation);
        if(g_smOwnerCommandSequence==UINT64_MAX)return 0;
        role.commandIncarnation=++g_smOwnerCommandSequence;
        auto seats=SmOwnerLivingSeats(scope);
        for(unsigned i=0;i<3;++i)if(i!=role.slot)seats[i]={}; // chain owner was nearest at role ingress
        R::ReactionPoint trigger=seats[role.slot].position;
        auto* window=g_sm.occurrences.Open(role.resource,role.commandIncarnation,role.netId,
            static_cast<std::uint16_t>(command),scope.epoch,g_frameCounter,180,trigger,R::kMaxFloat,seats);
        if(!window)return 0;
        role.reaction=*window;role.commandAnnounced=false;role.commandMode=commandMode;
    }
    role.commandFrame=g_frameCounter;
    if(!role.commandAnnounced) {
        auto identity=SmOwnerRecord(role,12);
        identity.values[0]=R::Tagged(R::kTagInt,role.slot);
        identity.values[1]=R::Tagged(R::kTagInt,static_cast<std::uint32_t>(role.connection));
        identity.values[2]=R::Tagged(R::kTagInt,static_cast<std::uint32_t>(role.connection>>32));
        auto offer=SmOwnerRecord(role,1);
        offer.values[0]=R::Tagged(R::kTagInt,static_cast<std::uint32_t>(role.reaction.occurrence));
        offer.values[1]=R::Tagged(R::kTagInt,static_cast<std::uint32_t>(role.reaction.occurrence>>32));
        offer.values[2]=R::Tagged(R::kTagInt,command);offer.values[3]=R::Tagged(R::kTagInt,role.slot);
        role.commandAnnounced=SmEmit(identity) && SmEmit(offer);
        if(role.commandAnnounced)Log("[script-rc] offer source=%u resource=%u occurrence=%llu command=%u owner=%u epoch=%u mode=%u helper=%u caller=%llX",
            role.netId,role.resource,role.reaction.occurrence,command,role.slot,role.epoch,commandMode,
            static_cast<unsigned>(entry),static_cast<unsigned long long>(caller));
    }
    if(!role.commandAnnounced || !R::RefreshReaction(role.reaction,scope.epoch,g_frameCounter,SmOwnerLivingSeats(scope)))return 0;
    auto descriptor=SmOwnerRecord(role,7);
    descriptor.values[0]=R::Tagged(R::kTagInt,commandMode);
    descriptor.values[1]=R::Tagged(R::kTagInt,command);descriptor.values[2]=R::Tagged(R::kTagInt,flags);
    descriptor.values[3]=R::Tagged(R::kTagInt,static_cast<std::uint32_t>(priority));SmEmit(descriptor);
    if(role.slot!=0)return 0;
    uintptr_t ownedController=0;
    if(!ReadHitTrace(role.actor+0xDD0,ownedController) || !ownedController)return 0;
    const auto replay=mode==R::OwnerCommandMode::CapturedMotion?g_smOwnerCapturedCommand:g_smOwnerCommand;
    bool ok=false;return SmOwnerInvokeCommand(replay,ownedController,1,command,targetCopy.data(),flags,priority,ok);
}
static uintptr_t __fastcall SmOwnerCommand(uintptr_t controller,int kind,std::uint32_t command,
    const std::uint32_t* target,std::uint32_t flags,int priority) {
    const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    return SmOwnerRouteCommand(g_smOwnerCommand,R::OwnerCommandEntry::General,caller>=g_exeBase?caller-g_exeBase:0,
        controller,kind,command,target,flags,priority);
}
static uintptr_t __fastcall SmOwnerCapturedCommand(uintptr_t controller,int kind,std::uint32_t command,
    const std::uint32_t* target,std::uint32_t flags,int priority) {
    const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    return SmOwnerRouteCommand(g_smOwnerCapturedCommand,R::OwnerCommandEntry::Captured,caller>=g_exeBase?caller-g_exeBase:0,
        controller,kind,command,target,flags,priority);
}
static bool SmOwnerPin(std::uint32_t rva,const std::uint8_t* expected,std::size_t size) {
    std::uint8_t body[1024]{};
    return size<=sizeof(body) && ReadHitTraceMemory(g_exeBase+rva,body,size) && !std::memcmp(body,expected,size);
}
static void SmOwnerShutdown() {
    SmLifeShutdown(); // isolated observer teardown precedes core revocation
    SmNativeReactionShutdown();
    enemysync::SetScriptCameraCurrent(nullptr);
    // A callback already inside its detour may still need the trampoline.
    // Disable our entries, but leave trampoline reclamation to global teardown.
    g_smOwnerInstalled.store(false,std::memory_order_release);
    SmOwnerRestoreSlot(g_smAxelCameraSlot);
    SmOwnerRestoreSlot(g_smAxelCaptureQuerySlot);
    for(auto& slot:g_smOwnerTraps)if(slot.swapped) {
        const auto previous=InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(slot.entry),
            reinterpret_cast<void*>(slot.original),reinterpret_cast<void*>(slot.detour));
        if(previous==reinterpret_cast<void*>(slot.detour) || previous==reinterpret_cast<void*>(slot.original))slot.swapped=false;
        else Log("[script-owner] restore slot changed index=%u",slot.index);
    }
    if(g_smOwnerCommandCreated) {
        const auto status=MH_DisableHook(reinterpret_cast<void*>(g_exeBase+0x3B2850));
        if(status!=MH_OK && status!=MH_ERROR_DISABLED)Log("[script-owner] disable command failed status=%d",status);
    }
    if(g_smOwnerCapturedCommandCreated) {
        const auto status=MH_DisableHook(reinterpret_cast<void*>(g_exeBase+0x3B25C0));
        if(status!=MH_OK && status!=MH_ERROR_DISABLED)Log("[script-owner] disable captured command failed status=%d",status);
    }
    if(g_smOwnerDispatchCreated) {
        const auto status=MH_DisableHook(reinterpret_cast<void*>(g_exeBase+0x3E1840));
        if(status!=MH_OK && status!=MH_ERROR_DISABLED)Log("[script-owner] disable dispatch failed status=%d",status);
    }
}
static bool SmOwnerInstall() {
    g_smOwnerInstalled.store(false,std::memory_order_release);
    const bool reactionPins=SmNativeReactionPins();
    uintptr_t table=0;if(!ReadHitTrace(g_exeBase+R::kTrapTables+8,table) || !table)return false;
    for(std::size_t i=0;i<kSmOwnerCoreTrapCount;++i) {
        auto& slot=g_smOwnerTraps[i];
        std::uint32_t flags=0;slot.entry=R::EntryRva(table,slot.index);
        if(!ReadHitTrace(slot.entry,slot.original) || slot.original!=g_exeBase+slot.rva ||
           !ReadHitTrace(slot.entry+8,flags) || flags!=slot.flags || !SmOwnerPin(slot.rva,slot.body,slot.size))return false;
    }
    if(!SmOwnerPin(0x42C5F0,R::kOwnerDetach,sizeof(R::kOwnerDetach)) ||
       !SmOwnerPin(0x42D0E0,R::kOwnerIdle,sizeof(R::kOwnerIdle)) ||
       !SmOwnerPin(0x42B320,R::kOwnerMotion,sizeof(R::kOwnerMotion)) ||
       !SmOwnerPin(0x3D6F80,R::kOwnerCancel,sizeof(R::kOwnerCancel)) ||
       !SmOwnerPin(0x3D9FE0,R::kOwnerAttachHelper,sizeof(R::kOwnerAttachHelper)) ||
       !SmOwnerPin(0x3DA250,R::kOwnerDetachHelper,sizeof(R::kOwnerDetachHelper)) ||
       !SmOwnerPin(0x3DB2F0,R::kOwnerCameraHelper,sizeof(R::kOwnerCameraHelper)) ||
       !SmOwnerPin(0x3A6480,kSmOwnerBarLookup,sizeof(kSmOwnerBarLookup)) ||
       !SmOwnerPin(0x3B2850,R::kOwnerCommandHelper,sizeof(R::kOwnerCommandHelper)) ||
       !SmOwnerPin(0x3B25C0,R::kOwnerCapturedCommandHelper,sizeof(R::kOwnerCapturedCommandHelper)) ||
       !SmOwnerPin(0x40DA22,R::kOwnerAnimationCommandCaller,sizeof(R::kOwnerAnimationCommandCaller)) ||
       !SmOwnerPin(0x40E557,R::kOwnerEmitterCommandCaller,sizeof(R::kOwnerEmitterCommandCaller)) ||
       !SmOwnerPin(0x3E1840,R::kScriptDispatch,sizeof(R::kScriptDispatch)))return false;
    auto status=MH_CreateHook(reinterpret_cast<void*>(g_exeBase+0x3E1840),reinterpret_cast<void*>(&SmOwnerDispatch),
        reinterpret_cast<void**>(&g_smScriptDispatch));
    if(status!=MH_OK)return false;
    g_smOwnerDispatchCreated=true;
    status=MH_CreateHook(reinterpret_cast<void*>(g_exeBase+0x3B2850),reinterpret_cast<void*>(&SmOwnerCommand),
        reinterpret_cast<void**>(&g_smOwnerCommand));
    if(status!=MH_OK){SmOwnerShutdown();return false;}
    g_smOwnerCommandCreated=true;
    status=MH_CreateHook(reinterpret_cast<void*>(g_exeBase+0x3B25C0),reinterpret_cast<void*>(&SmOwnerCapturedCommand),
        reinterpret_cast<void**>(&g_smOwnerCapturedCommand));
    if(status!=MH_OK){SmOwnerShutdown();return false;}
    g_smOwnerCapturedCommandCreated=true;
    if(MH_EnableHook(reinterpret_cast<void*>(g_exeBase+0x3E1840))!=MH_OK ||
       MH_EnableHook(reinterpret_cast<void*>(g_exeBase+0x3B2850))!=MH_OK ||
       MH_EnableHook(reinterpret_cast<void*>(g_exeBase+0x3B25C0))!=MH_OK) {
        SmOwnerShutdown();return false;
    }
    for(std::size_t i=0;i<kSmOwnerCoreTrapCount;++i) {
        auto& slot=g_smOwnerTraps[i];
        slot.swapped=InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(slot.entry),
            reinterpret_cast<void*>(slot.detour),reinterpret_cast<void*>(slot.original))==reinterpret_cast<void*>(slot.original);
        if(!slot.swapped){SmOwnerShutdown();return false;}
    }
    g_smOwnerInstalled.store(true,std::memory_order_release);
    enemysync::SetScriptCameraCurrent(&SmOwnerCameraCurrent);
    // Optional RC qualification must not roll back the existing one-brain
    // dispatch suppression or owner traps on a new pin/hook failure.
    if(reactionPins && SmNativeReactionInstall())
        enemysync::SetScriptReactionCurrent(&SmNativeReactionCurrent);
    else Log("[script-rc-cache] unavailable pins=%u; existing owner suppression retained",reactionPins?1u:0u);
    (void)SmLifeInstall(table); // diagnostic refusal never rolls back the core
    // VUH-2913: optional too. Refused, a friend's Axel RC camera keeps playing on the host, as before.
    if(!SmOwnerSwapOptional(g_smAxelCameraSlot,table))Log("[axel-rc] camera_start_global hook refused; Axel's RC cameras stay native");
    if(!SmOwnerSwapOptional(g_smAxelCaptureQuerySlot,table))
        Log("[axel-rc] obj_motion_is_capture hook refused; the abort is unarmed, friends execute no Axel RC");
    return true;
}
