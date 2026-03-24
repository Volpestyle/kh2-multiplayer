#include "kh2coop/NetworkClient.hpp"

#include <enet/enet.h>

namespace kh2coop {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

NetworkClient::NetworkClient(const std::string& hostAddress, std::uint16_t port,
                             const std::string& gameBuild,
                             const std::string& modHash,
                             const std::string& peerId,
                             SlotType requestedSlot,
                             ClientCallbacks callbacks)
    : hostAddress_(hostAddress),
      port_(port),
      gameBuild_(gameBuild),
      modHash_(modHash),
      peerId_(peerId),
      requestedSlot_(requestedSlot),
      callbacks_(std::move(callbacks)) {}

NetworkClient::~NetworkClient() { disconnect(); }

// ---------------------------------------------------------------------------
// Connect
// ---------------------------------------------------------------------------

bool NetworkClient::connect() {
    if (connected_) return true;

    enetHost_ = enet_host_create(nullptr /* client, no bind */, 1 /* one peer */,
                                 2 /* channels */, 0, 0);
    if (!enetHost_) {
        log("Failed to create ENet client host.");
        return false;
    }

    ENetAddress address;
    enet_address_set_host(&address, hostAddress_.c_str());
    address.port = port_;

    enetPeer_ = enet_host_connect(enetHost_, &address, 2 /* channels */, 0);
    if (!enetPeer_) {
        log("Failed to initiate connection to " + hostAddress_ + ":" +
            std::to_string(port_));
        enet_host_destroy(enetHost_);
        enetHost_ = nullptr;
        return false;
    }

    log("Connecting to " + hostAddress_ + ":" + std::to_string(port_) + "...");
    return true;
}

// ---------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------

void NetworkClient::tick(std::uint32_t timeoutMs) {
    if (!enetHost_) return;

    ENetEvent event;
    while (enet_host_service(enetHost_, &event, timeoutMs) > 0) {
        switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT:
                onConnect();
                break;
            case ENET_EVENT_TYPE_DISCONNECT:
                onDisconnect();
                break;
            case ENET_EVENT_TYPE_RECEIVE:
                onReceive(event.packet->data, event.packet->dataLength);
                enet_packet_destroy(event.packet);
                break;
            case ENET_EVENT_TYPE_NONE:
                break;
        }
        timeoutMs = 0;
    }
}

// ---------------------------------------------------------------------------
// Outbound
// ---------------------------------------------------------------------------

void NetworkClient::sendInput(const InputFrame& input) {
    if (!connected_) return;
    auto pkt = encode(input);
    sendPacket(pkt, false /* unreliable */);
}

void NetworkClient::sendHeartbeat() {
    if (!connected_) return;
    // Minimal heartbeat: just the framed header with empty payload.
    auto pkt = encodePacket(PacketType::Heartbeat, {});
    sendPacket(pkt, false);
}

// ---------------------------------------------------------------------------
// Disconnect
// ---------------------------------------------------------------------------

void NetworkClient::disconnect() {
    if (enetPeer_) {
        enet_peer_disconnect(enetPeer_, 0);
        // Flush.
        if (enetHost_) {
            ENetEvent event;
            while (enet_host_service(enetHost_, &event, 100) > 0) {
                if (event.type == ENET_EVENT_TYPE_RECEIVE)
                    enet_packet_destroy(event.packet);
            }
        }
        enetPeer_ = nullptr;
    }
    if (enetHost_) {
        enet_host_destroy(enetHost_);
        enetHost_ = nullptr;
    }
    connected_ = false;
}

// ---------------------------------------------------------------------------
// ENet event handlers
// ---------------------------------------------------------------------------

void NetworkClient::onConnect() {
    connected_ = true;
    log("Connected to host. Sending version handshake...");

    // Send a SessionState as the version handshake.
    // Include one actor entry to declare the requested slot.
    SessionState handshake;
    handshake.sessionId = peerId_;
    handshake.gameBuild = gameBuild_;
    handshake.modHash = modHash_;
    SessionActor self;
    self.actorId = static_cast<std::uint32_t>(requestedSlot_);
    self.slot = requestedSlot_;
    self.ownerPeerId = peerId_;
    handshake.actors.push_back(self);
    auto pkt = encode(handshake);
    sendPacket(pkt, true /* reliable */);

    if (callbacks_.onConnected) callbacks_.onConnected();
}

void NetworkClient::onDisconnect() {
    connected_ = false;
    log("Disconnected from host.");
    if (callbacks_.onDisconnected) callbacks_.onDisconnected();
}

void NetworkClient::onReceive(const std::uint8_t* data, std::size_t size) {
    try {
        const std::uint8_t* payload = nullptr;
        std::size_t payloadSize = 0;
        auto type = decodePacketHeader(data, size, payload, payloadSize);
        ByteReader reader(payload, payloadSize);

        switch (type) {
            case PacketType::SessionState: {
                SessionState ss;
                read(reader, ss);
                if (callbacks_.onSessionState) callbacks_.onSessionState(ss);
                break;
            }
            case PacketType::ActorSnapshot: {
                ActorSnapshot snap;
                read(reader, snap);
                if (callbacks_.onActorSnapshot) callbacks_.onActorSnapshot(snap);
                break;
            }
            case PacketType::EnemySnapshot: {
                EnemySnapshot snap;
                read(reader, snap);
                if (callbacks_.onEnemySnapshot) callbacks_.onEnemySnapshot(snap);
                break;
            }
            case PacketType::EventMessage: {
                EventMessage evt;
                read(reader, evt);
                if (callbacks_.onEvent) callbacks_.onEvent(evt);
                break;
            }
            default:
                log("Unknown packet type from host: " +
                    std::to_string(static_cast<int>(type)));
                break;
        }
    } catch (const std::exception& ex) {
        log("Packet decode error: " + std::string(ex.what()));
    }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void NetworkClient::sendPacket(const std::vector<std::uint8_t>& packet,
                               bool reliable) {
    if (!enetPeer_) return;
    auto* enetPacket = enet_packet_create(
        packet.data(), packet.size(),
        reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    enet_peer_send(enetPeer_, reliable ? 0 : 1, enetPacket);
}

void NetworkClient::log(const std::string& msg) {
    if (callbacks_.onLog) {
        callbacks_.onLog("[NetworkClient] " + msg);
    }
}

} // namespace kh2coop
