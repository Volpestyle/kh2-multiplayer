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
#include <functional>
#include <string>
#include <vector>
#include "NativeHitTrace.hpp"
#include "kh2coop/PuppetProvenance.hpp"
#include "kh2coop/WorldContext.hpp"

namespace kh2coop {
namespace inject {
namespace enemysync {

using LogFn = void (*)(const char* fmt, ...);
// ApplyStatDelta(actor, delta, idx, react): the native HP path (0x3D2EB0).
using StatDeltaFn = int(__fastcall*)(void* actor, int delta, int idx, int reactFlag);
using TakeDamageFn = void(__fastcall*)(void* actor, int delta, int idx, std::uint8_t reactFlag);

void Install(uintptr_t exeBase, LogFn log, StatDeltaFn applyStatDelta, TakeDamageFn takeDamage);
// Install-time opt-in, checked flush result; no authority or protocol role.
void SetHashDiagnosticSink(std::function<bool(const std::string&)> sink);

// Submitted synchronously by the verified local-player HP-hit hook. Native
// addresses are inspected now and never retained or put on the wire.
struct LocalPlayerEnemyHit {
    uintptr_t victim = 0, attacker = 0;
    std::uint32_t attackId = 0;
    std::int32_t damage = 0;
    float attackerPosition[3] {};
};
bool RecordLocalPlayerEnemyHit(const LocalPlayerEnemyHit& hit) noexcept;

// Game thread, at the head actor's update entry, before its native update.
// Dependency scheduling means this need not be the first actor callback.
void OnFrameStart(std::uint32_t frame);

// Compatibility entry point for EntityHook; callback coverage is not presence.
void NoteActor(uintptr_t actor);

// True on a client with enemy sync running: its own hits on enemies must
// not change their HP (the host owns it).
bool DropLocalEnemyDamage(uintptr_t victim);

// Copied diagnostic context on the registered game thread. Partial observations
// retain their masks; this never advances authority or performs a census.
nativehittrace::Context CaptureNativeHitContext() noexcept;
// Damage ownership snapshot on the registered game thread, independent of
// trace opt-in. A false result leaves authority unavailable and roster zero.
bool CaptureDamageContext(nativehittrace::Context& context,
                          std::uint64_t (&roster)[3]) noexcept;
// Read-only avatar authority on the registered game thread. An unavailable or
// changing network binding never grants standalone authority.
PuppetAuthority CapturePuppetAuthority() noexcept;
// Bounded release eligibility from the existing complete native census. This
// proves current list membership/lifecycle, not creation incarnation.
bool CurrentPuppetActor(uintptr_t actor, std::uint32_t transition,
                        std::uint32_t load) noexcept;

// Link quality the runtime publishes (app-level round trip, loss in
// per-mille). False until a runtime has connected.
bool NetStats(std::uint32_t& rttMs, std::uint32_t& lossPermille);

// Read the runtime's current session role even between gameplay frames,
// so disconnecting while a menu/load is active releases native exits.
bool HasClientAuthority();

// Read-only on the registered game thread: the nonzero header generation whose
// ordered reset has been consumed, or zero while retired/unavailable. Callers
// retain and recheck this value before applying a queued native world action.
std::uint32_t WorldSessionGeneration() noexcept;
// Capture once at the native observation. Retained work must send that context;
// a newer header never grants permission to relabel an old packet.
bool CaptureWorldContext(ProducerWorldContext& context);
bool WorldContextCurrent(const ProducerWorldContext& context) noexcept;
bool SendCapturedWorld(const std::vector<std::uint8_t>& packet, const ProducerWorldContext& context);

// Scoped native controller hook callbacks. A point is captured only after an
// accepted client challenge; a client copy never falls back to its local point.
// Role values: 0 off, 1 host, 2 client. Called on the native game thread.
std::uint8_t ActivationRole();
void CaptureHostActivation(const float* position4);
bool CopyHostActivation(float* position4, uintptr_t controller, std::uint64_t updateSequence);

void Shutdown();

} // namespace enemysync
} // namespace inject
} // namespace kh2coop
