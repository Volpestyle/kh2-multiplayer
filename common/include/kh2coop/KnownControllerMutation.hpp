#pragma once

#include <atomic>
#include <cstdint>

namespace kh2coop {

// One sampled negative fence for three known native mutation entries. This is
// not an incarnation, exclusive interval, creator census or creation permission.
struct KnownControllerMutationTicket {
    std::uint64_t revision = 0;
    std::uint32_t inFlight = 0;
    bool available = false;
    bool poisoned = false;
    bool coverageComplete = false;
};

// Internal producer state. One lock-free word gives readers a coherent sample;
// it does not prevent a mutation starting immediately after that sample.
// Poison is permanent for this object/DLL lifetime, including across shutdown.
struct KnownControllerMutationFence {
    static constexpr std::uint64_t InFlightMask = 0xFFFF;
    static constexpr std::uint64_t RevisionStep = 1ULL << 16;
    static constexpr std::uint64_t CoverageBit = 1ULL << 62;
    static constexpr std::uint64_t PoisonBit = 1ULL << 63;
    static constexpr std::uint64_t RevisionMask = (CoverageBit - 1) & ~InFlightMask;
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    std::atomic<std::uint64_t> word {0};

    KnownControllerMutationTicket Snapshot() const noexcept {
        const auto value = word.load();
        KnownControllerMutationTicket out;
        out.revision = (value & RevisionMask) / RevisionStep;
        out.inFlight = static_cast<std::uint32_t>(value & InFlightMask);
        out.poisoned = (value & PoisonBit) != 0;
        out.coverageComplete = (value & CoverageBit) != 0;
        out.available = out.coverageComplete && !out.poisoned && !out.inFlight && out.revision;
        return out;
    }
    bool Current(const KnownControllerMutationTicket& ticket) const noexcept {
        const auto now = Snapshot();
        return ticket.available && !ticket.poisoned && ticket.coverageComplete && !ticket.inFlight &&
            now.available && ticket.revision == now.revision;
    }
    void Poison() noexcept { word.fetch_or(PoisonBit); }
    void CloseCoverage() noexcept {
        auto old = word.load();
        while (!word.compare_exchange_weak(old, (old | PoisonBit) & ~CoverageBit)) {}
    }
    void CompleteCoverage() noexcept {
        auto old = word.load();
        do {
            if (old & PoisonBit) return;
        } while (!word.compare_exchange_weak(old, old | CoverageBit |
            ((old & RevisionMask) ? 0 : RevisionStep)));
    }
    // Must run before any native read/call. False means poison, and End must not
    // decrement a count this invocation never acquired. Native still runs once.
    bool Begin() noexcept {
        auto old = word.load();
        for (;;) {
            if ((old & RevisionMask) == RevisionMask || (old & InFlightMask) == InFlightMask) {
                Poison();
                return false;
            }
            if (word.compare_exchange_weak(old, old + RevisionStep + 1)) return true;
        }
    }
    void End(bool counted, bool unwound) noexcept {
        if (unwound) Poison();
        if (!counted) return;
        auto old = word.load();
        for (;;) {
            if (!(old & InFlightMask)) { Poison(); return; }
            if (word.compare_exchange_weak(old, old - 1)) return;
        }
    }
};

} // namespace kh2coop
