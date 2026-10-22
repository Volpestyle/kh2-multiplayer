#pragma once
#include "kh2coop/Protocol.hpp"
#include <vector>

namespace kh2coop {
// Owned by the game thread. Keep consumed receipts until the room ends so
// duplicate transport/cache delivery cannot grant another spawn. A failed
// native allocation leaves its receipt pending for the next ordinary tick.
// VUH-2466: Struggle orb rows share the ledger but never answer a controller lookup; the client
// takes them in arrival order with TakeOrbs().
class SpawnEmissionLedger {
    struct Receipt { SpawnEmission emission; bool consumed = false; bool reserved = false; };
    std::vector<Receipt> receipts_;
    std::uint64_t floor_ = 0;
public:
    void Clear() { receipts_.clear(); floor_ = 0; }
    bool Add(const SpawnEmission& e) {
        if (!e.sequence || e.sequence <= floor_ || receipts_.size() >= SPAWN_EMISSION_CAP) return false;
        // A combat occurrence grants one birth, even if the host retries it
        // under a new sequence after the original receipt was consumed.
        if (e.netId) for (const auto& r : receipts_)
            if (r.emission.netId == e.netId) return false;
        // An orb is born once and collected once, whatever the transport repeats.
        if (e.kind != SpawnEmissionKind::Controller) for (const auto& r : receipts_)
            if (r.emission.kind == e.kind && r.emission.orbId == e.orbId) return false;
        receipts_.push_back({e, false, false}); floor_ = e.sequence; return true;
    }
    const SpawnEmission* Pending(std::uint32_t key, std::uint16_t header) const {
        for (const auto& r : receipts_)
            if (!r.consumed && r.emission.kind == SpawnEmissionKind::Controller &&
                r.emission.controllerKey == key && r.emission.headerId == header) return &r.emission;
        return nullptr;
    }
    // Exact FIFO receipt for the impending local native birth. Copy its
    // identity before another Add(), which can move vector storage.
    const SpawnEmission* Pending(std::uint32_t key, std::uint16_t header, std::uint16_t index) const {
        const auto* next = Pending(key, header);
        return next && next->recordIndex == index ? next : nullptr;
    }
    // Reserve before crossing the native allocation call. Nested calls cannot
    // borrow this receipt or skip it to the next birth from this controller.
    const SpawnEmission* Reserve(std::uint32_t key, std::uint16_t header, std::uint16_t index) {
        for (auto& r : receipts_) {
            if (r.consumed || r.emission.kind != SpawnEmissionKind::Controller ||
                r.emission.controllerKey != key || r.emission.headerId != header) continue;
            if (r.reserved || r.emission.recordIndex != index) return nullptr;
            r.reserved = true;
            return &r.emission;
        }
        return nullptr;
    }
    bool Commit(std::uint64_t sequence) {
        for (auto& r : receipts_) if (r.emission.sequence == sequence && r.reserved && !r.consumed) {
            r.reserved = false; r.consumed = true; return true;
        }
        return false;
    }
    bool Cancel(std::uint64_t sequence) {
        for (auto& r : receipts_) if (r.emission.sequence == sequence && r.reserved && !r.consumed) {
            r.reserved = false; return true;
        }
        return false;
    }
    bool Permit(std::uint32_t key, std::uint16_t header, std::uint16_t index, bool consume) {
        // Preserve the host's order within each controller, including refills.
        for (auto& r : receipts_) {
            if (r.consumed || r.emission.kind != SpawnEmissionKind::Controller ||
                r.emission.controllerKey != key || r.emission.headerId != header) continue;
            if (r.reserved || r.emission.recordIndex != index) return false;
            if (consume) r.consumed = true;
            return true;
        }
        return false;
    }
    // Every orb row not yet taken, in arrival order (the host's sequence order).
    std::vector<SpawnEmission> TakeOrbs() {
        std::vector<SpawnEmission> out;
        for (auto& r : receipts_)
            if (!r.consumed && r.emission.kind != SpawnEmissionKind::Controller) { r.consumed = true; out.push_back(r.emission); }
        return out;
    }
};
} // namespace kh2coop
