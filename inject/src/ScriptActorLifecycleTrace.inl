// VUH-2569: observational native lifecycle receipts. Included after the role
// snapshot helper; no value in this file grants or renews gameplay authority.
using SmLifeCancelFn=void(__fastcall*)(uintptr_t);
static SmLifeCancelFn g_smLifeCancelOriginal=nullptr;
static bool g_smLifeCancelEnabled=false;
static std::atomic<bool> g_smLifeReady{false};
static std::atomic<bool> g_smLifeRetired{false};
static std::atomic<std::uint64_t> g_smLifeCallSequence{0};
static std::uint64_t g_smLifeCaptureSerial=0;
struct SmLifeCleanupFrame {SmLifeSnapshot snapshot{};uintptr_t anchor{};std::uint64_t serial{};};
struct SmLifeLocal {unsigned depth{};std::array<SmLifeCleanupFrame,4> frames{};};
static tracefiber::Store<SmLifeLocal> g_smLifeStorage;
struct SmLifePost {
    uintptr_t thread{};
    std::uint32_t resource{};
    int read{-1};
    const char* status{"not-associated"};
};
struct SmLifeMemo {
    SmLifeSnapshot snapshot{};
    std::uint64_t input{},output{},capture{};
    std::uint32_t postResource{};
    uintptr_t postThread{};
    int postRead{-1};
    int inputRead{-1};
    bool read{},captureRead{};
    const char* postStatus{""};
};
static std::array<std::array<SmLifeMemo,3>,2> g_smLifeQueries{};
static std::array<SmLifeMemo,3> g_smLifeCaptures{};
// An unusable cleanup context is never replaced by a guessed active role.
// Retire observations permanently; installed hooks keep calling originals.
static void SmLifeRetire(const char* reason) noexcept {
    const auto error=GetLastError();
    g_smLifeReady.store(false,std::memory_order_release);
    if(!g_smLifeRetired.exchange(true,std::memory_order_acq_rel))
        Log("[script-lifecycle] unavailable reason=%s; original pass-through retained",reason);
    SetLastError(error);
}
static const SmLifeSnapshot* SmLifeCleanupContext(uintptr_t actor) noexcept {
    auto* local=g_smLifeStorage.Current();
    if(!local){SmLifeRetire("cleanup-context-unavailable");return nullptr;}
    if(!local->depth)return nullptr;
    if(local->depth>local->frames.size()){SmLifeRetire("cleanup-context-overflow");return nullptr;}
    auto& frame=local->frames[local->depth-1];
    if(!g_smLifeStorage.Anchor(frame.anchor)){
        g_smLifeStorage.Reject(local);SmLifeRetire("cleanup-context-anchor");return nullptr;
    }
    return frame.snapshot.associated && frame.snapshot.role.actor==actor?&frame.snapshot:nullptr;
}

static bool SmLifeSameKey(const ScriptActorRole& a,const ScriptActorRole& b) noexcept {
    const auto& x=a.admittedScope;const auto& y=b.admittedScope;
    return a.resource==b.resource && a.actor==b.actor && a.source==b.source && a.thread==b.thread &&
        a.actorHandle==b.actorHandle && a.sourceHandle==b.sourceHandle && a.actorStatus==b.actorStatus &&
        a.sourceStatus==b.sourceStatus && a.actorObjentry==b.actorObjentry && a.sourceObjentry==b.sourceObjentry &&
        a.connection==b.connection && a.slot==b.slot && a.epoch==b.epoch && a.netId==b.netId && a.objectId==b.objectId &&
        a.load==b.load && a.transition==b.transition && x.context.generation==y.context.generation &&
        x.context.deliverySerial==y.context.deliverySerial && x.context.hostSourceSerial==y.context.hostSourceSerial &&
        x.connections==y.connections && x.localSlot==y.localSlot && x.epoch==y.epoch &&
        x.worldId==y.worldId && x.roomId==y.roomId && x.door==y.door && x.mapProgram==y.mapProgram &&
        x.battleProgram==y.battleProgram && x.eventProgram==y.eventProgram;
}
// Registration precedes every cached actor dereference. SmOwnHandle alone
// reads actor+4 and therefore is not the initial registration proof.
static const char* SmLifeRoots(const ScriptActorRole& role) noexcept {
    if(!g_smLifeReady.load(std::memory_order_acquire) || !SmGameThread() || !SmOwnerReady() || !g_sm.hostActive)
        return "observer-or-host-unavailable";
    if(!role.resource || !role.actor || !role.source || !role.actorHandle || !role.sourceHandle ||
       !role.thread || role.slot>=3 || !role.connection)return "cached-shape";
    if(warp::TransitionPending() || warp::LoadPending() || role.load!=warp::LoadSerial() ||
       role.transition!=warp::TransitionSerial() || role.admittedScope.localSlot!=0 ||
       role.epoch!=role.admittedScope.epoch || role.admittedScope.connections[role.slot]!=role.connection ||
       !enemysync::ScriptCameraScopeCurrent(role.admittedScope))return "admitted-scope-changed";
    if(enemytarget::ListHandle(role.actor)!=role.actorHandle ||
       enemytarget::ListHandle(role.source)!=role.sourceHandle)return "allocation-not-registered";
    if(SmDecode(role.actorHandle)!=role.actor || SmDecode(role.sourceHandle)!=role.source)return "handle-replaced";
    if(!SmOwnerActorCurrent(role))return "native-roots-changed";
    if(enemysync::HostEnemyNetId(role.source,role.objectId)!=role.netId)return "host-source-binding";
    return "registered-roots";
}
static SmLifeSnapshot SmLifePrepareRole(const ScriptActorRole& role,bool revoked) noexcept {
    SmLifeSnapshot s{};
    try {
        if(!g_smLifeReady.load(std::memory_order_acquire) || !SmGameThread() || !SmOwnerReady() || !g_sm.hostActive ||
           !role.resource)return s;
        s.associated=true;s.revoked=revoked;s.role=role;
        s.trace=D::Claim(role.epoch,role.resource,role.objectId,role.netId,role.reaction.command,
            role.slot,role.connection,role.reaction.occurrence);
        s.trace.tuple=role.reaction.occurrence!=0;SmRcRoleSnapshot(s.trace,role);
        s.trace.generation=role.admittedScope.context.generation;
        s.trace.sourceDelivery=role.admittedScope.context.deliverySerial;
        s.trace.expected=role.thread;
        s.executingRead=ReadHitTrace(g_exeBase+0x2A250B0,s.executing)?1:0;
        if(role.slot>=g_smRoles.size() ||
           (g_smRoles[role.slot].resource && !SmLifeSameKey(role,g_smRoles[role.slot])) ||
           (!revoked && !SmLifeSameKey(role,g_smRoles[role.slot]))) {
            s.failure="pre-active-role-replaced";return s;
        }
        s.failure=SmLifeRoots(role);
        if(std::strcmp(s.failure,"registered-roots"))return s;
        uintptr_t thread=0;const bool read=ReadHitTrace(role.actor+0xD48,thread);
        s.trace.observed=thread;s.trace.observedRead=read?1:0;
        s.qualified=read && thread==role.thread;
        s.failure=!read?"native-thread-unreadable":!thread?"native-thread-zero":
            thread!=role.thread?"native-thread-replaced":"qualified";
    } catch(...) {s.qualified=false;s.failure="diagnostic-exception";}
    return s;
}
static SmLifeSnapshot SmLifeForArgument(std::uint64_t argument) noexcept {
    if(!g_smLifeReady.load(std::memory_order_acquire) || !SmGameThread() || R::TagOf(argument)!=R::kTagAddress)return {};
    const ScriptActorRole* match=nullptr;
    for(const auto& role:g_smRoles)if(role.resource && role.actorHandle==R::BitsOf(argument)) {
        if(match)return {}; // ambiguous registry association has no qualified tuple
        match=&role;
    }
    if(match) {
        auto s=SmLifePrepareRole(*match,false);
        if(s.qualified && (s.executingRead!=1 || s.executing!=s.role.thread ||
            s.trace.observed!=s.role.thread)) {s.qualified=false;s.failure="not-executing-role-thread";}
        return s;
    }
    return {};
}
// Motion-end's BDX operand is not the outer-object argument used by capture-ID.
// Identify its executing admitted VM only. The raw target stays unresolved;
// neither its tag nor its numerical bits are decoded or granted authority.
static SmLifeSnapshot SmLifeForExecution() noexcept {
    if(!g_smLifeReady.load(std::memory_order_acquire) || !SmGameThread() ||
       !SmOwnerReady() || !g_sm.hostActive)return {};
    uintptr_t executing=0;
    if(!ReadHitTrace(g_exeBase+0x2A250B0,executing) || !executing)return {};
    const ScriptActorRole* match=nullptr;
    for(const auto& role:g_smRoles)if(role.resource && role.thread==executing) {
        if(match)return {}; // ambiguous executing VM is not an association
        match=&role;
    }
    if(!match)return {};
    auto s=SmLifePrepareRole(*match,false);
    // PrepareRole takes its own immutable execution receipt. A different or
    // unavailable recheck cannot qualify the VM selected by the first read.
    if(s.associated && (s.executingRead!=1 || s.executing!=executing)) {
        s.qualified=false;s.failure="executing-vm-recheck-changed";
    }
    return s;
}
static SmLifePost SmLifeAfter(const SmLifeSnapshot& s) noexcept {
    SmLifePost p{};
    try {
        if(!s.associated)return p;
        if(!g_smLifeReady.load(std::memory_order_acquire) || !SmGameThread()) {
            p.status="post-observer-or-thread-unavailable";return p;
        }
        if(s.role.slot<g_smRoles.size())p.resource=g_smRoles[s.role.slot].resource;
        const bool same=s.role.slot<g_smRoles.size() && SmLifeSameKey(s.role,g_smRoles[s.role.slot]);
        if((!s.revoked && !same) || (s.revoked && p.resource && !same)) {
            p.status=s.revoked?"cleanup-target-reassigned":"active-role-replaced";return p;
        }
        if(!s.qualified){p.status="pre-unqualified";return p;}
        p.status=SmLifeRoots(s.role);
        if(std::strcmp(p.status,"registered-roots"))return p;
        const bool read=ReadHitTrace(s.role.actor+0xD48,p.thread);p.read=read?1:0;
        if(!read){p.status="post-thread-unreadable";return p;}
        p.status=s.revoked?"revoked-cleanup-metadata":"same-active-role";
    } catch(...) {p.read=-1;p.status="post-diagnostic-exception";}
    return p;
}
static void SmLifeEmit(D::Stage stage,const char* result,const SmLifeSnapshot& s,const SmLifePost& post,
    std::uint64_t call,std::uint32_t index,std::uint64_t input,std::uint64_t output,bool read,
    uintptr_t caller=0,std::uint64_t capture=0,bool captureRead=false,int inputRead=-1) noexcept {
    if(!s.associated)return;
    const auto record=s.trace;
    D::Emit(stage,result,s.failure,record,[&](const char* base) {
        if(!std::strstr(base," tuple=")){Log("%s",base);return;}
        Log("%s diagCall=%llu nativeIndex=%u origin=%s preQualified=%u executingVm=%llX executingRead=%d inputTag=%llX outputTag=%llX outputRead=%u expectedCaptureTag=%llX expectedCaptureRead=%u postStatus=%s postRoleResource=%u postD48=%llX postRead=%d caller=%llX schedulerReturn=%u queryAssociation=%s queryTarget=%s inputRead=%d",
            base,static_cast<unsigned long long>(call),index,s.revoked?"revoked-cleanup":"active-role",s.qualified?1u:0u,
            static_cast<unsigned long long>(s.executing),s.executingRead,
            static_cast<unsigned long long>(input),static_cast<unsigned long long>(output),read?1u:0u,
            static_cast<unsigned long long>(capture),captureRead?1u:0u,post.status,post.resource,
            static_cast<unsigned long long>(post.thread),post.read,static_cast<unsigned long long>(caller),
            caller==g_exeBase+0x3D72BE?1u:0u,
            index==14?"executing-role-only":index==238?"direct-admitted-handle":"not-query",
            index==14?"unresolved":index==238?"argument-matches-admitted-handle":"not-query",inputRead);
    });
}
static SmLifeSnapshot SmLifeCapturePrepare(std::uint64_t argument) noexcept {
    auto s=SmLifeForArgument(argument);
    if(g_smLifeReady.load(std::memory_order_acquire) && SmGameThread() && g_sm.hostActive)
        s.captureSerial=++g_smLifeCaptureSerial;
    return s;
}
static void SmLifeCaptureReturned(const SmLifeSnapshot& s,std::uint64_t* args) noexcept {
    if(!s.associated || s.role.slot>=3)return;
    std::uint64_t value=0;const bool read=SmCopyArgs(args,&value,1);
    const auto post=SmLifeAfter(s);const auto call=++g_smLifeCallSequence;
    SmLifeEmit(D::Stage::LifecycleCapture,"actual-capture-return",s,post,call,29,0,value,read);
    if(!g_smLifeReady.load(std::memory_order_acquire) || !SmGameThread())return;
    auto& memo=g_smLifeCaptures[s.role.slot];
    if(memo.snapshot.associated && SmLifeSameKey(memo.snapshot.role,s.role))memo={};
    if(s.qualified && read && R::TagOf(value)==R::kTagInt && s.captureSerial==g_smLifeCaptureSerial &&
       !std::strcmp(post.status,"same-active-role") && post.read==1 && post.thread==s.role.thread) {
        memo={};memo.snapshot=s;memo.output=value;memo.read=true;memo.capture=s.captureSerial;
    }
}
static void SmLifeQuery(std::uint64_t* args,unsigned which) {
    const auto incomingError=GetLastError();
    std::uint64_t argument=0;const bool copied=SmCopyArgs(args,&argument,1);
    const auto s=which==0?SmLifeForExecution():SmLifeForArgument(copied?argument:0);
    // The actual query is called once, including unrelated/unqualified cases.
    // Never catch its exception or execute an extra getter to fill a receipt.
    SetLastError(incomingError);
    reinterpret_cast<TrapFn>(g_smOwnerTraps[kSmOwnerCoreTrapCount+which].original)(args);
    const auto nativeError=GetLastError();
    if(!s.associated || s.role.slot>=3){SetLastError(nativeError);return;}
    std::uint64_t value=0;const bool read=SmCopyArgs(args,&value,1);
    const auto post=SmLifeAfter(s);
    if(!g_smLifeReady.load(std::memory_order_acquire) || !SmGameThread()) {
        SmLifeEmit(D::Stage::LifecycleQuery,"actual-query-output",s,post,++g_smLifeCallSequence,
            which?238u:14u,argument,value,read,0,0,false,copied?1:0);
        SetLastError(nativeError);return;
    }
    const auto& capture=g_smLifeCaptures[s.role.slot];
    const bool captureRead=capture.read && capture.capture==g_smLifeCaptureSerial && SmLifeSameKey(capture.snapshot.role,s.role);
    const auto captureTag=captureRead?capture.output:0;
    auto& memo=g_smLifeQueries[which][s.role.slot];
    if(memo.snapshot.associated && SmLifeSameKey(memo.snapshot.role,s.role) &&
       memo.snapshot.qualified==s.qualified && !std::strcmp(memo.snapshot.failure,s.failure) &&
       memo.snapshot.trace.observed==s.trace.observed && memo.snapshot.trace.observedRead==s.trace.observedRead &&
       memo.snapshot.executing==s.executing && memo.snapshot.executingRead==s.executingRead &&
       memo.input==argument && memo.inputRead==(copied?1:0) && memo.output==value && memo.read==read && memo.capture==captureTag &&
       memo.captureRead==captureRead && memo.postResource==post.resource && memo.postThread==post.thread &&
       memo.postRead==post.read && !std::strcmp(memo.postStatus,post.status)){
        SetLastError(nativeError);return;
    }
    memo={};memo.snapshot=s;memo.input=argument;memo.inputRead=copied?1:0;memo.output=value;memo.read=read;memo.capture=captureTag;
    memo.captureRead=captureRead;memo.postResource=post.resource;memo.postThread=post.thread;memo.postRead=post.read;
    memo.postStatus=post.status;
    SmLifeEmit(D::Stage::LifecycleQuery,"actual-query-output",s,post,++g_smLifeCallSequence,which?238u:14u,
        argument,value,read,0,captureTag,captureRead,copied?1:0);
    SetLastError(nativeError);
}
static void __fastcall SmLifeMotionEnd(std::uint64_t* args) {SmLifeQuery(args,0);}
static void __fastcall SmLifeCaptureId(std::uint64_t* args) {SmLifeQuery(args,1);}
static void __fastcall SmLifeCancel(uintptr_t actor) {
    const auto incomingError=GetLastError();
    const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    SmLifeSnapshot s{};
    if(g_smLifeReady.load(std::memory_order_acquire) && SmGameThread()) {
        const auto* cleanup=SmLifeCleanupContext(actor);
        if(cleanup)s=SmLifePrepareRole(cleanup->role,true);
        else if(g_smLifeReady.load(std::memory_order_acquire)) {
            const ScriptActorRole* match=nullptr;bool ambiguous=false;
            for(const auto& role:g_smRoles)if(role.resource && role.actor==actor) {
                if(match){ambiguous=true;break;}match=&role;
            }
            if(match && !ambiguous)s=SmLifePrepareRole(*match,false);
        }
    }
    const auto call=s.associated?++g_smLifeCallSequence:0;
    if(s.associated)SmLifeEmit(D::Stage::LifecycleCancel,"native-entry",s,{},call,0,0,0,false,caller);
    // Enabled only after the generated trampoline is qualified. Void native
    // ABI, same actor, exactly one original call; native exceptions propagate.
    SetLastError(incomingError);
    g_smLifeCancelOriginal(actor);
    const auto nativeError=GetLastError();
    if(s.associated)SmLifeEmit(D::Stage::LifecycleCancel,"native-return",s,SmLifeAfter(s),call,0,0,0,false,caller);
    SetLastError(nativeError);
}
// Separate POD-only SEH frame: diagnostic context is restored even if native
// cancellation unwinds. It does not catch or replace that native exception.
static void SmLifeInvokeCleanup(SmLifeLocal* local,unsigned previous,std::uint64_t serial,uintptr_t actor) {
    __try {reinterpret_cast<SmLifeCancelFn>(g_exeBase+0x3D6F80)(actor);}
    __finally {
        if(local) {
            if(g_smLifeStorage.Same(local) && local->depth==previous+1 &&
               (previous>=local->frames.size() || (local->frames[previous].serial==serial &&
                g_smLifeStorage.Anchor(local->frames[previous].anchor)))) {
                if(previous<local->frames.size())local->frames[previous]={};
                local->depth=previous;
            } else {
                g_smLifeStorage.Abandon(local);SmLifeRetire("cleanup-return-context-changed");
            }
        }
    }
}
static void SmLifeCancelForCleanup(const ScriptActorRole& released) {
    const auto incomingError=GetLastError();
    const auto s=SmLifePrepareRole(released,true);
    const bool observe=g_smLifeReady.load(std::memory_order_acquire) && SmGameThread();
    auto* local=observe?g_smLifeStorage.Current():nullptr;
    if(observe && !local)SmLifeRetire("cleanup-entry-context-unavailable");
    unsigned previous=0;std::uint64_t serial=0;
    if(local) {
        previous=local->depth;
        if(previous>=local->frames.size()){
            g_smLifeStorage.Reject(local);SmLifeRetire("cleanup-entry-context-overflow");local=nullptr;
        }
        else {
            ++local->depth;
            if(previous<local->frames.size()) {
                auto& frame=local->frames[previous];frame.snapshot=s;
                frame.anchor=reinterpret_cast<uintptr_t>(_AddressOfReturnAddress());
                frame.serial=serial=++g_smLifeCallSequence;
            }
        }
    }
    SetLastError(incomingError);
    SmLifeInvokeCleanup(local,previous,serial,released.actor);
}
static void SmLifeShutdown() {
    g_smLifeReady.store(false,std::memory_order_release);
    for(std::size_t i=kSmOwnerCoreTrapCount;i<std::size(g_smOwnerTraps);++i) {
        auto& slot=g_smOwnerTraps[i];if(!slot.swapped)continue;
        const auto previous=InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(slot.entry),
            reinterpret_cast<void*>(slot.original),reinterpret_cast<void*>(slot.detour));
        if(previous==reinterpret_cast<void*>(slot.detour) || previous==reinterpret_cast<void*>(slot.original))slot.swapped=false;
        else Log("[script-lifecycle] diagnostic restore changed index=%u",slot.index);
    }
    if(g_smLifeCancelEnabled) {
        const auto status=MH_DisableHook(reinterpret_cast<void*>(g_exeBase+0x3D6F80));
        if(status==MH_OK || status==MH_ERROR_DISABLED)g_smLifeCancelEnabled=false;
        else Log("[script-lifecycle] disable cancel failed status=%d",status);
    }
    // Keep trampoline storage until the existing global quiescent MH teardown.
}
static bool SmLifeTrampoline() noexcept {
    std::uint8_t body[19]{};
    uintptr_t target=0;
    if(!g_smLifeCancelOriginal || !ReadHitTraceMemory(reinterpret_cast<uintptr_t>(g_smLifeCancelOriginal),body,sizeof(body)) ||
       std::memcmp(body,R::kOwnerCancel,5) || body[5]!=0xFF || body[6]!=0x25 || body[7] || body[8] || body[9] || body[10])return false;
    std::memcpy(&target,body+11,sizeof(target));
    return target==g_exeBase+0x3D6F85;
}
static bool SmLifeEnabledEntry() noexcept {
    std::uint8_t entry[5]{},relay[14]{};std::int32_t displacement=0;uintptr_t target=0;
    if(!ReadHitTraceMemory(g_exeBase+0x3D6F80,entry,sizeof(entry)) || entry[0]!=0xE9)return false;
    std::memcpy(&displacement,entry+1,sizeof(displacement));
    const auto at=g_exeBase+0x3D6F85+static_cast<std::intptr_t>(displacement);
    if(!ReadHitTraceMemory(at,relay,sizeof(relay)) || relay[0]!=0xFF || relay[1]!=0x25 ||
       relay[2] || relay[3] || relay[4] || relay[5])return false;
    std::memcpy(&target,relay+6,sizeof(target));
    return target==reinterpret_cast<uintptr_t>(&SmLifeCancel);
}
static bool SmLifeInstall(uintptr_t table) {
    const auto fail=[](const char* reason) {
        SmLifeShutdown();Log("[script-lifecycle] unavailable reason=%s; existing owner suppression retained",reason);return false;
    };
    if(g_smLifeRetired.load(std::memory_order_acquire))return fail("observer-permanently-retired");
    // Core validates its original63-byte cancel body before this observer can
    // patch it. Refusal/rollback here never revokes the original six traps.
    if(!SmOwnerPin(0x3D6F80,R::kOwnerCancel,sizeof(R::kOwnerCancel)) ||
       !SmOwnerPin(0x3D7280,R::kOwnerCancelScheduler,sizeof(R::kOwnerCancelScheduler)))return fail("cancel-or-caller-pin");
    if(!g_smLifeStorage.Init(reinterpret_cast<const void*>(&SmLifeInstall)))return fail("fiber-storage");
    for(std::size_t i=kSmOwnerCoreTrapCount;i<std::size(g_smOwnerTraps);++i) {
        auto& slot=g_smOwnerTraps[i];std::uint32_t flags=0;slot.entry=R::EntryRva(table,slot.index);
        if(!ReadHitTrace(slot.entry,slot.original) || slot.original!=g_exeBase+slot.rva ||
           !ReadHitTrace(slot.entry+8,flags) || flags!=slot.flags || !SmOwnerPin(slot.rva,slot.body,slot.size))return fail("query-pin-or-table");
    }
    const auto created=MH_CreateHook(reinterpret_cast<void*>(g_exeBase+0x3D6F80),reinterpret_cast<void*>(&SmLifeCancel),
        reinterpret_cast<void**>(&g_smLifeCancelOriginal));
    if(created!=MH_OK)return fail("cancel-create");
    if(!SmLifeTrampoline())return fail("cancel-trampoline-layout");
    if(MH_EnableHook(reinterpret_cast<void*>(g_exeBase+0x3D6F80))!=MH_OK)return fail("cancel-enable");
    g_smLifeCancelEnabled=true;
    if(!SmLifeEnabledEntry())return fail("cancel-entry-layout");
    for(std::size_t i=kSmOwnerCoreTrapCount;i<std::size(g_smOwnerTraps);++i) {
        auto& slot=g_smOwnerTraps[i];
        slot.swapped=InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(slot.entry),
            reinterpret_cast<void*>(slot.detour),reinterpret_cast<void*>(slot.original))==reinterpret_cast<void*>(slot.original);
        if(!slot.swapped)return fail("query-slot-replaced");
    }
    g_smLifeReady.store(true,std::memory_order_release);
    Log("[script-lifecycle] installed motionEnd=1 captureId=1 cancel=1 queryBudget=2048 cancelBudget=1024 captureBudget=512 changedOnly=1");
    return true;
}
