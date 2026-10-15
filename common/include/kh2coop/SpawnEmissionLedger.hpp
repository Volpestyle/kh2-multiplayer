#pragma once
#include "kh2coop/Protocol.hpp"
#include <vector>

namespace kh2coop {
// Owned by the game thread. Keep consumed receipts until the room ends so
// duplicate transport/cache delivery cannot grant another spawn. A failed
// native allocation leaves its receipt pending for the next ordinary tick.
class SpawnEmissionLedger {
    struct Receipt { SpawnEmission emission; bool consumed = false; };
    std::vector<Receipt> receipts_;
    std::uint64_t floor_ = 0;
public:
    void Clear() { receipts_.clear(); floor_ = 0; }
    bool Add(const SpawnEmission& e) {
        if (!e.sequence || e.sequence <= floor_ || receipts_.size() >= SPAWN_EMISSION_CAP) return false;
        receipts_.push_back({e, false}); floor_ = e.sequence; return true;
    }
    const SpawnEmission* Pending(std::uint32_t key, std::uint16_t header) const {
        for (const auto& r : receipts_)
            if (!r.consumed && r.emission.controllerKey == key && r.emission.headerId == header) return &r.emission;
        return nullptr;
    }
    bool Permit(std::uint32_t key, std::uint16_t header, std::uint16_t index, bool consume) {
        // Preserve the host's order within each controller, including refills.
        for (auto& r : receipts_) {
            if (r.consumed || r.emission.controllerKey != key || r.emission.headerId != header) continue;
            if (r.emission.recordIndex != index) return false;
            if (consume) r.consumed = true;
            return true;
        }
        return false;
    }
};
} // namespace kh2coop
