#pragma once
// VUH-2415 crown inputs: the field/menu/event gates and this frame's camera,
// read-only, in their own SEH frame. A faulting read leaves the crown hidden
// and returns normally, so the avatar/HUD publish around it continues.
// Takes the image base so a test can point it at owned or no-access memory.

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

// Returns false when a read faulted; fieldClear/cameraValid are then false.
inline bool ReadInputs(std::uintptr_t exeBase, hud::Input& input) noexcept {
    input.fieldClear = input.cameraValid = false;
    bool field = false, finite = false;
    Vec3 look {}, eye {};
    __try {
        field = *reinterpret_cast<const volatile std::uint8_t*>(exeBase + offsets::IN_FIELD) != 0 &&
            *reinterpret_cast<const volatile std::uint8_t*>(exeBase + offsets::OPEN_MENU) == 0xFF &&
            *reinterpret_cast<const volatile std::int32_t*>(exeBase + offsets::CUTSCENE_STATE) == 0 &&
            *reinterpret_cast<const volatile std::uintptr_t*>(exeBase + offsets::EVENT_CONTEXT) == 0;
        const auto* camera = reinterpret_cast<const volatile float*>(exeBase + offsets::CAMERA_STRUCT);
        constexpr std::size_t l = offsets::camera::SMOOTH_LOOKAT / 4, e = offsets::camera::EYE_POS / 4;
        look = {camera[l], camera[l + 1], camera[l + 2]};
        eye = {camera[e], camera[e + 1], camera[e + 2]};
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    finite = std::isfinite(eye.x) && std::isfinite(eye.y) && std::isfinite(eye.z) &&
        std::isfinite(look.x) && std::isfinite(look.y) && std::isfinite(look.z);
    input.cameraEye = eye;
    input.cameraLook = look;
    input.cameraValid = finite;
    input.fieldClear = field;
    return true;
}

}  // namespace kh2coop::inject::crown
