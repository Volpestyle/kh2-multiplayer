#pragma once

#include "kh2coop/Types.hpp"
#include "kh2coop/PuppetProvenance.hpp"
#include "kh2coop/HudRoster.hpp"

#include <array>
#include <cstdint>
#include <cwchar>
#include <type_traits>

namespace kh2coop::inject::hud {

inline constexpr std::uint64_t FreshMs = 1000;
inline constexpr int Width = 820, Height = 130, TrackWidth = 220;
enum class RowState : std::uint8_t { Open, Waiting, Available };
struct Member {
    std::uint64_t connectionId {};
    RowState state {RowState::Open};
    bool hpValid {};
    std::int32_t hp {}, maxHp {};
    hudnames::Name name {};
};
struct Snapshot {
    std::uint64_t sampledAtMs {}, namesSampledAtMs {};
    std::uint32_t frame {}, generation {};
    std::uint8_t localSlot {255};
    bool locationValid {}, networkCurrent {};
    std::uint16_t world {}, room {};
    std::array<Member, 3> members {}; // Network owner slot, never native friend index.
};
static_assert(std::is_trivially_copyable_v<Snapshot> && std::is_standard_layout_v<Snapshot>);

struct Remote {
    bool active {}; // Copied IsPuppetActive result, including DLL frame freshness.
    PuppetProvenance provenance {};
    AvatarState avatar {};
};
struct Input {
    PuppetAuthority before {}, after {};
    bool localAvailable {}, gameplayCurrent {};
    AvatarState local {};
    hudnames::Roster names {};
    std::array<Remote, 2> remote {};
};

inline bool SameAuthority(const PuppetAuthority& a, const PuppetAuthority& b) noexcept {
    return a.mode == b.mode && a.generation == b.generation &&
        a.localSlot == b.localSlot && a.connectionIds == b.connectionIds;
}
inline bool ValidAuthority(const PuppetAuthority& a) noexcept {
    if (a.mode != PuppetAuthorityMode::Network || !a.generation || a.localSlot >= 3 ||
        !a.connectionIds[0] || !a.connectionIds[a.localSlot]) return false;
    for (std::size_t i = 0; i < 3; ++i)
        for (std::size_t j = i + 1; j < 3; ++j)
            if (a.connectionIds[i] && a.connectionIds[i] == a.connectionIds[j]) return false;
    return true;
}
inline bool ValidHp(std::int32_t hp, std::int32_t maxHp) noexcept {
    return maxHp > 0 && hp >= 0 && hp <= maxHp;
}
inline void SetHp(Member& row, const AvatarState& avatar) noexcept {
    row.hpValid = ValidHp(avatar.hp, avatar.maxHp);
    if (row.hpValid) { row.hp = avatar.hp; row.maxHp = avatar.maxHp; }
}

// Pure projection. No pointers, game reads, allocation, formatting or publication.
inline Snapshot Project(const Input& in, std::uint64_t nowMs, std::uint32_t frame) noexcept {
    Snapshot out;
    out.sampledAtMs = nowMs;
    out.frame = frame;
    out.locationValid = in.localAvailable;
    if (out.locationValid) { out.world = in.local.worldId; out.room = in.local.roomId; }
    if (!in.localAvailable || !in.gameplayCurrent || (in.local.flags & AvatarInCutscene) ||
        !SameAuthority(in.before, in.after) || !ValidAuthority(in.after)) return out;
    const auto& authority = in.after;
    out.networkCurrent = true;
    out.generation = authority.generation;
    out.localSlot = authority.localSlot;
    const bool namesCurrent = hudnames::Matches(in.names, authority, nowMs);
    if (namesCurrent) out.namesSampledAtMs = in.names.sampledAtMs;
    for (std::size_t slot = 0; slot < 3; ++slot) {
        auto& row = out.members[slot];
        row.connectionId = authority.connectionIds[slot];
        if (namesCurrent) row.name = in.names.names[slot];
        row.state = row.connectionId ? RowState::Waiting : RowState::Open;
    }
    auto& local = out.members[out.localSlot];
    local.state = RowState::Available;
    SetHp(local, in.local); // captureAvatar.ownerSlot defaults to 0: do not use it here.
    for (int i = 0; i < 2; ++i) {
        const auto& remote = in.remote[static_cast<std::size_t>(i)];
        const auto& avatar = remote.avatar;
        const auto slot = static_cast<std::uint8_t>(avatar.ownerSlot);
        if (!remote.active || !ValidPuppetProvenance(remote.provenance, slot, i, authority) ||
            avatar.worldId != out.world || avatar.roomId != out.room ||
            (avatar.flags & AvatarInCutscene)) continue;
        auto& row = out.members[slot]; // Provenance validator bounded and mapped slot above.
        row.state = RowState::Available;
        SetHp(row, avatar);
    }
    return out;
}

inline bool Fresh(const Snapshot& value, std::uint64_t nowMs) noexcept {
    return nowMs >= value.sampledAtMs && nowMs - value.sampledAtMs < FreshMs;
}
// Check name age again at Present: a newer owner snapshot does not renew the
// runtime metadata, and a stalled owner must not extend name freshness.
inline void ExpireNames(Snapshot& snapshot, std::uint64_t nowMs) noexcept {
    if (!snapshot.namesSampledAtMs || nowMs < snapshot.namesSampledAtMs ||
        nowMs - snapshot.namesSampledAtMs >= hudnames::FreshMs) {
        snapshot.namesSampledAtMs = 0;
        for (auto& member : snapshot.members) member.name = {};
    }
}
// Changes that must invalidate rendered pixels immediately, not on the 15-Present cadence.
inline bool SameDisplayScope(const Snapshot& a, const Snapshot& b) noexcept {
    if (a.locationValid != b.locationValid || a.networkCurrent != b.networkCurrent ||
        a.world != b.world || a.room != b.room || a.generation != b.generation ||
        a.localSlot != b.localSlot) return false;
    for (std::size_t i = 0; i < 3; ++i)
        if (a.members[i].connectionId != b.members[i].connectionId ||
            a.members[i].state != b.members[i].state || a.members[i].hpValid != b.members[i].hpValid ||
            a.members[i].name != b.members[i].name)
            return false;
    return true;
}

struct RowText {
    wchar_t label[24] {}, health[32] {}, status[24] {}, name[hudnames::NameBytes] {};
    bool showBar {}, lowHealth {};
    int fillPixels {};
};
struct Text {
    bool available {};
    std::array<RowText, 3> rows {};
};
inline Text Format(const Snapshot& snapshot) noexcept {
    Text out;
    out.available = snapshot.networkCurrent;
    if (!out.available) return out;
    for (std::size_t slot = 0; slot < 3; ++slot) {
        auto& text = out.rows[slot];
        const auto& member = snapshot.members[slot];
        (void)std::swprintf(text.label, 24, L"P%u%ls%ls", static_cast<unsigned>(slot + 1),
            slot == 0 ? L" Host" : L"", slot == snapshot.localSlot ? L" You" : L"");
        if (member.state == RowState::Available) {
            if (member.name[0]) {
                for (std::size_t i = 0; i + 1 < hudnames::NameBytes; ++i)
                    text.name[i] = static_cast<unsigned char>(member.name[i]);
            } else {
                (void)std::swprintf(text.name, hudnames::NameBytes, L"Name unavailable");
            }
        }
        if (member.state == RowState::Open) {
            (void)std::swprintf(text.status, 24, L"Open slot");
        } else if (member.state == RowState::Waiting) {
            (void)std::swprintf(text.status, 24, L"Waiting for avatar");
        } else if (member.hpValid && ValidHp(member.hp, member.maxHp)) {
            (void)std::swprintf(text.health, 32, L"%d / %d", member.hp, member.maxHp);
            text.showBar = true;
            text.fillPixels = static_cast<int>(static_cast<std::int64_t>(member.hp) * TrackWidth / member.maxHp);
            text.lowHealth = static_cast<std::int64_t>(member.hp) * 4 <= member.maxHp;
        } else {
            (void)std::swprintf(text.health, 32, L"HP --");
        }
    }
    return out;
}
} // namespace kh2coop::inject::hud
