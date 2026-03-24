#pragma once
#include "kh2coop/Codec.hpp"
#include "kh2coop/Protocol.hpp"
#include "kh2coop/Types.hpp"

#include <cstdint>
#include <functional>
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
                  const std::string& peerId, SlotType requestedSlot,
                  ClientCallbacks callbacks = {});
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

    // Graceful disconnect.
    void disconnect();

    [[nodiscard]] bool isConnected() const { return connected_; }

private:
    void onConnect();
    void onDisconnect();
    void onReceive(const std::uint8_t* data, std::size_t size);
    void sendPacket(const std::vector<std::uint8_t>& packet, bool reliable);
    void log(const std::string& msg);

    std::string hostAddress_;
    std::uint16_t port_;
    std::string gameBuild_;
    std::string modHash_;
    std::string peerId_;
    SlotType requestedSlot_;
    ClientCallbacks callbacks_;

    _ENetHost* enetHost_{nullptr};
    _ENetPeer* enetPeer_{nullptr};
    bool connected_{false};
};

} // namespace kh2coop
