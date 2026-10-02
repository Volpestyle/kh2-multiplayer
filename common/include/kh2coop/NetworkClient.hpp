#pragma once
#include "kh2coop/Codec.hpp"
#include "kh2coop/LinkConditioner.hpp"
#include "kh2coop/Protocol.hpp"
#include "kh2coop/Types.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

struct _ENetHost;
struct _ENetPeer;

namespace kh2coop {

// ---------------------------------------------------------------------------
// Callbacks the client fires when it receives data from the host.
// ---------------------------------------------------------------------------
struct ClientCallbacks {
    std::function<void()> onConnected;
    std::function<void()> onDisconnected;
    std::function<void(const SessionState&)> onSessionState;
    std::function<void(const ActorSnapshot&)> onActorSnapshot;
    std::function<void(const EnemySnapshot&)> onEnemySnapshot;
    std::function<void(const EventMessage&)> onEvent;
    std::function<void(const AvatarState&)> onAvatarState;
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
                  std::uint16_t protocolVersion = 2,
                  std::string peerName = {});
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
    [[nodiscard]] bool hasClockSync() const { return clockSamples_ > 0; }
    [[nodiscard]] std::uint64_t estimatedServerTimeMs() const;
    [[nodiscard]] std::uint32_t roundTripMs() const { return bestRttMs_; }

    // Test hooks: simulated network conditions per direction, and a skew
    // added to this client's clock to mimic a different machine.
    void setLinkConditions(const LinkConditions& outbound,
                           const LinkConditions& inbound);
    void setClockSkewMs(std::int64_t skewMs) { clockSkewMs_ = skewMs; }

    // Graceful disconnect.
    void disconnect();

    [[nodiscard]] bool isConnected() const { return connected_; }

private:
    void onConnect();
    void onDisconnect();
    void onReceive(const std::uint8_t* data, std::size_t size);
    void sendPacket(const std::vector<std::uint8_t>& packet, bool reliable);
    void sendNow(const std::vector<std::uint8_t>& packet, bool reliable);
    void flushConditioned();
    void onClockPong(const ClockPong& pong);
    [[nodiscard]] std::uint64_t localTimeMs() const;
    void log(const std::string& msg);

    std::string hostAddress_;
    std::uint16_t port_;
    std::string gameBuild_;
    std::string modHash_;
    std::string contentHash_;
    std::string peerId_;
    std::string peerName_;
    std::optional<SlotType> requestedSlot_;
    RuntimeMode requestedMode_{RuntimeMode::CampaignCoop};
    std::uint16_t protocolVersion_{2};
    ClientCallbacks callbacks_;

    _ENetHost* enetHost_{nullptr};
    _ENetPeer* enetPeer_{nullptr};
    bool connected_{false};

    LinkConditioner outbound_;
    LinkConditioner inbound_;

    std::int64_t clockSkewMs_{0};
    std::int64_t clockOffsetMs_{0}; // serverTime - localTime
    std::uint32_t bestRttMs_{0};
    std::uint32_t clockSamples_{0};
    std::uint64_t lastPingMs_{0};
    std::uint32_t avatarSeq_{0};
};

} // namespace kh2coop
