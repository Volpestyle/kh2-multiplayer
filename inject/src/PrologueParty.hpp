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
// with a hold reason when the roster's profiles are not agreed. VUH-2911: `encounterBody` is the site's
// (RouteSpawnPolicy); a default-Roxas remote then builds as that body (prologue::SiteBody).
bool CloneProfile(std::uint16_t encounterBody, std::uint16_t& object, const char*& reason);
// VUH-1512: the body this seat's own local player takes at a prologue site (its agreed profile, or the site's
// encounter body for a default-Roxas seat, and that body's status key), or false before agreement.
bool LocalBody(std::uint16_t encounterBody, std::uint16_t& object, std::uint16_t& statusKey);
// VUH-1512, member resolver post-hook (the area load's loading thread): at a checked prologue site, a seat
// whose body is not the site's story body (Roxas, or Axel II's dual-wield Roxas) gets its body as resolved
// member 0, so the authored player record builds it. Scripted events and every other load keep the native member.
void ResolveLocal(std::uintptr_t resolvedMembers);
// VUH-1512, missing-party ingress after a bound construction (game thread): a clone of this seat's own body
// takes its owner seat's color through the actor's native model color bytes (+0x4D8..0x4DA, read each
// frame by the render setup FUN_1403CC3D0; alpha +0x4DB untouched). Nothing else is written.
void ColorDuplicate(std::uintptr_t clone, std::uint16_t cloneObject, std::uint16_t localObject);
// The construction outcome at `location` for this load: bound clone/local, or a refusal reason.
void NoteConstruction(const std::array<std::uint8_t, 10>& location, bool bound, const char* reason,
                      std::uintptr_t clone = 0);

// Any thread: the Roxas prologue is live here (seats requested, NOW in TT and the resolved member 0
// is native story Roxas, the encounter body of the checked site at this NOW (VUH-2911), or this seat's
// chosen body that ResolveLocal set for this location). Puppets then drive only bound missing-party clones: the prologue has no
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
// VUH-1512, any thread: the declared profile this seat's prologue hold refuses (0 when none), for the
// on-screen note while both seats hold at a checked site.
std::uint16_t RefusedProfile();
// VUH-1512, any thread: false until the seats' profiles are declared and acknowledged on this world-session
// generation's roster (a reconnect starts a new one). A client's host-led load into a prologue site waits
// for it, so that load builds this seat's own body and the clone from the agreed pair.
bool ProfilesSettled(std::uint32_t generation);
// VUH-1512, any thread: while this seat waits alone at a checked site (prologue::SoloWait), the seconds left
// and the last known display name of the seat it waits for (empty when unknown). False when not waiting.
bool SoloWaiting(std::array<char, 24>& name, std::uint32_t& secondsLeft);
// Any thread: the session's latest display names (HUD roster), remembered per connected slot for SoloWaiting.
void NoteRosterNames(const std::array<std::uint64_t, 3>& connections, const std::array<std::array<char, 24>, 3>& names);

} // namespace kh2coop::inject::prologueparty
