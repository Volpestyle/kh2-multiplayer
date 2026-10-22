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
// VUH-2640 F2, the owner service: on the game thread, each input-collector callback before ApplyInput
// (the collector runs while menus 8/10 are open: it is what drives them). Decides the orphaned pause's next
// bounded press from its own fully validated sample of this callback's collector instance; ApplyInput only
// emits it.
void ServiceOrphanedPause(void* inputStruct) noexcept;
// VUH-2640: the press (or neutral frame, 0) ApplyInput overlaid for the orphaned pause in this callback.
// The downed hold keeps exactly these buttons; nothing else passes it.
bool OrphanPressThisCallback(std::uint16_t& buttons) noexcept;
} // namespace kh2coop::inject::eventholdnative
