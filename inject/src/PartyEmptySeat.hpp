#pragma once
#include <array>
#include <cstdint>

namespace kh2coop::inject::partyempty {
using LogFn = void (*)(const char*, ...);
// These wrappers only project a synchronous, read-only row argument. They do
// not authorize an empty load: the selected spawn package needs its own gate.
bool Install(std::uintptr_t base, LogFn log);
bool Ready();
bool RetainsMinHookResources(); // created callback originals stay mapped until process exit
void NativeResolved();
void Arm(std::uint16_t remote, std::uint16_t local);
bool Shutdown(); // false retains guards if an owned zero member cannot be restored
bool DisableOtherHooks(); // queued teardown preserves retained row guards without a gap
bool ProjectRow(const std::array<std::uint8_t, 4>& row,
                std::array<std::uint8_t, 4>& projected);
}
