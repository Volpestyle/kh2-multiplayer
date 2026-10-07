#pragma once
#include "kh2coop/CausalDiagnostics.hpp"
#include "kh2coop/Codec.hpp"
#include "kh2coop/CausalDiagnostics.hpp"
#include "kh2coop/PeerState.hpp"
#include "kh2coop/Protocol.hpp"
#include "kh2coop/Types.hpp"
#include "kh2coop/Transport.hpp"
#include "kh2coop/DesyncCapture.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <optional>
#include <string>
#include <vector>


namespace kh2coop {

// ---------------------------------------------------------------------------
// Configuration passed to SessionHost on creation.
// ---------------------------------------------------------------------------
struct SessionConfig {
    std::uint16_t port{7782}; // default listen port
    std::string bindAddress;  // empty = all interfaces; else e.g. a 100.x Tailscale IP
    std::uint16_t protocolVersion{PROTOCOL_VERSION};
    std::uint32_t maxPeers{3};
    std::uint32_t heartbeatTimeoutMs{5000};
    std::uint32_t pendingPeerTimeoutMs{2000};
    std::string gameBuild;
    std::string contentHash;
    std::string modHash;
    std::string sessionId; // configured display label; wire incarnation is minted per host
    RuntimeMode runtimeMode{RuntimeMode::CampaignCoop};
    std::string desyncOutputRoot; // empty disables collection; executable supplies explicit local root
    std::string authenticatedHostIdentity; // Steam opt-in; empty preserves ENet behavior
};

// ---------------------------------------------------------------------------
// Callback interface — SessionHost fires these so the owner can log or react.
// ---------------------------------------------------------------------------
struct SessionCallbacks {
    CausalSink onCausalDiagnostic;
    CausalSink onHashDiagnostic; // true only after a successful sink flush
    std::function<void(const std::string& peerId, SlotType slot)> onPeerJoined;
    std::function<void(const std::string& peerId)> onPeerLeft;
    std::function<void(const std::string& peerId, const std::string& reason)> onPeerRejected;
    std::function<void(const std::string& peerId, const InputFrame& input)> onInputReceived;
    std::function<void(const std::string& msg)> onLog;
    std::function<void(const DesyncCaptureResult&)> onDesyncCaptureFinalized;
    std::function<void(const ResyncResult&)> onResyncFinalized;
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
                         SessionCallbacks callbacks = {},
                         std::unique_ptr<Transport> transport = {});
    ~SessionHost();

    void sealHashDiagnostics(const char* action = "interval") { hashDiagnostics_.seal(callbacks_.onHashDiagnostic,"relay-hash",action); }
    const CausalStream& hashDiagnostics() const { return hashDiagnostics_; }

    // Non-copyable, non-movable (owns an ENet host)
    SessionHost(const SessionHost&) = delete;
    SessionHost& operator=(const SessionHost&) = delete;

    // Start listening. Returns false on bind failure.
    bool start();
    void sealCacheDiagnostics(const char* action = "interval") noexcept {
        cacheDiagnostics_.seal(callbacks_.onCausalDiagnostic,"relay-cache-delivery",action);
    }
    const CausalStream& cacheDiagnostics() const { return cacheDiagnostics_; }

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
    [[nodiscard]] std::uint64_t relayedAvatarCount() const { return relayedAvatars_; }

    // World sync (host-authored). The host is the verified peer in
    // SlotType::Player; world messages from anyone else are dropped.
    [[nodiscard]] std::uint64_t rejectedWorldMessages() const { return rejectedWorld_; }
    [[nodiscard]] const std::optional<RoomTransition>& currentRoom() const { return room_; }
    [[nodiscard]] std::size_t manifestSize() const { return manifest_.entries.size(); }
    [[nodiscard]] std::size_t progressBytes() const { return progress_.size(); }
    [[nodiscard]] std::uint64_t desyncNoticeCount() const { return desyncNotices_; }
    [[nodiscard]] const PeerState* peerBySlot(SlotType slot) const;
    [[nodiscard]] const std::optional<DesyncCaptureResult>& lastDesyncCapture() const { return lastDesyncCapture_; }
    [[nodiscard]] const DesyncCaptureStats& desyncCaptureStats() const { return desyncCapture_->Stats(); }
    [[nodiscard]] const std::optional<DesyncSuppressionResult>& lastDesyncSuppression() const { return lastDesyncSuppression_; }

    const std::optional<ResyncResult>& lastResyncResult() const { return lastResyncResult_; }
    const std::optional<ResyncPlan>& activeResyncPlan() const { return resyncPlan_; }
    std::uint64_t resyncDeadlineMs() const { return resyncDeadline_; }
    // Production monotonic deadline pump, also usable by headless controls.
    void pumpResync(std::uint64_t nowMs);
private:
    CausalStream cacheDiagnostics_;
    // ENet event handlers
    void onConnect(TransportPeer* peer);
    void onDisconnect(TransportPeer* peer);
    void onReceive(TransportPeer* peer, const std::uint8_t* data, std::size_t size, const WorldScope* admittedScope = nullptr);

    // Peer helpers
    PeerState* findPeer(TransportPeer* peer);
    PeerState* findPeerById(const std::string& peerId);
    std::optional<SlotType> firstFreeSlot() const;
    bool isSlotTaken(SlotType slot) const;
    void rebuildSessionActors();
    void expireStalePeers(std::uint64_t nowMs);
    void removePeer(TransportPeer* peer);

    // Packet send helpers
    bool sendTo(TransportPeer* peer, const std::vector<std::uint8_t>& packet,
                bool reliable, bool cached = false);
    void broadcastToVerified(const std::vector<std::uint8_t>& packet,
                             bool reliable);

    void log(const std::string& msg);
    // Logs, reports, sends HelloReject{code, reason}, then disconnects once
    // queued packets have gone out.
    void rejectPeer(TransportPeer* peer, const std::string& peerId,
                    const std::string& reason, std::uint8_t code);

    // World sync helpers
    bool fromHost(const PeerState& ps) const;
    PeerState* hostPeer();
    void forwardToOthers(TransportPeer* sender, const std::vector<std::uint8_t>& packet,
                         bool reliable);
    void sendWorldStateTo(TransportPeer* peer);
    void clearWorldState();
    CausalStream hashDiagnostics_;
    void hashDiagnostic(const char* action, const PeerState& client, const PeerState* host,
                        std::uint8_t fields = 0, std::uint64_t previousCompare = 0, std::uint64_t compare = 0);
    void compareWithHost(PeerState& client);
    void pumpDesyncCapture();

    void sendBinding(PeerState&);
    void finishResync(ResyncResultReason, const std::string&);
    void publishResyncPlan();
    bool receiveResync(PeerState&, PacketType, ByteReader&);
    void refreshResyncCache(const ResyncSnapshot&, const std::vector<WorldEnvelope>&, std::uint64_t cut);
    bool resyncMaterialDifference(PacketType, const std::vector<std::uint8_t>&) const;
    std::optional<ResyncPlan> resyncPlan_;
    std::optional<ResyncResult> lastResyncResult_;
    ResyncAssembler resyncAssembler_;
    std::optional<ResyncBegin> resyncBegin_;
    std::optional<ResyncSnapshot> resyncSnapshot_;
    std::array<std::optional<ResyncAck>,2> resyncAcks_{};
    std::array<std::vector<WorldEnvelope>,2> resyncContinuation_{};
    std::array<std::size_t,2> resyncContinuationBytes_{};
    std::uint64_t resyncDeadline_{0}, lastResyncRequestId_{0}, lastHostSourceSerial_{0}, hostSourceCutFloor_{0};
    bool resyncMaterialChanged_{false};
    std::optional<WorldScope> forwardingScope_;
    std::uint64_t simulationSourceSerial_{0};
    bool simulationActive_{false};
    // State
    SessionConfig config_;
    SessionCallbacks callbacks_;
    SessionState session_;
    std::vector<PeerState> peers_;
    std::unique_ptr<Transport> transport_;
    bool running_{false};
    std::uint32_t nextSnapshotId_{1};
    std::uint64_t nextConnectionId_{1}; // never reset on room/session/stop boundaries
    std::uint64_t relayedAvatars_{0};

    // Cached world state for late joiners (VUH-1495).
    std::optional<RoomTransition> room_;
    std::optional<EventHold> hold_;
    EnemyManifest manifest_;
    std::map<std::uint16_t, EnemyHpEntry> enemyHp_;
    // Host connection/world lifetime, not room/cache lifetime. Cached union
    // replays carry this admitted producer sequence without restamping.
    std::uint64_t lastEnemyHpSequence_{0};
    std::set<std::uint16_t> deadEnemies_;
    std::uint64_t rejectedWorld_{0};
    std::map<std::uint32_t, std::uint8_t> progress_; // merged host flags
    std::uint32_t progressVersion_{0};
    std::uint64_t desyncNotices_{0};
    std::unique_ptr<DesyncCapture> desyncCapture_;
    std::optional<DesyncCaptureResult> lastDesyncCapture_;
    std::optional<DesyncSuppressionResult> lastDesyncSuppression_;
    std::uint64_t nextDesyncReportId_{1}, desyncComparisonSeq_{0};
    std::string desyncRelayLog_;
    std::uint64_t desyncRelayLogBytes_{0};

    // Routing proof only, never a replay/snapshot cache. Bounded per requester
    // connection and cleared at every room/host/session boundary.
    struct PendingActivation {
        ActivationRequest request;
        std::uint64_t receivedMs = 0;
    };
    std::map<TransportPeer*, std::vector<PendingActivation>> pendingActivation_;
};

} // namespace kh2coop
