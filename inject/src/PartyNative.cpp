// PartyNative — see PartyNative.hpp (VUH-1519).
#include "PartyNativePolicy.hpp"

#include <atomic>
#include <cstring>

#ifndef KH2COOP_PARTYNATIVE_POLICY_ONLY
#include "NativePrivateStatus.hpp"
#include "PlayerKit.hpp"
#include <Windows.h>
#include <atomic>
#endif

namespace kh2coop::inject::partynative {

// ---------------------------------------------------------------- policy
const char* PlanName(Plan p) {
    switch (p) {
    case Plan::None: return "none";
    case Plan::Unsupported: return "unsupported";
    case Plan::TwoClones: return "two-clones";
    }
    return "?";
}

const char* AcceptName(Accept a) {
    switch (a) {
    case Accept::Accepted: return "accepted";
    case Accept::NoGeneration: return "no-generation";
    case Accept::RosterMismatch: return "roster-mismatch";
    case Accept::LocalSlot: return "local-slot";
    case Accept::StaleVersion: return "stale-version";
    case Accept::Invalid: return "invalid";
    }
    return "?";
}

const char* ReapplyName(Reapply r) {
    switch (r) {
    case Reapply::Cleared: return "cleared";
    case Reapply::LoggedOnly: return "logged-only";
    case Reapply::OtherGeneration: return "other-generation";
    }
    return "?";
}

const char* LoadName(Load r) {
    switch (r) {
    case Load::Applied: return "applied";
    case Load::NoIntent: return "no-intent";
    case Load::Unsupported: return "unsupported-layout";
    case Load::WorldNotQualified: return "world-not-qualified";
    case Load::EventRoom: return "event-room";
    case Load::EventActive: return "event-active";
    case Load::IntentOtherRoom: return "intent-other-room";
    case Load::RowNotDefault: return "row-not-default";
    case Load::NativeNotDefault: return "native-not-default";
    case Load::ChangedUnderUs: return "changed-under-us";
    case Load::ReadFault: return "read-fault";
    case Load::PrivateStatusUnavailable: return "private-status-unavailable";
    case Load::NeutralInputUnavailable: return "neutral-input-unavailable";
    }
    return "?";
}

const char* PlanSourceName(PlanSource s) {
    switch (s) {
    case PlanSource::None: return "none";
    case PlanSource::Layout: return "layout";
    case PlanSource::Intent: return "intent";
    case PlanSource::Hold: return "hold";
    }
    return "?";
}

bool QualifiedRoom(std::uint16_t world, std::uint16_t room, std::uint16_t evt) {
    return world == 4 && evt == 0 && (room == 0x1A || room == 0x0A);
}

Plan Project(const PartyLayout& layout, std::uint8_t localSlot) {
    if (localSlot > 2 || !layout.connections[localSlot]) return Plan::Unsupported;
    if (!validPartyLayout(layout, layout.connections)) return Plan::Unsupported;
    if (layout.rule != PartyRule::Default) return Plan::Unsupported;
    for (const auto c : layout.connections) if (!c) return Plan::Unsupported; // three players only
    // Host view: seat0 = network slot0, seats1/2 = the two remote network slots.
    // With every seat a player, each machine's two native friend seats hold the
    // two network slots other than its own; no seat needs Donald/Goofy/empty.
    if (layout.seats[1].kind != PartyMemberKind::RemotePlayer ||
        layout.seats[2].kind != PartyMemberKind::RemotePlayer) return Plan::Unsupported;
    return Plan::TwoClones;
}

bool HasPlans(const Intent& intent) {
    return intent.plan != Plan::None || intent.hold.valid || IntentCount(intent) != 0;
}

unsigned IntentCount(const Intent& intent) {
    unsigned n = 0;
    for (const auto& t : intent.targets) n += t.valid ? 1u : 0u;
    return n;
}

bool Rebase(Intent& intent, std::uint32_t generation, const std::array<std::uint64_t, 3>& roster, std::uint8_t localSlot) {
    if (intent.generation == generation && intent.roster == roster && intent.localSlot == localSlot) return false;
    const bool had = HasPlans(intent);
    // Versions are one namespace per host connection within a generation.
    const bool keepFloor = intent.generation == generation && intent.roster[0] == roster[0];
    const auto floor = intent.version, intentFloor = intent.intentVersion, seq = intent.seq;
    intent = {};
    intent.generation = generation; intent.roster = roster; intent.localSlot = localSlot; intent.seq = seq;
    if (keepFloor) { intent.version = floor; intent.intentVersion = intentFloor; }
    return had;
}

bool HoldCovers(const Intent& intent, std::uint16_t world, std::uint16_t room) {
    return intent.hold.valid && (intent.hold.world & 0xFF) == (world & 0xFF) && (intent.hold.room & 0xFF) == (room & 0xFF);
}

bool ReleaseHold(Intent& intent, std::uint64_t consumedSeq) {
    if (!intent.hold.valid || consumedSeq != intent.hold.seq) return false;
    intent.hold = {};
    return true;
}

bool StoryVisitCovers(const StoryVisit& v, std::uint16_t world, std::uint16_t room) {
    return v.valid && (v.world & 0xFF) == (world & 0xFF) && (v.room & 0xFF) == (room & 0xFF);
}

bool StorySuppresses(StoryVisit& v, const Intent& intent, const RoomTransition& l) {
    if (HoldCovers(intent, l.worldId, l.roomId)) return true; // the hold has not had its load yet
    if (!v.valid) return false;
    if (StoryVisitCovers(v, l.worldId, l.roomId)) {
        if (v.epoch == 0) { v.epoch = l.epoch; return true; } // the held visit's own tuple
        if (l.epoch == v.epoch) return true;
    }
    v = {}; // a new epoch of the room, or another room: the visit is over
    return false;
}

bool IntentEchoConfirms(const Intent& intent, Accept a, const PartyIntentTarget& target, std::uint64_t sentVersion) {
    if (a != Accept::Accepted && a != Accept::StaleVersion) return false;
    for (const auto& t : intent.targets)
        if (t.valid && t.target == target) return t.version == sentVersion;
    return false;
}

bool EchoConfirms(Accept a, std::uint64_t echoedVersion, std::uint64_t sentVersion) {
    return a == Accept::Accepted || (a == Accept::StaleVersion && echoedVersion == sentVersion);
}

Accept AcceptLayout(Intent& intent, const PartyLayout& layout, std::uint8_t localSlot,
                    std::uint32_t generation, const std::array<std::uint64_t, 3>& roster) {
    if (!generation) return Accept::NoGeneration;
    if (localSlot > 2 || !roster[localSlot]) return Accept::LocalSlot;
    if (layout.connections != roster) return Accept::RosterMismatch;
    if (!validPartyLayout(layout, roster)) return Accept::Invalid;
    Rebase(intent, generation, roster, localSlot); // S4: nothing pinned to another roster survives
    if (layout.version <= intent.version) return Accept::StaleVersion;
    intent.version = layout.version;
    intent.plan = Project(layout, localSlot);
    intent.world = layout.location.worldId;
    intent.room = layout.location.roomId;
    intent.evt = layout.location.eventProgram;
    intent.layoutSeq = ++intent.seq;
    if (HoldCovers(intent, intent.world, intent.room)) intent.hold = {}; // B1: a newer layout supersedes the hold
    return Accept::Accepted;
}

Plan ProjectIntent(const PartyIntent& m, std::uint8_t localSlot) {
    PartyLayout probe {};
    probe.location.epoch = 1; probe.location.worldId = m.target.worldId; probe.location.roomId = m.target.roomId;
    probe.location.eventProgram = m.target.eventProgram;
    probe.version = m.version; probe.connections = m.connections; probe.rule = m.rule; probe.seats = m.seats;
    for (unsigned i = 1; i < 3; ++i)
        if (m.seats[i].kind == PartyMemberKind::RemotePlayer && m.kits[i] != SORA) return Plan::Unsupported; // Sora-only kits
    return Project(probe, localSlot);
}

Accept AcceptIntent(Intent& intent, const PartyIntent& m, std::uint8_t localSlot,
                    std::uint32_t generation, const std::array<std::uint64_t, 3>& roster) {
    if (!generation) return Accept::NoGeneration;
    if (localSlot > 2 || !roster[localSlot]) return Accept::LocalSlot;
    if (m.connections != roster) return Accept::RosterMismatch;
    if (!validPartyIntent(m, roster)) return Accept::Invalid;
    if (!QualifiedRoom(m.target.worldId, m.target.roomId, m.target.eventProgram)) return Accept::Invalid; // N2: pinned rooms only
    Rebase(intent, generation, roster, localSlot); // S4
    if (m.version <= intent.intentVersion) return Accept::StaleVersion;
    intent.intentVersion = m.version;
    StoredIntent* slot = nullptr;
    for (auto& t : intent.targets) if (t.valid && t.target == m.target) slot = &t;
    if (!slot) for (auto& t : intent.targets) if (!t.valid) { slot = &t; break; }
    if (!slot) { // replace the oldest
        slot = &intent.targets[0];
        for (auto& t : intent.targets) if (t.seq < slot->seq) slot = &t;
    }
    *slot = {true, m.target, ProjectIntent(m, localSlot), m.version, ++intent.seq};
    if (HoldCovers(intent, m.target.worldId, m.target.roomId)) intent.hold = {}; // B1: a newer intent supersedes the hold
    return Accept::Accepted;
}

namespace {
std::uint64_t PlanKey(Plan p, std::uint16_t w, std::uint16_t r, std::uint16_t e, PlanSource s) {
    return static_cast<std::uint64_t>(p) | (static_cast<std::uint64_t>(w & 0xFF) << 8) | (static_cast<std::uint64_t>(r & 0xFF) << 16) |
        (static_cast<std::uint64_t>(e) << 24) | (static_cast<std::uint64_t>(s) << 40);
}
} // namespace

void PackPlanTable(const Intent& intent, PlanTable& out) {
    out = {};
    if (intent.plan != Plan::None) out[0] = {PlanKey(intent.plan, intent.world, intent.room, intent.evt, PlanSource::Layout), intent.layoutSeq};
    if (intent.hold.valid) out[1] = {PlanKey(Plan::None, intent.hold.world, intent.hold.room, intent.hold.evt, PlanSource::Hold), intent.hold.seq};
    for (unsigned i = 0; i < MAX_INTENT_TARGETS; ++i) {
        const auto& t = intent.targets[i];
        if (t.valid && t.plan != Plan::None)
            out[2 + i] = {PlanKey(t.plan, t.target.worldId, t.target.roomId, t.target.eventProgram, PlanSource::Intent), t.seq};
    }
}

PlanChoice SelectPlan(const PlanTable& table, std::uint16_t world, std::uint16_t room, std::uint16_t evt) {
    const auto decode = [](const PlanEntry& v) {
        const auto source = static_cast<PlanSource>((v.key >> 40) & 0xFF);
        return PlanChoice {source == PlanSource::Hold ? Plan::None : static_cast<Plan>(v.key & 0xFF),
                           static_cast<std::uint16_t>((v.key >> 8) & 0xFF), static_cast<std::uint16_t>((v.key >> 16) & 0xFF),
                           static_cast<std::uint16_t>((v.key >> 24) & 0xFFFF), source, v.seq};
    };
    PlanChoice best; bool found = false;
    for (const auto& v : table) {
        const PlanChoice c = decode(v);
        if (c.source == PlanSource::None) continue;
        if (c.world != (world & 0xFF) || c.room != (room & 0xFF)) continue;
        if (c.source != PlanSource::Hold && c.evt != evt) continue; // a hold covers its room at any evt
        if (!found || c.seq > best.seq) { best = c; found = true; }
    }
    if (found) return best;
    const PlanChoice layout = decode(table[0]);
    return layout.source == PlanSource::Layout ? layout : PlanChoice {};
}

void PlanTableCell::Publish(const PlanTable& table) {
    const auto v = version_.load(std::memory_order_relaxed);
    version_.store(v + 1, std::memory_order_relaxed); // odd: write in progress
    std::atomic_thread_fence(std::memory_order_release);
    for (unsigned i = 0; i < PLAN_TABLE; ++i) {
        words_[2 * i].store(table[i].key, std::memory_order_relaxed);
        words_[2 * i + 1].store(table[i].seq, std::memory_order_relaxed);
    }
    version_.store(v + 2, std::memory_order_release);
}

bool PlanTableCell::Read(PlanTable& out, unsigned maxTries) const {
    for (unsigned k = 0; k < maxTries; ++k) {
        const auto v1 = version_.load(std::memory_order_acquire);
        if (v1 & 1u) continue;
        PlanTable t {};
        for (unsigned i = 0; i < PLAN_TABLE; ++i) {
            t[i].key = words_[2 * i].load(std::memory_order_relaxed);
            t[i].seq = words_[2 * i + 1].load(std::memory_order_relaxed);
        }
        std::atomic_thread_fence(std::memory_order_acquire);
        if (version_.load(std::memory_order_relaxed) == v1) { out = t; return true; }
    }
    out = {};
    return false;
}

bool ObserveSession(Intent& intent, std::uint32_t generation, const std::array<std::uint64_t, 3>& roster,
                    std::uint8_t localSlot, bool bridgeOpen) {
    if (intent.generation == 0 && !HasPlans(intent)) return false;
    if (!bridgeOpen) {
        const bool had = HasPlans(intent);
        const auto seq = intent.seq;
        intent = {}; intent.seq = seq;
        return had;
    }
    if (generation == 0) return false; // unknown (ordering/delivery flicker): hold
    return Rebase(intent, generation, roster, localSlot);
}

Reapply ApplyReapply(Intent& intent, const PartyReapply& reapply, std::uint32_t generation) {
    if (!generation || (intent.generation != 0 && intent.generation != generation)) return Reapply::OtherGeneration;
    if (reapply.reason != PartyApplyReason::StoryForced && reapply.reason != PartyApplyReason::RosterChanged)
        return Reapply::LoggedOnly;
    intent.plan = Plan::None;
    intent.world = intent.room = intent.evt = 0xFFFF;
    if (intent.generation == generation && reapply.afterVersion > intent.version) intent.version = reapply.afterVersion;
    if (reapply.reason == PartyApplyReason::RosterChanged) { // roster-pinned: retired
        intent.targets = {};
        intent.hold = {};
    } else { // B1: the story-forced party wins the next load of its room; the intents stay for later loads
        intent.hold = {true, reapply.location.worldId, reapply.location.roomId, reapply.location.eventProgram, ++intent.seq};
    }
    return Reapply::Cleared;
}

Load Decide(const LoadInput& in) {
    if (in.plan == Plan::None) return Load::NoIntent;
    if (in.plan != Plan::TwoClones) return Load::Unsupported;
    if (!QualifiedRoom(in.world, in.room, 0)) return Load::WorldNotQualified; // pinned rooms only (GoA 04/1A, 04/0A); evt next
    if (in.evtProgram != 0) return Load::EventRoom;
    if (in.eventContext != 0 || in.cutsceneState != 0) return Load::EventActive;
    // The intent is pinned to the room its layout was authored for (lead decision).
    if (in.world != in.intentWorld || in.room != in.intentRoom || in.evtProgram != in.intentEvt)
        return Load::IntentOtherRoom;
    // Never spawn clones that would share Sora's SAVE-bound record.
    if (!in.privateStatusReady) return Load::PrivateStatusUnavailable;
    // R1: never spawn clones that would execute the local player's FIELD_COMMAND records.
    if (!in.neutralInputReady) return Load::NeutralInputUnavailable;
    if (!in.rowRead) return Load::ReadFault;
    if (in.row != DEFAULT_ROW) return Load::RowNotDefault; // NO_FRIEND / guest / forced rows: native
    if (in.resolved[0] != SORA || in.resolved[1] != DONALD || in.resolved[2] != GOOFY) return Load::NativeNotDefault;
    return Load::Applied;
}

Load ApplyAfterResolve(const LoadInput& in, std::uint16_t* resolved, Originals& original) {
    // 3E2EB0 has just rewritten the array from MEMT: values recorded at an
    // earlier load are gone whatever this load decides.
    original = {};
    const Load r = Decide(in);
    if (r != Load::Applied) return r;
    if (!resolved) return Load::ReadFault;
    if (resolved[0] != in.resolved[0] || resolved[1] != in.resolved[1] || resolved[2] != in.resolved[2])
        return Load::ChangedUnderUs; // never guess
    original = {true, resolved[1], resolved[2]};
    resolved[1] = SORA;
    resolved[2] = SORA;
    return r;
}

void RestoreMembers(Originals& original, std::uint16_t* resolved) {
    if (!original.valid || !resolved) { original = {}; return; }
    if (resolved[1] == SORA) resolved[1] = original.member1;
    if (resolved[2] == SORA) resolved[2] = original.member2;
    original = {};
}

PartyRule PinnedRule(const RoomTransition& l) {
    return QualifiedRoom(l.worldId, l.roomId, l.eventProgram) ? PartyRule::Default : PartyRule::Unavailable;
}

namespace {
bool SameTuple(const RoomTransition& a, const RoomTransition& b) {
    return a.epoch == b.epoch && a.worldId == b.worldId && a.roomId == b.roomId && a.door == b.door &&
        a.mapProgram == b.mapProgram && a.battleProgram == b.battleProgram && a.eventProgram == b.eventProgram;
}
} // namespace

bool HostShouldPublish(const HostPublished& last, std::uint32_t generation, const RoomTransition& location,
                       const std::array<std::uint64_t, 3>& roster, bool rowRead,
                       const std::array<std::uint8_t, 4>& row, std::uint64_t nowMs, PartyApplyReason& reason) {
    if (!generation || !location.epoch || PinnedRule(location) != PartyRule::Default) return false; // unknown rooms refused
    for (const auto c : roster) if (!c) return false;
    if (!rowRead || row != DEFAULT_ROW) return false;
    const bool same = last.any && last.generation == generation && SameTuple(last.location, location) && last.roster == roster;
    if (same) {
        if (last.echoed || last.resends >= HOST_ECHO_MAX_RESENDS || nowMs - last.sentMs < HOST_ECHO_RETRY_MS) return false;
        reason = last.reason; // not echoed by the relay: retry with a newer version
        return true;
    }
    reason = !last.any || last.generation != generation ? PartyApplyReason::HostChoice
           : last.roster != roster                       ? PartyApplyReason::RosterChanged
                                                         : PartyApplyReason::RoomChanged;
    return true;
}

bool HostIntentDue(const HostPublished& last, std::uint32_t generation, const std::array<std::uint64_t, 3>& roster,
                   std::uint64_t nowMs) {
    if (!generation) return false;
    for (const auto c : roster) if (!c) return false;
    if (!last.any || last.generation != generation || last.roster != roster) return true;
    if (last.echoed || last.resends >= HOST_ECHO_MAX_RESENDS) return false;
    return nowMs - last.sentMs >= HOST_ECHO_RETRY_MS;
}

bool HostEchoGivenUp(const HostPublished& last, std::uint32_t generation, const RoomTransition& location,
                     const std::array<std::uint64_t, 3>& roster) {
    return last.any && !last.echoed && last.resends >= HOST_ECHO_MAX_RESENDS && last.generation == generation &&
        SameTuple(last.location, location) && last.roster == roster;
}

#ifndef KH2COOP_PARTYNATIVE_POLICY_ONLY
// ---------------------------------------------------------------- native
namespace {
constexpr std::uint64_t RVA_RESOLVED = playerkit::RVA_RESOLVED_MEMBERS;
std::uintptr_t g_base = 0;
LogFn g_log = nullptr;
std::atomic<bool> g_requested {false};
ReadyProbe g_neutralProbe = nullptr; // set once by ConfirmLocalReadiness, before any load
bool g_echoGiveUpLogged = false;
bool NeutralReady() { return g_neutralProbe && g_neutralProbe(); }
// R2: the host neither publishes nor adopts unless every local prerequisite is live.
bool LocallyReady() { return g_requested.load(std::memory_order_acquire) && privatestatus::Ready() && NeutralReady(); }
// Game thread only.
Intent g_intent {};
HostPublished g_hostLast {};
std::uint64_t g_hostVersion = 0;
// Published by the game thread as one set (S3 seqlock), read by the loading thread at each resolve.
PlanTableCell g_planCell {};
PlanTable g_lastPublished {};
bool g_publishedOnce = false;
// Loading thread -> game thread: the seq of the last story hold a load used (B1 release).
std::atomic<std::uint64_t> g_holdConsumed {0};
std::atomic<std::uint32_t> g_tableMisses {0}; // reads that fell back to an empty table (logged: misses=)
StoryVisit g_storyVisit {}; // rev3 F1, game thread
bool g_storyQuietLogged = false;
// VUH-1786 host intents: one per pinned qualified room.
constexpr std::array<PartyIntentTarget, 2> INTENT_ROOMS {{{4, 0x1A, 0}, {4, 0x0A, 0}}};
std::array<HostPublished, 2> g_hostIntent {};
std::uint64_t g_hostIntentVersion = 0;
// Loading thread (hook); Shutdown runs after the hook is disabled.
Originals g_original {};
std::atomic<bool> g_applied {false};
LoadInput g_lastIn {};
Load g_lastResult = Load::NoIntent;
bool g_lastOk = false;
std::uint32_t g_loads = 0, g_appliedLoads = 0;

void PublishIntent() {
    PlanTable table {};
    PackPlanTable(g_intent, table);
    bool same = g_publishedOnce;
    for (unsigned i = 0; same && i < PLAN_TABLE; ++i) same = table[i].key == g_lastPublished[i].key && table[i].seq == g_lastPublished[i].seq;
    if (same) return; // only real changes are published (the reader retries only around a write)
    g_planCell.Publish(table);
    g_lastPublished = table; g_publishedOnce = true;
}

bool ReadBytes(std::uintptr_t p, void* out, std::size_t n) {
    __try { std::memcpy(out, reinterpret_cast<const void*>(p), n); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool ReadRow(std::uint8_t world, std::array<std::uint8_t, 4>& row) {
    std::array<char, 4> magic {};
    return world < 0x20 && ReadBytes(g_base + RVA_SAVE, magic.data(), 4) &&
        magic == std::array<char, 4>{'K', 'H', '2', 'J'} &&
        ReadBytes(g_base + RVA_SAVE + SAVE_PARTY_ROWS + 4u * world, row.data(), 4);
}

// Loading thread, inside PlayerKit's post-3E2EB0 SEH scope, after the kit and remote
// blocks (both refused by the flag matrix when this observer exists). No logging.
void Observer(std::uint16_t* resolved, const playerkit::LoadContext& c) {
    if (!g_requested.load(std::memory_order_acquire)) return; // cleared by ConfirmLocalReadiness: no reads
    LoadInput in;
    in.world = c.world; in.room = c.room; in.evtProgram = c.evtProgram;
    in.eventContext = c.eventContext; in.cutsceneState = c.cutsceneState;
    PlanTable table {};
    if (!g_planCell.Read(table)) g_tableMisses.fetch_add(1, std::memory_order_relaxed); // empty table: native load
    const PlanChoice choice = SelectPlan(table, in.world, in.room, in.evtProgram);
    if (choice.source == PlanSource::Hold) g_holdConsumed.store(choice.seq, std::memory_order_release); // B1: this load was the story's
    in.plan = choice.plan; in.source = choice.source;
    in.intentWorld = choice.world; in.intentRoom = choice.room; in.intentEvt = choice.evt;
    in.privateStatusReady = privatestatus::Ready();
    in.neutralInputReady = NeutralReady();
    bool ok = false;
    Load r = Load::ReadFault;
    __try {
        in.resolved[0] = resolved[0]; in.resolved[1] = resolved[1]; in.resolved[2] = resolved[2];
        in.rowRead = ReadRow(in.world, in.row);
        r = ApplyAfterResolve(in, resolved, g_original);
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_original = {};
        r = Load::ReadFault;
    }
    g_applied.store(ok && r == Load::Applied, std::memory_order_release);
    g_lastIn = in; g_lastResult = r; g_lastOk = ok;
    ++g_loads;
    if (ok && r == Load::Applied) ++g_appliedLoads;
}

// Same thread, right after the kit/remote lines, outside SEH.
void ObserverLog(playerkit::LogFn log) {
    if (!log || !g_requested.load(std::memory_order_acquire)) return;
    const auto& in = g_lastIn;
    log("[partynative] load world=%u room=%u evt=%u eventCtx=%d cutscene=%d private=%u neutral=%u intent=%02X/%02X/%u row=%02X/%02X/%02X/%02X native=0x%X/0x%X/0x%X plan=%s result=%s set=0x%X/0x%X loads=%u applied=%u source=%s misses=%u",
        in.world, in.room, in.evtProgram, in.eventContext != 0, in.cutsceneState, in.privateStatusReady ? 1u : 0u,
        in.neutralInputReady ? 1u : 0u, in.intentWorld & 0xFF, in.intentRoom & 0xFF, in.intentEvt, in.row[0], in.row[1], in.row[2], in.row[3],
        in.resolved[0], in.resolved[1], in.resolved[2], PlanName(in.plan), g_lastOk ? LoadName(g_lastResult) : "fault",
        g_lastResult == Load::Applied ? SORA : in.resolved[1], g_lastResult == Load::Applied ? SORA : in.resolved[2],
        g_loads, g_appliedLoads, PlanSourceName(in.source), g_tableMisses.load(std::memory_order_relaxed));
}

bool Enabled(const char* name) { char v[2] {}; return GetEnvironmentVariableA(name, v, 2) == 1 && v[0] == '1'; }
} // namespace

bool Install(std::uintptr_t exeBase, LogFn log) {
    const auto flags = playerkit::ReadFlagMatrix(); // the one parser for all three flags (N-a)
    if (!flags.party) return true; // default OFF: no observer, no game reads
    g_base = exeBase; g_log = log;
    if (!Enabled("KH2COOP_PARTY_NATIVE")) {
        if (log) log("[partynative] REFUSED: KH2COOP_PARTY_NATIVE is set but not exactly 1; kit/remote modes stay refused by the flag matrix");
        return false;
    }
    if (const unsigned conflict = playerkit::PartyConflicts(flags)) {
        if (log) log("[partynative] REFUSED: KH2COOP_PARTY_NATIVE conflicts with%s%s (VUH-1519 flag matrix); no observer",
                     conflict & 1 ? " KH2COOP_PLAYER_KIT" : "", conflict & 2 ? " KH2COOP_REMOTE_KIT_SLOT" : "");
        return false;
    }
    if (!Enabled("KH2COOP_NATIVE_SORA_PRIVATE_STATUS")) {
        if (log) log("[partynative] REFUSED: requires KH2COOP_NATIVE_SORA_PRIVATE_STATUS=1 (clones would share Sora's SAVE-bound status)");
        return false;
    }
    if (!Enabled("KH2COOP_CLONE_NEUTRAL_INPUT")) {
        if (log) log("[partynative] REFUSED: requires KH2COOP_CLONE_NEUTRAL_INPUT=1 (undriven clones would execute the local player's field commands)");
        return false;
    }
    playerkit::SetResolveObserver(&Observer, &ObserverLog);
    g_requested.store(true, std::memory_order_release);
    if (log) log("[partynative] requested: pinned rooms 04/1A and 04/0A, layout/intent '3 players, no NPCs' -> members 1/2 = Sora on the next qualified load of a planned room; party row and MEMT never written");
    return true;
}

bool Requested() { return g_requested.load(std::memory_order_acquire); }

void ConfirmLocalReadiness(bool resolverHookInstalled, ReadyProbe neutralInputConfigured) {
    if (!Requested()) return;
    g_neutralProbe = neutralInputConfigured;
    const bool privateReady = privatestatus::Ready(), neutral = NeutralReady();
    if (resolverHookInstalled && privateReady && neutral) {
        if (g_log) g_log("[partynative] locally ready: resolver hook=1 private status=1 neutral input=1");
        return;
    }
    g_requested.store(false, std::memory_order_release);
    if (g_log)
        g_log("[partynative] REFUSED: not locally ready (resolver hook=%u private status=%u neutral input=%u); party-native off: no member write, no publication, no adoption",
              resolverHookInstalled ? 1u : 0u, privateReady ? 1u : 0u, neutral ? 1u : 0u);
}

void NoteLayout(const PartyLayout& layout, std::uint8_t localSlot, std::uint32_t generation,
                const std::array<std::uint64_t, 3>& roster, bool own) {
    if (!Requested()) return;
    if (own && !LocallyReady()) {
        if (g_log) g_log("[partynative] layout version=%llu own=1 NOT adopted: host not locally ready",
                         static_cast<unsigned long long>(layout.version));
        return;
    }
    const Accept a = AcceptLayout(g_intent, layout, localSlot, generation, roster);
    if (a == Accept::Accepted) PublishIntent();
    if (own && g_hostLast.any && SameTuple(layout.location, g_hostLast.location) && layout.connections == g_hostLast.roster &&
        EchoConfirms(a, layout.version, g_hostLast.version))
        g_hostLast.echoed = true; // B2: only an adopted (or already adopted) echo stops the re-sends
    const bool applied = g_applied.load(std::memory_order_acquire) && AppliedClones() == 2;
    const Plan want = a == Accept::Accepted ? g_intent.plan : Plan::None;
    const bool match = (want == Plan::TwoClones) == applied;
    if (g_log)
        g_log("[partynative] layout version=%llu own=%u rule=%u reason=%u room=%02X/%02X evt=%u epoch=%u seats=%u:%u,%u:%u local=%u plan=%s accept=%s applied=%u match=%u%s",
              static_cast<unsigned long long>(layout.version), own ? 1u : 0u, static_cast<unsigned>(layout.rule),
              static_cast<unsigned>(layout.reason), layout.location.worldId, layout.location.roomId,
              layout.location.eventProgram, layout.location.epoch,
              static_cast<unsigned>(layout.seats[1].kind), layout.seats[1].playerSlot,
              static_cast<unsigned>(layout.seats[2].kind), layout.seats[2].playerSlot, localSlot, PlanName(want),
              AcceptName(a), applied ? 1u : 0u, match ? 1u : 0u,
              a == Accept::Accepted && !match ? " reapply-needed: the next qualified load of this room applies the plan" : "");
}

void NoteIntent(const PartyIntent& m, std::uint8_t localSlot, std::uint32_t generation,
                const std::array<std::uint64_t, 3>& roster, bool own) {
    if (!Requested()) return;
    if (own && !LocallyReady()) {
        if (g_log) g_log("[partynative] intent version=%llu own=1 NOT adopted: host not locally ready",
                         static_cast<unsigned long long>(m.version));
        return;
    }
    const Accept a = AcceptIntent(g_intent, m, localSlot, generation, roster);
    if (a == Accept::Accepted) PublishIntent();
    if (own)
        for (unsigned i = 0; i < INTENT_ROOMS.size(); ++i)
            if (INTENT_ROOMS[i] == m.target && g_hostIntent[i].any && g_hostIntent[i].roster == m.connections &&
                IntentEchoConfirms(g_intent, a, m.target, g_hostIntent[i].version))
                g_hostIntent[i].echoed = true; // B2: a refused echo keeps the re-sends going
    if (g_log)
        g_log("[partynative] intent version=%llu own=%u target=%02X/%02X/%u rule=%u seats=%u:%u,%u:%u kits=0x%X/0x%X local=%u plan=%s accept=%s",
              static_cast<unsigned long long>(m.version), own ? 1u : 0u, m.target.worldId, m.target.roomId, m.target.eventProgram,
              static_cast<unsigned>(m.rule), static_cast<unsigned>(m.seats[1].kind), m.seats[1].playerSlot,
              static_cast<unsigned>(m.seats[2].kind), m.seats[2].playerSlot, m.kits[1], m.kits[2], localSlot,
              PlanName(a == Accept::Accepted ? ProjectIntent(m, localSlot) : Plan::None), AcceptName(a));
}

bool HostIntentToPublish(std::uint32_t generation, const std::array<std::uint64_t, 3>& roster, PartyIntent& out) {
    if (!LocallyReady()) return false;
    const auto now = GetTickCount64();
    for (unsigned i = 0; i < INTENT_ROOMS.size(); ++i) {
        if (!HostIntentDue(g_hostIntent[i], generation, roster, now)) continue;
        if (HoldCovers(g_intent, INTENT_ROOMS[i].worldId, INTENT_ROOMS[i].roomId)) continue; // B1: never override a story hold
        if (StoryVisitCovers(g_storyVisit, INTENT_ROOMS[i].worldId, INTENT_ROOMS[i].roomId)) continue; // F1: nor during the held visit
        if (g_hostIntentVersion == UINT64_MAX) return false;
        RoomTransition room {}; room.epoch = 1; room.worldId = INTENT_ROOMS[i].worldId; room.roomId = INTENT_ROOMS[i].roomId;
        room.eventProgram = INTENT_ROOMS[i].eventProgram;
        const auto layout = defaultPartyLayout(room, g_hostIntentVersion + 1, PartyApplyReason::HostChoice, PinnedRule(room), 0, roster, {1, 2});
        if (!layout) continue;
        PartyIntent m {};
        m.version = g_hostIntentVersion + 1; m.connections = roster; m.target = INTENT_ROOMS[i]; m.rule = layout->rule; m.seats = layout->seats;
        for (unsigned s = 1; s < 3; ++s) m.kits[s] = m.seats[s].kind == PartyMemberKind::RemotePlayer ? SORA : 0;
        if (!validPartyIntent(m, roster)) continue;
        out = m;
        return true;
    }
    return false;
}

void NoteHostIntentSent(const PartyIntent& m, std::uint32_t generation) {
    if (!Requested()) return;
    g_hostIntentVersion = m.version;
    for (unsigned i = 0; i < INTENT_ROOMS.size(); ++i) {
        if (!(INTENT_ROOMS[i] == m.target)) continue;
        auto& last = g_hostIntent[i];
        const bool same = last.any && last.generation == generation && last.roster == m.connections;
        last.resends = same ? last.resends + 1 : 0;
        last.generation = generation; last.roster = m.connections; last.version = m.version; last.sentMs = GetTickCount64();
        last.any = true; last.echoed = false;
        if (last.resends >= HOST_ECHO_MAX_RESENDS && g_log)
            g_log("[partynative] ECHO MISSING: host intent version=%llu for %02X/%02X not echoed after %u re-sends",
                  static_cast<unsigned long long>(m.version), m.target.worldId, m.target.roomId, HOST_ECHO_MAX_RESENDS);
    }
    if (g_log)
        g_log("[partynative] host sent intent version=%llu target=%02X/%02X/%u; adopted only when the relay echoes it",
              static_cast<unsigned long long>(m.version), m.target.worldId, m.target.roomId, m.target.eventProgram);
}

void NoteReapply(const PartyReapply& reapply, std::uint32_t generation) {
    if (!Requested()) return;
    const Reapply r = ApplyReapply(g_intent, reapply, generation);
    if (r == Reapply::Cleared) PublishIntent();
    if (g_log)
        g_log("[partynative] reapply reason=%u afterVersion=%llu room=%02X/%02X epoch=%u result=%s%s",
              static_cast<unsigned>(reapply.reason), static_cast<unsigned long long>(reapply.afterVersion),
              reapply.location.worldId, reapply.location.roomId, reapply.location.epoch, ReapplyName(r),
              r != Reapply::Cleared ? ""
              : reapply.reason == PartyApplyReason::StoryForced ? "; story hold: the next load of this room resolves natively, then stored intents apply again"
                                                                : "; layout and intents retired: the next load resolves natively until a newer layout or intent");
}

void Observe(std::uint32_t generation, const std::array<std::uint64_t, 3>& roster, std::uint8_t localSlot,
             bool bridgeOpen) {
    if (!Requested()) return;
    const unsigned layoutBefore = g_intent.plan != Plan::None ? 1u : 0u, intentsBefore = IntentCount(g_intent);
    const unsigned holdBefore = g_intent.hold.valid ? 1u : 0u;
    if (ObserveSession(g_intent, generation, roster, localSlot, bridgeOpen) && g_log)
        g_log("[partynative] intent cleared generation=%u local=%u bridge=%u layout=%u intents=%u hold=%u; the next load resolves natively",
              generation, localSlot, bridgeOpen ? 1u : 0u, layoutBefore, intentsBefore, holdBefore); // N4: counts
    const StoryHold hold = g_intent.hold;
    if (ReleaseHold(g_intent, g_holdConsumed.load(std::memory_order_acquire))) {
        g_storyVisit = {true, hold.world, hold.room, 0}; g_storyQuietLogged = false; // F1: the host stays quiet for this visit
        if (g_log)
            g_log("[partynative] story hold released room=%02X/%02X evt=%u after one native load; stored intents apply again; host publishes nothing for this room until its next epoch",
                  hold.world & 0xFF, hold.room & 0xFF, hold.evt);
    }
    PublishIntent(); // publishes only on a change
}

bool HostLayoutToPublish(std::uint32_t generation, const RoomTransition& location,
                         const std::array<std::uint64_t, 3>& roster, PartyLayout& out) {
    if (!LocallyReady()) return false; // R2
    if (StorySuppresses(g_storyVisit, g_intent, location)) { // F1: never supersede a client's story hold
        if (!g_storyQuietLogged && g_log)
            g_log("[partynative] host layout suppressed room=%02X/%02X epoch=%u: story hold or held visit", location.worldId, location.roomId, location.epoch);
        g_storyQuietLogged = true;
        return false;
    }
    if (HostEchoGivenUp(g_hostLast, generation, location, roster)) {
        if (!g_echoGiveUpLogged && g_log)
            g_log("[partynative] ECHO MISSING: host layout version=%llu for %02X/%02X epoch=%u not echoed after %u re-sends; the host stays native (clients may apply: asymmetric)",
                  static_cast<unsigned long long>(g_hostLast.version), location.worldId, location.roomId, location.epoch,
                  HOST_ECHO_MAX_RESENDS);
        g_echoGiveUpLogged = true;
        return false;
    }
    std::array<std::uint8_t, 4> row {};
    const bool rowRead = location.worldId < 0x20 && ReadRow(static_cast<std::uint8_t>(location.worldId), row);
    PartyApplyReason reason = PartyApplyReason::HostChoice;
    if (!HostShouldPublish(g_hostLast, generation, location, roster, rowRead, row, GetTickCount64(), reason)) return false;
    if (g_hostVersion == UINT64_MAX) return false;
    const auto layout = defaultPartyLayout(location, g_hostVersion + 1, reason, PinnedRule(location), 0, roster, {1, 2});
    if (!layout) return false;
    out = *layout;
    return true;
}

void NoteHostSent(const PartyLayout& layout, std::uint32_t generation) {
    if (!Requested()) return;
    const bool same = g_hostLast.any && g_hostLast.generation == generation && SameTuple(g_hostLast.location, layout.location) &&
        g_hostLast.roster == layout.connections;
    g_hostLast.resends = same ? g_hostLast.resends + 1 : 0;
    if (!same) g_echoGiveUpLogged = false;
    g_hostVersion = layout.version;
    g_hostLast.generation = generation;
    g_hostLast.location = layout.location;
    g_hostLast.roster = layout.connections;
    g_hostLast.version = layout.version;
    g_hostLast.sentMs = GetTickCount64();
    g_hostLast.reason = layout.reason;
    g_hostLast.any = true;
    g_hostLast.echoed = false;
    // S2: each room-change layout re-arms the pinned-room intents (fresh versions), so a client that
    // lost its store is repaired at the next transition. Two extra reliable packets per transition.
    if (!same && layout.reason == PartyApplyReason::RoomChanged)
        for (auto& h : g_hostIntent) h.any = false;
    if (g_log)
        g_log("[partynative] host sent version=%llu reason=%u room=%02X/%02X epoch=%u resend=%u; adopted only when the relay echoes it",
              static_cast<unsigned long long>(layout.version), static_cast<unsigned>(layout.reason),
              layout.location.worldId, layout.location.roomId, layout.location.epoch, g_hostLast.resends);
}

unsigned AppliedClones() {
    if (!Requested() || !g_applied.load(std::memory_order_acquire)) return 0;
    std::uint16_t members[3] {};
    if (!ReadBytes(g_base + RVA_RESOLVED, members, sizeof(members))) return 0;
    return members[1] == SORA && members[2] == SORA ? 2u : 0u;
}

void Shutdown() {
    if (!Requested()) return;
    std::uint16_t before[3] {}, after[3] {};
    bool ok = false;
    __try {
        auto* resolved = reinterpret_cast<std::uint16_t*>(g_base + RVA_RESOLVED);
        std::memcpy(before, resolved, sizeof(before));
        RestoreMembers(g_original, resolved);
        std::memcpy(after, resolved, sizeof(after));
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    g_applied.store(false, std::memory_order_release);
    if (g_log)
        g_log("[partynative] shutdown: members 0x%X/0x%X -> 0x%X/0x%X ok=%u (live clones keep their actors until the next load)",
              before[1], before[2], after[1], after[2], ok ? 1u : 0u);
}
#endif

} // namespace kh2coop::inject::partynative
