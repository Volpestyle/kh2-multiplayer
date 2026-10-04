#pragma once
#include "kh2coop/NetworkClient.hpp"
#include <array>
#include <functional>
#include <optional>
#include <sstream>
#include <utility>

namespace kh2coop {

// Runtime ownership can retire before NetworkClient transport/binding state.
// This owner-thread gate is checked at every immediate or drained submission.
inline bool automaticResyncRuntimeCurrent(bool running, bool admitted,
                                         bool membershipInvalidated, const NetworkClient& network) {
    return running && admitted && !membershipInvalidated && network.worldReady();
}

// Host owner-thread policy only. A notice is a hint, never native creation
// authority. Two pending slots; no timer, retry loop or resync deadline owner.
class AutomaticResyncNotice {
public:
    struct Candidate {
        std::uint64_t receipt {}, receivedMs {};
        DesyncNotice notice;
        HostResyncContext context;
    };
    struct Event {
        const char* action;
        const char* reason;
        Candidate candidate;
        std::uint64_t relatedReceipt {};
        ResyncKey key;
        bool busy {};
    };
    using Sink = std::function<void(const Event&)>;
    using Submit = std::function<bool(std::uint8_t, ResyncRequest*)>;

    explicit AutomaticResyncNotice(bool enabled, Sink sink)
        : enabled_(enabled), sink_(std::move(sink)) {}

    void Notice(const DesyncNotice& notice, const std::optional<HostResyncContext>& current,
                bool busy, std::uint64_t now, const Submit& submit) {
        if (!enabled_) return;
        if (nextReceipt_ == UINT64_MAX) {
            Emit("discard", "receipt-exhausted", {}, 0, {}, busy);
            return;
        }
        Candidate candidate; candidate.receipt = ++nextReceipt_;
        candidate.receivedMs = now; candidate.notice = notice;
        if (current) candidate.context = *current;
        Emit("received", "notice-hint", candidate, 0, {}, busy);
        const auto slot = static_cast<std::size_t>(notice.slot);
        if (!current || !ValidHost(*current) || slot < 1 || slot > 2 ||
            !current->connections[slot] || !notice.epoch || notice.epoch != current->room.epoch ||
            !(notice.fields & DesyncEnemies) || (notice.fields & ~std::uint8_t{7})) {
            Emit("discard", "not-current-host-enemies-hint", candidate, 0, {}, busy);
            return;
        }
        Synchronize(current);
        auto& state = slots_[slot - 1];
        if (!SameScope(state, candidate)) {
            // An actual captured incarnation/epoch change starts a new bounded
            // dedupe scope. Temporary unavailability alone never resets it.
            if (state.pending) Emit("discard", "scope-changed", *state.pending);
            state = {};
            state.session = current->binding.sessionId;
            state.host = current->binding.hostConnectionId;
            state.connection = current->connections[slot];
            state.epoch = notice.epoch;
        }
        const auto fields = notice.fields;
        if (state.attempted[fields]) {
            Emit("duplicate", "already-attempted", candidate, state.attempted[fields], state.keys[fields], busy);
            return;
        }
        if (state.pending && state.pending->notice.fields == fields) {
            Emit("coalesced", "same-pending-key", candidate, state.pending->receipt, {}, busy);
            return;
        }
        if (state.pending) Emit("superseded", "newer-field-set", *state.pending, candidate.receipt, {}, busy);
        state.pending = candidate;
        if (busy) Emit("pending", "transaction-busy", candidate, 0, {}, true);
        else Send(state, submit);
    }

    // Call after the network result callback has finished its existing native
    // cleanup. Drain is deferred until outside NetworkClient::tick callbacks.
    void Terminal(const ResyncResult& result) {
        if (!enabled_) return;
        Candidate candidate;
        for (const auto& state : slots_) for (std::size_t i = 0; i < state.keys.size(); ++i)
            if (state.attempted[i] && state.keys[i] == result.key) candidate.receipt = state.attempted[i];
        Emit("terminal", "transaction-finished", candidate, 0, result.key);
        drain_ = true;
    }

    void Pump(const std::optional<HostResyncContext>& current, const std::function<bool()>& busy,
              const Submit& submit) {
        if (!enabled_) return;
        Synchronize(current);
        if (!drain_) return;
        drain_ = false;
        for (auto& state : slots_) {
            if (!state.pending) continue;
            // Another slot can consume the single global transaction first.
            // Its eventual terminal callback will drain the remaining slot.
            if (busy()) break;
            Emit("drain", "terminal-revalidated", *state.pending);
            Send(state, submit);
        }
    }

    void Shutdown() {
        for (auto& state : slots_) {
            if (state.pending) Emit("discard", "shutdown", *state.pending);
            state.pending.reset();
        }
        drain_ = false;
    }
    std::size_t PendingCount() const { return (slots_[0].pending ? 1u : 0u) + (slots_[1].pending ? 1u : 0u); }

private:
    struct Slot {
        std::string session;
        std::uint64_t host {}, connection {};
        std::uint32_t epoch {};
        std::array<std::uint64_t, 8> attempted {};
        std::array<ResyncKey, 8> keys {};
        std::optional<Candidate> pending;
    };
    static bool ValidHost(const HostResyncContext& context) {
        return context.binding.selfSlot == 0 && !context.binding.sessionId.empty() &&
            context.binding.hostConnectionId && context.binding.deliverySerial &&
            context.binding.selfConnectionId == context.binding.hostConnectionId &&
            context.connections[0] == context.binding.hostConnectionId && context.room.epoch;
    }
    static bool SameScope(const Slot& state, const Candidate& candidate) {
        return state.session == candidate.context.binding.sessionId &&
            state.host == candidate.context.binding.hostConnectionId &&
            state.connection == candidate.context.connections[static_cast<std::size_t>(candidate.notice.slot)] &&
            state.epoch == candidate.notice.epoch;
    }
    static bool SameContext(const HostResyncContext& a, const HostResyncContext& b) {
        return ValidHost(b) && a.binding.sessionId == b.binding.sessionId &&
            a.binding.hostConnectionId == b.binding.hostConnectionId &&
            a.binding.selfConnectionId == b.binding.selfConnectionId &&
            a.binding.deliverySerial == b.binding.deliverySerial &&
            a.connections == b.connections && sameResyncRoom(a.room, b.room);
    }
    void Synchronize(const std::optional<HostResyncContext>& current) {
        for (auto& state : slots_) if (state.pending &&
            (!current || !SameContext(state.pending->context, *current))) {
            Emit("discard", "captured-context-changed", *state.pending);
            state.pending.reset();
        }
    }
    void Send(Slot& state, const Submit& submit) {
        const auto candidate = *state.pending;
        state.pending.reset();
        const auto fields = candidate.notice.fields;
        // Failed submission is also terminal: repeated notices must not become
        // an unbounded retry mechanism. No stored candidate owns a deadline.
        state.attempted[fields] = candidate.receipt;
        ResyncRequest generated;
        const bool sent = submit(static_cast<std::uint8_t>(1u << static_cast<unsigned>(candidate.notice.slot)), &generated);
        state.keys[fields] = generated.key;
        Emit(sent ? "submitted" : "discard", sent ? "automatic-notice" : "submission-failed",
             candidate, 0, generated.key);
    }
    void Emit(const char* action, const char* reason, const Candidate& candidate,
              std::uint64_t related = 0, const ResyncKey& key = {}, bool busy = false) {
        if (sink_) sink_({action, reason, candidate, related, key, busy});
    }
    bool enabled_ {}, drain_ {};
    std::uint64_t nextReceipt_ {};
    std::array<Slot, 2> slots_ {};
    Sink sink_;
};

inline std::string formatAutomaticResyncNotice(const AutomaticResyncNotice::Event& event,
                                               std::uint64_t sequence, std::uint64_t now,
                                               std::uint32_t generation) {
    const auto& candidate = event.candidate;
    const auto& context = candidate.context;
    const auto slot = static_cast<unsigned>(candidate.notice.slot);
    const auto& room = context.room;
    std::ostringstream out;
    out << "[automatic-resync] schema=1 seq=" << sequence << " observationMs=" << now
        << " action=" << event.action << " reason=" << event.reason
        << " receipt=" << candidate.receipt << " receivedMs=" << candidate.receivedMs
        << " related=" << event.relatedReceipt << " busy=" << event.busy
        << " slot=" << slot << " epoch=" << candidate.notice.epoch
        << " fields=" << static_cast<unsigned>(candidate.notice.fields)
        << " contextAvailable=" << !context.binding.sessionId.empty()
        << " session=" << (context.binding.sessionId.empty() ? "-" : context.binding.sessionId)
        << " host=" << context.binding.hostConnectionId
        << " connection=" << (slot < context.connections.size() ? context.connections[slot] : 0)
        << " delivery=" << context.binding.deliverySerial
        << " roster0=" << context.connections[0] << " roster1=" << context.connections[1]
        << " roster2=" << context.connections[2]
        << " roomEpoch=" << room.epoch << " world=" << room.worldId << " room=" << room.roomId
        << " door=" << static_cast<unsigned>(room.door) << " map=" << room.mapProgram
        << " battle=" << room.battleProgram << " event=" << room.eventProgram
        << " requestSession=" << (event.key.sessionId.empty() ? "-" : event.key.sessionId)
        << " requestHost=" << event.key.hostConnectionId << " request=" << event.key.requestId
        << " producerGeneration=" << generation << " dropped=0";
    return out.str();
}
// Read-only interval snapshot: caller supplies its existing cadence/highwaters.
// Formatting cannot drain candidates, submit a request or change a deadline.
inline std::string formatAutomaticResyncInterval(const AutomaticResyncNotice& policy,
    const NetworkClient& network, std::uint64_t sealSequence, std::uint64_t now,
    std::uint64_t automaticHighWater, std::uint64_t worldCauseHighWater,
    std::uint32_t generation, const std::array<std::uint64_t, 3>& roster) {
    const auto& binding = network.worldBinding();
    std::ostringstream out;
    out << "[automatic-resync-seal] schema=1 action=interval sealSeq=" << sealSequence
        << " observationMs=" << now << " autoHighWater=" << automaticHighWater
        << " worldCauseHighWater=" << worldCauseHighWater << " pending=" << policy.PendingCount()
        << " requested=" << network.resyncRequestPending() << " planned=" << network.pendingResync().has_value()
        << " busy=" << network.resyncBusy() << " bindingAvailable=" << binding.has_value()
        << " generation=" << generation << " delivery=" << (binding ? binding->deliverySerial : 0)
        << " slot=" << (binding ? static_cast<unsigned>(binding->selfSlot) : 255)
        << " session=" << (binding ? binding->sessionId : "-")
        << " host=" << (binding ? binding->hostConnectionId : 0)
        << " self=" << (binding ? binding->selfConnectionId : 0)
        << " roster0=" << roster[0] << " roster1=" << roster[1] << " roster2=" << roster[2]
        << " dropped=0";
    return out.str();
}
} // namespace kh2coop
