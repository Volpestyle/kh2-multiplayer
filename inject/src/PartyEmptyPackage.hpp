#pragma once
#include <cstddef>
#include <cstdint>

namespace kh2coop::inject::partyemptypackage {
// Loading thread only, before any empty-member write. Read-only callback permits
// owned buffer controls; nullptr selects guarded native reads. Never invokes BAR
// loading, handle registration, a native constructor, or a SAVE writer.
using ReadFn = bool (*)(std::uintptr_t address, void* destination, std::size_t bytes);
constexpr std::uintptr_t ROOT_RVA = 0x2A0F828;
constexpr std::uintptr_t HANDLE_TABLE_RVA = 0x2B0D720;
// Entire pinned hb26/hb10 BAR metadata and payloads, including inactive/future
// definitions. The only payload normalization is each type12 header's native
// activation byte +0x0E, which may be 0 or 1. Root and handle bindings bracket
// two complete passes. Unknown/unreadable/mutated packages refuse.
bool Qualified(std::uintptr_t moduleBase, std::uint8_t world, std::uint8_t room,
               std::uint16_t eventProgram, ReadFn read = nullptr);
} // namespace kh2coop::inject::partyemptypackage
