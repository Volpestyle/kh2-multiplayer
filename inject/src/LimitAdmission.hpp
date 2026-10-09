#pragma once
#include <cstdint>
namespace kh2coop::inject::limitadmission {
using ActiveFn = bool (*)();
using LogFn = void (*)(const char*, ...);
bool Install(std::uintptr_t base, ActiveFn active, LogFn log);
bool Ready();
bool RejectReinitialization();
bool RetainsMinHookResources();
// Called only after shutdown's physical-party restoration/early-return barriers.
void HoldForShutdown();
void Shutdown();
// After queued global disable, preserve a Held restriction until exact owned disable succeeds.
bool QueuePreserveHeld();
}
