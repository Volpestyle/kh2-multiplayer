#pragma once
#include "NativeSpawnController.hpp"
#include <array>
#include <cstdint>

namespace kh2coop::inject::lifetimetrace {
enum class Kind : std::uint8_t { Factory, SelectedDestructor, Allocator };
enum class Phase : std::uint8_t { Entry, Exit };
struct Actor {
    uintptr_t pointer{}, object{}, controller{}, record{};
    std::uint32_t objectId{}, handlerHandle{}, flags120{}, readMask{};
    std::uint16_t recordId{};
    std::uint8_t type{}, mode{}, stage{};
    bool repeatedMetadataEqual{};
};
struct Wrapper {
    std::uint64_t serial{}, coverage{}, sequence{};
    uintptr_t controller{}, record{};
    std::uint32_t depth{};
    bool present{}, ambiguous{}, ancestryProven{}; // always false: TLS is not authority
};
struct Event {
    std::uint64_t sequence{}, parent{}, tickMs{}, generation{}, droppedAtEntry{};
    uintptr_t caller{}, actor{}, handler{}, domain{}, currentDomain{}, vtable{}, slot{};
    uintptr_t fiber{}, pointPointer{}, result{};
    std::array<float,4> point{};
    float yaw{};
    Actor before{};
    Wrapper wrapper{};
    spawncontroller::TraceStamp stamp{};
    std::uint32_t threadId{}, depth{}, rawObjectId{}, installedMask{}, exceptionCode{};
    Kind kind{};
    Phase phase{};
    bool ownerThread{}, isFiber{}, stampAvailable{}, pointAvailable{}, pointFinite{};
    bool domainAvailable{}, domainMatches{}, selectedHandler{}, scopeMatched{};
    bool originalReturned{}, unwound{}, retiredDuringCall{}, ancestryProven{}, fiberExcludedDuringCall{};
};
struct Statistics {
    std::uint32_t verified{}, installed{}, failed{};
    std::uint64_t entered{}, published{}, dropped{}, foreign{}, filtered{}, unwound{}, exceptions{};
    std::uint64_t fiberBypassed{}, fiberThreads{}, scopeAbandoned{};
    bool requested{}, recording{}, retained{};
};
// Opt-in KH2COOP_LIFETIME_TRACE=1 only. MinHook already initialized.
// FLS slot and bounded scope POD pool retained until process exit; no stack pointers.
// Legacy SPAWN_TRACE/NATURAL_RESOURCE_TRACE must remain OFF pending separate fiber review.
// fiberExcludedDuringCall now denotes a rejected FLS/stack/return boundary, not mere fiber use.
bool Initialize(uintptr_t base);
void StopRecording(); // Retain all exposed code/metadata until process exit.
bool RetainsMinHookResources();
Statistics GetStatistics();
bool Pop(Event& out); // Actual registered owner thread only; nonblocking.
void Drain(spawncontroller::LogFn log, unsigned maximum = 8);
} // namespace kh2coop::inject::lifetimetrace
