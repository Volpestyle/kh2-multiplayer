#pragma once
#include "kh2coop/EventScenes.hpp"
#include "kh2coop/NetworkClient.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace kh2coop {

// Host owner-thread scheduling hints only; no snapshot/native authority, clock,
// retry deadline or request-ID generator. Callbacks must not reenter this policy.
// ObserveRoster accepts only the current NetworkClient's verified host roster.
class ReturningSeatResync {
public:
    struct Event {
        const char* action;
        const char* reason;
        std::uint8_t slot {255};
        std::uint64_t connection {};
        std::string peer;
        HostResyncContext context;
        ResyncKey key;
    };
    using Sink = std::function<void(const Event&)>;
    using Submit = std::function<bool(std::uint8_t, ResyncRequest*)>;

    explicit ReturningSeatResync(bool enabled, Sink sink = {})
        : enabled_(enabled), sink_(std::move(sink)) {}

    void ObserveRoster(const SessionState& verified, std::string_view localHostPeer) {
        if (!enabled_) return;
        std::array<const SessionActor*, 3> actors {};
        bool valid = !verified.sessionId.empty() && !localHostPeer.empty();
        for (const auto& actor : verified.actors) {
            const auto slot = static_cast<std::size_t>(actor.slot);
            if (slot >= actors.size() || actors[slot] || actor.ownerPeerId.empty() || !actor.connectionId) {
                valid = false; break;
            }
            for (const auto* previous : actors)
                if (previous && (previous->connectionId == actor.connectionId ||
                                 previous->ownerPeerId == actor.ownerPeerId)) valid = false;
            actors[slot] = &actor;
        }
        if (!valid || !actors[0] || actors[0]->ownerPeerId != localHostPeer) {
            Retire("unverified-host-roster"); return;
        }
        if (session_ != verified.sessionId || hostPeer_ != actors[0]->ownerPeerId ||
            connections_[0] != actors[0]->connectionId) {
            Retire("host-session-changed");
            session_ = verified.sessionId;
            hostPeer_ = actors[0]->ownerPeerId;
        }
        std::array<std::uint64_t, 3> next {};
        for (std::size_t i = 0; i < next.size(); ++i)
            if (actors[i]) next[i] = actors[i]->connectionId;
        // An old result cannot dispose a hint under a different full roster.
        if (next != connections_) for (auto& slot : slots_) slot.key = {};
        connections_ = next;
        for (std::size_t i = 1; i < actors.size(); ++i) {
            auto& state = slots_[i - 1];
            if (!actors[i]) { state.present = false; continue; }
            const auto& actor = *actors[i];
            if (state.peer != actor.ownerPeerId) {
                if (state.pending) Emit("retired", "seat-peer-changed", i, state);
                state = {};
                state.peer = actor.ownerPeerId;
                state.connection = actor.connectionId; // first sight is never a return
            } else if (state.connection != actor.connectionId) {
                state = {};
                state.peer = actor.ownerPeerId;
                state.connection = actor.connectionId;
                state.pending = true;
                Emit("pending", "same-peer-new-connection", i, state);
            }
            state.present = true;
        }
    }

    // eligibleField is a fresh caller-owned supported native field guard, NOT
    // membership readiness. Known event-only scenes are also refused here.
    // Failure spends this incarnation/epoch. Only a strictly newer eligible
    // host epoch may submit again; unknown context never erases that spend.
    void Pump(const std::optional<HostResyncContext>& current, bool eligibleField,
              const std::function<bool()>& busy, const Submit& submit) {
        if (!enabled_) return;
        for (std::size_t i = 1; i <= slots_.size(); ++i) {
            auto& state = slots_[i - 1];
            if (!state.pending) continue;
            const auto epoch = current ? current->room.epoch : 0;
            const char* wait = !current ? "context-unavailable"
                : !Current(*current) || !state.present ? "binding-roster-mismatch"
                : !eligibleField || eventscenes::Qualified(current->room) ? "unsupported-host-state"
                : state.key.requestId ? "transaction-pending"
                : state.attempted && epoch <= state.attemptedEpoch ? "epoch-already-attempted"
                : busy() ? "transaction-busy" : nullptr;
            if (wait) {
                if (state.waitReason != wait || state.waitEpoch != epoch) {
                    state.waitReason = wait; state.waitEpoch = epoch;
                    Emit("waiting", wait, i, state, current ? *current : HostResyncContext {});
                }
                continue;
            }
            state.waitReason = nullptr;
            state.attempted = true;
            state.attemptedEpoch = epoch;
            const auto captured = *current;
            ResyncRequest generated;
            // Spend BEFORE calling the existing host generator, including throws
            // or failed sends. This policy never constructs an authoritative key.
            const bool sent = submit(static_cast<std::uint8_t>(1u << i), &generated);
            const bool matched = generated.key.requestId && generated.key.sessionId == session_ &&
                generated.key.hostConnectionId == connections_[0] &&
                generated.targetMask == static_cast<std::uint8_t>(1u << i) &&
                generated.connections == captured.connections && sameResyncRoom(generated.room, captured.room);
            if (sent && matched) state.key = generated.key;
            Emit(sent && matched ? "submitted" : "refused",
                 sent && matched ? "host-generated-request" : "submission-failed-or-mismatched",
                 i, state, captured, generated.key);
            // Only one global transaction at a time; next Pump rechecks busy and
            // the complete binding/roster, even if a test submitter returns false.
            return;
        }
    }

    // Call only for authenticated NetworkClient results after existing cleanup.
    // Exact generated key and current target are required even for Converged.
    void Terminal(const ResyncResult& result) {
        if (!enabled_) return;
        for (std::size_t i = 1; i <= slots_.size(); ++i) {
            auto& state = slots_[i - 1];
            if (!state.pending || !state.key.requestId || state.key != result.key) continue;
            const auto key = state.key;
            state.key = {}; // duplicate/late results cannot reopen or retire a successor
            const bool converged = result.reason == ResyncResultReason::Converged &&
                state.present && connections_[i] == state.connection && result.targetCount == 1 &&
                result.targets[0].target.slot == i && result.targets[0].target.connectionId == state.connection &&
                result.targets[0].target.deliverySerial && result.targets[0].status == ResyncAckStatus::Converged;
            if (converged) state.pending = false;
            state.waitReason = nullptr;
            Emit(converged ? "retired" : "waiting", converged ? "converged" : "terminal-needs-new-epoch",
                 i, state, {}, key);
        }
    }

    void Shutdown() { Retire("shutdown"); }
    std::size_t PendingCount() const {
        std::size_t count = 0;
        for (const auto& state : slots_) count += state.pending;
        return count;
    }

private:
    struct State {
        std::string peer;
        std::uint64_t connection {};
        bool present {}, pending {}, attempted {};
        std::uint32_t attemptedEpoch {}, waitEpoch {};
        const char* waitReason {};
        ResyncKey key;
    };
    bool Current(const HostResyncContext& context) const {
        return !session_.empty() && context.binding.sessionId == session_ &&
            context.binding.selfSlot == 0 && context.binding.hostConnectionId == connections_[0] &&
            context.binding.selfConnectionId == connections_[0] && connections_[0] &&
            context.binding.deliverySerial && context.connections == connections_ && context.room.epoch;
    }
    void Retire(const char* reason) {
        for (std::size_t i = 1; i <= slots_.size(); ++i)
            if (slots_[i - 1].pending) Emit("retired", reason, i, slots_[i - 1]);
        slots_ = {}; connections_ = {}; session_.clear(); hostPeer_.clear();
    }
    void Emit(const char* action, const char* reason, std::size_t slot, const State& state,
              const HostResyncContext& context = {}, const ResyncKey& key = {}) {
        if (sink_) sink_({action, reason, static_cast<std::uint8_t>(slot), state.connection,
                          state.peer, context, key});
    }
    bool enabled_ {};
    Sink sink_;
    std::string session_, hostPeer_;
    std::array<std::uint64_t, 3> connections_ {};
    std::array<State, 2> slots_ {};
};
} // namespace kh2coop
