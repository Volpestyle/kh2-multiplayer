#pragma once
// PartyNative pure policy and layout-facing API (VUH-1519); see PartyNative.hpp.
#include "PartyNative.hpp"
#include "kh2coop/PartyLayout.hpp"
#include <array>
#include <atomic>
#include <cstdint>

namespace kh2coop::inject::partynative {

// ---------------------------------------------------------------- pure policy
enum class Plan : std::uint8_t {
    None,         // no accepted layout: native party
    Unsupported,  // accepted, but not the qualified case: native party
    TwoClones,    // both native friend seats are remote-player clones (three players)
    OneClone,     // two players: member 0 = the other player's clone, member 1 = native Goofy, member 2 = own kit
    OneCloneEmpty, // two players: member 0 = remote clone, member 1 = absent (0), member 2 = own kit
    OneCloneDonald, // two players: remote clone, native Donald, canonical own kit
    SoloDonald,    // one player: canonical own kit, native Donald, absent
    SoloGoofy      // one player: canonical own kit, native Goofy, absent
};
// A plan that spawns clones, and how many.
constexpr bool IsOneClone(Plan p) { return p == Plan::OneClone || p == Plan::OneCloneEmpty || p == Plan::OneCloneDonald; }
constexpr bool ClonePlan(Plan p) { return p == Plan::TwoClones || IsOneClone(p); }
constexpr unsigned PlanClones(Plan p) { return p == Plan::TwoClones ? 2u : IsOneClone(p) ? 1u : 0u; }
constexpr bool SoloPlan(Plan p) { return p == Plan::SoloDonald || p == Plan::SoloGoofy; }
constexpr bool SupportedPlan(Plan p) { return ClonePlan(p) || SoloPlan(p); }
constexpr bool EmptyPlan(Plan p) { return p == Plan::OneCloneEmpty || SoloPlan(p); }
constexpr std::uint16_t CompanionMember(Plan p) {
    return p == Plan::OneCloneDonald || p == Plan::SoloDonald ? DONALD : p == Plan::OneClone || p == Plan::SoloGoofy ? GOOFY : 0;
}
enum class HostAIChoice : std::uint8_t { Default, None, Donald, Goofy };
const char* PlanName(Plan p);

// Machine-local projection. Host-view seats are translated so this machine's
// own player stays canonical (last-built with clones, native member0 in solo); the two friend seats
// then hold the other two network slots. Only rule DEFAULT with every seat a
// player is TwoClones.
// Two players: remote in seat 1, Goofy in seat 2 is OneClone; replacing Goofy
// with Empty is OneCloneEmpty.
// Donald in seat 2 is OneCloneDonald. Remote/AI seat permutations remain Unsupported.
// One host with an explicit chosen AI in seat 1 and Empty seat 2 is SoloDonald/SoloGoofy.
Plan Project(const PartyLayout& layout, std::uint8_t localSlot);
// Host opt-in: preserve the default authoring contract; only Default with two players
// changes Goofy seat 2 to Empty. Other rules and three-player layouts are unchanged.
std::optional<PartyLayout> AuthorLayout(RoomTransition room, std::uint64_t version,
        PartyApplyReason reason, PartyRule rule, std::uint32_t forcedAllyObject,
        const std::array<std::uint64_t,3>& roster, std::array<std::uint8_t,2> priority={1,2}, bool noAI=false);
// Chosen Donald/Goofy changes two-player AI seat 2 and admits explicit host-only
// [Local, chosen AI, Empty]. Native application still needs the missing-seat gates.
std::optional<PartyLayout> AuthorLayout(RoomTransition room, std::uint64_t version,
        PartyApplyReason reason, PartyRule rule, std::uint32_t forcedAllyObject,
        const std::array<std::uint64_t,3>& roster, std::array<std::uint8_t,2> priority, HostAIChoice choice);

// Standing intent (lead decision, scoped to this GoA-only candidate): the newest
// accepted layout of this session generation, roster and host connection,
// pinned to the layout's own world/room/evt. Applies only at a load of that
// same room (epoch and door ignored, so a same-room reload matches).
// VUH-1786: one stored host PartyIntent per target room (room-independent), so the
// first load of another pinned room needs no extra reload.
struct StoredIntent {
    bool valid = false;
    PartyIntentTarget target {};
    Plan plan = Plan::None;
    std::uint64_t version = 0, seq = 0; // seq: local acceptance order (shared with the layout)
};
constexpr unsigned MAX_INTENT_TARGETS = 4;
// VUH-1786 rev2 (B1): a story-forced reapply wins exactly one load of its room. The hold is
// selected like a plan (newest seq wins) and resolves natively; it is released after one load
// of that room (any evt), or when a newer layout or intent for that room is accepted.
struct StoryHold {
    bool valid = false;
    std::uint16_t world = 0xFFFF, room = 0xFFFF, evt = 0xFFFF;
    std::uint64_t seq = 0;
};
struct Intent {
    std::uint32_t generation = 0;
    std::array<std::uint64_t, 3> roster {};
    std::uint8_t localSlot = 0xFF;
    std::uint64_t version = 0; // layout version floor for this generation and host connection
    Plan plan = Plan::None;
    std::uint16_t world = 0xFFFF, room = 0xFFFF, evt = 0xFFFF;
    // VUH-1786
    std::array<StoredIntent, MAX_INTENT_TARGETS> targets {};
    std::uint64_t intentVersion = 0; // PartyIntent floor (its own namespace)
    std::uint64_t layoutSeq = 0, seq = 0; // seq: monotonic for the process (kept across every reset)
    StoryHold hold {};
    // Party kits: each network slot's kit (objentry), from the newest accepted intent of this
    // roster. Kits belong to the roster, not the room: layout plans borrow them. Rebase clears.
    std::array<std::uint16_t, 3> slotKits {};
    bool kitsKnown = false;
};
// Party kits: the two other network slots of `localSlot`, ascending (= puppet 0, puppet 1 = the clone
// kits written to members 0, 1; rev3).
std::array<std::uint8_t, 2> OtherSlots(std::uint8_t localSlot);
// 3-bit codes in the plan key: a qualified kit's roster code (kh2coop/PlayerKits.hpp), 0 = legacy Sora,
// KIT_CODE_INVALID (7) for anything else. (Was 2 bits with 3 = invalid: collided with roster 3, Mickey.)
constexpr std::uint8_t KIT_CODE_INVALID = 7;
std::uint8_t KitCode(std::uint16_t kit);
std::uint16_t KitFromCode(std::uint8_t code);
// True when a layout plan, a stored intent or a story hold exists.
bool HasPlans(const Intent& intent);
unsigned IntentCount(const Intent& intent);
// S4: re-keys the store to (generation, roster, localSlot). Same key: no-op. Same generation and host
// connection: layout plan, intents and hold retired, both version floors kept. Otherwise the floors
// reset too. seq is always kept. True when a plan, intent or hold was retired.
bool Rebase(Intent& intent, std::uint32_t generation, const std::array<std::uint64_t, 3>& roster, std::uint8_t localSlot);
// Pinned qualified rooms (authored rule DEFAULT, no evt program 0 Party opcode, world-4 row
// 00/01/02/12): GoA 04/1A and 04/0A (hb10), both at evt 0.
bool QualifiedRoom(std::uint16_t world, std::uint16_t room, std::uint16_t evt);
// The plan for a PartyIntent on this machine (same projection as a layout).
// Party kits: a non-Sora kit is Unsupported unless `kitsAllowed` (KH2COOP_PARTY_KITS on this machine).
Plan ProjectIntent(const PartyIntent& intent, std::uint8_t localSlot, bool kitsAllowed = false);
// Plan source table published to the loading thread: entry 0 = the room-pinned layout, entry 1 =
// the story hold, entries 2.. = stored intents. key = plan | world<<8 | room<<16 | evt<<24 | source<<40;
// seq is the full 64-bit local acceptance order (S3: no truncation).
enum class PlanSource : std::uint8_t { None = 0, Layout = 1, Intent = 2, Hold = 3 };
const char* PlanSourceName(PlanSource s);
constexpr unsigned PLAN_TABLE = 2 + MAX_INTENT_TARGETS;
struct PlanEntry { std::uint64_t key = 0, seq = 0; };
using PlanTable = std::array<PlanEntry, PLAN_TABLE>;
// Party kits: key bits 48-50 / 51-53 / 54-56 = kit codes of clone 1 (member 0) / clone 2 (member 1) / this
// machine's own kit (expected in member 0 at entry, written to member 2) (from intent.slotKits). In kitsMode a
// layout plan with no kits yet is Unsupported.
void PackPlanTable(const Intent& intent, PlanTable& out, bool kitsMode = false);
struct PlanChoice {
    Plan plan = Plan::None; std::uint16_t world = 0xFFFF, room = 0xFFFF, evt = 0xFFFF;
    PlanSource source = PlanSource::None; std::uint64_t seq = 0;
    std::uint16_t kit1 = SORA, kit2 = SORA, localKit = SORA; // party kits (Sora when off)
};
// The most recently accepted entry for the load's room (a hold matches its world/room at any evt;
// it selects Plan::None); if none, the layout entry (so Decide reports intent-other-room), else none.
PlanChoice SelectPlan(const PlanTable& table, std::uint16_t world, std::uint16_t room, std::uint16_t evt);
// S3: the table is published as one set. Seqlock: one writer (game thread), any reader (loading
// thread). A reader that cannot get a stable copy in maxTries gets an empty table (native load).
class PlanTableCell {
public:
    void Publish(const PlanTable& table, std::uint32_t generation = 0); // single writer
    bool Read(PlanTable& out, unsigned maxTries = 64, std::uint32_t* generation = nullptr) const;
    std::uint32_t Version() const { return version_.load(std::memory_order_acquire); }
private:
    std::atomic<std::uint32_t> generation_ {0};
    std::atomic<std::uint32_t> version_ {0}; // odd while a write is in progress; full 32-bit, wraps
    std::array<std::atomic<std::uint64_t>, 2 * PLAN_TABLE> words_ {};
};
// B1: release the story hold once the loading thread has used it (consumedSeq == hold.seq).
bool ReleaseHold(Intent& intent, std::uint64_t consumedSeq);
// True when a story hold covers this world/room (any evt).
bool HoldCovers(const Intent& intent, std::uint16_t world, std::uint16_t room);
// Rev3 F1: the visit a story hold resolved. After the host's held load of R, the host publishes no layout
// or intent for R until a new epoch of R (or another room): a host post-load layout would otherwise clear
// the clients' holds before their own load (asymmetric session). epoch 0 = not yet seen.
struct StoryVisit { bool valid = false; std::uint16_t world = 0xFFFF, room = 0xFFFF; std::uint32_t epoch = 0; };
// Host publication gate: true while a hold covers `location`'s room, or for the held visit itself (the
// first epoch of that room seen after the release). A later epoch or another room ends the visit.
bool StorySuppresses(StoryVisit& visit, const Intent& intent, const RoomTransition& location);
bool StoryVisitCovers(const StoryVisit& visit, std::uint16_t world, std::uint16_t room);

enum class Accept : std::uint8_t { Accepted, NoGeneration, RosterMismatch, LocalSlot, StaleVersion, Invalid };
const char* AcceptName(Accept a);
Accept AcceptLayout(Intent& intent, const PartyLayout& layout, std::uint8_t localSlot,
                    std::uint32_t generation, const std::array<std::uint64_t, 3>& roster);
// VUH-1786: store one host intent per target (own version floor; same session rules as a layout).
Accept AcceptIntent(Intent& intent, const PartyIntent& m, std::uint8_t localSlot,
                    std::uint32_t generation, const std::array<std::uint64_t, 3>& roster, bool kitsAllowed = false);
// B2: an echo marks the host's publication echoed only when it was adopted (Accepted), or refused
// as StaleVersion for exactly the version the host sent (already adopted).
bool EchoConfirms(Accept a, std::uint64_t echoedVersion, std::uint64_t sentVersion);
// Rev3 N-a: an own intent echo confirms only when the store now holds that target at the version sent.
bool IntentEchoConfirms(const Intent& intent, Accept a, const PartyIntentTarget& target, std::uint64_t sentVersion);
// generation 0 means "unknown": the intent is held. It is cleared on a different
// non-zero generation, a roster or local-slot change, or the bridge closing.
// True when a plan, intent or hold was cleared.
bool ObserveSession(Intent& intent, std::uint32_t generation, const std::array<std::uint64_t, 3>& roster,
                    std::uint8_t localSlot, bool bridgeOpen);
// Relay-authored PartyReapply. StoryForced/RosterChanged clear the layout plan (version
// floor kept and raised to afterVersion). StoryForced also sets a one-load hold on its room
// (intents kept); RosterChanged retires the intents and any hold. RoomChanged/HostChoice are
// logged only (the room pin already refuses another room).
enum class Reapply : std::uint8_t { Cleared, LoggedOnly, OtherGeneration };
const char* ReapplyName(Reapply r);
Reapply ApplyReapply(Intent& intent, const PartyReapply& reapply, std::uint32_t generation);

enum class Load : std::uint8_t {
    Applied, NoIntent, Unsupported, WorldNotQualified, EventRoom, EventActive, IntentOtherRoom,
    RowNotDefault, NativeNotDefault, ChangedUnderUs, ReadFault, PrivateStatusUnavailable,
    NeutralInputUnavailable, LocalKitMismatch, EmptySeatUnavailable, EmptyPackageUnqualified
};
const char* LoadName(Load r);
struct LoadInput {
    std::uint8_t world = 0, room = 0;
    std::uint16_t evtProgram = 0;
    std::uint64_t eventContext = 0;
    std::int32_t cutsceneState = 0;
    bool rowRead = false;
    bool privateStatusReady = false; // NativePrivateStatus armed (installed, no fault since)
    bool neutralInputReady = false;  // CloneNeutralInput configured (page + 0x3A89A0 hook), live
    bool emptySeatReady = false;       // all scoped native empty-row projections armed
    bool emptyPackageQualified = false; // selected load package excludes unsafe deferred empty aliases
    std::array<std::uint8_t, 4> row {};
    std::uint16_t resolved[3] {}; // what 3E2EB0 just resolved for members 0..2
    Plan plan = Plan::None;
    std::uint16_t intentWorld = 0xFFFF, intentRoom = 0xFFFF, intentEvt = 0xFFFF;
    PlanSource source = PlanSource::None; // VUH-1786: which stored plan this load uses
    // Party kits (rev3 mapping): kit1/kit2 = the clone kits written to members 0/1; localKit = this
    // machine's own kit, expected in member 0 at entry (PlayerKit) and written to member 2 (canonical).
    std::uint16_t kit1 = SORA, kit2 = SORA, localKit = SORA;
    // R3-1: member 0 as 3E2EB0 resolved it, BEFORE PlayerKit's kit write (0 = unknown: use resolved[0]).
    std::uint16_t native0 = 0;
};
Load Decide(const LoadInput& in);
// Rev3: the observer writes members 0, 1 (the clones, built first) and 2 (the canonical local, built
// last: raw568, live run 20261007-084248). set0/set1 = clone kits, set2 = this machine's own kit.
struct Originals { bool valid = false; std::uint16_t member0 = 0, member1 = 0, member2 = 0, set0 = 0, set1 = 0, set2 = 0; };
// Applies to a real or synthetic resolved array. On Applied writes members 0..2 (rev3: clones on 0/1, own
// kit on 2; kits off = members 1/2 only, all Sora), after re-reading that they still hold the decided values.
Load ApplyAfterResolve(const LoadInput& in, std::uint16_t* resolved, Originals& original);
// Restores each member only while it still holds our Sora; never overwrites a
// later native value. Clears `original`.
void RestoreMembers(Originals& original, std::uint16_t* resolved);

// Host producer policy: GoA 04/1A evt 0, three connections, native DEFAULT row,
// and a (generation, room tuple, roster) not yet published, or published but not
// echoed back by the relay within the retry interval.
constexpr std::uint64_t HOST_ECHO_RETRY_MS = 5000;
constexpr unsigned HOST_ECHO_MAX_RESENDS = 6; // then the host gives up loudly and stays native
struct HostPublished {
    std::uint32_t generation = 0;
    RoomTransition location {};
    std::array<std::uint64_t, 3> roster {};
    std::uint64_t version = 0, sentMs = 0;
    unsigned resends = 0; // re-sends of this same (generation, tuple, roster)
    PartyApplyReason reason = PartyApplyReason::HostChoice;
    bool any = false, echoed = false;
    std::array<std::uint16_t, 3> kits {}; // party kits: the kit vector this intent carried
};
// Party kits, host: kits[slot] from the own kit and the latched remote rosters. False while any
// remote slot's kit is unknown or unsupported (the host then publishes nothing in kits mode).
struct RemoteKits { std::array<bool, 2> seen {}; std::array<std::uint8_t, 2> roster {}; };
bool HostKits(std::uint16_t ownKit, std::uint8_t localSlot, const RemoteKits& remote, std::array<std::uint16_t, 3>& kits);
// Two players: only the CONNECTED remote slots of `roster` need a kit; an absent slot's kit is 0.
bool HostKits(std::uint16_t ownKit, std::uint8_t localSlot, const RemoteKits& remote, std::array<std::uint16_t, 3>& kits,
              const std::array<std::uint64_t, 3>& roster);
// The other network slots (ascending = puppet order) that are connected in `roster`; 0xFF pads.
std::array<std::uint8_t, 2> PresentOtherSlots(const std::array<std::uint64_t, 3>& roster, std::uint8_t localSlot);
// The puppet index (OtherSlots order) of the one connected other slot, or -1 (none or two).
int PresentIndex(const std::array<std::uint64_t, 3>& roster, std::uint8_t localSlot);
// VUH-1786 host: is the intent for a target due (first send, new generation/roster, or an unechoed retry)?
// Party kits: also due when `kits` differs from the vector last published for that target.
bool HostIntentDue(const HostPublished& last, std::uint32_t generation, const std::array<std::uint64_t, 3>& roster,
                   std::uint64_t nowMs, const std::array<std::uint16_t, 3>& kits = {}, bool soloAllowed = false);
bool HostShouldPublish(const HostPublished& last, std::uint32_t generation, const RoomTransition& location,
                       const std::array<std::uint64_t, 3>& roster, bool rowRead,
                       const std::array<std::uint8_t, 4>& row, std::uint64_t nowMs, PartyApplyReason& reason, bool soloAllowed = false);
// True when the same tuple was sent, never echoed, and the re-send cap is spent.
bool HostEchoGivenUp(const HostPublished& last, std::uint32_t generation, const RoomTransition& location,
                     const std::array<std::uint64_t, 3>& roster);
// The one authored rule this candidate pins (ARD hb26 has no evt program 0, so no
// Party opcode; live GoA row 00/01/02/12). Anything else is Unavailable.
PartyRule PinnedRule(const RoomTransition& location);

#ifndef KH2COOP_PARTYNATIVE_POLICY_ONLY
// Game thread. Client: an admitted host PartyLayout. Host: the relay's echo of its own
// layout (own=true); the host never adopts a layout the relay has not accepted.
void NoteLayout(const PartyLayout& layout, std::uint8_t localSlot, std::uint32_t generation,
                const std::array<std::uint64_t, 3>& roster, bool own);
// Game thread: an admitted relay-authored PartyReapply.
void NoteReapply(const PartyReapply& reapply, std::uint32_t generation);
// Game thread, every frame while requested.
void Observe(std::uint32_t generation, const std::array<std::uint64_t, 3>& roster, std::uint8_t localSlot,
             bool bridgeOpen);
// VUH-1786. Game thread. Client: an admitted host PartyIntent. Host: the relay's echo (own=true).
void NoteIntent(const PartyIntent& intent, std::uint8_t localSlot, std::uint32_t generation,
                const std::array<std::uint64_t, 3>& roster, bool own);
// Host, game thread: the next due intent for a pinned qualified room, or false.
bool HostIntentToPublish(std::uint32_t generation, const std::array<std::uint64_t, 3>& roster, PartyIntent& out);
void NoteHostIntentSent(const PartyIntent& intent, std::uint32_t generation);
// Game thread, host role only: returns a layout to publish, or false.
bool HostLayoutToPublish(std::uint32_t generation, const RoomTransition& location,
                         const std::array<std::uint64_t, 3>& roster, PartyLayout& out);
// After a successful enqueue; records it for dedupe/retry, does not adopt it.
void NoteHostSent(const PartyLayout& layout, std::uint32_t generation);
#endif

} // namespace kh2coop::inject::partynative
