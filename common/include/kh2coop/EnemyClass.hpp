#pragma once
#include <cstdint>

namespace kh2coop::enemyclass {

// Installed 00objentry.bin calls B_EX100 a large boss. Type 12 is also
// used by noncombat objects, so it is deliberately not a type-wide admission.
inline constexpr std::uint32_t TwilightThorn = 307;
// B_EX110, the Mansion's Axel (02/14 btl137). Axel I (B_EX110_SKIRMISH, 2229) offers no reaction command.
inline constexpr std::uint32_t AxelII = 81;
inline constexpr bool LargeBoss(std::uint32_t type, std::uint32_t objectId) noexcept {
    return type == 12 && objectId == TwilightThorn;
}
inline constexpr bool Boss(std::uint32_t type, std::uint32_t objectId) noexcept {
    return type == 3 || LargeBoss(type, objectId);
}
inline constexpr bool Combat(std::uint32_t type, std::uint32_t objectId) noexcept {
    return type == 4 || Boss(type, objectId);
}

// Bosses the common target_search adapter may retarget (VUH-2912). B_EX110 (Axel II, type 3) takes
// every ordinary target through target_search modes 0/2 into +0xBF8 (docs/ENEMY_TARGET_REMOTE.md).
// Exact identities only: other type-3 bosses keep their native targeting until their scripts are read.
// B_EX110_SKIRMISH (2229, Axel I) shares the BDX but stays out until a live run qualifies it. AxelII is above.
inline constexpr bool SearchTargetBoss(std::uint32_t type, std::uint32_t objectId) noexcept {
    return LargeBoss(type, objectId) || (type == 3 && objectId == AxelII);
}

} // namespace kh2coop::enemyclass
