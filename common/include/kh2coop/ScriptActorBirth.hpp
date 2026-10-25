#pragma once
// Shared normalized script-birth identity without importing wire messages into
// the inject hook translation unit (its local HitClaim has a separate ABI).
#include <array>
#include <cstdint>

namespace kh2coop {
inline constexpr std::uint32_t SCRIPT_TAG_INT = 0x544E4940;   // '@INT'
inline constexpr std::uint16_t SCRIPT_ACTOR_BIRTH_BEGIN = 15, SCRIPT_ACTOR_BIRTH_END = 16;
inline constexpr std::uint32_t SCRIPT_ACTOR_BIRTH_PARENT_OBJECT = 307, SCRIPT_ACTOR_BIRTH_CHILD_OBJECT = 317;
inline constexpr bool scriptActorBirthOperation(std::uint8_t table,std::uint16_t index) noexcept {
    return table==255 && (index==SCRIPT_ACTOR_BIRTH_BEGIN || index==SCRIPT_ACTOR_BIRTH_END);
}
inline constexpr bool scriptActorBirthPolicy(std::uint32_t parentObject,std::uint32_t childObject) noexcept {
    return parentObject==SCRIPT_ACTOR_BIRTH_PARENT_OBJECT && childObject==SCRIPT_ACTOR_BIRTH_CHILD_OBJECT;
}
// Four tagged INT cells carry unsigned32 bit patterns, not signed arithmetic.
inline constexpr bool validScriptActorBirthValues(const std::array<std::uint64_t,4>& values) noexcept {
    for(const auto value:values)if(static_cast<std::uint32_t>(value>>32)!=SCRIPT_TAG_INT)return false;
    const auto low=static_cast<std::uint32_t>(values[0]), high=static_cast<std::uint32_t>(values[1]);
    const auto child=static_cast<std::uint32_t>(values[2]);
    return (low || high) && child && child<=UINT16_MAX &&
        static_cast<std::uint32_t>(values[3])==SCRIPT_ACTOR_BIRTH_CHILD_OBJECT;
}
inline constexpr bool validScriptActorBirthIdentity(std::uint16_t parent,std::uint32_t parentObject,
        std::uint32_t childResource,const std::array<std::uint64_t,4>& values) noexcept {
    return parent && validScriptActorBirthValues(values) &&
        scriptActorBirthPolicy(parentObject,static_cast<std::uint32_t>(values[3])) &&
        childResource==static_cast<std::uint32_t>(values[2]) && childResource!=parent;
}
} // namespace kh2coop
