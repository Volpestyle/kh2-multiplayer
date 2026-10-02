#include "kh2coop/SessionHost.hpp"
#include "kh2coop/ProgressMirror.hpp"

#include <algorithm>
#include <chrono>
#include <enet/enet.h>
#include <optional>
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

bool isValidRuntimeMode(RuntimeMode mode) {
    switch (mode) {
        case RuntimeMode::CampaignCoop:
        case RuntimeMode::PublicRealm:
            return true;
    }

    return false;
}

const char* runtimeModeName(RuntimeMode mode) {
    switch (mode) {
        case RuntimeMode::CampaignCoop:
            return "campaign_coop";
        case RuntimeMode::PublicRealm:
            return "public_realm";
    }

    return "unknown";
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
    if (!config_.bindAddress.empty() &&
        enet_address_set_host_ip(&address, config_.bindAddress.c_str()) != 0) {
        log("Invalid bind address: " + config_.bindAddress);
        return false;
    }

    enetHost_ = enet_host_create(&address, config_.maxPeers, 2 /* channels */,
                                 0 /* unlimited downstream */,
                                 0 /* unlimited upstream */);
    if (!enetHost_) {
        log("Failed to create ENet host on port " + std::to_string(config_.port));
        return false;
    }

    running_ = true;
    log("Session host listening on port " + std::to_string(config_.port) +
        " (proto=" + std::to_string(config_.protocolVersion) +
        " mode=" + runtimeModeName(config_.runtimeMode) +
        " build=" + config_.gameBuild +
        " content=" + config_.contentHash +
        " mod=" + config_.modHash + ")");
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

std::optional<SlotType> SessionHost::firstFreeSlot() const {
    for (auto slot : {SlotType::Player, SlotType::Friend1, SlotType::Friend2}) {
        if (!isSlotTaken(slot)) {
            return slot;
        }
    }

    return std::nullopt;
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
            case PacketType::ClientHello: {
                // Dedicated handshake packet (B2: replaces SessionState-as-hello).
                ClientHello hello;
                read(reader, hello);

                if (hello.protocolVersion != config_.protocolVersion) {
                    const std::string reason =
                        "Protocol mismatch: client=" +
                        std::to_string(hello.protocolVersion) +
                        " server=" + std::to_string(config_.protocolVersion);
                    rejectPeer(peer, ps->peerId, reason, 1);
                    return;
                }

                if (!isValidRuntimeMode(hello.requestedMode)) {
                    const std::string reason = "Invalid requested mode";
                    rejectPeer(peer, ps->peerId, reason, 1);
                    return;
                }

                if (hello.requestedMode != config_.runtimeMode) {
                    const std::string reason =
                        "Mode mismatch: client=" +
                        std::string(runtimeModeName(hello.requestedMode)) +
                        " server=" + runtimeModeName(config_.runtimeMode);
                    rejectPeer(peer, ps->peerId, reason, 1);
                    return;
                }

                if (hello.gameBuild != config_.gameBuild ||
                    hello.contentHash != config_.contentHash ||
                    hello.modHash != config_.modHash) {
                    std::string reason =
                        "Version mismatch: build=" + hello.gameBuild +
                        " content=" + hello.contentHash +
                        " mod=" + hello.modHash +
                        " (relay expects build=" + config_.gameBuild +
                        " content=" + config_.contentHash +
                        " mod=" + config_.modHash + ")";
                    rejectPeer(peer, ps->peerId, reason, 1);
                    return;
                }

                // Resolve requested slot — 0xFF means no preference.
                std::optional<SlotType> requestedSlot;
                if (hello.requestedSlot != 0xFF) {
                    const auto requested =
                        static_cast<SlotType>(hello.requestedSlot);
                    if (!isValidSlot(requested)) {
                        const std::string reason = "Invalid requested slot";
                        rejectPeer(peer, ps->peerId, reason, 2);
                        return;
                    }
                    requestedSlot = requested;
                } else {
                    requestedSlot = firstFreeSlot();
                    if (!requestedSlot.has_value()) {
                        const std::string reason = "No free slots available";
                        rejectPeer(peer, ps->peerId, reason, 2);
                        return;
                    }
                }

                if (isSlotTaken(*requestedSlot)) {
                    const std::string reason =
                        "Requested slot " +
                        std::to_string(static_cast<int>(*requestedSlot)) +
                        " is already taken";
                    rejectPeer(peer, ps->peerId, reason, 2);
                    return;
                }

                // Version OK — assign the validated requested slot.
                ps->gameBuild = hello.gameBuild;
                ps->modHash = hello.modHash;
                if (!hello.peerId.empty()) {
                    ps->peerId = hello.peerId;
                }

                ps->status = PeerStatus::Verified;
                ps->assignedSlot = *requestedSlot;

                log("Peer verified: " + ps->peerId + " -> slot " +
                    std::to_string(static_cast<int>(ps->assignedSlot)));

                rebuildSessionActors();

                if (callbacks_.onPeerJoined)
                    callbacks_.onPeerJoined(ps->peerId, ps->assignedSlot);

                // Send full session state to everyone.
                broadcastSessionState();
                if (!fromHost(*ps)) sendWorldStateTo(peer);
                break;
            }

            case PacketType::SessionState: {
                // Legacy handshake: client sends SessionState as version check.
                // Kept for backward compatibility during transition to ClientHello.
                SessionState clientSession;
                read(reader, clientSession);

                if (clientSession.gameBuild != config_.gameBuild ||
                    clientSession.modHash != config_.modHash) {
                    std::string reason =
                        "Version mismatch: build=" + clientSession.gameBuild +
                        " mod=" + clientSession.modHash +
                        " (relay expects build=" + config_.gameBuild +
                        " mod=" + config_.modHash + ")";
                    rejectPeer(peer, ps->peerId, reason, 1);
                    return;
                }

                SlotType requestedSlot = SlotType::Player;
                if (!tryGetRequestedSlot(clientSession, requestedSlot)) {
                    const std::string reason =
                        "Handshake missing a valid requested slot";
                    rejectPeer(peer, ps->peerId, reason, 2);
                    return;
                }

                if (isSlotTaken(requestedSlot)) {
                    const std::string reason =
                        "Requested slot " +
                        std::to_string(static_cast<int>(requestedSlot)) +
                        " is already taken";
                    rejectPeer(peer, ps->peerId, reason, 2);
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

                log("Peer verified (legacy handshake): " + ps->peerId +
                    " -> slot " +
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

            case PacketType::RoomTransition:
            case PacketType::EventHold:
            case PacketType::EnemyManifest:
            case PacketType::EnemyHp:
            case PacketType::EnemyDeath:
            case PacketType::ProgressUpdate: {
                if (ps->status != PeerStatus::Verified || !fromHost(*ps)) {
                    ++rejectedWorld_;
                    log("Dropping world message from non-host " + ps->peerId);
                    return;
                }
                const std::vector<std::uint8_t> packet(data, data + size);
                bool reliable = true;
                if (type == PacketType::RoomTransition) {
                    RoomTransition m;
                    read(reader, m);
                    clearWorldState();
                    room_ = m;
                    log("Host transition epoch " + std::to_string(m.epoch) +
                        " -> world " + std::to_string(m.worldId) + " room " +
                        std::to_string(m.roomId));
                } else if (type == PacketType::EventHold) {
                    EventHold m;
                    read(reader, m);
                    hold_ = m;
                } else if (type == PacketType::EnemyManifest) {
                    EnemyManifest m;
                    read(reader, m);
                    if (m.replace || m.epoch != manifest_.epoch) {
                        manifest_ = m;
                        enemyHp_.clear();
                        deadEnemies_.clear();
                    } else {
                        manifest_.entries.insert(manifest_.entries.end(),
                                                 m.entries.begin(), m.entries.end());
                    }
                } else if (type == PacketType::EnemyHp) {
                    EnemyHp m;
                    read(reader, m);
                    for (const auto& e : m.entries) enemyHp_[e.netId] = e;
                    reliable = false; // periodic absolute values
                } else if (type == PacketType::ProgressUpdate) {
                    ProgressUpdate m;
                    read(reader, m);
                    if (m.full) progress_.clear();
                    for (const auto& span : m.spans) {
                        for (std::size_t i = 0; i < span.bytes.size(); ++i) {
                            progress_[static_cast<std::uint32_t>(span.offset + i)] =
                                span.bytes[i];
                        }
                    }
                    progressVersion_ = m.version;
                } else {
                    EnemyDeath m;
                    read(reader, m);
                    deadEnemies_.insert(m.netId);
                }
                forwardToOthers(peer, packet, reliable);
                break;
            }

            case PacketType::HitClaim: {
                if (ps->status != PeerStatus::Verified || fromHost(*ps)) {
                    // The host applies its own hits natively.
                    ++rejectedWorld_;
                    return;
                }
                HitClaim claim;
                read(reader, claim);
                claim.attackerSlot = ps->assignedSlot; // never trust the claim
                if (auto* host = hostPeer()) {
                    sendTo(host->enetPeer, encode(claim), true);
                }
                break;
            }

            case PacketType::TransitionAck: {
                if (ps->status != PeerStatus::Verified) return;
                TransitionAck ack;
                read(reader, ack);
                ps->ackEpoch = ack.epoch;
                ps->ackWorldId = ack.worldId;
                ps->ackRoomId = ack.roomId;
                ps->ackArrived = ack.arrived;
                if (room_ && ack.epoch == room_->epoch &&
                    (!ack.arrived || ack.worldId != room_->worldId ||
                     ack.roomId != room_->roomId)) {
                    log("Divergence: " + ps->peerId + " reports world " +
                        std::to_string(ack.worldId) + " room " +
                        std::to_string(ack.roomId) + " for epoch " +
                        std::to_string(ack.epoch));
                }
                break;
            }

            case PacketType::StateHash: {
                if (ps->status != PeerStatus::Verified) return;
                read(reader, ps->lastHash);
                ps->hasHash = true;
                if (fromHost(*ps)) {
                    for (auto& other : peers_) {
                        if (!fromHost(other) && other.hasHash) compareWithHost(other);
                    }
                } else {
                    compareWithHost(*ps);
                }
                break;
            }

            case PacketType::ResyncRequest: {
                if (ps->status != PeerStatus::Verified || !fromHost(*ps)) {
                    ++rejectedWorld_;
                    return;
                }
                ResyncRequest req;
                read(reader, req);
                for (auto& other : peers_) {
                    if (other.status != PeerStatus::Verified || fromHost(other)) continue;
                    if (req.slot != 0xFF &&
                        req.slot != static_cast<std::uint8_t>(other.assignedSlot)) {
                        continue;
                    }
                    log("Resync requested for " + other.peerId);
                    other.mismatchStreak = 0;
                    other.reportedFields = 0;
                    sendWorldStateTo(other.enetPeer);
                }
                break;
            }

            case PacketType::ClockPing: {
                ClockPing ping;
                read(reader, ping);
                sendTo(peer, encode(ClockPong {ping.clientSendMs, currentTimeMs()}),
                       false);
                break;
            }

            case PacketType::AvatarState: {
                if (ps->status != PeerStatus::Verified) {
                    log("Ignoring avatar from unverified peer " + ps->peerId);
                    return;
                }
                AvatarState avatar;
                read(reader, avatar);
                // The owner is whoever sent it, never what the packet claims.
                avatar.ownerSlot = ps->assignedSlot;
                const auto relay = encode(avatar, PacketType::AvatarRelay);
                for (auto& other : peers_) {
                    if (other.enetPeer != peer &&
                        other.status == PeerStatus::Verified && other.enetPeer) {
                        sendTo(other.enetPeer, relay, false);
                    }
                }
                ++relayedAvatars_;
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
// World sync helpers
// ---------------------------------------------------------------------------

bool SessionHost::fromHost(const PeerState& ps) const {
    return ps.status == PeerStatus::Verified && ps.assignedSlot == SlotType::Player;
}

PeerState* SessionHost::hostPeer() {
    for (auto& ps : peers_) {
        if (fromHost(ps)) return &ps;
    }
    return nullptr;
}

const PeerState* SessionHost::peerBySlot(SlotType slot) const {
    for (const auto& ps : peers_) {
        if (ps.status == PeerStatus::Verified && ps.assignedSlot == slot) return &ps;
    }
    return nullptr;
}

void SessionHost::forwardToOthers(ENetPeer* sender,
                                  const std::vector<std::uint8_t>& packet,
                                  bool reliable) {
    for (auto& other : peers_) {
        if (other.enetPeer != sender && other.status == PeerStatus::Verified &&
            other.enetPeer) {
            sendTo(other.enetPeer, packet, reliable);
        }
    }
}

// Catch a late joiner up: room, hold, enemy set, HP, deaths.
void SessionHost::sendWorldStateTo(ENetPeer* peer) {
    if (!progress_.empty()) {
        // Coalesce the merged byte map into spans, then chunk under the
        // packet size limit; the first chunk replaces the joiner's state.
        std::vector<ProgressSpan> spans;
        for (const auto& [offset, value] : progress_) {
            if (!spans.empty() &&
                spans.back().offset + spans.back().bytes.size() == offset) {
                spans.back().bytes.push_back(value);
            } else {
                spans.push_back({offset, {value}});
            }
        }
        for (const auto& u : splitProgressUpdate(progressVersion_, true, spans)) {
            sendTo(peer, encode(u), true);
        }
    }
    if (!room_) return;
    sendTo(peer, encode(*room_), true);
    if (hold_ && hold_->epoch == room_->epoch) sendTo(peer, encode(*hold_), true);
    if (!manifest_.entries.empty()) {
        EnemyManifest m = manifest_;
        m.replace = true;
        sendTo(peer, encode(m), true);
    }
    if (!enemyHp_.empty()) {
        EnemyHp hp;
        hp.epoch = manifest_.epoch;
        for (const auto& [id, e] : enemyHp_) hp.entries.push_back(e);
        sendTo(peer, encode(hp), true);
    }
    for (auto id : deadEnemies_) {
        sendTo(peer, encode(EnemyDeath {manifest_.epoch, id}), true);
    }
}

// A client disagreeing with the host on the same epoch for two consecutive
// comparisons is reported once per distinct set of fields (transient
// mismatches during a load don't fire). Different epochs aren't compared:
// the client may simply still be loading.
void SessionHost::compareWithHost(PeerState& client) {
    auto* host = hostPeer();
    if (!host || !host->hasHash || !client.hasHash) return;
    const auto& h = host->lastHash;
    const auto& c = client.lastHash;
    if (h.epoch != c.epoch) return;
    std::uint8_t fields = 0;
    if (h.worldId != c.worldId || h.roomId != c.roomId) fields |= DesyncRoom;
    if (h.enemiesHash != c.enemiesHash) fields |= DesyncEnemies;
    if (h.progressHash != c.progressHash) fields |= DesyncProgress;
    if (fields == 0) {
        client.mismatchStreak = 0;
        client.reportedFields = 0;
        return;
    }
    if (++client.mismatchStreak < 2 || fields == client.reportedFields) return;
    client.reportedFields = fields;
    ++desyncNotices_;
    log("Desync: " + client.peerId + " fields=" + std::to_string(fields) +
        " epoch=" + std::to_string(h.epoch));
    broadcastToVerified(encode(DesyncNotice {client.assignedSlot, h.epoch, fields}), true);
}

void SessionHost::clearWorldState() {
    hold_.reset();
    manifest_ = {};
    enemyHp_.clear();
    deadEnemies_.clear();
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
    if (auto* ps = findPeer(peer); ps && fromHost(*ps)) {
        room_.reset();
        clearWorldState();
        progress_.clear();
    }
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

void SessionHost::rejectPeer(ENetPeer* peer, const std::string& peerId,
                             const std::string& reason, std::uint8_t code) {
    log("Rejecting " + peerId + ": " + reason);
    if (callbacks_.onPeerRejected) callbacks_.onPeerRejected(peerId, reason);
    HelloReject reject;
    reject.code = code;
    reject.reason = reason;
    sendTo(peer, encode(reject), true);
    // disconnect_later lets the reject packet go out first.
    enet_peer_disconnect_later(peer, code);
}

void SessionHost::log(const std::string& msg) {
    if (callbacks_.onLog) {
        callbacks_.onLog("[SessionHost] " + msg);
    }
}

} // namespace kh2coop
