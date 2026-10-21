#pragma once
#include <cstdint>

namespace kh2coop::enemyclass {

// Installed 00objentry.bin calls B_EX100 a large boss. Type 12 is also
// used by noncombat objects, so it is deliberately not a type-wide admission.
inline constexpr std::uint32_t TwilightThorn = 307;
inline constexpr bool LargeBoss(std::uint32_t type, std::uint32_t objectId) noexcept {
    return type == 12 && objectId == TwilightThorn;
}
inline constexpr bool Boss(std::uint32_t type, std::uint32_t objectId) noexcept {
    return type == 3 || LargeBoss(type, objectId);
}
inline constexpr bool Combat(std::uint32_t type, std::uint32_t objectId) noexcept {
    return type == 4 || Boss(type, objectId);
}

} // namespace kh2coop::enemyclass
