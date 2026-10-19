#pragma once
#include <cstdint>
namespace kh2coop::routespawn { struct MissingPartySite; }
namespace kh2coop::inject::privatestatus {
using LogFn = void (*)(const char*, ...);
// Exact Steam 9002b2de private status. Explicit legacy experiment, or automatic
// two-player route dependency under shipped TARGET_ALL + MIRROR. Bailey uses an
// explicit retained-record scope; it never claims or writes a party leaf.
// VUH-1515 (KH2COOP_ENEMY_TARGET_REMOTE): scope also verified rooms (BC 05/06) with their exact leaf row 00/00/02/12.
void EnableEnemyTargetScope();
bool Initialize(std::uintptr_t base);
void Drain(LogFn log);
void StopNewAllocations(); // Existing ownership/veto remains until native release.
bool RetainsMinHookResources();
// VUH-1519: true while every hook is installed and no fault has disarmed selection.
// PartyNative refuses to spawn clones otherwise (they would share Sora's SAVE-bound record).
bool Ready();
using NativeWrapper = void* (__fastcall*)(const void*,void*);
// Caller qualified exact MissingPartySite ingress (Bailey; VUH-2464 prologue sites). Normal paths call
// nativeRecord once and return its result; an added direct construction of `cloneObject` (Sora 84, or the
// prologue remote's qualified profile) uses retained immutable storage and explicit FLS selection.
void* SpawnMissingParty(const void* nativeRecord,void* controller,NativeWrapper spawn,LogFn log,
                        const routespawn::MissingPartySite& site,std::uint16_t cloneObject,
                        bool* bound=nullptr,std::uintptr_t* clone=nullptr);
// Only actors created by that scope are filtered. Unbound/stale/disconnected actors stay quarantined.
bool MissingPartyPuppetAllowed(std::uintptr_t actor);
// VUH-2464: a prologue site admitted a non-Sora clone this process and NOW is in TT: qualified kit
// descriptors (Roxas) count as clones for neutral input and private status there.
bool MissingPartyKitScope();
// VUH-2464: `actor` is a bound missing-party clone that is still current (MissingPartyPuppetAllowed
// for a known entry). Unknown actors are false here, unlike MissingPartyPuppetAllowed.
bool MissingPartyClone(std::uintptr_t actor);
// VUH-2482: shows the owner's streamed HP on a bound missing-party clone by writing its own private
// status record only (game thread, hp 1..maxHp). 1 written, 0 already equal, negative refused.
int MirrorMissingPartyHp(std::uintptr_t actor, std::int32_t hp, std::int32_t maxHp);

}
