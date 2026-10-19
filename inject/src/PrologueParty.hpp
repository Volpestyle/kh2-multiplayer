#pragma once
// ============================================================================
// PrologueParty (VUH-2464): two players in the Roxas prologue from the first
// controllable frame. Each seat declares its profile (default Roxas) and
// acknowledges the roster's profiles through SeatState as soon as its runtime
// connects, during the opening's events. At each checked prologue field site
// (kh2coop/RouteSpawnPolicy.hpp MissingPartySites) the missing-party ingress
// builds the remote's clone first and the native story local last; control is
// held, with a logged reason, until both seats report that binding at the same
// location. No party row, member table, SAVE or story script is written.
// Policy: kh2coop/PrologueSeats.hpp. Game thread unless noted.
// ============================================================================
#include <array>
#include <cstdint>

// No Protocol.hpp here: EntityHook includes HitChannel.hpp, which redefines HitClaim.
namespace kh2coop { struct SeatState; }
namespace kh2coop::inject::prologueparty {

using LogFn = void (*)(const char* fmt, ...);
void Install(std::uintptr_t exeBase, LogFn log);

// Once per frame from enemysync: the armed session (generation 0 = none), roster and local slot.
// Returns true with `out` when this seat's report is due (first send, change or unechoed retry).
bool Tick(std::uint32_t generation, const std::array<std::uint64_t, 3>& roster, std::uint8_t localSlot,
          std::uint64_t nowMs, SeatState& out);
void NoteSent(const SeatState& sent, std::uint64_t nowMs);
// An admitted SeatState (relay-stamped owner slot; the author's own echo included).
void Receive(const SeatState& m);

// Missing-party ingress at a prologue site (load time, game thread): the clone's object, or false
// with a hold reason when the roster's profiles are not agreed.
bool CloneProfile(std::uint16_t& object, const char*& reason);
// The construction outcome at `location` for this load: bound clone/local, or a refusal reason.
void NoteConstruction(const std::array<std::uint8_t, 10>& location, bool bound, const char* reason,
                      std::uintptr_t clone = 0);

// Any thread: the Roxas prologue is live here (seats requested, NOW in TT and the resolved member 0
// is native story Roxas). Puppets then drive only bound missing-party clones: the prologue has no
// party, and its events build their own player-class Roxas actors that must never be driven.
bool PrologueScope();
// Input collector (any thread): neutralize slot-0 raw input while the published hold applies.
// True when this call held the input.
bool HoldInput(void* inputStruct);
// Diagnostics/fixtures: the current published hold (0 = released or not applicable).
std::uint8_t PublishedHold();
// VUH-2486, any thread: the on-screen note for a session that started at an unchecked prologue field.
enum class UnpinnedNotice : std::uint8_t { None, Holding, PlayingSolo };
UnpinnedNotice Notice();

} // namespace kh2coop::inject::prologueparty
