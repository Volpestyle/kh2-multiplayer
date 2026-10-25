// VUH-2605 S4: Twilight Thorn's script-born Creepers on the client, and the host's motion permit for them.
//
// The host announces each child (ScriptMirror 255/15) once its own census lists it. The client keeps the
// receipt in the existing SpawnEmission ledger, waits for the parent's and the child's own manifests, then
// builds the child once through the native factory at the host manifest's point, under native16's exact
// resident-resource lease. The child is built like every other mirrored mob (lead #259, option C): its
// constructor-time initializer runs natively (audited local actor setup only) and its brain is host-only
// from its first update (SuppressClientDecision). The ordinary binder adopts it by its exact typed receipt,
// never by points; afterwards it follows its own manifest, HP and death like any mirrored enemy.
// Global 317 stays denied: only an exact receipt-backed child gets stream, pose or record authority.

struct ScriptChild {
    ScriptCallEntry birth{};                       // canonical BEGIN
    std::uint64_t returnSerial{};                  // this client's native-return counter: incarnation, not identity
    uintptr_t actor{};
    uintptr_t objentry{};                          // read at the native return
    std::uint32_t returnFrame{};
    ordinarybinding::ScriptActorScope scope{};     // full scope captured at the native return
    int row = -1;                                  // the census row this body took when first listed
    bool adopted{}, retired{};
    bool held{};                                   // brain hook refused: its native update never runs
};
std::vector<ScriptChild> g_scriptChildren;
// Held addresses (Sage 18ccfc67, lead #270): quarantined until native room teardown, fail-closed by construction.
// Keyed by address only, nothing read: a same-class body later at a held address stays held. No reset, role or
// generation change releases them; only a completed native room load (the load serial) does. Fixed capacity,
// no allocation. Once any hold happens, no further script child is built in that room.
constexpr std::size_t kScriptChildHoldCap = 16;
std::array<uintptr_t, kScriptChildHoldCap> g_heldScriptChildren{};
std::size_t g_heldScriptChildCount = 0;
bool g_scriptChildRoomBlocked = false;             // a hold (or a full hold array) stops building for the room
std::uint32_t g_holdLoad = 0;                     // the native load the holds belong to
std::uint64_t g_scriptReturnSerial = 0;           // never rewinds
EnemyFactoryFn g_scriptChildFactory = nullptr;    // 0x3DF930, prologue byte-checked at install; scoped to this path
unsigned g_scriptChildLogs = 0;
std::uint8_t g_scriptChildNullRetries = 0;
ScriptBirthHostQuery g_scriptBirthHostQuery = nullptr;
ScriptChildBrainGate g_scriptChildBrainGate = nullptr; // EnemyMirror; none: no child is ever built
ScriptChildHide g_scriptChildHide = nullptr;           // ScriptMirror's native obj_hide
constexpr std::uint8_t kScriptChildNullRetries = 3;
constexpr std::uint32_t kScriptChildAdoptFrames = 300; // a constructed child the binder never adopts stops being a candidate
constexpr std::uint32_t kScriptChildCensusFrames = 60;  // a returned body never seen by the census is gone

std::uint64_t ScriptBirthOccurrence(const ScriptCallEntry& e) noexcept {
    return static_cast<std::uint32_t>(e.args[0]) | (static_cast<std::uint64_t>(static_cast<std::uint32_t>(e.args[1])) << 32);
}
ScriptCallEntry ScriptBirthEnd(ScriptCallEntry e) noexcept { e.index = SCRIPT_ACTOR_BIRTH_END; return e; }

void ScriptChildLog(const char* fmt, ...) {
    if (!g_log || g_scriptChildLogs >= 48) return;
    ++g_scriptChildLogs;
    char line[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    g_log("%s", line);
}

void ResetScriptChildren() {
    // The room teardown owns every native body; only our records go. Held addresses are not records: they stay
    // quarantined until the native room changes (SyncScriptChildHoldRoom).
    g_scriptChildren.clear();
    g_scriptChildNullRetries = 0;
    g_scriptChildLogs = 0;
}

// Read-only full scope while callers retain a census. Never refreshes activation/session state.
bool ScriptActorConsumerScope(ordinarybinding::Scope& out) {
    out = {};
    RoomTransition room;
    const auto role = g_role;
    const auto generation = g_activationGeneration;
    const auto writer = g_bridge.DeliverySerial();
    if (!g_mirrorRequested || !spawncontroller::IsDiagnosticGameThread() ||
        role == Role::Off || CurrentRole() != role || !generation ||
        g_bridge.SessionGeneration() != generation || !writer || writer != g_orderedDeliverySerial ||
        !RuntimeWriterLive() || !g_inst.live || g_recordBindingRequested || g_recordAuthorityRequested ||
        g_resyncWriteFence != ResyncWriteFence::None || !SafeNativeGameplay() ||
        g_seenLoad != warp::LoadSerial() || g_seenTransition != warp::TransitionSerial() ||
        !ReadLocationChecked(room) || room.worldId != g_inst.world || room.roomId != g_inst.room ||
        room.door != g_inst.door || room.mapProgram != g_inst.map || room.battleProgram != g_inst.btl ||
        room.eventProgram != g_inst.evt || (role == Role::Client && !g_host.arrived)) return false;
    const auto epoch = role == Role::Client ? g_host.epoch : g_epoch;
    ordinarybinding::ScriptActorScope full{generation, epoch, warp::LoadSerial(), warp::TransitionSerial(),
        g_bridge.ConnectionId(0), role == Role::Host ? writer : g_bridge.PeerDeliverySerial(0), writer,
        {room.worldId, room.roomId, room.door, room.mapProgram, room.battleProgram, room.eventProgram}};
    if (!full.epoch || !full.load || !full.transition || !full.hostConnection || !full.hostDelivery ||
        !g_hitTraceFrame || g_bridge.SessionGeneration() != generation || g_bridge.DeliverySerial() != writer) return false;
    out = {generation, epoch, g_hitTraceFrame, warp::LoadSerial(), full};
    return true;
}

// The exact child's current native roots: ready status, objentry 317, never an indexed controller/record.
bool ScriptChildRoots(uintptr_t actor, ordinarybinding::Identity& out) {
    out = {};
    uintptr_t objentry = 0, status = 0;
    std::uint32_t objectId = 0;
    if (!actor || !ReadNative(actor + offsets::actor::OBJENTRY_PTR, objentry) || !objentry ||
        !ReadNative(objentry + offsets::objentry::OBJECT_ID, objectId) || objectId != enemymirror::kCreeperObjectId ||
        !ReadNative(actor + ACTOR_STATUS, status) || !status) return false;
    out = {actor, objentry, status, 0, 0, objectId};
    return true;
}

// ---- receipts -----------------------------------------------------------------------------------
bool ReceiveScriptActorBirthImpl(std::uint16_t netId, std::uint32_t objectId, std::uint16_t index, std::uint32_t effect,
                             const std::uint64_t* args) noexcept {
    try {
        if (!args || !spawncontroller::IsDiagnosticGameThread() || g_role != Role::Client || !g_mirrorRequested) return false;
        ScriptCallEntry e{};
        e.netId = netId; e.objectId = objectId; e.table = 255; e.index = index; e.effect = effect;
        for (std::size_t k = 0; k < 4; ++k) e.args[k] = args[k];
        const auto added = g_emissionLedger.AddScriptActor(e);
        // An end before adoption abandons the build; an adopted child follows its own manifest instead.
        if (index == SCRIPT_ACTOR_BIRTH_END)
            for (auto& c : g_scriptChildren) if (c.birth.effect == effect && !c.adopted) c.retired = true;
        using A = SpawnEmissionLedger::ScriptActorAdd;
        if (added != A::Duplicate)
            ScriptChildLog("[script-birth] client receipt op=%u parent=%u child=%u occurrence=%llu result=%s frame=%u", index,
                netId, effect, static_cast<unsigned long long>(ScriptBirthOccurrence(e)),
                added == A::Added ? "added" : added == A::Ended ? "ended" : added == A::Conflict ? "conflict" : "refused",
                g_hitTraceFrame);
        return added != A::Refused && added != A::Conflict;
    } catch (...) {
        return false;
    }
}

// A native room load completed since the holds began: its teardown disposed every body in the old room. The
// only release.
void SyncScriptChildHoldRoom() noexcept {
    if (!g_heldScriptChildCount && !g_scriptChildRoomBlocked) return;
    // Only a completed load (CompleteLoad): a transition request or a pending load still has the old room's bodies.
    if (warp::LoadSerial() == g_holdLoad) return;
    if (g_log) g_log("[script-birth] client HOLD released count=%zu blocked=%d reason=room-teardown frame=%u",
                     g_heldScriptChildCount, g_scriptChildRoomBlocked ? 1 : 0, g_hitTraceFrame);
    g_heldScriptChildren = {};
    g_heldScriptChildCount = 0;
    g_scriptChildRoomBlocked = false;
}
bool ScriptChildHeldImpl(uintptr_t actor) noexcept {
    if (!actor) return false;
    SyncScriptChildHoldRoom();
    for (std::size_t i = 0; i < g_heldScriptChildCount; ++i) if (g_heldScriptChildren[i] == actor) return true;
    return false;
}
bool ScriptChildOwnedImpl(uintptr_t actor) noexcept {
    if (!actor) return false;
    if (ScriptChildHeldImpl(actor)) return true; // whatever the role or session
    if (g_role != Role::Client) return false;
    for (const auto& c : g_scriptChildren) if (c.actor == actor) return true;
    return false;
}
// Last resort (lead #268): never updated, so it has no VM, status or census row (never a mirror, claim or hit
// target); hidden with the game's own obj_hide (type-4 mob +0x80: actor+0x124 |= 0x300, flags only); the room
// teardown disposes it natively, which skips the null +0x5B0 thread. Logged every time, outside the log budget.
void HoldScriptChildImpl(uintptr_t actor) noexcept {
    if (!actor) return;
    SyncScriptChildHoldRoom();
    if (!g_heldScriptChildCount && !g_scriptChildRoomBlocked) g_holdLoad = warp::LoadSerial();
    g_scriptChildRoomBlocked = true; // no further script child is built in this room
    for (auto& c : g_scriptChildren) if (c.actor == actor) { c.held = true; c.retired = true; } // never adopted, streamed or driven
    bool already = false;
    for (std::size_t i = 0; i < g_heldScriptChildCount; ++i) already = already || g_heldScriptChildren[i] == actor;
    const bool stored = already || g_heldScriptChildCount < g_heldScriptChildren.size();
    if (!already && stored) g_heldScriptChildren[g_heldScriptChildCount++] = actor;
    const bool hidden = !already && g_scriptChildHide && g_scriptChildHide(actor);
    if (g_log && !already)
        g_log("[script-birth] client HOLD actor=%llX reason=brain-hook stored=%d hidden=%d held=%zu frame=%u (quarantined until room teardown; no more children this room)",
              static_cast<unsigned long long>(actor), stored ? 1 : 0, hidden ? 1 : 0, g_heldScriptChildCount, g_hitTraceFrame);
}


// The one native creation call for a script child, POD only so SEH is allowed. Game thread.
void* CallScriptChildFactory(const float* point4, float yaw, bool& fault) noexcept {
    fault = false;
    __try {
        return g_scriptChildFactory(enemymirror::kCreeperObjectId, point4, yaw);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        fault = true;
        return nullptr;
    }
}

// The census row listing this actor this frame, or -1. Presence decides, not byActor: TrackSpawns keeps
// absent addresses indexed.
int ScriptChildRow(uintptr_t actor) noexcept {
    const auto found = g_inst.byActor.find(actor);
    return found != g_inst.byActor.end() && found->second < g_inst.spawns.size() &&
        g_inst.spawns[found->second].present && g_inst.spawns[found->second].actor == actor ? static_cast<int>(found->second) : -1;
}
// Still the body this record built: listed in the row it took when first listed.
bool ScriptChildRowCurrent(const ScriptChild& c) noexcept { return c.row >= 0 && ScriptChildRow(c.actor) == c.row; }

// Client owner frame, last in the client branch: no census or container reference is held across the
// native call, and the next frame's fresh census is what the binder sees.
void TickScriptChildBirths() {
    namespace ob = ordinarybinding;
    using Out = spawncontroller::ScriptChildNativeOutcome;
    SyncScriptChildHoldRoom();
    if (g_role != Role::Client || !g_scriptChildFactory || !spawncontroller::ScriptChildNativeReady()) return;
    // Constructed children the binder never took stop being candidates (their brain stays suppressed).
    for (auto& c : g_scriptChildren)
        if (!c.adopted && !c.retired && g_hitTraceFrame - c.returnFrame > kScriptChildAdoptFrames) {
            c.retired = true;
            ScriptChildLog("[script-birth] client unadopted child=%u actor=%llX frame=%u", c.birth.effect,
                static_cast<unsigned long long>(c.actor), g_hitTraceFrame);
        }
    // Each record takes the census row its body gets when first listed (same objentry, a 317). A different row
    // at that address later, or a first row with other roots, is a replacement body that was never ours.
    for (auto& c : g_scriptChildren) {
        const int row = ScriptChildRow(c.actor);
        if (row < 0 || c.held) continue;
        const auto& s = g_inst.spawns[static_cast<std::size_t>(row)];
        if (c.row < 0 && s.objentry == c.objentry && s.objectId == enemymirror::kCreeperObjectId) c.row = row;
        else if (c.row != row) {
            ScriptChildLog("[script-birth] client replaced child=%u actor=%llX row=%d->%d frame=%u", c.birth.effect,
                static_cast<unsigned long long>(c.actor), c.row, row, g_hitTraceFrame);
            c.actor = 0; // erased below; the body at that address keeps its own (non-script) ownership
        }
    }
    // A record whose body this frame's census does not list is gone (native death/dispose or teardown). A new
    // child enters the census only after its first update gives it a status, so an unadopted one gets a grace
    // period. A held child's quarantine is its address in the hold array, not this record.
    std::erase_if(g_scriptChildren, [](const ScriptChild& c) {
        if (!c.actor) return true;
        return ScriptChildRow(c.actor) < 0 && (c.adopted || g_hitTraceFrame - c.returnFrame > kScriptChildCensusFrames);
    });
    ob::Scope scope;
    ScriptCallEntry pending{};
    if (!ScriptActorConsumerScope(scope) || !g_emissionLedger.PendingScriptActor(pending)) return;
    const auto parent = g_host.enemies.find(pending.netId);
    const auto child = g_host.enemies.find(static_cast<std::uint16_t>(pending.effect));
    // A new birth needs the still-admitted parent; the child's own manifest says where and whether it lives.
    if (parent == g_host.enemies.end() || parent->second.objectId != SCRIPT_ACTOR_BIRTH_PARENT_OBJECT ||
        parent->second.dead || parent->second.removed || parent->second.battleProgram != g_inst.btl) return;
    if (child == g_host.enemies.end()) return; // its manifest has not arrived yet
    if (child->second.objectId != SCRIPT_ACTOR_BIRTH_CHILD_OBJECT || child->second.battleProgram != g_inst.btl ||
        child->second.dead || child->second.removed || child->second.left) {
        (void)g_emissionLedger.AddScriptActor(ScriptBirthEnd(pending)); // nothing live to build
        ScriptChildLog("[script-birth] client skip child=%u reason=manifest-not-live frame=%u", pending.effect, g_hitTraceFrame);
        return;
    }
    if (g_scriptChildren.size() >= SPAWN_EMISSION_CAP) return;
    for (const auto& s : g_inst.spawns)
        if (s.present && s.netId == static_cast<int>(pending.effect)) return; // already bound to a local body
    // After any hold in this room nothing more is built. Otherwise every owned, unheld child must still have a
    // free hold slot, so a later refusal can always be held (the array never allocates and never releases early).
    std::size_t unheld = 0;
    for (const auto& c : g_scriptChildren) unheld += c.held ? 0 : 1;
    if (g_scriptChildRoomBlocked || unheld + g_heldScriptChildCount + 1 > kScriptChildHoldCap) {
        ScriptChildLog("[script-birth] client wait child=%u reason=%s frame=%u", pending.effect,
            g_scriptChildRoomBlocked ? "room-held" : "hold-capacity", g_hitTraceFrame);
        return;
    }
    // Option C needs the class brain hooked before any child exists: without it, build nothing (retried later).
    if (!g_scriptChildBrainGate || !g_scriptChildBrainGate(0)) {
        ScriptChildLog("[script-birth] client wait child=%u reason=brain-hook frame=%u", pending.effect, g_hitTraceFrame);
        return;
    }
    spawncontroller::TraceStamp stamp;
    if (!spawncontroller::CaptureDiagnosticStamp(g_exeBase, stamp)) return;
    SpawnEmissionLedger::ScriptActorReservation token;
    if (!g_emissionLedger.ReserveScriptActor(pending, token)) return;
    spawncontroller::ScriptChildResourceLease lease{};
    const auto acquired = spawncontroller::AcquireScriptChildResources(stamp, lease);
    if (acquired != Out::Ready) {
        // Refused: nothing was acquired and no factory ran; the receipt may try again later.
        if (acquired == Out::Refused) (void)g_emissionLedger.CancelScriptActor(token, SpawnEmissionLedger::ScriptActorCancel::QualifiedNormalNull);
        else (void)g_emissionLedger.CommitScriptActor(token);
        ScriptChildLog("[script-birth] client resources child=%u outcome=%u frame=%u", pending.effect,
            static_cast<unsigned>(acquired), g_hitTraceFrame);
        return;
    }
    ob::Scope again;
    void* made = nullptr;
    bool fault = false, called = false;
    if (spawncontroller::ScriptChildResourcesCurrent(lease) && ScriptActorConsumerScope(again) &&
        again.scriptActor == scope.scriptActor) {
        const float point[4] = {child->second.spawnPos.x, child->second.spawnPos.y, child->second.spawnPos.z, 1.0f};
        called = true;
        made = CallScriptChildFactory(point, 0.0f, fault);
    }
    const auto released = spawncontroller::ReleaseScriptChildResources(lease);
    const auto actor = reinterpret_cast<uintptr_t>(made);
    if (!called) {
        (void)g_emissionLedger.CancelScriptActor(token, SpawnEmissionLedger::ScriptActorCancel::QualifiedNormalNull);
        return;
    }
    if (actor) {
        // Every constructed body is tracked, whatever the lease release reported: an untracked child would run
        // its native brain on this client. A release Fault/Ambiguous concerns only our resource references.
        (void)g_emissionLedger.CommitScriptActor(token);
        g_scriptChildNullRetries = 0;
        // A native return at an earlier record's address is a new body: retire the old record first.
        for (auto& c : g_scriptChildren) if (c.actor == actor) c.retired = true;
        std::erase_if(g_scriptChildren, [actor](const ScriptChild& c) { return c.actor == actor; });
        ScriptChild record{};
        record.birth = pending; record.returnSerial = ++g_scriptReturnSerial; record.actor = actor;
        (void)ReadNative(actor + offsets::actor::OBJENTRY_PTR, record.objentry);
        record.returnFrame = g_hitTraceFrame; record.scope = scope.scriptActor;
        g_scriptChildren.push_back(record);
        // Its own handler must already be hooked before its first update; otherwise it is held, never run.
        if (!g_scriptChildBrainGate(actor)) HoldScriptChildImpl(actor);
    } else if (fault || released == Out::Fault || released == Out::Ambiguous) {
        (void)g_emissionLedger.CommitScriptActor(token); // never retry an outcome that may have created a body
    } else {
        // The factory's null returns precede construction (entry lookup or allocation): bounded retry.
        if (++g_scriptChildNullRetries < kScriptChildNullRetries)
            (void)g_emissionLedger.CancelScriptActor(token, SpawnEmissionLedger::ScriptActorCancel::QualifiedNormalNull);
        else (void)g_emissionLedger.CommitScriptActor(token);
    }
    ScriptChildLog("[script-birth] client build parent=%u child=%u occurrence=%llu actor=%llX fault=%u release=%u at=(%.0f,%.0f,%.0f) frame=%u",
        pending.netId, pending.effect, static_cast<unsigned long long>(ScriptBirthOccurrence(pending)),
        static_cast<unsigned long long>(actor), fault ? 1u : 0u, static_cast<unsigned>(released),
        child->second.spawnPos.x, child->second.spawnPos.y, child->second.spawnPos.z, g_hitTraceFrame);
}

// ---- witness provider for the ordinary binder (adds-owner24 API) -------------------------------------
// Copies only; never constructs, refreshes session state or touches census/spawn containers.
bool QueryScriptActorWitness(const ScriptActorQuery& q, ordinarybinding::ScriptActorWitness& out) noexcept {
    out = {};
    if (g_role != Role::Client) return false;
    for (const auto& c : g_scriptChildren) {
        if (c.retired || c.scope != q.scope.scriptActor) continue;
        const int childId = static_cast<int>(c.birth.effect);
        if (q.use == ScriptActorUse::Stream ? childId != q.childId : (c.actor != q.actor || (q.childId && childId != q.childId)))
            continue;
        // A native witness is only for the body this record built: its own pinned census row, still listed.
        if (q.use != ScriptActorUse::Stream && !ScriptChildRowCurrent(c)) return false;
        ordinarybinding::Identity roots{};
        if (q.use != ScriptActorUse::Stream && !ScriptChildRoots(c.actor, roots)) return false;
        out = {ScriptBirthOccurrence(c.birth), c.returnSerial, childId, static_cast<int>(c.birth.netId), roots, c.scope};
        return true;
    }
    return false;
}
bool AdoptScriptActorWitness(const ordinarybinding::ScriptActorWitness& w, ordinarybinding::Scope) noexcept {
    for (auto& c : g_scriptChildren)
        if (!c.retired && c.actor == w.child.actor && c.returnSerial == w.nativeReturnSerial &&
            static_cast<int>(c.birth.effect) == w.childId && ScriptChildRowCurrent(c)) {
            if (!c.adopted)
                ScriptChildLog("[script-birth] client adopted child=%u actor=%llX frame=%u", c.birth.effect,
                    static_cast<unsigned long long>(c.actor), g_hitTraceFrame);
            c.adopted = true;
            return true;
        }
    return false;
}

// ---- consumer adapters (adds-owner24) ----------------------------------------------------------------
bool ScriptActorProof(const ScriptActorQuery& query, ordinarybinding::ScriptActorWitness& out) {
    namespace ob = ordinarybinding;
    out = {};
    ob::Scope now;
    if (query.objectId != enemymirror::kCreeperObjectId || !ScriptActorConsumerScope(now) ||
        now.generation != query.scope.generation || now.epoch != query.scope.epoch || now.frame != query.scope.frame ||
        now.scriptActor != query.scope.scriptActor) return false;
    ob::ScriptActorWitness witness;
    if (!QueryScriptActorWitness(query, witness) || !witness.occurrence || witness.childId <= 0 ||
        witness.childId > 65535 || witness.parentId <= 0 || witness.parentId > 65535 ||
        witness.parentId == witness.childId || (query.use != ScriptActorUse::Stream && witness.child.objectId != query.objectId) ||
        (query.childId && query.childId != witness.childId) || witness.scope != query.scope.scriptActor ||
        (ob::HasScriptActor(query.adopted) && query.adopted != witness)) return false;
    if (query.use != ScriptActorUse::Stream) {
        ob::Local checked;
        checked.identity = witness.child; checked.scriptActor = witness; checked.scriptActorCurrent = true;
        if (!query.actor || witness.child.actor != query.actor || !ob::ScriptActorCurrent(checked, query.scope)) return false;
    } else if (query.actor || !query.childId) {
        return false;
    }
    out = witness;
    return true;
}
// Per-entry stream buffering for one exact child: grants no actor or write rights by itself.
bool ScriptActorStreamPermit(std::uint16_t id, enemymirror::ScriptActorStreamPermit& out) {
    out = {};
    ordinarybinding::Scope scope;
    if (!id || g_role != Role::Client || !ScriptActorConsumerScope(scope)) return false;
    const auto host = g_host.enemies.find(id);
    if (host == g_host.enemies.end() || host->second.objectId != enemymirror::kCreeperObjectId ||
        host->second.battleProgram != g_inst.btl || host->second.dead || host->second.removed) return false;
    ordinarybinding::ScriptActorWitness witness;
    if (!ScriptActorProof({ScriptActorUse::Stream, scope, 0, id, enemymirror::kCreeperObjectId, {}}, witness)) return false;
    out = {scope.epoch, id, enemymirror::kCreeperObjectId, witness.occurrence};
    return true;
}
// Host: publish EnemyMotion for a 317 only while ScriptMirror still announces it as this exact child.
bool ScriptActorHostMotion(uintptr_t actor, int id) {
    std::uint64_t occurrence = 0;
    std::uint16_t parent = 0;
    return g_role == Role::Host && g_scriptBirthHostQuery && id > 0 && id <= 65535 &&
        g_scriptBirthHostQuery(actor, static_cast<std::uint16_t>(id), occurrence, parent) && occurrence && parent;
}
bool ScriptActorAuthority(uintptr_t actor, bool terminal) {
    namespace ob = ordinarybinding;
    ob::Scope scope;
    if (g_role != Role::Client || !ScriptActorConsumerScope(scope)) return false;
    const auto found = g_inst.byActor.find(actor);
    if (found == g_inst.byActor.end() || found->second >= g_inst.spawns.size()) return false;
    const auto& spawn = g_inst.spawns[found->second];
    const auto state = spawn.ordinaryBinding;
    if (!spawn.present || spawn.objectId != enemymirror::kCreeperObjectId || spawn.netId != state.bound ||
        !ob::HasScriptActor(state.scriptActor) || state.bound <= 0 || state.bound > 65535) return false;
    const auto host = g_host.enemies.find(static_cast<std::uint16_t>(state.bound));
    if (host == g_host.enemies.end() || host->second.objectId != enemymirror::kCreeperObjectId ||
        host->second.battleProgram != g_inst.btl || host->second.removed || host->second.dead != terminal) return false;
    const auto census = CaptureNativeCensus(actor);
    if (!CensusMatchesInstance(census) || !census.watchedActorPresent) return false;
    for (const auto& native : census.enemies)
        if (native.actor == actor) {
            ob::Local local;
            if (!ReadOrdinaryIdentity(native, local.identity)) return false;
            local.living = native.hp > 0;
            local.scriptActorCurrent = ScriptActorProof({terminal ? ScriptActorUse::Terminal : ScriptActorUse::Living,
                scope, actor, state.bound, native.objectId, state.scriptActor}, local.scriptActor);
            return ob::ScriptActorBoundCurrent(state, local, scope, terminal, host->second.dead);
        }
    return false;
}
