#pragma once
#include "EventHoldNativeInput.hpp"
#include "kh2coop/EventHoldControl.hpp"
#include "kh2coop/ResyncProtocol.hpp"

namespace kh2coop::inject::eventholdnative {
eventhold::Channel& Control() noexcept;
// Owner only: after the normal world consumer accepted the corresponding fact.
void Observe(const eventhold::Scope& scope, eventhold::Kind kind, std::uint64_t source,
             std::uint32_t epoch, const RoomTransition* room, std::uint16_t eventProgram,
             const EventStart* start = nullptr);
void RetireOwner() noexcept;
// Game owner only, after matching relay result AND native two-frame resync
// convergence. Never called in the collector; old abort remains immutable.
bool BeginRecoveredLifetime(const eventhold::Scope& scope, std::uint32_t epoch);
void OwnerAck(const eventhold::Scope& scope, std::uint32_t epoch, bool eligible, bool converged);
} // namespace kh2coop::inject::eventholdnative
