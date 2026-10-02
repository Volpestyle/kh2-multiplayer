#include "kh2coop/NetworkClient.hpp"

#include <enet/enet.h>

#include <chrono>
#include <utility>

namespace kh2coop {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

NetworkClient::NetworkClient(const std::string& hostAddress, std::uint16_t port,
                             const std::string& gameBuild,
                             const std::string& modHash,
                             const std::string& peerId,
                             std::optional<SlotType> requestedSlot,
                             ClientCallbacks callbacks,
                             RuntimeMode requestedMode,
                             std::string contentHash,
                             std::uint16_t protocolVersion,
                             std::string peerName)
    : hostAddress_(hostAddress),
      port_(port),
      gameBuild_(gameBuild),
      modHash_(modHash),
      contentHash_(std::move(contentHash)),
      peerId_(peerId),
      peerName_(std::move(peerName)),
      requestedSlot_(requestedSlot),
      requestedMode_(requestedMode),
      protocolVersion_(protocolVersion),
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

    if (connected_) {
        // Fast pings until the estimate settles, then a slow refresh.
        const std::uint64_t interval = clockSamples_ < 5 ? 100 : 2000;
        if (localTimeMs() - lastPingMs_ >= interval) sendClockPing();
    }

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
                if (inbound_.conditions().active()) {
                    // Channel 0 is reliable; channel 1 may be dropped.
                    inbound_.enqueue(
                        localTimeMs(),
                        std::vector<std::uint8_t>(
                            event.packet->data,
                            event.packet->data + event.packet->dataLength),
                        event.channelID == 0);
                } else {
                    onReceive(event.packet->data, event.packet->dataLength);
                }
                enet_packet_destroy(event.packet);
                break;
            case ENET_EVENT_TYPE_NONE:
                break;
        }
        timeoutMs = 0;
    }

    flushConditioned();
}

void NetworkClient::flushConditioned() {
    const auto now = localTimeMs();
    for (auto& pkt : outbound_.popDue(now)) sendNow(pkt.bytes, pkt.reliable);
    for (auto& pkt : inbound_.popDue(now)) {
        onReceive(pkt.bytes.data(), pkt.bytes.size());
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

void NetworkClient::sendAvatar(AvatarState avatar) {
    if (!connected_) return;
    if (avatar.seq == 0) avatar.seq = ++avatarSeq_;
    if (avatar.serverTimeMs == 0) avatar.serverTimeMs = estimatedServerTimeMs();
    sendPacket(encode(avatar, PacketType::AvatarState), false);
}

void NetworkClient::sendClockPing() {
    if (!connected_) return;
    lastPingMs_ = localTimeMs();
    sendPacket(encode(ClockPing {lastPingMs_}), false);
}

std::uint64_t NetworkClient::localTimeMs() const {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    return static_cast<std::uint64_t>(ms + clockSkewMs_);
}

std::uint64_t NetworkClient::estimatedServerTimeMs() const {
    return static_cast<std::uint64_t>(
        static_cast<std::int64_t>(localTimeMs()) + clockOffsetMs_);
}

void NetworkClient::onClockPong(const ClockPong& pong) {
    const auto now = localTimeMs();
    if (pong.clientSendMs > now) return; // stale or bogus
    const auto rtt = static_cast<std::uint32_t>(now - pong.clientSendMs);
    // The lowest-RTT sample has the least queuing asymmetry, so trust it.
    if (clockSamples_ == 0 || rtt <= bestRttMs_) {
        bestRttMs_ = rtt;
        clockOffsetMs_ = static_cast<std::int64_t>(pong.serverMs) +
                         static_cast<std::int64_t>(rtt / 2) -
                         static_cast<std::int64_t>(now);
    }
    ++clockSamples_;
}

void NetworkClient::setLinkConditions(const LinkConditions& outbound,
                                      const LinkConditions& inbound) {
    outbound_.configure(outbound);
    inbound_.configure(inbound);
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
    log("Connected to host. Sending ClientHello handshake...");

    // Send a dedicated ClientHello packet for the version handshake.
    // This replaces the old SessionState-as-hello pattern (B2 cleanup).
    ClientHello hello;
    hello.protocolVersion = protocolVersion_;
    hello.gameBuild = gameBuild_;
    hello.contentHash = contentHash_;
    hello.modHash = modHash_;
    hello.peerId = peerId_;
    hello.peerName = peerName_.empty() ? peerId_ : peerName_;
    hello.requestedMode = requestedMode_;
    hello.requestedSlot = requestedSlot_.has_value()
                              ? static_cast<std::uint8_t>(*requestedSlot_)
                              : 0xFF;
    auto pkt = encode(hello);
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
            case PacketType::AvatarRelay: {
                AvatarState avatar;
                read(reader, avatar);
                if (callbacks_.onAvatarState) callbacks_.onAvatarState(avatar);
                break;
            }
            case PacketType::ClockPong: {
                ClockPong pong;
                read(reader, pong);
                onClockPong(pong);
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
    if (outbound_.conditions().active()) {
        outbound_.enqueue(localTimeMs(), packet, reliable);
        return;
    }
    sendNow(packet, reliable);
}

void NetworkClient::sendNow(const std::vector<std::uint8_t>& packet,
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
