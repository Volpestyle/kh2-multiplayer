#pragma once
#include "kh2coop/Codec.hpp"
#include "kh2coop/DownedState.hpp"
#include <cmath>

namespace kh2coop {
inline bool revivePosition(const AvatarState& a) {
    return std::isfinite(a.position.x) && std::isfinite(a.position.y) && std::isfinite(a.position.z);
}
inline bool reviveInRange(const AvatarState& a, const AvatarState& b) {
    if (!revivePosition(a) || !revivePosition(b)) return false;
    const double x = double(a.position.x) - b.position.x;
    const double y = double(a.position.y) - b.position.y;
    const double z = double(a.position.z) - b.position.z;
    return x*x + y*y + z*z <= double(REVIVE_RANGE)*REVIVE_RANGE;
}

// Owner-thread final gate, AFTER normal envelope/session admission. Reserve
// BEFORE calling downed::TryRevive(episode); even a native refusal is not
// retried. No native pointer, HP, timer, or game input is read/written here.
// Keep this gate for the entire game lifetime, including room/network resets.
class ReviveOwnerGate {
public:
    bool Consume(const ReviveRequest& r, const WorldScope& wire,
                 const std::string& session, const ProducerWorldContext& current,
                 const std::array<std::uint64_t,3>& connections, std::uint8_t localSlot,
                 const LocalDownedState& local, std::uint64_t now) {
        if (localSlot >= 3 || r.targetSlot != localSlot || r.requesterSlot >= 3 ||
            r.requesterSlot == localSlot || !r.seq || !r.requesterConnectionId ||
            r.requesterConnectionId != connections[r.requesterSlot] ||
            !r.targetConnectionId || r.targetConnectionId != connections[localSlot] ||
            wire.kind != WorldSourceKind::Native || session.empty() || wire.sessionId != session ||
            wire.sourceConnectionId != r.requesterConnectionId ||
            wire.targetConnectionId != r.targetConnectionId ||
            wire.targetDeliverySerial != current.deliverySerial ||
            !freshDownedState(local,current,now) || !local.downed ||
            !(r.location.epoch == local.epoch && r.location.worldId == local.worldId &&
              r.location.roomId == local.roomId && r.location.door == local.door &&
              r.location.mapProgram == local.mapProgram && r.location.battleProgram == local.battleProgram &&
              r.location.eventProgram == local.eventProgram) || r.targetEpisode != local.episode ||
            r.targetEpisode <= consumedEpisode_) return false;
        consumedEpisode_ = r.targetEpisode;
        return true;
    }
private:
    std::uint64_t consumedEpisode_{0};
};
} // namespace kh2coop
