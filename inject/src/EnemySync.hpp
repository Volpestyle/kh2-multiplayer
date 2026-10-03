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
// Both: once per second in confirmed, controllable gameplay, publish a hash
//       of actual local live actors (including unmatched copies) and verified
//       applied SAVE bytes. Native actor observations accompany each hash log.
// Presence comes from a checked canonical native active-list census. An
// unavailable census suspends enemy writes, sends, hashes and despawn inference;
// time without a complete observation does not count toward despawn grace.
// Hash rows use a fresh post-application census, independently of spawn presence.
// Host room announcements wait for safe native gameplay and an enqueued full
// progress snapshot; actors remain tracked while the room packet is retried.
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

// Game thread, at the head actor's update entry, before its native update.
// Dependency scheduling means this need not be the first actor callback.
void OnFrameStart(std::uint32_t frame);

// Compatibility entry point for EntityHook; callback coverage is not presence.
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
