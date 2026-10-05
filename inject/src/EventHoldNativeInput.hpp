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
} // namespace kh2coop::inject::eventholdnative
