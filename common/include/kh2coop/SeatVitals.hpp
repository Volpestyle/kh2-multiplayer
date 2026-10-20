#pragma once
#include <cmath>
#include <cstdint>

namespace kh2coop {
// Owner-authored display state. Never a native status record or save payload.
struct SeatVitals {
    std::int32_t hp{}, maxHp{}, mp{}, maxMp{};
    float charge{}, chargeMax{};
    std::uint16_t objectId{};
    std::uint8_t flags{};
    static constexpr std::uint8_t Available = 1, Downed = 2;
    bool operator==(const SeatVitals&) const = default;
};
inline bool validSeatVitals(const SeatVitals& v) {
    if (v.flags & ~3u) return false;
    if (!(v.flags & SeatVitals::Available)) return v == SeatVitals{};
    return v.objectId && v.hp >= 0 && v.maxHp > 0 && v.hp <= v.maxHp && v.maxHp <= 99999 &&
        v.mp >= 0 && v.maxMp >= 0 && v.mp <= v.maxMp && v.maxMp <= 9999000 &&
        std::isfinite(v.charge) && std::isfinite(v.chargeMax) && v.charge >= 0 &&
        v.chargeMax >= 0 && v.charge <= v.chargeMax && v.chargeMax <= 10000000 &&
        (!(v.flags & SeatVitals::Downed) || v.hp == 0);
}
}
