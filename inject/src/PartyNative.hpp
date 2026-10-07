#pragma once
// ============================================================================
// PartyNative — native application of the host party layout (VUH-1519).
// Default OFF: nothing is hooked, read or written unless KH2COOP_PARTY_NATIVE=1.
//
// Minimal qualified case only: GoA (04/1A, NOW evt program 0, no event or
// cutscene), native DEFAULT party (save row 00/01/02/12, resolved members
// 1/2 = Donald 0x5C / Goofy 0x5D), three players present and the host layout
// putting a remote player in both friend seats. On such an area load the
// shared 3E2EB0 post-hook (PlayerKit.cpp) replaces resolved members 1 and 2
// with Sora (0x54), so Friend1/Friend2 spawn as player-class Sora clones and
// NativePrivateStatus gives both clones private status records. The selectors
// in the save-backed party row are never written; MEMT is never written.
// Every other layout, room or rule leaves the native resolution untouched.
//
// Timing: the resolver runs during the load, before the room-pinned
// PartyLayout for that room can exist. Application therefore uses the standing
// projection of the newest accepted layout in the same session generation and
// roster, pinned to that layout's world/room/evt and re-checked against the
// native rule read at this load (scoped lead decision; a PartyIntent message is
// required before a second room). The layout published after the load is
// compared with what was applied (confirm line).
// ============================================================================

// This header has no protocol dependency (EntityHook includes HitChannel.hpp,
// whose HitClaim conflicts with Protocol.hpp). Layout-facing policy and API:
// PartyNativePolicy.hpp.
#include <array>
#include <cstdint>

namespace kh2coop::inject::partynative {

using LogFn = void (*)(const char* fmt, ...);

constexpr std::uint16_t SORA = 0x54, DONALD = 0x5C, GOOFY = 0x5D;
constexpr std::uint16_t ROXAS = 0x5A; // party kits: the one qualified non-Sora seat kit (P_EX110)
constexpr std::uint64_t RVA_SAVE = 0x9A98B0, SAVE_PARTY_ROWS = 0x3534; // row = SAVE+0x3534+4*world
constexpr std::array<std::uint8_t, 4> DEFAULT_ROW {0, 1, 2, 0x12};

#ifndef KH2COOP_PARTYNATIVE_POLICY_ONLY
// Reads KH2COOP_PARTY_NATIVE. Must run before playerkit::Install (it registers
// the resolver observer there). Flag matrix (playerkit::ReadFlagMatrix): refuses
// while KH2COOP_PLAYER_KIT or KH2COOP_REMOTE_KIT_SLOT is set (each of those then
// refuses too). Also requires KH2COOP_NATIVE_SORA_PRIVATE_STATUS=1 and
// KH2COOP_CLONE_NEUTRAL_INPUT=1 (an undriven clone executes the local player's
// field commands).
bool Install(std::uintptr_t exeBase, LogFn log);
bool Requested();
// R1/R2: called once after playerkit::Install, privatestatus::Initialize and
// cloneneutral::Install. `neutralInputConfigured` is the module's own live state
// (cloneneutral::Enabled), read again at every load. If the shared hook, private
// status or neutral input is not ready, party-native is cleared (not requested):
// no member write, no publication, no adoption. Logged.
using ReadyProbe = bool (*)();
void ConfirmLocalReadiness(bool resolverHookInstalled, ReadyProbe neutralInputConfigured);
// The clones the last applied load spawned while the members still hold what it wrote: 2 (TwoClones),
// 1 (OneClone: [clone kit, Goofy, own kit]), otherwise 0. Read by NativePrivateStatus and EntityHook (game thread).
unsigned AppliedClones();
// Two players: the puppet index of the one connected other player (the clone's owner), or -1.
int PresentPuppetIndex();
// Party kits (KH2COOP_PARTY_KITS=1 accepted at Install): per-seat kits are on for this machine.
bool KitsActive();
// Party kits: the clone kits the last applied load wrote (rev3: members 0/1; member 2 = AppliedLocal()).
// OneClone: member2 = 0 (member 1 is native Goofy). False when not applied.
bool AppliedMembers(std::uint16_t& member1, std::uint16_t& member2);
// Rev2 S3: the local member 0 the applied load expected (this machine's own kit); 0 when not applied.
std::uint16_t AppliedLocal();
// Party kits: the kit of puppet `index`'s player in the applied load (puppet 0 = the lower other
// network slot = clone kit 1 (member 0), puppet 1 = clone kit 2 (member 1)); 0 when nothing is applied.
std::uint16_t PuppetKit(int index);
// Party kits, owner thread: the latest VALIDATED pose roster byte of puppet `index` (0 Sora,
// 1 Roxas; anything else = unsupported). The host builds its intent kits from these.
void NoteRemoteKit(int index, std::uint8_t roster);
// After playerkit::Shutdown disabled the shared hook.
void Shutdown();
#endif

} // namespace kh2coop::inject::partynative
