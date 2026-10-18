// VUH-2415 host crown: projection, crown rectangles and the snapshot gate. No game.
#include "CoopHudState.hpp"
#include "HostCrown.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

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
    Check(gold == std::array<unsigned, 6> {0, 0, 0, 28, 7, 1} &&
          shade == std::array<unsigned, 6> {64, 0, 0, 68, 3, 1}, "source box is in D3D12_BOX order and non-empty");
}

hud::Input Fixture(std::uint8_t localSlot) {
    hud::Input in;
    in.before = {PuppetAuthorityMode::Network, localSlot, 7, {101, 102, 103}};
    in.after = in.before;
    in.localAvailable = in.gameplayCurrent = in.fieldClear = in.cameraValid = true;
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
    Gate();
    std::printf("Host crown: %s (%d checks, %d failures)\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
