#pragma once
// ============================================================================
// EnemySync — shared enemy HP and deaths, enemy sync step 1 (VUH-1502).
//
// Every instance runs its own enemy AI. Enemies are identified by the
// VUH-1499 key: room instance (epoch) + battle program + spawn sequence
// index + objentry id; actor addresses are never used across machines.
//
// Host: announces each room instance (RoomTransition, epoch++), sends the
//       manifest as enemies spawn, absolute HP ~10 Hz, and each death once.
// Client: matches its own spawns to the host's manifest, holds matched HP
//       at the host's value, applies host deaths through the native lethal
//       path, and drops its own hits on enemies (claims are VUH-1501).
// Packets travel through the WorldBridge (kh2coop/WorldBridge.hpp) as the
// codec encodes them; the runtime forwards them to and from the relay.
// ============================================================================

#include <cstdint>

namespace kh2coop {
namespace inject {
namespace enemysync {

using LogFn = void (*)(const char* fmt, ...);
// ApplyStatDelta(actor, delta, idx, react): the native HP path (0x3D2EB0).
using StatDeltaFn = int(__fastcall*)(void* actor, int delta, int idx, int reactFlag);

void Install(uintptr_t exeBase, LogFn log, StatDeltaFn applyStatDelta);

// Game thread, at the start of each frame's entity update (the previous
// frame's actor list is complete).
void OnFrameStart(std::uint32_t frame);

// Game thread, once per actor per frame.
void NoteActor(uintptr_t actor);

// True on a client with enemy sync running: its own hits on enemies must
// not change their HP (the host owns it).
bool DropLocalEnemyDamage(uintptr_t victim);

// Link quality the runtime publishes (app-level round trip, loss in
// per-mille). False until a runtime has connected.
bool NetStats(std::uint32_t& rttMs, std::uint32_t& lossPermille);

// Read the runtime's current session role even between gameplay frames,
// so disconnecting while a menu/load is active releases native exits.
bool HasClientAuthority();

void Shutdown();

} // namespace enemysync
} // namespace inject
} // namespace kh2coop
