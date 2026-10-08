#pragma once
#include <array>
#include <cstdint>

namespace kh2coop::inject::spectate {
constexpr std::uint16_t CycleButton = 0x0004; // raw PS2 R3; consumed only while eligible
constexpr std::uint32_t MaxPoseFrames = 30;
struct RemoteFacts {
    bool active{}, sameRoom{}, finite{}, nativeBound{};
    std::uint32_t age{};
    std::uint8_t flags{};
    int hp{};
};
inline bool EligibleRemote(const RemoteFacts& f) noexcept {
    // Types.hpp: Downed=2, InCutscene=4, Held=8; Airborne=1 is allowed.
    return f.active && f.sameRoom && f.finite && f.nativeBound && f.age <= MaxPoseFrames &&
        !(f.flags & (2U | 4U | 8U)) && f.hp > 0;
}
struct Target {
    std::uint64_t actor{}, connection{}, objentry{}, status{};
    std::uint32_t handle{};
    bool valid{};
};
inline bool Same(const Target& a, const Target& b) noexcept {
    return a.actor == b.actor && a.connection == b.connection && a.objentry == b.objentry &&
        a.status == b.status && a.handle == b.handle;
}
struct State {
    Target target{};
    std::uint32_t generation{}, transition{}, load{};
    std::uint8_t slot{255};
    bool armed{}, buttonDown{}, pending{};
};
inline void Release(State& s) noexcept { s = {}; }
// A neutral input sample after entry is required; held R3 cannot cycle on entry.
inline void Input(State& s, bool eligible, bool down) noexcept {
    if (!eligible) { s.armed = s.buttonDown = s.pending = false; return; }
    if (!down) s.armed = true;
    if (down && !s.buttonDown && s.armed) s.pending = true;
    s.buttonDown = down;
}
struct Facts {
    bool eligible{};
    std::uint8_t localSlot{255};
    std::uint32_t generation{}, transition{}, load{};
    std::array<Target, 3> targets{}; // native local body at localSlot, living peers elsewhere
};
inline std::uint8_t Step(State& s, const Facts& f) noexcept {
    if (!f.eligible || f.localSlot >= 3 || !f.generation) { Release(s); return 255; }
    if (s.generation && (s.generation != f.generation || s.transition != f.transition || s.load != f.load)) {
        Release(s); return 255; // retry/load is a release boundary, never a retarget in this call
    }
    const bool keep = s.slot < 3 && f.targets[s.slot].valid && Same(s.target, f.targets[s.slot]);
    unsigned start = s.slot < 3 ? (s.slot + 1U) % 3U : (f.localSlot + 1U) % 3U;
    if (keep && !s.pending) start = s.slot;
    s.pending = false;
    for (unsigned i = 0; i < 3; ++i) {
        const unsigned slot = (start + i) % 3U;
        if (!f.targets[slot].valid) continue;
        s.slot = static_cast<std::uint8_t>(slot); s.target = f.targets[slot];
        s.generation = f.generation; s.transition = f.transition; s.load = f.load;
        return s.slot;
    }
    Release(s); return 255;
}
// Camera reads may finish after native lifecycle callbacks. Never restore a stale
// pointer, or overwrite a pointer that native code replaced during the call.
inline bool RestoreOwned(std::uint64_t current, std::uint64_t borrowed,
                         bool sameLifecycle, bool canonicalCurrent) noexcept {
    return current == borrowed && borrowed != 0 && sameLifecycle && canonicalCurrent;
}
} // namespace kh2coop::inject::spectate
