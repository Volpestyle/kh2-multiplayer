#pragma once
// VUH-2415 crown inputs: the field/menu/event gates and this frame's camera,
// read-only, in their own SEH frame. A faulting read leaves the crown hidden
// and returns normally, so the avatar/HUD publish around it continues.
// Takes the image base so a test can point it at owned or no-access memory.
// VUH-2455: also whether the camera follows `localActor` (the local Sora), and the raw gates.

#include "CoopHudState.hpp"
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

}  // namespace kh2coop::inject::crown
