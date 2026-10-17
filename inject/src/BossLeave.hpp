#pragma once
// VUH-2367: the host's boss died, but its brainless copy on a client refused the native lethal (its own
// script plays the death on the host; Shan-Yu's copy went 9 -> 221 HP). The client fades it out through
// the native leave instead (VUH-2272), once. Pure rule; the native leaf is ApplyNativeBossLeave in
// EnemySync.cpp, which re-checks the objentry type at write time.
#include <cstdint>

namespace kh2coop::inject::bossleave {

inline constexpr std::uint8_t kBossType = 3;  // objentry TYPE_FLAGS: 3 boss, 4 mob

// After a host death's lethal: fall back only for a boss still alive, outside a resync's record-authority
// write fence, and only once per local spawn.
inline bool Fallback(std::int32_t hpAfterLethal, std::uint32_t objectType, bool contentRequired, bool attempted) noexcept {
    return hpAfterLethal > 0 && objectType == kBossType && !contentRequired && !attempted;
}

}  // namespace kh2coop::inject::bossleave
