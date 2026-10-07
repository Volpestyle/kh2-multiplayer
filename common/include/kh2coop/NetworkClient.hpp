#pragma once
#include "kh2coop/Codec.hpp"
#include "kh2coop/CausalDiagnostics.hpp"
#include "kh2coop/LinkConditioner.hpp"
#include "kh2coop/Protocol.hpp"
#include "kh2coop/Types.hpp"
#include "kh2coop/Transport.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>


namespace kh2coop {

struct SessionResumePin {
    std::string sessionId;
    std::string hostPeerId;
    std::uint64_t hostConnectionId {0};
    std::string localPeerId;
    SlotType localSlot {SlotType::Friend1};
};

struct HostResyncContext {
    WorldBinding binding;
    RoomTransition room;
    std::array<std::uint64_t, 3> connections {};
};

struct ClientCloseInfo {
    DisconnectReason reason {DisconnectReason::TransportLost};
    std::uint32_t rawCode {0};
    bool hadReady {false};
    bool local {false}; // SessionChanged, not an ENet-reported reason
    std::optional<HelloReject> rejection;
};

// ---------------------------------------------------------------------------
// Callbacks the client fires when it receives data from the host.
// ---------------------------------------------------------------------------
struct ClientCallbacks {
    CausalSink onCausalDiagnostic;
    std::function<void()> onConnected;
    std::function<void()> onDisconnected;
    // After state reset and legacy onDisconnected, once per remote/local-pin
    // closure. Explicit disconnect/destruction remain silent.
    std::function<void(const ClientCloseInfo&)> onClosed;
    // The relay refused this client (version, mode or slot); reason is
    // human-readable. onDisconnected follows.
    std::function<void(const HelloReject&)> onRejected;
    std::function<void(const SessionState&)> onSessionState;
    std::function<void(const ActorSnapshot&)> onActorSnapshot;
    std::function<void(const EnemySnapshot&)> onEnemySnapshot;
    std::function<void(const EventMessage&)> onEvent;
    std::function<void(const AvatarRelay&)> onAvatarState;
    // World sync (from the host, via the relay)
    std::function<void(const RoomTransition&)> onRoomTransition;
    std::function<void(const EventHold&)> onEventHold;
    std::function<void(const EnemyManifest&)> onEnemyManifest;
    std::function<void(const EnemyHp&)> onEnemyHp;
    std::function<void(const EnemyDeath&)> onEnemyDeath;
    // Host only: a client's hit claim, attackerSlot stamped by the relay
    std::function<void(const HitClaim&)> onHitClaim;
    std::function<void(const ReviveRequest&)> onReviveRequest;
    std::function<void(const PartyLayout&)> onPartyLayout;
    std::function<void(const PartyReapply&)> onPartyReapply;
    std::function<void(const ProgressUpdate&)> onProgressUpdate;
    std::function<void(const DesyncNotice&)> onDesyncNotice;
    std::function<void(const DesyncCaptureRequest&)> onDesyncCaptureRequest;
    std::function<void(const ActivationRequest&)> onActivationRequest;
    std::function<void(const HostActivationPoint&)> onHostActivationPoint;
    // Any world packet (the types above), raw, for forwarding to the DLL.
    std::function<void(const std::vector<std::uint8_t>&)> onWorldPacket;
    std::function<void(const WorldBinding&)> onWorldBinding;
    std::function<void(const WorldEnvelope&)> onWorldEnvelope;
    std::function<void(const ResyncPlan&)> onResyncPlan;
    std::function<void(const ResyncBegin&, const ResyncSnapshot&)> onResyncSnapshot;
    std::function<void(const ResyncResult&)> onResyncResult;
    std::function<void(const std::string&)> onLog;
};

// ---------------------------------------------------------------------------
// NetworkClient — connects to a SessionHost and exchanges packets.
//
// Usage:
//   1. Construct with host address + port.
//   2. Call connect() — sends the version handshake.
//   3. Call tick() every frame to pump ENet events.
//   4. Call sendInput() to send input frames to the host.
//   5. Call disconnect() when done.
// ---------------------------------------------------------------------------
class NetworkClient {
public:
    NetworkClient(const std::string& hostAddress, std::uint16_t port,
                  const std::string& gameBuild, const std::string& modHash,
                  const std::string& peerId,
                  std::optional<SlotType> requestedSlot,
                  ClientCallbacks callbacks = {},
                  RuntimeMode requestedMode = RuntimeMode::CampaignCoop,
                  std::string contentHash = {},
                  std::uint16_t protocolVersion = PROTOCOL_VERSION,
                  std::string peerName = {},
                  std::unique_ptr<Transport> transport = {});
    ~NetworkClient();

    // Non-copyable
    NetworkClient(const NetworkClient&) = delete;
    NetworkClient& operator=(const NetworkClient&) = delete;

    // Attempt to connect. Returns false on immediate failure.
    bool connect();

    // Pump ENet events. Call once per frame.
    void tick(std::uint32_t timeoutMs = 0);

    // Send an input frame to the host (unreliable).
    void sendInput(const InputFrame& input);

    // Send a heartbeat to the host.
    void sendHeartbeat();

    // Send this client's avatar (unreliable). The host stamps ownerSlot and
    // relays it to the other peers. Fills seq and serverTimeMs when they are 0.
    void sendAvatar(AvatarState avatar);

    // Clock sync. tick() pings the host automatically (fast until a few
    // samples arrive, then every 2 s); the estimate uses the lowest-RTT sample.
    void sendClockPing();

    // World sync. Host-only messages are dropped by the relay when sent by a
    // non-host. EnemyHp is unreliable (periodic absolute values).
    void sendRoomTransition(const RoomTransition& m);
    void sendEventHold(const EventHold& m);
    void sendEnemyManifest(const EnemyManifest& m);
    void sendEnemyHp(const EnemyHp& m);
    void sendEnemyDeath(const EnemyDeath& m);
    void sendHitClaim(const HitClaim& m);
    void sendReviveRequest(const ReviveRequest& m);
    bool sendPartyLayout(const PartyLayout& m);
    bool requestPartyReapply(const PartyReapply& m); // host story-forced change only
    const std::optional<PartyLayout>& partyLayout() const { return partyLayout_; }
    void sendTransitionAck(const TransitionAck& m);
    void sendProgressUpdate(const ProgressUpdate& m); // host only
    void sendStateHash(const StateHash& m);
    bool sendResyncRequest(const ResyncRequest& m); // exact immutable host request
    bool requestWorldResync(std::uint8_t targetMask, ResyncRequest* generated = nullptr,
                            ResyncRequestOrigin origin = ResyncRequestOrigin::Unspecified);
    void recordResyncCallerRejection(ResyncRequestOrigin origin, std::uint8_t mask) noexcept;
    void sealRequestDiagnostics(const char* action = "interval") noexcept {
        requestDiagnostics_.seal(callbacks_.onCausalDiagnostic,"resync-request",action);
    }
    void sealWorldAdmissionDiagnostics(const char* action = "interval") noexcept {
        worldAdmissionDiagnostics_.seal(callbacks_.onCausalDiagnostic,"world-envelope-admission",action);
    }
    const CausalStream& worldAdmissionDiagnostics() const { return worldAdmissionDiagnostics_; }
    bool requestDiagnosticsEnabled() const { return static_cast<bool>(callbacks_.onCausalDiagnostic); }
    const CausalStream& requestDiagnostics() const { return requestDiagnostics_; }
    // Owner-thread observations; no request ID allocation or deadline mutation.
    bool resyncBusy() const { return requestedResync_.has_value() || resyncPlan_.has_value(); }
    bool resyncRequestPending() const { return requestedResync_.has_value(); }
    std::optional<HostResyncContext> hostResyncContext() const;
    void failWorldResync(ResyncResultReason reason, const std::string& error) { abortResync(reason,error); }
    bool sendNativeWorld(const std::vector<std::uint8_t>&, const ProducerWorldContext&, bool reliable);
    bool sendResyncCapture(const ResyncBegin&, const ResyncSnapshot&, const ProducerWorldContext&);
    bool sendResyncAck(const ResyncAck&, const ProducerWorldContext&);
    bool sendResyncFailure(const ResyncResult&, const ProducerWorldContext&);
    bool hasCurrentWorldBinding() const { return ready() && worldBinding_ && avatarLocalSlot_<3 &&
        worldBinding_->sessionId==avatarSessionId_ && worldBinding_->hostConnectionId==avatarConnections_[0] &&
        worldBinding_->selfSlot==avatarLocalSlot_ && worldBinding_->selfConnectionId==avatarConnections_[avatarLocalSlot_]; }
    bool worldReady() const { return hasCurrentWorldBinding() && !worldQuarantined_; }
    std::uint64_t deliverySerial() const { return worldBinding_ ? worldBinding_->deliverySerial : 0; }
    const std::optional<WorldBinding>& worldBinding() const { return worldBinding_; }
    const std::optional<ResyncPlan>& pendingResync() const { return resyncPlan_; }
    // Explicit packet-construction factory for typed legacy/headless callers.
    // Runtime native transport MUST use sendNativeWorld with observed context.
    ProducerWorldContext makeTestingWorldContext();
    // True means submitted to ENet or accepted by the configured delay queue,
    // not relay receipt. A later delayed-send failure is logged explicitly.
    bool sendDesyncArtifactChunk(const DesyncArtifactChunk&);
    bool sendDesyncCaptureDone(const DesyncCaptureDone&);
    void sendActivationRequest(const ActivationRequest& m); // ephemeral client -> host
    void sendHostActivationPoint(const HostActivationPoint& m); // host -> requester

    // An already-encoded packet (e.g. from the DLL's WorldBridge).
    void sendRawPacket(const std::vector<std::uint8_t>& packet, bool reliable);
    [[nodiscard]] bool hasClockSync() const { return clockSamples_ > 0; }
    [[nodiscard]] std::uint64_t estimatedServerTimeMs() const;
    [[nodiscard]] std::uint32_t roundTripMs() const { return bestRttMs_; }

    // Link quality for logs and the overlay (VUH-1493). rttMs/rttVarMs and
    // lossPermille are ENet's transport estimates (lossPermille only reflects
    // reliable traffic). appRttMs is the latest clock-ping round trip, which
    // also includes any LinkConditioner delay, so it's the one that moves in
    // impaired tests. valid is false until connected.
    struct LinkStats {
        bool valid {false};
        std::uint32_t rttMs {0};
        std::uint32_t rttVarMs {0};
        std::uint32_t lossPermille {0};
        std::uint32_t appRttMs {0};
        // Loss players actually see: gaps in received avatar sequence numbers,
        // worst remote owner over its last ~300-packet window (~5 s at 60 Hz).
        // Includes LinkConditioner drops. kNoAvatarLoss until a window closes.
        std::uint32_t avatarLossPermille {kNoAvatarLoss};
    };
    static constexpr std::uint32_t kNoAvatarLoss = 0xFFFFFFFFu;
    [[nodiscard]] LinkStats linkStats() const;

    // Test hooks: simulated network conditions per direction, and a skew
    // added to this client's clock to mimic a different machine.
    void setLinkConditions(const LinkConditions& outbound,
                           const LinkConditions& inbound);
    void setClockSkewMs(std::int64_t skewMs) { clockSkewMs_ = skewMs; }

    // Graceful disconnect.
    void disconnect();

    [[nodiscard]] bool isConnected() const { return connected_; }
    [[nodiscard]] bool ready() const { return connected_ && avatarRosterValid_; }
    // Only between attempts; a resume pin may name Friend1/Friend2, never host.
    bool SetResumePin(std::optional<SessionResumePin> pin);

private:
    CausalStream requestDiagnostics_;
    CausalStream worldAdmissionDiagnostics_;
    std::uint64_t originalDiagnosticRequestId_{};
    RequestSubmissionObservation originalDiagnosticDeadline_;
    bool sendResyncRequestObserved(const ResyncRequest&, RequestSubmissionObservation&);
    void recordResyncRequest(ResyncRequestOrigin, std::uint8_t, const ResyncRequest*,
                            const char*, const RequestSubmissionObservation&) noexcept;
    void onConnect();
    void onDisconnect(std::uint32_t code = 0, bool local = false);
    void onReceive(const std::uint8_t* data, std::size_t size, bool reliable, const WorldScope* admittedScope = nullptr,
                   const std::uint8_t* admittedWire = nullptr, std::size_t admittedWireSize = 0);
    bool sendPacket(const std::vector<std::uint8_t>& packet, bool reliable);
    bool sendNow(const std::vector<std::uint8_t>& packet, bool reliable);
    void flushConditioned();
    void onClockPong(const ClockPong& pong);
    void resetTransportState();
    bool updateAvatarRoster(const SessionState& session);
    bool admitAvatar(const AvatarRelay& relay);
    bool admitDesync(const DesyncKey&, std::uint64_t connectionId) const;
    void dispatchPendingDesync();
    bool matchesResumePin(const SessionState& session) const;
    void closeChangedSession();
    [[nodiscard]] std::uint64_t localTimeMs() const;
    void log(const std::string& msg);

    bool validWorldContext(const ProducerWorldContext&) const;
    void abortResync(ResyncResultReason, const std::string&);
    std::optional<WorldBinding> worldBinding_;
    std::optional<ResyncPlan> resyncPlan_;
    std::optional<ResyncRequest> requestedResync_;
    std::optional<ResyncBegin> completedResyncBegin_;
    std::optional<RoomTransition> hostRoom_;
    ResyncAssembler resyncAssembler_;
    bool worldQuarantined_{false};
    std::array<std::uint64_t,3> remoteDeliverySerials_{1,1,1};
    std::array<std::uint64_t,3> deliveryConnections_{};
    std::uint64_t worldSourceFloor_{0}, testingSourceSerial_{0}, nextResyncRequest_{1};
    std::uint64_t resyncDeadline_{0}, lastResyncPlanId_{0};
    std::string hostAddress_;
    std::uint16_t port_;
    std::string gameBuild_;
    std::string modHash_;
    std::string contentHash_;
    std::string peerId_;
    std::string peerName_;
    std::optional<SlotType> requestedSlot_;
    RuntimeMode requestedMode_{RuntimeMode::CampaignCoop};
    std::uint16_t protocolVersion_{PROTOCOL_VERSION};
    ClientCallbacks callbacks_;

    std::unique_ptr<Transport> transport_;
    TransportPeer* transportPeer_{nullptr};
    bool connected_{false};
    bool attemptActive_{false};
    bool hadReady_{false};
    std::optional<HelloReject> rejection_;
    std::optional<SessionResumePin> resumePin_;
    std::uint64_t transportGeneration_{0};
    bool avatarRosterValid_{false};
    std::string avatarSessionId_;
    std::uint8_t avatarLocalSlot_{0xFF};
    std::uint64_t avatarConnections_[3]{};
    std::uint32_t avatarLastSeq_[3]{};
    // Independent of transient roster validity: only a real transport or a
    // newly verified host/world identity change may retire the HP floor.
    std::string enemyHpSessionId_;
    std::uint64_t enemyHpHostConnectionId_{0}, enemyHpSequence_{0};
    std::uint32_t enemyHpRoomEpoch_{0};
    std::optional<DesyncCaptureRequest> desyncRequest_;
    std::uint64_t lastDesyncReportId_{0};
    std::uint64_t lastObservedDesyncReportId_{0}; // expiry/rejection cannot renew an observed request
    std::uint64_t desyncDeadlineMs_{0};
    std::optional<DesyncCaptureRequest> pendingDesyncRequest_;
    std::uint64_t pendingDesyncDeadlineMs_{0};

    LinkConditioner outbound_;
    LinkConditioner inbound_;

    std::int64_t clockSkewMs_{0};
    std::int64_t clockOffsetMs_{0}; // serverTime - localTime
    std::uint32_t bestRttMs_{0};
    std::uint32_t lastRttMs_{0};
    // Avatar loss windows per owner slot (VUH-1493).
    struct AvatarLossWindow {
        bool started {false};
        std::uint32_t firstSeq {0};
        std::uint32_t maxSeq {0};
        std::uint32_t received {0};
        std::uint32_t lastLossPermille {kNoAvatarLoss};
    };
    AvatarLossWindow avatarLoss_[3];
    void noteAvatarSeq(std::uint8_t ownerSlot, std::uint32_t seq);
    std::uint32_t clockSamples_{0};
    std::uint64_t lastPingMs_{0};
    std::uint32_t avatarSeq_{0};
    std::optional<PartyLayout> partyLayout_;
    std::uint64_t partyVersion_{0};
    AvatarState reviveLocal_{};
    std::uint64_t reviveLocalMs_{0}, receivedReviveEpisode_{0};
};

} // namespace kh2coop
