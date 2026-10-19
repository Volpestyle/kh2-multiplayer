#pragma once
// VUH-2415 crown inputs: the field/menu/event gates and this frame's camera,
// read-only, in their own SEH frame. A faulting read leaves the crown hidden
// and returns normally, so the avatar/HUD publish around it continues.
// Takes the image base so a test can point it at owned or no-access memory.
// VUH-2455: also whether the camera follows `localActor` (the local Sora), and the raw gates.
// VUH-2480: ReadHostHead reads the crowned actor's head-bone world matrix and the view-projection.

#include "CoopHudState.hpp"
#include "kh2coop/CrownProp.hpp"
#include "kh2coop/KH2Offsets.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace kh2coop::inject::crown {

// Returns false when a read faulted; fieldClear/cameraValid/cameraFollowsLocal are then false.
inline bool ReadInputs(std::uintptr_t exeBase, std::uintptr_t localActor, hud::Input& input) noexcept {
    input.fieldClear = input.cameraValid = input.cameraFollowsLocal = false;
    input.gates = {};
    bool finite = false;
    std::uint8_t inField = 0, openMenu = 0;
    std::int32_t cutscene = 0;
    std::uintptr_t event = 0, followed = 0;
    Vec3 look {}, eye {};
    __try {
        inField = *reinterpret_cast<const volatile std::uint8_t*>(exeBase + offsets::IN_FIELD);
        openMenu = *reinterpret_cast<const volatile std::uint8_t*>(exeBase + offsets::OPEN_MENU);
        cutscene = *reinterpret_cast<const volatile std::int32_t*>(exeBase + offsets::CUTSCENE_STATE);
        event = *reinterpret_cast<const volatile std::uintptr_t*>(exeBase + offsets::EVENT_CONTEXT);
        const auto* camera = reinterpret_cast<const volatile float*>(exeBase + offsets::CAMERA_STRUCT);
        constexpr std::size_t l = offsets::camera::SMOOTH_LOOKAT / 4, e = offsets::camera::EYE_POS / 4;
        look = {camera[l], camera[l + 1], camera[l + 2]};
        eye = {camera[e], camera[e + 1], camera[e + 2]};
        followed = *reinterpret_cast<const volatile std::uintptr_t*>(
            exeBase + offsets::CAMERA_STRUCT + offsets::camera::ACTOR_PTR);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    input.gates.inField = inField;
    input.gates.openMenu = openMenu;
    input.gates.cutsceneState = cutscene;
    input.gates.eventActive = event != 0;
    const bool field = inField != 0 && openMenu == 0xFF && cutscene == 0 && event == 0;
    finite = std::isfinite(eye.x) && std::isfinite(eye.y) && std::isfinite(eye.z) &&
        std::isfinite(look.x) && std::isfinite(look.y) && std::isfinite(look.z);
    input.cameraEye = eye;
    input.cameraLook = look;
    input.cameraValid = finite;
    input.fieldClear = field;
    input.cameraFollowsLocal = localActor != 0 && followed == localActor;
    return true;
}

namespace detail {
// Raw copies only, each in its own SEH frame.
inline bool CopyModelName(std::uintptr_t actor, char (&name)[32]) noexcept {
    __try {
        const auto objentry = *reinterpret_cast<const volatile std::uintptr_t*>(actor + crownprop::ActorObjentry);
        for (std::uintptr_t i = 0; i < 31; ++i)
            name[i] = *reinterpret_cast<const volatile char*>(objentry + crownprop::ObjentryName + i);
        name[31] = '\0';
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
inline bool CopyHeadInputs(std::uintptr_t exeBase, std::uintptr_t actor, std::uint16_t boneIndex, float* boneOut,
                           float* worldOut, float* viewProjOut) noexcept {
    __try {
        const auto model = *reinterpret_cast<const volatile std::uintptr_t*>(actor + crownprop::ActorModel);
        const auto bones = *reinterpret_cast<const volatile std::uintptr_t*>(model + crownprop::ModelBones);
        const auto* b = reinterpret_cast<const volatile float*>(bones + 0x40u * boneIndex);
        const auto* w = reinterpret_cast<const volatile float*>(actor + crownprop::ActorWorld);
        const auto* vp = reinterpret_cast<const volatile float*>(exeBase + crownprop::ViewProjRva);
        for (int i = 0; i < 16; ++i) { boneOut[i] = b[i]; worldOut[i] = w[i]; viewProjOut[i] = vp[i]; }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
}  // namespace detail

// VUH-2480: fills out.crownHead/crownViewProj/crownCharacter for `actor` when it is a known
// gameplay model, every value is finite and the head sits within reach of the actor.
// A faulting read, an unknown model or an implausible matrix leaves crownHeadValid false.
inline bool ReadHostHead(std::uintptr_t exeBase, std::uintptr_t actor, hud::Snapshot& out) noexcept {
    out.crownHeadValid = false;
    if (!exeBase || !actor) return false;
    char name[32] {};
    if (!detail::CopyModelName(actor, name)) return false;
    const auto* who = crownprop::FindCharacter(std::span<const char>(name, 32));
    crownprop::Matrix bone {}, world {}, viewProj {};
    if (!who || !detail::CopyHeadInputs(exeBase, actor, who->headBone, bone.data(), world.data(), viewProj.data()))
        return false;
    // View x projection: its w column (view depth) can't be empty.
    if (!crownprop::Finite(bone) || !crownprop::Finite(world) || !crownprop::Finite(viewProj) ||
        !(std::fabs(viewProj[3]) + std::fabs(viewProj[7]) + std::fabs(viewProj[11]) > 1e-3f)) return false;
    const auto head = crownprop::HeadWorld(bone, world);
    const float dx = head[12] - world[12], dy = head[13] - world[13], dz = head[14] - world[14];
    if (!crownprop::Finite(head) || !(dx * dx + dy * dy + dz * dz < 400.0f * 400.0f)) return false;
    out.crownHead = head;
    out.crownViewProj = viewProj;
    out.crownCharacter = static_cast<std::uint8_t>(who - crownprop::Characters.data());
    out.crownHeadValid = true;
    return true;
}

}  // namespace kh2coop::inject::crown
