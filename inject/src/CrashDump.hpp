#pragma once
// ============================================================================
// CrashDump — a minidump for every unhandled exception in an injected
// instance (VUH-1488), so a crashed scenario leaves evidence.
//
// The dump goes to KH2COOP_LOG_DIR (the rig's log folder) as
// kh2coop_crash_<pid>.dmp, next to the inject log. The game installs its own
// unhandled-exception filter later in init; SetUnhandledExceptionFilter is
// hooked so that filter is chained after ours instead of replacing it.
// ============================================================================

namespace kh2coop {
namespace inject {
namespace crashdump {

using LogFn = void (*)(const char* fmt, ...);

// Call after MH_Initialize. Returns false (logged) if the hook failed; the
// filter is still installed in that case, but the game may replace it.
bool Install(LogFn log);

} // namespace crashdump
} // namespace inject
} // namespace kh2coop
