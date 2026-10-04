#pragma once
// ============================================================================
// AvatarSync — runtime-side routing between the network and the puppets.
//
// Keeps one AvatarInterpolator per remote owner, assigns remote owners to the
// two friend-slot puppets (lowest slot first, stable while both stay), and
// decides each puppet's visibility: shown only with fresh data, in the local
// player's room, and not flagged as in a cutscene/load. Platform-free so it
// can be tested without KH2 or shared memory.
// ============================================================================

#include "kh2coop/AvatarInterpolator.hpp"
#include "kh2coop/Protocol.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace kh2coop {

struct PuppetTarget {
    bool active {false};
    std::uint64_t ownerConnectionId {0}; // identity admitted with this interpolation buffer
    AvatarState pose {};
};

class AvatarSync {
public:
    struct Config {
        std::uint32_t renderDelayMs {120};  // behind estimated server time
        std::uint32_t staleAfterMs {1000};  // hide when no data for this long
    };

    explicit AvatarSync(SlotType localSlot) : AvatarSync(localSlot, Config {}) {}
    AvatarSync(SlotType localSlot, Config config)
        : localSlot_(localSlot), config_(config) {}

    void setLocalSlot(SlotType slot) {
        setRoster(slot, {}); // no slot-only assignment can preserve authenticated poses
    }

    void setRoster(SlotType localSlot, const std::array<std::uint64_t, 3>& ids) {
        const auto local = static_cast<unsigned>(localSlot);
        bool valid = local < 3 && ids[0] != 0 && ids[local] != 0;
        for (unsigned i = 0; i < 3; ++i)
            for (unsigned j = i + 1; j < 3; ++j)
                if (ids[i] != 0 && ids[i] == ids[j]) valid = false;
        if (!valid || !rosterValid_ || localSlot != localSlot_ || ids[0] != roster_[0] ||
            (local < 3 && ids[local] != roster_[local])) {
            clear();
        } else {
            for (unsigned i = 0; i < 3; ++i)
                if (ids[i] != roster_[i]) { interp_[i].clear(); admittedIds_[i] = 0; }
        }
        localSlot_ = localSlot;
        roster_ = valid ? ids : std::array<std::uint64_t, 3> {};
        rosterValid_ = valid;
    }

    // Admit the relay's immutable connection identity before buffering. A
    // replacement clears that buffer; no old pose can be relabeled at sample.
    bool onRemote(const AvatarRelay& relay) {
        const auto& s = relay.avatar;
        const int owner = static_cast<int>(s.ownerSlot);
        if (!rosterValid_ || owner < 0 || owner > 2 || s.ownerSlot == localSlot_ ||
            relay.ownerConnectionId == 0 || relay.ownerConnectionId != roster_[owner]) return false;
        if (!interp_[owner].push(s)) return false;
        admittedIds_[owner] = relay.ownerConnectionId;
        return true;
    }

    // Owners of puppet 0 (friend slot 1) and puppet 1 (friend slot 2):
    // the remote slots in ascending order.
    [[nodiscard]] std::array<int, 2> puppetOwners() const {
        std::array<int, 2> owners {-1, -1};
        int n = 0;
        for (int slot = 0; slot < 3 && n < 2; ++slot) {
            if (slot != static_cast<int>(localSlot_)) owners[n++] = slot;
        }
        return owners;
    }

    // Poses to publish for the two puppets at the given server time.
    [[nodiscard]] std::array<PuppetTarget, 2> sample(
        std::uint64_t serverNowMs, std::uint16_t localWorld,
        std::uint16_t localRoom) const {
        std::array<PuppetTarget, 2> out {};
        if (!rosterValid_) return out;
        const auto owners = puppetOwners();
        const std::uint64_t renderMs =
            serverNowMs > config_.renderDelayMs ? serverNowMs - config_.renderDelayMs : 0;
        for (int i = 0; i < 2; ++i) {
            if (admittedIds_[owners[i]] == 0 || admittedIds_[owners[i]] != roster_[owners[i]]) continue;
            const auto& buf = interp_[owners[i]];
            const AvatarState* newest = buf.latest();
            if (!newest) continue;
            if (serverNowMs > newest->serverTimeMs + config_.staleAfterMs) continue;
            const auto pose = buf.sample(renderMs);
            if (!pose) continue;
            if (pose->worldId != localWorld || pose->roomId != localRoom) continue;
            if (pose->flags & AvatarInCutscene) continue;
            out[i].active = true;
            out[i].ownerConnectionId = admittedIds_[owners[i]];
            out[i].pose = *pose;
        }
        return out;
    }

    // On a local room change old snapshots describe the previous room.
    void clear() {
        for (auto& b : interp_) b.clear();
        admittedIds_ = {};
    }

private:
    SlotType localSlot_;
    Config config_;
    std::array<std::uint64_t, 3> roster_ {};
    std::array<std::uint64_t, 3> admittedIds_ {};
    bool rosterValid_ {false};
    std::array<AvatarInterpolator, 3> interp_;
};

} // namespace kh2coop
