// PartyNative — see PartyNative.hpp (VUH-1519).
#include "PartyNativePolicy.hpp"

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

Accept AcceptLayout(Intent& intent, const PartyLayout& layout, std::uint8_t localSlot,
                    std::uint32_t generation, const std::array<std::uint64_t, 3>& roster) {
    if (!generation) return Accept::NoGeneration;
    if (localSlot > 2 || !roster[localSlot]) return Accept::LocalSlot;
    if (layout.connections != roster) return Accept::RosterMismatch;
    if (!validPartyLayout(layout, roster)) return Accept::Invalid;
    // Versions are one namespace per host connection within a generation.
    if (intent.generation == generation && intent.roster[0] == roster[0] && layout.version <= intent.version)
        return Accept::StaleVersion;
    intent.generation = generation;
    intent.roster = roster;
    intent.localSlot = localSlot;
    intent.version = layout.version;
    intent.plan = Project(layout, localSlot);
    intent.world = layout.location.worldId;
    intent.room = layout.location.roomId;
    intent.evt = layout.location.eventProgram;
    return Accept::Accepted;
}

bool ObserveSession(Intent& intent, std::uint32_t generation, const std::array<std::uint64_t, 3>& roster,
                    std::uint8_t localSlot, bool bridgeOpen) {
    if (intent.generation == 0 && intent.plan == Plan::None) return false;
    const bool hadPlan = intent.plan != Plan::None;
    if (!bridgeOpen) { intent = {}; return hadPlan; }
    if (generation == 0) return false; // unknown (ordering/delivery flicker): hold
    if (generation == intent.generation && roster == intent.roster && localSlot == intent.localSlot) return false;
    // Keep the version floor only for the same generation and host connection.
    const bool keepFloor = generation == intent.generation && roster[0] == intent.roster[0];
    const auto floor = intent.version;
    intent = {};
    if (keepFloor) { intent.generation = generation; intent.roster = roster; intent.localSlot = localSlot; intent.version = floor; }
    return hadPlan;
}

Reapply ApplyReapply(Intent& intent, const PartyReapply& reapply, std::uint32_t generation) {
    if (!generation || (intent.generation != 0 && intent.generation != generation)) return Reapply::OtherGeneration;
    if (reapply.reason != PartyApplyReason::StoryForced && reapply.reason != PartyApplyReason::RosterChanged)
        return Reapply::LoggedOnly;
    intent.plan = Plan::None;
    intent.world = intent.room = intent.evt = 0xFFFF;
    if (intent.generation == generation && reapply.afterVersion > intent.version) intent.version = reapply.afterVersion;
    return Reapply::Cleared;
}

Load Decide(const LoadInput& in) {
    if (in.plan == Plan::None) return Load::NoIntent;
    if (in.plan != Plan::TwoClones) return Load::Unsupported;
    if (in.world != 4 || in.room != 0x1A) return Load::WorldNotQualified; // GoA only
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
    return l.worldId == 4 && l.roomId == 0x1A && l.eventProgram == 0 ? PartyRule::Default : PartyRule::Unavailable;
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
// Published by the game thread, read by the loading thread at each resolve:
// plan | world << 8 | room << 24 | evt << 40.
std::atomic<std::uint64_t> g_loadPlan {0};
// Loading thread (hook); Shutdown runs after the hook is disabled.
Originals g_original {};
std::atomic<bool> g_applied {false};
LoadInput g_lastIn {};
Load g_lastResult = Load::NoIntent;
bool g_lastOk = false;
std::uint32_t g_loads = 0, g_appliedLoads = 0;

void PublishIntent() {
    const std::uint64_t v = static_cast<std::uint64_t>(g_intent.plan) | (static_cast<std::uint64_t>(g_intent.world) << 8) |
        (static_cast<std::uint64_t>(g_intent.room) << 24) | (static_cast<std::uint64_t>(g_intent.evt) << 40);
    g_loadPlan.store(v, std::memory_order_release);
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
    const std::uint64_t packed = g_loadPlan.load(std::memory_order_acquire);
    in.plan = static_cast<Plan>(packed & 0xFF);
    in.intentWorld = static_cast<std::uint16_t>(packed >> 8);
    in.intentRoom = static_cast<std::uint16_t>(packed >> 24);
    in.intentEvt = static_cast<std::uint16_t>(packed >> 40);
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
    log("[partynative] load world=%u room=%u evt=%u eventCtx=%d cutscene=%d private=%u neutral=%u intent=%02X/%02X/%u row=%02X/%02X/%02X/%02X native=0x%X/0x%X/0x%X plan=%s result=%s set=0x%X/0x%X loads=%u applied=%u",
        in.world, in.room, in.evtProgram, in.eventContext != 0, in.cutsceneState, in.privateStatusReady ? 1u : 0u,
        in.neutralInputReady ? 1u : 0u, in.intentWorld & 0xFF, in.intentRoom & 0xFF, in.intentEvt, in.row[0], in.row[1], in.row[2], in.row[3],
        in.resolved[0], in.resolved[1], in.resolved[2], PlanName(in.plan), g_lastOk ? LoadName(g_lastResult) : "fault",
        g_lastResult == Load::Applied ? SORA : in.resolved[1], g_lastResult == Load::Applied ? SORA : in.resolved[2],
        g_loads, g_appliedLoads);
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
    if (log) log("[partynative] requested: GoA 04/1A only, layout '3 players, no NPCs' -> members 1/2 = Sora on the next qualified load of the layout's own room; party row and MEMT never written");
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
    if (own && g_hostLast.any && SameTuple(layout.location, g_hostLast.location) && layout.connections == g_hostLast.roster)
        g_hostLast.echoed = true;
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

void NoteReapply(const PartyReapply& reapply, std::uint32_t generation) {
    if (!Requested()) return;
    const Reapply r = ApplyReapply(g_intent, reapply, generation);
    if (r == Reapply::Cleared) PublishIntent();
    if (g_log)
        g_log("[partynative] reapply reason=%u afterVersion=%llu room=%02X/%02X epoch=%u result=%s%s",
              static_cast<unsigned>(reapply.reason), static_cast<unsigned long long>(reapply.afterVersion),
              reapply.location.worldId, reapply.location.roomId, reapply.location.epoch, ReapplyName(r),
              r == Reapply::Cleared ? "; the next load resolves natively until a newer layout" : "");
}

void Observe(std::uint32_t generation, const std::array<std::uint64_t, 3>& roster, std::uint8_t localSlot,
             bool bridgeOpen) {
    if (!Requested()) return;
    if (ObserveSession(g_intent, generation, roster, localSlot, bridgeOpen)) {
        PublishIntent();
        if (g_log) g_log("[partynative] intent cleared generation=%u local=%u bridge=%u; the next load resolves natively",
                         generation, localSlot, bridgeOpen ? 1u : 0u);
    } else if (g_intent.plan == Plan::None) {
        PublishIntent();
    }
}

bool HostLayoutToPublish(std::uint32_t generation, const RoomTransition& location,
                         const std::array<std::uint64_t, 3>& roster, PartyLayout& out) {
    if (!LocallyReady()) return false; // R2
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
