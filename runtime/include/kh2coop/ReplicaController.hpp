#pragma once
#include "kh2coop/IGameBridge.hpp"
#include "kh2coop/Protocol.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace kh2coop {

class ReplicaController {
public:
    explicit ReplicaController(IGameBridge& game) : game_(game) {}

    void Reset() {
        lastActorSnapshotIds_.fill(0);
        lastEnemySnapshotIds_.clear();
    }

    void ApplyActorSnapshot(const ActorSnapshot& snapshot) {
        std::size_t index = 0;
        if (!tryActorSlotIndex(snapshot.actor.slot, index)) {
            return;
        }

        auto& lastSnapshotId = lastActorSnapshotIds_[index];
        if (snapshot.snapshotId <= lastSnapshotId) {
            return;
        }

        lastSnapshotId = snapshot.snapshotId;
        game_.ApplyReplicaActorState(snapshot.actor);
    }

    void ApplyActorSnapshots(const std::vector<ActorSnapshot>& snapshots) {
        for (const auto& snapshot : snapshots) {
            ApplyActorSnapshot(snapshot);
        }
    }

    void ApplyEnemySnapshot(const EnemySnapshot& snapshot) {
        auto& lastSnapshotId = lastEnemySnapshotIds_[snapshot.enemy.netId];
        if (snapshot.snapshotId <= lastSnapshotId) {
            return;
        }

        lastSnapshotId = snapshot.snapshotId;
        game_.ApplyReplicaEnemyState(snapshot.enemy);
    }

    void ApplyEnemySnapshots(const std::vector<EnemySnapshot>& snapshots) {
        for (const auto& snapshot : snapshots) {
            ApplyEnemySnapshot(snapshot);
        }
    }

private:
    static bool tryActorSlotIndex(SlotType slot, std::size_t& index) {
        switch (slot) {
            case SlotType::Player:
                index = 0;
                return true;
            case SlotType::Friend1:
                index = 1;
                return true;
            case SlotType::Friend2:
                index = 2;
                return true;
        }

        return false;
    }

    IGameBridge& game_;
    std::array<std::uint32_t, 3> lastActorSnapshotIds_ {};
    std::unordered_map<std::uint32_t, std::uint32_t> lastEnemySnapshotIds_;
};

} // namespace kh2coop
