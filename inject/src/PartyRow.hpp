#pragma once
// VUH-2342: story party rows follow the host into a friend's next load. Pure rules; the native
// adapter is in EnemySync.cpp (host stamp in QueueHostBeginInstance, friend ApplyHostPartyRow).
// docs/PARTY_SETUP.md "Story party rows follow the host".
#include <cstdint>
#include "PlayerKit.hpp"

namespace kh2coop::inject::partyrow {

inline constexpr std::uintptr_t kSaveRva = 0x9A98B0, kPartyRows = 0x3534;  // row = SAVE + 0x3534 + 4*world
inline constexpr std::uint32_t kSaveMagic = 0x4A32484B;                    // "KH2J" at the save body's start
inline constexpr std::uint16_t kWorlds = 19;
inline constexpr std::uint32_t kCompanionMask = 0xFFFFFF00u;               // friend1, friend2, world ally

// TT, HB and BC rows belong to the two-player clone (playerkit::UpdateBcClone), which rewrites them on
// every machine itself; worlds past the table are refused.
inline bool StoryWorld(std::uint16_t world) noexcept {
    if (world >= kWorlds) return false;
    for (const auto clone : playerkit::AUTO_CLONE_WORLDS) if (world == clone) return false;
    return true;
}

// Row bytes index MEMT's members (OpenKH Memt.cs): 0 the player, 1 Donald, 2 Goofy, 3 the world's
// character (Ping in LoD), 0x12 empty. Only those values may follow the host into a companion byte:
// a form, Mickey or the clone selector 0 is refused.
inline bool CompanionBytesValid(std::uint32_t row) noexcept {
    for (unsigned k = 1; k < 4; ++k) {
        const auto member = (row >> (8 * k)) & 0xFF;
        if (member != 0x01 && member != 0x02 && member != 0x03 && member != 0x12) return false;
    }
    return true;
}

// The friend's row after taking the host's companions: its own player byte stays.
inline std::uint32_t Merge(std::uint32_t local, std::uint32_t host) noexcept {
    return (local & ~kCompanionMask) | (host & kCompanionMask);
}

}  // namespace kh2coop::inject::partyrow
