#pragma once
#include "kh2coop/IGameBridge.hpp"

#include <cstdint>

namespace kh2coop {

class CameraController {
public:
    explicit CameraController(IGameBridge& game) : game_(game) {}

    void SetOwnedSlot(SlotType slot) { ownedSlot_ = slot; }
    void SetOverrideEnabled(bool enabled) {
        overrideEnabled_ = enabled;
        if (!overrideEnabled_) {
            releaseOverride();
        }
    }

    void Tick(const RoomState& room) {
        const bool roomChanged = haveLastRoom_ &&
                                 (room.worldId != lastWorldId_ ||
                                  room.roomId != lastRoomId_);
        if (roomChanged) {
            // Hold one extra stable tick after a room swap so the bridge can
            // refresh Friend1/Friend2 pointers before we re-apply the override.
            roomChangeCooldownTicks_ = 2;
        }

        lastWorldId_ = room.worldId;
        lastRoomId_ = room.roomId;
        haveLastRoom_ = true;

        const bool scriptedCameraActive = room.inCutscene ||
                                          room.inTransition ||
                                          room.eventProgram != 0;
        if (!overrideEnabled_) {
            releaseOverride();
            return;
        }

        if (scriptedCameraActive || roomChanged || roomChangeCooldownTicks_ > 0) {
            releaseOverride();
            if (roomChangeCooldownTicks_ > 0) {
                --roomChangeCooldownTicks_;
            }
            return;
        }

        if (!game_.WriteCameraTarget(ownedSlot_)) {
            releaseOverride();
            return;
        }

        overrideActive_ = (ownedSlot_ != SlotType::Player);
    }

private:
    void releaseOverride() {
        game_.RestoreVanillaCamera();
        overrideActive_ = false;
    }

    IGameBridge& game_;
    SlotType ownedSlot_ {SlotType::Player};
    bool overrideEnabled_ {true};
    bool overrideActive_ {false};
    bool haveLastRoom_ {false};
    std::uint32_t lastWorldId_ {0};
    std::uint32_t lastRoomId_ {0};
    std::uint32_t roomChangeCooldownTicks_ {0};
};

} // namespace kh2coop
