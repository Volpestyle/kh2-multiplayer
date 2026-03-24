#pragma once

#include "kh2coop/Protocol.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace kh2coop {

class SimulationState {
public:
    SimulationState();

    void applyInput(SlotType slot, const InputFrame& input);
    void tick(float dt);

    [[nodiscard]] std::vector<ActorSnapshot> generateSnapshots() const;

    [[nodiscard]] const std::array<ActorState, 3>& actors() const { return actors_; }
    [[nodiscard]] const std::vector<EnemyState>& enemies() const { return enemies_; }
    [[nodiscard]] const RoomState& room() const { return room_; }
    [[nodiscard]] std::uint32_t snapshotId() const { return snapshotId_; }

private:
    struct ActorRuntime {
        InputFrame input {};
        float actionTimer {0.0f};
    };

    static std::size_t slotIndex(SlotType slot);
    static bool isMoving(const InputFrame& input);

    void refreshBaseAction(std::size_t index);
    void startAttack(std::size_t index);
    void startJump(std::size_t index);
    void startGuard(std::size_t index);
    void startDodge(std::size_t index);

    std::array<ActorState, 3> actors_ {};
    std::array<ActorRuntime, 3> runtime_ {};
    std::vector<EnemyState> enemies_;
    RoomState room_ {};
    std::uint32_t snapshotId_ {0};
};

} // namespace kh2coop
