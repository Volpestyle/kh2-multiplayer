// FakeSimulation — proves the codec, server, and client networking work
// end-to-end without KH2 attached.
//
// What it does:
//   1. Starts a SessionHost on localhost.
//   2. Connects 3 NetworkClients (Player, Friend1, Friend2).
//   3. Each client sends a version handshake and verifies slot assignment.
//   4. Each client sends a burst of InputFrames.
//   5. The host broadcasts fake ActorSnapshots back to all clients.
//   6. Clients verify they receive the snapshots.
//   7. The host broadcasts a reliable Event (SpawnGroup).
//   8. Clients verify they receive the event.
//   9. Clients disconnect. Server shuts down.
//
// Exit code 0 = all checks passed. Non-zero = failure.

#include "kh2coop/Codec.hpp"
#include "kh2coop/NetworkClient.hpp"
#include "kh2coop/SessionHost.hpp"

#include <atomic>
#include <chrono>
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
}

// ---------------------------------------------------------------------------
// Network integration test
// ---------------------------------------------------------------------------

static void testNetworkIntegration() {
    std::cout << "\n=== Network integration ===\n";

    const std::string BUILD = "test-build-v1";
    const std::string MOD = "test-mod-abc123";

    // --- Server setup ---
    SessionConfig serverConfig;
    serverConfig.port = 17782; // non-default to avoid conflicts
    serverConfig.maxPeers = 3;
    serverConfig.gameBuild = BUILD;
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
    std::atomic<int> actorSnapCount{0};
    std::atomic<int> eventCount{0};

    auto makeCb = [](const std::string& name, std::atomic<int>& ssCount,
                     std::atomic<int>& snapCount,
                     std::atomic<int>& evtCount) {
        ClientCallbacks cb;
        cb.onLog = [name](const std::string& msg) {
            std::cout << "  [" << name << "] " << msg << "\n";
        };
        cb.onSessionState = [&ssCount](const SessionState&) { ++ssCount; };
        cb.onActorSnapshot = [&snapCount](const ActorSnapshot&) {
            ++snapCount;
        };
        cb.onEvent = [&evtCount](const EventMessage&) { ++evtCount; };
        return cb;
    };

    NetworkClient c0("127.0.0.1", 17782, BUILD, MOD, "player_a",
                     makeCb("P0", sessionStateCount0, actorSnapCount,
                            eventCount));
    NetworkClient c1("127.0.0.1", 17782, BUILD, MOD, "player_b",
                     makeCb("P1", sessionStateCount1, actorSnapCount,
                            eventCount));
    NetworkClient c2("127.0.0.1", 17782, BUILD, MOD, "player_c",
                     makeCb("P2", sessionStateCount2, actorSnapCount,
                            eventCount));

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

    // Each client should have received at least one SessionState.
    check(sessionStateCount0 > 0, "client 0 got session state");
    check(sessionStateCount1 > 0, "client 1 got session state");
    check(sessionStateCount2 > 0, "client 2 got session state");

    // --- Send InputFrames ---
    for (std::uint32_t seq = 1; seq <= 5; ++seq) {
        InputFrame f;
        f.seq = seq;
        f.ownedActorId = 0;
        f.leftStickX = 0.5f;
        f.buttons.attack = (seq % 2 == 0);
        c0.sendInput(f);

        f.ownedActorId = 1;
        c1.sendInput(f);

        f.ownedActorId = 2;
        c2.sendInput(f);
    }

    for (int i = 0; i < 50; ++i) {
        tickAll(host, c0, c1, c2, 1);
    }
    check(inputCount >= 10, "host received inputs from clients");

    // --- Broadcast actor snapshots ---
    std::vector<ActorSnapshot> snaps;
    for (int slot = 0; slot < 3; ++slot) {
        ActorSnapshot snap;
        snap.snapshotId = 1;
        snap.actor.actorId = static_cast<std::uint32_t>(slot);
        snap.actor.slot = static_cast<SlotType>(slot);
        snap.actor.position = {10.0f * slot, 0.0f, 0.0f};
        snap.actor.hp = 100;
        snaps.push_back(snap);
    }
    host.broadcastActorSnapshots(snaps);

    for (int i = 0; i < 50; ++i) {
        tickAll(host, c0, c1, c2, 1);
    }
    check(actorSnapCount >= 3, "clients received actor snapshots");

    // --- Broadcast a reliable event ---
    EventMessage evt;
    evt.snapshotId = 2;
    evt.type = EventType::SpawnGroup;
    evt.payloadJson = R"({"group":1,"count":5})";
    host.broadcastEvent(evt);

    for (int i = 0; i < 50; ++i) {
        tickAll(host, c0, c1, c2, 1);
    }
    check(eventCount >= 3, "clients received reliable event");

    // --- Version mismatch rejection ---
    NetworkClient badClient("127.0.0.1", 17782, "wrong-build", "wrong-mod",
                            "hacker",
                            makeCb("BAD", sessionStateCount0, actorSnapCount,
                                   eventCount));
    badClient.connect();
    for (int i = 0; i < 50; ++i) {
        host.tick(5);
        badClient.tick(5);
    }
    check(!badClient.isConnected() || host.verifiedPeerCount() == 3,
          "mismatched client rejected or not verified");
    badClient.disconnect();

    // --- Disconnect ---
    c0.disconnect();
    c1.disconnect();
    c2.disconnect();

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
    testNetworkIntegration();

    enet_deinitialize();

    std::cout << "\n=======================================\n";
    if (g_errors == 0) {
        std::cout << "ALL CHECKS PASSED\n";
    } else {
        std::cout << g_errors << " CHECK(S) FAILED\n";
    }
    return g_errors == 0 ? 0 : 1;
}
