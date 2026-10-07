#pragma once
// ============================================================================
// EnemyPopulation — VUH-1788: the client's enemy population follows the host's
// (KH2COOP_ENEMY_POPULATION=1, requires KH2COOP_ENEMY_MIRROR=1; default off).
//
// Pure planning state, no game memory (EnemySync.cpp owns the native calls):
//   * Missing host enemies. A host enemy (manifest netId) that is alive, of an
//     allowlisted family whose object id this client's own game has already
//     spawned in this instance (resources loaded), has a fresh EnemyMotion
//     stream and no local binding for kMissingFrames is force-spawned at its
//     host spawn point (native generic factory 0x3DF930), so the ordinary binding
//     (objectId + spawn point) binds it and the mirror drives it. One spawn per
//     kSpawnGap frames, at most kMaxForced per epoch, at most kMaxAttempts per
//     netId, a retry only after the earlier forced actor is gone + kRetryFrames.
//   * Forced copies are identified by actor + objentry + status (an address alone
//     can be reused). They are held from creation while their host enemy lives,
//     and removed natively (removal predicate forced, script term only) once the
//     host enemy is dead or unknown, the epoch moved, or a native local duplicate
//     claims the same netId. Entries survive an epoch change or session retire
//     inside the same loaded instance (so they still get removed) and are cleared
//     only by this client's own transition or load (the native teardown).
// Types.hpp only (safe beside HitChannel.hpp in EntityHook).
// ============================================================================

#include "kh2coop/Types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace kh2coop::inject::enemypop {

// rev3: 600, not 180. Live-fixture-01 (063223): the friend's own game spawned the later wave ~205
// frames after the host announced it, and the earlier forced attempt raced it and was refused.
inline constexpr std::uint32_t kMissingFrames = 600;   // host-only this long before forcing
inline constexpr std::uint32_t kRetryFrames = 900;     // after a forced actor is gone
inline constexpr std::uint32_t kSpawnGap = 30;         // frames between forced spawns
inline constexpr std::size_t kMaxForced = 8;           // per epoch
inline constexpr std::uint8_t kMaxAttempts = 3;        // per netId per epoch
inline constexpr std::uint32_t kCullHoldFrames = 1800; // a copy driven this recently is never culled
inline constexpr std::uint32_t kBindGap = 30;          // a bind lost for this long still counts (S2)
inline constexpr std::uint32_t kBindDeadline = 300;    // rev3 C2: a forced copy unbound this long after creation goes
inline constexpr std::size_t kTracked = 64;

struct HostView {
    std::uint16_t netId = 0;
    std::uint32_t objectId = 0;
    bool allowed = false;       // family allowlist
    bool loadedObject = false;  // this instance's census already showed a native actor of this object id (C5)
    bool dead = false;
    bool boundLocally = false;  // a present local spawn carries this netId
    bool streamFresh = false;   // EnemyMotion drivable for this netId
    bool forcedPresent = false; // our earlier forced actor for it is still in the census
};

struct Identity {
    std::uintptr_t actor = 0, objentry = 0, status = 0;
    bool operator==(const Identity&) const = default;
};
struct Forced {
    std::uint16_t netId = 0;
    Identity id {};
    std::uint32_t epoch = 0, frame = 0;
    bool yield = false;  // a native local duplicate claims the same netId (S1), or it never bound (rev3 C2)
    bool boundOnce = false;
    std::uint32_t unboundTicks = 0;  // rev4 R2: resolved-binding frames it spent unbound (not wall frames)
};

class Planner {
public:
    // A new host epoch or a retired session inside the same loaded instance:
    // planning restarts, forced entries stay (they are removed by ForceRemove).
    void Rebase(std::uint32_t epoch) noexcept {
        const auto keep = forced_;
        const auto generation = generation_ + 1;
        *this = Planner {};
        forced_ = keep;
        epoch_ = epoch;
        generation_ = generation;
    }
    // This client's own transition/load: the native teardown owns every actor.
    void Clear() noexcept {
        const auto generation = generation_ + 1;
        *this = Planner {};
        generation_ = generation;
    }
    // Bumped by Rebase/Clear: the cull hook's bind/drive history is dropped on change (rev3 S2).
    std::uint32_t generation() const noexcept { return generation_; }
    std::uint32_t epoch() const noexcept { return epoch_; }
    std::size_t forcedCount() const noexcept { return forcedCount_; }

    // Once per client frame with the whole host view. Returns the netId to force
    // now (0: none). `safe` = native gameplay safe, client live and arrived.
    template <class Span> std::uint16_t Plan(const Span& hosts, std::uint32_t frame, bool safe) noexcept {
        std::uint16_t pick = 0;
        for (const HostView& h : hosts) {
            Track* t = Find(h.netId, true);
            if (!t) continue;
            const bool missing = h.allowed && h.loadedObject && !h.dead && h.streamFresh && !h.boundLocally &&
                                 !h.forcedPresent;
            if (!missing) { t->missingSince = 0; continue; }
            if (!t->missingSince) t->missingSince = frame ? frame : 1;
            if (pick || !safe) continue;
            if (frame - t->missingSince < kMissingFrames) continue;
            if (t->attempts >= kMaxAttempts || forcedCount_ >= kMaxForced) continue;
            if (t->attempts && frame - t->lastAttempt < kRetryFrames) continue;
            if (lastSpawn_ && frame - lastSpawn_ < kSpawnGap) continue;
            pick = h.netId;
        }
        return pick;
    }
    // The native call was made for `netId` (actor 0: refused/failed).
    void Attempted(std::uint16_t netId, const Identity& id, std::uint32_t frame) noexcept {
        Track* t = Find(netId, true);
        if (!t) return;
        ++t->attempts;
        t->lastAttempt = frame ? frame : 1;
        t->missingSince = 0;
        lastSpawn_ = frame ? frame : 1;
        if (!id.actor) return;
        ++forcedCount_;
        for (auto& f : forced_) {
            if (!f.id.actor) { f = {netId, id, epoch_, frame, false, false, 0}; return; }
        }
    }
    // Identity match, never the address alone.
    const Forced* ForcedFor(const Identity& id) const noexcept {
        if (!id.actor) return nullptr;
        for (const auto& f : forced_) if (f.id == id) return &f;
        return nullptr;
    }
    // One resolved-binding frame in which `actor` was unbound; returns the running count (rev4 R2).
    std::uint32_t NoteUnbound(std::uintptr_t actor) noexcept {
        for (auto& f : forced_) if (f.id.actor == actor) return ++f.unboundTicks;
        return 0;
    }
    void MarkBound(std::uintptr_t actor) noexcept {
        for (auto& f : forced_) if (f.id.actor == actor) f.boundOnce = true;
    }
    void MarkYield(std::uintptr_t actor) noexcept {
        for (auto& f : forced_) if (f.id.actor == actor) f.yield = true;
    }
    // The forced actor left the census (removed natively or by us).
    void Forget(std::uintptr_t actor) noexcept {
        for (auto& f : forced_) if (f.id.actor == actor) f = {};
    }
    const std::array<Forced, kMaxForced>& forced() const noexcept { return forced_; }
    bool hasForced() const noexcept {
        for (const auto& f : forced_) if (f.id.actor) return true;
        return false;
    }

private:
    struct Track {
        std::uint16_t netId = 0;
        std::uint32_t missingSince = 0, lastAttempt = 0;
        std::uint8_t attempts = 0;
    };
    Track* Find(std::uint16_t netId, bool create) noexcept {
        if (!netId) return nullptr;
        for (auto& t : tracks_) if (t.netId == netId) return &t;
        if (!create) return nullptr;
        for (auto& t : tracks_) if (!t.netId) { t = {}; t.netId = netId; return &t; }
        return nullptr;
    }
    std::array<Track, kTracked> tracks_ {};
    std::array<Forced, kMaxForced> forced_ {};
    std::size_t forcedCount_ = 0;
    std::uint32_t epoch_ = 0, lastSpawn_ = 0, generation_ = 0;
};

// The factory's own admission 0x3A1F00: weight (objentry +0x54, u8) <= limit (0x2A0F7DC) - used (0x2A0F830),
// float32. A refused admission makes 0x3DF930 return null; never call it while this is false.
inline bool BudgetAllows(float limit, float used, std::uint8_t weight) noexcept {
    return static_cast<float>(weight) <= limit - used;
}

// A forced copy goes as soon as its host enemy is dead or unknown, the epoch
// moved (also: no client session at all), or a native duplicate claims it.
inline bool ForceRemove(const Forced& f, bool hostKnown, bool hostDead, std::uint32_t epoch) noexcept {
    return f.id.actor && (f.yield || !hostKnown || hostDead || f.epoch != epoch);
}
// A forced copy is held from creation while its host enemy lives (C2: through the settle).
inline bool ForcedHold(const Forced& f, bool hostKnown, bool hostDead, std::uint32_t epoch) noexcept {
    return f.id.actor && !ForceRemove(f, hostKnown, hostDead, epoch);
}

// The removal predicate's override for a mirrored copy (EnemyMirror.inl):
// -1 keep native, 0 hold (refuse removal), 1 force removal (the caller still
// requires the native +0x80/+0x98 and +0xBB4-child terms, C4).
inline int CullDecision(bool nativeRemove, bool forceRemove, bool recentlyBound, bool living, bool running,
                        std::uint32_t sinceDriven, bool forcedHold) noexcept {
    if (forceRemove) return 1;
    if (!nativeRemove) return -1;
    if (!living) return -1;
    if (forcedHold) return 0;
    if (recentlyBound && (running || sinceDriven <= kCullHoldFrames)) return 0;
    return -1;
}

}  // namespace kh2coop::inject::enemypop
