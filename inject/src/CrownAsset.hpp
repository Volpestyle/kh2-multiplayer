#pragma once
// VUH-2480: loads KH2 Final Mix's crown (kh2coop/CrownProp.hpp) from the player's own install,
// once, on a background thread. Reads only: the game's .hed/.pkg files and the EGS key table in
// the running game's image. Until it is ready, or if it fails, the host keeps the painted marker.

#include "kh2coop/CrownProp.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace kh2coop::inject::crownasset {

struct Loaded {
    crownprop::Prop prop;
    std::vector<std::uint8_t> rgba;  // gold sprite, side x side RGBA8
    int side {};
    std::string source;              // e.g. "kh2_sixth.hed"
};

using LogFn = void (*)(const char* fmt, ...);

// Pure of process state: finds AssetName in imageDir\kh2_*.hed and builds the prop.
bool LoadFromInstall(const std::wstring& imageDir, std::span<const std::uint8_t, 256> keyTable, Loaded& out,
                     std::string& why);

// Starts the one background load (DLL init). exeBase is the game's image, for the key table.
void Start(std::uintptr_t exeBase, LogFn log);
// The loaded crown once ready, else nullptr. Immutable after publication.
const Loaded* Ready() noexcept;

}  // namespace kh2coop::inject::crownasset
