#pragma once
// VUH-2864: either living player starts a wave. Pure rules only: no game memory, no Windows calls.
//
// The game ticks every spawn controller with one point, the host actor's position
// (FUN_1403a4f90 -> 0x3FF000), and a type-2 controller dispatches its delayed records when any
// of its regions contains that point (0x3FE320 case 1 ignores which region accepted). The host
// therefore passes, per controller, its own point when a region contains it, otherwise the first
// eligible remote seat's point that a region contains, otherwise its own point (native),
// including when it cannot read the controller's regions. That actual choice is authoritative:
// the host records one row per controller it ticked (Choices) and sends the rows with its lease.
// A client never chooses again from its own region reads; it applies the host's row, keeps the
// host's point where the host ticked no seat at all, and holds the tick when it cannot establish
// the host's decision (a conflicting row, a truncated table, or a controller missing from a
// window in which the host used a seat). Native predicates (docs/pointer_map_v1.md, "Runtime region
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

// ---- The host's actual decisions, authoritative for clients ----------------------------------

inline constexpr std::size_t kMaxChoices = 24;    // controller rows per lease
inline constexpr std::uint8_t kSeatConflict = 0xFF;

// One controller's tick on the host: its group key and header ID, the seat whose point it
// passed (0 = the host's own point, 1..2 = that remote slot, kSeatConflict = two different
// controllers or decisions under one identity in a window), and the exact point it passed.
struct Choice {
    std::uint32_t key = 0;
    std::uint16_t header = 0;
    std::uint8_t seat = 0;
    Point point {};
    bool operator==(const Choice&) const = default;
};

struct Choices {
    std::uint8_t count = 0;
    std::uint8_t truncated = 0;  // a row did not fit: missing rows cannot be told from untouched controllers
    std::array<Choice, kMaxChoices> rows {};
    bool operator==(const Choices&) const = default;
};

inline bool WellFormed(const Choices& c) noexcept {
    if (c.count > kMaxChoices || c.truncated > 1) return false;
    for (std::size_t i = 0; i < kMaxChoices; ++i) {
        const auto& r = c.rows[i];
        if (i >= c.count) {
            if (r != Choice {}) return false;
            continue;
        }
        if (r.seat == kSeatConflict) {
            if (r.point != Point {}) return false;
        } else if (r.seat > kMaxSeats || !Finite(r.point)) {
            return false;
        }
        for (std::size_t j = 0; j < i; ++j)
            if (c.rows[j].key == r.key && c.rows[j].header == r.header) return false;
    }
    return true;
}

// The host's recorder. owners[i] is the controller address that wrote rows[i] (host-local, never
// sent): a second write from the same controller is its newer tick and replaces the row; one from
// another controller under the same identity, with a different seat or point, is a conflict.
inline void Record(Choices& c, std::array<std::uintptr_t, kMaxChoices>& owners, std::uintptr_t controller,
                   const Choice& choice) noexcept {
    for (std::size_t i = 0; i < c.count; ++i) {
        auto& r = c.rows[i];
        if (r.key != choice.key || r.header != choice.header) continue;
        if (owners[i] == controller) {
            if (r.seat != kSeatConflict) r = choice;
        } else if (r.seat != choice.seat || r.point != choice.point) {
            r.seat = kSeatConflict;
            r.point = {};
        }
        return;
    }
    if (c.count == kMaxChoices) { c.truncated = 1; return; }
    owners[c.count] = controller;
    c.rows[c.count++] = choice;
}

enum class Resolution : std::uint8_t { Host, Row, Hold };
struct Resolved {
    Resolution kind = Resolution::Host;
    Point point {};
    std::uint8_t seat = 0;
};

// The client's decision for one controller. Host: no row and the host passed no seat point in
// that window (or recorded nothing): tick with the leased host point, as before VUH-2864.
inline Resolved Resolve(const Choices& c, std::uint32_t key, std::uint16_t header) noexcept {
    for (std::size_t i = 0; i < c.count && i < kMaxChoices; ++i) {
        const auto& r = c.rows[i];
        if (r.key != key || r.header != header) continue;
        if (r.seat == kSeatConflict) return {Resolution::Hold};
        return {Resolution::Row, r.point, r.seat};
    }
    if (c.truncated) return {Resolution::Hold};
    for (std::size_t i = 0; i < c.count && i < kMaxChoices; ++i)
        if (c.rows[i].seat != 0) return {Resolution::Hold};
    return {Resolution::Host};
}

} // namespace kh2coop::activationseats
