#pragma once
// VUH-2359: in-field event mirroring. The host records the arguments of each checked in-field event
// start (EventScenes.hpp InFieldEvents) at the native start FUN_1403AC350, and its event-hold producer
// publishes them with the Acquire for the context that start installed. A friend standing in the same
// field location calls the same native start with the same arguments, so it plays the host's event
// with its own camera and dialog, and leaves it like a scene: its own exit is blocked and it takes the
// host's next room. Installed with the event hold (KH2COOP_EVENT_HOLD_PRODUCER or _CONTROL); a
// refused install leaves every event on the hold.
#include <cstdint>

namespace kh2coop { struct EventStart; }  // kh2coop/Protocol.hpp
namespace kh2coop::inject::eventstartmirror {
using LogFn = void (*)(const char* fmt, ...);

void Install(std::uintptr_t exeBase, LogFn log);
bool Installed() noexcept;
// Host, game thread: the checked start that installed `context`, if that is the current one.
bool StartFor(std::uintptr_t context, EventStart& out) noexcept;
// Friend, game thread (the input callback): starts the host's event if it qualifies and this seat is
// in the same open field location (in field, no menu, cutscene or event). False leaves the game as it
// was; the caller keeps the event hold.
bool Replay(const EventStart& start, std::uint16_t eventProgram) noexcept;
}  // namespace kh2coop::inject::eventstartmirror
