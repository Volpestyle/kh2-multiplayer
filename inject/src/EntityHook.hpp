#pragma once
// ============================================================================
// EntityHook — PerEntityUpdate hook + friend AI replacement
//
// Public API for the injection system. Called from DllMain / Panacea exports.
// ============================================================================

#include <cstdint>

namespace kh2coop {
namespace inject {

// Initialize the hook system. Installs MinHook detours on PerEntityUpdate
// and prepares friend entity identification. Call once after the game module
// is loaded.
// Returns true on success.
bool Initialize(uintptr_t exeBase);

// Remove all hooks and clean up. Call on DLL unload.
void Shutdown();

// Per-frame tick. Reads gamepads and performs periodic status logging.
// Called from the Panacea OnFrame export or internally via the
// PerEntityUpdate hook.
void OnFrame();

} // namespace inject
} // namespace kh2coop
