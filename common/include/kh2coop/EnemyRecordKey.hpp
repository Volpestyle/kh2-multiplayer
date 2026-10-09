#pragma once
#include "kh2coop/NativeRecordContentTypes.hpp"
#include <array>
#include <cstdint>

namespace kh2coop {
// Protocol v15. Content correspondence only: no addresses or incarnation claim.
struct EnemyRecordKey {
    std::uint16_t schema = 1;
    NativeRecordLocation location {};
    std::uint32_t controllerKey = 0;
    std::uint16_t group = 0, ordinal = 0, nativeId = 0;
    std::array<std::uint8_t, 32> definition {}, header {}, record {};
    bool operator==(const EnemyRecordKey&) const = default;
};
inline bool RecordFamily(std::uint32_t id) noexcept { return id == 317 || id == 76; }
} // namespace kh2coop
