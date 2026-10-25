#pragma once
#include <cstdint>
namespace kh2coop { struct EventStart; namespace eventhold { struct Scope; } }
namespace kh2coop::inject::enemysync {
// Actual collector callback only, under its existing entry guard and fresh
// unchanged native safety/convergence/health checks. Returns -1 after a native
// attempt whose result is unavailable, 1 after a start, otherwise0.
int ServiceThornDefeat(const eventhold::Scope&, bool nativeSafe, bool converged) noexcept;
// A matching host315 joins the already owned native sequence, never replays it.
bool OwnsThorn315(const EventStart&, std::uint16_t eventProgram) noexcept;
// Hooked native start: false refuses only a second exact315 in this occurrence.
bool PermitThorn315(const EventStart&, std::uint16_t eventProgram) noexcept;
void CompleteThorn315(const EventStart&, std::uint16_t eventProgram, std::uintptr_t before,
                      std::uintptr_t after) noexcept;
} // namespace kh2coop::inject::enemysync
