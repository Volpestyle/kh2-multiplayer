// VUH-1819, included after puppet collection. Native owner thread only. Default off.
namespace downedspectate {
using downedspike::g_down;
using Follow = void(__fastcall*)(void*);
using Aim = unsigned char(__fastcall*)(uintptr_t);
static Follow originalFollow = nullptr;
static Aim originalAim = nullptr;
static std::atomic<bool> enabled {false};
static spectate::CallbackGate callbacks;
static thread_local unsigned callbackDepth = 0;
static thread_local bool scopedAim = false;
static void* followAddress = nullptr;
static void* aimAddress = nullptr;
static bool EnterCallback() { if (!callbacks.Enter()) return false; ++callbackDepth; return true; }
static void LeaveCallback() { --callbackDepth; callbacks.Leave(); }
static bool borrowing = false; // owner thread only
static uintptr_t borrowed = 0;
static uintptr_t borrowedBefore = 0;
static uintptr_t* borrowedPointer = nullptr;
static spectate::State policy;
static spectate::Channel receipt {};
static spectate::Channel* channel = nullptr;
static HANDLE mapping = nullptr;

static void Publish() {
    if (!channel) return;
    InterlockedIncrement(&channel->sequence);
    MemoryBarrier();
    // sequence is the only field not copied. One owner, read-only observers.
    std::memcpy(reinterpret_cast<char*>(channel) + 12,
                reinterpret_cast<const char*>(&receipt) + 12, sizeof(receipt) - 12);
    MemoryBarrier();
    InterlockedIncrement(&channel->sequence);
}
static void Release() {
    // The warp observer fires before native teardown. Remove our borrow there,
    // even if a lifecycle callback occurs inside native camera processing.
    if (downedspike::OwnerThread() && borrowing && borrowedPointer) {
        uintptr_t current = 0;
        if (ReadHitTrace(reinterpret_cast<uintptr_t>(borrowedPointer), current) && current == borrowed)
            *borrowedPointer = downedspike::Canonical(borrowedBefore) ? borrowedBefore : 0;
    }
    spectate::Release(policy);
    receipt.active = 0; receipt.slot = 255; ++receipt.released;
    Publish();
}
static bool Held() {
    if (!enabled || !downedspike::OwnerThread() || g_soloTestMode ||
        warp::TransitionPending() || eventholdnative::HoldingInput() ||
        g_down.state != downedspike::State::Downed || g_down.control) return false;
    const auto s = downedspike::Capture(g_soraActor);
    uint8_t menu = 0;
    return s.canonical && s.actor == g_down.downedActor && s.room == g_down.downedRoom &&
        s.handle0 == g_down.downedHandle0 && s.objentry == g_down.downedObjentry &&
        (s.flags & 4U) != 0 && s.hp == 0 && s.deadAction && s.controllerOff && !s.task && !s.inEvent &&
        s.room.ok && s.room.event == 0 &&
        ReadHitTrace(g_exeBase + offsets::OPEN_MENU, menu) && menu == 0xFF;
}
static void InputBody(void* raw) {
    if (!enabled || !downedspike::OwnerThread()) return;
    uint16_t bits = 0;
    const auto address = reinterpret_cast<uintptr_t>(raw) + offsets::input::RAW_SLOT0 + offsets::input::BUTTONS;
    const bool eligible = Held();
    if (!ReadHitTrace(address, bits)) { spectate::Input(policy, false, false); return; }
    spectate::Input(policy, eligible, (bits & spectate::CycleButton) != 0);
    if (eligible) *reinterpret_cast<uint16_t*>(address) = static_cast<uint16_t>(bits & ~spectate::CycleButton);
}
static void TickBody() {
    if (!enabled || !downedspike::OwnerThread()) return;
    uint32_t mode = 255;
    if (!Held() || !ReadHitTrace(g_exeBase + offsets::CAMERA_STRUCT + offsets::camera::CAMERA_TYPE, mode) || mode != 0) {
        if (policy.slot < 3 || policy.armed || policy.pending) Release();
    }
}
static spectate::Target NativeTarget(uintptr_t actor, uint64_t connection, bool body,
                                     uint32_t transition, uint32_t load) {
    spectate::Target out {};
    if (!actor || !connection) return out;
    const auto a = CaptureHitActor(actor);
    constexpr auto mask = nativehittrace::ActorObject | nativehittrace::ActorStatus |
        nativehittrace::ActorType | nativehittrace::ActorId | nativehittrace::ActorName |
        nativehittrace::ActorHp | nativehittrace::ActorMaxHp | nativehittrace::ActorRepeated;
    uint32_t handle = 0, flags = 0;
    float x = 0, y = 0, z = 0, w = 0;
    if ((a.readMask & mask) != mask || !a.objentry || !a.status || a.type > 1 ||
        a.maxHp <= 0 || a.hp < 0 || a.hp > a.maxHp ||
        !ReadHitTrace(actor, handle) || !ReadHitTrace(actor + 0x9B8, flags) ||
        !ReadHitTrace(actor + 0x670, x) || !ReadHitTrace(actor + 0x674, y) ||
        !ReadHitTrace(actor + 0x678, z) || !ReadHitTrace(actor + 0x67C, w) ||
        !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(w) ||
        std::fabs(w - 1.0f) > 0.01f) return out;
    if (body ? !downedspike::Canonical(actor) :
        (a.hp <= 0 || (flags & 4U) || !enemysync::CurrentPuppetActor(actor, transition, load))) return out;
    const auto repeated = CaptureHitActor(actor);
    uint32_t handleAgain = 0;
    if ((repeated.readMask & mask) != mask || repeated.objentry != a.objentry ||
        repeated.status != a.status || repeated.objectId != a.objectId || repeated.type != a.type ||
        repeated.namePrefix != a.namePrefix || !ReadHitTrace(actor, handleAgain) || handleAgain != handle) return out;
    return {actor, connection, a.objentry, a.status, handle, true};
}
static spectate::Facts Facts() {
    spectate::Facts f {};
    f.eligible = Held();
    if (!f.eligible) return f;
    const auto authority = enemysync::CapturePuppetAuthority();
    if (!hud::ValidAuthority(authority)) { f.eligible = false; return f; }
    f.localSlot = authority.localSlot; f.generation = authority.generation;
    f.transition = warp::TransitionSerial(); f.load = warp::LoadSerial();
    f.targets[f.localSlot] = NativeTarget(g_soraActor, authority.connectionIds[f.localSlot], true, f.transition, f.load);
    for (int i = 0; i < 2; ++i) {
        const auto& d = g_puppets[i]; const auto& a = d.pose.pose;
        const auto slot = static_cast<uint8_t>(a.ownerSlot);
        spectate::CandidateFacts candidate {};
        candidate.actor = d.actor; candidate.localActor = g_soraActor;
        candidate.index = static_cast<unsigned>(i); candidate.slot = slot; candidate.localSlot = f.localSlot;
        candidate.clones = {g_clones[0], g_clones[1]};
        candidate.friends = {g_friend1Actor, g_friend2Actor};
        candidate.planned = partynative::AppliedClones(); candidate.present = partynative::PresentPuppetIndex();
        candidate.kits = partynative::KitsActive(); candidate.blocksPlayer = playerkit::BlocksNativeSoraPuppets();
        candidate.wantKit = partynative::PuppetKit(i); candidate.otherKit = partynative::PuppetKit(1-i);
        candidate.remote = {IsPuppetActive(i),
            a.worldId == g_down.downedRoom.world && a.roomId == g_down.downedRoom.room,
            std::isfinite(a.position.x) && std::isfinite(a.position.y) && std::isfinite(a.position.z),
            d.applied && d.actor != 0 && d.actor != g_soraActor,
            g_frameCounter - d.poseFrame, a.flags, a.hp};
        const auto target = spectate::ResolveCandidate(candidate,
            [&](uint64_t actor) { return enemysync::CurrentPuppetActor(static_cast<uintptr_t>(actor), f.transition, f.load); },
            [&](uint64_t actor) {
                // Resolver has already checked slot/pose/plan/census. Every
                // metadata read is guarded; also reject a recycled binding.
                const auto now = CaptureHitActor(static_cast<uintptr_t>(actor));
                const auto& bound = d.boundActor;
                constexpr auto mask = nativehittrace::ActorObject | nativehittrace::ActorStatus |
                    nativehittrace::ActorType | nativehittrace::ActorId | nativehittrace::ActorName | nativehittrace::ActorRepeated;
                spectate::CandidateMetadata metadata {};
                if ((now.readMask & mask) != mask || (bound.readMask & mask) != mask ||
                    now.actor != bound.actor || now.objentry != bound.objentry || now.status != bound.status ||
                    now.objectId != bound.objectId || now.type != bound.type || now.namePrefix != bound.namePrefix) return metadata;
                metadata.target = NativeTarget(static_cast<uintptr_t>(actor), authority.connectionIds[slot], false, f.transition, f.load);
                metadata.objectId = now.objectId; metadata.type = now.type; return metadata;
            });
        if (slot < 3 && target.valid) f.targets[slot] = target;
    }
    if (!hud::SameAuthority(authority, enemysync::CapturePuppetAuthority()) ||
        f.transition != warp::TransitionSerial() || f.load != warp::LoadSerial() || !Held()) f.eligible = false;
    return f;
}
// Suppress the native target-action camera/aim branch ONLY inside our borrowed
// normal-follow call. This avoids mode1 and actor+A10 writes on a teammate.
static unsigned char __fastcall AimHook(uintptr_t actor) {
    if (scopedAim && actor == borrowed) {
        ++receipt.aimSuppressed; return 0;
    }
    return originalAim(actor);
}
static void FollowBody(void* camera) {
    if (!enabled || !downedspike::OwnerThread() || borrowing ||
        reinterpret_cast<uintptr_t>(camera) != g_exeBase + offsets::CAMERA_STRUCT) {
        originalFollow(camera); return;
    }
    auto* ptr = reinterpret_cast<uintptr_t*>(static_cast<char*>(camera) + offsets::camera::ACTOR_PTR);
    uint32_t mode = 255; uintptr_t before = 0;
    auto f = Facts();
    if (!ReadHitTrace(reinterpret_cast<uintptr_t>(camera) + offsets::camera::CAMERA_TYPE, mode) || mode != 0 ||
        !ReadHitTrace(reinterpret_cast<uintptr_t>(ptr), before) || before != g_soraActor) f.eligible = false;
    const bool cycle = policy.pending;
    const auto slot = spectate::Step(policy, f);
    receipt.frame = g_frameCounter; receipt.localActor = g_soraActor; receipt.episode = g_down.episode;
    receipt.mode = mode; ++receipt.calls;
    receipt.generation = f.generation; receipt.transition = f.transition; receipt.load = f.load;
    if (slot >= 3) {
        receipt.active = 0; receipt.slot = 255; receipt.actorBefore = before;
        originalFollow(camera);
        ReadHitTrace(reinterpret_cast<uintptr_t>(ptr), receipt.actorAfter);
        receipt.actorDuring = before; Publish(); return;
    }
    const uintptr_t target = static_cast<uintptr_t>(policy.target.actor);
    if (cycle) ++receipt.cycles;
    receipt.active = 1; receipt.slot = slot; receipt.actorBefore = before;
    ++receipt.overrides;
    if (receipt.overrides == 1 || cycle || borrowed != target)
        Log("[spectate] frame=%u slot=%u actor=%llX local=%llX episode=%llX cycles=%u", g_frameCounter, slot,
            static_cast<unsigned long long>(target), static_cast<unsigned long long>(g_soraActor),
            static_cast<unsigned long long>(g_down.episode), receipt.cycles);
    borrowed = target; borrowedBefore = before; borrowedPointer = ptr; borrowing = true; scopedAim = true;
    __try {
        *ptr = target;
        receipt.actorDuring = *ptr; // actual pointer used by native normal-follow
        originalFollow(camera);
    } __finally {
        const bool same = f.transition == warp::TransitionSerial() && f.load == warp::LoadSerial() && !warp::TransitionPending();
        uintptr_t current = 0;
        if (ReadHitTrace(reinterpret_cast<uintptr_t>(ptr), current) &&
            spectate::RestoreOwned(current, target, same, downedspike::Canonical(before))) *ptr = before;
        if (!same || AbnormalTermination()) Release();
        borrowing = false; borrowedPointer = nullptr; scopedAim = false;
        ReadHitTrace(reinterpret_cast<uintptr_t>(ptr), receipt.actorAfter);
        Publish();
    }
}
// No C++ RAII objects span SEH: even abnormal native exits retire the reader.
static void Input(void* raw) {
    if (!enabled || !EnterCallback()) return;
    __try { InputBody(raw); } __finally { LeaveCallback(); }
}
static void Tick() {
    if (!enabled || !EnterCallback()) return;
    __try { TickBody(); } __finally { LeaveCallback(); }
}
static void __fastcall FollowHook(void* camera) {
    if (!EnterCallback()) { originalFollow(camera); return; }
    __try { FollowBody(camera); } __finally { LeaveCallback(); }
}
static void RequestRelease() {
    // Transition nested inside originalFollow must release even after Close.
    if (callbackDepth) { Release(); return; }
    if (!enabled || !EnterCallback()) return;
    __try { Release(); } __finally { LeaveCallback(); }
}
static bool RetainsMinHookResources() { return callbacks.RetainsHooks(); }
static bool Flag(const wchar_t* name) {
    wchar_t value[8] {}; return GetEnvironmentVariableW(name, value, 8) == 1 && value[0] == L'1';
}
static void Install() {
    if (!Flag(L"KH2COOP_DOWNED_SPECTATE")) return;
    constexpr uint8_t followBytes[] = {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57};
    constexpr uint8_t aimBytes[] = {0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x8b,0x49,0x0c,0xe8,0x5f,0x2f,0x0f,0x00};
    if (!g_down.requested || g_down.control || g_down.state != downedspike::State::Ready ||
        !downedspike::Matches(g_exeBase, 0x165110, followBytes, sizeof(followBytes)) ||
        !downedspike::Matches(g_exeBase, 0x3BA300, aimBytes, sizeof(aimBytes))) {
        Log("[spectate] configure REFUSED: downed dependency or camera signatures"); return;
    }
    auto* follow = reinterpret_cast<void*>(g_exeBase + 0x165110);
    auto* aim = reinterpret_cast<void*>(g_exeBase + 0x3BA300);
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&FollowHook), &pinned)) {
        Log("[spectate] configure REFUSED: module pin"); return;
    }
    const auto a = MH_CreateHook(aim, reinterpret_cast<void*>(&AimHook), reinterpret_cast<void**>(&originalAim));
    const auto b = a == MH_OK ? MH_CreateHook(follow, reinterpret_cast<void*>(&FollowHook), reinterpret_cast<void**>(&originalFollow)) : MH_ERROR_NOT_CREATED;
    if (a == MH_OK) { aimAddress = aim; callbacks.RetainHooks(); }
    if (b == MH_OK) { followAddress = follow; callbacks.RetainHooks(); }
    if (a != MH_OK || b != MH_OK || MH_EnableHook(aim) != MH_OK || MH_EnableHook(follow) != MH_OK) {
        // No partial active feature. Trampolines remain until normal hook teardown.
        if (a == MH_OK) MH_DisableHook(aim);
        if (b == MH_OK) MH_DisableHook(follow);
        Log("[spectate] configure REFUSED: hook installation"); return;
    }
    enabled = true; receipt.installed = 1; receipt.slot = 255;
    if (Flag(L"KH2COOP_DOWNED_SPECTATE_FIXTURE")) {
        wchar_t name[80] {};
        std::swprintf(name, 80, L"Local\\kh2coop_spectate_%lu", GetCurrentProcessId());
        mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(spectate::Channel), name);
        if (mapping) channel = static_cast<spectate::Channel*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(spectate::Channel)));
        if (channel) { std::memset(channel, 0, sizeof(*channel)); channel->magic = 0x53504543; channel->version = 1; }
    }
    Log("[spectate] configure installed=1 cycle=R3 normalFollow=165110 scoped=1"); Publish();
}
static bool Shutdown() {
    if (!RetainsMinHookResources()) return true;
    enabled = false;
    // Keep aim suppression active until every borrowed follow has restored.
    // No other feature is needed to retain our pinned DLL and trampolines.
    return spectate::DrainCallbacks(callbacks, callbackDepth != 0,
        [] { if (followAddress) MH_DisableHook(followAddress); },
        [] { Sleep(0); },
        [] {
            if (aimAddress) MH_DisableHook(aimAddress);
            spectate::Release(policy); receipt.active = 0; receipt.slot = 255; ++receipt.released; Publish();
        });
}
} // namespace downedspectate
static void ReleaseDownedSpectate() { downedspectate::RequestRelease(); }
