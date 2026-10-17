#pragma once
#include <cstdint>

// Native mission flags shared by the pure mission and route admission rules.
namespace kh2coop::inject::missionmirror::rules {
inline constexpr std::uint32_t kEnded = 0x14, kFailedResult = 2, kInUse = 0x1;
}
