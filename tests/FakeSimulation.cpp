// FakeSimulation — proves the codec, server, and client networking work
// end-to-end without KH2 attached.
//
// What it does:
//   1. Starts a SessionHost on localhost.
//   2. Connects 3 NetworkClients (Player, Friend1, Friend2).
//   3. Each client sends a version handshake and verifies slot assignment.
//   4. The host runs a fake authoritative simulation from client input.
//   5. The host broadcasts authoritative ActorSnapshots back to all clients.
//   6. Clients verify they receive consistent snapshots.
//   7. The host broadcasts a reliable Event (SpawnGroup).
//   8. Clients verify they receive the event.
//   9. Clients disconnect. Server shuts down.
//
// Exit code 0 = all checks passed. Non-zero = failure.

#include "kh2coop/Codec.hpp"
#include "kh2coop/NetworkClient.hpp"
#include "kh2coop/ReplicaController.hpp"
#include "kh2coop/SessionHost.hpp"
#include "kh2coop/SimulationState.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <enet/enet.h>
#include <iostream>
#include <thread>

using namespace kh2coop;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static int g_errors = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "  FAIL: " << msg << "\n";
        ++g_errors;
    } else {
        std::cout << "  PASS: " << msg << "\n";
    }
}

static void tickAll(SessionHost& host, NetworkClient& c0, NetworkClient& c1,
                    NetworkClient& c2, int rounds = 10) {
    for (int i = 0; i < rounds; ++i) {
        host.tick(5);
        c0.tick(5);
        c1.tick(5);
        c2.tick(5);
    }
}

static void tickPair(SessionHost& host, NetworkClient& client,
                     int rounds = 10, std::uint32_t timeoutMs = 5) {
    for (int i = 0; i < rounds; ++i) {
        host.tick(timeoutMs);
        client.tick(timeoutMs);
    }
}

struct SnapshotTracker {
    struct SnapshotSet {
        std::array<ActorSnapshot, 3> actors {};
        std::array<bool, 3> received {false, false, false};
    };

    void record(const ActorSnapshot& snapshot) {
        const auto index = static_cast<std::size_t>(snapshot.actor.slot);
        auto& set = snapshots[snapshot.snapshotId];
        set.actors[index] = snapshot;
        set.received[index] = true;
    }

    [[nodiscard]] bool hasCompleteSnapshot(std::uint32_t snapshotId) const {
        const auto it = snapshots.find(snapshotId);
        if (it == snapshots.end()) {
            return false;
        }

        const auto& received = it->second.received;
        return received[0] && received[1] && received[2];
    }

    [[nodiscard]] const SnapshotSet* find(std::uint32_t snapshotId) const {
        const auto it = snapshots.find(snapshotId);
        if (it == snapshots.end()) {
            return nullptr;
        }
        return &it->second;
    }

    std::map<std::uint32_t, SnapshotSet> snapshots;
};

static void tickAuthoritativeFrame(SessionHost& host, SimulationState& sim,
                                   NetworkClient& c0, NetworkClient& c1,
                                   NetworkClient& c2,
                                   float dtSeconds = 1.0f / 60.0f) {
    c0.tick(0);
    c1.tick(0);
    c2.tick(0);
    host.tick(0);

    for (const auto& peer : host.peers()) {
        if (peer.status == PeerStatus::Verified) {
            sim.applyInput(peer.assignedSlot, peer.lastInput);
        }
    }

    sim.tick(dtSeconds);
    host.broadcastActorSnapshots(sim.generateSnapshots());

    host.tick(0);
    c0.tick(0);
    c1.tick(0);
    c2.tick(0);
}

static std::uint32_t findHighestCommonSnapshotId(const SnapshotTracker& a,
                                                 const SnapshotTracker& b,
                                                 const SnapshotTracker& c) {
    std::uint32_t highest = 0;

    for (const auto& [snapshotId, set] : a.snapshots) {
        if (!set.received[0] || !set.received[1] || !set.received[2]) {
            continue;
        }

        if (b.hasCompleteSnapshot(snapshotId) && c.hasCompleteSnapshot(snapshotId)) {
            highest = snapshotId;
        }
    }

    return highest;
}

static bool nearlyEqual(float lhs, float rhs, float epsilon = 0.0001f) {
    return std::fabs(lhs - rhs) <= epsilon;
}

class RecordingGameBridge final : public IGameBridge {
public:
    bool IsAttached() const override { return true; }
    RoomState ReadRoomState() const override { return {}; }
    std::optional<ActorState> ReadActorState(SlotType) const override {
        return std::nullopt;
    }
    std::vector<EnemyState> ReadEnemyStates() const override { return {}; }
    bool WriteCameraTarget(SlotType) override { return true; }
    bool RestoreVanillaCamera() override { return true; }
    bool InjectOwnedInput(SlotType, const InputFrame&) override { return true; }

    bool ApplyReplicaActorState(const ActorState& state) override {
        appliedActors.push_back(state);
        lastActorBySlot[static_cast<std::size_t>(state.slot)] = state;
        return true;
    }

    bool ApplyReplicaEnemyState(const EnemyState& state) override {
        appliedEnemies.push_back(state);
        lastEnemyById[state.netId] = state;
        return true;
    }

    std::vector<ActorState> appliedActors;
    std::vector<EnemyState> appliedEnemies;
    std::array<ActorState, 3> lastActorBySlot {};
    std::map<std::uint32_t, EnemyState> lastEnemyById;
};

// ---------------------------------------------------------------------------
// Codec round-trip test (no networking)
// ---------------------------------------------------------------------------

static void testCodecRoundtrip() {
    std::cout << "\n=== Codec round-trip ===\n";

    // ActorState
    ActorState orig;
    orig.actorId = 42;
    orig.slot = SlotType::Friend1;
    orig.position = {1.0f, 2.0f, 3.0f};
    orig.rotationY = 1.57f;
    orig.velocity = {0.1f, 0.0f, -0.5f};
    orig.motionId = 100;
    orig.action = ActionState::Attack;
    orig.comboStep = 2;
    orig.hp = 120;
    orig.mp = 50;
    orig.drive = 30;
    orig.targetId = 7;
    orig.airborne = true;
    orig.invuln = false;
    orig.staggered = true;
    orig.downed = false;

    ByteWriter w;
    write(w, orig);
    ByteReader r(w.data());
    ActorState decoded;
    read(r, decoded);

    check(decoded.actorId == 42, "actorId round-trip");
    check(decoded.slot == SlotType::Friend1, "slot round-trip");
    check(decoded.position.x == 1.0f && decoded.position.y == 2.0f &&
              decoded.position.z == 3.0f,
          "position round-trip");
    check(decoded.action == ActionState::Attack, "action round-trip");
    check(decoded.hp == 120 && decoded.mp == 50, "hp/mp round-trip");
    check(decoded.airborne == true && decoded.staggered == true,
          "flags round-trip");
    check(decoded.invuln == false && decoded.downed == false,
          "flags false round-trip");

    // InputFrame
    InputFrame input;
    input.seq = 999;
    input.clientTimeMs = 123456789ULL;
    input.ownedActorId = 1;
    input.leftStickX = -0.5f;
    input.leftStickY = 0.8f;
    input.buttons.attack = true;
    input.buttons.dodge = true;

    ByteWriter w2;
    write(w2, input);
    ByteReader r2(w2.data());
    InputFrame decoded2;
    read(r2, decoded2);

    check(decoded2.seq == 999, "input seq round-trip");
    check(decoded2.clientTimeMs == 123456789ULL, "input time round-trip");
    check(decoded2.buttons.attack && decoded2.buttons.dodge,
          "input buttons round-trip");
    check(!decoded2.buttons.jump && !decoded2.buttons.guard,
          "input buttons false round-trip");

    // Framed packet round-trip
    auto framed = encode(input);
    const std::uint8_t* payload = nullptr;
    std::size_t payloadSize = 0;
    auto type = decodePacketHeader(framed.data(), framed.size(), payload,
                                   payloadSize);
    check(type == PacketType::InputFrame, "framed packet type");

    ByteReader r3(payload, payloadSize);
    InputFrame decoded3;
    read(r3, decoded3);
    check(decoded3.seq == 999, "framed payload round-trip");

    // Debug string smoke test
    auto dbg = toDebugString(orig);
    check(!dbg.empty(), "debug string not empty");
    std::cout << "  Debug: " << dbg << "\n";

    // Wire format should be explicit little-endian, independent of host ABI.
    ByteWriter endianWriter;
    endianWriter.writeU16(0x1234);
    endianWriter.writeU32(0x89ABCDEF);
    endianWriter.writeI32(-2);
    endianWriter.writeF32(1.0f);
    const auto& endianBytes = endianWriter.data();
    check(endianBytes.size() == 14, "wire format byte count matches");
    check(endianBytes[0] == 0x34 && endianBytes[1] == 0x12,
          "u16 encoded little-endian");
    check(endianBytes[2] == 0xEF && endianBytes[3] == 0xCD &&
              endianBytes[4] == 0xAB && endianBytes[5] == 0x89,
          "u32 encoded little-endian");
    check(endianBytes[6] == 0xFE && endianBytes[7] == 0xFF &&
              endianBytes[8] == 0xFF && endianBytes[9] == 0xFF,
          "i32 encoded little-endian");
    check(endianBytes[10] == 0x00 && endianBytes[11] == 0x00 &&
              endianBytes[12] == 0x80 && endianBytes[13] == 0x3F,
          "f32 encoded IEEE little-endian");

    bool caughtOversizeString = false;
    try {
        ByteWriter oversizedStringWriter;
        oversizedStringWriter.writeString(
            std::string(static_cast<std::size_t>((std::numeric_limits<std::uint16_t>::max)()) + 1U,
                        'x'));
    } catch (const std::runtime_error&) {
        caughtOversizeString = true;
    }
    check(caughtOversizeString, "oversized strings are rejected");
}

static void testReplicaOrdering() {
    std::cout << "\n=== Replica ordering ===\n";

    RecordingGameBridge game;
    ReplicaController controller(game);

    ActorSnapshot playerSnap5;
    playerSnap5.snapshotId = 5;
    playerSnap5.actor.slot = SlotType::Player;
    playerSnap5.actor.position.x = 5.0f;

    ActorSnapshot stalePlayerSnap = playerSnap5;
    stalePlayerSnap.snapshotId = 4;
    stalePlayerSnap.actor.position.x = 4.0f;

    ActorSnapshot friendSnap4;
    friendSnap4.snapshotId = 4;
    friendSnap4.actor.slot = SlotType::Friend1;
    friendSnap4.actor.position.x = 1.0f;

    ActorSnapshot playerSnap6 = playerSnap5;
    playerSnap6.snapshotId = 6;
    playerSnap6.actor.position.x = 6.0f;

    controller.ApplyActorSnapshot(playerSnap5);
    controller.ApplyActorSnapshot(stalePlayerSnap);
    controller.ApplyActorSnapshot(friendSnap4);
    controller.ApplyActorSnapshot(playerSnap6);

    check(game.appliedActors.size() == 3,
          "stale actor snapshots are ignored per slot");
    check(nearlyEqual(game.lastActorBySlot[0].position.x, 6.0f),
          "newer actor snapshot wins for the same slot");
    check(nearlyEqual(game.lastActorBySlot[1].position.x, 1.0f),
          "different slots track ordering independently");

    EnemySnapshot enemySnap7;
    enemySnap7.snapshotId = 7;
    enemySnap7.enemy.netId = 99;
    enemySnap7.enemy.hp = 70;

    EnemySnapshot staleEnemySnap = enemySnap7;
    staleEnemySnap.snapshotId = 6;
    staleEnemySnap.enemy.hp = 60;

    EnemySnapshot enemySnap8 = enemySnap7;
    enemySnap8.snapshotId = 8;
    enemySnap8.enemy.hp = 80;

    controller.ApplyEnemySnapshot(enemySnap7);
    controller.ApplyEnemySnapshot(staleEnemySnap);
    controller.ApplyEnemySnapshot(enemySnap8);

    check(game.appliedEnemies.size() == 2,
          "stale enemy snapshots are ignored per enemy");
    check(game.lastEnemyById[99].hp == 80,
          "newer enemy snapshot wins for the same enemy");
}

static void testHeartbeatTimeout() {
    std::cout << "\n=== Heartbeat timeout ===\n";

    constexpr std::uint32_t kTimeoutMs = 60;
    const std::string build = "heartbeat-build";
    const std::string mod = "heartbeat-mod";

    SessionConfig config;
    config.port = 17783;
    config.maxPeers = 1;
    config.heartbeatTimeoutMs = kTimeoutMs;
    config.pendingPeerTimeoutMs = kTimeoutMs;
    config.gameBuild = build;
    config.contentHash = "heartbeat-content";
    config.modHash = mod;
    config.sessionId = "heartbeat-test";

    SessionHost host(config, {});
    check(host.start(), "heartbeat test server starts");

    std::atomic<int> disconnectCount{0};
    ClientCallbacks callbacks;
    callbacks.onDisconnected = [&disconnectCount]() { ++disconnectCount; };

    NetworkClient client("127.0.0.1", config.port, build, mod, "heartbeat_peer",
                         SlotType::Player, std::move(callbacks),
                         RuntimeMode::CampaignCoop, config.contentHash);
    check(client.connect(), "heartbeat client connect initiated");

    for (int i = 0; i < 100 && host.verifiedPeerCount() < 1; ++i) {
        tickPair(host, client, 1, 5);
    }
    check(host.verifiedPeerCount() == 1, "heartbeat client verified");

    for (int i = 0; i < 3; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        client.sendHeartbeat();
        tickPair(host, client, 2, 5);
    }
    check(host.verifiedPeerCount() == 1,
          "heartbeats keep an idle peer connected");

    std::this_thread::sleep_for(std::chrono::milliseconds(kTimeoutMs + 30));
    tickPair(host, client, 10, 5);

    check(host.verifiedPeerCount() == 0, "idle peer timeout releases the slot");
    check(disconnectCount > 0 || !client.isConnected(),
          "timed-out client observes disconnect");

    client.disconnect();
    tickPair(host, client, 5, 5);
    host.stop();
    check(!host.isRunning(), "heartbeat test server stopped cleanly");
}

// ---------------------------------------------------------------------------
// Network integration test
// ---------------------------------------------------------------------------

static void testNetworkIntegration() {
    std::cout << "\n=== Network integration ===\n";

    const std::string BUILD = "test-build-v1";
    const std::string CONTENT = "test-content-xyz";
    const std::string MOD = "test-mod-abc123";

    // --- Server setup ---
    SessionConfig serverConfig;
    serverConfig.port = 17782; // non-default to avoid conflicts
    serverConfig.maxPeers = 3;
    serverConfig.gameBuild = BUILD;
    serverConfig.contentHash = CONTENT;
    serverConfig.modHash = MOD;
    serverConfig.sessionId = "integration-test";

    std::atomic<int> joinCount{0};
    std::atomic<int> inputCount{0};

    SessionCallbacks serverCb;
    serverCb.onLog = [](const std::string& msg) {
        std::cout << "  " << msg << "\n";
    };
    serverCb.onPeerJoined = [&](const std::string&, SlotType) { ++joinCount; };
    serverCb.onInputReceived = [&](const std::string&, const InputFrame&) {
        ++inputCount;
    };

    SessionHost host(serverConfig, std::move(serverCb));
    check(host.start(), "server starts");

    // --- Client setup ---
    std::atomic<int> sessionStateCount0{0};
    std::atomic<int> sessionStateCount1{0};
    std::atomic<int> sessionStateCount2{0};
    std::atomic<int> actorSnapCount0{0};
    std::atomic<int> actorSnapCount1{0};
    std::atomic<int> actorSnapCount2{0};
    std::atomic<int> eventCount0{0};
    std::atomic<int> eventCount1{0};
    std::atomic<int> eventCount2{0};

    SnapshotTracker tracker0;
    SnapshotTracker tracker1;
    SnapshotTracker tracker2;

    auto makeCb = [](const std::string& name, std::atomic<int>& ssCount,
                     std::atomic<int>& snapCount,
                     std::atomic<int>& evtCount,
                     SnapshotTracker& tracker) {
        ClientCallbacks cb;
        cb.onLog = [name](const std::string& msg) {
            std::cout << "  [" << name << "] " << msg << "\n";
        };
        cb.onSessionState = [&ssCount](const SessionState&) { ++ssCount; };
        cb.onActorSnapshot = [&snapCount, &tracker](const ActorSnapshot& snap) {
            ++snapCount;
            tracker.record(snap);
        };
        cb.onEvent = [&evtCount](const EventMessage&) { ++evtCount; };
        return cb;
    };

    // Each client declares its canonical slot per the design doc:
    // Player A -> Slot 0 (Player), Player B -> Slot 1 (Friend1),
    // Player C -> Slot 2 (Friend2).
    NetworkClient c0("127.0.0.1", 17782, BUILD, MOD, "player_a",
                     SlotType::Player,
                     makeCb("P0", sessionStateCount0, actorSnapCount0,
                             eventCount0, tracker0),
                     RuntimeMode::CampaignCoop, CONTENT);
    NetworkClient c1("127.0.0.1", 17782, BUILD, MOD, "player_b",
                     SlotType::Friend1,
                     makeCb("P1", sessionStateCount1, actorSnapCount1,
                             eventCount1, tracker1),
                     RuntimeMode::CampaignCoop, CONTENT);
    NetworkClient c2("127.0.0.1", 17782, BUILD, MOD, "player_c",
                     SlotType::Friend2,
                     makeCb("P2", sessionStateCount2, actorSnapCount2,
                             eventCount2, tracker2),
                     RuntimeMode::CampaignCoop, CONTENT);

    // --- Connect all three ---
    check(c0.connect(), "client 0 connect initiated");
    check(c1.connect(), "client 1 connect initiated");
    check(c2.connect(), "client 2 connect initiated");

    // Tick until all three are verified.
    for (int i = 0; i < 100 && joinCount < 3; ++i) {
        tickAll(host, c0, c1, c2, 1);
    }
    check(joinCount == 3, "all 3 peers joined");
    check(host.verifiedPeerCount() == 3, "host sees 3 verified peers");
    check(host.sessionState().actors.size() == 3,
          "session has 3 actors");

    // Verify canonical slot assignments match the design doc:
    // player_a -> Player (0), player_b -> Friend1 (1), player_c -> Friend2 (2)
    {
        const auto& actors = host.sessionState().actors;
        bool foundA = false, foundB = false, foundC = false;
        for (const auto& a : actors) {
            if (a.ownerPeerId == "player_a") {
                check(a.slot == SlotType::Player,
                      "player_a assigned to slot 0 (Player)");
                foundA = true;
            } else if (a.ownerPeerId == "player_b") {
                check(a.slot == SlotType::Friend1,
                      "player_b assigned to slot 1 (Friend1)");
                foundB = true;
            } else if (a.ownerPeerId == "player_c") {
                check(a.slot == SlotType::Friend2,
                      "player_c assigned to slot 2 (Friend2)");
                foundC = true;
            }
        }
        check(foundA && foundB && foundC, "all 3 peers have canonical slots");
    }

    // Each client should have received at least one SessionState.
    check(sessionStateCount0 > 0, "client 0 got session state");
    check(sessionStateCount1 > 0, "client 1 got session state");
    check(sessionStateCount2 > 0, "client 2 got session state");

    // --- Run authoritative simulation for 60 ticks ---
    constexpr int kSimFrames = 60;
    constexpr float kExpectedDistance = 4.5f;

    SimulationState sim;

    for (std::uint32_t seq = 1; seq <= static_cast<std::uint32_t>(kSimFrames);
         ++seq) {
        InputFrame moveRight;
        moveRight.seq = seq;
        moveRight.ownedActorId = 0;
        moveRight.leftStickX = 1.0f;
        c0.sendInput(moveRight);

        InputFrame idle;
        idle.seq = seq;
        idle.ownedActorId = 1;
        c1.sendInput(idle);

        idle.ownedActorId = 2;
        c2.sendInput(idle);

        tickAuthoritativeFrame(host, sim, c0, c1, c2);
    }

    check(inputCount >= kSimFrames, "host received authoritative inputs");
    check(sim.snapshotId() == static_cast<std::uint32_t>(kSimFrames),
          "simulation ticked 60 frames");
    check(sim.actors()[0].position.x >= kExpectedDistance,
          "actor 0 moved right in authoritative sim");
    check(nearlyEqual(sim.actors()[1].position.x, 0.0f),
          "actor 1 stayed in place in authoritative sim");
    check(nearlyEqual(sim.actors()[2].position.x, 0.0f),
          "actor 2 stayed in place in authoritative sim");

    check(actorSnapCount0 >= kSimFrames, "client 0 received authoritative snapshots");
    check(actorSnapCount1 >= kSimFrames, "client 1 received authoritative snapshots");
    check(actorSnapCount2 >= kSimFrames, "client 2 received authoritative snapshots");

    const std::uint32_t commonSnapshotId =
        findHighestCommonSnapshotId(tracker0, tracker1, tracker2);
    check(commonSnapshotId >= 50, "all clients share a late authoritative snapshot");

    const auto* common0 = tracker0.find(commonSnapshotId);
    const auto* common1 = tracker1.find(commonSnapshotId);
    const auto* common2 = tracker2.find(commonSnapshotId);
    check(common0 != nullptr && common1 != nullptr && common2 != nullptr,
          "common snapshot data exists for all clients");

    if (common0 != nullptr && common1 != nullptr && common2 != nullptr) {
        for (std::size_t actorIndex = 0; actorIndex < 3; ++actorIndex) {
            const auto& actor0 = common0->actors[actorIndex].actor;
            const auto& actor1 = common1->actors[actorIndex].actor;
            const auto& actor2 = common2->actors[actorIndex].actor;

            check(nearlyEqual(actor0.position.x, actor1.position.x) &&
                      nearlyEqual(actor1.position.x, actor2.position.x),
                  "authoritative actor x positions match across clients");
            check(nearlyEqual(actor0.position.y, actor1.position.y) &&
                      nearlyEqual(actor1.position.y, actor2.position.y),
                  "authoritative actor y positions match across clients");
            check(nearlyEqual(actor0.position.z, actor1.position.z) &&
                      nearlyEqual(actor1.position.z, actor2.position.z),
                  "authoritative actor z positions match across clients");
        }

        check(common0->actors[0].actor.position.x > 0.0f,
              "shared snapshot shows actor 0 moved right");
        check(nearlyEqual(common0->actors[1].actor.position.x, 0.0f),
              "shared snapshot shows actor 1 idle");
        check(nearlyEqual(common0->actors[2].actor.position.x, 0.0f),
              "shared snapshot shows actor 2 idle");
    }

    // --- Broadcast a reliable event ---
    EventMessage evt;
    evt.snapshotId = sim.snapshotId();
    evt.type = EventType::SpawnGroup;
    evt.payloadJson = R"({"group":1,"count":5})";
    host.broadcastEvent(evt);

    for (int i = 0; i < 50; ++i) {
        tickAll(host, c0, c1, c2, 1);
    }
    check(eventCount0 > 0 && eventCount1 > 0 && eventCount2 > 0,
          "clients received reliable event");

    // --- Version mismatch rejection ---
    std::atomic<int> ignoredSessionStateCount{0};
    std::atomic<int> ignoredActorSnapCount{0};
    std::atomic<int> ignoredEventCount{0};
    SnapshotTracker ignoredTracker;

    NetworkClient badClient("127.0.0.1", 17782, "wrong-build", "wrong-mod",
                            "hacker", SlotType::Player,
                            makeCb("BAD", ignoredSessionStateCount,
                                   ignoredActorSnapCount, ignoredEventCount,
                                   ignoredTracker),
                            RuntimeMode::CampaignCoop, "wrong-content");
    badClient.connect();
    for (int i = 0; i < 50; ++i) {
        host.tick(5);
        badClient.tick(5);
    }
    check(!badClient.isConnected() || host.verifiedPeerCount() == 3,
          "mismatched client rejected or not verified");
    badClient.disconnect();

    NetworkClient badProtocolClient("127.0.0.1", 17782, BUILD, MOD,
                                    "proto_skew", SlotType::Player,
                                    makeCb("PROTO", ignoredSessionStateCount,
                                           ignoredActorSnapCount, ignoredEventCount,
                                           ignoredTracker),
                                    RuntimeMode::CampaignCoop, CONTENT, 99);
    badProtocolClient.connect();
    for (int i = 0; i < 50; ++i) {
        host.tick(5);
        badProtocolClient.tick(5);
    }
    check(!badProtocolClient.isConnected() || host.verifiedPeerCount() == 3,
          "protocol-mismatched client rejected or not verified");
    badProtocolClient.disconnect();

    NetworkClient badModeClient("127.0.0.1", 17782, BUILD, MOD,
                                "realm_client", SlotType::Player,
                                makeCb("MODE", ignoredSessionStateCount,
                                       ignoredActorSnapCount, ignoredEventCount,
                                       ignoredTracker),
                                RuntimeMode::PublicRealm, CONTENT);
    badModeClient.connect();
    for (int i = 0; i < 50; ++i) {
        host.tick(5);
        badModeClient.tick(5);
    }
    check(!badModeClient.isConnected() || host.verifiedPeerCount() == 3,
          "mode-mismatched client rejected or not verified");
    badModeClient.disconnect();

    // --- Duplicate slot rejection ---
    // A client requesting slot 0 (Player) should be rejected because
    // player_a already owns that slot.
    NetworkClient dupeClient("127.0.0.1", 17782, BUILD, MOD,
                             "slot_thief", SlotType::Player,
                             makeCb("DUPE", ignoredSessionStateCount,
                                     ignoredActorSnapCount, ignoredEventCount,
                                     ignoredTracker),
                             RuntimeMode::CampaignCoop, CONTENT);
    dupeClient.connect();
    for (int i = 0; i < 50; ++i) {
        host.tick(5);
        dupeClient.tick(5);
    }
    check(host.verifiedPeerCount() == 3,
          "duplicate slot client rejected (still 3 verified)");
    dupeClient.disconnect();

    // --- No-preference slot assignment ---
    c2.disconnect();
    for (int i = 0; i < 20; ++i) {
        host.tick(5);
        c2.tick(5);
    }
    check(host.verifiedPeerCount() == 2,
          "slot 2 released after disconnecting player_c");

    std::atomic<int> autoJoinSessionCount{0};
    std::atomic<int> autoJoinActorCount{0};
    std::atomic<int> autoJoinEventCount{0};
    SnapshotTracker autoJoinTracker;
    NetworkClient autoSlotClient("127.0.0.1", 17782, BUILD, MOD,
                                 "auto_slot", std::nullopt,
                                 makeCb("AUTO", autoJoinSessionCount,
                                        autoJoinActorCount, autoJoinEventCount,
                                        autoJoinTracker),
                                 RuntimeMode::CampaignCoop, CONTENT);
    autoSlotClient.connect();
    for (int i = 0; i < 50; ++i) {
        host.tick(5);
        autoSlotClient.tick(5);
    }
    check(host.verifiedPeerCount() == 3,
          "no-preference client accepted when a slot is free");
    {
        bool foundAuto = false;
        for (const auto& actor : host.sessionState().actors) {
            if (actor.ownerPeerId == "auto_slot") {
                check(actor.slot == SlotType::Friend2,
                      "no-preference client assigned next free slot");
                foundAuto = true;
            }
        }
        check(foundAuto, "auto-slot client appears in session state");
    }
    autoSlotClient.disconnect();

    // --- Disconnect ---
    c0.disconnect();
    c1.disconnect();

    for (int i = 0; i < 20; ++i) {
        host.tick(5);
    }

    host.stop();
    check(!host.isRunning(), "server stopped cleanly");
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main() {
    std::cout << "kh2coop — Fake Simulation Test Harness\n";
    std::cout << "=======================================\n";

    if (enet_initialize() != 0) {
        std::cerr << "Failed to initialize ENet.\n";
        return 1;
    }

    testCodecRoundtrip();
    testReplicaOrdering();
    testNetworkIntegration();
    testHeartbeatTimeout();

    enet_deinitialize();

    std::cout << "\n=======================================\n";
    if (g_errors == 0) {
        std::cout << "ALL CHECKS PASSED\n";
    } else {
        std::cout << g_errors << " CHECK(S) FAILED\n";
    }
    return g_errors == 0 ? 0 : 1;
}
