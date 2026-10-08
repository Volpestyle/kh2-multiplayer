#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace kh2coop::latency {
inline constexpr std::uint32_t kAvatarDelayMs = 80;
inline constexpr std::uint32_t kAvatarDelayMinMs = 1;
inline constexpr std::uint32_t kAvatarDelayMaxMs = 500;
inline constexpr std::uint32_t kEnemyDelayFrames = 6;
// Keep the existing +/-3-frame cursor hysteresis above its one-frame minimum.
inline constexpr std::uint32_t kEnemyDelayMinFrames = 4;
inline constexpr std::uint32_t kEnemyDelayMaxFrames = 18;

inline std::optional<std::uint32_t> parseDelay(std::string_view text,
                                            std::uint32_t minimum,
                                            std::uint32_t maximum) noexcept {
    if (text.empty() || text.size() > 3) return std::nullopt;
    std::uint32_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return std::nullopt;
        value = value * 10 + static_cast<std::uint32_t>(c - '0');
    }
    if (value < minimum || value > maximum) return std::nullopt;
    return value;
}
inline auto parseAvatarDelayMs(std::string_view text) noexcept {
    return parseDelay(text, kAvatarDelayMinMs, kAvatarDelayMaxMs);
}
inline auto parseEnemyDelayFrames(std::string_view text) noexcept {
    return parseDelay(text, kEnemyDelayMinFrames, kEnemyDelayMaxFrames);
}
} // namespace kh2coop::latency
