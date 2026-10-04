#pragma once

#include "kh2coop/Protocol.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace kh2coop {

inline bool sameActivationLocation(const RoomTransition& a, const RoomTransition& b) {
    return a.epoch == b.epoch && a.worldId == b.worldId && a.roomId == b.roomId &&
           a.door == b.door && a.mapProgram == b.mapProgram &&
           a.battleProgram == b.battleProgram && a.eventProgram == b.eventProgram;
}

inline bool sameActivationRequest(const ActivationRequest& a, const ActivationRequest& b) {
    return sameActivationLocation(a.location, b.location) && a.incarnation == b.incarnation &&
           a.requestSeq == b.requestSeq && a.requesterSlot == b.requesterSlot;
}

// Native request time, rather than receipt time, bounds every queue and the
// host capture. All callers use the same client-local monotonic clock.
class ActivationLease {
public:
    static constexpr std::uint64_t kLeaseMs = ACTIVATION_LEASE_MS;
    static constexpr std::uint64_t kRequestMs = ACTIVATION_REQUEST_INTERVAL_MS;
    static constexpr std::size_t kMaxOutstanding = ACTIVATION_MAX_OUTSTANDING;

    void Clear() {
        requests_ = {};
        point_.reset();
        sourceSeq_ = 0;
        lastRequestMs_.reset();
        // Retain requestSeq across session/epoch resets: delayed old replies
        // must never collide when an epoch is reused by a new session.
    }

    std::optional<ActivationRequest> Request(const RoomTransition& location,
                                             std::array<std::uint64_t, 2> incarnation,
                                             std::uint8_t slot, std::uint64_t now) {
        if (location.epoch == 0 || (incarnation[0] == 0 && incarnation[1] == 0) ||
            slot < 1 || slot > 2 || now > std::numeric_limits<std::uint64_t>::max() - kLeaseMs ||
            requestSeq_ == std::numeric_limits<std::uint64_t>::max()) return std::nullopt;
        if (lastRequestMs_ && (now < *lastRequestMs_ || now - *lastRequestMs_ < kRequestMs))
            return std::nullopt;
        for (auto& request : requests_) {
            if (request && now >= request->expires) request.reset();
        }
        auto free = std::find_if(requests_.begin(), requests_.end(),
                                 [](const auto& request) { return !request; });
        if (free == requests_.end()) return std::nullopt;
        ActivationRequest request;
        request.location = location;
        request.incarnation = incarnation;
        request.requestSeq = ++requestSeq_;
        request.requesterSlot = slot;
        *free = Pending {request, now, now + kLeaseMs};
        lastRequestMs_ = now;
        return request;
    }

    bool Accept(const HostActivationPoint& response, std::uint64_t now) {
        if (response.sourceSeq == 0 || response.sourceSeq <= sourceSeq_ ||
            !std::all_of(response.position.begin(), response.position.end(),
                         [](float value) { return std::isfinite(value); })) return false;
        for (auto& pending : requests_) {
            if (!pending || !sameActivationRequest(pending->request, response.request)) continue;
            if (now < pending->issued || now >= pending->expires) {
                pending.reset();
                return false;
            }
            point_ = Point {response.position, pending->issued, pending->expires};
            sourceSeq_ = response.sourceSeq;
            pending.reset();
            return true;
        }
        return false;
    }

    bool Copy(std::uint64_t now, float* destination) const {
        if (!destination || !point_ || now < point_->issued || now >= point_->expires) return false;
        std::copy(point_->position.begin(), point_->position.end(), destination);
        return true;
    }

    [[nodiscard]] std::uint64_t SourceSeq() const { return sourceSeq_; }

private:
    struct Pending {
        ActivationRequest request;
        std::uint64_t issued = 0, expires = 0;
    };
    struct Point {
        std::array<float, 4> position;
        std::uint64_t issued = 0, expires = 0;
    };
    std::array<std::optional<Pending>, kMaxOutstanding> requests_ {};
    std::optional<Point> point_;
    std::optional<std::uint64_t> lastRequestMs_;
    std::uint64_t requestSeq_ = 0, sourceSeq_ = 0;
};

} // namespace kh2coop
