#pragma once
#include <cstdint>
namespace kh2coop::inject::privatestatus {
using LogFn = void (*)(const char*, ...);
// Exact Steam 9002b2de private status. Explicit legacy experiment, or automatic
// two-player BC dependency under shipped TARGET_ALL + MIRROR (owned default leaf).
// VUH-1515 (KH2COOP_ENEMY_TARGET_REMOTE): scope also verified rooms (BC 05/06) with their exact leaf row 00/00/02/12.
void EnableEnemyTargetScope();
bool Initialize(std::uintptr_t base);
void Drain(LogFn log);
void StopNewAllocations(); // Existing ownership/veto remains until native release.
bool RetainsMinHookResources();
// VUH-1519: true while every hook is installed and no fault has disarmed selection.
// PartyNative refuses to spawn clones otherwise (they would share Sora's SAVE-bound record).
bool Ready();
}
