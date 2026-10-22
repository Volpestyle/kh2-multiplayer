#pragma once
#include <cstdint>
namespace kh2coop::inject::eventholdnative {
using LogFn = void (*)(const char*, ...);
void Install(uintptr_t base, LogFn log);
void Disable() noexcept;
bool Enabled() noexcept;
bool EnterInput() noexcept;
void LeaveInput() noexcept;
bool HoldingInput() noexcept;
void AbortInput() noexcept;
bool ApplyInput(void* inputStruct);
// VUH-2640: this seat's current event-control lifetime ended with HeartbeatExpired (the old abort stays
// until a recovered lifetime replaces it). A read of the immutable abort only; no rebinding or clearing.
bool LifetimeHeartbeatExpired() noexcept;
} // namespace kh2coop::inject::eventholdnative
