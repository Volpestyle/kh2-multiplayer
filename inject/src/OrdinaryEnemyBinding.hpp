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
// Script births have no indexed native controller/record. This full copied
// scope is supplied only by their owner adapter, not inferred from a position.
struct ScriptActorScope {
    std::uint32_t generation{}, epoch{}, load{}, transition{};
    std::uint64_t hostConnection{}, hostDelivery{}, writerDelivery{};
    std::array<std::uint16_t, 6> location{};
    bool operator==(const ScriptActorScope&) const = default;
};
struct Scope {
    std::uint32_t generation{}, epoch{}, frame{};
    std::uint32_t load{}; // VUH-2584: the load serial (0: unknown)
    ScriptActorScope scriptActor{}; // default leaves existing indexed callers unchanged
};
struct ScriptActorWitness {
    std::uint64_t occurrence{}, nativeReturnSerial{};
    int childId{}, parentId{}; // parent is immutable history, never a current native pointer
    Identity child{}; // ready roots of the exact actual factory return, not ConstructorPending
    ScriptActorScope scope{};
    bool operator==(const ScriptActorWitness&) const = default;
};
struct State {
    int highWater{};
    int bound{};
    Identity identity{};
    Scope scope{};
    bool living{};
    // Set only after a continuous authenticated host death is observed locally
    // dead. Retained on refusal so the same tracked body cannot resurrect.
    bool terminal{};
    bool removed{}; // authenticated non-death retirement; never reusable as a new ID
    // VUH-2584: the ID this body was bound to when the census was last held (gameplay gate closed).
    int held{};
    // VUH-2550: the current binding came from only-unmatched-of-its-kind (no point match ever).
    bool byKind{};
    ScriptActorWitness scriptActor{}; // retained after adoption, death and refusal
};
struct Local {
    Identity identity{};
    std::array<float, 3> point{}; // historical first observation, never spawn authority
    State prior{};
    bool available{};
    bool living{};
    bool certificateRequired{};
    int certifiedId{};
    int birthId{}; // one-use receipt + fresh native birth proof supplied by owner adapter
    bool unharmed{}; // native HP at its maximum in this sample
    ScriptActorWitness scriptActor{};
    bool scriptActorCurrent{}; // fresh owner-frame proof, never carried forward by State
};
struct Host {
    int id{};
    std::uint32_t objectId{};
    std::array<float, 3> point{};
    bool dead{};
    bool removed{};
    bool left{}; // host disappeared alive: finish the established body's leave before a refill
    bool claimed{}; // some local body of this instance has already carried this ID
};
enum class Reason { Unavailable, NoPoint, Older, UnestablishedDeath, Conflict, Live, BoundDeath, Retired, Relabel, UnseenDeath,
                    UniqueKind, ScriptDiscontinuity };
inline const char* Name(Reason r) noexcept {
    switch (r) {
    case Reason::Retired: return "authenticated-membership-retired";
    case Reason::Relabel: return "live-body-id-relabel-refused";
    case Reason::Unavailable: return "native-unavailable";
    case Reason::NoPoint: return "no-point-match";
    case Reason::Older: return "older-than-binding";
    case Reason::UnestablishedDeath: return "death-without-continuous-binding";
    case Reason::Conflict: return "nonunique-live-id";
    case Reason::Live: return "newest-live-point";
    case Reason::BoundDeath: return "established-death";
    case Reason::UnseenDeath: return "death-before-first-sighting";
    case Reason::UniqueKind: return "only-unmatched-of-its-kind";
    case Reason::ScriptDiscontinuity: return "script-birth-without-continuous-binding";
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
inline bool HasScriptActor(const ScriptActorWitness& w) noexcept {
    return w != ScriptActorWitness{};
}
inline bool ScriptActorCurrent(const Local& l, Scope now) noexcept {
    const auto& w = l.scriptActor;
    const auto& s = w.scope;
    return l.scriptActorCurrent && !l.certificateRequired && w.occurrence && w.nativeReturnSerial &&
        w.childId > 0 && w.childId <= 65535 && w.parentId > 0 && w.parentId <= 65535 &&
        w.childId != w.parentId && (!l.birthId || l.birthId == w.childId) &&
        w.child == l.identity && w.child.actor && w.child.objentry && w.child.status && w.child.objectId &&
        !w.child.controller && !w.child.record && now.frame &&
        s.generation && s.epoch && s.load && s.transition && s.hostConnection && s.hostDelivery && s.writerDelivery &&
        s.generation == now.generation && s.epoch == now.epoch && s == now.scriptActor &&
        s.location[0] < 256 && s.location[1] < 256 && s.location[2] < 256;
}
inline bool Continuous(const Local& l, Scope now) noexcept {
    const auto& p = l.prior;
    if (!(p.bound > 0 && (p.living || (p.terminal && !l.living)) && p.identity == l.identity &&
          p.scope.generation == now.generation && p.scope.epoch == now.epoch &&
          now.frame > p.scope.frame && now.frame-p.scope.frame == 1)) return false;
    if (HasScriptActor(l.scriptActor) || HasScriptActor(p.scriptActor))
        return ScriptActorCurrent(l,now) && p.scriptActor == l.scriptActor &&
               p.bound == l.scriptActor.childId && p.scope.scriptActor == now.scriptActor;
    // Existing indexed roots are still required; sampled equal roots do not
    // manufacture an incarnation counter for a body with no typed receipt.
    return l.identity.controller && l.identity.record;
}

// VUH-2584: a held census (the gameplay gate closed for an event, such as a mission's closing event)
// skips frames, so a death announced during it fails Continuous and was never applied here. The body
// that was bound and living when the census was held, in the same load, with every sampled root
// unchanged, is still that body; only its own held ID's death may apply to it.
inline bool HeldContinuous(const Local& l, Scope now) noexcept {
    const auto& p = l.prior;
    return !p.bound && p.held > 0 && p.living && p.identity == l.identity &&
           l.identity.controller && l.identity.record &&
           p.scope.generation == now.generation && p.scope.epoch == now.epoch &&
           now.load && p.scope.load == now.load && now.frame > p.scope.frame;
}

// VUH-2550: a body the game placed differently on each seat (the Back Alley ambush's late 309) never
// matches a host point. It is still that enemy when it is the only fresh, never-bound, unharmed living
// body of its kind here without a point match, and the host has exactly one living, never-claimed entry
// of that kind without a local point match. Point matches always win; anything ambiguous stays unbound.
inline bool FreshBody(const Local& l) noexcept {
    return l.living && l.unharmed && l.available && !l.birthId && !l.certificateRequired &&
           l.identity.controller && l.identity.record &&
           !l.prior.bound && !l.prior.highWater && !l.prior.terminal && !l.prior.removed;
}
inline int UniqueKindId(std::span<const Local> locals, std::span<const Host> hosts, std::size_t i) noexcept {
    const auto& l = locals[i];
    if (!FreshBody(l)) return 0;
    const auto pointMatched = [&](const Local& a) {
        for (const auto& h : hosts)
            if (!h.removed && h.id > 0 && h.objectId == a.identity.objectId && SamePoint(h.point, a.point)) return true;
        return false;
    };
    if (pointMatched(l)) return 0;
    for (std::size_t j = 0; j < locals.size(); ++j) {
        if (j == i || locals[j].identity.objectId != l.identity.objectId || !locals[j].living) continue;
        if (!pointMatched(locals[j])) return 0;  // another unmatched living body of this kind: ambiguous
    }
    int id = 0;
    for (const auto& h : hosts) {
        if (h.removed || h.dead || h.left || h.claimed || h.id <= 0 || h.objectId != l.identity.objectId) continue;
        bool matched = false;
        for (const auto& o : locals)
            if (o.identity.objectId == h.objectId && SamePoint(h.point, o.point)) { matched = true; break; }
        if (matched) continue;
        if (id) return 0;  // two unmatched host entries of this kind: ambiguous
        id = h.id;
    }
    return id;
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
        d.next.held = 0;
        d.next.identity = l.identity;
        d.next.scope = now;
        d.next.living = l.living;
        if (!now.generation || !now.epoch || !l.identity.actor ||
            !l.identity.objentry || !l.identity.status || !Finite(l.point)) continue;
        if(l.prior.removed){d.reason=Reason::Retired;continue;}
        const bool script = HasScriptActor(l.scriptActor) || HasScriptActor(l.prior.scriptActor);
        const bool establishedScript = script && (HasScriptActor(l.prior.scriptActor) || l.prior.bound ||
                                                  l.prior.highWater || l.prior.terminal);
        if (script) {
            // Invalid/partial typed proof never falls back to points or indexed births.
            if (!ScriptActorCurrent(l,now) || (!establishedScript &&
                (l.birthId != l.scriptActor.childId || !l.living))) continue;
        } else if (!l.identity.controller || !l.identity.record) continue;
        if(l.birthId>0 && l.certificateRequired)continue;
        const Host* best = nullptr;
        for (const auto& h : hosts) {
            if (!h.removed && h.id > 0 && h.objectId == l.identity.objectId &&
                (script ? h.id==l.scriptActor.childId : l.birthId>0 ? h.id==l.birthId :
                 l.certificateRequired ? h.id==l.certifiedId : SamePoint(h.point,l.point)) &&
                (!best || h.id > best->id)) best = &h;
        }
        // VUH-2292: a reliable leave and its refill manifest can drain in one
        // owner frame. Keep the continuously established OLD identity for
        // native removal; never relabel its living body to the new occurrence.
        if (!script && !l.birthId && !l.certificateRequired && Continuous(l,now)) {
            for (const auto& h : hosts) {
                if (h.id == l.prior.bound && h.dead && h.left && !h.removed &&
                    h.objectId == l.identity.objectId && SamePoint(h.point,l.point)) {
                    best = &h;
                    break;
                }
            }
        }
        // VUH-2550: a body bound without a point match (only-unmatched-of-its-kind) keeps its host entry
        // while it stays continuously bound to it (or held through a closed gate) and no other local body
        // point-matches that entry.
        if (!best && l.prior.byKind && !l.birthId && !l.certificateRequired) {
            const int keep = l.prior.bound > 0 && Continuous(l,now) ? l.prior.bound
                           : l.prior.held > 0 && HeldContinuous(l,now) ? l.prior.held : 0;
            for (const auto& h : hosts) {
                if (!keep || h.id != keep || h.removed || h.objectId != l.identity.objectId) continue;
                bool other = false;
                for (std::size_t j = 0; j < locals.size(); ++j)
                    if (j != i && locals[j].identity.objectId == h.objectId && SamePoint(h.point, locals[j].point)) other = true;
                if (!other) best = &h;
                break;
            }
        }
        if (!best) {
            if (const int unique = UniqueKindId(locals, hosts, i)) {
                d.candidate = unique; d.id = unique; d.reason = Reason::UniqueKind;
            } else {
                d.reason = Reason::NoPoint;
            }
            continue;
        }
        d.candidate = best->id;
        if(l.prior.highWater>0 && best->id!=l.prior.highWater){d.reason=Reason::Relabel;continue;}
        // A newly minted local return cannot steal another body's historical ID.
        // An established child naturally marks its own ID claimed and continues.
        if (script && !establishedScript && best->claimed) { d.reason=Reason::Relabel;continue; }
        if (establishedScript && !Continuous(l,now)) { d.reason=Reason::ScriptDiscontinuity;continue; }
        if (!l.available) continue; // retain the candidate for whole-census conflict detection
        if (best->id < l.prior.highWater) { d.reason = Reason::Older; continue; }
        // A terminal body never becomes a new live candidate. Dead continuation
        // still requires the original ID, every sampled root and every frame.
        if (l.prior.terminal && (l.living || !best->dead)) {
            d.reason = Reason::UnestablishedDeath;
            continue;
        }
        const bool exactBirth=l.birthId==best->id && !l.prior.bound && !l.prior.highWater && !l.prior.terminal &&
            !l.certificateRequired && (script ? l.living && ScriptActorCurrent(l,now) :
                                      l.identity.controller && l.identity.record);
        // VUH-2497: the host can kill a wave while this game's census is held (the friend is still in
        // the room's entry event). The body's first sample then already names a dead ID. It is the
        // occurrence that ID was announced for only when nothing here ever saw it before: never
        // sampled, unharmed, no ticket, and no earlier local body of this instance carried the ID (a
        // local refill at the same point after a bound death stays refused until its own ID comes).
        const bool unseen=best->dead && !best->left && !best->claimed && l.living && l.unharmed && !l.birthId &&
            !l.certificateRequired && l.identity.controller && l.identity.record &&
            !l.prior.bound && !l.prior.highWater && !l.prior.terminal && !l.prior.scope.generation;
        const bool held = l.prior.held == best->id && HeldContinuous(l,now);
        if (best->dead && !exactBirth && !unseen && !held && (l.prior.bound != best->id || !Continuous(l,now))) {
            d.reason = Reason::UnestablishedDeath;
            continue;
        }
        d.id = best->id;
        d.reason = !best->dead ? Reason::Live : unseen && !exactBirth ? Reason::UnseenDeath : Reason::BoundDeath;
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
    for (std::size_t i=0; i<out.size(); ++i) {
        auto& d = out[i];
        const auto& p = locals[i].prior;
        d.next.byKind = d.id > 0 && (d.reason == Reason::UniqueKind ||
                                     (p.byKind && (d.id == p.bound || d.id == p.held)));
        if (d.id > 0 && d.reason == Reason::BoundDeath && !d.next.living)
            d.next.terminal = true;
        d.next.bound = d.id;
        d.next.highWater = std::max(d.next.highWater,d.id);
        if (d.id > 0 && HasScriptActor(locals[i].scriptActor)) d.next.scriptActor=locals[i].scriptActor;
    }
    return out;
}
} // namespace kh2coop::inject::ordinarybinding
