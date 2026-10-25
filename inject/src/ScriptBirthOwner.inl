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
    std::uint32_t returnFrame{};
    ordinarybinding::ScriptActorScope scope{};     // full scope captured at the native return
    bool adopted{}, retired{};
};
std::vector<ScriptChild> g_scriptChildren;
std::uint64_t g_scriptReturnSerial = 0;           // never rewinds
EnemyFactoryFn g_scriptChildFactory = nullptr;    // 0x3DF930, prologue byte-checked at install; scoped to this path
unsigned g_scriptChildLogs = 0;
std::uint8_t g_scriptChildNullRetries = 0;
ScriptBirthHostQuery g_scriptBirthHostQuery = nullptr;
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
    // The room teardown owns every native body; only our records go.
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

bool ScriptChildOwnedImpl(uintptr_t actor) noexcept {
    if (!actor || g_role != Role::Client) return false;
    for (const auto& c : g_scriptChildren) if (c.actor == actor) return true;
    return false;
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

// Client owner frame, last in the client branch: no census or container reference is held across the
// native call, and the next frame's fresh census is what the binder sees.
void TickScriptChildBirths() {
    namespace ob = ordinarybinding;
    using Out = spawncontroller::ScriptChildNativeOutcome;
    if (g_role != Role::Client || !g_scriptChildFactory || !spawncontroller::ScriptChildNativeReady()) return;
    // Constructed children the binder never took stop being candidates (their brain stays suppressed).
    for (auto& c : g_scriptChildren)
        if (!c.adopted && !c.retired && g_hitTraceFrame - c.returnFrame > kScriptChildAdoptFrames) {
            c.retired = true;
            ScriptChildLog("[script-birth] client unadopted child=%u actor=%llX frame=%u", c.birth.effect,
                static_cast<unsigned long long>(c.actor), g_hitTraceFrame);
        }
    // A record whose body the census no longer tracks is gone (native death/dispose or teardown). A new child
    // enters the census only after its first update gives it a status, so an unadopted one gets a grace period.
    std::erase_if(g_scriptChildren, [](const ScriptChild& c) {
        return !g_inst.byActor.contains(c.actor) && (c.adopted || g_hitTraceFrame - c.returnFrame > kScriptChildCensusFrames);
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
    if (fault || released == Out::Fault || released == Out::Ambiguous) {
        (void)g_emissionLedger.CommitScriptActor(token); // never retry an outcome that may have created a body
    } else if (!actor) {
        // The factory's null returns precede construction (entry lookup or allocation): bounded retry.
        if (++g_scriptChildNullRetries < kScriptChildNullRetries)
            (void)g_emissionLedger.CancelScriptActor(token, SpawnEmissionLedger::ScriptActorCancel::QualifiedNormalNull);
        else (void)g_emissionLedger.CommitScriptActor(token);
    } else {
        (void)g_emissionLedger.CommitScriptActor(token);
        g_scriptChildNullRetries = 0;
        // A native return at an earlier record's address is a new body: retire the old record first.
        for (auto& c : g_scriptChildren) if (c.actor == actor) c.retired = true;
        std::erase_if(g_scriptChildren, [actor](const ScriptChild& c) { return c.actor == actor; });
        g_scriptChildren.push_back({pending, ++g_scriptReturnSerial, actor, g_hitTraceFrame, scope.scriptActor, false, false});
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
            static_cast<int>(c.birth.effect) == w.childId) {
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
