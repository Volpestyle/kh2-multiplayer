#pragma once
// ============================================================================
// AvatarInterpolator — turns one remote avatar's snapshot stream into a
// smooth pose for the puppet driver.
//
// Snapshots are keyed by serverTimeMs (the owner's estimate of server time at
// capture). The receiver samples at renderTimeMs = estimatedServerTime -
// interpolation delay, so it normally sits between two known snapshots. Past
// the newest snapshot it extrapolates along the last observed displacement
// for at most maxExtrapolateMs, then holds — never invents unbounded motion.
// Motion ids switch at the earlier snapshot's boundary; motion time is
// interpolated only while both sides play the same motion.
// ============================================================================

#include "kh2coop/Types.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <optional>

namespace kh2coop {

class AvatarInterpolator {
public:
    explicit AvatarInterpolator(std::size_t capacity = 64,
                                std::uint32_t maxExtrapolateMs = 100)
        : capacity_(capacity), maxExtrapolateMs_(maxExtrapolateMs) {}

    // Returns false for stale or duplicate snapshots (out of order, same seq).
    bool push(const AvatarState& s) {
        if (!buffer_.empty() && s.serverTimeMs <= buffer_.back().serverTimeMs) {
            return false;
        }
        buffer_.push_back(s);
        while (buffer_.size() > capacity_) buffer_.pop_front();
        return true;
    }

    void clear() { buffer_.clear(); }
    [[nodiscard]] bool empty() const { return buffer_.empty(); }
    [[nodiscard]] std::size_t size() const { return buffer_.size(); }
    [[nodiscard]] const AvatarState* latest() const {
        return buffer_.empty() ? nullptr : &buffer_.back();
    }

    [[nodiscard]] std::optional<AvatarState> sample(std::uint64_t renderTimeMs) const {
        if (buffer_.empty()) return std::nullopt;
        if (renderTimeMs <= buffer_.front().serverTimeMs) return buffer_.front();

        const AvatarState& newest = buffer_.back();
        if (renderTimeMs >= newest.serverTimeMs) {
            if (buffer_.size() < 2) return newest;
            const AvatarState& prev = buffer_[buffer_.size() - 2];
            const auto ahead = std::min<std::uint64_t>(
                renderTimeMs - newest.serverTimeMs, maxExtrapolateMs_);
            const float span =
                static_cast<float>(newest.serverTimeMs - prev.serverTimeMs);
            AvatarState out = newest;
            if (span > 0.0f) {
                const float k = static_cast<float>(ahead) / span;
                out.position = add(newest.position,
                                   scale(sub(newest.position, prev.position), k));
            }
            if (out.motionId == prev.motionId) {
                out.motionTime += (newest.motionTime - prev.motionTime) *
                                  (span > 0.0f ? static_cast<float>(ahead) / span : 0.0f);
            }
            return out;
        }

        // Find a <= t < b.
        auto it = std::upper_bound(
            buffer_.begin(), buffer_.end(), renderTimeMs,
            [](std::uint64_t t, const AvatarState& s) { return t < s.serverTimeMs; });
        const AvatarState& b = *it;
        const AvatarState& a = *(it - 1);
        const float alpha = static_cast<float>(renderTimeMs - a.serverTimeMs) /
                            static_cast<float>(b.serverTimeMs - a.serverTimeMs);

        AvatarState out = a; // discrete fields (flags, hp, room) from a
        out.serverTimeMs = renderTimeMs;
        out.position = lerp(a.position, b.position, alpha);
        out.velocity = lerp(a.velocity, b.velocity, alpha);
        out.rotationY = lerpAngle(a.rotationY, b.rotationY, alpha);
        if (a.motionId == b.motionId && b.motionTime >= a.motionTime) {
            out.motionTime = a.motionTime + (b.motionTime - a.motionTime) * alpha;
        }
        return out;
    }

    // Shortest-arc angle interpolation in radians.
    static float lerpAngle(float a, float b, float t) {
        constexpr float kPi = 3.14159265358979f;
        float d = std::fmod(b - a, 2.0f * kPi);
        if (d > kPi) d -= 2.0f * kPi;
        if (d < -kPi) d += 2.0f * kPi;
        return a + d * t;
    }

private:
    static Vec3 add(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
    static Vec3 sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
    static Vec3 scale(const Vec3& a, float k) { return {a.x * k, a.y * k, a.z * k}; }
    static Vec3 lerp(const Vec3& a, const Vec3& b, float t) {
        return add(a, scale(sub(b, a), t));
    }

    std::deque<AvatarState> buffer_;
    std::size_t capacity_;
    std::uint32_t maxExtrapolateMs_;
};

} // namespace kh2coop
