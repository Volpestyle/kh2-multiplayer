#pragma once

#include "NativeOwnedEmitterCode.hpp"
#include <cstdint>

namespace kh2coop::inject::ownedemitter::installation {

enum class Status : std::uint8_t {
    Disabled, IdentityUnavailable, IdentityMismatch, UnwindMismatch,
    PinFailed, AllocationUnavailable, PlanRejected, ProtectionFailedRetained,
    CacheFlushFailedRetained, RegistrationFailedRetained, ReadbackFailedRetained,
    PreparedCommitHeld
};
struct Snapshot {
    Status status = Status::Disabled;
    CodeStatus codeStatus = CodeStatus::Unavailable;
    bool attempted{}, stopped{}, reinitializationRejected{}, modulePinned{};
    bool resourcesMayBeReferenced{}, registered{}, identityVerified{};
    // This component NEVER exposes a native branch or grants execution.
    bool branchesExposed{}, commitAvailable{}, quiescenceProven{}, creationAuthority{};
    std::uint32_t lastError{};
    std::uintptr_t imageBase{}, allocation{}, unwindAddressBase{};
};
struct PreparedView {
    // Retained, default-inactive state. Its external owner must serialize every
    // activation and establish execution prerequisites; this is not a permit.
    PrimitiveState* state{};
    CodePlan code{};
    std::uintptr_t unwindAddressBase{};
};

// Default-off, one requested attempt per DLL lifetime. Only verifies the current
// main image; exact emitter body/parent unwind identity is a finite byte gate,
// not whole-image attestation. No native text/branch write or native call occurs.
// A true result means PreparedCommitHeld, not installed/enabled/authorized.
bool Prepare(std::uintptr_t imageBase, bool requested = false) noexcept;
Snapshot GetSnapshot() noexcept;
bool GetPreparedView(PreparedView& out) noexcept;
// Refuses future views/reinitialization; never clears a potentially live POD,
// deletes a runtime table, frees executable storage or releases the module pin.
void Stop() noexcept;
bool RequiresProcessLifetimeRetention() noexcept;

// There is deliberately no Commit or caller-bool quiescence interface. A future
// patch owner must establish a real closed admission interval for all relevant
// threads, including creation/resumption races, and handle an IP already inside
// any replaced instruction. This module neither suspends threads nor relocates
// such an IP. Native branch exposure remains HOLD.
} // namespace kh2coop::inject::ownedemitter::installation
