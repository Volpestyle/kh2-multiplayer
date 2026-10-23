#pragma once
// ============================================================================
// PlayerKit — per-session player-class kit for the local slot-0 player
// (VUH-1513 step 2). Default OFF: nothing is hooked unless KH2COOP_PLAYER_KIT
// names an allowed vanilla kit, or TARGET_ALL/MIRROR requests the automatic
// two-player route clone dependency. That path owns only verified default RAM leaves.
//
// Mechanism (static RE on exe 9002b2de, live-proven by VUH-1513 E1-02/03):
//   area load 39C860 -> 3E2EB0 walks the MEMT ([exe+0x2A252E0]) into the
//   resolved member array exe+0x2A25300 (u16[18]); member 0 is the player's
//   objentry ID. The array is only read later, through getters
//   (3E2E50/3E2E60/3E3180/3E3680/3E3830); 3E2EB0's only other effect is the
//   pool-size setter 39E270. A post-hook on 3E2EB0 therefore sees every
//   area load on the game's own loading thread, and replacing member 0 there
//   is the same downstream state E1 produced by editing the MEMT entry.
//   The MEMT itself is never written.
//
// Kits: the qualified non-Sora rows of kh2coop/PlayerKits.hpp (today Roxas 0x5A, P_EX110, and Mickey 0x5B,
// P_EX200); dual-wield Roxas 0x323 is refused until it has its own fixture.
// 0 / unset = Sora (no hook, no reads).
// ============================================================================

#include <cstdint>

namespace kh2coop::inject::playerkit {

using LogFn = void (*)(const char* fmt, ...);

constexpr std::uint16_t SORA = 0x54, ROXAS = 0x5A, ROXAS_DW = 0x323, MICKEY = 0x5B;
constexpr std::uint64_t RVA_RESOLVE_MEMBERS = 0x3E2EB0;
constexpr std::uint64_t RVA_RESOLVED_MEMBERS = 0x2A25300; // u16[18]
constexpr std::uint64_t RVA_WORLD = 0x717008, RVA_ROOM = 0x717009, RVA_EVT_PROGRAM = 0x717010;
constexpr std::uint64_t RVA_EVENT_CONTEXT = 0x2A11478, RVA_CUTSCENE_STATE = 0xB65210;

// Pure policy (offline-controlled in checks/kit_policy_checks.cpp).
enum class Reason : std::uint8_t {
    Applied, Disabled, WorldNotQualified, EventRoom, EventActive, NativeNotSora, AlreadyKit
};
struct LoadContext {
    std::uint8_t world = 0, room = 0;
    std::uint16_t evtProgram = 0;
    std::uint64_t eventContext = 0;
    std::int32_t cutsceneState = 0;
    std::uint16_t resolved0 = 0;   // what 3E2EB0 just resolved for member 0
};
bool KitAllowed(std::uint16_t kit);
// The solo path (KH2COOP_PLAYER_KIT without party kits): KitAllowed and the row's soloQualified flag.
bool SoloKitAllowed(std::uint16_t kit);
// A qualified non-Sora kit (e.g. "0x5A"/"90" -> Roxas), "0"/empty -> off; false on anything else.
bool ParseKit(const char* text, std::uint16_t& kit);
bool WorldQualified(std::uint8_t world);   // first live run: HB(4) only; BB(5) later
// True for any set, non-"0" KH2COOP_PLAYER_KIT text, valid or not (conservative puppet guard).
bool KitEnvBlocksPuppets(const char* text);
Reason Decide(std::uint16_t kit, const LoadContext& c);
const char* ReasonName(Reason r);
// 0 Sora, 1 Roxas, 2 dual-wield Roxas, 3 Mickey; 0 for anything unknown.
std::uint8_t RosterFromObjectId(std::uint32_t objectId);
// Apply to a resolved array (real or synthetic). Returns the decision; on
// Applied, *original receives the replaced value.
Reason ApplyAfterResolve(std::uint16_t kit, const LoadContext& c, std::uint16_t* resolved,
                         std::uint16_t* original);
// Restore member 0 only while it still holds our kit; true when restored or nothing to do.
bool RestoreResolved(std::uint16_t kit, std::uint16_t original, std::uint16_t* resolved);

struct Stats {
    bool requested = false, installed = false;
    std::uint16_t kit = 0;
    std::uint32_t loads = 0, applied = 0, skipped = 0, faults = 0;
    std::uint8_t lastWorld = 0xFF, lastRoom = 0xFF;
    Reason lastReason = Reason::Disabled;
};

// Automatic two-player route clone uses the same verified selector row in TT/HB/BC.
// Only the default row is ours to change; restoration never overwrites story changes.
constexpr std::uint32_t BC_NATIVE_ROW = 0x12020100, BC_CLONE_ROW = 0x12020000;
bool BcCloneEligible(bool requested, bool twoPlayers, bool prerequisites, const LoadContext& c);
std::uint32_t BcCloneRow(std::uint32_t row, bool eligible, bool& owned, bool deferRestore = false);
bool BcCloneRequested();
void SetBcNeutralReady(bool (*probe)());
// Existing two-player/private-status/neutral-input prerequisites, without a party row write.
bool MissingPartyReady();
constexpr std::uint8_t AUTO_CLONE_WORLDS[] = {2, 4, 5}; // TT, HB (including GoA), BC
bool BcCloneWorld(std::uint8_t world);
bool BcCloneApplied(std::uint8_t world);
void UpdateBcClone(bool resolving = false); // game frame and pre-resolver
// VUH-2679: what the party resolver last built with, for the transition and world it ran in: the clone row (the
// two-player layout, no native third party slot) or not. True only for exactly that transition and world.
bool BcCloneBuiltFor(std::uint32_t transition, std::uint8_t world);
// VUH-2679: a client whose current room was built with the clone layout, once the roster has three seats, asks the
// relay for the join replay of its admitted room (RoomReplayRequest) and reloads it natively; once per build, on a
// positively safe field frame (RebuildSafeField). Host and the seats built natively never ask.
struct CloneRebuildFacts {
    bool client = false;          // a client seat, arrived in the host's current epoch
    bool builtWithClone = false;  // this room's own load resolved the party with the clone row (BcCloneBuiltFor)
    bool threeSeats = false;      // the host and both friend connections are present
    bool requested = false;       // a replay was already requested for this build (this transition)
    bool settledField = false;    // RebuildSafeField now
};
enum class CloneRebuildStep : std::uint8_t { None, Request };
inline CloneRebuildStep CloneRebuildDecide(const CloneRebuildFacts& f, const char*& why) {
    if (!f.client) { why = "not-client"; return CloneRebuildStep::None; }
    if (!f.builtWithClone) { why = "native-layout"; return CloneRebuildStep::None; }
    if (!f.threeSeats) { why = "two-seats"; return CloneRebuildStep::None; }
    if (f.requested) { why = "requested"; return CloneRebuildStep::None; }
    if (!f.settledField) { why = "deferred"; return CloneRebuildStep::None; }
    why = "clone-outgrown"; return CloneRebuildStep::Request;
}
// Quill r1 R4 on 07260e4: the replay is a join replay, not a recovery of combat, boss or script state, so the rebuild
// runs only at a positively qualified quiet field: controllable, in field, menu closed, no transition pending, no event
// or cutscene, no battle, this seat not logically downed, no host event hold and no owned input hold, and an ordinary
// living population (eventhold::RecoverySnapshotSupported's rule: types 3/4, not Thorn, no dead rows). Otherwise it
// waits; an outgrown room that never qualifies is rebuilt natively by the next host room.
struct RebuildSafeFacts {
    bool gameplay = false;       // SafeNativeGameplay: controllable, field, menu closed, no transition pending
    bool noEvent = false, noCutscene = false, noBattle = false, notDowned = false;
    bool noHostHold = false, noInputHold = false, ordinaryPopulation = false;
};
inline bool RebuildSafeField(const RebuildSafeFacts& f, const char*& why) {
    if (!f.gameplay) { why = "gameplay"; return false; }
    if (!f.noEvent) { why = "event"; return false; }
    if (!f.noCutscene) { why = "cutscene"; return false; }
    if (!f.noBattle) { why = "battle"; return false; }
    if (!f.notDowned) { why = "downed"; return false; }
    if (!f.noHostHold) { why = "host-hold"; return false; }
    if (!f.noInputHold) { why = "input-hold"; return false; }
    if (!f.ordinaryPopulation) { why = "population"; return false; }
    why = "safe"; return true;
}
// While a replay is awaited (lead #269): a lost request is retried once after REPLAY_TIMEOUT_MS; after a second
// timeout it is abandoned. The seat never leaves its admitted epoch while waiting.
constexpr std::uint64_t REPLAY_TIMEOUT_MS = 10000;
constexpr unsigned REPLAY_ATTEMPTS = 2;
enum class ReplayWait : std::uint8_t { Waiting, Retry, Abandon };
inline ReplayWait CloneReplayWait(std::uint64_t elapsedMs, unsigned attempts) {
    if (elapsedMs < REPLAY_TIMEOUT_MS) return ReplayWait::Waiting;
    return attempts < REPLAY_ATTEMPTS ? ReplayWait::Retry : ReplayWait::Abandon;
}
// Quill r1 R2/R3 on 07260e4: one pending rebuild, bound to the exact admitted identity it was asked for (the WorldBridge
// session and generation, this seat's and the host's connections, the host's delivery serial, the epoch and the build's
// transition). Any change of that identity (a world-session retirement or replacement, a new epoch, a new load) drops it:
// nothing of an old request is retried, re-acked or accepted under a new identity. Once this seat has acked arrived=false
// it owes the relay arrived=true: only a committed replay (whose load then acks arrival) or an explicit AckTrue step closes
// that, never a silent reset under the same identity.
struct RebuildIdentity {
    std::uint64_t session = 0, connection = 0, hostConnection = 0, hostDelivery = 0;
    std::uint32_t generation = 0, epoch = 0, transition = 0;
    bool operator==(const RebuildIdentity&) const = default;
};
enum class RebuildPhase : std::uint8_t { Idle, AckFalse, Request, Awaiting, AckTrue };
struct RebuildMachine {
    RebuildPhase phase = RebuildPhase::Idle;
    RebuildIdentity id;
    std::uint64_t phaseMs = 0;
    unsigned attempts = 0;
    std::uint32_t seq = 0;                  // monotonic for this author; survives every reset
    std::uint32_t requestedTransition = 0;  // the build a request was started for: once per build
    void Reset() { phase = RebuildPhase::Idle; id = {}; phaseMs = 0; attempts = 0; }
};
struct RebuildTick {
    CloneRebuildFacts facts;  // requested is derived from the machine
    bool safe = false;        // RebuildSafeField now
    RebuildIdentity now;
    std::uint64_t nowMs = 0;
};
// One frame. ack(bool arrived) and request(seq) report whether the bridge took the packet (false: ring pressure, the
// next frame tries again). Returns what happened, for the log ("" when nothing).
template<class Ack, class Request>
const char* StepCloneRebuild(RebuildMachine& m, const RebuildTick& t, Ack&& ack, Request&& request) {
    if (m.phase != RebuildPhase::Idle && !(m.id == t.now)) { m.Reset(); return "identity-changed"; }
    switch (m.phase) {
    case RebuildPhase::Idle: {
        auto f = t.facts; f.requested = m.requestedTransition && m.requestedTransition == t.now.transition;
        f.settledField = t.safe;
        const char* why = "";
        if (CloneRebuildDecide(f, why) != CloneRebuildStep::Request) return "";
        m.phase = RebuildPhase::AckFalse; m.id = t.now; m.phaseMs = t.nowMs; m.attempts = 0;
        m.requestedTransition = t.now.transition;
        [[fallthrough]];
    }
    case RebuildPhase::AckFalse:
        if (!t.safe) { m.Reset(); return "unsafe-before-ack"; }  // nothing published yet: no obligation
        if (!ack(false)) return "";
        m.phase = RebuildPhase::Request; m.phaseMs = t.nowMs;
        [[fallthrough]];
    case RebuildPhase::Request:
        if (!t.safe || t.nowMs - m.phaseMs >= REPLAY_TIMEOUT_MS) { m.phase = RebuildPhase::AckTrue; return "abandoning"; }
        if (!request(m.seq + 1)) return "";
        ++m.seq; m.attempts = 1; m.phase = RebuildPhase::Awaiting; m.phaseMs = t.nowMs;
        return "requested";
    case RebuildPhase::Awaiting:
        if (!t.safe) { m.phase = RebuildPhase::AckTrue; return "abandoning"; }
        switch (CloneReplayWait(t.nowMs - m.phaseMs, m.attempts)) {
        case ReplayWait::Waiting: return "";
        case ReplayWait::Retry:
            if (!request(m.seq + 1)) return "";
            ++m.seq; ++m.attempts; m.phaseMs = t.nowMs; return "retried";
        case ReplayWait::Abandon: m.phase = RebuildPhase::AckTrue; return "abandoning";
        }
        return "";
    case RebuildPhase::AckTrue:
        if (!ack(true)) return "";
        m.Reset(); return "abandoned";
    }
    return "";
}
// The replayed room: accepted at the same epoch only for this machine's own pending request, under the identity it was
// asked for, and only while the field is still safe (an unsafe accept closes the obligation instead).
enum class ReplayAdmission : std::uint8_t { NotOurs, Accept, RefuseUnsafe };
inline ReplayAdmission AdmitReplay(const RebuildMachine& m, const RebuildIdentity& now, std::uint32_t roomEpoch, bool safe) {
    if ((m.phase != RebuildPhase::Request && m.phase != RebuildPhase::Awaiting) || !(m.id == now) || !roomEpoch ||
        roomEpoch != m.id.epoch) return ReplayAdmission::NotOurs;
    return safe ? ReplayAdmission::Accept : ReplayAdmission::RefuseUnsafe;
}
// After an Accept: consumed only if the host transition was actually queued; a context or queue refusal (or an unsafe
// accept) goes to the explicit AckTrue step that closes the arrived=false obligation.
inline void CommitReplay(RebuildMachine& m, bool queued) {
    if (queued) m.Reset(); else m.phase = RebuildPhase::AckTrue;
}

// Reads KH2COOP_PLAYER_KIT. Unset/0: returns true without hooking. Unknown
// kit or mismatched 3E2EB0 bytes: logs, returns false, no hook.
bool Install(std::uintptr_t exeBase, LogFn log);
// Disables the hook, then restores member 0 if it still holds our kit.
void Shutdown();
void StopResolver(); // disable shared load hook before party member restoration
Stats GetStats();
// Roster byte for the actor's actual descriptor (actor+0x918 -> objentry id).
std::uint8_t RosterForActor(std::uintptr_t actor);
// True once a kit was requested (non-"0" env): only then may callers read the roster.
bool KitRequested();
// Party kits: the roster byte this machine streams in AvatarState.character. In the combined
// party-kits path it is the CHOSEN kit (stable per launch; peers build our clone from it);
// otherwise the actual descriptor of `actor` (VUH-1513).
std::uint8_t StreamRoster(std::uintptr_t actor);
// Party kits: this machine's own player kit as an objentry (Sora 0x54 when no kit is installed).
std::uint16_t LocalKit();
// Party kits: KH2COOP_PLAYER_KIT is installed together with KH2COOP_PARTY_NATIVE (KH2COOP_PARTY_KITS=1).
bool PartyKitsCombined();

// VUH-1519 flag matrix: KH2COOP_PARTY_NATIVE is mutually exclusive with KH2COOP_PLAYER_KIT and
// KH2COOP_REMOTE_KIT_SLOT. "Set" = present, non-empty and not exactly "0" (over-long counts as set).
// Every conflicting combination refuses each side that conflicts (fail closed, logged).
// Party kits: KH2COOP_PARTY_KITS lifts ONLY the PARTY_NATIVE/PLAYER_KIT exclusion (the combined
// path); REMOTE_KIT_SLOT stays exclusive. PartyNative requires PARTY_KITS to be exactly "1".
struct FlagMatrix { bool kit = false, remote = false, party = false, partyKits = false; };
bool FlagSet(const char* text);
// Pure: the flags `party` conflicts with (bit0 kit, bit1 remote); 0 when none or party unset.
unsigned PartyConflicts(const FlagMatrix& f);
// Native: reads the three environment variables (no game memory).
FlagMatrix ReadFlagMatrix();

// VUH-1519: a second consumer of the same per-load resolver post-hook (MinHook
// allows one hook per target). Hook order per load, one SEH scope: local kit,
// then remote kit member, then the observer. The observer runs on the loading
// thread, must not log or allocate; its log callback runs after the kit and
// remote lines, outside SEH. Must be set before Install: with an observer set,
// Install hooks 3E2EB0 even when no kit is requested, and refuses to combine
// it with a kit or remote mode.
using ResolveObserver = void (*)(std::uint16_t* resolved, const LoadContext& context);
using ResolveObserverLog = void (*)(LogFn log);
void SetResolveObserver(ResolveObserver observer, ResolveObserverLog log);

// Puppet collision guard (lead decision, VUH-1513): a selector-0 friend puppet
// resolves through the same member 0, so with any non-zero KH2COOP_PLAYER_KIT
// (even a refused one) the native-Sora clone puppet path must refuse instead of
// driving a kit clone. Per-puppet member slots are VUH-1519's job.
bool BlocksNativeSoraPuppets();
// Game thread: called when a player-class clone was refused; logs once per area load.
void NoteRefusedClone(std::uintptr_t actor);

// ---------------------------------------------------------------- remote kit (VUH-1513 step 3, rev 4)
// Receiving side, default OFF (KH2COOP_REMOTE_KIT_SLOT=1, refused while a local kit is set).
// Layout: the rig/party setup writes Friend1 selector 3 (GoA row 00/03/02/12; 3 is the native
// "world ally" member, unused while the world slot is 0x12). With two player-class spawns the
// game builds the player-slot actor (member 0) FIRST (raw566) and the Friend1 actor (member 3)
// LAST (raw567); every player-class constructor stores [exe+0x2A105D0], so the LAST one becomes
// the canonical player and the first one becomes the clone the puppet driver binds
// (VUH-1489 actor-fix; live VUH-1513 attempt 03). Therefore, after each area load:
//   member 0 (feeds the clone = puppet target) := remote kit (0x5A for roster 1, else Sora 0x54)
//   member 3 (feeds the receiver's own player) := Sora 0x54
constexpr std::uint8_t FRIEND1_SELECTOR = 3;
constexpr std::uint8_t PUPPET_TARGET_MEMBER = 0;           // raw566 -> clone -> puppet target
constexpr std::uint8_t OWN_PLAYER_MEMBER = FRIEND1_SELECTOR; // raw567 -> canonical player
constexpr std::uint64_t RVA_SAVE = 0x9A98B0, PARTY_ROWS_OFFSET = 0x3534;
enum class RemoteReason : std::uint8_t {
    Applied, WorldNotQualified, EventRoom, EventActive, RowNotRemoteLayout, NativeNotSora, ChangedUnderUs
};
struct RemoteValues { std::uint16_t member0 = 0, member3 = 0; };
struct RemoteContext {
    LoadContext load{};        // load.resolved0 = what 3E2EB0 resolved for member 0
    std::uint8_t row[4]{};     // SAVE party row for the current world
    std::uint16_t native3 = 0; // what 3E2EB0 resolved for member 3
    std::uint8_t roster = 0;   // latest streamed roster byte for puppet 0
};
// Pure: on Applied, *set holds the two member values to write.
RemoteReason DecideRemote(const RemoteContext& c, RemoteValues* set);
const char* RemoteReasonName(RemoteReason r);
RemoteReason ApplyRemote(const RemoteContext& c, std::uint16_t* resolved, RemoteValues* original, RemoteValues* set);
// Restores each member only while it still holds the value we wrote.
bool RestoreRemote(const RemoteValues& set, const RemoteValues& original, bool recorded, std::uint16_t* resolved);
bool RemoteEnvRequested(const char* text);
// True once remote mode is installed (env set, no local kit, hook live).
bool RemoteKitMemberActive();
// Owner thread: the latest validated pose roster byte for puppet `index` (only 0 is used).
void NoteRemoteRoster(int index, std::uint8_t roster);

} // namespace kh2coop::inject::playerkit
