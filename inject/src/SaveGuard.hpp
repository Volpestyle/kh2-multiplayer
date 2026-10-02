#pragma once
// ============================================================================
// SaveGuard — keeps an injected instance from writing James's saves (VUH-1488).
//
// The saves live under "My Games\KINGDOM HEARTS HD 1.5+2.5 ReMIX\" and sync
// to Steam Cloud. Once installed, any open of a file there that could write
// it (write/delete access or a creating disposition) is redirected to the
// same relative path in a per-process sandbox (<KH2COOP_LOG_DIR>\
// save_sandbox_<pid>), copying the original in first when the open keeps
// contents. The game's save writer doesn't check its fopen for NULL, so a
// redirect is safer than a denial. Deletes, moves, copies, replaces and
// directory removals there fail with ERROR_ACCESS_DENIED. Everything is
// logged as [saveguard]. Reads of the real save work as before.
// ============================================================================

#include <cstdint>

namespace kh2coop {
namespace inject {
namespace saveguard {

using LogFn = void (*)(const char* fmt, ...);

// Hooks the KernelBase file APIs. Call after MH_Initialize, before the game
// can reach its save code. Returns false (logged) if any hook failed.
bool Install(LogFn log);

// Number of operations denied so far (redirects aren't counted).
std::uint32_t BlockedCount();

} // namespace saveguard
} // namespace inject
} // namespace kh2coop
