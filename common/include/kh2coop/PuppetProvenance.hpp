#pragma once
#include <array>
#include <cstdint>

namespace kh2coop {

// Copied shared-pose identity, not an actor lifetime or a native-room stamp.
enum class PuppetProducer : std::uint8_t { Invalid, Standalone, Network };
enum class PuppetAuthorityMode : std::uint8_t { Unavailable, Off, Network };
struct PuppetProvenance {
    PuppetProducer producer {PuppetProducer::Invalid};
    std::uint8_t localSlot {255};
    std::uint32_t generation {0};
    std::uint64_t ownerConnectionId {0};
    std::uint64_t localConnectionId {0};
    std::uint64_t hostConnectionId {0};
};
struct PuppetAuthority {
    PuppetAuthorityMode mode {PuppetAuthorityMode::Unavailable};
    std::uint8_t localSlot {255};
    std::uint32_t generation {0};
    std::array<std::uint64_t, 3> connectionIds {};
};

// Same predicate for newly read and cached poses. Unavailable is never Off;
// a rejected network tag never becomes a standalone producer on disconnect.
inline bool ValidPuppetProvenance(const PuppetProvenance& tag, std::uint8_t ownerSlot,
                                int puppetIndex, const PuppetAuthority& current) noexcept {
    if (puppetIndex < 0 || puppetIndex > 1) return false;
    if (tag.producer == PuppetProducer::Standalone) {
        return current.mode == PuppetAuthorityMode::Off && current.generation == 0 && current.localSlot == 255 &&
            current.connectionIds == std::array<std::uint64_t, 3> {} &&
            tag.localSlot == 255 && tag.generation == 0 && tag.ownerConnectionId == 0 &&
            tag.localConnectionId == 0 && tag.hostConnectionId == 0;
    }
    if (tag.producer != PuppetProducer::Network || current.mode != PuppetAuthorityMode::Network ||
        current.generation == 0 || tag.generation != current.generation || current.localSlot >= 3 ||
        tag.localSlot != current.localSlot || current.connectionIds[0] == 0 ||
        current.connectionIds[current.localSlot] == 0 ||
        tag.hostConnectionId != current.connectionIds[0] ||
        tag.localConnectionId != current.connectionIds[current.localSlot]) return false;
    for (unsigned i = 0; i < 3; ++i)
        for (unsigned j = i + 1; j < 3; ++j)
            if (current.connectionIds[i] != 0 && current.connectionIds[i] == current.connectionIds[j]) return false;
    int index = 0;
    for (std::uint8_t slot = 0; slot < 3; ++slot) {
        if (slot == current.localSlot) continue;
        if (index++ == puppetIndex)
            return ownerSlot == slot && tag.ownerConnectionId != 0 &&
                tag.ownerConnectionId == current.connectionIds[slot];
    }
    return false;
}
}
