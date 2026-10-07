#pragma once
// Deliberately independent of Protocol.hpp: legacy native HitChannel also
// names a HitClaim. AvatarBridge must remain safe to include beside it.
#include "kh2coop/Types.hpp"
#include "kh2coop/WorldContext.hpp"
namespace kh2coop {
inline constexpr std::uint64_t DOWNED_STATE_MAX_AGE_MS = 1000;
// Single native owner publishes every checked frame, including while alive.
// Default/unavailable/retired state cannot grant revive authority. Episode must
// strictly increase at each new downing; never recycle it on room/reset/revive.
struct LocalDownedState {
    ProducerWorldContext context{};
    std::uint32_t epoch{0};
    std::uint16_t worldId{0}, roomId{0}, door{0}, mapProgram{0}, battleProgram{0}, eventProgram{0};
    std::uint64_t episode{0}, sampledAtMs{0}; // GetTickCount64 on this machine
    bool downed{false};
};
inline bool freshDownedState(const LocalDownedState& s, const ProducerWorldContext& c,
                            std::uint64_t now) {
    return c.generation && c.deliverySerial && s.context.generation == c.generation &&
        s.context.deliverySerial == c.deliverySerial && s.epoch && s.sampledAtMs &&
        now >= s.sampledAtMs && now - s.sampledAtMs <= DOWNED_STATE_MAX_AGE_MS &&
        (!s.downed || s.episode);
}
inline void projectLocalDowned(AvatarState& a, const LocalDownedState& s,
                              const ProducerWorldContext& c, std::uint64_t now) {
    a.flags &= static_cast<std::uint8_t>(~AvatarDowned);
    a.downedEpoch = 0; a.downedEpisode = 0; a.downedDelivery = 0;
    if (!freshDownedState(s, c, now) || a.worldId != s.worldId ||
        a.roomId != s.roomId) return;
    a.downedEpoch = s.epoch;
    a.downedEpisode = s.episode;
    a.downedDelivery = s.context.deliverySerial;
    if (s.downed) a.flags |= AvatarDowned;
}
} // namespace kh2coop
