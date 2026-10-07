#pragma once
// VUH-1515: host enemies can target remote players' native clones (KH2COOP_ENEMY_TARGET_REMOTE).
// Pure rules only: no game memory, no Windows calls. The native adapter is
// EnemyTargetRemote.inl (included inside EntityHook's namespace). See docs/ENEMY_TARGET_REMOTE.md.
#include <cmath>
#include <cstdint>
#include <cstring>

namespace kh2coop::inject::enemytarget::rules {

// ---- Families -------------------------------------------------------------------------------
// An object id is allowlisted only after its own bdscript was read: the attack decision must be
// "target_search (bank1/121) then distance to +0xBF8" with no other player read on that path.
// Shadow (302, M_EX020): mode_battle PC 2275/2290/2078, verified statically and live (VUH-1515
// attempt07, run 20261007-033207). Every other object id keeps the native behaviour.
inline constexpr std::uint32_t kAllowedObjectIds[] = {302};
inline bool AllowedObject(std::uint32_t objectId) noexcept {
    for (const auto id : kAllowedObjectIds) if (id == objectId) return true;
    return false;
}

// ---- bdscript "target_search" (bank1/121) ---------------------------------------------------
// 0x4303A0, all 75 bytes: resolves its script object and TARGET* (0x4AD270) and tail-jumps into
// the selector 0x3BE2B0(TARGET*, mode, self). Swapped only after this exact match.
inline constexpr std::uint8_t kTargetSearch[75] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF1,
    0x8B, 0x49, 0x08, 0xE8, 0xB6, 0xCE, 0x07, 0x00, 0x8B, 0x48, 0x04, 0xE8, 0xAE, 0xCE, 0x07, 0x00, 0x8B, 0x0E,
    0x48, 0x8B, 0xD8, 0x8B, 0x7E, 0x10, 0xE8, 0xA1, 0xCE, 0x07, 0x00, 0x4C, 0x8B, 0xC3, 0x8B, 0xD7, 0x48, 0x8B,
    0xC8, 0x48, 0x8B, 0x5C, 0x24, 0x30, 0x48, 0x8B, 0x74, 0x24, 0x38, 0x48, 0x83, 0xC4, 0x20, 0x5F, 0xE9, 0xC5,
    0xDE, 0xF8, 0xFF};
inline bool TargetSearchShape(const std::uint8_t (&b)[75]) noexcept {
    return std::memcmp(b, kTargetSearch, sizeof(kTargetSearch)) == 0;
}

// The TARGET struct the selector fills at enemy+0xBF8.
struct Slot { std::uint32_t handle = 0, part = 0, aux = 0, mode = 0; };
// Only the native player form is ever replaced: mode 0 (empty attention list -> player) or
// mode 2 (player), aux 0, naming the canonical player. The replacement keeps aux and mode and
// writes {clone handle, part 0}; no TARGET consumer reads +0xC except 0x4319D0's mode != 3 test.
inline bool PlayerForm(const Slot& s, bool namesCanonical) noexcept {
    return namesCanonical && (s.mode == 0 || s.mode == 2) && s.aux == 0;
}

// ---- Policy ---------------------------------------------------------------------------------
struct Policy {
    static constexpr float Hysteresis = 150.0f;        // a new target must be this much nearer
    static constexpr std::uint32_t MinHoldFrames = 180; // ~3 s at 60 fps before a switch
    static constexpr std::uint32_t CloneCap = 3;        // enemies assigned to one remote clone at once
    static constexpr std::uint32_t TableCapacity = 64;  // tracked enemies; full -> native
    static constexpr std::uint64_t AuthorityPeriodMs = 500;       // host re-advertises at least every 500 ms (S9: time, not frames)
    static constexpr std::uint64_t AuthorityMaxAgeMs = 1500;      // owner: older advertisement = no cancel
    static constexpr std::uint32_t VetoRiseFrames = 2;            // owner: our cancel's i-frames must rise this soon
};

// ---- Owner: the i-frame episode our own cancel started (review N2) --------------------------
// Armed only by a cancel that found +0xD70 at 0. It becomes active when +0xD70 rises within
// VetoRiseFrames, and ends when the timer falls back to 0 or is raised again by anyone (a guard,
// revive grace or another hit writes a larger value).
struct VetoEpisode { bool armed = false, active = false; std::uint32_t armFrame = 0; float last = 0; };
inline void EpisodeOnVeto(VetoEpisode& e, float timerBefore, std::uint32_t frame) noexcept {
    if (timerBefore <= 0.0f) { e.armed = true; e.armFrame = frame; }
}
inline void EpisodeTick(VetoEpisode& e, float timer, std::uint32_t frame) noexcept {
    if (e.active) {
        if (timer <= 0.0f || timer > e.last + 0.5f) e.active = false;
        else e.last = timer;
    }
    if (e.armed) {
        if (timer > 0.0f && frame - e.armFrame <= Policy::VetoRiseFrames) { e.active = true; e.last = timer; e.armed = false; }
        else if (frame - e.armFrame > Policy::VetoRiseFrames) e.armed = false;
    }
}
inline bool EpisodeOverrides(const VetoEpisode& e, bool reviveGrace) noexcept { return e.active && !reviveGrace; }

// S10: DownedSpike's GraceActive() stays true after the first revive whenever +0xD70 > 0
// (graceActor is cleared only on an actor/state change), and its invulnTimer is a snapshot Tick takes
// BEFORE the revive runs in that frame, so neither can mark a grace episode. Latch on facts that are
// synchronous with the revive: reviveCount changed, graceActor set, and the LIVE +0xD70 > 0. Clear
// when the live timer reaches 0. A failed revive with a stale graceActor latches spuriously, which
// fails safe (Invulnerable refusals until 0). (Main's graceHpDrops has the stale latch; separate issue.)
inline bool GraceLatch(bool latched, bool reviveEvent, bool graceActorSet, float liveTimer) noexcept {
    if (liveTimer <= 0.0f) return false;
    return latched || (reviveEvent && graceActorSet);
}

// S9: the owner cancels and applies on one clock. Both need continuous coverage of at least
// AuthorityMaxAgeMs, so a warm-up (or any lapse) gives native local damage, never immunity.
inline bool CoverageReady(std::uint64_t coveredMs) noexcept { return coveredMs >= Policy::AuthorityMaxAgeMs; }

// ---- Session mode and the TargetAuthority advertisement (review B2/B3) ----------------------
// Exactly one damage path per mode. Forward (mirror off): the host forwards RemoteHit and the owner
// cancels its local copy of the family's hits. Mirror (KH2COOP_ENEMY_MIRROR on the host): this
// feature only chooses targets and keeps the clone's team; mirrored hits stay native on the owner.
enum class Mode : std::uint8_t { Forward = 0, Mirror = 1 };
inline constexpr std::uint32_t kFamilyShadow = 1;  // familyMask bit 0 = objectId 302
inline std::uint32_t FamilyBit(std::uint32_t objectId) noexcept { return objectId == 302 ? kFamilyShadow : 0; }
// Host: bit (owner slot) for each valid candidate clone, only while active and swapped.
inline std::uint8_t SlotMask(bool active, bool swapped, const bool (&valid)[3], const std::uint8_t (&owner)[3]) noexcept {
    std::uint8_t m = 0;
    if (!active || !swapped) return 0;
    for (unsigned k = 1; k < 3; ++k)
        if (valid[k] && (owner[k] == 1 || owner[k] == 2)) m = static_cast<std::uint8_t>(m | (1u << owner[k]));
    return m;
}
// Owner: cancel a local family hit only with a held forward-mode advertisement naming our slot.
inline bool OwnerCancels(bool held, std::uint8_t mode, std::uint8_t slotMask, std::uint32_t familyMask,
                         std::uint8_t localSlot, std::uint32_t objectId) noexcept {
    return held && mode == static_cast<std::uint8_t>(Mode::Forward) && (localSlot == 1 || localSlot == 2) &&
           (slotMask & (1u << localSlot)) != 0 && (familyMask & FamilyBit(objectId)) != 0;
}

// Candidate 0 is the local player; 1 and 2 are remote clones (puppet index + 1).
inline constexpr unsigned kCandidates = 3;
struct Candidate { bool valid = false; float x = 0, z = 0; };

struct Assignment {
    std::uint64_t enemy = 0, objentry = 0;
    std::uint8_t target = 0;          // candidate index
    std::uint32_t since = 0;          // frame of the last switch
    bool used = false;
};

// Horizontal distance, y ignored, as the Shadow script measures it (PC 2290).
inline float HDist(float ax, float az, float bx, float bz) noexcept {
    const float dx = ax - bx, dz = az - bz;
    return std::sqrt(dx * dx + dz * dz);
}

// Deterministic choice for one enemy at (ex, ez). `current` is its assignment (nullptr = none),
// `load[k]` the number of OTHER enemies currently assigned to candidate k. Returns the
// candidate index, or -1 when no candidate is valid (keep the native result).
inline int Choose(const Candidate (&c)[kCandidates], float ex, float ez, const Assignment* current,
                  const std::uint32_t (&load)[kCandidates], std::uint32_t now) noexcept {
    float d[kCandidates] {};
    for (unsigned k = 0; k < kCandidates; ++k)
        d[k] = c[k].valid ? HDist(ex, ez, c[k].x, c[k].z) : INFINITY;
    const bool haveCurrent = current && current->used && current->target < kCandidates && c[current->target].valid;
    if (haveCurrent && now - current->since < Policy::MinHoldFrames) return current->target;
    int best = -1;
    for (unsigned k = 0; k < kCandidates; ++k) {
        if (!c[k].valid || !std::isfinite(d[k])) continue;
        const bool room = k == 0 || load[k] < Policy::CloneCap || (haveCurrent && current->target == k);
        if (!room) continue;
        if (best < 0 || d[k] < d[best]) best = static_cast<int>(k);  // ties keep the lower index
    }
    if (best < 0) return haveCurrent ? current->target : -1;
    if (haveCurrent && best != current->target && !(d[best] < d[current->target] - Policy::Hysteresis))
        return current->target;
    return best;
}

// ---- Liveness -------------------------------------------------------------------------------
// Listed and not removed (0x3BA720: +0x120 & 0x10080000 == 0) and not dead (+0x9B8 bit 2).
inline bool ActorLive(std::uint32_t flags120, std::uint32_t flags9b8, std::int32_t hp) noexcept {
    return (flags120 & 0x10080000u) == 0 && (flags9b8 & 4u) == 0 && hp > 0;
}
// A remote clone is a candidate only with a fresh, same-room, alive, not-downed, not-in-cutscene
// stream from its owner, a distinct native status from the local player's, and a live actor.
struct CloneFacts {
    bool puppetActive = false, playerClass = false, distinctStatus = false, actorLive = false;
    bool poseFresh = false, sameRoom = false, downed = false, inCutscene = false;
    bool held = false; // VUH-1787: AvatarHeld, the owner's stream has stalled
    std::uint8_t ownerSlot = 0xFF;
};
inline bool CloneCandidate(const CloneFacts& f) noexcept {
    return f.puppetActive && f.playerClass && f.distinctStatus && f.actorLive && f.poseFresh &&
           !f.held && f.sameRoom && !f.downed && !f.inCutscene && (f.ownerSlot == 1 || f.ownerSlot == 2);
}

// ---- Owner side: a forwarded hit ------------------------------------------------------------
enum class ApplyRefusal : std::uint8_t { None, Amount, NotCanonical, Transition, Dead, Invulnerable, Event, Stale };
struct OwnerFacts {
    std::int32_t damage = 0, hp = 0;
    bool canonical = false, transition = false, inEvent = false, dead = false;
    float invulnFrames = 0.0f;
    bool invulnFromVeto = false;  // the current i-frame episode is the one our own cancel started (S3/N2)
    std::uint64_t coveredMs = 0;  // how long the owner has been cancelling continuously (S7)
};
inline ApplyRefusal OwnerApply(const OwnerFacts& f) noexcept {
    if (f.damage <= 0 || f.damage > 9999) return ApplyRefusal::Amount;
    // S7: a RemoteHit is the host's copy of a hit we cancelled locally. Apply it only after we have
    // been cancelling the family continuously for AuthorityMaxAgeMs; otherwise our local copy may
    // already have landed (for example a delayed burst after a downlink stall).
    if (f.coveredMs < Policy::AuthorityMaxAgeMs) return ApplyRefusal::Stale;
    if (!f.canonical) return ApplyRefusal::NotCanonical;
    if (f.transition) return ApplyRefusal::Transition;
    if (f.inEvent) return ApplyRefusal::Event;
    if (f.dead || f.hp <= 0) return ApplyRefusal::Dead;
    if (f.invulnFrames > 0.0f && !f.invulnFromVeto) return ApplyRefusal::Invulnerable;
    return ApplyRefusal::None;
}

} // namespace kh2coop::inject::enemytarget::rules
