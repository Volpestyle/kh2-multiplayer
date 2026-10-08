#pragma once
#include "kh2coop/NetworkClient.hpp"
#include "kh2coop/SteamBroker.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace kh2coop {

// Runtime's bounded friend rejoin scheduler. It owns no transport or game state.
// Caller supplies monotonic milliseconds, performs returned actions once, and
// forwards callbacks from the current NetworkClient transport only. NetworkClient
// must validate the retained SessionResumePin before admitted() is called.
class ClientRecovery {
public:
    enum class State { Idle, Connecting, AwaitingRoster, Admitted, RetryWait, Terminal };
    enum class Action { None, Connect, Disconnect };
    enum class Backend { Enet, Steam };
    static constexpr std::uint64_t kTransportMs = 4000;
    static constexpr std::uint64_t kRosterMs = 4000;
    static constexpr std::uint64_t kEpisodeMs = 60000;
    static constexpr std::uint64_t kMembershipStableMs = 10000;
    static constexpr std::array<std::uint64_t, 5> kRetryDelays {1000, 2000, 4000, 8000, 8000};

    explicit ClientRecovery(SlotType localSlot, Backend backend = Backend::Enet)
        : localSlot_(localSlot), transportMs_(backend == Backend::Steam ? steam::ConnectTimeoutMs : kTransportMs) {}

    // One explicit initial attempt. Calling start twice never retries/restarts.
    [[nodiscard]] Action start(std::uint64_t now) {
        advance(now);
        if (state_ != State::Idle) return Action::None;
        state_ = State::Connecting;
        enteredAt_ = now_;
        return Action::Connect;
    }
    void connected(std::uint64_t now) {
        advance(now);
        if (state_ != State::Connecting) return;
        if (elapsed(enteredAt_) >= transportMs_) { failed("initial transport deadline exceeded", true); return; }
        state_ = State::AwaitingRoster;
        enteredAt_ = now_;
    }
    void admitted(std::uint64_t now, std::uint64_t selfId) {
        advance(now);
        // Repeated valid rosters must not restart the stability timer.
        if (state_ == State::Admitted) {
            if (selfId != selfId_) terminate("admitted self identity changed", true);
            return;
        }
        if (state_ != State::AwaitingRoster) return;
        if (elapsed(enteredAt_) >= kRosterMs) { failed("initial roster deadline exceeded", true); return; }
        if (episode_ && elapsed(episodeAt_) >= kEpisodeMs) { terminate("rejoin episode exceeded 60 seconds", true); return; }
        if (selfId == 0 || (everAdmitted_ && selfId == selfId_)) {
            terminate("rejoin did not establish a fresh self identity", true);
            return;
        }
        selfId_ = selfId;
        everAdmitted_ = true;
        state_ = State::Admitted;
        enteredAt_ = now_;
        // This is verified membership, NOT native bootstrap/arrival readiness.
        // Existing episode budget survives a short successful roster flare.
    }
    void closed(std::uint64_t now, const ClientCloseInfo& info) {
        advance(now);
        if (state_ == State::Idle || state_ == State::Terminal || state_ == State::RetryWait) return;
        if (info.local) { terminate("locally terminated session", false); return; }
        switch (info.reason) {
            case DisconnectReason::TransportLost:
            case DisconnectReason::PeerIdleTimeout:
            case DisconnectReason::HandshakeTimeout:
            case DisconnectReason::SlotOccupied:
            case DisconnectReason::LobbyFull:
                failed("initial connection closed", false);
                break;
            default:
                terminate("terminal relay/session close", false);
                break;
        }
    }
    // HelloReject arrives before disconnect_later. Latch it immediately so a
    // roster deadline cannot turn an explicit terminal refusal into a retry.
    void rejected(std::uint64_t now, DisconnectReason reason) {
        if (state_ != State::Connecting && state_ != State::AwaitingRoster && state_ != State::Admitted) return;
        ClientCloseInfo info;
        info.reason = reason == DisconnectReason::TransportLost ? DisconnectReason::AdmissionRejected : reason;
        info.rawCode = static_cast<std::uint32_t>(reason);
        closed(now, info);
        disconnectPending_ = true;
    }
    void initiationFailed(std::uint64_t now) {
        advance(now);
        if (state_ == State::Connecting) failed("initial connection initiation failed", false);
    }
    [[nodiscard]] Action tick(std::uint64_t now) {
        advance(now);
        if (disconnectPending_) { disconnectPending_ = false; return Action::Disconnect; }
        if (state_ == State::Idle || state_ == State::Terminal) return Action::None;
        if (episode_ && elapsed(episodeAt_) >= kEpisodeMs) {
            const bool active = state_ == State::Connecting || state_ == State::AwaitingRoster || state_ == State::Admitted;
            terminate("rejoin episode exceeded 60 seconds", false);
            return active ? Action::Disconnect : Action::None;
        }
        if (state_ == State::Connecting && elapsed(enteredAt_) >= transportMs_) {
            failed("initial transport deadline exceeded", false);
            return Action::Disconnect;
        }
        if (state_ == State::AwaitingRoster && elapsed(enteredAt_) >= kRosterMs) {
            failed("initial roster deadline exceeded", false);
            return Action::Disconnect;
        }
        if (state_ == State::RetryWait && elapsed(enteredAt_) >= kRetryDelays[attempts_]) {
            ++attempts_;
            state_ = State::Connecting;
            enteredAt_ = now_; // late ticks issue one attempt, never catch up a burst
            return Action::Connect;
        }
        return Action::None;
    }
    void shutdown() { terminate("local shutdown", false); disconnectPending_ = false; }
    [[nodiscard]] State state() const { return state_; }
    [[nodiscard]] unsigned attempts() const { return attempts_; } // excludes initial connect
    [[nodiscard]] std::string_view terminalReason() const { return terminalReason_; }

private:
    void advance(std::uint64_t now) {
        if (now > now_) now_ = now;
        if (episode_ && state_ == State::Admitted && elapsed(enteredAt_) >= kMembershipStableMs &&
            enteredAt_ - episodeAt_ <= kEpisodeMs - kMembershipStableMs) {
            // Ten uninterrupted seconds of membership stability, whether a game
            // is attached or not. This deliberately makes no native-ready claim.
            episode_ = false;
            attempts_ = 0;
        }
    }
    [[nodiscard]] std::uint64_t elapsed(std::uint64_t since) const { return now_ - since; }
    void terminate(std::string_view reason, bool disconnect) {
        state_ = State::Terminal;
        terminalReason_ = reason;
        disconnectPending_ = disconnect;
    }
    void failed(std::string_view initialReason, bool disconnect) {
        if (!everAdmitted_) { terminate(initialReason, disconnect); return; }
        if (localSlot_ != SlotType::Friend1 && localSlot_ != SlotType::Friend2) {
            terminate("Player does not automatically rejoin", disconnect); return;
        }
        if (!episode_) { episode_ = true; episodeAt_ = now_; attempts_ = 0; }
        if (elapsed(episodeAt_) >= kEpisodeMs) {
            terminate("rejoin episode exceeded 60 seconds", disconnect); return;
        }
        if (attempts_ >= kRetryDelays.size()) {
            terminate("five rejoin attempts exhausted", disconnect); return;
        }
        state_ = State::RetryWait;
        enteredAt_ = now_;
        disconnectPending_ = disconnect;
    }
    SlotType localSlot_;
    const std::uint64_t transportMs_;
    State state_ {State::Idle};
    std::uint64_t now_ {0}, enteredAt_ {0}, episodeAt_ {0}, selfId_ {0};
    bool everAdmitted_ {false}, episode_ {false}, disconnectPending_ {false};
    unsigned attempts_ {0};
    std::string_view terminalReason_ {};
};
} // namespace kh2coop
