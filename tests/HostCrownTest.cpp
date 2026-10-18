// VUH-2415 host crown: projection, crown rectangles and the snapshot gate. No game.
#include "CoopHudState.hpp"
#include "CrownInputs.hpp"
#include "HostCrown.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace kh2coop;
using namespace kh2coop::inject;

namespace {
int checks = 0, failures = 0;
void Check(bool yes, const char* name) {
    ++checks;
    if (!yes) ++failures;
    std::printf("%s %s\n", yes ? "PASS" : "FAIL", name);
}
bool Near(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }

// Camera 420 units behind and 80 above a target at the origin, looking along +Z
// (KH2: +Y is down, so "above" is negative Y).
const Vec3 kEye {0.0f, -80.0f, -420.0f};
const Vec3 kLook {0.0f, -80.0f, 0.0f};

void Projection() {
    auto p = crown::Project(kEye, kLook, {0.0f, -80.0f, 0.0f}, 45.0f, 1920, 1080);
    Check(p.visible && Near(p.x, 960.0f, 0.5f) && Near(p.y, 540.0f, 0.5f), "look-at point projects to screen centre");
    p = crown::Project(kEye, kLook, {0.0f, -200.0f, 0.0f}, 45.0f, 1920, 1080);
    Check(p.visible && p.y < 540.0f, "a higher point (more negative Y) is higher on screen");
    // Screen right is the pilot's +X stick direction: forward (0,0,1) turns to (1,0,0).
    p = crown::Project(kEye, kLook, {100.0f, -80.0f, 0.0f}, 45.0f, 1920, 1080);
    Check(p.visible && p.x > 960.0f && Near(p.y, 540.0f, 0.5f), "+X is screen right when facing +Z");
    const float expected = 540.0f - 120.0f / (420.0f * std::tan(22.5f * 3.14159265f / 180.0f)) * 540.0f;
    p = crown::Project(kEye, kLook, {0.0f, -200.0f, 0.0f}, 45.0f, 1920, 1080);
    Check(Near(p.y, expected, 0.5f), "vertical offset follows the field of view");
    Check(!crown::Project(kEye, kLook, {0.0f, -80.0f, -900.0f}, 45.0f, 1920, 1080).visible, "behind the camera is hidden");
    Check(!crown::Project(kEye, kLook, {5000.0f, -80.0f, 10.0f}, 45.0f, 1920, 1080).visible, "far off screen is hidden");
    Check(!crown::Project(kEye, kEye, {0.0f, 0.0f, 0.0f}, 45.0f, 1920, 1080).visible, "degenerate camera is hidden");
    Check(!crown::Project(kEye, kLook, {0.0f, -80.0f, 0.0f}, 0.0f, 1920, 1080).visible, "invalid field of view is hidden");
    const Vec3 nan {std::nanf(""), 0.0f, 0.0f};
    Check(!crown::Project(nan, kLook, {0.0f, 0.0f, 0.0f}, 45.0f, 1920, 1080).visible, "non-finite camera is hidden");
}

bool Inside(const crown::Shape& shape, int width, int height) {
    for (std::size_t i = 0; i < shape.count; ++i) {
        const auto& r = shape.rects[i];
        if (r.x < 0 || r.y < 0 || r.w <= 0 || r.h <= 0 || r.x + r.w > width || r.y + r.h > height ||
            r.w > crown::Block || r.h > crown::Block) return false;
    }
    return true;
}

void Shapes() {
    auto shape = crown::Crown(960.0f, 500.0f, 1920, 1080);
    Check(shape.count == crown::MaxRects && Inside(shape, 1920, 1080), "full crown at 1080p stays inside");
    int bottom = 0;
    for (std::size_t i = 0; i < shape.count; ++i) bottom = (std::max)(bottom, shape.rects[i].y + shape.rects[i].h);
    Check(bottom == 500, "crown band sits on the projected head point");
    shape = crown::Crown(1920.0f, 2160.0f, 3840, 2160);
    Check(Inside(shape, 3840, 2160) && shape.count > 0, "4K crown is clipped at the corner and stays in blocks");
    shape = crown::Crown(-100.0f, -100.0f, 1920, 1080);
    Check(shape.count == 0, "crown fully off screen records no copies");
    shape = crown::Crown(5.0f, 30.0f, 1920, 1080);
    Check(Inside(shape, 1920, 1080), "crown at the edge is clipped, never out of bounds");
    crown::Rect r {-5, -5, 10, 10, crown::Gold};
    Check(crown::Clip(r, 4, 4) && r.x == 0 && r.y == 0 && r.w == 4 && r.h == 4, "clip trims to the destination");
    shape = crown::ForHost(kEye, kLook, {0.0f, 0.0f, 0.0f}, 45.0f, 180.0f, 1920, 1080);
    Check(shape.count > 0 && Inside(shape, 1920, 1080), "host feet project to a crown above the head");
    shape = crown::ForHost(kEye, kLook, {0.0f, 0.0f, -2000.0f}, 45.0f, 180.0f, 1920, 1080);
    Check(shape.count == 0, "host behind the camera has no crown");
    // crown-01 drew nothing: the box was built as {left, top, right, bottom, ...} (empty).
    const auto gold = crown::SourceBox({100, 100, 28, 7, crown::Gold});
    const auto shade = crown::SourceBox({100, 100, 4, 3, crown::Shade});
    Check(gold.left == 0 && gold.right == 28 && gold.top == 0 && gold.bottom == 7 && gold.front == 0 &&
          gold.back == 1, "gold source box: named D3D12 fields, non-empty, one slice deep");
    Check(shade.left == 64 && shade.right == 68 && shade.top == 0 && shade.bottom == 3 && shade.front == 0 &&
          shade.back == 1, "shade source box selects the second colour block");
    shape = crown::Crown(960.0f, 500.0f, 3840, 2160);
    bool boxesFit = shape.count > 0;
    for (std::size_t i = 0; i < shape.count; ++i) {
        const auto b = crown::SourceBox(shape.rects[i]);
        boxesFit = boxesFit && b.right > b.left && b.bottom > b.top && b.back > b.front &&
            b.right <= 2u * crown::Block && b.bottom <= static_cast<unsigned>(crown::Block);
    }
    Check(boxesFit, "every crown rect's source box is non-empty and inside the colour blocks");
}

// VUH-2455: head height by camera distance, against run 20261018-004712's logged cameras.
void Height() {
    Check(crown::HeadHeight(kEye, {0.0f, -80.0f, -100.0f}) == crown::DefaultHeadHeight,
          "a near camera keeps the default head height");
    Check(crown::HeadHeight(kEye, {0.0f, -80.0f, 600.0f}) == crown::FarHeadHeight, "a far camera clamps at the far height");
    const float mid = crown::HeadHeight({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 381.0f});
    Check(mid < crown::DefaultHeadHeight && mid > crown::FarHeadHeight, "between them the height is interpolated");
    Check(crown::HeadHeight({std::nanf(""), 0.0f, 0.0f}, kLook) == crown::DefaultHeadHeight,
          "a non-finite camera keeps the default head height");
    // 4/26 field: the crown band stays where crown-03 placed it, ~35 px over Sora's hair (y ~545).
    const Vec3 fieldEye {2025.0f, -254.0f, 0.0f}, fieldLook {1697.0f, -190.0f, 0.0f}, fieldHost {1696.0f, -20.0f, 0.0f};
    auto p = crown::Project(fieldEye, fieldLook,
        {fieldHost.x, fieldHost.y - crown::HeadHeight(fieldEye, fieldLook), fieldHost.z}, 70.0f, 1920, 1080);
    Check(p.visible && Near(p.y, 505.0f, 3.0f), "field camera keeps the crown just over Sora");
    // BC 05/0B fight: Sora's hair at y ~593; 185 put the band at 513, ~80 px over it.
    const Vec3 bcEye {-23.0f, -251.0f, -6.0f}, bcLook {-24.0f, -170.0f, -426.0f}, bcHost {-25.0f, 0.0f, -426.0f};
    p = crown::Project(bcEye, bcLook, {bcHost.x, bcHost.y - crown::HeadHeight(bcEye, bcLook), bcHost.z}, 70.0f, 1920, 1080);
    Check(p.visible && p.y > 555.0f && p.y < 565.0f, "fight camera lowers the crown to ~33 px over Sora");
}

// The isolated reader over owned memory laid out like the game image, and over
// reserved-but-uncommitted memory, where every read faults.
void Reader() {
    const std::size_t span = offsets::EVENT_CONTEXT + 0x1000;
    auto* image = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, span, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    Check(image != nullptr, "test image allocated");
    if (!image) return;
    const auto base = reinterpret_cast<std::uintptr_t>(image);
    image[offsets::IN_FIELD] = 1;
    image[offsets::OPEN_MENU] = 0xFF;
    auto* camera = reinterpret_cast<float*>(image + offsets::CAMERA_STRUCT);
    const float look[3] {1.0f, -2.0f, 3.0f}, eye[3] {4.0f, -5.0f, 6.0f};
    std::memcpy(camera + offsets::camera::SMOOTH_LOOKAT / 4, look, sizeof(look));
    std::memcpy(camera + offsets::camera::EYE_POS / 4, eye, sizeof(eye));
    const std::uintptr_t sora = 0x7FF612340000;
    std::memcpy(image + offsets::CAMERA_STRUCT + offsets::camera::ACTOR_PTR, &sora, sizeof(sora));
    hud::Input in;
    Check(crown::ReadInputs(base, sora, in) && in.fieldClear && in.cameraValid && in.cameraLook.y == -2.0f &&
          in.cameraEye.z == 6.0f, "reader takes field gates and camera from the image");
    Check(in.cameraFollowsLocal, "camera following the local Sora is read");
    Check(crown::ReadInputs(base, sora + 0x10, in) && !in.cameraFollowsLocal, "camera on another actor is not local");
    Check(crown::ReadInputs(base, 0, in) && !in.cameraFollowsLocal, "an unknown local actor never matches");
    image[offsets::OPEN_MENU] = 3;
    Check(crown::ReadInputs(base, sora, in) && !in.fieldClear && in.cameraValid && in.gates.openMenu == 3,
          "an open menu clears the field gate");
    image[offsets::OPEN_MENU] = 0xFF;
    const std::uintptr_t event = 1;
    std::memcpy(image + offsets::EVENT_CONTEXT, &event, sizeof(event));
    Check(crown::ReadInputs(base, sora, in) && !in.fieldClear && in.gates.eventActive && in.gates.inField == 1,
          "an event clears the field gate and is logged raw");
    std::memset(image + offsets::EVENT_CONTEXT, 0, sizeof(event));
    camera[offsets::camera::EYE_POS / 4] = std::nanf("");
    Check(crown::ReadInputs(base, sora, in) && in.fieldClear && !in.cameraValid, "a non-finite camera is not valid");
    VirtualFree(image, 0, MEM_RELEASE);

    auto* reserved = VirtualAlloc(nullptr, span, MEM_RESERVE, PAGE_NOACCESS);
    Check(reserved != nullptr, "reserved test range allocated");
    if (!reserved) return;
    in.fieldClear = in.cameraValid = in.cameraFollowsLocal = true;
    const bool read = crown::ReadInputs(reinterpret_cast<std::uintptr_t>(reserved), sora, in);
    Check(!read && !in.fieldClear && !in.cameraValid && !in.cameraFollowsLocal,
          "a faulting read returns normally with the crown hidden");
    VirtualFree(reserved, 0, MEM_RELEASE);
}

hud::Input Fixture(std::uint8_t localSlot) {
    hud::Input in;
    in.before = {PuppetAuthorityMode::Network, localSlot, 7, {101, 102, 103}};
    in.after = in.before;
    in.localAvailable = in.gameplayCurrent = in.fieldClear = in.cameraValid = in.cameraFollowsLocal = true;
    in.clearFrames = hud::CrownSettleFrames;
    in.cameraEye = kEye;
    in.cameraLook = kLook;
    in.local.worldId = 4; in.local.roomId = 26; in.local.hp = 18; in.local.maxHp = 24;
    in.local.position = {1.0f, 2.0f, 3.0f};
    int index = 0;
    for (std::uint8_t slot = 0; slot < 3; ++slot) {
        if (slot == localSlot) continue;
        auto& remote = in.remote[static_cast<std::size_t>(index++)];
        remote.active = true;
        remote.avatar = in.local;
        remote.avatar.ownerSlot = static_cast<SlotType>(slot);
        remote.avatar.position = {10.0f * slot, 20.0f, 30.0f};
        remote.provenance = {PuppetProducer::Network, localSlot, 7,
            in.after.connectionIds[slot], in.after.connectionIds[localSlot], 101};
    }
    return in;
}

void Gate() {
    auto snap = hud::Project(Fixture(0), 100, 1);
    Check(snap.crownCurrent && snap.crownHost.x == 1.0f && snap.crownHost.z == 3.0f &&
          snap.cameraEye.z == kEye.z && snap.cameraLook.y == kLook.y, "host sees the crown on itself");
    snap = hud::Project(Fixture(1), 100, 1);
    Check(snap.crownCurrent && snap.crownHost.x == 0.0f && snap.crownHost.y == 20.0f,
          "friend places the crown on the slot-0 puppet, not on itself");
    auto in = Fixture(1);
    in.fieldClear = false;
    Check(!hud::Project(in, 100, 1).crownCurrent, "menu, event or loading hides the crown");
    in = Fixture(1);
    in.cameraValid = false;
    Check(!hud::Project(in, 100, 1).crownCurrent, "unread camera hides the crown");
    in = Fixture(0);
    in.cameraFollowsLocal = false;
    Check(!hud::Project(in, 100, 1).crownCurrent, "a camera off the local Sora (event shot) hides the crown");
    in = Fixture(1);
    in.clearFrames = hud::CrownSettleFrames - 1;
    Check(!hud::Project(in, 100, 1).crownCurrent, "a gate clear for fewer than the settle frames hides the crown");
    // VUH-2455 live: the BC battle-start close-ups follow the event with the gate clear.
    in = Fixture(1);
    in.afterEvent = true;
    in.clearFrames = hud::CrownSettleFrames;
    Check(!hud::Project(in, 100, 1).crownCurrent, "after an event the ordinary settle is not enough");
    in.clearFrames = hud::CrownEventSettleFrames - 1;
    Check(!hud::Project(in, 100, 1).crownCurrent, "after an event the crown waits the whole event settle");
    in.clearFrames = hud::CrownEventSettleFrames;
    Check(hud::Project(in, 100, 1).crownCurrent, "after the event settle the crown shows");
    in = Fixture(1);
    for (auto& remote : in.remote) if (static_cast<int>(remote.avatar.ownerSlot) == 0) remote.active = false;
    Check(!hud::Project(in, 100, 1).crownCurrent, "inactive host puppet has no crown");
    in = Fixture(1);
    for (auto& remote : in.remote) if (static_cast<int>(remote.avatar.ownerSlot) == 0) remote.avatar.roomId = 27;
    Check(!hud::Project(in, 100, 1).crownCurrent, "host in another room has no crown");
    in = Fixture(1);
    for (auto& remote : in.remote) if (static_cast<int>(remote.avatar.ownerSlot) == 0) remote.avatar.flags = AvatarInCutscene;
    Check(!hud::Project(in, 100, 1).crownCurrent, "host in a cutscene has no crown");
    in = Fixture(0);
    in.local.flags = AvatarInCutscene;
    Check(!hud::Project(in, 100, 1).crownCurrent, "local cutscene hides the crown");
    in = Fixture(0);
    in.gameplayCurrent = false;
    Check(!hud::Project(in, 100, 1).crownCurrent, "suspended puppets or a pending warp hide the crown");
    in = Fixture(0);
    in.after.generation = 8;
    Check(!hud::Project(in, 100, 1).crownCurrent, "changed authority hides the crown");
}
}  // namespace

int main() {
    Projection();
    Shapes();
    Height();
    Reader();
    Gate();
    std::printf("Host crown: %s (%d checks, %d failures)\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
