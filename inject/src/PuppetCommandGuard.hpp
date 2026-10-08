#pragma once
#include <cstdint>

namespace kh2coop::inject::puppetcommand {
using ActiveFn = bool (*)();
using LogFn = void (*)(const char*, ...);
// All three native gates are mandatory before an entity hook can admit puppets.
bool Install(std::uintptr_t base, ActiveFn active, LogFn log);
bool Ready();
void Shutdown();
// Created trampolines and callbacks are retained until process exit, including
// failed installation/teardown. Never free an original under a retired callback.
bool RetainsMinHookResources();
}
