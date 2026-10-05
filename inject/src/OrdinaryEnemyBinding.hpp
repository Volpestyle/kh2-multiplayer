#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace kh2coop::inject::ordinarybinding {

// These are sampled native identities, NOT a native incarnation counter.
struct Identity {
    std::uintptr_t actor{}, objentry{}, status{}, controller{}, record{};
    std::uint32_t objectId{};
    bool operator==(const Identity&) const = default;
};
struct Scope {
    std::uint32_t generation{}, epoch{}, frame{};
};
struct State {
    int highWater{};
    int bound{};
    Identity identity{};
    Scope scope{};
    bool living{};
};
struct Local {
    Identity identity{};
    std::array<float, 3> point{}; // historical first observation, never spawn authority
    State prior{};
    bool available{};
    bool living{};
};
struct Host {
    int id{};
    std::uint32_t objectId{};
    std::array<float, 3> point{};
    bool dead{};
};
enum class Reason { Unavailable, NoPoint, Older, UnestablishedDeath, Conflict, Live, BoundDeath };
inline const char* Name(Reason r) noexcept {
    switch (r) {
    case Reason::Unavailable: return "native-unavailable";
    case Reason::NoPoint: return "no-point-match";
    case Reason::Older: return "older-than-binding";
    case Reason::UnestablishedDeath: return "death-without-continuous-binding";
    case Reason::Conflict: return "nonunique-live-id";
    case Reason::Live: return "newest-live-point";
    case Reason::BoundDeath: return "established-death";
    }
    return "unknown";
}
struct Decision {
    int id{}; // zero MUST be published as unmatched; never retain a stale netId
    int candidate{};
    Reason reason{Reason::Unavailable};
    bool conflict{};
    State next{};
};
inline bool Finite(const std::array<float, 3>& p) noexcept {
    return std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]);
}
inline bool SamePoint(const std::array<float, 3>& a, const std::array<float, 3>& b) noexcept {
    const float x = a[0]-b[0], y = a[1]-b[1], z = a[2]-b[2];
    return Finite(a) && Finite(b) && x*x+y*y+z*z <= 64.0f;
}
inline bool Continuous(const Local& l, Scope now) noexcept {
    const auto& p = l.prior;
    // Missing frames, changed sampled roots, and absent controller/record cannot
    // authorize a tombstone. Same-address reuse between samples is unresolved.
    return p.bound > 0 && p.living && p.identity == l.identity &&
           l.identity.controller && l.identity.record &&
           p.scope.generation == now.generation && p.scope.epoch == now.epoch &&
           now.frame > p.scope.frame && now.frame-p.scope.frame == 1;
}

// Whole-census, order-independent selection; no native writes or spawn creation.
// All claimants of a nonunique live ID are held, including an old incumbent.
inline std::vector<Decision> Resolve(std::span<const Local> locals,
                                     std::span<const Host> hosts, Scope now) {
    std::vector<Decision> out(locals.size());
    for (std::size_t i=0; i<locals.size(); ++i) {
        const auto& l = locals[i];
        auto& d = out[i];
        d.next = l.prior;
        d.next.bound = 0;
        d.next.identity = l.identity;
        d.next.scope = now;
        d.next.living = l.living;
        if (!now.generation || !now.epoch || !l.identity.actor ||
            !l.identity.objentry || !l.identity.status || !Finite(l.point)) continue;
        const Host* best = nullptr;
        for (const auto& h : hosts) {
            if (h.id > 0 && h.objectId == l.identity.objectId && SamePoint(h.point,l.point) &&
                (!best || h.id > best->id)) best = &h;
        }
        if (!best) { d.reason = Reason::NoPoint; continue; }
        d.candidate = best->id;
        if (!l.available) continue; // retain the candidate for whole-census conflict detection
        if (best->id < l.prior.highWater) { d.reason = Reason::Older; continue; }
        if (best->dead && (l.prior.bound != best->id || !Continuous(l,now))) {
            d.reason = Reason::UnestablishedDeath;
            continue;
        }
        d.id = best->id;
        d.reason = best->dead ? Reason::BoundDeath : Reason::Live;
    }
    // Compare candidates before committing ANY choice. Even a claimant held by
    // the death gate conflicts with another live body's proposed same host ID.
    for (std::size_t i=0; i<locals.size(); ++i) {
        auto& d = out[i];
        if (!locals[i].living || !d.candidate) continue;
        for (std::size_t j=0; j<locals.size(); ++j) {
            if (i != j && locals[j].living && out[j].candidate == d.candidate) {
                d.id = 0; d.conflict = true; d.reason = Reason::Conflict;
                break;
            }
        }
    }
    for (auto& d : out) {
        d.next.bound = d.id;
        d.next.highWater = std::max(d.next.highWater,d.id);
    }
    return out;
}
} // namespace kh2coop::inject::ordinarybinding
