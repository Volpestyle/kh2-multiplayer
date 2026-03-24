#pragma once
#include "kh2coop/IGameBridge.hpp"
#include "kh2coop/Protocol.hpp"
#include <vector>

namespace kh2coop {

class ReplicaController {
public:
    explicit ReplicaController(IGameBridge& game) : game_(game) {}

    void ApplyActorSnapshots(const std::vector<ActorSnapshot>& snapshots) {
        for (const auto& snapshot : snapshots) {
            game_.ApplyReplicaActorState(snapshot.actor);
        }
    }

    void ApplyEnemySnapshots(const std::vector<EnemySnapshot>& snapshots) {
        for (const auto& snapshot : snapshots) {
            game_.ApplyReplicaEnemyState(snapshot.enemy);
        }
    }

private:
    IGameBridge& game_;
};

} // namespace kh2coop
