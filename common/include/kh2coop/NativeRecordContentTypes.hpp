#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace kh2coop {

// Portable content only: no native pointer, lifetime, eligibility or authority.
struct NativeRecordLocation {
    std::uint16_t world{}, room{}, door{}, mapProgram{}, battleProgram{}, eventProgram{};
    bool operator==(const NativeRecordLocation&) const = default;
};
// Use before narrowing a wider decoded source. Native readers also validate their
// own source field widths; this comparison format deliberately uses six u16s.
inline std::optional<NativeRecordLocation> MakeNativeRecordLocation(
    const std::array<std::uint32_t, 6>& values) {
    for (const auto value : values) if (value > 0xffffu) return std::nullopt;
    return NativeRecordLocation{static_cast<std::uint16_t>(values[0]),
        static_cast<std::uint16_t>(values[1]), static_cast<std::uint16_t>(values[2]),
        static_cast<std::uint16_t>(values[3]), static_cast<std::uint16_t>(values[4]),
        static_cast<std::uint16_t>(values[5])};
}
struct NativeRecordContentDefinition {
    std::array<std::uint8_t, 32> layoutSha256{};
    NativeRecordLocation location{};
    std::uint32_t groupKey{};
    std::array<std::uint8_t, 44> header{};
    std::vector<std::array<std::uint8_t, 64>> records;
};
enum class NativeRecordContentStatus {
    Complete, Unsupported, Partial, Unavailable, Ambiguous, NoMatch
};
inline constexpr std::size_t NativeRecordContentMaxDefinitions = 64;
inline constexpr std::size_t NativeRecordContentMaxRecords = 256;
inline constexpr std::size_t NativeRecordContentMaxTotalRecords = 1024;

} // namespace kh2coop
