#pragma once
// ============================================================================
// RenderHook — IDXGISwapChain::Present hook: in-renderer screenshots, clip
// frames and the debug overlay. Requests arrive through CaptureChannel
// (common/include/kh2coop/CaptureChannel.hpp) from kh2ctl.
// ============================================================================

#include <cstdint>
#include "CoopHudState.hpp"

namespace kh2coop {
namespace inject {
namespace render {

using LogFn = void (*)(const char* fmt, ...);

// Creates the capture channel and hooks Present. MinHook must already be
// initialized. Returns false (and logs why) if the hook couldn't be installed;
// the rest of the DLL keeps working without it.
bool Install(uintptr_t exeBase, LogFn log);

// Game-owner thread publishes value-only telemetry. Nonblocking; failure hides
// prior HUD data. No native writes or game pointers enter the render thread.
void PublishCoopHud(const hud::Snapshot& snapshot) noexcept;
void InvalidateCoopHud() noexcept;

// Stops the encoder thread and closes the channel. Call before
// MH_DisableHook / MH_Uninitialize.
void Shutdown();

} // namespace render
} // namespace inject
} // namespace kh2coop
