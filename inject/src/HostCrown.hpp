#pragma once
// VUH-2415: a crown over the host (network slot 0) on every player's screen,
// as the TCM co-op mod shows. Pure: no game reads, pointers or GPU work.
// The game thread supplies the host's avatar position and the camera; the
// Present hook copies the returned rectangles into the backbuffer. The copy
// path has no blending, so the crown is a few opaque rectangles.

#include "kh2coop/Types.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace kh2coop::inject::crown {

// Side of each solid colour block in the upload buffer; every rect fits in one.
inline constexpr int Block = 64;
inline constexpr std::size_t MaxRects = 6;
// KH2's projection matrix is not located. Live crown-02 (4/26): the look-at, about 170 units above
// the feet, sat 300 px above them at depth ~419 on 1080 lines, so tan(fov/2) ~= 0.73.
inline constexpr float DefaultFovYDegrees = 70.0f;
inline constexpr float DefaultHeadHeight = 185.0f;  // world units above the feet: just over Sora's head (crown-03)
inline constexpr float NearPlane = 10.0f;

enum Color : std::uint8_t { Gold = 0, Shade = 1 };
struct Rect {
    int x {}, y {}, w {}, h {};
    Color color {Gold};
};
struct Shape {
    std::array<Rect, MaxRects> rects {};
    std::size_t count {};
};
struct Point {
    bool visible {};
    float x {}, y {};
};

inline Vec3 Sub(const Vec3& a, const Vec3& b) noexcept { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline float Dot(const Vec3& a, const Vec3& b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 Cross(const Vec3& a, const Vec3& b) noexcept {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline bool Normalize(Vec3& v) noexcept {
    const float length = std::sqrt(Dot(v, v));
    if (!(length > 1e-3f) || !std::isfinite(length)) return false;
    v = {v.x / length, v.y / length, v.z / length};
    return true;
}

// Screen position of a world point for a camera at `eye` looking at `look`.
// KH2's world has +Y down. Screen right is the forward direction turned like
// the left stick's +X (the route pilot's calibrated steer basis).
inline Point Project(const Vec3& eye, const Vec3& look, const Vec3& point, float fovYDegrees,
                     int width, int height) noexcept {
    Point out;
    if (width <= 0 || height <= 0 || !(fovYDegrees > 1.0f && fovYDegrees < 170.0f)) return out;
    Vec3 forward = Sub(look, eye);
    Vec3 right {forward.z, 0.0f, -forward.x};
    if (!Normalize(forward) || !Normalize(right)) return out;
    Vec3 up = Cross(right, forward);
    if (!Normalize(up)) return out;
    if (up.y > 0.0f) up = {-up.x, -up.y, -up.z};  // up is -Y
    const Vec3 d = Sub(point, eye);
    const float depth = Dot(d, forward);
    if (!(depth > NearPlane) || !std::isfinite(depth)) return out;
    const float tanHalf = std::tan(fovYDegrees * 3.14159265f / 360.0f);
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    const float nx = Dot(d, right) / (depth * tanHalf * aspect);
    const float ny = Dot(d, up) / (depth * tanHalf);
    if (!std::isfinite(nx) || !std::isfinite(ny) || std::fabs(nx) > 1.2f || std::fabs(ny) > 1.2f) return out;
    out.visible = true;
    out.x = (nx + 1.0f) * 0.5f * static_cast<float>(width);
    out.y = (1.0f - ny) * 0.5f * static_cast<float>(height);
    return out;
}

// Clip to [0,width) x [0,height). An empty result is dropped: CopyTextureRegion
// outside the destination is invalid and must never be recorded.
inline bool Clip(Rect& r, int width, int height) noexcept {
    const int x0 = r.x < 0 ? 0 : r.x, y0 = r.y < 0 ? 0 : r.y;
    const int x1 = r.x + r.w > width ? width : r.x + r.w;
    const int y1 = r.y + r.h > height ? height : r.y + r.h;
    if (x1 <= x0 || y1 <= y0) return false;
    r = {x0, y0, x1 - x0, y1 - y0, r.color};
    return true;
}

// Source box for a rect's colour block. Same fields as D3D12_BOX; the Present hook
// copies them by name, never by position (crown-01 drew nothing with a positional box).
struct Box {
    unsigned left {}, top {}, front {}, right {}, bottom {}, back {};
};
inline Box SourceBox(const Rect& r) noexcept {
    Box box;
    box.left = r.color == Shade ? static_cast<unsigned>(Block) : 0u;
    box.right = box.left + static_cast<unsigned>(r.w);
    box.top = 0u;
    box.bottom = static_cast<unsigned>(r.h);
    box.front = 0u;
    box.back = 1u;
    return box;
}

// A small crown whose band's bottom centre sits at (cx, bottom). Scales with the
// backbuffer height (1x at 1080 lines, at most 2x so each rect fits a Block).
inline Shape Crown(float cx, float bottom, int width, int height) noexcept {
    Shape out;
    float s = static_cast<float>(height) / 1080.0f;
    s = s < 1.0f ? 1.0f : (s > 2.0f ? 2.0f : s);
    const auto px = [s](float v) { return static_cast<int>(std::lround(v * s)); };
    const int x = static_cast<int>(std::lround(cx)) - px(14), y = static_cast<int>(std::lround(bottom));
    const Rect parts[] = {
        {x, y - px(7), px(28), px(7), Gold},                // band
        {x, y - px(15), px(6), px(8), Gold},                // left point
        {x + px(11), y - px(18), px(6), px(11), Gold},      // centre point
        {x + px(13), y - px(21), px(2), px(3), Gold},       // centre tip
        {x + px(22), y - px(15), px(6), px(8), Gold},       // right point
        {x + px(12), y - px(5), px(4), px(3), Shade},       // jewel
    };
    for (Rect r : parts) {
        if (r.w > Block || r.h > Block) continue;
        if (Clip(r, width, height)) out.rects[out.count++] = r;
    }
    return out;
}

// Crown for the host's feet position, or an empty shape when it is off screen.
inline Shape ForHost(const Vec3& eye, const Vec3& look, const Vec3& hostFeet, float fovYDegrees,
                     float headHeight, int width, int height) noexcept {
    const Vec3 head {hostFeet.x, hostFeet.y - headHeight, hostFeet.z};
    const Point p = Project(eye, look, head, fovYDegrees, width, height);
    return p.visible ? Crown(p.x, p.y, width, height) : Shape {};
}

}  // namespace kh2coop::inject::crown
