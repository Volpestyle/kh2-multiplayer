#pragma once
#include <cstdint>
namespace kh2coop::inject::spectate {
// Read-only fixture receipt of the actual native camera call. The borrowed
// pointer is intentionally restored before returning to the game.
struct Channel {
    std::uint32_t magic, version;
    volatile long sequence;
    std::uint32_t installed, frame, active, slot, cycles;
    std::uint64_t actorDuring, actorBefore, actorAfter, localActor, episode;
    std::uint32_t calls, overrides, generation, transition, load, mode, released, aimSuppressed;
};
static_assert(sizeof(Channel) == 104);
} // namespace kh2coop::inject::spectate
