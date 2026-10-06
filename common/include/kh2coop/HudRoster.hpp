#pragma once
#include "kh2coop/PuppetProvenance.hpp"
#include <array>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace kh2coop::hudnames {
inline constexpr std::size_t NameBytes = 24;
inline constexpr std::uint64_t FreshMs = 1000;
using Name = std::array<char, NameBytes>;

// Display metadata only. No world authority, native actor identity or pointers.
// Explicit field sizes and no padding; this is local AvatarBridge v3, not wire.
struct Roster {
    std::uint64_t sampledAtMs {};
    std::array<std::uint64_t, 3> connections {};
    std::uint32_t generation {};
    std::uint32_t localSlot {255};
    std::array<Name, 3> names {};
};
static_assert(sizeof(Roster) == 112 && std::is_trivially_copyable_v<Roster> &&
              std::is_standard_layout_v<Roster>);

// Narrow display profile: printable ASCII, trimmed, at most 23 characters.
// Reject controls/NUL/non-ASCII rather than interpreting escapes or malformed UTF-8.
// Long names are explicitly ellipsized; slot/Host/You remain separate labels.
inline Name Sanitize(std::string_view input) noexcept {
    Name out {};
    if (input.empty() || input.size() > 128) return out;
    for (const unsigned char c : input) if (c < 32 || c > 126) return out;
    while (!input.empty() && input.front() == ' ') input.remove_prefix(1);
    while (!input.empty() && input.back() == ' ') input.remove_suffix(1);
    const auto count = input.size() < NameBytes ? input.size() : NameBytes - 4;
    for (std::size_t i = 0; i < count; ++i) out[i] = input[i];
    if (input.size() >= NameBytes) out[count] = out[count + 1] = out[count + 2] = '.';
    return out;
}

inline bool Valid(const Roster& r) noexcept {
    if (r.localSlot >= 3 || !r.connections[0] || !r.connections[r.localSlot]) return false;
    for (std::size_t i = 0; i < 3; ++i) {
        if (!r.connections[i]) {
            if (r.names[i] != Name {}) return false;
            continue;
        }
        if (!r.names[i][0] || r.names[i].back() != 0) return false;
        bool ended = false;
        for (const unsigned char c : r.names[i]) {
            if (!c) ended = true;
            else if (ended || c < 32 || c > 126) return false;
        }
        for (std::size_t j = 0; j < i; ++j)
            if (r.connections[j] && (r.connections[j] == r.connections[i] ||
                r.names[j] == r.names[i])) return false;
    }
    return true;
}

// Call only AFTER NetworkClient's roster admission. A duplicate/truncated name
// collision, empty name or malformed member makes display names unavailable;
// it does not change networking/session admission or HP publication.
template <typename Session>
inline Roster FromSession(const Session& session, std::uint8_t localSlot) noexcept {
    Roster out;
    out.localSlot = localSlot;
    if (session.actors.size() > 3) return {};
    for (const auto& member : session.actors) {
        const auto slot = static_cast<std::uint8_t>(member.slot);
        if (slot >= 3 || !member.connectionId || out.connections[slot]) return {};
        out.connections[slot] = member.connectionId;
        out.names[slot] = Sanitize(member.ownerPeerId);
    }
    return Valid(out) ? out : Roster {};
}

inline bool SameBinding(const Roster& r, const PuppetAuthority& a) noexcept {
    return a.mode == PuppetAuthorityMode::Network && a.generation &&
        r.localSlot == a.localSlot && r.connections == a.connectionIds && Valid(r);
}
// The runtime retains the admitted session ID separately; require an exact
// session match on every binding/renewal. A room reset may advance generation in the SAME admitted
// session; no name is copied from avatar/native friend object names.
inline Roster Bind(const Roster& admitted, const PuppetAuthority& current,
                   std::uint64_t nowMs, std::string_view admittedSession,
                   std::string_view currentSession) noexcept {
    if (admittedSession.empty() || admittedSession != currentSession ||
        !SameBinding(admitted, current)) return {};
    auto out = admitted;
    out.generation = current.generation;
    out.sampledAtMs = nowMs;
    return out;
}
inline bool Fresh(const Roster& r, std::uint64_t nowMs) noexcept {
    return r.sampledAtMs && nowMs >= r.sampledAtMs && nowMs - r.sampledAtMs < FreshMs;
}
inline bool Matches(const Roster& r, const PuppetAuthority& current,
                    std::uint64_t nowMs) noexcept {
    return r.generation == current.generation && SameBinding(r, current) && Fresh(r, nowMs);
}
} // namespace kh2coop::hudnames
