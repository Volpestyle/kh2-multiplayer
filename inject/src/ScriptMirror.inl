// ScriptMirror.inl - VUH-2252 (D14 item 3): the host's script calls on its mirrored enemies.
// Included by EntityHook.cpp after EnemyMirror.inl (the driven table) and g_resolveHandle.
// On by default with the enemy mirror (enemysync::MirrorRequested); KH2COOP_SCRIPT_MIRROR=0 parks it.
//
// Host (role 1, live session):
//   Swap  each trap's .data entry in the bdscript tables (exe+0x753490) for a detour, only after
//         every entry's function, flags and full body match this build. The detour always runs the
//         native trap; before it, it copies the arguments the trap reads (a start returns its effect
//         in args[0]) and looks up the effect an effect call names (a kill zeroes its part).
//   Actor calls on an announced, mirrored host spawn go to the Sender: a repeat of the last value
//         for its key is dropped, a change replaces a queued one.
//   Effects a mirrored spawn's script starts get a fresh id; a start is sent with it, then each
//         field the script sets (read back from the effect's part), pauses and ends. An effect call
//         on any other effect is counted as untracked and not sent.
//   Flush at frame start, the queued calls in call order: up to two ScriptCalls of 32 records.
// Client (role 2):
//   Apply each admitted record to the copy EnemyMirror drives for that netId (brain skipped):
//         an actor call through the native trap with that copy's own handle (obj_set_xyzrot's
//         float4 through its helper 0x3B65F0); a start through the native trap, pairing the host id
//         with the effect it made here; a field as a write into the paired effect's part; an effect
//         call through the native trap with the paired effect's handle. A paired effect is acted on
//         only while it is still the same live instance. A record for a copy not driven yet waits
//         in the Hold (180 frames) and later records for that netId wait behind it.
// Both: one "[script-mirror] ... sec" line per second with activity: the per-second message counts.
// Actor-owner playback includes native capture/attachment, pose and collision-resource
// helpers. HP damage still enters the existing native BuildHit/D4 owner path.

namespace scriptmirror {

namespace R = rules;
namespace D = ::kh2coop::rcdiag;
static D::Record SmRcClaim(const enemysync::ScriptReactionCompletionRecord& c) noexcept {
    return D::Claim(c.epoch,c.resource,c.objectId,c.netId,c.command,c.ownerSlot,c.requesterConnectionId,c.occurrence);
}
static void SmRcHop(D::Stage stage,const char* result,const char* reason,const D::Record& r) noexcept {
    D::Emit(stage,result,reason,r,[](const char* line){Log("%s",line);});
}
using TrapFn = void(__fastcall*)(std::uint64_t* args);
using XyzRotFn = void(__fastcall*)(uintptr_t actor, const float* rotation);

struct Slot { uintptr_t entry = 0, native = 0; bool swapped = false; };
struct ReactionBinding {
    std::uint32_t handle{}, resource{}, objectId{}, epoch{}, command{}, expires{};
    std::uint16_t netId{};
    uintptr_t attack{}, owner{};
    float range{};
    std::uint64_t occurrence{}, connection{};
    std::uint8_t slot{0xFF};
    bool consumed{};
};
struct State {
    bool requested = false, hostActive = false, swapRefused = false;
    bool clientChecked = false, clientReady = false;
    std::uint8_t role = 0;
    std::uint32_t generation = 0, transition = 0, load = 0;
    Slot slots[R::kTrapCount] {};
    XyzRotFn xyzRot = nullptr;
    R::Sender sender;
    R::Retirements retirements;
    R::ReactionOccurrences occurrences;
    std::array<ReactionBinding,R::ReactionOccurrences::Capacity> reactions{};
    R::HostEffects hostEffects, hostAttacks;
    R::ClientEffects clientEffects, clientAttacks;
    R::Hold hold;
    R::Second second;
    std::uint64_t secondMs = 0;
    std::uint64_t faults = 0, invalid = 0;
    unsigned unmappedLogs = 0;  // this room
    unsigned resourceMissLogs = 0, storedHandleLogs = 0, invalidLogs = 0; // bounded raw-tag diagnostics per room
    ::kh2coop::inject::scriptbirth::HostBirths births; // VUH-2605: host script-born children announced to clients
    unsigned birthLogs = 0;         // this room
};
static State g_sm;
static std::atomic<bool> g_smLifetimeReliable{true};
static bool g_smLifetimeReady = false;
using AttackConstructFn = uintptr_t(__fastcall*)(uintptr_t,uintptr_t,uintptr_t,std::uint32_t,std::uint32_t,int,std::uint32_t);
using AttackDestroyFn = void(__fastcall*)(uintptr_t);
static AttackConstructFn g_smAttackConstruct = nullptr;
static AttackDestroyFn g_smAttackDestroy = nullptr;
using ReactionCallbackFn=void(__fastcall*)(uintptr_t,uintptr_t,std::uint32_t,const std::uint32_t*);
static ReactionCallbackFn g_smReactionCallback=nullptr;
static bool SmInstallReactionCallback();
static void SmTickReactions();
static int SmApplyReactionOffer(const R::Call&,uintptr_t);
static int SmApplyReactionOwner(const R::Call&,uintptr_t);
static bool SmCompleteReaction(const enemysync::ScriptReactionCompletionRecord&);
static bool SmAxelPlayer(std::uint64_t*,uintptr_t,const enemysync::DownedScope&);
static R::AxelAct SmAxelAct(const std::uint64_t*,const char*);
static void SmAxelSyncHeld();
static int SmAxelHeldBy(uintptr_t);
static bool SmAxelRoleHeld(std::uint32_t,std::uint8_t,uintptr_t,uintptr_t);
static bool SmAxelCamera(const std::uint64_t*);
static R::AxelAct SmAxelCaptureQuery(const std::uint64_t*);
static bool SmAxelCameraWatch(const std::uint64_t*);
static bool SmAxelCameraUnobserved();
static bool SmAxelThreadWatched() noexcept;
static void SmAxelThreadStarted(const std::uint64_t*,const std::uint64_t*);
static bool SmAxelFadePending() noexcept;
static bool SmAxelFade(R::AxelCameraKind,const std::uint64_t*);
struct ScriptActorRole; // before its first use below (Kit's 09d8d37d: MSVC C2065)
static bool SmAxelApplyCameraOp(ScriptActorRole&,const R::Call&,uintptr_t) noexcept;
static void SmAxelRetireCamera(const ScriptActorRole&) noexcept;
static void SmAxelClientCameraTick();
static void SmAxelCameraNativesInstall(uintptr_t,bool(*)(std::uint32_t,const std::uint8_t*,std::size_t));
static void SmAxelCameraHooksInstall(bool(*)(R::AxelCameraHook));
static void SmAxelCameraHooksRevoke();
static void SmAxelClientReset();
static bool SmAxelCloneHeld(const ScriptActorRole&);
static bool SmAxelApplyHold(ScriptActorRole&,bool) noexcept;
static R::Call SmAxelHoldRecord(const ScriptActorRole&,bool);
static bool SmSetReactionCommand(const ReactionBinding&,std::uint32_t) noexcept;
static bool SmOwnerInstall();
static std::uint16_t SmNativeReactionCurrent(std::uint16_t) noexcept;
static bool SmNativeReactionReady() noexcept;
static bool SmNativeReactionPins();
static bool SmNativeReactionInstall();
static void SmNativeReactionShutdown();
static bool SmNativeReactionCallbackAllowed(uintptr_t,std::uint32_t,const std::uint32_t*,D::Record* =nullptr);
static void SmOwnerReset(bool);
static uintptr_t SmOwnerVmSource(std::uint16_t&,std::uint32_t&);
static void SmOwnerFrame();
static int SmOwnerApply(const R::Call&,uintptr_t);
static bool SmOwnerAttackSource(std::uint64_t,std::uint16_t&,std::uint32_t&,std::uint8_t&,std::uint32_t&);
static uintptr_t SmOwnerAttackActor(std::uint32_t,std::uint16_t,std::uint32_t,std::uint8_t&);

static bool SmGameThread() noexcept { return spawncontroller::IsDiagnosticGameThread(); }

// Invalidate BEFORE destruction or construction. Reusing the same handle,
// address, owner and attack ID can never make an old wire resource live again.
static void SmForgetAllocation(uintptr_t attack) {
    if (!g_sm.requested) return;
    if (!SmGameThread()) { g_smLifetimeReliable.store(false); return; }
    if (const auto* paired=g_sm.hostAttacks.FindAllocation(attack)) {
        R::Call end{};
        end.netId=paired->netId;end.objectId=paired->objectId;end.effect=paired->id;
        end.trap=static_cast<std::uint8_t>(R::FindTrap(2,13));
        if(g_sm.sender.Observe(end)==R::Sender::Outcome::Full && !g_sm.retirements.Put(end))
            g_smLifetimeReliable.store(false);
    }
    g_sm.hostAttacks.ForgetAllocation(attack);
    g_sm.clientAttacks.ForgetAllocation(attack);
    for(auto& reaction:g_sm.reactions)if(reaction.attack==attack) {
        g_sm.occurrences.Retire(reaction.handle,reaction.resource);
        reaction={};
    }
}
static uintptr_t __fastcall SmAttackConstruct(uintptr_t attack,uintptr_t owner,uintptr_t source,
    std::uint32_t id,std::uint32_t kind,int collision,std::uint32_t serial) {
    SmForgetAllocation(attack);
    return g_smAttackConstruct(attack,owner,source,id,kind,collision,serial);
}
static void __fastcall SmAttackDestroy(uintptr_t attack) {
    SmForgetAllocation(attack);
    g_smAttackDestroy(attack);
}
static bool SmInstallAttackLifetimes() {
    std::uint8_t construct[sizeof(R::kAttackConstruct)]{}, destroy[sizeof(R::kAttackDestroy)]{};
    if (!ReadHitTraceMemory(g_exeBase+0x3CF830,construct,sizeof(construct)) ||
        !ReadHitTraceMemory(g_exeBase+0x3CFB10,destroy,sizeof(destroy)) ||
        std::memcmp(construct,R::kAttackConstruct,sizeof(construct)) ||
        std::memcmp(destroy,R::kAttackDestroy,sizeof(destroy))) return false;
    void* ctor=reinterpret_cast<void*>(g_exeBase+0x3CF830);
    void* dtor=reinterpret_cast<void*>(g_exeBase+0x3CFB10);
    auto status=MH_CreateHook(ctor,reinterpret_cast<void*>(&SmAttackConstruct),reinterpret_cast<void**>(&g_smAttackConstruct));
    if(status!=MH_OK)return false;
    status=MH_CreateHook(dtor,reinterpret_cast<void*>(&SmAttackDestroy),reinterpret_cast<void**>(&g_smAttackDestroy));
    if(status!=MH_OK){MH_RemoveHook(ctor);return false;}
    if(MH_EnableHook(ctor)!=MH_OK){MH_RemoveHook(dtor);MH_RemoveHook(ctor);return false;}
    if(MH_EnableHook(dtor)!=MH_OK){MH_DisableHook(ctor);MH_RemoveHook(dtor);MH_RemoveHook(ctor);return false;}
    return true;
}

static bool SmEnvParked() {
    char v[8] {};
    const DWORD n = GetEnvironmentVariableA("KH2COOP_SCRIPT_MIRROR", v, sizeof(v));
    return n > 0 && n < sizeof(v) && v[0] == '0' && v[1] == 0;
}

// The table entry for trap i, its function and flags, and whether the function body is this build's.
static bool SmReadEntry(std::size_t i, uintptr_t& entry, uintptr_t& fn, std::uint32_t& flags, bool& bodyOk) noexcept {
    entry = 0; fn = 0; flags = 0; bodyOk = false;
    const auto& t = R::kTraps[i];
    uintptr_t table = 0;
    if (!ReadHitTrace(g_exeBase + R::kTrapTables + 8u * t.table, table) || !table) return false;
    entry = R::EntryRva(table, t.index);
    if (!ReadHitTrace(entry, fn) || !ReadHitTrace(entry + 8, flags)) return false;
    std::uint8_t body[512] {};
    const uintptr_t native = g_exeBase + t.fnRva;
    bodyOk = t.bodySize <= sizeof(body) && ReadHitTraceMemory(native, body, t.bodySize) && R::BodyMatches(i, body);
    return true;
}

// ---- raw readers (POD only: SEH-safe) ---------------------------------------------------------
static uintptr_t SmDecode(std::uint32_t handle) noexcept {
    if (!handle || !g_resolveHandle) return 0;
    __try { return g_resolveHandle(handle); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
// SmDecode, telling a lookup that could not be made (no resolver, a caught fault) from an answer, 0 included.
static bool SmDecodeKnown(std::uint32_t handle, uintptr_t& out) noexcept {
    out = 0;
    if (!handle) return true;
    if (!g_resolveHandle) return false;
    __try { out = g_resolveHandle(handle); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { out = 0; return false; }
}
// The actor a trap's actor argument names (the argument, then that object's +4), and its object id.
static uintptr_t SmActorOf(std::uint64_t arg, std::uint32_t& objectId) noexcept {
    objectId = 0;
    if (R::TagOf(arg) != R::kTagAddress) return 0;
    __try {
        const uintptr_t object = SmDecode(R::BitsOf(arg));
        if (!object) return 0;
        const uintptr_t actor = SmDecode(*reinterpret_cast<const std::uint32_t*>(object + 4));
        if (!actor) return 0;
        const uintptr_t objentry = *reinterpret_cast<const uintptr_t*>(actor + offsets::actor::OBJENTRY_PTR);
        if (!objentry) return 0;
        objectId = *reinterpret_cast<const std::uint32_t*>(objentry + offsets::objentry::OBJECT_ID);
        return actor;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}
static bool SmReadFloat4(uintptr_t at, std::uint64_t* values) noexcept {
    if (!at) return false;
    __try {
        const auto* v = reinterpret_cast<const std::uint32_t*>(at);
        for (unsigned k = 0; k < 4; ++k) values[k] = R::Tagged(R::kTagFloat, v[k]);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
// An effect instance's flags and part; false when it can't be read.
static bool SmEffectState(uintptr_t effect, bool& killed, uintptr_t& part) noexcept {
    killed = true; part = 0;
    if (!effect) return false;
    __try {
        killed = (*reinterpret_cast<const std::uint32_t*>(effect + R::kEffectFlags) & R::kEffectKilled) != 0;
        part = *reinterpret_cast<const uintptr_t*>(effect + R::kEffectPart);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
// The handle still names the same live effect with the part it had when it started.
static bool SmSameEffect(std::uint32_t handle, uintptr_t effect, uintptr_t part) noexcept {
    bool killed = true;
    uintptr_t now = 0;
    return effect && SmDecode(handle) == effect && SmEffectState(effect, killed, now) && !killed && now && now == part;
}
// ATTACK_MAGIC: flags+8 (0x800 retires it), owner handle+0x10. The script
// creator re-pairs every allocation, including reuse of a native handle.
static bool SmAttackState(std::uint32_t handle, uintptr_t attack, uintptr_t owner,const char** failure=nullptr) noexcept {
    std::uint32_t flags=0, ownerHandle=0;uintptr_t vtable=0;const char* why="unknown";
    const bool ok=D::Check(g_smLifetimeReady,"attack-lifetime-not-ready",why) &&
        D::Check(g_smLifetimeReliable.load(),"attack-lifetime-unreliable",why) &&
        D::Check(attack!=0,"attack-zero",why) && D::Check(owner!=0,"attack-owner-zero",why) &&
        D::Check(SmDecode(handle)==attack,"attack-handle-replaced",why) &&
        D::Check(ReadHitTrace(attack,vtable),"attack-vtable-unreadable",why) &&
        D::Check(vtable==g_exeBase+0x5D7090,"attack-vtable",why) &&
        D::Check(ReadHitTrace(attack+8,flags),"attack-flags-unreadable",why) &&
        D::Check(!(flags&0x800u),"attack-retired-flag",why) &&
        D::Check(ReadHitTrace(attack+0x10,ownerHandle),"attack-owner-unreadable",why) &&
        D::Check(SmDecode(ownerHandle)==owner,"attack-owner-replaced",why);
    if(!ok && failure)*failure=why;return ok;
}


// ---- host ----------------------------------------------------------------------------------------
// What a detour keeps from before the native call.
struct Before {
    std::uint64_t args[5] {};
    std::uint32_t argc = 0;
    R::NativeResourceReference resource{}; // checked snapshot; native reentry can retire the registry entry
};
static void SmBefore(std::size_t i, const std::uint64_t* args, Before& b) noexcept;
static void SmObserve(std::size_t i, const Before& b, const std::uint64_t* after);
template <std::size_t I> static void __fastcall SmDetour(std::uint64_t* args) {
    Before b;
    const bool active = g_sm.hostActive;
    if (active) SmBefore(I, args, b);
    reinterpret_cast<TrapFn>(g_sm.slots[I].native)(args);  // the native trap, always
    if (active) SmObserve(I, b, args);
}
template <std::size_t... I> static constexpr std::array<TrapFn, sizeof...(I)> SmMakeDetours(std::index_sequence<I...>) {
    return {&SmDetour<I>...};
}
static constexpr auto kSmDetours = SmMakeDetours(std::make_index_sequence<R::kTrapCount>{});

static bool SmCopyArgs(const std::uint64_t* args, std::uint64_t* out, std::uint32_t n) noexcept {
    __try {
        for (std::uint32_t k = 0; k < n; ++k) out[k] = args[k];
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
static void SmBefore(std::size_t i, const std::uint64_t* args, Before& b) noexcept {
    const auto& t = R::kTraps[i];
    b.argc = (t.flags & 0xFFFF) < 5 ? (t.flags & 0xFFFF) : 5;
    if (!SmCopyArgs(args, b.args, b.argc)) { b.argc = 0; return; }
    if (t.kind != R::Kind::EffectField && t.kind != R::Kind::EffectCall &&
        t.kind != R::Kind::AttackField && t.kind != R::Kind::AttackCall && t.kind != R::Kind::AttackRc &&
        t.kind != R::Kind::AttackStrike) return;
    if (R::IsAttackKind(t.kind)) {
        b.resource=g_sm.hostAttacks.CaptureNative(b.args[0],[](std::uint32_t handle,const R::HostEffect& paired) {
            return SmAttackState(handle,paired.effect,paired.part);
        });
    } else {
        b.resource=g_sm.hostEffects.CaptureNative(b.args[0],[](std::uint32_t handle,const R::HostEffect& paired) {
            return SmSameEffect(handle,paired.effect,paired.part);
        });
    }
    if(b.resource.status!=R::NativeResourceStatus::Paired && g_sm.resourceMissLogs<12) {
        ++g_sm.resourceMissLogs;
        const auto why=b.resource.status==R::NativeResourceStatus::WrongTag?"tag":
            b.resource.status==R::NativeResourceStatus::Unpaired?"unpaired":"native";
        Log("[script-mirror] resource miss trap=%s reason=%s raw=%016llX tag=%08X handle=%08X registered=%u id=%u",
            t.name,why,static_cast<unsigned long long>(b.args[0]),R::TagOf(b.args[0]),b.resource.handle,
            b.resource.value.id?1u:0u,b.resource.value.id);
    } else if(b.resource.status==R::NativeResourceStatus::Paired && (R::TagOf(b.args[0])==R::kTagInt || R::TagOf(b.args[0])==R::kTagWork) &&
              g_sm.storedHandleLogs<4) {
        ++g_sm.storedHandleLogs;
        Log("[script-mirror] stored handle accepted trap=%s raw=%016llX handle=%08X id=%u",
            t.name,static_cast<unsigned long long>(b.args[0]),b.resource.handle,b.resource.value.id);
    }
}

static bool SmInvalidValues(const R::Call& c,const char* reason) {
    ++g_sm.invalid;
    if(g_sm.invalidLogs++<8)Log("[script-mirror] invalid trap=%s reason=%s values=%016llX,%016llX,%016llX,%016llX",
        c.trap<R::kTrapCount?R::kTraps[c.trap].name:"unknown",reason,
        static_cast<unsigned long long>(c.values[0]),static_cast<unsigned long long>(c.values[1]),
        static_cast<unsigned long long>(c.values[2]),static_cast<unsigned long long>(c.values[3]));
    return false;
}
static bool SmNativeScalars(std::size_t trap,const Before& b,R::Call& c) {
    return R::NormalizeNativeScalars(trap,b.args,c.values) || SmInvalidValues(c,"native-scalar");
}
static bool SmEmit(R::Call c) {
    if (!R::ValidValues(c.trap, c.values)) return SmInvalidValues(c,"wire-values");
    ++g_sm.second.calls;
    ++g_sm.second.perTrap[c.trap];
    switch (g_sm.sender.Observe(c)) {
        case R::Sender::Outcome::Queued: ++g_sm.second.queued; break;
        case R::Sender::Outcome::Replaced: ++g_sm.second.replaced; break;
        case R::Sender::Outcome::Repeat: ++g_sm.second.repeats; break;
        case R::Sender::Outcome::Full: ++g_sm.second.full; return false;
    }
    return true;
}

// The announced, mirrored host spawn an actor argument names; 0 otherwise (counted, and the first
// kUnmappedLogs per room logged with their object id: which actors a fight's calls come from).
static constexpr unsigned kUnmappedLogs = 12;
static std::uint16_t SmMirroredSpawn(std::size_t trap, std::uint64_t arg, std::uint32_t& objectId) {
    const uintptr_t actor = SmActorOf(arg, objectId);
    const std::uint16_t netId = actor ? enemysync::HostEnemyNetId(actor, objectId) : std::uint16_t{0};
    if (!netId) {  // not an announced spawn: the player, party, weapons, script-made actors
        ++g_sm.second.unmapped;
        if (g_sm.unmappedLogs < kUnmappedLogs) {
            ++g_sm.unmappedLogs;
            Log("[script-mirror] unmapped trap=%s objectId=%u actor=%llX", R::kTraps[trap].name, objectId,
                static_cast<unsigned long long>(actor ? actor - g_exeBase : 0));
        }
        return 0;
    }
    if (!enemymirror::FamilyAllowed(objectId)) { ++g_sm.second.notMirrored; return 0; }  // its client copy keeps its brain
    return netId;
}

// VUH-2605: one exact sysobj_appear return inside the admitted Twilight Thorn's own script. Only the
// executing VM's announced 307 creating a 317 is recorded; every other appear stays native-only. The host
// publishes nothing here: SmTickBirths sends the birth once the census has announced that exact child.
static void SmObserveBirth(const Before& b, const std::uint64_t* after) {
    namespace SB = ::kh2coop::inject::scriptbirth;
    if (b.argc < 1 || R::TagOf(b.args[0]) != R::kTagInt || R::BitsOf(b.args[0]) != SB::kChildObjectId) return;
    std::uint16_t parentNetId = 0;
    std::uint32_t parentObjectId = 0;
    const uintptr_t parent = SmOwnerVmSource(parentNetId, parentObjectId);
    if (!parent || parentObjectId != SB::kParentObjectId) return;
    std::uint64_t result = 0;
    uintptr_t entry = 0;
    std::uint32_t childObjectId = 0;
    const uintptr_t child = SmCopyArgs(after, &result, 1) && R::TagOf(result) == R::kTagAddress ? SmDecode(R::BitsOf(result)) : 0;
    const bool exact = child && child != parent && ReadHitTrace(child + offsets::actor::OBJENTRY_PTR, entry) && entry &&
        ReadHitTrace(entry + offsets::objentry::OBJECT_ID, childObjectId) && childObjectId == SB::kChildObjectId;
    const auto outcome = exact ? g_sm.births.Add(parentNetId, child, g_frameCounter, enemysync::WorldSessionGeneration()) :
                                 SB::Observe::Refused;
    if (g_sm.birthLogs < 16) {
        ++g_sm.birthLogs;
        const char* what = outcome == SB::Observe::Added ? "recorded" : outcome == SB::Observe::Full ? "full" :
                           outcome == SB::Observe::Exhausted ? "exhausted" : "not-exact-child";
        Log("[script-birth] host observe parent=%u child=%llX childObject=%u result=%s occurrence=%llu frame=%u", parentNetId,
            static_cast<unsigned long long>(child ? child - g_exeBase : 0), childObjectId, what,
            static_cast<unsigned long long>(outcome == SB::Observe::Added ? g_sm.births.NextOccurrence() - 1 : 0), g_frameCounter);
    }
}
static void SmObserve(std::size_t i, const Before& b, const std::uint64_t* after) {
    ++g_sm.second.raw;
    ++g_sm.second.rawPerTrap[i];
    if (!SmGameThread()) { ++g_sm.second.offThread; return; }
    if (!b.argc) { ++g_sm.faults; return; }
    const auto& t = R::kTraps[i];
    R::Call c {};
    c.trap = static_cast<std::uint8_t>(i);
    switch (t.kind) {
        case R::Kind::AttackStart: {
            if (g_sm.retirements.Size()+g_sm.hostAttacks.Size()>=R::Retirements::kCap) {
                ++g_sm.second.full;return;
            }
            std::uint8_t ownerSlot=0xFF;std::uint32_t ownerRole=0;
            if(SmOwnerAttackSource(b.args[0],c.netId,c.objectId,ownerSlot,ownerRole))
                c.trap=static_cast<std::uint8_t>(R::FindTrap(255,9));
            else c.netId=SmMirroredSpawn(i,b.args[0],c.objectId);
            if(!c.netId)return;
            std::uint64_t result=0;
            if(!SmCopyArgs(after,&result,1) || R::TagOf(result)!=R::kTagAddress)return;
            const auto handle=R::BitsOf(result);
            const auto attack=SmDecode(handle);
            std::uint32_t ignored=0;
            const auto owner=SmActorOf(b.args[0],ignored);
            if(!SmAttackState(handle,attack,owner))return;
            if(!SmNativeScalars(i,b,c))return;
            if(ownerSlot!=0xFF) {
                for(unsigned k=3;k>0;--k)c.values[k]=c.values[k-1];
                c.values[0]=R::Tagged(R::kTagInt,ownerRole);
            }
            if(!R::ValidValues(c.trap,c.values)){SmInvalidValues(c,"native-start");return;}
            c.effect=g_sm.hostAttacks.Add(handle,attack,owner,c.netId,c.objectId,false,ownerSlot,ownerRole);
            if (!c.effect) { ++g_sm.second.full; return; }
            SmEmit(c);
            return;
        }
        case R::Kind::ReactionOwner:
        case R::Kind::ReactionOffer: return; // wire-only operations
        case R::Kind::ActorBirth: return;   // wire-only; the host sends it from SmTickBirths
        case R::Kind::ActorBirthObserve: SmObserveBirth(b, after); return;
        case R::Kind::Owner:
        case R::Kind::OwnerAttackStart:return;
        case R::Kind::AttackStrike:
        case R::Kind::AttackRc:
        case R::Kind::AttackField:
        case R::Kind::AttackCall: {
            if(b.resource.status!=R::NativeResourceStatus::Paired || !g_sm.hostAttacks.StillPaired(b.resource)) {
                if(b.resource.status==R::NativeResourceStatus::NotCurrent && g_sm.hostAttacks.StillPaired(b.resource))
                    g_sm.hostAttacks.Erase(b.resource.handle);
                ++g_sm.second.untracked;return;
            }
            const auto tracked=b.resource.value;
            c.netId=tracked.netId;c.objectId=tracked.objectId;c.effect=tracked.id;
            if(t.kind==R::Kind::AttackStrike) {
                std::uint32_t targetId=0;
                const auto target=SmActorOf(b.args[1],targetId);
                // The paired source enemy is the victim. Player attacks stay on
                // their actual owner and use D4; no host clone damage exception.
                if(tracked.ownerSlot==0xFF || enemysync::HostEnemyNetId(target,targetId)!=c.netId || targetId!=c.objectId)return;
                if(!SmNativeScalars(i,b,c))return;
            } else if(t.kind==R::Kind::AttackRc) {
                // The native receiver can be a local BDX object. It stays on
                // the host; the client uses its source actor only as a callback
                // anchor and never executes the receiver's workspace.
                // Validate before installing authority or clearing native attack+11C.
                if(!SmNativeScalars(i,b,c))return;
                ReactionBinding* entry=nullptr;
                for(auto& r:g_sm.reactions)if(r.resource==tracked.id){entry=&r;break;}
                if(!entry)for(auto& r:g_sm.reactions)if(!r.resource){entry=&r;break;}
                if(entry) {
                    *entry={};entry->handle=b.resource.handle;entry->resource=tracked.id;
                    entry->netId=tracked.netId;entry->objectId=tracked.objectId;
                    entry->attack=tracked.effect;entry->owner=tracked.part;
                    entry->command=R::BitsOf(c.values[0]);
                    entry->range=std::max(R::FloatOf(c.values[1]),R::FloatOf(c.values[2]));
                }
                // No seat receives an unarbitrated prompt, including seat 0.
                const std::uint32_t noCommand=0;
                std::memcpy(reinterpret_cast<void*>(tracked.effect+0x11C),&noCommand,sizeof(noCommand));
            } else if(t.kind==R::Kind::AttackField) {
                if(!SmReadFloat4(tracked.effect+t.field,c.values)){++g_sm.faults;return;}
            } else if(!SmNativeScalars(i,b,c))return;
            const bool queued=SmEmit(c);
            if(t.ends) {
                if(!queued && !g_sm.retirements.Put(c)) {
                    // Admission above reserves a deferred slot for every resource.
                    Log("[script-mirror] retirement invariant failed id=%u",c.effect);
                    g_smLifetimeReliable.store(false);
                }
                g_sm.hostAttacks.Erase(b.resource.handle);
            }
            return;
        }
        case R::Kind::Actor:
        case R::Kind::ActorEvent: {
            c.netId = SmMirroredSpawn(i, b.args[0], c.objectId);
            if (!c.netId) return;
            if (t.vector) {
                if (R::TagOf(b.args[1]) != R::kTagAddress || !SmReadFloat4(SmDecode(R::BitsOf(b.args[1])), c.values)) { ++g_sm.faults; return; }
            } else {
                for (unsigned k = 0; k < t.values; ++k) c.values[k] = b.args[1 + k];
            }
            SmEmit(c);
            if (t.kind == R::Kind::ActorEvent) g_sm.hostEffects.EraseOwner(c.netId);  // kill_all: its effects are gone
            return;
        }
        case R::Kind::EffectStart:
        case R::Kind::EffectStartPos: {
            c.netId = SmMirroredSpawn(i, b.args[0], c.objectId);
            if (!c.netId) return;
            std::uint64_t result = 0;
            if (!SmCopyArgs(after, &result, 1) || R::TagOf(result) != R::kTagAddress || !R::BitsOf(result)) return;  // no effect made
            const std::uint32_t handle = R::BitsOf(result);
            const uintptr_t effect = SmDecode(handle);
            bool killed = true;
            uintptr_t part = 0;
            if (!SmEffectState(effect, killed, part) || killed || !part) return;
            if(!SmNativeScalars(i,b,c))return;
            c.effect = g_sm.hostEffects.Add(handle, effect, part, c.netId, c.objectId);
            if (t.kind == R::Kind::EffectStart) {
                SmEmit(c);
                return;
            }
            // start_pos(id, pos, a3, a4) is start(id, a3, a4) then set_pos(pos).
            c.trap = static_cast<std::uint8_t>(R::FindTrap(t.wireTable, t.wireIndex));
            SmEmit(c);
            R::Call pos = c;
            pos.trap = static_cast<std::uint8_t>(R::FindTrap(0, 38));
            pos.values[0] = pos.values[1] = pos.values[2] = pos.values[3] = 0;
            if (!SmReadFloat4(part + R::kTraps[pos.trap].field, pos.values)) { ++g_sm.faults; return; }
            SmEmit(pos);
            return;
        }
        case R::Kind::EffectField:
        case R::Kind::EffectCall: {
            if (b.resource.status!=R::NativeResourceStatus::Paired || !g_sm.hostEffects.StillPaired(b.resource)) {
                if (b.resource.status==R::NativeResourceStatus::NotCurrent && g_sm.hostEffects.StillPaired(b.resource))
                    g_sm.hostEffects.Erase(b.resource.handle);  // never erase a replacement born inside the call
                ++g_sm.second.untracked;
                return;
            }
            const R::HostEffect tracked = b.resource.value;
            c.netId = tracked.netId;
            c.objectId = tracked.objectId;
            c.effect = tracked.id;
            c.trap = static_cast<std::uint8_t>(R::FindTrap(t.wireTable, t.wireIndex));
            if (t.kind == R::Kind::EffectField) {
                if (!SmReadFloat4(tracked.part + t.field, c.values)) { ++g_sm.faults; return; }  // what the setter wrote
            } else {
                if(!SmNativeScalars(i,b,c))return;
            }
            SmEmit(c);
            if (t.ends) g_sm.hostEffects.Erase(b.resource.handle);
            return;
        }
    }
}

static void SmRestoreAll() noexcept {
    for (std::size_t i = 0; i < R::kTrapCount; ++i) {
        auto& s = g_sm.slots[i];
        if (!s.swapped) continue;
        void* prior = InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(s.entry),
                                                        reinterpret_cast<void*>(s.native), reinterpret_cast<void*>(kSmDetours[i]));
        if (prior != reinterpret_cast<void*>(kSmDetours[i])) Log("[script-mirror] restore-mismatch trap=%s", R::kTraps[i].name);
        s.swapped = false;
    }
}

// All or nothing: every entry must hold this build's native function before any is swapped.
static bool SmSwapAll() {
    if (!g_smLifetimeReady || !g_smLifetimeReliable.load()) return false;
    for (std::size_t i = 0; i < R::kTrapCount; ++i) {
        if (!R::kTraps[i].fnRva) continue;
        uintptr_t entry = 0, fn = 0;
        std::uint32_t flags = 0;
        bool bodyOk = false;
        MEMORY_BASIC_INFORMATION mbi {};
        const uintptr_t native = g_exeBase + R::kTraps[i].fnRva;
        if (!SmReadEntry(i, entry, fn, flags, bodyOk) || fn != native || flags != R::kTraps[i].flags || !bodyOk ||
            VirtualQuery(reinterpret_cast<void*>(entry), &mbi, sizeof(mbi)) != sizeof(mbi) || mbi.State != MEM_COMMIT ||
            mbi.Protect != PAGE_READWRITE) {
            Log("[script-mirror] swap-refused trap=%s entry=%llX fn=%llX flags=%X body=%u protect=%lX", R::kTraps[i].name,
                static_cast<unsigned long long>(entry ? entry - g_exeBase : 0),
                static_cast<unsigned long long>(fn ? fn - g_exeBase : 0), flags, bodyOk ? 1u : 0u,
                static_cast<unsigned long>(mbi.Protect));
            return false;
        }
        g_sm.slots[i].entry = entry;
        g_sm.slots[i].native = native;
    }
    for (std::size_t i = 0; i < R::kTrapCount; ++i) {
        if (!R::kTraps[i].fnRva) continue;
        auto& s = g_sm.slots[i];
        void* prior = InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(s.entry),
                                                        reinterpret_cast<void*>(kSmDetours[i]), reinterpret_cast<void*>(s.native));
        s.swapped = prior == reinterpret_cast<void*>(s.native);
        if (!s.swapped) { SmRestoreAll(); Log("[script-mirror] swap-raced trap=%s", R::kTraps[i].name); return false; }
    }
    Log("[script-mirror] swap ok traps=%zu", R::kTrapCount);
    return true;
}

// VUH-2605: the host's motion permit for a 317: still the published child at that address and netId.
static bool SmHostBirthQuery(uintptr_t actor, std::uint16_t netId, std::uint64_t& occurrence, std::uint16_t& parent) {
    occurrence = 0; parent = 0;
    if (!g_sm.hostActive || !SmGameThread()) return false;
    const auto* row = g_sm.births.Published(actor, netId);
    if (!row) return false;
    occurrence = row->occurrence; parent = row->parentNetId;
    return true;
}
// VUH-2605: publish each recorded child once its census netId exists, repeat on a heartbeat while its
// parent lives, end it once.
static void SmTickBirths(std::uint32_t generation) {
    namespace SB = ::kh2coop::inject::scriptbirth;
    g_sm.births.Tick(g_frameCounter, generation,
        [](uintptr_t child) -> std::uint16_t { return enemysync::HostEnemyNetId(child, SB::kChildObjectId); },
        [](std::uint16_t parent) { return enemysync::HostEnemyLive(parent, SB::kParentObjectId); },
        [](SB::Emit kind, const SB::HostBirth& r) {
            R::Call c {};
            c.trap = static_cast<std::uint8_t>(R::FindTrap(255, kind == SB::Emit::Begin ? 15 : 16));
            c.netId = r.parentNetId;
            c.objectId = SB::kParentObjectId;
            c.effect = r.childNetId;
            c.values[0] = R::Tagged(R::kTagInt, static_cast<std::uint32_t>(r.occurrence));
            c.values[1] = R::Tagged(R::kTagInt, static_cast<std::uint32_t>(r.occurrence >> 32));
            c.values[2] = R::Tagged(R::kTagInt, r.childNetId);
            c.values[3] = R::Tagged(R::kTagInt, SB::kChildObjectId);
            if (!R::ValidCall(c)) return true; // never retry a record the wire rules reject
            const bool sent = SmEmit(c);
            if (sent && (kind == SB::Emit::End || !r.sentFrame) && g_sm.birthLogs < 16) {
                ++g_sm.birthLogs;
                Log("[script-birth] host %s parent=%u child=%u occurrence=%llu frame=%u", kind == SB::Emit::Begin ? "begin" : "end",
                    r.parentNetId, r.childNetId, static_cast<unsigned long long>(r.occurrence), g_frameCounter);
            }
            return sent;
        });
}
static void SmFlush() {
    g_sm.retirements.Drain(g_sm.sender);
    R::Call batch[32];
    enemysync::ScriptCallRecord records[32];
    for (int packet = 0; packet < 2 && g_sm.sender.Pending(); ++packet) {
        const std::size_t n = g_sm.sender.Peek(batch, 32);
        for (std::size_t k = 0; k < n; ++k) {
            const auto& t = R::kTraps[batch[k].trap];
            records[k] = {};
            records[k].netId = batch[k].netId;
            records[k].objectId = batch[k].objectId;
            records[k].table = t.table;
            records[k].index = t.index;
            records[k].effect = batch[k].effect;
            for (std::size_t a = 0; a < 4; ++a) records[k].args[a] = batch[k].values[a];
        }
        std::uint64_t seq = 0;
        if (!enemysync::SendScriptCalls(records, n, g_frameCounter, seq)) { ++g_sm.second.sendFailed; return; }
        g_sm.sender.Commit(n);
        g_sm.retirements.Drain(g_sm.sender);
        ++g_sm.second.packets;
        g_sm.second.records += static_cast<std::uint32_t>(n);
    }
}

// Attack resources carry collision. Retire them before discarding a pairing;
// unlike visual effects, silent registry eviction is never acceptable.
static void SmRetireClientAttacks() noexcept {
    // Native free reenters SmForgetAllocation, which erases client bindings.
    // Never keep a map iterator or reference across that callback.
    std::array<std::pair<std::uint32_t,R::ClientEffect>,R::ClientEffects::kCap> retiring{};
    std::size_t count=0;
    g_sm.clientAttacks.Each([&](std::uint32_t id,const R::ClientEffect& pair) {
        retiring[count++]={id,pair};
    });
    for(std::size_t i=0;i<count;++i) {
        const auto& [id,pair]=retiring[i];
        const auto* current=g_sm.clientAttacks.Find(id);
        if(!current || current->handle!=pair.handle || current->effect!=pair.effect ||
           current->part!=pair.part || current->netId!=pair.netId ||
           current->ownerSlot!=pair.ownerSlot || current->ownerRole!=pair.ownerRole ||
           !SmAttackState(pair.handle,pair.effect,pair.part))continue;
        __try {
            std::uint64_t args[1]{R::Tagged(R::kTagAddress, pair.handle)};
            reinterpret_cast<TrapFn>(g_exeBase + 0x4323B0)(args);
        } __except(EXCEPTION_EXECUTE_HANDLER) { ++g_sm.faults; }
    }
    g_sm.clientAttacks.Reset();
}

static void SmResetRoom(bool retire = true) {
    SmOwnerReset(retire);
    g_sm.unmappedLogs = 0;
    g_sm.resourceMissLogs = g_sm.storedHandleLogs = g_sm.invalidLogs = 0;
    g_sm.births.Reset(); g_sm.birthLogs = 0; // clients reset their receipts with the room too
    g_sm.sender.Reset();
    g_sm.retirements.Reset();
    g_sm.occurrences.Reset();g_sm.reactions={};
    SmAxelClientReset();
    g_sm.hostEffects.Reset();
    g_sm.hostAttacks.Reset();
    g_sm.clientEffects.Reset();
    // After load/room teardown, old native objects are no longer ours to write.
    if (retire) SmRetireClientAttacks(); else g_sm.clientAttacks.Reset();
    g_sm.hold.Reset();
}

static void SmStopHost(const char* why) {
    if (!g_sm.hostActive && !g_sm.slots[0].swapped) return;
    g_sm.hostActive = false;
    for(const auto& binding:g_sm.reactions)if(binding.resource)
        (void)SmSetReactionCommand(binding,binding.command);
    g_sm.occurrences.Reset();g_sm.reactions={};
    SmRestoreAll();
    g_sm.sender.Reset();
    g_sm.retirements.Reset();
    g_sm.hostEffects.Reset();
    g_sm.hostAttacks.Reset();
    g_sm.births.Reset(); // a later host session announces its own children afresh
    Log("[script-mirror] host stop why=%s faults=%llu invalid=%llu", why, static_cast<unsigned long long>(g_sm.faults),
        static_cast<unsigned long long>(g_sm.invalid));
}

// ---- client --------------------------------------------------------------------------------------
// The native traps, unswapped, and the xyzrot helper, all this build's (checked once).
static bool SmClientReady() {
    if (!g_smLifetimeReady || !g_smLifetimeReliable.load()) return false;
    if (g_sm.clientChecked) return g_sm.clientReady;
    g_sm.clientChecked = true;
    for (std::size_t i = 0; i < R::kTrapCount; ++i) {
        if (!R::kTraps[i].fnRva) continue;
        uintptr_t entry = 0, fn = 0;
        std::uint32_t flags = 0;
        bool bodyOk = false;
        const uintptr_t native = g_exeBase + R::kTraps[i].fnRva;
        if (!SmReadEntry(i, entry, fn, flags, bodyOk) || fn != native || flags != R::kTraps[i].flags || !bodyOk) {
            Log("[script-mirror] client-refused trap=%s fn=%llX flags=%X body=%u", R::kTraps[i].name,
                static_cast<unsigned long long>(fn ? fn - g_exeBase : 0), flags, bodyOk ? 1u : 0u);
            return false;
        }
        g_sm.slots[i].entry = entry;
        g_sm.slots[i].native = native;
    }
    std::uint8_t helper[sizeof(R::kXyzRotHelper)] {};
    if (!ReadHitTraceMemory(g_exeBase + R::kXyzRotHelperRva, helper, sizeof(helper)) ||
        std::memcmp(helper, R::kXyzRotHelper, sizeof(helper)) != 0) {
        Log("[script-mirror] client-refused helper=3B65F0");
        return false;
    }
    g_sm.xyzRot = reinterpret_cast<XyzRotFn>(g_exeBase + R::kXyzRotHelperRva);
    std::uint8_t attackHelper[sizeof(R::kAttackPosHelper)]{};
    if(!ReadHitTraceMemory(g_exeBase+0x437A80,attackHelper,sizeof(attackHelper)) ||
       std::memcmp(attackHelper,R::kAttackPosHelper,sizeof(attackHelper))!=0)return false;
    g_sm.clientReady = true;
    Log("[script-mirror] client ready traps=%zu", R::kTrapCount);
    return true;
}

// The copy EnemyMirror drives for netId right now (brain skipped), with the host's object id.
static uintptr_t SmDrivenActor(std::uint16_t netId, std::uint32_t objectId) {
    if (!enemymirror::OwnerThread()) return 0;
    for (const auto& d : enemymirror::g_driven) {
        if (!d.actor || d.netId != netId || !d.running || g_frameCounter - d.frame > 2) continue;
        if(d.pose.field && !enemysync::FieldActorCurrent(d.actor))continue;
        uintptr_t objentry = 0;
        std::uint32_t id = 0;
        if (ReadHitTrace(d.actor + offsets::actor::OBJENTRY_PTR, objentry) && objentry &&
            ReadHitTrace(objentry + offsets::objentry::OBJECT_ID, id) && id == objectId)
            return d.actor;
        return 0;
    }
    return 0;
}

// The native trap with `handle` as its first argument; args[0] holds its result afterwards.
static bool SmCallTrap(std::size_t i, std::uint32_t handle, const std::uint64_t* values, std::uint64_t& result) noexcept {
    result = 0;
    __try {
        std::uint64_t args[5] = {R::Tagged(R::kTagAddress, handle), values[0], values[1], values[2], values[3]};
        reinterpret_cast<TrapFn>(g_sm.slots[i].native)(args);
        result = args[0];
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
static bool SmCallXyzRot(uintptr_t actor, const std::uint64_t* values) noexcept {
    __try {
        float v[4];
        for (unsigned k = 0; k < 4; ++k) { const std::uint32_t b = R::BitsOf(values[k]); std::memcpy(&v[k], &b, 4); }
        g_sm.xyzRot(actor, v);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
static bool SmWriteField(uintptr_t at, const std::uint64_t* values) noexcept {
    __try {
        auto* v = reinterpret_cast<std::uint32_t*>(at);
        for (unsigned k = 0; k < 4; ++k) v[k] = R::BitsOf(values[k]);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
// An actor's own handle, checked to name itself (the trap decodes it, then its +4).
static std::uint32_t SmOwnHandle(uintptr_t actor) noexcept {
    std::uint32_t handle = 0;
    if (!ReadHitTrace(actor + 4, handle) || !handle || SmDecode(handle) != actor) return 0;
    return handle;
}

// VUH-2640 Leg A veil (DownedSpike's SitOutVeil): the game's own obj_hide (1/133) or obj_show (1/134) trap, the
// client's pinned native as for a replayed call, on an actor by its own handle. B_EX100's class (vtable 0x5D2858
// +0x80/+0x88: 0x419820/0x419830) sets +0x124 |= 0x300, or clears 0x100 and restores the 0x200 shadow bit from +0xB90.
bool VeilReady() noexcept { return g_sm.requested && SmGameThread() && SmClientReady(); }
// An actor the census just proved: its own handle (its +4, naming itself).
std::uint32_t VeilHandle(uintptr_t actor) noexcept { return actor ? SmOwnHandle(actor) : 0; }
// A retained receipt (Quill r1 on 23c8bb8): the saved handle must resolve to the saved actor before that actor's +4 is
// read; then its +4 must still be that handle.
bool VeilHandleNames(uintptr_t actor, std::uint32_t handle) noexcept {
    return actor && handle && SmDecode(handle) == actor && SmOwnHandle(actor) == handle;
}
bool Veil(uintptr_t actor, std::uint32_t handle, bool hide) noexcept {
    const int i = R::FindTrap(1, hide ? 133 : 134);
    if (i < 0 || !VeilReady() || !VeilHandleNames(actor, handle)) return false;
    const std::uint64_t none[4] {};
    std::uint64_t result = 0;
    return SmCallTrap(static_cast<std::size_t>(i), handle, none, result);
}

// VUH-2605: a held script child (brain hook refused) is hidden with the same native obj_hide.
static bool SmHideScriptChild(uintptr_t actor) noexcept { return Veil(actor, VeilHandle(actor), true); }

static void SmRcWindow(D::Record& r,const R::ReactionWindow& w,
                       const std::array<R::ReactionSeat,3>& seats) noexcept {
    r.facts|=D::Window;r.openedFrame=w.openedFrame;r.duration=w.duration;
    r.consumedBefore=w.consumed?1:0;
    if(w.slot<seats.size()) {const auto& seat=seats[w.slot];r.facts|=D::Seat;
        r.living=seat.living?1:0;r.fresh=seat.fresh?1:0;r.capable=seat.capable?1:0;
        r.expected=w.connection;r.observed=seat.connection;}
}
static const char* SmRcConsumeReason(const R::ReactionWindow& w,const R::ReactionCompletion& c,
    std::uint32_t epoch,std::uint32_t frame,const std::array<R::ReactionSeat,3>& seats) noexcept {
    if(!w.occurrence || !w.source || !w.command || !w.connection || w.slot>=3)return "window-shape";
    if(w.epoch!=epoch)return "window-epoch";
    if(w.consumed)return "already-consumed";
    if(!w.duration || frame<w.openedFrame || frame-w.openedFrame>=w.duration)return "window-frame";
    if(c.epoch!=w.epoch || c.occurrence!=w.occurrence || c.source!=w.source || c.command!=w.command ||
       c.slot!=w.slot || c.connection!=w.connection)return "window-claim";
    const auto& seat=seats[w.slot];
    if(!seat.living)return "seat-dead";
    if(!seat.fresh)return "seat-stale";
    if(!seat.capable)return "seat-incapable";
    if(seat.connection!=w.connection)return "seat-connection";
    return "consume-returned-false";
}

#include "ScriptActorOwner.inl"
#include "AxelReactions.inl"

static bool SmAttackVector(uintptr_t attack, const std::uint64_t* values) noexcept {
    __try {
        float position[4];
        for(unsigned k=0;k<4;++k)position[k]=R::FloatOf(values[k]);
        using Setter=void(__fastcall*)(uintptr_t,const float*);
        reinterpret_cast<Setter>(g_exeBase+0x437A80)(attack,position);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
enum class Applied : std::uint8_t { Ok, Refused, Unpaired, Stale, Fault };
// One record on its driven copy.
static Applied SmReplay(const R::Call& c, uintptr_t actor) {
    const auto& t = R::kTraps[c.trap];
    std::uint64_t result = 0;
    if(t.kind==R::Kind::Owner)return SmOwnerApply(c,actor)?Applied::Ok:Applied::Refused;
    if(R::IsAttackKind(t.kind)) {
        if(t.kind==R::Kind::ReactionOffer)return SmApplyReactionOffer(c,actor)?Applied::Ok:Applied::Refused;
        if(t.kind==R::Kind::ReactionOwner)return SmApplyReactionOwner(c,actor)?Applied::Ok:Applied::Refused;
        if(t.kind==R::Kind::AttackStart || t.kind==R::Kind::OwnerAttackStart) {
            std::uint8_t ownerSlot=0xFF;
            uintptr_t attackOwner=actor;std::uint32_t ownerRole=0;
            const std::uint64_t* values=c.values;
            std::uint64_t shifted[4]{};
            std::size_t nativeTrap=c.trap;
            if(t.kind==R::Kind::OwnerAttackStart) {
                ownerRole=R::BitsOf(c.values[0]);
                attackOwner=SmOwnerAttackActor(ownerRole,c.netId,c.objectId,ownerSlot);
                if(!attackOwner)return Applied::Refused;
                nativeTrap=static_cast<std::size_t>(R::FindTrap(2,10));
                for(unsigned k=0;k<3;++k)shifted[k]=c.values[k+1];
                values=shifted;
            }
            const auto handle=SmOwnHandle(attackOwner);
            if(!handle || !SmCallTrap(nativeTrap,handle,values,result))return Applied::Fault;
            const auto made=R::TagOf(result)==R::kTagAddress?R::BitsOf(result):0;
            const auto attack=SmDecode(made);
            if(!SmAttackState(made,attack,attackOwner))return Applied::Refused;
            // Repeated start IDs must never replace a still-live collision object.
            if (g_sm.clientAttacks.Find(c.effect) ||
                !g_sm.clientAttacks.Put(c.effect,{made,attack,attackOwner,c.netId,ownerSlot,ownerRole},false)) {
                std::uint64_t unused=0;
                (void)SmCallTrap(static_cast<std::size_t>(R::FindTrap(2,13)),made,c.values,unused);
                return Applied::Refused;
            }
            return Applied::Ok;
        }
        const auto* pair=g_sm.clientAttacks.Find(c.effect);
        if(!pair)return Applied::Unpaired;
        const auto paired=*pair;
        const auto attackOwner=paired.ownerSlot==0xFF?actor:paired.part;
        if(paired.netId!=c.netId || !SmAttackState(paired.handle,paired.effect,attackOwner)) {
            g_sm.clientAttacks.Erase(c.effect);return Applied::Stale;
        }
        if(paired.ownerRole && !t.ends) {
            std::uint8_t slot=0xFF;
            if(SmOwnerAttackActor(paired.ownerRole,c.netId,c.objectId,slot)!=attackOwner)return Applied::Refused;
        }
        bool ok=false;
        if(t.replay==R::Replay::AttackStrike) {
            const auto target=SmOwnHandle(actor);
            if(!target || paired.ownerSlot==0xFF || attackOwner!=g_soraActor)return Applied::Refused;
            std::uint64_t args[4]{R::Tagged(R::kTagAddress,paired.handle),R::Tagged(R::kTagAddress,target),c.values[0],c.values[1]};
            reinterpret_cast<TrapFn>(g_sm.slots[c.trap].native)(args);ok=true;
        } else if(t.replay==R::Replay::AttackRc) {
            const auto receiver=SmOwnHandle(actor);
            if(!receiver)return Applied::Refused;
            std::uint64_t args[5]{R::Tagged(R::kTagAddress,paired.handle),R::Tagged(R::kTagAddress,receiver),
                R::Tagged(R::kTagInt,0),c.values[1],c.values[2]};
            reinterpret_cast<TrapFn>(g_sm.slots[c.trap].native)(args);
            ok=true;
        }
        else if(t.replay==R::Replay::AttackVector)ok=SmAttackVector(paired.effect,c.values);
        else if(t.replay==R::Replay::FieldWrite)ok=SmWriteField(paired.effect+t.field,c.values);
        else ok=SmCallTrap(c.trap,paired.handle,c.values,result);
        if(t.ends)g_sm.clientAttacks.Erase(c.effect);
        return ok?Applied::Ok:Applied::Fault;
    }
    if (!c.effect) {
        if (t.replay == R::Replay::XyzRotHelper) return SmCallXyzRot(actor, c.values) ? Applied::Ok : Applied::Fault;
        const std::uint32_t handle = SmOwnHandle(actor);
        if (!handle) return Applied::Refused;
        if (!SmCallTrap(c.trap, handle, c.values, result)) return Applied::Fault;
        if (t.kind == R::Kind::ActorEvent) g_sm.clientEffects.EraseOwner(c.netId);
        return Applied::Ok;
    }
    if (t.kind == R::Kind::EffectStart) {
        // An already-paired resource has its birth, including held replay.
        // Preserve its binding before invoking a native start that would orphan it.
        if (g_sm.clientEffects.Find(c.effect)) return Applied::Refused;
        const std::uint32_t handle = SmOwnHandle(actor);
        if (!handle) return Applied::Refused;
        if (!SmCallTrap(c.trap, handle, c.values, result)) return Applied::Fault;
        const std::uint32_t made = R::TagOf(result) == R::kTagAddress ? R::BitsOf(result) : 0;
        const uintptr_t effect = SmDecode(made);
        bool killed = true;
        uintptr_t part = 0;
        if (!made || !SmEffectState(effect, killed, part) || killed || !part) return Applied::Refused;  // this copy has no such effect
        g_sm.clientEffects.Put(c.effect, {made, effect, part, c.netId});
        return Applied::Ok;
    }
    const R::ClientEffect* pair = g_sm.clientEffects.Find(c.effect);
    if (!pair) return Applied::Unpaired;
    const R::ClientEffect e = *pair;
    if (!SmSameEffect(e.handle, e.effect, e.part)) { g_sm.clientEffects.Erase(c.effect); return Applied::Stale; }
    if (t.replay == R::Replay::FieldWrite) return SmWriteField(e.part + t.field, c.values) ? Applied::Ok : Applied::Fault;
    if (!SmCallTrap(c.trap, e.handle, c.values, result)) return Applied::Fault;
    if (t.ends) g_sm.clientEffects.Erase(c.effect);
    return Applied::Ok;
}
static bool SmCount(Applied a, bool held) {
    switch (a) {
        case Applied::Ok: if (held) ++g_sm.second.heldApplied; else ++g_sm.second.applied; return true;
        case Applied::Unpaired: ++g_sm.second.unpaired; return false;
        case Applied::Stale: ++g_sm.second.stale; return false;
        case Applied::Fault: ++g_sm.faults; ++g_sm.second.refused; return false;
        case Applied::Refused: ++g_sm.second.refused; return false;
    }
    return false;
}

static int SmApplyRecord(const enemysync::ScriptCallRecord& r) {
    ++g_sm.second.received;
    const int i = R::FindWire(r.table, r.index);
    if (i < 0 || !SmGameThread() || !SmClientReady()) { ++g_sm.second.refused; return 0; }
    ++g_sm.second.perTrap[i];
    R::Call c {};
    c.netId = r.netId;
    c.objectId = r.objectId;
    c.trap = static_cast<std::uint8_t>(i);
    c.effect = r.effect;
    for (std::size_t a = 0; a < 4; ++a) c.values[a] = r.args[a];
    if ((c.effect != 0) != R::EffectRecord(static_cast<std::size_t>(i)) || !R::ValidValues(static_cast<std::size_t>(i), c.values)) {
        ++g_sm.second.refused;
        return 0;
    }
    if (R::IsActorBirthKind(R::kTraps[i].kind)) {
        // VUH-2605: a birth has no native replay. Until the client owner consumes it, only count it.
        if (!R::ValidCall(c)) { ++g_sm.second.refused; return 0; }
        return enemysync::ReceiveScriptActorBirth(c.netId, c.objectId, r.index, c.effect, c.values) ? 1 : 0;
    }
    if (r.table==2 && r.index==13) {
        // Ending an existing resource needs its still-live allocation, not a
        // still-driven source. EnemyDeath may precede its final free on the wire.
        const auto* pair=g_sm.clientAttacks.Find(c.effect);
        if(!pair) {
            // A start waiting for birth/drive must not outlive its end. Cancel
            // the whole uncreated resource, including fields and RC offers.
            g_sm.hold.CancelResource(c.netId,c.effect,true);
            return 1;
        }
        if(pair->netId!=c.netId)return 0;
        return SmCount(SmReplay(c,pair->part),false)?1:0;
    }
    if(r.table==255 && r.index==6) {
        g_sm.hold.CancelOwner(c.netId,c.effect);
        return SmOwnerApply(c,0);
    }
    const uintptr_t actor = SmDrivenActor(c.netId, c.objectId);
    // Behind anything already waiting for this netId, or until its copy is driven. An effect record
    // waits only behind its own start; without one it has nothing to act on.
    if (!actor || g_sm.hold.HoldsNetId(c.netId)) {
        const bool attack = R::IsAttackKind(R::kTraps[i].kind);
        const bool start = R::kTraps[i].kind == R::Kind::EffectStart || R::kTraps[i].kind == R::Kind::AttackStart || R::kTraps[i].kind == R::Kind::OwnerAttackStart || R::kTraps[i].kind == R::Kind::Owner;
        const bool needsStart = c.effect && !start && !(attack ? g_sm.clientAttacks.Find(c.effect) : g_sm.clientEffects.Find(c.effect));
        if (needsStart && !g_sm.hold.HoldsStart(c.effect, attack)) { ++g_sm.second.unpaired; return 0; }
        if (g_sm.hold.Put(c, g_frameCounter)) ++g_sm.second.held; else ++g_sm.second.refused;
        return 0;
    }
    g_sm.hold.Erase(R::KeyOf(c));  // a held older value for this key must not land after this one
    return SmCount(SmReplay(c, actor), false) ? 1 : 0;
}

static void SmDrainHold() {
    if (!g_sm.hold.Size()) return;
    std::size_t expired = 0;
    g_sm.hold.Drain(g_frameCounter, [](const R::Call& c) {
        const uintptr_t actor = SmDrivenActor(c.netId, c.objectId);
        if (!actor) return false;
        SmCount(SmReplay(c, actor), true);
        return true;
    }, expired);
    g_sm.second.expired += static_cast<std::uint32_t>(expired);
}

// ---- owner-routed reactions ---------------------------------------------------------------------
static bool SmPoint(uintptr_t actor,R::ReactionPoint& point) noexcept {
    float v[3]{};
    const auto at=actor+offsets::actor::ENTITY_TRANSFORM+offsets::entity::POS_X;
    if(!actor || !ReadHitTraceMemory(at,v,sizeof(v)))return false;
    point={v[0],v[1],v[2]};return R::Finite(point);
}
static std::array<R::ReactionSeat,3> SmReactionSeats(const enemysync::DownedScope& scope) {
    std::array<R::ReactionSeat,3> seats{};
    for(unsigned candidate=0;candidate<3;++candidate) {
        const auto actor=enemytarget::g.candidateActor[candidate];
        const auto slot=candidate?enemytarget::g.ownerSlot[candidate]:std::uint8_t{0};
        uintptr_t status=0;
        if(!actor || slot>=3 || !scope.connections[slot] || (slot==0 && downedspike::LogicallyDowned()) ||
           !enemytarget::ReadLive(actor,status))continue;
        auto& seat=seats[slot];
        seat.connection=scope.connections[slot];
        seat.fresh=SmPoint(actor,seat.position);
        seat.living=seat.fresh;seat.capable=seat.fresh;
    }
    return seats;
}
// Native FIELD_COMMAND cache authority. All native stores remain in original
// functions; this adapter observes a fresh winner and may refuse preacceptance.
using NativeRcRefreshFn=void(__fastcall*)(uintptr_t);
using NativeRcWinnerFn=void(__fastcall*)(uintptr_t,uintptr_t,const std::uint32_t*,int,int);
using NativeRcAcceptFn=bool(__fastcall*)(uintptr_t,uintptr_t,const std::uint32_t*);
static NativeRcRefreshFn g_smRcRefresh=nullptr;
static NativeRcWinnerFn g_smRcWinner=nullptr;
static NativeRcAcceptFn g_smRcAccept=nullptr;
static bool g_smRcCreated[3]{};
static std::atomic<bool> g_smRcInstalled{false};
static bool SmNativeReactionReady() noexcept {
    return g_smRcInstalled.load(std::memory_order_acquire);
}
static R::NativeReactionCache g_smRcCache;
static R::NativeReactionAcceptance g_smRcAcceptance;
struct SmRcProof {
    R::NativeReactionIdentity identity{};
    enemysync::DownedScope scope{};
};
static SmRcProof g_smRcPending{},g_smRcPublished{},g_smRcAccepted{},g_smRcOwnedSource{};
static uintptr_t g_smRcRefreshing=0;
static bool g_smRcAccepting=false,g_smRcAcceptedHandoff=false;
static constexpr std::uint32_t kSmRcHookRvas[]{0x408520,0x408BD0,0x3B22D0};
static bool SmRcSameScope(const enemysync::DownedScope& a,const enemysync::DownedScope& b) noexcept {
    return a.context.generation==b.context.generation && a.context.deliverySerial==b.context.deliverySerial &&
        a.context.hostSourceSerial==b.context.hostSourceSerial && a.epoch==b.epoch && a.worldId==b.worldId &&
        a.roomId==b.roomId && a.door==b.door && a.mapProgram==b.mapProgram &&
        a.battleProgram==b.battleProgram && a.eventProgram==b.eventProgram &&
        a.connections==b.connections && a.localSlot==b.localSlot;
}
// Canonical static controller only. No arbitrary DD0/controller or TARGET decode.
static bool SmRcController(uintptr_t controller) noexcept {
    uintptr_t canonical=0,dd0=0,owner=0,bound=0,back=0;
    return SmGameThread() && SmOwnerReady() && g_sm.requested &&
        controller==g_exeBase+0x2A10620 && ReadHitTrace(g_exeBase+0x2A105D0,canonical) &&
        canonical && canonical==g_soraActor && SmOwnHandle(canonical) &&
        ReadHitTrace(canonical+0xDD0,dd0) && dd0==controller &&
        ReadHitTrace(controller+0xAE0,owner) && owner==canonical &&
        ReadHitTrace(controller+0xC38,bound) && bound==canonical &&
        ReadHitTrace(controller+0xB10,back) && back==controller;
}
static bool SmRcTarget(uintptr_t at,std::array<std::uint32_t,4>& target) noexcept {
    return at && ReadHitTraceMemory(at,target.data(),sizeof(target));
}
static bool SmRcRoots(uintptr_t actor,uintptr_t& status,uintptr_t& entry) noexcept {
    return actor && SmOwnHandle(actor) && enemytarget::ReadLive(actor,status) && status &&
        ReadHitTrace(actor+offsets::actor::OBJENTRY_PTR,entry) && entry;
}
static bool SmRcSourceCurrent(uintptr_t source,std::uint16_t netId,std::uint32_t objectId) noexcept {
    return g_sm.hostActive?enemysync::HostEnemyNetId(source,objectId)==netId:
        SmDrivenActor(netId,objectId)==source;
}
// A host has no driven-table entry after the RC resource retires. Retain only
// this source-ownership receipt across cache early returns; it grants refusal,
// never a new occurrence. All roots and the admitted scope must still match.
static bool SmRcObservedSource(const SmRcProof& observed,std::uint32_t handle) noexcept {
    const auto& id=observed.identity;
    uintptr_t status=0,entry=0;std::uint32_t objectId=0;
    const auto source=static_cast<uintptr_t>(id.roots[2]);
    return id.resource && id.target[0]==handle && source &&
        id.load==warp::LoadSerial() && id.transition==warp::TransitionSerial() &&
        enemysync::ScriptCameraScopeCurrent(observed.scope) && SmOwnHandle(source)==handle &&
        ReadHitTrace(source+0x5C0,status) && status==id.roots[5] &&
        ReadHitTrace(source+offsets::actor::OBJENTRY_PTR,entry) && entry && entry==id.roots[6] &&
        ReadHitTrace(entry+offsets::objentry::OBJECT_ID,objectId) && objectId==id.objectId;
}
// Recognition is registry/role first. A stale attack may have disappeared while
// its source is still admitted; the source handle alone never grants admission.
static bool SmRcKnownSource(std::uint32_t handle) noexcept {
    if(!handle)return false;
    if(SmRcObservedSource(g_smRcOwnedSource,handle) || SmRcObservedSource(g_smRcAccepted,handle))return true;
    if(SmAxelKnownSource(handle))return true;
    for(const auto& binding:g_sm.reactions)if(binding.resource && SmOwnHandle(binding.owner)==handle &&
        SmRcSourceCurrent(binding.owner,binding.netId,binding.objectId))return true;
    for(const auto& role:g_smRoles)if(role.resource && role.sourceHandle==handle &&
        SmOwnerActorCurrent(role) && SmRcSourceCurrent(role.source,role.netId,role.objectId))return true;
    for(const auto& cleanup:g_smOwnerCleanup) {
        const auto& role=cleanup.role;
        if(role.resource && role.sourceHandle==handle && SmOwnHandle(role.source)==handle &&
            SmRcSourceCurrent(role.source,role.netId,role.objectId))return true;
    }
    // VUH-2620: a mirrored copy only for a host-routed class (Thorn); a Dusk's own command stays native.
    for(const auto& driven:enemymirror::g_driven) {
        if(!driven.actor || SmOwnHandle(driven.actor)!=handle)continue;
        uintptr_t entry=0;std::uint32_t objectId=0;
        const bool known=ReadHitTrace(driven.actor+offsets::actor::OBJENTRY_PTR,entry) && entry &&
            ReadHitTrace(entry+offsets::objentry::OBJECT_ID,objectId);
        if(known && R::KnownReactionSource(false,SmOwnerMirroredSource(driven.actor),objectId))return true;
    }
    return false;
}
// Build a current, local-owner proof without mutating/renewing a window. The
// full immutable identity is captured at the winner, never relabelled on read.
static bool SmRcProofNow(uintptr_t controller,std::uint16_t command,
    const std::array<std::uint32_t,4>& target,SmRcProof& proof) noexcept {
    proof={};
    enemysync::DownedScope scope;
    if(!command || !SmRcController(controller) || !SmOwnerScope(scope) || scope.localSlot>=3 ||
        downedspike::LogicallyDowned() || !target[0] || target[1] || target[3]!=UINT32_MAX)return false;
    // The host-only targeting table is never populated on a client. Validate
    // this seat's checked canonical roots below; arbitration already happened
    // in the host-owned occurrence and remains unchanged.
    const auto localActor=R::ReactionActorForSeat(scope.localSlot,scope.localSlot,g_soraActor);
    const auto connection=scope.connections[scope.localSlot];
    if(!localActor || !connection)return false;
    auto& id=proof.identity;
    id.target=target;id.command=command;id.epoch=scope.epoch;id.slot=scope.localSlot;id.connection=connection;
    id.load=warp::LoadSerial();id.transition=warp::TransitionSerial();id.actorHandle=SmOwnHandle(g_soraActor);
    uintptr_t playerStatus=0,playerEntry=0,source=0,sourceStatus=0,sourceEntry=0,native=0;
    if(!SmRcRoots(localActor,playerStatus,playerEntry))return false;
    if(target[2] && SmAxelProof(command,target,scope,connection,source,native,id)) {
    } else if(target[2]) {
        const ReactionBinding* selected=nullptr;
        for(const auto& binding:g_sm.reactions) {
            if(!binding.resource || binding.handle!=target[2] || SmOwnHandle(binding.owner)!=target[0])continue;
            if(selected)return false;
            selected=&binding;
        }
        if(!selected || selected->command!=command || !selected->occurrence || selected->consumed ||
            selected->slot!=scope.localSlot || selected->epoch!=scope.epoch || selected->connection!=connection ||
            !SmAttackState(selected->handle,selected->attack,selected->owner) ||
            !SmRcSourceCurrent(selected->owner,selected->netId,selected->objectId))return false;
        if(g_sm.hostActive) {
            const auto* pair=g_sm.hostAttacks.Find(selected->handle);
            const auto* window=g_sm.occurrences.Find(selected->occurrence);
            if(!pair || pair->id!=selected->resource || pair->effect!=selected->attack ||
                pair->part!=selected->owner || !window || !R::ReactionLive(*window,scope.epoch,g_frameCounter) ||
                window->slot!=scope.localSlot || window->connection!=connection ||
                window->source!=selected->netId || window->command!=command)return false;
        } else {
            const auto* pair=g_sm.clientAttacks.Find(selected->resource);
            if(!pair || pair->handle!=selected->handle || pair->effect!=selected->attack ||
                pair->netId!=selected->netId || g_frameCounter>=selected->expires)return false;
        }
        source=selected->owner;native=selected->attack;
        id.family=1;id.resource=selected->resource;id.incarnation=selected->resource;
        id.occurrence=selected->occurrence;id.source=selected->netId;id.objectId=selected->objectId;
    } else {
        const ScriptActorRole* selected=nullptr;
        for(const auto& role:g_smRoles) {
            if(role.sourceHandle!=target[0] || !role.resource || role.reaction.command!=command ||
                role.slot!=scope.localSlot || role.actor!=g_soraActor)continue;
            if(selected)return false;
            selected=&role;
        }
        if(!selected || !SmOwnerCurrent(*selected,scope) || selected->actor!=localActor ||
            (g_sm.hostActive && (g_frameCounter-selected->commandFrame>15 || !selected->commandAnnounced)) ||
            (!g_sm.hostActive && GetTickCount64()>=selected->deadline) ||
            !R::OwnerCommandCaptureLive(selected->commandMode,SmOwnerCaptureCurrent(*selected)) ||
            !R::ReactionLive(selected->reaction,scope.epoch,g_frameCounter) ||
            selected->reaction.slot!=scope.localSlot || selected->reaction.connection!=connection)return false;
        source=selected->source;native=selected->thread;
        id.family=2;id.resource=selected->resource;
        id.incarnation=g_sm.hostActive?selected->commandIncarnation:selected->reaction.occurrence;
        id.objectId=selected->objectId;id.mode=selected->commandMode;
        id.occurrence=selected->reaction.occurrence;id.source=selected->netId;
    }
    if(!SmRcRoots(source,sourceStatus,sourceEntry))return false;
    id.roots={controller,g_soraActor,source,playerStatus,playerEntry,sourceStatus,sourceEntry,native};
    proof.scope=scope;
    return true;
}
static bool SmRcProofCurrent(const SmRcProof& saved,const char** failure=nullptr) noexcept {
    const char* why="unknown";
    if(!D::Check(saved.identity.occurrence!=0,"proof-occurrence-zero",why) ||
       !D::Check(enemysync::ScriptCameraScopeCurrent(saved.scope),"proof-admitted-scope",why)) {
        if(failure)*failure=why;return false;
    }
    SmRcProof now{};
    const bool ok=D::Check(SmRcProofNow(static_cast<uintptr_t>(saved.identity.roots[0]),saved.identity.command,
        saved.identity.target,now),"proof-now",why) &&
        D::Check(SmRcSameScope(saved.scope,now.scope),"proof-scope-replaced",why) &&
        D::Check(saved.identity==now.identity,"proof-identity-replaced",why);
    if(!ok && failure)*failure=why;return ok;
}
// Bounded per-command/decision changes plus one continuing five-second sample.
// This budget is not spent by the first Dusks' per-frame offers.
struct SmRcTrace { std::uint16_t command{};std::uint8_t decision{};std::uint32_t frame{};unsigned changes{}; };
static std::array<SmRcTrace,16> g_smRcTrace{};
static void SmRcLog(std::uint16_t command,std::uint8_t decision,const char* where) noexcept {
    SmRcTrace* entry=nullptr;
    for(auto& trace:g_smRcTrace)if(trace.command==command){entry=&trace;break;}
    if(!entry)for(auto& trace:g_smRcTrace)if(!trace.command){trace={command,0xFF,0,0};entry=&trace;break;}
    if(!entry)return; // no hash collision/eviction can renew a per-command budget
    const bool changed=entry->decision!=decision;
    if((changed && entry->changes<12) || g_frameCounter-entry->frame>=300) {
        if(changed)++entry->changes;
        entry->decision=decision;entry->frame=g_frameCounter;
        const auto& receipt=g_smRcCache.Published();
        std::uint8_t flags=0,status=0;std::uint32_t control=0;std::uint64_t system=0;
        const auto controller=g_exeBase+0x2A10620;
        const bool gates=ReadHitTrace(controller+0xB3C,flags) && ReadHitTrace(controller+0xB45,status) &&
            ReadHitTrace(controller+0x24,control) && ReadHitTrace(g_exeBase+0x9AB580,system);
        std::uint16_t cached=0,active=0;std::array<std::uint32_t,4> cachedTarget{},activeTarget{};
        const bool cacheRead=ReadHitTrace(controller+0xB42,cached) && SmRcTarget(controller+0xB28,cachedTarget);
        const bool activeRead=ReadHitTrace(controller+8,active) && SmRcTarget(controller+0x10,activeTarget);
        Log("[script-rc-cache] where=%s command=%u decision=%u frame=%u serial=%llu selectedFrame=%u resource=%u occurrence=%llu target=%X/%X/%X/%X gates=%u flags=%X status=%u control=%X system=%llX accepted=%llu handoff=%u cacheRead=%u cached=%u cacheTarget=%X/%X/%X/%X activeRead=%u active=%u activeTarget=%X/%X/%X/%X",
            where,command,decision,g_frameCounter,receipt.refresh,receipt.frame,receipt.identity.resource,
            receipt.identity.occurrence,receipt.identity.target[0],receipt.identity.target[1],
            receipt.identity.target[2],receipt.identity.target[3],gates?1u:0u,flags,status,control,system,
            g_smRcAccepted.identity.occurrence,g_smRcAcceptedHandoff?1u:0u,
            cacheRead?1u:0u,cached,cachedTarget[0],cachedTarget[1],cachedTarget[2],cachedTarget[3],
            activeRead?1u:0u,active,activeTarget[0],activeTarget[1],activeTarget[2],activeTarget[3]);
    }
}
// 0 unrelated/unknown, 1 admitted, 2 no current binding, 3 no fresh selection.
// Log-only: 4 native gate, 5 callback admitted, 6 callback refused,
// 7 unqualified static owner, 8 pending native callback fence, 9 scope-retired fence,
// 10 native accepted result did not prove its active inline request.
static unsigned SmRcDecision(uintptr_t controller,std::uint16_t command,
    const std::array<std::uint32_t,4>& target) noexcept {
    if(!SmRcKnownSource(target[0]))return 0;
    SmRcProof now{};
    if(!SmRcProofNow(controller,command,target,now))return 2;
    if(!SmRcSameScope(g_smRcPublished.scope,now.scope) || !g_smRcCache.Current(now.identity,g_frameCounter))return 3;
    if(g_smRcAcceptance.Pending() && !g_smRcAcceptance.Current(now.identity) &&
        SmRcSameScope(g_smRcAccepted.scope,now.scope) &&
        g_smRcAccepted.identity.load==now.identity.load &&
        g_smRcAccepted.identity.transition==now.identity.transition)return 8;
    return 1;
}
static std::uint16_t SmNativeReactionCurrent(std::uint16_t raw) noexcept {
    const auto controller=g_exeBase+0x2A10620;
    if(!raw || !SmNativeReactionReady() || !SmGameThread() || !SmOwnerReady() || !g_sm.requested)return raw;
    std::uint16_t command=0;
    std::array<std::uint32_t,4> target{};
    if(!ReadHitTrace(controller+0xB42,command) || command!=raw || !SmRcTarget(controller+0xB28,target))return raw;
    if(!SmRcController(controller)) {
        if(SmRcKnownSource(target[0]))SmRcLog(command,7,"query-owner-unavailable");
        return raw; // unqualified/unstable canonical ownership remains conservative
    }
    auto decision=SmRcDecision(controller,command,target);
    if(decision==1) {
        std::uint8_t cacheFlags=0,status=0;std::uint32_t control=0;std::uint64_t system=0;
        if(!ReadHitTrace(controller+0xB3C,cacheFlags) || !(cacheFlags&1) ||
            !ReadHitTrace(controller+0xB45,status) || status>=4 || !ReadHitTrace(controller+0x24,control) || (control&1) ||
            !ReadHitTrace(g_exeBase+0x9AB580,system) || ((system&(1ull<<23)) && !(system&(1ull<<28))))decision=4;
    }
    if(decision)SmRcLog(command,static_cast<std::uint8_t>(decision),"query");
    return R::NativeReactionCommand(raw,decision!=0,decision==1);
}
static void __fastcall SmNativeRcWinner(uintptr_t result,uintptr_t descriptor,
    const std::uint32_t* target,int priority,int distance) {
    const auto returnAddress=reinterpret_cast<uintptr_t>(_ReturnAddress());
    if(!SmNativeReactionReady() || !SmGameThread() || !SmOwnerReady() || !g_smRcRefreshing) {
        g_smRcWinner(result,descriptor,target,priority,distance);return;
    }
    g_smRcPending={};g_smRcCache.Winner(nullptr,g_frameCounter);
    const auto caller=returnAddress>=g_exeBase?returnAddress-g_exeBase:0;
    uintptr_t component=0;std::uint16_t command=0;
    std::array<std::uint32_t,4> input{};SmRcProof before{};
    const bool candidate=(caller==0x408145 || caller==0x4084F2) &&
        ReadHitTrace(result,component) && component==g_smRcRefreshing &&
        ReadHitTrace(descriptor,command) && SmRcTarget(reinterpret_cast<uintptr_t>(target),input) &&
        SmRcProofNow(component-0xB10,command,input,before);
    g_smRcWinner(result,descriptor,target,priority,distance); // always preserve original side effects
    std::uint16_t written=0;std::array<std::uint32_t,4> output{};
    if(candidate && g_smRcRefreshing==component && ReadHitTrace(result+0xA,written) && written==command &&
        SmRcTarget(result+0x14,output) && output==input && SmRcProofCurrent(before)) {
        g_smRcPending=before;g_smRcCache.Winner(&before.identity,g_frameCounter);
    }
}
static void SmRcEndRefresh(uintptr_t component) noexcept {
    std::uint16_t command=0;std::array<std::uint32_t,4> target{};
    const bool current=SmRcController(component-0xB10) && ReadHitTrace(component+0x32,command) &&
        SmRcTarget(component+0x18,target) && SmRcProofCurrent(g_smRcPending);
    g_smRcCache.End(current?&g_smRcPending.identity:nullptr,command,target,g_frameCounter);
    g_smRcPublished=g_smRcCache.Published().valid?g_smRcPending:SmRcProof{};
    if(g_smRcCache.Published().valid)g_smRcOwnedSource=g_smRcPublished;
    if(SmRcKnownSource(target[0]))SmRcLog(command,
        static_cast<std::uint8_t>(SmRcDecision(component-0xB10,command,target)),"refresh");
    g_smRcPending={};
}
static void __fastcall SmNativeRcRefresh(uintptr_t component) {
    if(!SmNativeReactionReady() || !SmRcController(component-0xB10)) {g_smRcRefresh(component);return;}
    if(g_smRcRefreshing) {
        g_smRcCache.Poison();g_smRcPending={};g_smRcPublished={};
        g_smRcRefresh(component);return;
    }
    g_smRcPending={};g_smRcPublished={};
    if(!g_smRcCache.Begin()){g_smRcRefresh(component);return;}
    g_smRcRefreshing=component;
    __try {g_smRcRefresh(component);SmRcEndRefresh(component);}
    __finally {
        g_smRcRefreshing=0;
        if(AbnormalTermination()){g_smRcCache.Reset();g_smRcPending={};g_smRcPublished={};}
    }
}
static bool __fastcall SmNativeRcAccept(uintptr_t controller,uintptr_t descriptor,const std::uint32_t* target) {
    const auto returnAddress=reinterpret_cast<uintptr_t>(_ReturnAddress());
    if(returnAddress!=g_exeBase+0x3B24FC || controller!=g_exeBase+0x2A10620 ||
        !SmNativeReactionReady() || !SmGameThread() || !SmOwnerReady() || !g_sm.requested)
        return g_smRcAccept(controller,descriptor,target);
    std::uint16_t command=0;std::array<std::uint32_t,4> logical{};
    if(!ReadHitTrace(descriptor,command) || !SmRcTarget(reinterpret_cast<uintptr_t>(target),logical))
        return g_smRcAccept(controller,descriptor,target);
    if(!SmRcKnownSource(logical[0]))return g_smRcAccept(controller,descriptor,target);
    if(!SmRcController(controller)){SmRcLog(command,7,"accept-owner-refused");return false;}
    const auto decision=SmRcDecision(controller,command,logical);
    SmRcLog(command,static_cast<std::uint8_t>(decision),"accept");
    if(decision!=1 || g_smRcAccepting || !SmRcProofCurrent(g_smRcPublished))return false;
    // Latch immutable identity before native entry. Acceptance does not consume.
    // A native transition may invoke code reentrantly; it cannot replace this stamp.
    if(g_smRcAcceptance.Pending()) {
        // An interrupted old native action may still reach 3B5FA0 using the
        // controller's CURRENT inline TARGET. Never relabel it to a new binding.
        // Only a different admitted scope/load or its actual callback retires it.
        if(!SmRcSameScope(g_smRcAccepted.scope,g_smRcPublished.scope) ||
            g_smRcAccepted.identity.load!=g_smRcPublished.identity.load ||
            g_smRcAccepted.identity.transition!=g_smRcPublished.identity.transition) {
            SmRcLog(command,9,"pending-scope-retired");g_smRcAccepted={};g_smRcAcceptance.Reset();g_smRcAcceptedHandoff=false;
        } else {SmRcLog(command,8,"pending-callback-fence");return false;}
    }
    if(!g_smRcAcceptance.Reserve(g_smRcCache.Published(),g_smRcPublished.identity))return false;
    g_smRcAccepted=g_smRcPublished;g_smRcAccepting=true;g_smRcAcceptedHandoff=false;
    bool accepted=false;
    __try {
        accepted=g_smRcAccept(controller,descriptor,target);
        if(accepted) {
            std::uint16_t active=0;std::array<std::uint32_t,4> activeTarget{};
            g_smRcAcceptedHandoff=SmRcController(controller) && ReadHitTrace(controller+8,active) &&
                SmRcTarget(controller+0x10,activeTarget) &&
                R::NativeReactionActive(g_smRcAccepted.identity,controller+0x10,active,activeTarget);
            if(!g_smRcAcceptedHandoff)SmRcLog(command,10,"native-active-handoff-refused");
        }
    }
    __finally {
        g_smRcAccepting=false;g_smRcAcceptance.Finish(accepted);
        if(!accepted){g_smRcAccepted={};g_smRcAcceptedHandoff=false;}
    }
    return accepted;
}
static D::Record SmRcAcceptedTrace(const SmRcProof& accepted) noexcept {
    const auto& id=accepted.identity;
    auto r=D::Claim(id.epoch,id.resource,id.objectId,id.source,id.command,id.slot,id.connection,id.occurrence);
    r.tuple=id.occurrence!=0;r.facts|=D::Native|D::Accepted;r.frame=g_frameCounter;r.load=id.load;r.transition=id.transition;
    r.slot=id.slot;r.mode=id.mode;r.actor=id.roots[1];r.sourceActor=id.roots[2];
    r.generation=accepted.scope.context.generation;r.sourceDelivery=accepted.scope.context.deliverySerial;
    return r;
}
static bool SmNativeReactionCallbackAllowed(uintptr_t receiver,std::uint32_t command,const std::uint32_t* target,D::Record* output) {
    // Snapshot the actual accepted identity before the latch can be taken or a
    // proof query can reenter. Current publication is not callback identity.
    const auto diagnosticAccepted=g_smRcAccepted;
    auto trace=SmRcAcceptedTrace(diagnosticAccepted);if(output)*output=trace;
    const char* why="unknown";
    const auto result=[&](bool ok,const char* reason){SmRcHop(D::Stage::NativeCallback,ok?"admitted":"refused",reason,trace);return ok;};
    std::array<std::uint32_t,4> logical{};
    if(!SmRcTarget(reinterpret_cast<uintptr_t>(target),logical)) {
        const bool ordinary=!SmOwnerMirroredSource(receiver);if(ordinary){if(output)*output={};return true;}
        return result(false,"logical-target-unreadable");
    }
    if(!SmRcKnownSource(logical[0])){if(output)*output={};return true;}
    if(!SmNativeReactionReady())return result(false,"native-cache-not-ready");
    // Authority keeps the original post-guard snapshot point. The earlier
    // copy is diagnostic only; guards cannot make it the acceptance authority.
    const auto accepted=g_smRcAccepted;trace=SmRcAcceptedTrace(accepted);if(output)*output=trace;
    const auto controller=g_exeBase+0x2A10620;
    std::uint16_t active=0;std::array<std::uint32_t,4> activeTarget{};
    const bool observed=D::Check(g_smRcAcceptance.Current(accepted.identity),"accepted-latch",why) &&
        D::Check(SmRcController(controller),"canonical-controller",why) &&
        D::Check(ReadHitTrace(controller+8,active),"active-command-unreadable",why) &&
        D::Check(SmRcTarget(controller+0x10,activeTarget),"active-target-unreadable",why) &&
        D::Check(activeTarget==logical,"active-logical-target",why) &&
        D::Check(R::NativeReactionActive(accepted.identity,reinterpret_cast<uintptr_t>(target),active,activeTarget),"active-inline-target",why) &&
        D::Check(command==accepted.identity.command,"accepted-command",why) &&
        D::Check(logical==accepted.identity.target,"accepted-target",why) &&
        D::Check(receiver==accepted.identity.roots[2],"accepted-receiver",why);
    const bool admitted=observed && D::Check(g_smRcAcceptedHandoff,"native-handoff",why) &&
        SmRcProofCurrent(accepted,&why);
    SmRcLog(static_cast<std::uint16_t>(command),admitted?5:6,"callback");
    if(!observed)return result(false,why);
    // Preserve the original take even when admitted==false.
    if(!g_smRcAcceptance.Take(accepted.identity))return result(false,"accepted-take");
    g_smRcAccepted={};g_smRcAcceptedHandoff=false;
    return result(admitted,admitted?"accepted-taken":why);
}
static bool SmNativeReactionPins() {
    return SmOwnerPin(0x3A8B10,R::kNativeRcActionGeneral,sizeof(R::kNativeRcActionGeneral)) &&
        SmOwnerPin(0x3AA370,R::kNativeRcActionDirect,sizeof(R::kNativeRcActionDirect)) &&
        SmOwnerPin(0x3B22D0,R::kNativeRcAcceptreactionFull,sizeof(R::kNativeRcAcceptreactionFull)) &&
        SmOwnerPin(0x3B24BB,R::kNativeRcAcceptreactionCaller,sizeof(R::kNativeRcAcceptreactionCaller)) &&
        SmOwnerPin(0x408BD0,R::kNativeRcSelectcandidateFull,sizeof(R::kNativeRcSelectcandidateFull)) &&
        SmOwnerPin(0x408130,R::kNativeRcSelectcandidatePrimaryCaller,sizeof(R::kNativeRcSelectcandidatePrimaryCaller)) &&
        SmOwnerPin(0x4084E2,R::kNativeRcSelectcandidateFallbackCaller,sizeof(R::kNativeRcSelectcandidateFallbackCaller)) &&
        SmOwnerPin(0x408520,R::kNativeRcRefreshcacheFull,sizeof(R::kNativeRcRefreshcacheFull)) &&
        SmOwnerPin(0x40858D,R::kNativeRcRefreshcacheQueryCaller,sizeof(R::kNativeRcRefreshcacheQueryCaller)) &&
        SmOwnerPin(0x408651,R::kNativeRcRefreshcachePublishWindow,sizeof(R::kNativeRcRefreshcachePublishWindow)) &&
        SmOwnerPin(0x40873D,R::kNativeRcRefreshcacheClearWindow,sizeof(R::kNativeRcRefreshcacheClearWindow)) &&
        SmOwnerPin(0x3BEE00,R::kNativeRcAttacktargetFull,sizeof(R::kNativeRcAttacktargetFull));
}
static void SmNativeReactionShutdown() {
    g_smRcInstalled.store(false,std::memory_order_release);
    enemysync::SetScriptReactionCurrent(nullptr);
    // Detours check installed before touching owner-thread metadata. Trampolines
    // remain allocated until global MinHook teardown, including partial installs.
    for(unsigned i=0;i<3;++i)if(g_smRcCreated[i]) {
        const auto status=MH_DisableHook(reinterpret_cast<void*>(g_exeBase+kSmRcHookRvas[i]));
        if(status!=MH_OK && status!=MH_ERROR_DISABLED)Log("[script-rc-cache] disable rva=%X status=%d",kSmRcHookRvas[i],status);
    }
    if(SmGameThread()) {
        g_smRcCache.Reset();g_smRcAcceptance.Reset();g_smRcPending={};g_smRcPublished={};g_smRcAccepted={};g_smRcOwnedSource={};g_smRcAcceptedHandoff=false;g_smRcRefreshing=0;
    }
}
static bool SmNativeReactionInstall() {
    g_smRcInstalled.store(false,std::memory_order_release);
    void* detours[]{reinterpret_cast<void*>(&SmNativeRcRefresh),reinterpret_cast<void*>(&SmNativeRcWinner),
        reinterpret_cast<void*>(&SmNativeRcAccept)};
    void** originals[]{reinterpret_cast<void**>(&g_smRcRefresh),reinterpret_cast<void**>(&g_smRcWinner),
        reinterpret_cast<void**>(&g_smRcAccept)};
    for(unsigned i=0;i<3;++i) {
        const auto status=MH_CreateHook(reinterpret_cast<void*>(g_exeBase+kSmRcHookRvas[i]),detours[i],originals[i]);
        if(status!=MH_OK){SmNativeReactionShutdown();return false;}
        g_smRcCreated[i]=true;
    }
    for(unsigned i=0;i<3;++i)if(MH_EnableHook(reinterpret_cast<void*>(g_exeBase+kSmRcHookRvas[i]))!=MH_OK) {
        SmNativeReactionShutdown();return false;
    }
    g_smRcInstalled.store(true,std::memory_order_release);
    Log("[script-rc-cache] installed refresh=408520 winner=408BD0 accept=3B22D0");
    return true;
}


static bool SmSetReactionCommand(const ReactionBinding& binding,std::uint32_t command) noexcept {
    if(!SmAttackState(binding.handle,binding.attack,binding.owner))return false;
    __try { *reinterpret_cast<std::uint32_t*>(binding.attack+0x11C)=command;return true; }
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
static void SmTickReactions() {
    enemysync::DownedScope scope;
    if(!enemysync::CaptureDownedScope(scope))return;
    SmAxelFrame(scope);
    const auto seats=SmReactionSeats(scope);
    const auto role=enemysync::ActivationRole();
    for(auto& binding:g_sm.reactions) {
        if(!binding.resource)continue;
        if(!SmAttackState(binding.handle,binding.attack,binding.owner)) {
            g_sm.occurrences.Retire(binding.handle,binding.resource);binding={};continue;
        }
        bool enabled=false;
        if(role==1 && g_sm.hostActive) {
            R::ReactionPoint trigger{};
            float p[3]{};std::uint32_t flags=0;
            if(ReadHitTrace(binding.attack+8,flags) && (flags&1u) &&
               ReadHitTraceMemory(binding.attack+0xE4,p,sizeof(p)))trigger={p[0],p[1],p[2]};
            else if(!SmPoint(binding.owner,trigger))continue;
            auto* window=g_sm.occurrences.Open(binding.handle,binding.resource,binding.netId,
                static_cast<std::uint16_t>(binding.command),scope.epoch,g_frameCounter,180,trigger,binding.range,seats);
            if(window && !binding.occurrence) {
                R::Call offer{};offer.netId=binding.netId;offer.objectId=binding.objectId;offer.effect=binding.resource;
                offer.trap=static_cast<std::uint8_t>(R::FindTrap(255,1));
                offer.values[0]=R::Tagged(R::kTagInt,static_cast<std::uint32_t>(window->occurrence));
                offer.values[1]=R::Tagged(R::kTagInt,static_cast<std::uint32_t>(window->occurrence>>32));
                offer.values[2]=R::Tagged(R::kTagInt,window->command);
                offer.values[3]=R::Tagged(R::kTagInt,window->slot);
                R::Call identity=offer;identity.trap=static_cast<std::uint8_t>(R::FindTrap(255,12));
                identity.values[0]=R::Tagged(R::kTagInt,window->slot);
                identity.values[1]=R::Tagged(R::kTagInt,static_cast<std::uint32_t>(window->connection));
                identity.values[2]=R::Tagged(R::kTagInt,static_cast<std::uint32_t>(window->connection>>32));
                identity.values[3]=0;
                if(SmEmit(identity) && SmEmit(offer)) {
                    binding.occurrence=window->occurrence;binding.slot=window->slot;binding.epoch=scope.epoch;
                    binding.connection=window->connection;
                    Log("[script-rc] offer source=%u resource=%u occurrence=%llu command=%u owner=%u epoch=%u",
                        binding.netId,binding.resource,window->occurrence,binding.command,binding.slot,scope.epoch);
                }
            }
            if(window && R::RefreshReaction(*window,scope.epoch,g_frameCounter,seats)) {
                const auto& seat=seats[window->slot];
                enabled=window->slot==0 && seat.living && seat.fresh && seat.capable && seat.connection==window->connection;
            }
        } else if(role==2) {
            if(binding.slot<3 && (binding.epoch!=scope.epoch ||
               binding.connection!=scope.connections[binding.slot] ||
               (binding.slot==scope.localSlot && downedspike::LogicallyDowned())))binding.consumed=true;
            enabled=binding.slot==scope.localSlot && binding.epoch==scope.epoch && !binding.consumed &&
                binding.connection==scope.connections[scope.localSlot] &&
                g_frameCounter<binding.expires && !downedspike::LogicallyDowned();
        }
        (void)SmSetReactionCommand(binding,enabled?binding.command:0);
    }
}
static int SmApplyReactionOwner(const R::Call& c,uintptr_t actor) {
    if(std::uint16_t command=0;R::AxelResourceFor(c.effect,c.objectId,command))return SmAxelApplyIdentity(c,command,actor)?1:0;
    if(c.effect&0x80000000u)return SmOwnerReactionIdentity(c)?1:0;
    enemysync::DownedScope scope;
    const auto* pair=g_sm.clientAttacks.Find(c.effect);
    if(!pair || pair->netId!=c.netId || !SmAttackState(pair->handle,pair->effect,actor) ||
       !enemysync::CaptureDownedScope(scope))return 0;
    for(unsigned i=0;i<3;++i)if(R::TagOf(c.values[i])!=R::kTagInt)return 0;
    const auto slot=R::BitsOf(c.values[0]);
    const auto connection=static_cast<std::uint64_t>(R::BitsOf(c.values[1])) |
        (static_cast<std::uint64_t>(R::BitsOf(c.values[2]))<<32);
    if(slot>=3 || !connection || scope.connections[slot]!=connection)return 0;
    ReactionBinding* destination=nullptr;
    for(auto& binding:g_sm.reactions)if(binding.resource==c.effect) {
        // An occurrence is immutable. Repeated delivery never renews its life.
        return binding.connection==connection && binding.slot==slot?1:0;
    }
    for(auto& binding:g_sm.reactions)if(!binding.resource){destination=&binding;break;}
    if(!destination)return 0;
    auto& binding=*destination;
    binding={};binding.handle=pair->handle;binding.attack=pair->effect;binding.owner=actor;
    binding.resource=c.effect;binding.netId=c.netId;binding.objectId=c.objectId;binding.epoch=scope.epoch;
    binding.slot=static_cast<std::uint8_t>(slot);binding.connection=connection;
    return 1;
}
static int SmApplyReactionOffer(const R::Call& c,uintptr_t actor) {
    if(std::uint16_t command=0;R::AxelResourceFor(c.effect,c.objectId,command))return SmAxelApplyOffer(c,command,actor)?1:0;
    if(c.effect&0x80000000u)return SmOwnerReactionOffer(c)?1:0;
    enemysync::DownedScope scope;
    if(!enemysync::CaptureDownedScope(scope))return 0;
    const auto occurrence=static_cast<std::uint64_t>(R::BitsOf(c.values[0])) |
        (static_cast<std::uint64_t>(R::BitsOf(c.values[1]))<<32);
    const auto command=R::BitsOf(c.values[2]),slot=R::BitsOf(c.values[3]);
    for(auto& binding:g_sm.reactions)if(binding.resource==c.effect) {
        if(binding.slot!=slot || slot>=3 || binding.connection!=scope.connections[slot] ||
           binding.owner!=actor || binding.netId!=c.netId || binding.objectId!=c.objectId ||
           binding.epoch!=scope.epoch || !SmAttackState(binding.handle,binding.attack,actor))return 0;
        if(binding.occurrence)return binding.occurrence==occurrence && binding.command==command?1:0;
        binding.command=command;binding.occurrence=occurrence;binding.expires=g_frameCounter+180;
        return SmSetReactionCommand(binding,slot==scope.localSlot && !downedspike::LogicallyDowned()?command:0)?1:0;
    }
    return 0;
}
static bool SmCompleteReaction(const enemysync::ScriptReactionCompletionRecord& completion) {
    if(std::uint16_t command=0;R::AxelResourceFor(completion.resource,completion.objectId,command))
        return SmAxelComplete(completion,nullptr);
    if(completion.resource&0x80000000u)return SmOwnerCompleteReaction(completion);
    auto trace=SmRcClaim(completion);trace.facts|=D::Native;trace.frame=g_frameCounter;
    trace.load=g_sm.load;trace.transition=g_sm.transition;
    const char* why="unknown";
    const auto refused=[&](){SmRcHop(D::Stage::HostAttack,"refused",why,trace);return false;};
    if(!D::Check(SmGameThread(),"game-thread",why) || !D::Check(g_sm.hostActive,"host-active",why) ||
       !D::Check(g_smReactionCallback!=nullptr,"callback-ready",why))return refused();
    enemysync::DownedScope scope;
    if(!D::Check(enemysync::CaptureDownedScope(scope),"downed-scope",why) ||
       !D::Check(scope.localSlot==0,"local-host-seat",why))return refused();
    auto* window=g_sm.occurrences.Find(completion.occurrence);
    if(!window){why="occurrence-missing";return refused();}
    ReactionBinding* binding=nullptr;
    for(auto& r:g_sm.reactions)if(r.resource==completion.resource && r.netId==completion.netId &&
       r.objectId==completion.objectId && r.occurrence==completion.occurrence){binding=&r;break;}
    if(!D::Check(binding!=nullptr,"binding-missing",why) ||
       !SmAttackState(binding->handle,binding->attack,binding->owner,&why))return refused();
    trace.actor=binding->attack;trace.sourceActor=binding->owner;
    const auto seats=SmReactionSeats(scope);
    const R::ReactionCompletion claim{completion.occurrence,completion.requesterConnectionId,
        completion.epoch,completion.netId,completion.command,completion.ownerSlot};
    const auto before=*window;SmRcWindow(trace,before,seats);
    const bool consumed=R::ConsumeReaction(*window,claim,scope.epoch,g_frameCounter,seats);
    trace.consumedAfter=window->consumed?1:0;
    if(!consumed){why=SmRcConsumeReason(before,claim,scope.epoch,trace.frame,seats);return refused();}
    const auto player=SmOwnerSeatActor(completion.ownerSlot);
    const auto owner=SmOwnHandle(binding->owner);
    if(!D::Check(owner!=0,"source-handle",why) || !D::Check(player!=0,"seat-actor",why))return refused(); // remains consumed
    const std::uint32_t target[4]{owner,0,binding->handle,UINT32_MAX};
    (void)SmSetReactionCommand(*binding,0);
    const auto completed=completion; // Native callback may retire/reuse the paired binding.
    const auto entered=trace; // No binding/window dereference after native reentry.
    SmRcHop(D::Stage::HostAttack,"callback-enter","consumed",entered);
    g_smReactionCallback(binding->owner,player,completion.command,target);
    SmRcHop(D::Stage::HostAttack,"callback-returned","native-call-returned",entered);
    Log("[script-rc] complete source=%u resource=%u occurrence=%llu command=%u owner=%u",
        completed.netId,completed.resource,completed.occurrence,completed.command,completed.ownerSlot);
    return true;
}
static void __fastcall SmReactionCallback(uintptr_t receiver,uintptr_t player,std::uint32_t command,const std::uint32_t* target) {
    if(!g_sm.requested || !SmGameThread()) {g_smReactionCallback(receiver,player,command,target);return;}
    // VUH-2620: only a host-routed class (Thorn) keeps the client's unpaired callback refused.
    uintptr_t receiverEntry=0;std::uint32_t receiverObject=0;
    const bool receiverRead=ReadHitTrace(receiver+offsets::actor::OBJENTRY_PTR,receiverEntry) && receiverEntry &&
        ReadHitTrace(receiverEntry+offsets::objentry::OBJECT_ID,receiverObject);
    const bool ownedReceiver=R::ClientOwnedCallbackReceiver(enemysync::ActivationRole()==2,
        SmOwnerMirroredSource(receiver),receiverRead,receiverObject);
    // Pin/install/lifetime failure must not reopen a proven client's key11 BDX
    // merely because the new native cache hooks could not become ready.
    if(!SmOwnerReady()) {
        if(ownedReceiver)SmRcHop(D::Stage::LocalHandler,"refused","owner-not-ready",SmRcAcceptedTrace(g_smRcAccepted));
        if(!ownedReceiver)g_smReactionCallback(receiver,player,command,target);
        return;
    }
    D::Record acceptedTrace{};
    if(player==g_soraActor && !SmNativeReactionCallbackAllowed(receiver,command,target,&acceptedTrace))return;
    std::uint32_t attackHandle=0;
    if(!target || !ReadHitTrace(reinterpret_cast<uintptr_t>(target)+8,attackHandle)) {
        SmRcHop(D::Stage::LocalHandler,"not-handled","target-handle-unreadable",acceptedTrace);
        if(!ownedReceiver)g_smReactionCallback(receiver,player,command,target);
        return;
    }
    if(!attackHandle && SmOwnerReactionCallback(receiver,player,command,&acceptedTrace))return;
    if(attackHandle && SmAxelReactionCallback(receiver,player,command,target))return; // VUH-2913
    for(auto& binding:g_sm.reactions) {
        if(binding.handle!=attackHandle || !binding.resource)continue;
        auto trace=D::Claim(binding.epoch,binding.resource,binding.objectId,binding.netId,command,binding.slot,binding.connection,binding.occurrence);
        trace.facts|=D::Native;trace.frame=g_frameCounter;trace.expires=binding.expires;
        trace.consumedBefore=binding.consumed?1:0;trace.actor=player;trace.sourceActor=receiver;
        const char* why="unknown";
        enemysync::DownedScope scope;
        if(!D::Check(enemysync::CaptureDownedScope(scope),"downed-scope",why) ||
           !D::Check(receiver==binding.owner,"binding-receiver",why) || !D::Check(player==g_soraActor,"canonical-player",why) ||
           !D::Check(command==binding.command,"binding-command",why) || !D::Check(!binding.consumed,"already-consumed",why) ||
           !D::Check(binding.slot==scope.localSlot,"local-selected-seat",why) || !D::Check(binding.occurrence!=0,"occurrence-zero",why) ||
           !D::Check(binding.connection==scope.connections[scope.localSlot],"binding-connection",why) ||
           !D::Check(binding.epoch==scope.epoch,"binding-epoch",why) ||
           D::Reject(scope.localSlot && g_frameCounter>=binding.expires,"local-binding-expired",why) ||
           D::Reject(downedspike::LogicallyDowned(),"local-down",why)) {
            SmRcHop(D::Stage::LocalHandler,"refused",why,trace);return;
        }
        enemysync::ScriptReactionCompletionRecord claim{binding.occurrence,scope.connections[scope.localSlot],
            binding.resource,binding.objectId,scope.epoch,binding.netId,static_cast<std::uint16_t>(command),scope.localSlot};
        if(scope.localSlot==0)(void)SmCompleteReaction(claim);
        else {
            binding.consumed=true;(void)SmSetReactionCommand(binding,0);
            trace.consumedAfter=1;
            SmRcHop(D::Stage::LocalHandler,"send-enter","attack-consumed",trace);
            const bool queued=enemysync::SendScriptReactionCompletion(claim);
            SmRcHop(D::Stage::LocalHandler,queued?"ring-enqueued":"send-refused","attack-send-returned",trace);
            Log("[script-rc] local completion source=%u resource=%u occurrence=%llu command=%u owner=%u queued=%u",
                claim.netId,claim.resource,claim.occurrence,claim.command,claim.ownerSlot,queued?1u:0u);
        }
        return;
    }
    // This also covers warmup/held sources: an unpaired prompt must never
    // enter a client BDX callback merely because pose driving is not ready.
    if(ownedReceiver) {
        SmRcHop(D::Stage::LocalHandler,"refused","unpaired-callback",acceptedTrace);
        Log("[script-rc] refused unpaired callback command=%u",command);
        return;
    }
    g_smReactionCallback(receiver,player,command,target);
}
static bool SmInstallReactionCallback() {
    std::uint8_t body[sizeof(R::kReactionCallback)]{};
    void* target=reinterpret_cast<void*>(g_exeBase+0x3B5FA0);
    if(!ReadHitTraceMemory(g_exeBase+0x3B5FA0,body,sizeof(body)) ||
       std::memcmp(body,R::kReactionCallback,sizeof(body)))return false;
    auto status=MH_CreateHook(target,reinterpret_cast<void*>(&SmReactionCallback),reinterpret_cast<void**>(&g_smReactionCallback));
    if(status==MH_OK)status=MH_EnableHook(target);
    return status==MH_OK;
}

// ---- both ----------------------------------------------------------------------------------------
// "name:count" for each nonzero counter, comma separated.
static void SmTrapList(const std::uint32_t* counts, char* out, std::size_t size) {
    std::size_t used = 0;
    out[0] = 0;
    for (std::size_t i = 0; i < R::kTrapCount && used + 1 < size; ++i) {
        if (!counts[i]) continue;
        const int n = std::snprintf(out + used, size - used, "%s%s:%u", used ? "," : "", R::kTraps[i].name, counts[i]);
        if (n < 0) break;
        used += static_cast<std::size_t>(n) < size - used ? static_cast<std::size_t>(n) : size - used - 1;
    }
    if (!used) std::snprintf(out, size, "-");
}

static void SmLogSecond() {
    const std::uint64_t now = GetTickCount64();
    if (!g_sm.secondMs) g_sm.secondMs = now;
    if (now - g_sm.secondMs < 1000) return;
    g_sm.secondMs = now;
    const auto s = g_sm.second;
    g_sm.second = {};
    if (!s.Any()) return;
    char raw[768], sent[768];
    SmTrapList(s.rawPerTrap, raw, sizeof(raw));
    SmTrapList(s.perTrap, sent, sizeof(sent));
    if (g_sm.role == 1)
        Log("[script-mirror] host sec frame=%u calls=%u repeats=%u replaced=%u queued=%u packets=%u records=%u "
            "unmapped=%u notMirrored=%u untracked=%u full=%u sendFailed=%u pending=%zu raw=%u offThread=%u effects=%zu "
            "sent=%s seen=%s", g_frameCounter, s.calls, s.repeats, s.replaced, s.queued, s.packets, s.records, s.unmapped,
            s.notMirrored, s.untracked, s.full, s.sendFailed, g_sm.sender.Pending(), s.raw, s.offThread,
            g_sm.hostEffects.Size(), sent, raw);
    else
        Log("[script-mirror] client sec frame=%u received=%u applied=%u held=%u heldApplied=%u expired=%u refused=%u "
            "unpaired=%u stale=%u faults=%llu effects=%zu records=%s", g_frameCounter, s.received, s.applied, s.held,
            s.heldApplied, s.expired, s.refused, s.unpaired, s.stale, static_cast<unsigned long long>(g_sm.faults),
            g_sm.clientEffects.Size(), sent);
}

static void OnFrameStart() {
    if (!g_sm.requested || !SmGameThread()) return;
    const auto transition = warp::TransitionSerial();
    const auto load = warp::LoadSerial();
    const bool pending = warp::TransitionPending();
    if (transition != g_sm.transition || load != g_sm.load || pending) {
        // A room change ends every key, effect and wait: they all belong to the old room.
        SmResetRoom(false);
        g_sm.transition = transition;
        g_sm.load = load;
        if (pending) return;
    }
    const std::uint8_t role = enemysync::ActivationRole();
    const std::uint32_t generation = enemysync::WorldSessionGeneration();
    SmOwnerFrame();
    if (generation!=g_sm.generation && g_sm.role==2) {
        g_sm.occurrences.Reset();g_sm.reactions={};
    }
    if (generation!=g_sm.generation || role!=2) SmAxelClientReset(); // VUH-2913: offers never cross a session
    SmTickReactions();
    if (generation != g_sm.generation) g_sm.births.Reset(); // whatever the role: rows never cross a session
    if (role != 1 || !generation) {
        if (g_sm.hostActive || g_sm.slots[0].swapped) SmStopHost(role != 1 ? "not-host" : "no-session");
    } else {
        if (g_sm.hostActive && generation != g_sm.generation) { g_sm.sender.Reset(); g_sm.retirements.Reset(); g_sm.hostEffects.Reset(); g_sm.hostAttacks.Reset(); }
        if (!g_sm.hostActive && !g_sm.swapRefused) {
            if (SmSwapAll()) g_sm.hostActive = true; else g_sm.swapRefused = true;
        }
        if (g_sm.hostActive) { SmTickBirths(generation); SmFlush(); }
    }
    if (generation != g_sm.generation && g_sm.role == 2) {
        SmRetireClientAttacks(); g_sm.clientEffects.Reset(); g_sm.hold.Reset();
    }
    if (role == 2) SmDrainHold(); else { g_sm.hold.Reset(); g_sm.clientEffects.Reset(); SmRetireClientAttacks(); }
    g_sm.role = role;
    g_sm.generation = generation;
    SmLogSecond();
}

static void Install() {
    g_sm.requested = enemysync::MirrorRequested() && !SmEnvParked();
    if (g_sm.requested) {
        g_smLifetimeReady=SmInstallAttackLifetimes();
        if(g_smLifetimeReady)g_smLifetimeReady=SmInstallReactionCallback();
        if(g_smLifetimeReady)g_smLifetimeReady=SmOwnerInstall();
        if(!g_smLifetimeReady)Log("[script-mirror] attack lifetime hooks refused; script replay parked");
    }
    Log("[script-mirror] configure requested=%u mirror=%u traps=%zu", g_sm.requested ? 1u : 0u,
        enemysync::MirrorRequested() ? 1u : 0u, R::kTrapCount);
    if (g_sm.requested) enemysync::SetScriptCallApply(&SmApplyRecord);
    if (g_sm.requested) enemysync::SetScriptReactionCompletion(&SmCompleteReaction);
    if (g_sm.requested) enemysync::SetScriptBirthHostQuery(&SmHostBirthQuery);
    if (g_sm.requested) enemysync::SetScriptChildHide(&SmHideScriptChild);
}

static void Shutdown() {
    if (!g_sm.requested) return;
    g_sm.hostActive = false;
    SmOwnerShutdown();
    SmRestoreAll();  // no actor writes from the shutdown thread
}

} // namespace scriptmirror
