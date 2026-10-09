#pragma once
#include "PopulationAuthorityTrace.hpp"
#include "NativeSpawnController.hpp"

namespace kh2coop::inject::populationauthority {
using authoritytrace::Kind;
using authoritytrace::Token;
// Setup and drain only; hooks never invoke FLS setup, logging or allocation.
bool Configure(std::uintptr_t base,spawncontroller::LogFn log);
bool Requested();
bool Retained();
void Coverage(std::uint32_t installedMask);
// Installation only: qualify the exact MinHook relocation and register retained
// unwind metadata before enabling the diagnostic detour.
bool PrepareTrampoline(std::uintptr_t rva,void* original);
Token Enter(Kind kind,std::uintptr_t controller,std::uintptr_t record,std::uintptr_t actor,std::uintptr_t caller);
Token EnterFactory(std::uint32_t rawId,const float* point,float yaw,std::uintptr_t caller);
void Exit(Token token,Kind kind,std::uintptr_t controller,std::uintptr_t record,std::uintptr_t actor,
          std::uint64_t result,bool returned);
void Frame();
void Stop();
} // namespace kh2coop::inject::populationauthority
