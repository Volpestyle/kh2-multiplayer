#pragma once
#include "kh2coop/HudRoster.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <array>
#include <cstring>
#include <limits>

namespace kh2coop::hudnames {
// Windows x64 cross-process POD. All shared words use Interlocked operations,
// including payload reads: unlike a plain memcpy seqlock there is no concurrent
// ordinary C++ payload access. No allocation, waiting, retries or OS handle work.
// Runtime main/pump is the sole publisher. A partial/crashed write stays odd and
// unavailable until mapping lifetime ends; readers never reuse a cached name.
struct alignas(64) Slot {
    volatile LONG64 sequence {};
    volatile LONG words[sizeof(Roster) / sizeof(LONG)] {};
    LONG reserved[2] {}; // explicit tail padding: fixed128-byte local ABI

    bool TryWrite(const Roster& value) noexcept {
        const auto prior = InterlockedCompareExchange64(&sequence, 0, 0);
        if (prior < 0 || (prior & 1) || prior >= (std::numeric_limits<LONG64>::max)() - 2)
            return false; // Never wrap/reuse a sequence.
        if (InterlockedCompareExchange64(&sequence, prior + 1, prior) != prior) return false;
        std::array<LONG, sizeof(Roster) / sizeof(LONG)> copy {};
        std::memcpy(copy.data(), &value, sizeof(value));
        for (std::size_t i = 0; i < copy.size(); ++i) InterlockedExchange(&words[i], copy[i]);
        InterlockedExchange64(&sequence, prior + 2);
        return true;
    }
    bool TryRead(Roster& out) noexcept {
        out = {};
        const auto before = InterlockedCompareExchange64(&sequence, 0, 0);
        if (before <= 0 || (before & 1)) return false;
        std::array<LONG, sizeof(Roster) / sizeof(LONG)> copy {};
        for (std::size_t i = 0; i < copy.size(); ++i)
            copy[i] = InterlockedCompareExchange(&words[i], 0, 0);
        if (InterlockedCompareExchange64(&sequence, 0, 0) != before) return false;
        std::memcpy(&out, copy.data(), sizeof(out));
        return true;
    }
};
static_assert(sizeof(LONG) == 4 && sizeof(Slot) == 128 && alignof(Slot) == 64 &&
              std::is_trivially_copyable_v<Slot> && std::is_standard_layout_v<Slot>);
} // namespace kh2coop::hudnames
