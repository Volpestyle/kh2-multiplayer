#pragma once
#include "kh2coop/IGameBridge.hpp"

namespace kh2coop {

class CameraController {
public:
    explicit CameraController(IGameBridge& game) : game_(game) {}

    void SetOwnedSlot(SlotType slot) { ownedSlot_ = slot; }
    void SetOverrideEnabled(bool enabled) { overrideEnabled_ = enabled; }

    void Tick(const RoomState& room) {
        if (!overrideEnabled_) {
            return;
        }

        if (room.inCutscene || room.inTransition) {
            game_.RestoreVanillaCamera();
            return;
        }

        game_.WriteCameraTarget(ownedSlot_);
    }

private:
    IGameBridge& game_;
    SlotType ownedSlot_ {SlotType::Player};
    bool overrideEnabled_ {true};
};

} // namespace kh2coop
