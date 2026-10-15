// ScriptMirror.inl - VUH-2252 (D14 item 3): the host's script calls on its mirrored enemies.
// Included by EntityHook.cpp after EnemyMirror.inl (the driven table) and g_resolveHandle.
// On by default with the enemy mirror (enemysync::MirrorRequested); KH2COOP_SCRIPT_MIRROR=0 parks it.
//
// Host (role 1, live session):
//   Swap  each trap's .data entry in the bdscript tables (exe+0x753490) for a detour, only after
//         the entry's function, flags and full body match this build. The detour always runs the
//         native trap first, then reads the call's target actor (the trap's own decode: handle,
//         then its +4) and values. A call on an announced, mirrored host spawn goes to the Sender:
//         a repeat of the last value for its key is dropped, a change replaces a queued one.
//   Flush at frame start, the queued calls in call order: up to two ScriptCalls of 32 records.
// Client (role 2):
//   Apply each admitted record to the copy EnemyMirror drives for that netId (brain skipped):
//         through the native trap with that copy's own handle, or obj_set_xyzrot's float4 through
//         its helper 0x3B65F0. A record for a copy not driven yet waits in the Hold (latest per key,
//         180 frames) and is applied once the copy is driven.
// Both: one "[script-mirror] ... sec" line per second with activity: the per-second message counts.
// Nothing here writes HP, team, collision, motion or position.

namespace scriptmirror {

namespace R = rules;
using TrapFn = void(__fastcall*)(std::uint64_t* args);
using XyzRotFn = void(__fastcall*)(uintptr_t actor, const float* rotation);

struct Slot { uintptr_t entry = 0, native = 0; bool swapped = false; };
struct State {
    bool requested = false, hostActive = false, swapRefused = false;
    bool clientChecked = false, clientReady = false;
    std::uint8_t role = 0;
    std::uint32_t generation = 0, transition = 0, load = 0;
    Slot slots[R::kTrapCount] {};
    XyzRotFn xyzRot = nullptr;
    R::Sender sender;
    R::Hold hold;
    R::Second second;
    std::uint64_t secondMs = 0;
    std::uint64_t faults = 0, invalid = 0;
};
static State g_sm;

static bool SmGameThread() noexcept { return spawncontroller::IsDiagnosticGameThread(); }

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
    std::uint8_t body[128] {};
    const uintptr_t native = g_exeBase + t.fnRva;
    bodyOk = R::kBodies[i].size <= sizeof(body) && ReadHitTraceMemory(native, body, R::kBodies[i].size) && R::BodyMatches(i, body);
    return true;
}

// ---- host ----------------------------------------------------------------------------------------
static void SmObserve(std::size_t i, const std::uint64_t* args);
template <unsigned I> static void __fastcall SmDetour(std::uint64_t* args) {
    reinterpret_cast<TrapFn>(g_sm.slots[I].native)(args);  // the native trap, always and first
    SmObserve(I, args);
}
static_assert(R::kTrapCount == 7, "one detour per trap");
static constexpr TrapFn kSmDetours[R::kTrapCount] = {&SmDetour<0>, &SmDetour<1>, &SmDetour<2>, &SmDetour<3>,
                                                     &SmDetour<4>, &SmDetour<5>, &SmDetour<6>};

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
    for (std::size_t i = 0; i < R::kTrapCount; ++i) {
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
        auto& s = g_sm.slots[i];
        void* prior = InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(s.entry),
                                                        reinterpret_cast<void*>(kSmDetours[i]), reinterpret_cast<void*>(s.native));
        s.swapped = prior == reinterpret_cast<void*>(s.native);
        if (!s.swapped) { SmRestoreAll(); Log("[script-mirror] swap-raced trap=%s", R::kTraps[i].name); return false; }
    }
    Log("[script-mirror] swap ok traps=%zu", R::kTrapCount);
    return true;
}

// The call's target actor and values, read as the trap reads them (POD only: SEH-safe).
static bool SmReadCall(std::size_t i, const std::uint64_t* args, uintptr_t& actor, std::uint32_t& objectId,
                       std::uint64_t* values) noexcept {
    actor = 0; objectId = 0;
    if (!g_resolveHandle) return false;
    __try {
        if (R::TagOf(args[0]) != R::kTagAddress) return false;
        const uintptr_t object = g_resolveHandle(R::BitsOf(args[0]));
        if (!object) return false;
        actor = g_resolveHandle(*reinterpret_cast<const std::uint32_t*>(object + 4));
        if (!actor) return false;
        const uintptr_t objentry = *reinterpret_cast<const uintptr_t*>(actor + offsets::actor::OBJENTRY_PTR);
        if (!objentry) return false;
        objectId = *reinterpret_cast<const std::uint32_t*>(objentry + offsets::objentry::OBJECT_ID);
        const auto& t = R::kTraps[i];
        if (t.vector) {
            if (R::TagOf(args[1]) != R::kTagAddress) return false;
            const auto* v = reinterpret_cast<const std::uint32_t*>(g_resolveHandle(R::BitsOf(args[1])));
            if (!v) return false;
            for (unsigned k = 0; k < 4; ++k) values[k] = R::Tagged(R::kTagFloat, v[k]);  // NaN already zeroed by the trap
        } else {
            for (unsigned k = 0; k < t.values; ++k) values[k] = args[1 + k];
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        actor = 0;
        return false;
    }
}

static void SmObserve(std::size_t i, const std::uint64_t* args) {
    if (!g_sm.hostActive || !SmGameThread()) return;
    R::Call c {};
    uintptr_t actor = 0;
    std::uint32_t objectId = 0;
    if (!SmReadCall(i, args, actor, objectId, c.values)) { ++g_sm.second.unmapped; return; }
    const std::uint16_t netId = enemysync::HostEnemyNetId(actor, objectId);
    if (!netId) { ++g_sm.second.unmapped; return; }  // not an announced spawn: the player, party, weapons
    if (!enemymirror::FamilyAllowed(objectId)) { ++g_sm.second.notMirrored; return; }  // its client copy keeps its brain
    if (!R::ValidValues(i, c.values)) { ++g_sm.invalid; return; }
    c.netId = netId;
    c.objectId = objectId;
    c.trap = static_cast<std::uint8_t>(i);
    ++g_sm.second.calls;
    ++g_sm.second.perTrap[i];
    switch (g_sm.sender.Observe(c)) {
        case R::Sender::Outcome::Queued: ++g_sm.second.queued; break;
        case R::Sender::Outcome::Replaced: ++g_sm.second.replaced; break;
        case R::Sender::Outcome::Repeat: ++g_sm.second.repeats; break;
        case R::Sender::Outcome::Full: ++g_sm.second.full; break;
    }
}

static void SmFlush() {
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
            for (std::size_t a = 0; a < 4; ++a) records[k].args[a] = batch[k].values[a];
        }
        std::uint64_t seq = 0;
        if (!enemysync::SendScriptCalls(records, n, g_frameCounter, seq)) { ++g_sm.second.sendFailed; return; }
        g_sm.sender.Commit(n);
        ++g_sm.second.packets;
        g_sm.second.records += static_cast<std::uint32_t>(n);
    }
}

static void SmStopHost(const char* why) {
    if (!g_sm.hostActive && !g_sm.slots[0].swapped) return;
    g_sm.hostActive = false;
    SmRestoreAll();
    g_sm.sender.Reset();
    Log("[script-mirror] host stop why=%s faults=%llu invalid=%llu", why, static_cast<unsigned long long>(g_sm.faults),
        static_cast<unsigned long long>(g_sm.invalid));
}

// ---- client --------------------------------------------------------------------------------------
// The native traps, unswapped, and the xyzrot helper, all this build's (checked once).
static bool SmClientReady() {
    if (g_sm.clientChecked) return g_sm.clientReady;
    g_sm.clientChecked = true;
    for (std::size_t i = 0; i < R::kTrapCount; ++i) {
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
    g_sm.clientReady = true;
    Log("[script-mirror] client ready traps=%zu", R::kTrapCount);
    return true;
}

// The copy EnemyMirror drives for netId right now (brain skipped), with the host's object id.
static uintptr_t SmDrivenActor(std::uint16_t netId, std::uint32_t objectId) {
    if (!enemymirror::OwnerThread()) return 0;
    for (const auto& d : enemymirror::g_driven) {
        if (!d.actor || d.netId != netId || !d.running || g_frameCounter - d.frame > 2) continue;
        uintptr_t objentry = 0;
        std::uint32_t id = 0;
        if (ReadHitTrace(d.actor + offsets::actor::OBJENTRY_PTR, objentry) && objentry &&
            ReadHitTrace(objentry + offsets::objentry::OBJECT_ID, id) && id == objectId)
            return d.actor;
        return 0;
    }
    return 0;
}

static bool SmReplayRaw(std::size_t i, uintptr_t actor, const std::uint64_t* values) noexcept {
    __try {
        if (R::kTraps[i].replay == R::Replay::XyzRotHelper) {
            float v[4];
            for (unsigned k = 0; k < 4; ++k) { const std::uint32_t b = R::BitsOf(values[k]); std::memcpy(&v[k], &b, 4); }
            g_sm.xyzRot(actor, v);
            return true;
        }
        // The trap decodes its first argument, then that object's +4: an actor's own handle names itself.
        const std::uint32_t handle = *reinterpret_cast<const std::uint32_t*>(actor + 4);
        if (!handle || !g_resolveHandle || g_resolveHandle(handle) != actor) return false;
        std::uint64_t args[5] = {R::Tagged(R::kTagAddress, handle), values[0], values[1], values[2], values[3]};
        reinterpret_cast<TrapFn>(g_sm.slots[i].native)(args);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SmReplay(const R::Call& c, uintptr_t actor) {
    if (SmReplayRaw(c.trap, actor, c.values)) return true;
    ++g_sm.faults;
    return false;
}

static int SmApplyRecord(const enemysync::ScriptCallRecord& r) {
    ++g_sm.second.received;
    const int i = R::FindTrap(r.table, r.index);
    if (i < 0 || !SmGameThread() || !SmClientReady()) { ++g_sm.second.refused; return 0; }
    ++g_sm.second.perTrap[i];
    R::Call c {};
    c.netId = r.netId;
    c.objectId = r.objectId;
    c.trap = static_cast<std::uint8_t>(i);
    for (std::size_t a = 0; a < 4; ++a) c.values[a] = r.args[a];
    if (!R::ValidValues(static_cast<std::size_t>(i), c.values)) { ++g_sm.second.refused; return 0; }
    const uintptr_t actor = SmDrivenActor(c.netId, c.objectId);
    if (!actor) {
        if (g_sm.hold.Put(c, g_frameCounter)) ++g_sm.second.held; else ++g_sm.second.refused;
        return 0;
    }
    g_sm.hold.Erase(R::KeyOf(c));  // a held older value for this key must not land after this one
    if (!SmReplay(c, actor)) { ++g_sm.second.refused; return 0; }
    ++g_sm.second.applied;
    return 1;
}

static void SmDrainHold() {
    if (!g_sm.hold.Size()) return;
    std::size_t expired = 0;
    const std::size_t taken = g_sm.hold.Drain(g_frameCounter, [](const R::Call& c) {
        const uintptr_t actor = SmDrivenActor(c.netId, c.objectId);
        if (!actor) return false;
        SmReplay(c, actor);
        return true;
    }, expired);
    g_sm.second.heldApplied += static_cast<std::uint32_t>(taken);
    g_sm.second.expired += static_cast<std::uint32_t>(expired);
}

// ---- both ----------------------------------------------------------------------------------------
static void SmLogSecond() {
    const std::uint64_t now = GetTickCount64();
    if (!g_sm.secondMs) g_sm.secondMs = now;
    if (now - g_sm.secondMs < 1000) return;
    g_sm.secondMs = now;
    const auto s = g_sm.second;
    g_sm.second = {};
    if (!s.Any()) return;
    const auto* p = s.perTrap;
    if (g_sm.role == 1)
        Log("[script-mirror] host sec frame=%u calls=%u repeats=%u replaced=%u queued=%u packets=%u records=%u "
            "unmapped=%u notMirrored=%u full=%u sendFailed=%u pending=%zu traps=rot:%u,xyzrot:%u,texanm:%u/%u,shadow:%u/%u,scissor:%u",
            g_frameCounter, s.calls, s.repeats, s.replaced, s.queued, s.packets, s.records, s.unmapped, s.notMirrored, s.full,
            s.sendFailed, g_sm.sender.Pending(), p[0], p[1], p[2], p[3], p[4], p[5], p[6]);
    else
        Log("[script-mirror] client sec frame=%u received=%u applied=%u held=%u heldApplied=%u expired=%u refused=%u "
            "faults=%llu traps=rot:%u,xyzrot:%u,texanm:%u/%u,shadow:%u/%u,scissor:%u", g_frameCounter, s.received, s.applied,
            s.held, s.heldApplied, s.expired, s.refused, static_cast<unsigned long long>(g_sm.faults), p[0], p[1], p[2],
            p[3], p[4], p[5], p[6]);
}

static void OnFrameStart() {
    if (!g_sm.requested || !SmGameThread()) return;
    const auto transition = warp::TransitionSerial();
    const auto load = warp::LoadSerial();
    const bool pending = warp::TransitionPending();
    if (transition != g_sm.transition || load != g_sm.load || pending) {
        // A room change ends every key: the baseline and anything waiting belong to the old room.
        g_sm.sender.Reset();
        g_sm.hold.Reset();
        g_sm.transition = transition;
        g_sm.load = load;
        if (pending) return;
    }
    const std::uint8_t role = enemysync::ActivationRole();
    const std::uint32_t generation = enemysync::WorldSessionGeneration();
    if (role != 1 || !generation) {
        if (g_sm.hostActive || g_sm.slots[0].swapped) SmStopHost(role != 1 ? "not-host" : "no-session");
    } else {
        if (g_sm.hostActive && generation != g_sm.generation) g_sm.sender.Reset();
        if (!g_sm.hostActive && !g_sm.swapRefused) {
            if (SmSwapAll()) g_sm.hostActive = true; else g_sm.swapRefused = true;
        }
        if (g_sm.hostActive) SmFlush();
    }
    if (role == 2) SmDrainHold(); else g_sm.hold.Reset();
    g_sm.role = role;
    g_sm.generation = generation;
    SmLogSecond();
}

static void Install() {
    g_sm.requested = enemysync::MirrorRequested() && !SmEnvParked();
    Log("[script-mirror] configure requested=%u mirror=%u traps=%zu", g_sm.requested ? 1u : 0u,
        enemysync::MirrorRequested() ? 1u : 0u, R::kTrapCount);
    if (g_sm.requested) enemysync::SetScriptCallApply(&SmApplyRecord);
}

static void Shutdown() {
    if (!g_sm.requested) return;
    g_sm.hostActive = false;
    SmRestoreAll();  // no actor writes from the shutdown thread
}

} // namespace scriptmirror
