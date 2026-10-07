#pragma once
#include <cstdint>
namespace kh2coop::inject::privatestatus {
using LogFn = void (*)(const char*, ...);
// Exact Steam 9002b2de GoA Friend1 experiment. OFF unless explicitly requested.
bool Initialize(std::uintptr_t base);
void Drain(LogFn log);
void StopNewAllocations(); // Existing ownership/veto remains until native release.
bool RetainsMinHookResources();
}
