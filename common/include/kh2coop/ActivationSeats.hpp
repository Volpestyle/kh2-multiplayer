#pragma once
// VUH-2864: either living player starts a wave. Pure rules only: no game memory, no Windows calls.
//
// The game ticks every spawn controller with one point, the host actor's position
// (FUN_1403a4f90 -> 0x3FF000), and a type-2 controller dispatches its delayed records when any
// of its regions contains that point (0x3FE320 case 1 ignores which region accepted). The host
// therefore passes, per controller, its own point when a region contains it, otherwise the first
// eligible remote seat's point that a region contains, otherwise its own point (native). The
// client receives the same candidates with its lease and makes the same choice over its own
// copy of the authored regions. Native predicates (docs/pointer_map_v1.md, "Runtime region
// nodes"): BOX 0x421180, CYLINDER 0x421250, INFINITY 0x421320. Both force w = 1 and transform
// by the node's inverse matrix: q[j] = M[j]*x + M[j+4]*y + M[j+8]*z + M[j+12].

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace kh2coop::activationseats {

inline constexpr std::size_t kMaxSeats = 2;     // remote humans besides the host (three seats)
inline constexpr std::size_t kMaxRegions = 32;  // safety bound on one controller's region list

using Point = std::array<float, 4>;

// Remote seats' native positions on the host, in seat order (slot 1, then 2).
struct SeatPoints {
    std::uint8_t count = 0;
    std::array<Point, kMaxSeats> points {};
    std::array<std::uint8_t, kMaxSeats> slots {};
    bool operator==(const SeatPoints&) const = default;
};

enum class RegionKind : std::uint8_t { Box, Cylinder, Infinity };

struct Region {
    RegionKind kind = RegionKind::Box;
    std::array<float, 16> inverse {};
    std::array<float, 3> extent {};
};

inline bool Finite(const Point& p) noexcept {
    return std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]) && std::isfinite(p[3]);
}

inline bool Contains(const Region& r, const Point& p) noexcept {
    if (r.kind == RegionKind::Infinity) return true;
    const auto& m = r.inverse;
    const float x = p[0], y = p[1], z = p[2];
    const float qx = m[0] * x + m[4] * y + m[8] * z + m[12];
    const float qy = m[1] * x + m[5] * y + m[9] * z + m[13];
    const float qz = m[2] * x + m[6] * y + m[10] * z + m[14];
    if (r.kind == RegionKind::Box)
        return -r.extent[0] <= qx && qx <= r.extent[0] && -r.extent[1] <= qy && qy <= r.extent[1] &&
               -r.extent[2] <= qz && qz <= r.extent[2];
    if (!(-r.extent[1] <= qy && qy <= r.extent[1])) return false;
    const float nx = qx / r.extent[0], nz = qz / r.extent[2];
    return nz * nz + nx * nx <= 1.0f;
}

inline bool AnyContains(std::span<const Region> regions, const Point& p) noexcept {
    for (const auto& r : regions)
        if (Contains(r, p)) return true;
    return false;
}

// 0 = the host's own point; i > 0 = seats.points[i - 1]. Unreadable regions keep the host's.
inline std::size_t Select(std::span<const Region> regions, bool regionsAvailable, const Point& host,
                          const SeatPoints& seats) noexcept {
    if (!regionsAvailable || regions.empty() || AnyContains(regions, host)) return 0;
    for (std::size_t i = 0; i < seats.count && i < kMaxSeats; ++i)
        if (Finite(seats.points[i]) && AnyContains(regions, seats.points[i])) return i + 1;
    return 0;
}

// A well-formed candidate set: count in range, used points finite with w = 1, distinct seat
// slots 1..2 in ascending order, unused entries zero. The codec and the lease both require it.
inline bool WellFormed(const SeatPoints& s) noexcept {
    if (s.count > kMaxSeats) return false;
    for (std::size_t i = 0; i < kMaxSeats; ++i) {
        if (i < s.count) {
            if (!Finite(s.points[i]) || s.points[i][3] != 1.0f || s.slots[i] < 1 || s.slots[i] > 2) return false;
            if (i && s.slots[i] <= s.slots[i - 1]) return false;
        } else if (s.slots[i] != 0 || s.points[i] != Point {}) {
            return false;
        }
    }
    return true;
}

} // namespace kh2coop::activationseats
