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
    case Load::LocalKitMismatch: return "local-kit-mismatch";
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

std::array<std::uint8_t, 2> OtherSlots(std::uint8_t localSlot) {
    std::array<std::uint8_t, 2> out {0xFF, 0xFF}; unsigned n = 0;
    for (std::uint8_t s = 0; s < 3; ++s) if (s != localSlot && n < 2) out[n++] = s;
    return out;
}
// The reviewed kit table (kh2coop/PlayerKits.hpp): a qualified kit's code is its roster code; 0 is legacy Sora.
std::uint8_t KitCode(std::uint16_t kit) {
    if (kit == 0) return 0;
    const auto* k = qualifiedKit(kit);
    return k ? k->roster : KIT_CODE_INVALID;
}
std::uint16_t KitFromCode(std::uint8_t code) {
    const auto* k = qualifiedKitByRoster(code);
    return k ? k->member : 0;
}

Plan ProjectIntent(const PartyIntent& m, std::uint8_t localSlot, bool kitsAllowed) {
    PartyLayout probe {};
    probe.location.epoch = 1; probe.location.worldId = m.target.worldId; probe.location.roomId = m.target.roomId;
    probe.location.eventProgram = m.target.eventProgram;
    probe.version = m.version; probe.connections = m.connections; probe.rule = m.rule; probe.seats = m.seats;
    for (unsigned i = 0; i < 3; ++i) {
        const bool player = i == 0 || m.seats[i].kind == PartyMemberKind::RemotePlayer;
        const std::uint16_t kit = i == 0 && m.kits[0] == 0 ? SORA : m.kits[i];
        if (!player) continue;
        if (KitCode(kit) == KIT_CODE_INVALID) return Plan::Unsupported; // not a qualified kit (the table)
        if (kit != SORA && !kitsAllowed) return Plan::Unsupported;     // party kits off here: native
    }
    return Project(probe, localSlot);
}

Accept AcceptIntent(Intent& intent, const PartyIntent& m, std::uint8_t localSlot,
                    std::uint32_t generation, const std::array<std::uint64_t, 3>& roster, bool kitsAllowed) {
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
    *slot = {true, m.target, ProjectIntent(m, localSlot, kitsAllowed), m.version, ++intent.seq};
    // Party kits: the roster's kits, keyed by each seat's own network slot (newest intent wins).
    std::array<std::uint16_t, 3> kits {SORA, SORA, SORA};
    for (unsigned i = 0; i < 3; ++i) {
        const auto& seat = m.seats[i];
        const bool player = i == 0 || seat.kind == PartyMemberKind::RemotePlayer;
        if (player && seat.playerSlot < 3) kits[seat.playerSlot] = i == 0 && m.kits[0] == 0 ? SORA : m.kits[i];
    }
    intent.slotKits = kits; intent.kitsKnown = true;
    if (HoldCovers(intent, m.target.worldId, m.target.roomId)) intent.hold = {}; // B1: a newer intent supersedes the hold
    return Accept::Accepted;
}

namespace {
std::uint64_t PlanKey(Plan p, std::uint16_t w, std::uint16_t r, std::uint16_t e, PlanSource s) {
    return static_cast<std::uint64_t>(p) | (static_cast<std::uint64_t>(w & 0xFF) << 8) | (static_cast<std::uint64_t>(r & 0xFF) << 16) |
        (static_cast<std::uint64_t>(e) << 24) | (static_cast<std::uint64_t>(s) << 40);
}
} // namespace

void PackPlanTable(const Intent& intent, PlanTable& out, bool kitsMode) {
    out = {};
    std::uint64_t kitBits = 0; // member 1, member 2, expected local member 0 for this machine
    // Rev2 B1: a machine WITHOUT party kits never applies a roster that holds a non-Sora kit, through ANY
    // entry (a later layout borrows the roster's kits). Kits off: no kit bits at all, so a legacy
    // all-Sora roster stays bit-identical to VUH-1786.
    bool foreignKits = false;
    if (!kitsMode && intent.kitsKnown)
        for (const auto k : intent.slotKits) if (k != SORA) foreignKits = true;
    if (kitsMode && intent.kitsKnown && intent.localSlot < 3) {
        const auto o = OtherSlots(intent.localSlot);
        kitBits = (static_cast<std::uint64_t>(KitCode(intent.slotKits[o[0]]) & 7) << 48) |
                  (static_cast<std::uint64_t>(KitCode(intent.slotKits[o[1]]) & 7) << 51) |
                  (static_cast<std::uint64_t>(KitCode(intent.slotKits[intent.localSlot]) & 7) << 54);
    }
    if (intent.plan != Plan::None) {
        // Party kits: a layout carries no kits; in kits mode it may only borrow the roster's kits.
        const Plan p = intent.plan == Plan::TwoClones && ((kitsMode && !intent.kitsKnown) || foreignKits) ? Plan::Unsupported : intent.plan;
        out[0] = {PlanKey(p, intent.world, intent.room, intent.evt, PlanSource::Layout) | kitBits, intent.layoutSeq};
    }
    if (intent.hold.valid) out[1] = {PlanKey(Plan::None, intent.hold.world, intent.hold.room, intent.hold.evt, PlanSource::Hold), intent.hold.seq};
    for (unsigned i = 0; i < MAX_INTENT_TARGETS; ++i) {
        const auto& t = intent.targets[i];
        if (t.valid && t.plan != Plan::None)
            out[2 + i] = {PlanKey(t.plan == Plan::TwoClones && foreignKits ? Plan::Unsupported : t.plan, t.target.worldId, t.target.roomId,
                                  t.target.eventProgram, PlanSource::Intent) | kitBits, t.seq};
    }
}

PlanChoice SelectPlan(const PlanTable& table, std::uint16_t world, std::uint16_t room, std::uint16_t evt) {
    const auto decode = [](const PlanEntry& v) {
        const auto source = static_cast<PlanSource>((v.key >> 40) & 0xFF);
        return PlanChoice {source == PlanSource::Hold ? Plan::None : static_cast<Plan>(v.key & 0xFF),
                           static_cast<std::uint16_t>((v.key >> 8) & 0xFF), static_cast<std::uint16_t>((v.key >> 16) & 0xFF),
                           static_cast<std::uint16_t>((v.key >> 24) & 0xFFFF), source, v.seq,
                           KitFromCode(static_cast<std::uint8_t>((v.key >> 48) & 7)), KitFromCode(static_cast<std::uint8_t>((v.key >> 51) & 7)),
                           KitFromCode(static_cast<std::uint8_t>((v.key >> 54) & 7))};
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
    if (in.resolved[1] != DONALD || in.resolved[2] != GOOFY) return Load::NativeNotDefault;
    if (!in.kit1 || !in.kit2 || !in.localKit) return Load::Unsupported; // an invalid kit code
    // Party kits: member 0 must already be this machine's own kit (PlayerKit writes it first in the
    // same hook scope). Kits off: localKit is Sora, i.e. the VUH-1519 rule.
    if (in.resolved[0] != in.localKit)
        return qualifiedKit(in.resolved[0]) ? Load::LocalKitMismatch : Load::NativeNotDefault; // any qualified kit (the table)
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
    // Rev3: in a party stamp the game builds member 0 first (raw566, a clone), member 1 next (raw567, a clone)
    // and member 2 LAST (raw568). Every player-class constructor stores the canonical pointer, so the local
    // player is built from member 2 (live run 20261007-084248: a Roxas member 0 became a clone and a Roxas
    // member 2 became the local). Members 0/1 = the other players' kits, member 2 = this machine's own kit.
    // Kits off: all three are Sora, i.e. exactly the VUH-1519 write (member 0 untouched).
    // R3-1: member 0's original is the PRE-KIT native value, so member 0 ends native in either shutdown order.
    original = {true, in.native0 ? in.native0 : resolved[0], resolved[1], resolved[2], in.kit1, in.kit2, in.localKit};
    if (resolved[0] != in.kit1) resolved[0] = in.kit1;
    resolved[1] = in.kit2;
    resolved[2] = in.localKit;
    return r;
}

void RestoreMembers(Originals& original, std::uint16_t* resolved) {
    if (!original.valid || !resolved) { original = {}; return; }
    if (resolved[0] == original.set0) resolved[0] = original.member0; // only while still ours
    if (resolved[1] == original.set1) resolved[1] = original.member1;
    if (resolved[2] == original.set2) resolved[2] = original.member2;
    original = {};
}

bool HostKits(std::uint16_t ownKit, std::uint8_t localSlot, const RemoteKits& remote, std::array<std::uint16_t, 3>& kits) {
    kits = {};
    if (localSlot > 2 || KitCode(ownKit) == KIT_CODE_INVALID) return false; // own kit must be a qualified kit (or legacy 0)
    kits[localSlot] = ownKit == 0 ? SORA : ownKit;
    const auto o = OtherSlots(localSlot);
    for (unsigned i = 0; i < 2; ++i) {
        const auto* k = remote.seen[i] ? qualifiedKitByRoster(remote.roster[i]) : nullptr;
        if (!k) return false; // unknown, or not a qualified kit in the reviewed table (dual-wield, Mickey)
        kits[o[i]] = k->member;
    }
    return true;
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
                   std::uint64_t nowMs, const std::array<std::uint16_t, 3>& kits) {
    if (!generation) return false;
    for (const auto c : roster) if (!c) return false;
    if (!last.any || last.generation != generation || last.roster != roster || last.kits != kits) return true;
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
// Party kits.
std::atomic<bool> g_kits {false};                 // KH2COOP_PARTY_KITS=1 accepted
std::atomic<std::uint32_t> g_appliedSet {0};      // member1 | member2 << 16 of the last applied load
std::atomic<std::uint16_t> g_appliedLocal {0};    // rev2 S3: the expected local member 0 of that load
std::uint32_t g_kitsGeneration = 0; std::array<std::uint64_t, 3> g_kitsRoster {}; std::uint8_t g_kitsSlot = 0xFF; // rev2 S1
RemoteKits g_remoteKits {};                       // game thread (PollPuppetPoses)
bool g_kitsUnknownLogged = false;

void PublishIntent() {
    PlanTable table {};
    PackPlanTable(g_intent, table, g_kits.load(std::memory_order_acquire));
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
    in.kit1 = choice.kit1; in.kit2 = choice.kit2; in.localKit = choice.localKit;
    in.native0 = c.resolved0; // R3-1: PlayerKit captured it before its own member-0 write
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
    g_appliedSet.store(ok && r == Load::Applied ? static_cast<std::uint32_t>(in.kit1) | (static_cast<std::uint32_t>(in.kit2) << 16) : 0u,
                       std::memory_order_release);
    g_appliedLocal.store(ok && r == Load::Applied ? in.localKit : 0, std::memory_order_release);
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
        g_lastResult == Load::Applied ? in.kit1 : in.resolved[1], g_lastResult == Load::Applied ? in.kit2 : in.resolved[2],
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
    if (Enabled("KH2COOP_ENEMY_TARGET_REMOTE")) {  // VUH-1515: no joint fixture yet; both flags refuse each other
        if (log) log("[partynative] REFUSED: KH2COOP_PARTY_NATIVE conflicts with KH2COOP_ENEMY_TARGET_REMOTE (no joint fixture); no observer");
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
    if (flags.partyKits) { // party kits: exactly "1", and only on top of everything above
        if (!Enabled("KH2COOP_PARTY_KITS")) {
            if (log) log("[partynative] REFUSED: KH2COOP_PARTY_KITS is set but not exactly 1; no observer");
            return false;
        }
        g_kits.store(true, std::memory_order_release);
        if (log) log("[partynative] party kits: each seat shows its owner's chosen kit (a qualified kit of the PlayerKits table); local kit %s",
                     flags.kit ? "from KH2COOP_PLAYER_KIT (checked by PlayerKit)" : "Sora 0x54");
    }
    playerkit::SetResolveObserver(&Observer, &ObserverLog);
    g_requested.store(true, std::memory_order_release);
    if (log) log("[partynative] requested: pinned rooms 04/1A and 04/0A, layout/intent '3 players, no NPCs' -> members 1/2 = the other players' kits (Sora unless party kits) on the next qualified load of a planned room; party row and MEMT never written");
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
    const Accept a = AcceptIntent(g_intent, m, localSlot, generation, roster, g_kits.load(std::memory_order_acquire));
    if (a == Accept::Accepted) PublishIntent();
    if (own)
        for (unsigned i = 0; i < INTENT_ROOMS.size(); ++i)
            if (INTENT_ROOMS[i] == m.target && g_hostIntent[i].any && g_hostIntent[i].roster == m.connections &&
                IntentEchoConfirms(g_intent, a, m.target, g_hostIntent[i].version))
                g_hostIntent[i].echoed = true; // B2: a refused echo keeps the re-sends going
    if (g_log)
        g_log("[partynative] intent version=%llu own=%u target=%02X/%02X/%u rule=%u seats=%u:%u,%u:%u kits=0x%X/0x%X/0x%X local=%u plan=%s accept=%s",
              static_cast<unsigned long long>(m.version), own ? 1u : 0u, m.target.worldId, m.target.roomId, m.target.eventProgram,
              static_cast<unsigned>(m.rule), static_cast<unsigned>(m.seats[1].kind), m.seats[1].playerSlot,
              static_cast<unsigned>(m.seats[2].kind), m.seats[2].playerSlot, m.kits[0], m.kits[1], m.kits[2], localSlot,
              PlanName(a == Accept::Accepted ? ProjectIntent(m, localSlot, g_kits.load(std::memory_order_acquire)) : Plan::None), AcceptName(a));
}

bool HostIntentToPublish(std::uint32_t generation, const std::array<std::uint64_t, 3>& roster, PartyIntent& out) {
    if (!LocallyReady()) return false;
    const auto now = GetTickCount64();
    // kits off: the VUH-1786 encoding, unchanged. NB: HostPublished.kits is compared seat-indexed and `kits`
    // is slot-indexed; they coincide because the host is slot 0 and defaultPartyLayout seats slots {1, 2}.
    std::array<std::uint16_t, 3> kits {0, SORA, SORA};
    if (g_kits.load(std::memory_order_acquire) && !HostKits(playerkit::LocalKit(), 0, g_remoteKits, kits)) {
        if (!g_kitsUnknownLogged && g_log)
            g_log("[partynative] host intents withheld: party kits not known yet (seen=%u/%u roster=%u/%u)",
                  g_remoteKits.seen[0] ? 1u : 0u, g_remoteKits.seen[1] ? 1u : 0u, g_remoteKits.roster[0], g_remoteKits.roster[1]);
        g_kitsUnknownLogged = true;
        return false;
    }
    for (unsigned i = 0; i < INTENT_ROOMS.size(); ++i) {
        if (!HostIntentDue(g_hostIntent[i], generation, roster, now, kits)) continue;
        if (HoldCovers(g_intent, INTENT_ROOMS[i].worldId, INTENT_ROOMS[i].roomId)) continue; // B1: never override a story hold
        if (StoryVisitCovers(g_storyVisit, INTENT_ROOMS[i].worldId, INTENT_ROOMS[i].roomId)) continue; // F1: nor during the held visit
        if (g_hostIntentVersion == UINT64_MAX) return false;
        RoomTransition room {}; room.epoch = 1; room.worldId = INTENT_ROOMS[i].worldId; room.roomId = INTENT_ROOMS[i].roomId;
        room.eventProgram = INTENT_ROOMS[i].eventProgram;
        const auto layout = defaultPartyLayout(room, g_hostIntentVersion + 1, PartyApplyReason::HostChoice, PinnedRule(room), 0, roster, {1, 2});
        if (!layout) continue;
        PartyIntent m {};
        m.version = g_hostIntentVersion + 1; m.connections = roster; m.target = INTENT_ROOMS[i]; m.rule = layout->rule; m.seats = layout->seats;
        m.kits[0] = kits[0];
        for (unsigned s = 1; s < 3; ++s)
            m.kits[s] = m.seats[s].kind == PartyMemberKind::RemotePlayer && m.seats[s].playerSlot < 3 ? kits[m.seats[s].playerSlot] : 0;
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
        const bool same = last.any && last.generation == generation && last.roster == m.connections && last.kits == m.kits;
        last.resends = same ? last.resends + 1 : 0;
        last.generation = generation; last.roster = m.connections; last.version = m.version; last.sentMs = GetTickCount64();
        last.kits = m.kits; // party kits: HostIntentDue compares against what was sent
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
    // Rev2 S1: a remote kit is the previous occupant's after any generation, roster, slot or bridge change.
    if (!bridgeOpen || (generation != 0 && (generation != g_kitsGeneration || roster != g_kitsRoster || localSlot != g_kitsSlot))) {
        if ((g_remoteKits.seen[0] || g_remoteKits.seen[1]) && g_log)
            g_log("[partynative] remote kits reset (generation=%u local=%u bridge=%u): withheld until each new owner's pose", generation, localSlot, bridgeOpen ? 1u : 0u);
        g_remoteKits = {}; g_kitsUnknownLogged = false;
        g_kitsGeneration = bridgeOpen ? generation : 0; g_kitsRoster = bridgeOpen ? roster : std::array<std::uint64_t, 3>{}; g_kitsSlot = bridgeOpen ? localSlot : 0xFF;
    }
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
    { std::array<std::uint16_t, 3> kits {}; // party kits: never publish a (Sora-implied) layout before the kits are known
      if (g_kits.load(std::memory_order_acquire) && !HostKits(playerkit::LocalKit(), 0, g_remoteKits, kits)) return false; }
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
    std::uint16_t m1 = 0, m2 = 0;
    if (!AppliedMembers(m1, m2)) return 0;
    std::uint16_t members[3] {};
    if (!ReadBytes(g_base + RVA_RESOLVED, members, sizeof(members))) return 0;
    return members[0] == m1 && members[1] == m2 && members[2] == AppliedLocal() ? 2u : 0u; // still holds what this load wrote (rev3)
}

bool KitsActive() { return Requested() && g_kits.load(std::memory_order_acquire); }

std::uint16_t AppliedLocal() {
    std::uint16_t m1 = 0, m2 = 0;
    return AppliedMembers(m1, m2) ? g_appliedLocal.load(std::memory_order_acquire) : 0;
}

bool AppliedMembers(std::uint16_t& member1, std::uint16_t& member2) {
    member1 = member2 = 0;
    if (!Requested() || !g_applied.load(std::memory_order_acquire)) return false;
    const std::uint32_t v = g_appliedSet.load(std::memory_order_acquire);
    member1 = static_cast<std::uint16_t>(v & 0xFFFF); member2 = static_cast<std::uint16_t>(v >> 16);
    return member1 != 0 && member2 != 0;
}

std::uint16_t PuppetKit(int index) {
    std::uint16_t m1 = 0, m2 = 0;
    if (index < 0 || index > 1 || !AppliedMembers(m1, m2)) return 0;
    return index == 0 ? m1 : m2;
}

void NoteRemoteKit(int index, std::uint8_t roster) {
    if (index < 0 || index > 1 || !KitsActive()) return;
    auto& k = g_remoteKits;
    const bool change = !k.seen[static_cast<unsigned>(index)] || k.roster[static_cast<unsigned>(index)] != roster;
    k.seen[static_cast<unsigned>(index)] = true; k.roster[static_cast<unsigned>(index)] = roster;
    if (change) {
        g_kitsUnknownLogged = false;
        const auto* row = qualifiedKitByRoster(roster);
        if (g_log) g_log("[partynative] remote kit puppet %d: roster %u (%s 0x%X)", index, roster,
                         row ? row->name : "unsupported: not a qualified kit, no party kits plan", row ? row->member : 0u);
    }
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
    g_remoteKits = {}; // rev2 S1
    if (g_log)
        g_log("[partynative] shutdown: members 0x%X/0x%X -> 0x%X/0x%X ok=%u member0 0x%X -> 0x%X (live clones keep their actors until the next load)",
              before[1], before[2], after[1], after[2], ok ? 1u : 0u, before[0], after[0]);
}
#endif

} // namespace kh2coop::inject::partynative
