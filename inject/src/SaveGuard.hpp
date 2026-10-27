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
//
// Installed assets under any "\KH2ReSource\" folder get the same treatment
// (VUH-2775): the game's cipher movies decrypt their header in place before
// playback and restore it afterwards, so a write open there is redirected to
// <sandbox>\install-assets\<path below KH2ReSource> (a full copy of the
// original, about 63 MB for a post-boss movie) and later reads of that file
// follow the twin. The installed file is never opened for writing, and the
// KH2ReSource root itself can't be moved or removed. Asset writes match the
// resolved path (8.3, junction and \\?\ aliases); a twin is published only as
// a whole copy, and any doubt about it fails the write (VUH-2763 review). A
// sandbox under KH2ReSource disables asset twins only. Install prunes
// install-assets twins from all but the newest six finished sandboxes, within
// a fixed work and time budget.
// ============================================================================

#include <cstdint>

namespace kh2coop {
namespace inject {
namespace saveguard {

using LogFn = void (*)(const char* fmt, ...);

// Hooks the KernelBase file APIs. Call after MH_Initialize, before the game
// can reach its save code. Returns false (logged) if any hook failed.
bool Install(LogFn log);

// After Install succeeded AND every initialization failure/MinHook teardown
// path has been passed, authorize a suspended kh2ctl child to begin boot.
void AcknowledgeLaunch();

// Number of operations denied so far (redirects aren't counted).
std::uint32_t BlockedCount();

} // namespace saveguard
} // namespace inject
} // namespace kh2coop
