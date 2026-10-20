#pragma once
#include "kh2coop/Types.hpp"
#include "kh2coop/PuppetProvenance.hpp"
#include <array>
#include <cstdint>
namespace kh2coop { struct SeatState; }
namespace kh2coop::inject::nativehud {
using LogFn = void (*)(const char*, ...);
// Puppet indices enumerate the other two owner slots, including empty roster holes.
// A legacy PuppetTarget can name an NPC for an empty owner slot; that is not a human exclusion.
inline std::array<std::uintptr_t, 2> HumanTargets(const PuppetAuthority& a, const std::array<std::uintptr_t, 2>& targets) {
    std::array<std::uintptr_t, 2> out{};
    if (a.mode != PuppetAuthorityMode::Network || !a.generation || a.localSlot > 2) return out;
    unsigned index = 0;
    for (std::uint8_t owner = 0; owner < 3; ++owner) {
        if (owner == a.localSlot) continue;
        if (a.connectionIds[owner]) out[index] = targets[index];
        ++index;
    }
    return out;
}
void Install(std::uintptr_t base, LogFn log);
bool RetainsMinHookResources();
void Retire(); // stop new views; retained callbacks restore native bindings on the HUD thread
bool QueuePreserveRestoration();
void Session(std::uint32_t generation, const std::array<std::uint64_t, 3>& roster, std::uint8_t local);
void Receive(const SeatState& m);
// Game-thread observation. Only current admitted puppet actors supply portrait resources.
void Observe(const AvatarState& local, std::uintptr_t actor, const PuppetAuthority& authority,
             const std::array<std::uintptr_t, 3>& bodies, const std::array<std::uintptr_t, 2>& excluded = {});
void Fill(SeatState& m, std::uint64_t nowMs);
}
