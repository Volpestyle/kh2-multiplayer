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
    R::HostEffects hostEffects;
    R::ClientEffects clientEffects;
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
    std::uint8_t body[160] {};
    const uintptr_t native = g_exeBase + t.fnRva;
    bodyOk = t.bodySize <= sizeof(body) && ReadHitTraceMemory(native, body, t.bodySize) && R::BodyMatches(i, body);
    return true;
}

// ---- raw readers (POD only: SEH-safe) ---------------------------------------------------------
static uintptr_t SmDecode(std::uint32_t handle) noexcept {
    if (!handle || !g_resolveHandle) return 0;
    __try { return g_resolveHandle(handle); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
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

// ---- host ----------------------------------------------------------------------------------------
// What a detour keeps from before the native call.
struct Before {
    std::uint64_t args[5] {};
    std::uint32_t argc = 0;
    const R::HostEffect* effect = nullptr;  // effect calls: the tracked effect, checked live
    std::uint32_t effectHandle = 0;
    bool effectChecked = false;
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
    if (t.kind != R::Kind::EffectField && t.kind != R::Kind::EffectCall) return;
    if (R::TagOf(b.args[0]) != R::kTagAddress) return;
    b.effectHandle = R::BitsOf(b.args[0]);
    b.effect = g_sm.hostEffects.Find(b.effectHandle);
    b.effectChecked = b.effect && SmSameEffect(b.effectHandle, b.effect->effect, b.effect->part);
}

static void SmEmit(R::Call c) {
    if (!R::ValidValues(c.trap, c.values)) { ++g_sm.invalid; return; }
    ++g_sm.second.calls;
    ++g_sm.second.perTrap[c.trap];
    switch (g_sm.sender.Observe(c)) {
        case R::Sender::Outcome::Queued: ++g_sm.second.queued; break;
        case R::Sender::Outcome::Replaced: ++g_sm.second.replaced; break;
        case R::Sender::Outcome::Repeat: ++g_sm.second.repeats; break;
        case R::Sender::Outcome::Full: ++g_sm.second.full; break;
    }
}

// The announced, mirrored host spawn an actor argument names; 0 otherwise (counted).
static std::uint16_t SmMirroredSpawn(std::uint64_t arg, std::uint32_t& objectId) {
    const uintptr_t actor = SmActorOf(arg, objectId);
    if (!actor) { ++g_sm.second.unmapped; return 0; }
    const std::uint16_t netId = enemysync::HostEnemyNetId(actor, objectId);
    if (!netId) { ++g_sm.second.unmapped; return 0; }  // not an announced spawn: the player, party, weapons
    if (!enemymirror::FamilyAllowed(objectId)) { ++g_sm.second.notMirrored; return 0; }  // its client copy keeps its brain
    return netId;
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
        case R::Kind::Actor:
        case R::Kind::ActorEvent: {
            c.netId = SmMirroredSpawn(b.args[0], c.objectId);
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
            c.netId = SmMirroredSpawn(b.args[0], c.objectId);
            if (!c.netId) return;
            std::uint64_t result = 0;
            if (!SmCopyArgs(after, &result, 1) || R::TagOf(result) != R::kTagAddress || !R::BitsOf(result)) return;  // no effect made
            const std::uint32_t handle = R::BitsOf(result);
            const uintptr_t effect = SmDecode(handle);
            bool killed = true;
            uintptr_t part = 0;
            if (!SmEffectState(effect, killed, part) || killed || !part) return;
            c.effect = g_sm.hostEffects.Add(handle, effect, part, c.netId, c.objectId);
            if (t.kind == R::Kind::EffectStart) {
                for (unsigned k = 0; k < 3; ++k) c.values[k] = b.args[1 + k];
                SmEmit(c);
                return;
            }
            // start_pos(id, pos, a3, a4) is start(id, a3, a4) then set_pos(pos).
            c.trap = static_cast<std::uint8_t>(R::FindTrap(t.wireTable, t.wireIndex));
            c.values[0] = b.args[1]; c.values[1] = b.args[3]; c.values[2] = b.args[4];
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
            if (!b.effect || !b.effectChecked) {
                if (b.effect) g_sm.hostEffects.Erase(b.effectHandle);  // the handle no longer names it
                ++g_sm.second.untracked;
                return;
            }
            const R::HostEffect tracked = *b.effect;
            c.netId = tracked.netId;
            c.objectId = tracked.objectId;
            c.effect = tracked.id;
            c.trap = static_cast<std::uint8_t>(R::FindTrap(t.wireTable, t.wireIndex));
            if (t.kind == R::Kind::EffectField) {
                if (!SmReadFloat4(tracked.part + t.field, c.values)) { ++g_sm.faults; return; }  // what the setter wrote
            } else {
                for (unsigned k = 0; k < t.values; ++k) c.values[k] = b.args[1 + k];
            }
            SmEmit(c);
            if (t.ends) g_sm.hostEffects.Erase(b.effectHandle);
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
            records[k].effect = batch[k].effect;
            for (std::size_t a = 0; a < 4; ++a) records[k].args[a] = batch[k].values[a];
        }
        std::uint64_t seq = 0;
        if (!enemysync::SendScriptCalls(records, n, g_frameCounter, seq)) { ++g_sm.second.sendFailed; return; }
        g_sm.sender.Commit(n);
        ++g_sm.second.packets;
        g_sm.second.records += static_cast<std::uint32_t>(n);
    }
}

static void SmResetRoom() {
    g_sm.sender.Reset();
    g_sm.hostEffects.Reset();
    g_sm.clientEffects.Reset();
    g_sm.hold.Reset();
}

static void SmStopHost(const char* why) {
    if (!g_sm.hostActive && !g_sm.slots[0].swapped) return;
    g_sm.hostActive = false;
    SmRestoreAll();
    g_sm.sender.Reset();
    g_sm.hostEffects.Reset();
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

enum class Applied : std::uint8_t { Ok, Refused, Unpaired, Stale, Fault };
// One record on its driven copy.
static Applied SmReplay(const R::Call& c, uintptr_t actor) {
    const auto& t = R::kTraps[c.trap];
    std::uint64_t result = 0;
    if (!c.effect) {
        if (t.replay == R::Replay::XyzRotHelper) return SmCallXyzRot(actor, c.values) ? Applied::Ok : Applied::Fault;
        const std::uint32_t handle = SmOwnHandle(actor);
        if (!handle) return Applied::Refused;
        if (!SmCallTrap(c.trap, handle, c.values, result)) return Applied::Fault;
        if (t.kind == R::Kind::ActorEvent) g_sm.clientEffects.EraseOwner(c.netId);
        return Applied::Ok;
    }
    if (t.kind == R::Kind::EffectStart) {
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
    const uintptr_t actor = SmDrivenActor(c.netId, c.objectId);
    // Behind anything already waiting for this netId, or until its copy is driven. An effect record
    // waits only behind its own start; without one it has nothing to act on.
    if (!actor || g_sm.hold.HoldsNetId(c.netId)) {
        const bool needsStart = c.effect && R::kTraps[i].kind != R::Kind::EffectStart && !g_sm.clientEffects.Find(c.effect);
        if (needsStart && !g_sm.hold.HoldsStart(c.effect)) { ++g_sm.second.unpaired; return 0; }
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
        SmResetRoom();
        g_sm.transition = transition;
        g_sm.load = load;
        if (pending) return;
    }
    const std::uint8_t role = enemysync::ActivationRole();
    const std::uint32_t generation = enemysync::WorldSessionGeneration();
    if (role != 1 || !generation) {
        if (g_sm.hostActive || g_sm.slots[0].swapped) SmStopHost(role != 1 ? "not-host" : "no-session");
    } else {
        if (g_sm.hostActive && generation != g_sm.generation) { g_sm.sender.Reset(); g_sm.hostEffects.Reset(); }
        if (!g_sm.hostActive && !g_sm.swapRefused) {
            if (SmSwapAll()) g_sm.hostActive = true; else g_sm.swapRefused = true;
        }
        if (g_sm.hostActive) SmFlush();
    }
    if (role == 2) SmDrainHold(); else { g_sm.hold.Reset(); g_sm.clientEffects.Reset(); }
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
