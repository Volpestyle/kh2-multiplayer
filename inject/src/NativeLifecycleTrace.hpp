#pragma once

#include "NativeSpawnController.hpp"
#include <cstdint>

namespace kh2coop::inject::lifecycletrace {

enum class Kind : std::uint8_t {
    RemovalBookkeeping, Disposal, DeathMark, DeathBookkeeping, CountDecrement,
    RemovalPredicate
};
constexpr std::uint32_t HookBit(Kind kind) { return 1u << static_cast<unsigned>(kind); }
// Child hooks do not publish separate lifecycle events.
constexpr std::uint32_t ScriptPredicateHook = 1u << 6;
constexpr std::uint32_t AuxiliaryPredicateHook = 1u << 7;
constexpr std::uint32_t RemovalPredicateHooks = HookBit(Kind::RemovalPredicate) | ScriptPredicateHook | AuxiliaryPredicateHook;
constexpr std::uint32_t AllHooks = 0xFF;

enum class RemovalBranch : std::uint8_t {
    Unknown, ScriptBlocked, PointersBlocked, AuxiliaryBlocked, AuxiliaryAllowed, AuxiliaryMissingAllowed
};

// Independent checked boundary reads, never reconstructed branch operands.
// Mask bits: +5B0=1, +5B8=2, +80=4, +98=8, +BB4=16.
struct RemovalOperands {
    uintptr_t scriptState = 0, field80 = 0, field98 = 0;
    std::uint32_t scriptTest = 0, auxiliaryHandle = 0, availableMask = 0;
};

struct RemovalPredicates {
    std::uint32_t coverageMask = 0;
    std::uint64_t coverageGeneration = 0;
    RemovalOperands before {}, afterScript {}, after {};
    uintptr_t auxiliaryArgument = 0;
    std::uint32_t auxiliaryBefore = 0, auxiliaryAfter = 0, auxiliaryAvailableMask = 0;
    std::uint32_t scriptCalls = 0, scriptReturned = 0, auxiliaryCalls = 0, auxiliaryReturned = 0;
    // Parent=0x20, script child=0x40, auxiliary child=0x80. A propagated
    // exception can mark multiple observers; this is not multiple native faults.
    std::uint32_t faultMask = 0, unwindMask = 0;
    std::uint8_t parentResult = 0, scriptResult = 0, auxiliaryResult = 0;
    RemovalBranch branch = RemovalBranch::Unknown;
    bool originalReturned = false, resultAvailable = false, coverageStable = false;
    bool countOverflow = false, nestedAmbiguous = false;
};

// Raw local observations, not allocation generations or portable identities.
// available means all requested reads and a repeated metadata check succeeded.
struct ActorSnapshot {
    uintptr_t actor = 0, objentry = 0, status = 0, controller = 0, record = 0;
    std::uint32_t objectId = 0, flags120 = 0, flags9B8 = 0, flags6C8 = 0;
    std::int32_t hp = 0, maxHp = 0;
    std::uint16_t recordId = 0;
    std::uint8_t type = 0, recordMode = 0, recordStage = 0;
    float fadeA08 = 0, slopeA0C = 0, fadeAAC = 0, slopeAB0 = 0;
    bool available = false, classificationAvailable = false, combat = false, recordAvailable = false;
};

struct Event {
    std::uint64_t sequence = 0, parentSequence = 0;
    std::uint32_t depth = 0, exceptionCode = 0;
    Kind kind = Kind::RemovalBookkeeping;
    std::uint8_t role = 0;
    // False means unknown, including role==0. The callback is game-thread-only.
    bool roleAvailable = false;
    uintptr_t callerRva = 0, actor = 0, controller = 0;
    ActorSnapshot beforeActor {}, afterActor {};
    RemovalPredicates removal {};
    spawncontroller::TraceStamp beforeStamp {}, afterStamp {};
    spawncontroller::TraceState beforeState {}, afterState {};
    bool callerInImage = false, beforeStampAvailable = false, afterStampAvailable = false;
    bool originalReturned = false, unwound = false, lifecycleStable = false;
    // Disposal may destroy resources. False forbids interpreting afterActor as
    // the same actor's state; even true is metadata equality, never lifetime proof.
    bool postActorComparable = false;
    bool controllerComparable = false;
    bool controllerFromActor = false, actorControllerMismatch = false;
    bool unavailable = false, outOfScope = false;
};

struct Stats {
    bool requested = false;
    std::uint32_t verifiedMask = 0, installedMask = 0, failedMask = 0;
    std::uint64_t started = 0, published = 0, dropped = 0, unavailable = 0;
    std::uint64_t outOfScope = 0, nativeFaults = 0, unwound = 0, depthOverflow = 0;
    std::uint64_t predicateStarted = 0, predicatePublished = 0, predicateDropped = 0;
    std::uint64_t predicateForeign = 0, predicateUnmatched = 0, predicateUnwound = 0;
    std::uint64_t predicateDepthOverflow = 0, predicateCountOverflow = 0;
    // Per-observer exception observations; nested hooks can observe one native
    // exception more than once. parentSequence/depth identify that nesting.
    std::uint32_t lastNativeException = 0;
};

// MinHook must already be initialized. Each hook verifies and installs
// independently; inspect masks for partial coverage. False opts out entirely.
// No replay, suppression, network work, HP/count/cache writes, or native retries.
bool Install(uintptr_t exeBase, spawncontroller::LogFn log,
             spawncontroller::RoleFn role, bool trace = false);
void Shutdown();
Stats GetStats();
bool PopEvent(Event& event);

} // namespace kh2coop::inject::lifecycletrace
