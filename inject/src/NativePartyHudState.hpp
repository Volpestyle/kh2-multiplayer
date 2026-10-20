#pragma once
#include "kh2coop/Protocol.hpp"
#include <array>

namespace kh2coop::inject::nativehud {
inline constexpr std::uint64_t RefreshMs = 50, HeartbeatMs = 250, FreshMs = 500;
struct Sample { SeatState seat{}; std::uint64_t receivedMs{}; bool valid{}; };
struct State {
    std::uint32_t generation{};
    std::array<std::uint64_t, 3> roster{};
    std::uint8_t localSlot{0xFF};
    std::array<Sample, 3> samples{};
    void Rebase(std::uint32_t gen, const std::array<std::uint64_t, 3>& ids, std::uint8_t local) {
        if (generation == gen && roster == ids && localSlot == local) return;
        generation = gen; roster = ids; localSlot = local; samples = {};
    }
    bool Receive(const SeatState& m, std::uint64_t now) {
        if (!generation || localSlot > 2 || !roster[localSlot] || m.ownerSlot > 2 ||
            !roster[m.ownerSlot] || m.roster != roster || !m.seq || !validSeatVitals(m.vitals)) return false;
        auto& s = samples[m.ownerSlot];
        if (s.valid && m.seq <= s.seat.seq) return false;
        s = {m, now, true};
        return true;
    }
    // Connected humans reserve slots even before a fresh report arrives: never put an NPC in a
    // temporarily missing human's slot. Empty slots may retain distinct native story companions.
    std::array<std::uint8_t, 2> Owners() const {
        std::array<std::uint8_t, 2> out{0xFF, 0xFF};
        if (!generation || localSlot > 2 || !roster[localSlot]) return out;
        unsigned n = 0;
        for (std::uint8_t i = 0; i < 3; ++i) if (roster[i] && i != localSlot) out[n++] = i;
        return out;
    }
    const SeatVitals* Current(std::uint8_t owner, std::uint16_t world, std::uint16_t room, std::uint64_t now) const {
        if (owner > 2) return nullptr;
        const auto& s = samples[owner];
        if (!s.valid || now < s.receivedMs || now - s.receivedMs > FreshMs ||
            s.seat.worldId != world || s.seat.roomId != room || !(s.seat.vitals.flags & SeatVitals::Available)) return nullptr;
        return &s.seat.vitals;
    }
};
}
