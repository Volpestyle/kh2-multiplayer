#pragma once
// PartyNative pure policy and layout-facing API (VUH-1519); see PartyNative.hpp.
#include "PartyNative.hpp"
#include "kh2coop/PartyLayout.hpp"
#include <array>
#include <cstdint>

namespace kh2coop::inject::partynative {

// ---------------------------------------------------------------- pure policy
enum class Plan : std::uint8_t {
    None,         // no accepted layout: native party
    Unsupported,  // accepted, but not the qualified case: native party
    TwoClones     // both native friend seats are remote-player Sora clones
};
const char* PlanName(Plan p);

// Machine-local projection. Host-view seats are translated so this machine's
// own player stays the canonical (last-built) player; the two friend seats
// then hold the other two network slots. Only rule DEFAULT with every seat a
// player is TwoClones.
Plan Project(const PartyLayout& layout, std::uint8_t localSlot);

// Standing intent (lead decision, scoped to this GoA-only candidate): the newest
// accepted layout of this session generation, roster and host connection,
// pinned to the layout's own world/room/evt. Applies only at a load of that
// same room (epoch and door ignored, so a same-room reload matches). A
// room-independent PartyIntent message is required before a second room.
struct Intent {
    std::uint32_t generation = 0;
    std::array<std::uint64_t, 3> roster {};
    std::uint8_t localSlot = 0xFF;
    std::uint64_t version = 0; // version floor for this generation and host connection
    Plan plan = Plan::None;
    std::uint16_t world = 0xFFFF, room = 0xFFFF, evt = 0xFFFF;
};
enum class Accept : std::uint8_t { Accepted, NoGeneration, RosterMismatch, LocalSlot, StaleVersion, Invalid };
const char* AcceptName(Accept a);
Accept AcceptLayout(Intent& intent, const PartyLayout& layout, std::uint8_t localSlot,
                    std::uint32_t generation, const std::array<std::uint64_t, 3>& roster);
// generation 0 means "unknown": the intent is held. It is cleared on a different
// non-zero generation, a roster or local-slot change, or the bridge closing.
// True when a plan was cleared.
bool ObserveSession(Intent& intent, std::uint32_t generation, const std::array<std::uint64_t, 3>& roster,
                    std::uint8_t localSlot, bool bridgeOpen);
// Relay-authored PartyReapply. StoryForced/RosterChanged clear the plan (version
// floor kept and raised to afterVersion); RoomChanged/HostChoice are logged only
// (the room pin already refuses another room).
enum class Reapply : std::uint8_t { Cleared, LoggedOnly, OtherGeneration };
const char* ReapplyName(Reapply r);
Reapply ApplyReapply(Intent& intent, const PartyReapply& reapply, std::uint32_t generation);

enum class Load : std::uint8_t {
    Applied, NoIntent, Unsupported, WorldNotQualified, EventRoom, EventActive, IntentOtherRoom,
    RowNotDefault, NativeNotDefault, ChangedUnderUs, ReadFault, PrivateStatusUnavailable,
    NeutralInputUnavailable
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
    std::array<std::uint8_t, 4> row {};
    std::uint16_t resolved[3] {}; // what 3E2EB0 just resolved for members 0..2
    Plan plan = Plan::None;
    std::uint16_t intentWorld = 0xFFFF, intentRoom = 0xFFFF, intentEvt = 0xFFFF;
};
Load Decide(const LoadInput& in);
struct Originals { bool valid = false; std::uint16_t member1 = 0, member2 = 0; };
// Applies to a real or synthetic resolved array. On Applied writes members 1/2
// only, after re-reading that they still hold the decided native values.
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
};
bool HostShouldPublish(const HostPublished& last, std::uint32_t generation, const RoomTransition& location,
                       const std::array<std::uint64_t, 3>& roster, bool rowRead,
                       const std::array<std::uint8_t, 4>& row, std::uint64_t nowMs, PartyApplyReason& reason);
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
// Game thread, host role only: returns a layout to publish, or false.
bool HostLayoutToPublish(std::uint32_t generation, const RoomTransition& location,
                         const std::array<std::uint64_t, 3>& roster, PartyLayout& out);
// After a successful enqueue; records it for dedupe/retry, does not adopt it.
void NoteHostSent(const PartyLayout& layout, std::uint32_t generation);
#endif

} // namespace kh2coop::inject::partynative
