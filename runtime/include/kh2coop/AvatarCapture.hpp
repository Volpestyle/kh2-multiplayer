#pragma once
// ============================================================================
// AvatarCapture — builds the local player's AvatarState from game memory
// (VUH-1490). Header-only so the inject DLL (direct reads, every frame) and
// tests (fake memory) share one mapping of offsets to fields.
//
// Every offset used here is [CONFIRMED] in pointer_map_v1.md /
// HANDOFF_FRIEND_CONTROL.md. Things without a confirmed source are left to
// the caller: `inEvent` (cutscene/load — set AvatarInCutscene), `downed`.
// MP is not mapped yet and stays 0.
// ============================================================================

#include "kh2coop/KH2Offsets.hpp"
#include "kh2coop/Types.hpp"

#include <cstdint>

namespace kh2coop {

namespace capture {
    // The player's motion clock is inline in the actor (verified live,
    // VUH-1490): float frames since the motion started, +1 per frame, reset on
    // every motion change. The friend motCtrl (actor+0x158 -> +0x44) holds
    // fill bytes for Sora. Playback speed isn't mapped for the player; 1.0.
    constexpr std::uint64_t ACTOR_MOTION_TIME = 0x19C; // float
    // Physics inputs read by EntityPositionPhysics (pointer_map_v1.md).
    constexpr std::uint64_t ACTOR_VELOCITY  = 0xB98;  // 3 floats
} // namespace capture

// `Reader` must provide: template <class T> T read(std::uint64_t address) const.
// `exeBase` is the KH2 module base; `actor` is the local player's actor object
// (camStruct+0x50 or the entity list), or 0 when not in a room.
template <typename Reader>
AvatarState captureAvatar(const Reader& mem, std::uint64_t exeBase, std::uint64_t actor,
                          bool inEvent, bool downed) {
    namespace o = offsets;
    AvatarState a;
    a.worldId = mem.template read<std::uint8_t>(exeBase + o::WORLD_ID);
    a.roomId = mem.template read<std::uint8_t>(exeBase + o::ROOM_ID);
    a.hp = mem.template read<std::int32_t>(exeBase + o::slot0::HP);
    a.maxHp = mem.template read<std::int32_t>(exeBase + o::slot0::MAX_HP);
    if (inEvent) a.flags |= AvatarInCutscene;
    if (downed) a.flags |= AvatarDowned;
    if (actor == 0) {
        a.flags |= AvatarInCutscene; // nothing to show: hide the puppet
        return a;
    }

    const std::uint64_t entity = actor + o::actor::ENTITY_TRANSFORM;
    a.position = {mem.template read<float>(entity + o::entity::POS_X),
                  mem.template read<float>(entity + o::entity::POS_X + 4),
                  mem.template read<float>(entity + o::entity::POS_X + 8)};
    a.rotationY = mem.template read<float>(entity + o::entity::ROT_Y);
    if (mem.template read<std::uint32_t>(entity + o::entity::AIRBORNE_SUB) != 0) {
        a.flags |= AvatarAirborne;
    }
    a.velocity = {mem.template read<float>(actor + capture::ACTOR_VELOCITY),
                  mem.template read<float>(actor + capture::ACTOR_VELOCITY + 4),
                  mem.template read<float>(actor + capture::ACTOR_VELOCITY + 8)};
    a.motionId = mem.template read<std::uint32_t>(actor + o::actor::ANIM_ID);

    a.motionTime = mem.template read<float>(actor + capture::ACTOR_MOTION_TIME);
    a.motionSpeed = 1.0f;
    return a;
}

// In-process reader for the DLL: plain loads. Only call with addresses the
// game currently maps (the DLL already validates actor pointers each frame).
struct DirectMemory {
    template <class T>
    T read(std::uint64_t address) const {
        return *reinterpret_cast<const volatile T*>(address);
    }
};

} // namespace kh2coop
