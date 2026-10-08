#pragma once
#include "DownedSpectate.hpp"
#include <atomic>

namespace kh2coop::inject::spectate {
// Close prevents new feature accesses; an acquired reader owns restoration
// authority until Leave. Late detour entries use process-lifetime trampolines.
class CallbackGate {
    static constexpr std::uint32_t Closed = 1U << 31;
    std::atomic<std::uint32_t> state_ {};
    std::atomic<bool> retained_ {};
public:
    void RetainHooks() noexcept { retained_.store(true, std::memory_order_release); }
    bool RetainsHooks() const noexcept { return retained_.load(std::memory_order_acquire); }
    bool Enter() noexcept {
        auto value = state_.load(std::memory_order_acquire);
        while (!(value & Closed)) {
            if (state_.compare_exchange_weak(value, value + 1, std::memory_order_acq_rel)) return true;
        }
        return false;
    }
    void Leave() noexcept { state_.fetch_sub(1, std::memory_order_release); }
    void Close() noexcept { state_.fetch_or(Closed, std::memory_order_acq_rel); }
    bool Drained() const noexcept { return (state_.load(std::memory_order_acquire) & ~Closed) == 0; }
};
template<class StopFollow, class Yield, class Retire>
bool DrainCallbacks(CallbackGate& gate, bool insideCallback, StopFollow stopFollow, Yield yield, Retire retire) {
    gate.Close();
    stopFollow();
    if (insideCallback) return false; // never retire the authority beneath ourselves
    while (!gate.Drained()) yield();
    retire();
    return true;
}
struct CandidateFacts {
    RemoteFacts remote {};
    std::uint64_t actor{}, localActor{};
    std::array<std::uint64_t, 2> clones{}, friends{};
    unsigned index{}, slot{255}, localSlot{255}, planned{}, present{};
    bool kits{}, blocksPlayer{};
    std::uint32_t wantKit{}, otherKit{};
};
struct CandidateMetadata { Target target{}; std::uint32_t objectId{}, type{}; };
// Cached pointers participate only in equality tests. The native adapter must
// prove current census membership BEFORE its guarded metadata reader runs.
template<class Current, class Read>
Target ResolveCandidate(const CandidateFacts& f, Current current, Read read) {
    if (f.index >= 2 || f.slot >= 3 || f.slot == f.localSlot || f.localSlot >= 3 ||
        !f.actor || f.actor == f.localActor || !EligibleRemote(f.remote)) return {};
    const bool clone = f.planned || f.clones[0];
    if (f.planned == 1) {
        if (f.index != f.present || f.actor != f.clones[0] || f.blocksPlayer) return {};
    } else if (clone) {
        if (f.actor != f.clones[0] && f.actor != f.clones[1]) return {};
    } else if (f.actor != f.friends[f.index]) return {};
    if (!current(f.actor)) return {};
    const auto m = read(f.actor);
    if (!m.target.valid || m.target.actor != f.actor || (clone && m.type != 0) ||
        (f.blocksPlayer && m.type == 0) ||
        (clone && f.kits && f.wantKit && f.otherKit && f.wantKit != f.otherKit && m.objectId != f.wantKit) ||
        !current(f.actor)) return {};
    return m.target;
}
} // namespace kh2coop::inject::spectate
