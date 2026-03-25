#include "kh2coop/SessionHost.hpp"

#include <algorithm>
#include <chrono>
#include <enet/enet.h>
#include <sstream>

namespace kh2coop {

namespace {

std::uint64_t currentTimeMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

bool isValidSlot(SlotType slot) {
    switch (slot) {
        case SlotType::Player:
        case SlotType::Friend1:
        case SlotType::Friend2:
            return true;
    }

    return false;
}

bool tryGetRequestedSlot(const SessionState& handshake, SlotType& requestedSlot) {
    if (handshake.actors.size() != 1) {
        return false;
    }

    requestedSlot = handshake.actors.front().slot;
    return isValidSlot(requestedSlot);
}

} // namespace

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

SessionHost::SessionHost(const SessionConfig& config, SessionCallbacks callbacks)
    : config_(config), callbacks_(std::move(callbacks)) {
    session_.sessionId = config_.sessionId;
    session_.gameBuild = config_.gameBuild;
    session_.modHash = config_.modHash;
}

SessionHost::~SessionHost() { stop(); }

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool SessionHost::start() {
    if (running_) return true;

    ENetAddress address;
    address.host = ENET_HOST_ANY;
    address.port = config_.port;

    enetHost_ = enet_host_create(&address, config_.maxPeers, 2 /* channels */,
                                 0 /* unlimited downstream */,
                                 0 /* unlimited upstream */);
    if (!enetHost_) {
        log("Failed to create ENet host on port " + std::to_string(config_.port));
        return false;
    }

    running_ = true;
    log("Session host listening on port " + std::to_string(config_.port) +
        " (build=" + config_.gameBuild + " mod=" + config_.modHash + ")");
    return true;
}

void SessionHost::tick(std::uint32_t timeoutMs) {
    if (!running_ || !enetHost_) return;

    ENetEvent event;
    while (enet_host_service(enetHost_, &event, timeoutMs) > 0) {
        switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT:
                onConnect(event.peer);
                break;
            case ENET_EVENT_TYPE_DISCONNECT:
                onDisconnect(event.peer);
                break;
            case ENET_EVENT_TYPE_RECEIVE:
                onReceive(event.peer, event.packet->data, event.packet->dataLength);
                enet_packet_destroy(event.packet);
                break;
            case ENET_EVENT_TYPE_NONE:
                break;
        }
        // After first event, poll remaining without blocking.
        timeoutMs = 0;
    }

    expireStalePeers(currentTimeMs());
}

void SessionHost::stop() {
    if (!running_) return;
    running_ = false;

    // Disconnect all peers gracefully.
    for (auto& ps : peers_) {
        if (ps.enetPeer) {
            enet_peer_disconnect(ps.enetPeer, 0);
        }
    }

    // Flush disconnects.
    if (enetHost_) {
        ENetEvent event;
        while (enet_host_service(enetHost_, &event, 100) > 0) {
            if (event.type == ENET_EVENT_TYPE_RECEIVE) {
                enet_packet_destroy(event.packet);
            }
        }
        enet_host_destroy(enetHost_);
        enetHost_ = nullptr;
    }

    peers_.clear();
    log("Session host stopped.");
}

// ---------------------------------------------------------------------------
// Outbound broadcasting
// ---------------------------------------------------------------------------

void SessionHost::broadcastSessionState() {
    auto pkt = encode(session_);
    broadcastToVerified(pkt, true /* reliable */);
}

void SessionHost::broadcastActorSnapshots(
    const std::vector<ActorSnapshot>& snapshots) {
    for (const auto& snap : snapshots) {
        auto pkt = encode(snap);
        broadcastToVerified(pkt, false /* unreliable */);
    }
}

void SessionHost::broadcastEnemySnapshots(
    const std::vector<EnemySnapshot>& snapshots) {
    for (const auto& snap : snapshots) {
        auto pkt = encode(snap);
        broadcastToVerified(pkt, false /* unreliable */);
    }
}

void SessionHost::broadcastEvent(const EventMessage& event) {
    auto pkt = encode(event);
    broadcastToVerified(pkt, true /* reliable */);
}

// ---------------------------------------------------------------------------
// State queries
// ---------------------------------------------------------------------------

std::size_t SessionHost::verifiedPeerCount() const {
    return std::count_if(peers_.begin(), peers_.end(), [](const PeerState& ps) {
        return ps.status == PeerStatus::Verified;
    });
}

// ---------------------------------------------------------------------------
// ENet event handlers
// ---------------------------------------------------------------------------

void SessionHost::onConnect(ENetPeer* peer) {
    if (peers_.size() >= config_.maxPeers) {
        log("Rejecting connection: lobby full.");
        enet_peer_disconnect(peer, 0);
        return;
    }

    // Create a temporary peer id from the address until the client sends its
    // real identity in the version handshake.
    std::ostringstream oss;
    oss << "peer_" << peer->address.host << ":" << peer->address.port;

    PeerState ps;
    ps.enetPeer = peer;
    ps.peerId = oss.str();
    ps.status = PeerStatus::PendingVersion;
    ps.lastHeartbeatMs = currentTimeMs();
    peers_.push_back(std::move(ps));

    log("Peer connected: " + peers_.back().peerId + " (pending version check)");
}

void SessionHost::onDisconnect(ENetPeer* peer) {
    auto* ps = findPeer(peer);
    if (ps) {
        std::string id = ps->peerId;
        removePeer(peer);
        log("Peer disconnected: " + id);
        if (callbacks_.onPeerLeft) callbacks_.onPeerLeft(id);

        rebuildSessionActors();
        broadcastSessionState();
    }
}

void SessionHost::onReceive(ENetPeer* peer, const std::uint8_t* data,
                            std::size_t size) {
    auto* ps = findPeer(peer);
    if (!ps) return;

    try {
        const std::uint8_t* payload = nullptr;
        std::size_t payloadSize = 0;
        auto type = decodePacketHeader(data, size, payload, payloadSize);
        ByteReader reader(payload, payloadSize);
        ps->lastHeartbeatMs = currentTimeMs();

        switch (type) {
            case PacketType::SessionState: {
                // Client sends a SessionState as its version handshake.
                SessionState clientSession;
                read(reader, clientSession);

                if (clientSession.gameBuild != config_.gameBuild ||
                    clientSession.modHash != config_.modHash) {
                    std::string reason =
                        "Version mismatch: build=" + clientSession.gameBuild +
                        " mod=" + clientSession.modHash;
                    log("Rejecting " + ps->peerId + ": " + reason);
                    if (callbacks_.onPeerRejected)
                        callbacks_.onPeerRejected(ps->peerId, reason);
                    enet_peer_disconnect(peer, 1);
                    return;
                }

                SlotType requestedSlot = SlotType::Player;
                if (!tryGetRequestedSlot(clientSession, requestedSlot)) {
                    const std::string reason =
                        "Handshake missing a valid requested slot";
                    log("Rejecting " + ps->peerId + ": " + reason);
                    if (callbacks_.onPeerRejected)
                        callbacks_.onPeerRejected(ps->peerId, reason);
                    enet_peer_disconnect(peer, 2);
                    return;
                }

                if (isSlotTaken(requestedSlot)) {
                    const std::string reason =
                        "Requested slot " +
                        std::to_string(static_cast<int>(requestedSlot)) +
                        " is already taken";
                    log("Rejecting " + ps->peerId + ": " + reason);
                    if (callbacks_.onPeerRejected)
                        callbacks_.onPeerRejected(ps->peerId, reason);
                    enet_peer_disconnect(peer, 2);
                    return;
                }

                // Version OK — assign the validated requested slot.
                ps->gameBuild = clientSession.gameBuild;
                ps->modHash = clientSession.modHash;
                if (!clientSession.sessionId.empty()) {
                    ps->peerId = clientSession.sessionId; // use as peer name
                }

                ps->status = PeerStatus::Verified;
                ps->assignedSlot = requestedSlot;

                log("Peer verified: " + ps->peerId + " -> slot " +
                    std::to_string(static_cast<int>(ps->assignedSlot)));

                rebuildSessionActors();

                if (callbacks_.onPeerJoined)
                    callbacks_.onPeerJoined(ps->peerId, ps->assignedSlot);

                // Send full session state to everyone.
                broadcastSessionState();
                break;
            }

            case PacketType::InputFrame: {
                if (ps->status != PeerStatus::Verified) {
                    log("Ignoring input from unverified peer " + ps->peerId);
                    return;
                }
                InputFrame input;
                read(reader, input);
                ps->lastInput = input;

                if (callbacks_.onInputReceived)
                    callbacks_.onInputReceived(ps->peerId, input);
                break;
            }

            case PacketType::Heartbeat: {
                break;
            }

            default:
                log("Unknown packet type from " + ps->peerId + ": " +
                    std::to_string(static_cast<int>(type)));
                break;
        }
    } catch (const std::exception& ex) {
        log("Packet decode error from " + ps->peerId + ": " + ex.what());
    }
}

// ---------------------------------------------------------------------------
// Peer helpers
// ---------------------------------------------------------------------------

PeerState* SessionHost::findPeer(ENetPeer* peer) {
    for (auto& ps : peers_) {
        if (ps.enetPeer == peer) return &ps;
    }
    return nullptr;
}

PeerState* SessionHost::findPeerById(const std::string& peerId) {
    for (auto& ps : peers_) {
        if (ps.peerId == peerId) return &ps;
    }
    return nullptr;
}

bool SessionHost::isSlotTaken(SlotType slot) const {
    return std::any_of(peers_.begin(), peers_.end(), [slot](const PeerState& ps) {
        return ps.status == PeerStatus::Verified && ps.assignedSlot == slot;
    });
}

void SessionHost::rebuildSessionActors() {
    session_.actors.clear();
    for (const auto& peer : peers_) {
        if (peer.status != PeerStatus::Verified) {
            continue;
        }

        SessionActor actor;
        actor.actorId = static_cast<std::uint32_t>(peer.assignedSlot);
        actor.slot = peer.assignedSlot;
        actor.ownerPeerId = peer.peerId;
        session_.actors.push_back(std::move(actor));
    }
}

void SessionHost::expireStalePeers(std::uint64_t nowMs) {
    struct ExpiredPeer {
        ENetPeer* peer{nullptr};
        std::string peerId;
        PeerStatus status{PeerStatus::PendingVersion};
        std::string reason;
    };

    std::vector<ExpiredPeer> expired;
    expired.reserve(peers_.size());

    for (const auto& peer : peers_) {
        const auto timeoutMs =
            peer.status == PeerStatus::Verified ? config_.heartbeatTimeoutMs
                                                : config_.pendingPeerTimeoutMs;
        if (timeoutMs == 0 || peer.lastHeartbeatMs == 0) {
            continue;
        }

        if (nowMs - peer.lastHeartbeatMs < timeoutMs) {
            continue;
        }

        ExpiredPeer expiredPeer;
        expiredPeer.peer = peer.enetPeer;
        expiredPeer.peerId = peer.peerId;
        expiredPeer.status = peer.status;
        expiredPeer.reason = peer.status == PeerStatus::Verified
                                 ? "Heartbeat timed out"
                                 : "Handshake timed out";
        expired.push_back(std::move(expiredPeer));
    }

    bool removedVerifiedPeer = false;

    for (const auto& expiredPeer : expired) {
        if (expiredPeer.peer) {
            enet_peer_disconnect(expiredPeer.peer, 3);
        }

        removePeer(expiredPeer.peer);

        if (expiredPeer.status == PeerStatus::Verified) {
            removedVerifiedPeer = true;
            log("Peer timed out: " + expiredPeer.peerId);
            if (callbacks_.onPeerLeft) {
                callbacks_.onPeerLeft(expiredPeer.peerId);
            }
            continue;
        }

        log("Rejecting " + expiredPeer.peerId + ": " + expiredPeer.reason);
        if (callbacks_.onPeerRejected) {
            callbacks_.onPeerRejected(expiredPeer.peerId, expiredPeer.reason);
        }
    }

    if (removedVerifiedPeer) {
        rebuildSessionActors();
        broadcastSessionState();
    }
}

void SessionHost::removePeer(ENetPeer* peer) {
    peers_.erase(
        std::remove_if(peers_.begin(), peers_.end(),
                        [peer](const PeerState& ps) { return ps.enetPeer == peer; }),
        peers_.end());
}

// ---------------------------------------------------------------------------
// Send helpers
// ---------------------------------------------------------------------------

void SessionHost::sendTo(ENetPeer* peer,
                         const std::vector<std::uint8_t>& packet,
                         bool reliable) {
    auto* enetPacket = enet_packet_create(
        packet.data(), packet.size(),
        reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    // Channel 0 = reliable, channel 1 = unreliable snapshots.
    enet_peer_send(peer, reliable ? 0 : 1, enetPacket);
}

void SessionHost::broadcastToVerified(
    const std::vector<std::uint8_t>& packet, bool reliable) {
    for (auto& ps : peers_) {
        if (ps.status == PeerStatus::Verified && ps.enetPeer) {
            sendTo(ps.enetPeer, packet, reliable);
        }
    }
}

void SessionHost::log(const std::string& msg) {
    if (callbacks_.onLog) {
        callbacks_.onLog("[SessionHost] " + msg);
    }
}

} // namespace kh2coop
