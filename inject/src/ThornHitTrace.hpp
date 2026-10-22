#pragma once
// VUH-2617 (a) diagnostic, KH2COOP_THORN_HIT_TRACE=1 only (default off, reads only). Pure rules for which
// actor's fields may be read, so no cached pointer is ever followed (VUH-2612: a guarded read of a freed
// clone can still end the process). An actor is read only inside its own native update, where the engine
// is updating it now, and only after its identity is proven there:
//   Local   the updating actor is the active-list head and the canonical player;
//   Clone   it is the driver's applied clone, one of the current clones, and its metadata (objentry,
//           status, object id, type, name) equals the snapshot taken when the driver bound it, so a freed
//           clone's reused address is refused (its metadata is read, as the updating actor, to compare);
//   Boss    the mirror bound it as the host-brain-only boss copy in this same update.
// Anything else (no candidate, a retired boss, a released driver) is not read at all. The frame-start line
// only prints snapshots captured this way, marked unavailable when not captured in the previous frame.
#include <cstdint>

namespace kh2coop::inject::thornhittrace {

enum class Role : std::uint8_t { None, Local, Clone0, Clone1, Boss };

struct Identity {  // what the driver recorded at binding (ActorSnapshot fields)
    bool known = false;
    std::uintptr_t objentry = 0, status = 0;
    std::uint32_t objectId = 0;
    std::uint8_t type = 0;
    std::uint16_t namePrefix = 0;
};
struct Candidates {
    std::uintptr_t head = 0, canonical = 0;      // this frame's list head; the canonical player global
    std::uintptr_t clone[2] {};                  // the drivers' cached clones (compared, never read)
    bool cloneApplied[2] {};                     // driver applied and its bound snapshot names that clone
    bool cloneCurrent[2] {};                     // the cached clone is one of the current clones (g_clones)
    Identity cloneBound[2] {};
    bool bossBoundNow = false;                   // the mirror bound the updating actor as the boss copy now
};
// Who the updating actor is, from pointer compares and the mirror's own binding only (no reads).
constexpr Role Admit(std::uintptr_t updating, const Candidates& c) noexcept {
    if (!updating) return Role::None;
    if (c.bossBoundNow) return Role::Boss;
    if (updating == c.head && updating == c.canonical) return Role::Local;
    for (unsigned i = 0; i < 2; ++i)
        if (updating == c.clone[i] && c.cloneApplied[i] && c.cloneCurrent[i] && c.cloneBound[i].known)
            return i == 0 ? Role::Clone0 : Role::Clone1;
    return Role::None;
}

// Field offsets, all supplied by the caller from KH2Offsets; read through the caller's reader only.
struct Layout {
    std::uintptr_t objentry, status;          // actor fields
    std::uintptr_t objectId, type, name;      // objentry fields
    std::uintptr_t pos, motion, motionTime;   // actor fields (pos: x, then y and z at +4 and +8)
    std::uintptr_t mask;                      // +0, +4 enable, +8 disable: actor+0xD10 (FUN_1403d35f0)
};

struct Fields {
    float x = 0, y = 0, z = 0, time = -1.0f;
    std::uint32_t motion = UINT32_MAX, mask = 0;
};

// The clone's metadata, read as the updating actor, against its binding snapshot.
template <class Read>
bool SameIdentity(std::uintptr_t actor, const Identity& bound, const Layout& l, Read&& read) {
    std::uintptr_t objentry = 0, status = 0;
    std::uint32_t objectId = 0;
    std::uint8_t type = 0;
    std::uint16_t name = 0;
    return read(actor + l.objentry, &objentry, sizeof(objentry)) && objentry == bound.objentry &&
           read(actor + l.status, &status, sizeof(status)) && status == bound.status &&
           read(objentry + l.objectId, &objectId, sizeof(objectId)) && objectId == bound.objectId &&
           read(objentry + l.type, &type, sizeof(type)) && type == bound.type &&
           read(objentry + l.name, &name, sizeof(name)) && name == bound.namePrefix;
}

// Admits the updating actor, then reads its fields (the boss also its motion time). None: nothing read
// beyond a clone's identity check.
template <class Read>
Role Capture(std::uintptr_t updating, const Candidates& c, const Layout& l, Read&& read, Fields& out) {
    const Role role = Admit(updating, c);
    if (role == Role::None) return Role::None;
    if ((role == Role::Clone0 || role == Role::Clone1) &&
        !SameIdentity(updating, c.cloneBound[role == Role::Clone0 ? 0 : 1], l, read))
        return Role::None;
    Fields f {};
    std::uint32_t d10 = 0, d14 = 0, d18 = 0;
    if (!read(updating + l.pos, &f.x, sizeof(f.x)) || !read(updating + l.pos + 4, &f.y, sizeof(f.y)) ||
        !read(updating + l.pos + 8, &f.z, sizeof(f.z)) || !read(updating + l.mask, &d10, sizeof(d10)) ||
        !read(updating + l.mask + 4, &d14, sizeof(d14)) || !read(updating + l.mask + 8, &d18, sizeof(d18)))
        return Role::None;
    f.mask = (d10 | d14) & ~d18;
    (void)read(updating + l.motion, &f.motion, sizeof(f.motion));
    if (role == Role::Boss) (void)read(updating + l.motionTime, &f.time, sizeof(f.time));
    out = f;
    return role;
}

} // namespace kh2coop::inject::thornhittrace
