#pragma once
// ============================================================================
// PlayerKit — per-session player-class kit for the local slot-0 player
// (VUH-1513 step 2). Default OFF: nothing is hooked unless KH2COOP_PLAYER_KIT
// names an allowed vanilla kit.
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
// Kits: 0x5A Roxas (P_EX110) only for the first live run; 0x323 dual-wield Roxas
// and 0x5B Mickey are refused until each has its own fixture. 0 / unset = Sora
// (no hook, no reads).
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
// "0x5A"/"90" -> Roxas, "0"/empty -> off; false on anything else.
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

// Reads KH2COOP_PLAYER_KIT. Unset/0: returns true without hooking. Unknown
// kit or mismatched 3E2EB0 bytes: logs, returns false, no hook.
bool Install(std::uintptr_t exeBase, LogFn log);
// Disables the hook, then restores member 0 if it still holds our kit.
void Shutdown();
Stats GetStats();
// Roster byte for the actor's actual descriptor (actor+0x918 -> objentry id).
std::uint8_t RosterForActor(std::uintptr_t actor);
// True once a kit was requested (non-"0" env): only then may callers read the roster.
bool KitRequested();

// Puppet collision guard (lead decision, VUH-1513): a selector-0 friend puppet
// resolves through the same member 0, so with any non-zero KH2COOP_PLAYER_KIT
// (even a refused one) the native-Sora clone puppet path must refuse instead of
// driving a kit clone. Per-puppet member slots are VUH-1519's job.
bool BlocksNativeSoraPuppets();
// Game thread: called when a player-class clone was refused; logs once per area load.
void NoteRefusedClone(std::uintptr_t actor);

} // namespace kh2coop::inject::playerkit
