#pragma once

#include "NativeOwnedEmitterCode.hpp"

namespace kh2coop::inject::ownedemitter {

// Internal direct-call boundary, unused until a separately reviewed consumer
// establishes the native target, installed A/B/C gates, content/lifetime and B1.
// This request is not an execution permit and does not validate native bytes.
struct GatewayRequest {
    PrimitiveState* state{}; // stable writable 280B; same address as CodePlan
    std::uint64_t originalDispatcher{}; // original/trampoline, never hooked entry
    std::uint64_t controller{};
    std::uint64_t invocation{}; // supplied immutable nonzero strictly increasing ID
    std::uint64_t emitterReturnPc{}; // validated native M+3FE39A
    std::uint64_t wrapperReturnPc{}; // validated native M+3FE83F
    std::uint64_t expectedHeader{}, recordBase{}; // immutable supplied content association
};
enum class GatewayStatus : std::uint8_t { InvalidInput, Busy, StaleInvocation, StateUnavailable, Returned };
struct GatewayResult {
    GatewayStatus status = GatewayStatus::InvalidInput;
    std::uint64_t rawDispatcherRax{}; // all64 bits, including native type2 AL1
};

// Single outstanding activation across all threads/fibers. No waiting or TLS
// stack. Rejected calls do not mutate the sidecar. Accepted invocation IDs are
// consumed permanently, including unwind. There is no reset/rearm API.
// Caller owns immutable request/state storage until this call returns/unwinds;
// only continueInput may be cleared by the supported serialized cancellation
// path. It is a primitive conditional, not deadline/context/creator authority.
// Native SEH propagates; assembly retires frame identity during actual unwind,
// before the enclosing C++ finally releases the single-outstanding guard.
GatewayResult InvokeOwnedEmitterGateway(const GatewayRequest& request);

} // namespace kh2coop::inject::ownedemitter
