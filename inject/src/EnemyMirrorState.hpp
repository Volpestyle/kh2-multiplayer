#pragma once
// ============================================================================
// EnemyMirrorState — VUH-1515 step 2, pure client-side stream state for
// mirrored enemies (no game memory, no network, no allocation per frame).
//
// The host sends EnemyMotion (Protocol.hpp) about every 3 frames for each
// bound, allowlisted, living enemy. A client keeps a small ring per netId and
// renders on a host-frame cursor about DELAY frames behind the newest sample,
// so a Shadow on the client plays the host's motion ~150 ms late but smooth.
// The cursor has a natural position (advanced one host frame per local frame,
// i.e. an estimate of the host clock minus DELAY) and a displayed position,
// held at newest-MIN_LAG while the stream stalls. The difference (overflow) is
// added to the motion time, so a stall holds the position but never freezes or
// rewinds the motion (review S1).
// A netId is drivable only while fresh; after a release it needs RETAKE new
// samples before it is driven again (hysteresis).
// ============================================================================

#include "EnemyMirrorPose.hpp"
#include "kh2coop/Protocol.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>

namespace kh2coop::inject::enemymirror {

class Stream {
public:
    explicit Stream(std::uint32_t delayFrames = kDelay) noexcept
        : delayFrames_(delayFrames >= latency::kEnemyDelayMinFrames && delayFrames <= latency::kEnemyDelayMaxFrames
                           ? delayFrames : kDelay) {}
    std::uint32_t delayFrames() const noexcept { return delayFrames_; }
    std::uint32_t newestFrame() const noexcept { return newest_; }
    std::uint32_t epoch() const noexcept { return epoch_; }
    const StreamStats& stats() const noexcept { return stats_; }
    double cursor() const noexcept { return haveCursor_ ? cursor_ : -1.0; }
    std::size_t tracks() const noexcept { return tracks_.size(); }

    void Reset(std::uint32_t epoch) noexcept {
        tracks_.clear();
        epoch_ = epoch;
        sequence_ = 0;
        newest_ = 0;
        haveNewest_ = haveCursor_ = false;
        cursor_ = natural_ = 0.0;
        ++stats_.resets;
    }

    // One decoded packet. A new epoch resets the stream first. Returns false
    // (and changes nothing) for a zero/old sequence or zero epoch.
    bool Ingest(const EnemyMotion& m, std::uint32_t localFrame) {
        if (!m.epoch || !m.sequence) { ++stats_.rejected; return false; }
        if (m.epoch != epoch_) Reset(m.epoch);
        if (m.sequence <= sequence_) { ++stats_.rejected; return false; }
        sequence_ = m.sequence;
        if (!haveNewest_ || static_cast<std::int32_t>(m.hostFrame - newest_) > 0) {
            newest_ = m.hostFrame;
            newestLocal_ = localFrame;
        }
        haveNewest_ = true;
        for (const auto& e : m.entries) {
            if (!e.netId || !FamilyAllowed(e.objectId)) continue;
            if (tracks_.size() >= kMaxTracks && tracks_.find(e.netId) == tracks_.end()) {  // rev-2 N-c
                ++stats_.trackCap;
                continue;
            }
            Track& t = tracks_[e.netId];
            if (t.count && static_cast<std::int32_t>(m.hostFrame - t.ring[(t.head + kRing - 1) % kRing].hostFrame) <= 0) {
                ++stats_.staleSamples;
                continue;
            }
            t.ring[t.head] = {m.hostFrame, e.objectId, e.motionId, e.motionTime, e.position, e.rotationY};
            t.head = (t.head + 1) % kRing;
            if (t.count < kRing) ++t.count;
            t.lastArrival = localFrame;
            t.haveArrival = true;
            if (t.released && ++t.sinceRelease >= kRetake) {
                t.released = false;
                ++stats_.retakes;
            }
        }
        ++stats_.accepted;
        return true;
    }

    // A dead host enemy's track goes away (review N6): releases then count
    // only stream loss, never deaths.
    void Erase(std::uint16_t netId) { tracks_.erase(netId); }

    // Once per local frame, after the frame's packets were ingested.
    void Tick(std::uint32_t localFrame) noexcept {
        if (haveNewest_) {
            const double newest = static_cast<double>(newest_);
            if (!haveCursor_) {
                natural_ = newest - delayFrames_;
                haveCursor_ = true;
            } else {
                const double lag = newest - natural_;
                // Gentle catch-up after a burst (+2). And, rev2 of the Hook Bat lane: while the stream is
                // live (newest advanced within a publish interval) but the natural cursor sits too close
                // to (or past) the newest sample, hold it a frame (+0). A HOST stall (its frames stop, e.g.
                // a capture hitch) otherwise leaves natural permanently ahead of the host clock: the
                // displayed cursor pins at newest-1 (never a trace frame, % 30), and the overflow pushes
                // the motion time ahead by the stall length for good (fixture 073546: every netId).
                const bool live = localFrame - newestLocal_ <= kPublishInterval + 1;
                if (lag > static_cast<double>(delayFrames_ + 3)) {
                    natural_ += 2.0;
                    ++stats_.cursorCatchups;
                } else if (live && lag < static_cast<double>(delayFrames_) - 3.0) {
                    ++stats_.cursorHolds;
                } else {
                    natural_ += 1.0;
                }
                if (natural_ < newest - kMaxLag) {
                    natural_ = newest - delayFrames_;
                    ++stats_.cursorSnaps;
                }
                if (natural_ > newest + kStaleFrames) natural_ = newest + kStaleFrames;  // bounded overflow
            }
            cursor_ = natural_ > newest - kMinLag ? newest - kMinLag : natural_;
            // A stream clock clamp, not an inferred packet-loss count.
            if (natural_ > newest - kMinLag) ++stats_.underrunFrames;
        }
        for (auto& [id, t] : tracks_) {
            (void)id;
            if (!t.released && (!t.haveArrival || localFrame - t.lastArrival > kStaleFrames)) {
                t.released = true;
                t.sinceRelease = 0;
                ++stats_.releases;
            }
        }
    }

    bool Drivable(std::uint16_t netId, std::uint32_t localFrame) const noexcept {
        const auto it = tracks_.find(netId);
        if (it == tracks_.end()) return false;
        const Track& t = it->second;
        return !t.released && t.haveArrival && t.count && haveCursor_ && localFrame - t.lastArrival <= kStaleFrames;
    }

    // Interpolated pose at the render cursor; false if not drivable.
    bool PoseAt(std::uint16_t netId, std::uint32_t localFrame, Pose& out) const noexcept {
        if (!Drivable(netId, localFrame)) return false;
        const Track& t = tracks_.find(netId)->second;
        // Oldest -> newest view of the ring.
        std::array<const Sample*, kRing> s {};
        for (std::size_t i = 0; i < t.count; ++i) s[i] = &t.ring[(t.head + kRing - t.count + i) % kRing];
        const double c = cursor_;
        const float overflow = static_cast<float>(natural_ - cursor_);  // > 0 only while the stream stalls
        const Sample* s0 = s[0];
        const Sample* s1 = nullptr;
        for (std::size_t i = 0; i < t.count; ++i) {
            if (static_cast<double>(s[i]->hostFrame) <= c) s0 = s[i];
            else { s1 = s[i]; break; }
        }
        if (s1 == s0) s1 = nullptr;
        if (static_cast<double>(s0->hostFrame) > c) s1 = nullptr;  // cursor before the oldest: hold the oldest
        out.netId = netId;
        out.objectId = s0->objectId;
        out.cursor = c;
        if (!s1) {
            out.position = s0->position;
            out.rotationY = s0->rotationY;
            out.motionId = s0->motionId;
            const float ahead = static_cast<float>(c - s0->hostFrame);
            out.motionTime = s0->motionTime + (ahead > 0.0f ? ahead : 0.0f) + overflow;
            return true;
        }
        const float span = static_cast<float>(s1->hostFrame - s0->hostFrame);
        const float u = span > 0.0f ? static_cast<float>(c - s0->hostFrame) / span : 0.0f;
        out.position = {s0->position.x + (s1->position.x - s0->position.x) * u,
                        s0->position.y + (s1->position.y - s0->position.y) * u,
                        s0->position.z + (s1->position.z - s0->position.z) * u};
        out.rotationY = LerpAngle(s0->rotationY, s1->rotationY, u);
        out.motionId = s0->motionId;
        if (s1->motionId == s0->motionId && s1->motionTime >= s0->motionTime)
            out.motionTime = s0->motionTime + (s1->motionTime - s0->motionTime) * u;
        else
            out.motionTime = s0->motionTime + static_cast<float>(c - s0->hostFrame);
        out.motionTime += overflow;
        return true;
    }

private:
    struct Track {
        std::array<Sample, kRing> ring {};
        std::size_t head = 0, count = 0;
        std::uint32_t lastArrival = 0, sinceRelease = 0;
        bool haveArrival = false;
        bool released = true;  // a new netId needs kRetake samples too
    };
    std::map<std::uint16_t, Track> tracks_;
    std::uint32_t epoch_ = 0;
    std::uint64_t sequence_ = 0;
    std::uint32_t newest_ = 0;
    bool haveNewest_ = false, haveCursor_ = false;
    double cursor_ = 0.0, natural_ = 0.0;  // integral host frames; double: exact for 2^53 frames (N7)
    std::uint32_t newestLocal_ = 0;  // local frame at which newest_ last advanced
    StreamStats stats_ {};
    std::uint32_t delayFrames_;
};

}  // namespace kh2coop::inject::enemymirror
