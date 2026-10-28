#pragma once
// VUH-2912 stats marks (lead #76, run vuh2912-hp-09): the scenario runner asks each seat to log the mirror's per-actor
// stats line at a round's marks, so a window shorter than the 300-frame stats interval still holds a sample. Opened
// only with the hit trace on. The runner bumps `requested`; the game thread logs once per new value and stores it in
// `logged`. Diagnostic: it reads and logs only. Layout mirrored by tools/scenario/run.py (STATS_MARK_*).
#include <cstdint>

namespace kh2coop::inject::statsmark {

constexpr const wchar_t* kNamePrefix = L"Local\\kh2coop_mark_";
constexpr std::uint32_t kMagic = 0x4B52414Du;  // "MARK"
constexpr std::uint32_t kVersion = 1;

// Fixed-width fields: `long` is 8 bytes on LP64 (macOS), which made this 24 bytes there (Ione, a8cd4f9).
struct Channel {
    std::uint32_t magic;
    std::uint32_t version;
    volatile std::int32_t requested;  // written by the runner
    volatile std::int32_t logged;     // written by the game thread after it logged `requested`
};
static_assert(sizeof(Channel) == 16, "tools/scenario/run.py reads 16 bytes");

// A request is due when the runner's value differs from the last one logged.
inline bool Due(const Channel& c, std::int32_t seen) noexcept { return c.magic == kMagic && c.requested != seen; }

} // namespace kh2coop::inject::statsmark
