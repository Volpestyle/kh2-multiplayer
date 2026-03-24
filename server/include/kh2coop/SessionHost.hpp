#pragma once
#include "kh2coop/Codec.hpp"
#include "kh2coop/PeerState.hpp"
#include "kh2coop/Protocol.hpp"
#include "kh2coop/Types.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct _ENetHost; // forward-declare

namespace kh2coop {

// ---------------------------------------------------------------------------
// Configuration passed to SessionHost on creation.
// ---------------------------------------------------------------------------
struct SessionConfig {
    std::uint16_t port{7782}; // default listen port
    std::uint32_t maxPeers{3};
    std::string gameBuild;
    std::string modHash;
    std::string sessionId;
};

// ---------------------------------------------------------------------------
// Callback interface — SessionHost fires these so the owner can log or react.
// ---------------------------------------------------------------------------
struct SessionCallbacks {
    std::function<void(const std::string& peerId, SlotType slot)> onPeerJoined;
    std::function<void(const std::string& peerId)> onPeerLeft;
    std::function<void(const std::string& peerId, const std::string& reason)> onPeerRejected;
    std::function<void(const std::string& peerId, const InputFrame& input)> onInputReceived;
    std::function<void(const std::string& msg)> onLog;
};

// ---------------------------------------------------------------------------
// SessionHost — host-authoritative session manager.
//
// Responsibilities:
//   - listen for ENet connections
//   - version-gate peers (gameBuild + modHash must match)
//   - assign slots (Player / Friend1 / Friend2)
//   - receive InputFrames from clients
//   - broadcast snapshots + events to all verified peers
//   - track heartbeats and disconnect stale peers
// ---------------------------------------------------------------------------
class SessionHost {
public:
    explicit SessionHost(const SessionConfig& config,
                         SessionCallbacks callbacks = {});
    ~SessionHost();

    // Non-copyable, non-movable (owns an ENet host)
    SessionHost(const SessionHost&) = delete;
    SessionHost& operator=(const SessionHost&) = delete;

    // Start listening. Returns false on bind failure.
    bool start();

    // Process network events for up to `timeoutMs` milliseconds.
    // Call this once per server tick.
    void tick(std::uint32_t timeoutMs = 0);

    // Stop listening, disconnect all peers.
    void stop();

    // --- Outbound: host -> clients ---

    // Broadcast current session state to all verified peers.
    void broadcastSessionState();

    // Broadcast actor snapshots (one per slot) to all verified peers.
    void broadcastActorSnapshots(const std::vector<ActorSnapshot>& snapshots);

    // Broadcast enemy snapshots to all verified peers.
    void broadcastEnemySnapshots(const std::vector<EnemySnapshot>& snapshots);

    // Broadcast a reliable event to all verified peers.
    void broadcastEvent(const EventMessage& event);

    // --- State queries ---

    [[nodiscard]] const SessionState& sessionState() const { return session_; }
    [[nodiscard]] const std::vector<PeerState>& peers() const { return peers_; }
    [[nodiscard]] std::size_t verifiedPeerCount() const;
    [[nodiscard]] bool isRunning() const { return running_; }

private:
    // ENet event handlers
    void onConnect(_ENetPeer* peer);
    void onDisconnect(_ENetPeer* peer);
    void onReceive(_ENetPeer* peer, const std::uint8_t* data, std::size_t size);

    // Peer helpers
    PeerState* findPeer(_ENetPeer* peer);
    PeerState* findPeerById(const std::string& peerId);
    bool isSlotTaken(SlotType slot) const;
    void removePeer(_ENetPeer* peer);

    // Packet send helpers
    void sendTo(_ENetPeer* peer, const std::vector<std::uint8_t>& packet,
                bool reliable);
    void broadcastToVerified(const std::vector<std::uint8_t>& packet,
                             bool reliable);

    void log(const std::string& msg);

    // State
    SessionConfig config_;
    SessionCallbacks callbacks_;
    SessionState session_;
    std::vector<PeerState> peers_;
    _ENetHost* enetHost_{nullptr};
    bool running_{false};
    std::uint32_t nextSnapshotId_{1};
};

} // namespace kh2coop
