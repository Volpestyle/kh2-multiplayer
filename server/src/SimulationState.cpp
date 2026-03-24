#include "kh2coop/SimulationState.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace kh2coop {

namespace {

constexpr std::array<SlotType, 3> kSlots {
    SlotType::Player,
    SlotType::Friend1,
    SlotType::Friend2,
};

constexpr float kDeadZone = 0.1f;
constexpr float kMoveSpeed = 5.0f;
constexpr float kGravity = 25.0f;
constexpr float kJumpVelocity = 7.5f;

constexpr float kAttackDuration = 0.30f;
constexpr float kJumpDuration = 0.25f;
constexpr float kDodgeDuration = 0.20f;

constexpr std::uint32_t kIdleMotion = 0;
constexpr std::uint32_t kMoveMotion = 1;
constexpr std::uint32_t kJumpMotion = 2;
constexpr std::uint32_t kAttackMotion = 3;
constexpr std::uint32_t kGuardMotion = 4;
constexpr std::uint32_t kDodgeMotion = 5;

} // namespace

SimulationState::SimulationState() {
    for (std::size_t i = 0; i < actors_.size(); ++i) {
        auto& actor = actors_[i];
        actor.actorId = static_cast<std::uint32_t>(i);
        actor.slot = kSlots[i];
        actor.action = ActionState::Idle;
        actor.motionId = kIdleMotion;
        actor.hp = 100;
        actor.mp = 100;
    }
}

void SimulationState::applyInput(SlotType slot, const InputFrame& input) {
    const std::size_t index = slotIndex(slot);
    auto& actor = actors_[index];
    auto& runtime = runtime_[index];
    const InputButtons previousButtons = runtime.input.buttons;

    runtime.input = input;
    actor.targetId = input.requestedTargetId;

    if (input.buttons.attack && !previousButtons.attack) {
        startAttack(index);
        return;
    }

    if (input.buttons.jump && !previousButtons.jump && !actor.airborne) {
        startJump(index);
        return;
    }

    if (input.buttons.dodge && !previousButtons.dodge) {
        startDodge(index);
        return;
    }

    if (input.buttons.guard) {
        startGuard(index);
        return;
    }

    if (runtime.actionTimer <= 0.0f && !actor.airborne) {
        refreshBaseAction(index);
    }
}

void SimulationState::tick(float dt) {
    const float step = std::max(dt, 0.0f);

    for (std::size_t i = 0; i < actors_.size(); ++i) {
        auto& actor = actors_[i];
        auto& runtime = runtime_[i];

        const float moveX = std::clamp(runtime.input.leftStickX, -1.0f, 1.0f);
        const float moveZ = std::clamp(runtime.input.leftStickY, -1.0f, 1.0f);

        actor.velocity.x = std::fabs(moveX) >= kDeadZone ? moveX * kMoveSpeed : 0.0f;
        actor.velocity.z = std::fabs(moveZ) >= kDeadZone ? moveZ * kMoveSpeed : 0.0f;

        actor.position.x += actor.velocity.x * step;
        actor.position.z += actor.velocity.z * step;

        if (actor.airborne) {
            actor.velocity.y -= kGravity * step;
            actor.position.y += actor.velocity.y * step;

            if (actor.position.y <= 0.0f) {
                actor.position.y = 0.0f;
                actor.velocity.y = 0.0f;
                actor.airborne = false;
            }
        } else {
            actor.velocity.y = 0.0f;
        }

        if (runtime.actionTimer > 0.0f) {
            runtime.actionTimer = std::max(0.0f, runtime.actionTimer - step);
            if (runtime.actionTimer <= 0.0f) {
                actor.invuln = false;
                if (actor.action == ActionState::Attack) {
                    actor.comboStep = 0;
                }
            }
        }

        if (runtime.actionTimer <= 0.0f) {
            if (runtime.input.buttons.guard && !actor.airborne) {
                startGuard(i);
            } else if (!actor.airborne) {
                refreshBaseAction(i);
            } else {
                actor.action = ActionState::Jump;
                actor.motionId = kJumpMotion;
            }
        }
    }

    ++snapshotId_;
}

std::vector<ActorSnapshot> SimulationState::generateSnapshots() const {
    std::vector<ActorSnapshot> snapshots;
    snapshots.reserve(actors_.size());

    for (const auto& actor : actors_) {
        ActorSnapshot snapshot;
        snapshot.snapshotId = snapshotId_;
        snapshot.actor = actor;
        snapshots.push_back(snapshot);
    }

    return snapshots;
}

std::size_t SimulationState::slotIndex(SlotType slot) {
    return static_cast<std::size_t>(slot);
}

bool SimulationState::isMoving(const InputFrame& input) {
    return std::fabs(input.leftStickX) >= kDeadZone ||
           std::fabs(input.leftStickY) >= kDeadZone;
}

void SimulationState::refreshBaseAction(std::size_t index) {
    auto& actor = actors_[index];
    const auto& input = runtime_[index].input;

    if (isMoving(input)) {
        actor.action = ActionState::Move;
        actor.motionId = kMoveMotion;
    } else {
        actor.action = ActionState::Idle;
        actor.motionId = kIdleMotion;
    }
}

void SimulationState::startAttack(std::size_t index) {
    auto& actor = actors_[index];
    auto& runtime = runtime_[index];

    actor.action = ActionState::Attack;
    actor.motionId = kAttackMotion;
    actor.comboStep = (actor.comboStep % 3U) + 1U;
    actor.invuln = false;
    runtime.actionTimer = kAttackDuration;
}

void SimulationState::startJump(std::size_t index) {
    auto& actor = actors_[index];
    auto& runtime = runtime_[index];

    actor.action = ActionState::Jump;
    actor.motionId = kJumpMotion;
    actor.airborne = true;
    actor.velocity.y = kJumpVelocity;
    runtime.actionTimer = kJumpDuration;
}

void SimulationState::startGuard(std::size_t index) {
    auto& actor = actors_[index];
    auto& runtime = runtime_[index];

    actor.action = ActionState::Guard;
    actor.motionId = kGuardMotion;
    actor.invuln = false;
    runtime.actionTimer = 0.0f;
}

void SimulationState::startDodge(std::size_t index) {
    auto& actor = actors_[index];
    auto& runtime = runtime_[index];

    actor.action = ActionState::Dodge;
    actor.motionId = kDodgeMotion;
    actor.invuln = true;
    runtime.actionTimer = kDodgeDuration;
}

} // namespace kh2coop
