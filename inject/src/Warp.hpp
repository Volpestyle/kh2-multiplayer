#pragma once
// ============================================================================
// Warp — send this instance to a room on command (VUH-1486). Requests arrive
// through WarpChannel (common/include/kh2coop/WarpChannel.hpp).
// ============================================================================

#include <cstdint>

namespace kh2coop {
namespace inject {
namespace warp {

using LogFn = void (*)(const char* fmt, ...);

// Verifies the game's transition-request function and creates the channel.
// Returns false (logged) if the function bytes don't match this build.
bool Install(uintptr_t exeBase, LogFn log);

// Call on the game thread at the start of each frame's entity update, with
// the entity list head (Sora's actor). Publishes liveness and hands a
// pending request to the game.
void OnFrameStart(std::uint32_t frame, uintptr_t listHead);

void Shutdown();

} // namespace warp
} // namespace inject
} // namespace kh2coop
