#pragma once
#include <array>
#include <cstdint>

namespace kh2coop::inject::partyempty {
using LogFn = void (*)(const char*, ...);
// Row and scoped menu wrappers project synchronous, read-only arguments. They do
// not authorize an empty load: the selected spawn package needs its own gate.
bool Install(std::uintptr_t base, LogFn log);
bool Ready();
bool RetainsMinHookResources(); // created callback originals stay mapped until process exit
void NativeResolved();
void Arm(std::uint16_t remote, std::uint16_t local);
void ArmCompanionTuple(const std::array<std::uint16_t, 3>& members);
void ArmTuple(const std::array<std::uint16_t, 3>& members, unsigned missingIndex);
// Paired ownership requires a successfully restored, independently reread native tuple.
// Without that proof (including unreadable members), retain the history guards.
bool Shutdown(const std::array<std::uint16_t, 3>* restoredNative = nullptr);
bool DisableOtherHooks(); // queued teardown preserves retained row guards without a gap
bool ProjectRow(const std::array<std::uint8_t, 4>& row,
                std::array<std::uint8_t, 4>& projected, unsigned missingIndex = 1);
}
