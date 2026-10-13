#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>
#include <string_view>
#include <array>
#include <vector>

namespace kh2coop {
enum class CoopDifficulty : std::uint8_t { Off = 0, Normal = 1, Hard = 2 };
constexpr bool ValidDifficulty(CoopDifficulty mode) noexcept {
    return mode == CoopDifficulty::Off || mode == CoopDifficulty::Normal || mode == CoopDifficulty::Hard;
}
constexpr CoopDifficulty ParseDifficulty(std::string_view value) noexcept {
    return value == "normal" ? CoopDifficulty::Normal : value == "hard" ? CoopDifficulty::Hard : CoopDifficulty::Off;
}
struct EnemyHealth { std::int32_t hp{}, maxHp{}; };
// Zero replacement max preserves the currently native max (Off). Never publish
// a current HP above the max that will exist after this write.
constexpr bool HealthWriteWithinMax(std::int32_t hp, std::int32_t nativeMax,
                                   std::int32_t replacementMax = 0) noexcept {
    const auto targetMax = replacementMax > 0 ? replacementMax : nativeMax;
    return hp > 0 && hp <= targetMax;
}
using HealthIdentity = std::array<std::uintptr_t, 6>;
// Native-room lifetime, independent of transport/session resets. The existing
// spawn ordinal distinguishes a recycled actor even when its roots are reused.
class HealthScalingHistory {
    std::vector<HealthIdentity> entries_;
public:
    bool Applied(const HealthIdentity& identity) const noexcept {
        for (const auto& e : entries_) if (e == identity) return true;
        return false;
    }
    void Remember(const HealthIdentity& identity) {
        if (!Applied(identity)) entries_.push_back(identity);
    }
    void Clear() noexcept { entries_.clear(); }
};
constexpr std::uint32_t HealthFactor(CoopDifficulty mode, unsigned players, bool boss) noexcept {
    const unsigned i = players < 1 ? 0 : players > 3 ? 2 : players - 1;
    constexpr std::uint32_t normal[] = {1000, 1600, 2200}, hard[] = {1300, 2000, 2700};
    constexpr std::uint32_t normalBoss[] = {1000, 1300, 1600}, hardBoss[] = {1200, 1700, 2200};
    return mode == CoopDifficulty::Normal ? (boss ? normalBoss[i] : normal[i]) :
           mode == CoopDifficulty::Hard ? (boss ? hardBoss[i] : hard[i]) : 1000;
}
constexpr std::int32_t ScaleHealthValue(std::int32_t value, std::uint32_t factor) noexcept {
    if (value <= 0 || factor == 1000) return value;
    const auto scaled = (static_cast<std::int64_t>(value) * factor + 999) / 1000;
    return static_cast<std::int32_t>((std::min)(scaled, static_cast<std::int64_t>((std::numeric_limits<std::int32_t>::max)())));
}
// Copied into the existing native room instance; roster updates cannot relock it.
struct RoomDifficulty {
    CoopDifficulty mode{CoopDifficulty::Off};
    unsigned players{1};
    bool locked{};
    void Lock(CoopDifficulty requested, unsigned count) noexcept {
        if (locked) return;
        mode = ValidDifficulty(requested) ? requested : CoopDifficulty::Off;
        players = (std::clamp)(count, 1u, 3u);
        locked = true;
    }
    EnemyHealth Scale(EnemyHealth native, bool boss, bool alreadyApplied = false) const noexcept {
        if (alreadyApplied || !locked || mode == CoopDifficulty::Off || native.hp <= 0 || native.maxHp <= 0 || native.hp > native.maxHp)
            return native;
        const auto factor = HealthFactor(mode, players, boss);
        return {ScaleHealthValue(native.hp, factor), ScaleHealthValue(native.maxHp, factor)};
    }
};
} // namespace kh2coop
