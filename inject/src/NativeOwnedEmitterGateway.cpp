#include "NativeOwnedEmitterGateway.hpp"

#include <Windows.h>
#include <atomic>
#include <cstddef>

extern "C" std::uint64_t Kh2OwnedEmitterGatewayRaw(
    kh2coop::inject::ownedemitter::PrimitiveState*, std::uint64_t, std::uint64_t, std::uint64_t);

namespace kh2coop::inject::ownedemitter {
namespace {
std::atomic<bool> outstanding{false};
std::uint64_t lastInvocation{}; // protected by outstanding; never reset
bool Address(std::uint64_t a, std::uint64_t bytes = 1) noexcept {
    return a >= 0x10000 && a < 0x800000000000ULL && bytes <= 0x800000000000ULL - a;
}
// Every offset used by the assembly is tied to the actual shared state.
static_assert(sizeof(PrimitiveState) == 280);
#define OFFSET(field, value) static_assert(offsetof(PrimitiveState, field) == value)
OFFSET(live,0); OFFSET(s,8); OFFSET(w,16); OFFSET(d,24); OFFSET(e,32);
OFFSET(gatewayRA,40); OFFSET(emitterRA,48); OFFSET(wrapperRA,56);
OFFSET(controller,64); OFFSET(invocation,120); OFFSET(continueInput,144);
OFFSET(retired,152); OFFSET(unwound,160); OFFSET(activation,200);
OFFSET(phase,208); OFFSET(unknown,248);
#undef OFFSET
}
GatewayResult InvokeOwnedEmitterGateway(const GatewayRequest& request) {
    bool expected = false;
    if (!outstanding.compare_exchange_strong(expected, true, std::memory_order_acquire))
        return {GatewayStatus::Busy, 0};
    GatewayResult result{};
    __try {
        const GatewayRequest r = request;
        if (!r.state || !Address(reinterpret_cast<std::uint64_t>(r.state), sizeof(PrimitiveState))
            || reinterpret_cast<std::uint64_t>(r.state) % alignof(PrimitiveState)
            || !Address(r.originalDispatcher) || !Address(r.controller)
            || !Address(r.emitterReturnPc) || !Address(r.wrapperReturnPc) || !r.invocation
            || !Address(r.expectedHeader, 44) || !Address(r.recordBase, 5 * 64)) {
            result.status = GatewayStatus::InvalidInput;
        } else if (r.invocation <= lastInvocation) {
            result.status = GatewayStatus::StaleInvocation;
        } else if (r.state->live || r.state->retired || r.state->unwound || r.state->activation
                   || r.state->controller != r.controller || r.state->expectedHeader != r.expectedHeader
                   || r.state->recordBase != r.recordBase
                   || r.state->invocation != r.invocation || r.state->continueInput != 1) {
            result.status = GatewayStatus::StateUnavailable;
        } else {
            // Supplied content/attempt fields stay intact. This initialization
            // cannot attest that originalDispatcher is the validated original.
            lastInvocation = r.invocation;
            r.state->controller = r.controller;
            r.state->emitterRA = r.emitterReturnPc;
            r.state->wrapperRA = r.wrapperReturnPc;
            result.rawDispatcherRax = Kh2OwnedEmitterGatewayRaw(
                r.state, r.originalDispatcher, r.controller, r.invocation);
            result.status = GatewayStatus::Returned;
        }
    } __finally {
        outstanding.store(false, std::memory_order_release);
    }
    return result;
}
} // namespace kh2coop::inject::ownedemitter
