#pragma once
#include <cstdint>
#include "DamagePolicy.hpp"

namespace kh2coop::inject::nativehittrace {
constexpr std::uint32_t ApplyHook = 1, TakeHook = 2, StatHook = 4, AllHooks = 7;
constexpr std::uint32_t ContextRole = 1, ContextSession = 2, ContextBridge = 4,
    ContextNative = 8, ContextRoom = 16, ContextPhase = 32, ContextRepeated = 64,
    ContextComplete = 127;
constexpr std::uint32_t ActorObject = 1, ActorStatus = 2, ActorType = 4,
    ActorId = 8, ActorTeam = 16, ActorHp = 32, ActorMaxHp = 64,
    ActorRepeated = 128, ActorName = 256, ActorComplete = 511;
constexpr std::uint32_t HitFlags = 1, HitStat = 2, HitDamage = 4,
    HitAttack = 8, HitOwner = 16, HitKind = 32, HitAttackId = 64,
    HitCanonical = 128, HitComplete = 255;
constexpr unsigned ChildCapacity = 8, QueueCapacity = 128, MaxDepth = 8;

// Copied facts only. readMask records checks actually completed; available
// additionally means the enclosing phase/session checks all passed.
struct Context {
    std::uint64_t frame = 0, generation = 0, epoch = 0;
    std::uint64_t transitionSerial = 0, loadSerial = 0;
    std::uint64_t connectionId = 0, hostConnectionId = 0;
    std::uint32_t readMask = 0;
    std::uint16_t location[6] {};
    std::uint8_t role = 0, slot = 0;
    bool available = false;
};
struct ActorSnapshot {
    uintptr_t actor = 0, objentry = 0, status = 0;
    std::uint32_t objectId = 0, readMask = 0, team = 0;
    std::int32_t hp = 0, maxHp = 0;
    std::uint16_t namePrefix = 0;
    std::uint8_t type = 0;
};
struct HitSnapshot {
    uintptr_t hit = 0, attack = 0, owner = 0;
    uintptr_t canonicalPlayer = 0, head = 0, tracked = 0;
    std::uint32_t flags = 0, attackHandle = 0, atkpHandle = 0, ownerHandle = 0;
    std::uint32_t attackId = 0, readMask = 0;
    std::int32_t damage = 0;
    std::uint8_t stat = 0, kind = 0;
    bool syncDrop = false, manualFilterOn = false, manualDrop = false;
};
struct ApplyFacts {
    Context context {};
    ActorSnapshot victim {}, source {};
    HitSnapshot hit {};
};
// Optional copied production-policy envelope. It observes an actual decision
// and actual attempted operations; queued is not host acceptance. Unattempted
// operation results are placeholders, never success. No new native reads.
struct PolicyObservation {
    bool recorded = false;
    ApplyFacts authority {};
    damagepolicy::Facts facts {};
    damagepolicy::Decision decision {};
    std::uint64_t roster[3] {};
    bool revalidationAttempted = false, revalidationPassed = false, zeroAttempted = false;
    damagepolicy::ZeroResult zeroResult = damagepolicy::ZeroResult::InvalidExpected;
    bool claimAttempted = false, claimQueued = false;
};
enum class ChildKind : std::uint8_t { Take = 1, Stat = 2 };
struct Child {
    std::uint64_t sequence = 0, takeSequence = 0;
    uintptr_t actor = 0, callerRva = 0;
    std::int32_t delta = 0, stat = 0, react = 0, result = 0;
    ActorSnapshot before {}, after {};
    ChildKind kind = ChildKind::Take;
    bool callerAvailable = false, matching = false, returned = false, unwound = false;
};
struct Event {
    std::uint64_t sequence = 0, parentSequence = 0, coverageSerial = 0, lossSerial = 0;
    uintptr_t callerRva = 0, rawResult = 0;
    std::uint32_t depth = 0, coverageMask = 0, takeCalls = 0, statCalls = 0, childCount = 0;
    ApplyFacts before {}, after {};
    PolicyObservation policy {};
    Child children[ChildCapacity] {};
    bool ownerThread = false, callerAvailable = false, returned = false, unwound = false;
    bool nested = false, overflow = false, coverageStable = false, contextStable = false;
    bool metadataStable = false, lossStable = false, witness = false;
};
// Stack-owned POD tokens must remain alive until End*. Finish from a scalar
// __finally, passing nullptr for post facts on abnormal termination. No native
// memory is read by this module. Do not copy an active token.
struct ApplyToken { Event event {}; ApplyToken* previous = nullptr; void* previousTake = nullptr; bool active = false, scoped = false; };
struct ChildToken { ApplyToken* parent = nullptr; ChildToken* previousTake = nullptr; std::uint32_t index = ChildCapacity; bool active = false, isTake = false; };
struct Stats {
    bool requested = false;
    std::uint32_t verifiedMask = 0, installedMask = 0;
    std::uint64_t coverageSerial = 0, started = 0, published = 0, drained = 0;
    std::uint64_t dropped = 0, foreign = 0, unwound = 0, nested = 0, overflow = 0, unmatched = 0;
};
using LogFn = void(*)(const char*, ...);
void Configure(bool requested, std::uint32_t verifiedMask, std::uint32_t installedMask) noexcept;
void Shutdown() noexcept;
void RegisterOwnerThread() noexcept;
bool IsOwnerThread() noexcept;
bool Requested() noexcept;
bool CanCaptureApply() noexcept;
bool CanCaptureChild() noexcept;
void BeginApply(ApplyToken& token, uintptr_t callerRva, bool callerAvailable, const ApplyFacts& before) noexcept;
void EndApply(ApplyToken& token, bool normal, uintptr_t rawResult, const ApplyFacts* after) noexcept;
void BeginTake(ChildToken& token, uintptr_t actor, std::int32_t delta, std::int32_t stat,
    std::int32_t react, uintptr_t callerRva, bool callerAvailable, const ActorSnapshot& before) noexcept;
void EndTake(ChildToken& token, bool normal, const ActorSnapshot* after) noexcept;
void BeginStat(ChildToken& token, uintptr_t actor, std::int32_t delta, std::int32_t stat,
    std::int32_t react, uintptr_t callerRva, bool callerAvailable, const ActorSnapshot& before) noexcept;
void EndStat(ChildToken& token, bool normal, std::int32_t result, const ActorSnapshot* after) noexcept;
Stats GetStats() noexcept;
bool PopEvent(Event& event) noexcept;
// Call outside native adapters. Emits complete events and cumulative summary;
// at most 16 queued events per call. rawResult is opaque, never success.
// Requested idle summaries are eligible every 1000ms on a real drain.
// ownerFrame=0 keeps source compatibility but is not live interval evidence.
void Drain(LogFn log, std::uint32_t ownerFrame = 0);
}
