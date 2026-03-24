#pragma once
#include "kh2coop/Types.hpp"
#include <optional>
#include <vector>

namespace kh2coop {

class IGameBridge {
public:
    virtual ~IGameBridge() = default;

    virtual bool IsAttached() const = 0;
    virtual RoomState ReadRoomState() const = 0;
    virtual std::optional<ActorState> ReadActorState(SlotType slot) const = 0;
    virtual std::vector<EnemyState> ReadEnemyStates() const = 0;

    virtual bool WriteCameraTarget(SlotType slot) = 0;
    virtual bool RestoreVanillaCamera() = 0;

    virtual bool InjectOwnedInput(SlotType slot, const InputFrame& input) = 0;

    virtual bool ApplyReplicaActorState(const ActorState& state) = 0;
    virtual bool ApplyReplicaEnemyState(const EnemyState& state) = 0;
};

} // namespace kh2coop
