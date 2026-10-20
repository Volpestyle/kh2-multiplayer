#pragma once
// ============================================================================
// SceneCrown — VUH-2502: the host crown drawn inside the game's own frame, so
// scenery and characters in front of the head hide it and the game's effects
// and HUD draw over it.
//
// KH2 records each frame's scene as D3D12 bundles executed from one direct
// list, in the order world, characters, effects, particles, post-process, HUD
// (docs/HOST_CROWN.md). The crown is recorded into that direct list just before
// the first scene bundle whose pipeline tests depth without writing it (the
// effects), or, if there is none, just before the list leaves the scene depth
// target. It tests and writes the game's scene depth. Afterwards the game's
// root signature and root arguments are put back on the list, since its later
// bundles inherit them. Frames without an insertion point keep the
// Present-time crown.
// ============================================================================

#include <cstddef>
#include <cstdint>

#include "kh2coop/CrownProp.hpp"

namespace kh2coop::inject::scenecrown {

using LogFn = void (*)(const char* fmt, ...);
// Fills this frame's crown in clip space (z already the scene depth) and returns the vertex count;
// 0 draws nothing. Called while the game records its frame.
using VerticesFn = std::size_t (*)(crownprop::ClipVertex* out, std::size_t max);
struct Texture { const std::uint8_t* rgba; int side; };
using TextureFn = bool (*)(Texture& out);

// Hooks pipeline creation and command recording on the D3D12 runtime. MinHook must be initialized.
bool Install(LogFn log, VerticesFn vertices, TextureFn texture);
// Crowns recorded into the game's frames so far; the Present path skips its own when this moved.
std::uint64_t Drawn() noexcept;

}  // namespace kh2coop::inject::scenecrown
