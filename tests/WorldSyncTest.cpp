// WorldSyncTest — host-authored world messages through the relay (offline
// half of VUH-1495/1496/1498/1502/1503), no KH2.
//
// Checks: only the host (slot Player) can author room transitions, cutscene
// holds and enemy state; the relay forwards them to everyone else; a hit
// claim reaches only the host with the sender's real slot; a late joiner is
// caught up on room, hold, enemies, HP and deaths; transition acks are
// recorded per peer; the host leaving ends the world.
// Exit code 0 = all checks passed.

#include "kh2coop/Codec.hpp"
#include "kh2coop/NetworkClient.hpp"
#include "kh2coop/ProgressAllowList.hpp"
#include "kh2coop/ProgressMirror.hpp"
#include "kh2coop/SessionHost.hpp"

#include <enet/enet.h>

#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

using namespace kh2coop;

namespace {

int g_errors = 0;

void check(bool ok, const std::string& what) {
    std::cout << (ok ? "  PASS: " : "  FAIL: ") << what << "\n";
    if (!ok) ++g_errors;
}

std::uint64_t nowMs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

struct Seen {
    std::vector<RoomTransition> rooms;
    std::vector<EventHold> holds;
    std::vector<EnemyManifest> manifests;
    std::vector<EnemyHp> hps;
    std::vector<EnemyDeath> deaths;
    std::vector<HitClaim> claims;
    std::vector<ProgressUpdate> progress;
};

ClientCallbacks callbacksFor(Seen& seen) {
    ClientCallbacks cb;
    cb.onRoomTransition = [&seen](const RoomTransition& m) { seen.rooms.push_back(m); };
    cb.onEventHold = [&seen](const EventHold& m) { seen.holds.push_back(m); };
    cb.onEnemyManifest = [&seen](const EnemyManifest& m) { seen.manifests.push_back(m); };
    cb.onEnemyHp = [&seen](const EnemyHp& m) { seen.hps.push_back(m); };
    cb.onEnemyDeath = [&seen](const EnemyDeath& m) { seen.deaths.push_back(m); };
    cb.onHitClaim = [&seen](const HitClaim& m) { seen.claims.push_back(m); };
    cb.onProgressUpdate = [&seen](const ProgressUpdate& m) { seen.progress.push_back(m); };
    return cb;
}

EnemyManifestEntry entry(std::uint16_t netId, std::uint16_t spawn, std::uint32_t obj) {
    EnemyManifestEntry e;
    e.netId = netId;
    e.battleProgram = 3;
    e.spawnIndex = spawn;
    e.objectId = obj;
    e.spawnPosition = {100.0f * spawn, 0.0f, 50.0f};
    return e;
}

void testProgressMirror() {
    std::cout << "\n=== ProgressMirror ===\n";
    // Allow two story ranges; keep 0x2500.. (character stats) off the list.
    ProgressMirror host({{0x1C00, 0x40}, {0x1D20, 0x10}});
    std::vector<std::uint8_t> before(0x3000, 0), after(0x3000, 0);
    after[0x1C05] = 1; after[0x1C06] = 2;  // contiguous -> one span
    after[0x1D2E] = 9;                     // second range
    after[0x2500] = 99;                    // stats: not allowed
    const auto spans = host.diff(before.data(), after.data(), after.size());
    check(spans.size() == 2 && spans[0].offset == 0x1C05 && spans[0].bytes.size() == 2 &&
              spans[1].offset == 0x1D2E && spans[1].bytes[0] == 9,
          "diff coalesces changes and ignores bytes outside the allow list");

    ProgressMirror client({{0x1C00, 0x40}, {0x1D20, 0x10}});
    ProgressUpdate u {1, false, spans};
    u.spans.push_back({0x2500, {99}}); // a hostile/malformed span
    check(client.accept(u) == 1 && client.desiredSize() == 3,
          "client keeps allowed bytes and rejects the stats byte");

    std::vector<std::uint8_t> live(0x3000, 0);
    auto todo = client.pending(live.data(), live.size());
    check(todo.size() == 2, "pending lists both spans before they're written");
    for (const auto& sp : todo) {
        for (std::size_t i = 0; i < sp.bytes.size(); ++i) live[sp.offset + i] = sp.bytes[i];
    }
    check(client.pending(live.data(), live.size()).empty() && live[0x2500] == 0,
          "after applying, nothing is pending and stats were never touched");
    live[0x1C06] = 0; // the game reloaded the room and reset a flag
    todo = client.pending(live.data(), live.size());
    check(todo.size() == 1 && todo[0].offset == 0x1C06 && todo[0].bytes[0] == 2,
          "re-assert finds the flag the game reset");

    std::vector<ProgressSpan> big {{0, std::vector<std::uint8_t>(130000, 7)}};
    const auto parts = splitProgressUpdate(3, true, big);
    std::size_t total = 0;
    bool fits = true;
    for (const auto& part : parts) {
        for (const auto& sp : part.spans) total += sp.bytes.size();
        fits &= encode(part).size() < 65535;
    }
    check(parts.size() >= 3 && parts[0].full && !parts[1].full && total == 130000 && fits,
          "large snapshots split under the packet limit, first part replaces");

    // Policy check on the candidate allow list (D8: per-player state stays off).
    const auto allow = candidateProgressAllowList();
    bool sorted = true;
    for (std::size_t i = 1; i < allow.size(); ++i) {
        sorted &= allow[i - 1].offset + allow[i - 1].length <= allow[i].offset;
    }
    ProgressMirror policy(allow);
    check(sorted && !policy.allowed(0x24F8) && !policy.allowed(0x353C) &&
              !policy.allowed(0x3580) && policy.allowed(0x1CFF) && policy.allowed(0x0010),
          "candidate allow list: no overlaps; stats/party/inventory excluded; story included");
}

} // namespace

int main() {
    if (enet_initialize() != 0) return 2;
    testProgressMirror();

    SessionConfig cfg;
    cfg.port = 17796;
    cfg.maxPeers = 3;
    cfg.gameBuild = "world-build";
    cfg.contentHash = "world-content";
    cfg.modHash = "world-mod";
    cfg.sessionId = "world-test";
    SessionHost relay(cfg, {});
    check(relay.start(), "relay starts");

    Seen hostSeen, c1Seen, c2Seen;
    auto makeClient = [&](const char* id, SlotType slot, Seen& seen) {
        auto c = std::make_unique<NetworkClient>("127.0.0.1", cfg.port, cfg.gameBuild,
                                                 cfg.modHash, id, slot, callbacksFor(seen),
                                                 RuntimeMode::CampaignCoop, cfg.contentHash);
        c->connect();
        return c;
    };
    std::vector<NetworkClient*> live;
    auto pump = [&](int ms) {
        const auto end = nowMs() + ms;
        while (nowMs() < end) {
            relay.tick(0);
            for (auto* c : live) c->tick(0);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };
    auto waitFor = [&](const std::function<bool()>& cond, int timeoutMs = 3000) {
        const auto end = nowMs() + timeoutMs;
        while (nowMs() < end && !cond()) pump(5);
        return cond();
    };

    auto host = makeClient("host", SlotType::Player, hostSeen);
    auto c1 = makeClient("c1", SlotType::Friend1, c1Seen);
    live = {host.get(), c1.get()};
    check(waitFor([&] { return relay.verifiedPeerCount() == 2; }), "host + client verified");

    std::cout << "\n=== Room transitions ===\n";
    RoomTransition goa {1, 4, 0x1A, 2, 0x10, 0x11, 0x00};
    host->sendRoomTransition(goa);
    check(waitFor([&] { return !c1Seen.rooms.empty(); }) &&
              c1Seen.rooms.back().epoch == 1 && c1Seen.rooms.back().roomId == 0x1A &&
              c1Seen.rooms.back().door == 2 && c1Seen.rooms.back().battleProgram == 0x11,
          "client receives the host's transition with all fields");
    check(hostSeen.rooms.empty(), "host does not get its own transition back");

    RoomTransition forged {9, 2, 1, 0, 0, 0, 0};
    c1->sendRoomTransition(forged);
    pump(200);
    check(hostSeen.rooms.empty() && relay.rejectedWorldMessages() >= 1 &&
              relay.currentRoom() && relay.currentRoom()->epoch == 1,
          "a non-host transition is dropped; the relay keeps the host's room");

    std::cout << "\n=== Enemies ===\n";
    EnemyManifest wave1 {1, true, {entry(1, 0, 0x210), entry(2, 1, 0x210), entry(3, 2, 0x2A0)}};
    host->sendEnemyManifest(wave1);
    check(waitFor([&] { return !c1Seen.manifests.empty(); }) &&
              c1Seen.manifests.back().entries.size() == 3 &&
              c1Seen.manifests.back().entries[2].objectId == 0x2A0 &&
              c1Seen.manifests.back().entries[1].spawnPosition.x == 100.0f,
          "client receives the manifest with keys and spawn positions");
    EnemyManifest wave2 {1, false, {entry(4, 3, 0x210)}};
    host->sendEnemyManifest(wave2);
    check(waitFor([&] { return c1Seen.manifests.size() == 2; }) &&
              !c1Seen.manifests.back().replace && relay.manifestSize() == 4,
          "a later wave appends (relay tracks 4 enemies)");

    host->sendEnemyHp(EnemyHp {1, {{1, 40, 120}, {2, 120, 120}}});
    check(waitFor([&] { return !c1Seen.hps.empty(); }) &&
              c1Seen.hps.back().entries[0].hp == 40 &&
              c1Seen.hps.back().entries[0].maxHp == 120,
          "client receives absolute enemy HP");
    host->sendEnemyDeath(EnemyDeath {1, 2});
    check(waitFor([&] { return !c1Seen.deaths.empty(); }) && c1Seen.deaths.back().netId == 2,
          "client receives an enemy death");
    c1->sendEnemyDeath(EnemyDeath {1, 3});
    pump(200);
    check(hostSeen.deaths.empty(), "a non-host enemy death is dropped");

    std::cout << "\n=== Hit claims ===\n";
    HitClaim claim;
    claim.epoch = 1;
    claim.seq = 1;
    claim.netId = 1;
    claim.attackId = 0x41;
    claim.damage = 25;
    claim.attackerPosition = {10.0f, 0.0f, 20.0f};
    claim.attackerSlot = SlotType::Friend2; // lie: the relay must stamp Friend1
    c1->sendHitClaim(claim);
    check(waitFor([&] { return !hostSeen.claims.empty(); }) &&
              hostSeen.claims.back().attackerSlot == SlotType::Friend1 &&
              hostSeen.claims.back().damage == 25 &&
              hostSeen.claims.back().attackId == 0x41,
          "the host receives the claim stamped with the real attacker slot");
    check(c1Seen.claims.empty(), "the claimant does not get its claim echoed");

    std::cout << "\n=== Cutscene hold ===\n";
    host->sendEventHold(EventHold {1, true, 0x33});
    check(waitFor([&] { return !c1Seen.holds.empty(); }) && c1Seen.holds.back().active &&
              c1Seen.holds.back().eventProgram == 0x33,
          "client receives the host's cutscene hold");

    std::cout << "\n=== Progress ===\n";
    host->sendProgressUpdate(ProgressUpdate {1, true, {{0x1CFF, {8}}, {0x1D2E, {1, 2}}}});
    host->sendProgressUpdate(ProgressUpdate {2, false, {{0x1D2F, {3}}}});
    check(waitFor([&] { return c1Seen.progress.size() == 2; }) &&
              c1Seen.progress[0].full && c1Seen.progress[1].spans[0].bytes[0] == 3 &&
              relay.progressBytes() == 3,
          "client receives full + delta progress; relay merges 3 bytes");
    c1->sendProgressUpdate(ProgressUpdate {9, true, {{0x1CFF, {0}}}});
    pump(200);
    check(hostSeen.progress.empty() && relay.progressBytes() == 3,
          "a non-host progress update is dropped");

    std::cout << "\n=== Late joiner ===\n";
    auto c2 = makeClient("c2", SlotType::Friend2, c2Seen);
    live = {host.get(), c1.get(), c2.get()};
    check(waitFor([&] {
              return !c2Seen.rooms.empty() && !c2Seen.holds.empty() &&
                     !c2Seen.manifests.empty() && !c2Seen.hps.empty() &&
                     !c2Seen.deaths.empty();
          }),
          "late joiner receives room, hold, manifest, HP and deaths");
    check(!c2Seen.rooms.empty() && c2Seen.rooms.back().epoch == 1 &&
              c2Seen.rooms.back().roomId == 0x1A,
          "late joiner's room is the host's current room");
    check(!c2Seen.manifests.empty() && c2Seen.manifests.back().replace &&
              c2Seen.manifests.back().entries.size() == 4,
          "late joiner gets the full 4-enemy set as one replace");
    check(!c2Seen.deaths.empty() && c2Seen.deaths.back().netId == 2,
          "late joiner learns enemy 2 is already dead");
    {
        ProgressMirror joiner({{0x1C00, 0x200}});
        for (const auto& u : c2Seen.progress) joiner.accept(u);
        std::vector<std::uint8_t> live(0x2000, 0);
        const auto todo = joiner.pending(live.data(), live.size());
        check(!c2Seen.progress.empty() && c2Seen.progress[0].full && todo.size() == 2 &&
                  todo[1].offset == 0x1D2E && todo[1].bytes.size() == 2 &&
                  todo[1].bytes[1] == 3,
              "late joiner gets the merged progress (delta applied) as a full update");
    }

    std::cout << "\n=== Transition acks ===\n";
    c1->sendTransitionAck(TransitionAck {1, 4, 0x1A, true});
    c2->sendTransitionAck(TransitionAck {1, 4, 0x05, true}); // diverged
    pump(300);
    const auto* p1 = relay.peerBySlot(SlotType::Friend1);
    const auto* p2 = relay.peerBySlot(SlotType::Friend2);
    check(p1 && p1->ackEpoch == 1 && p1->ackRoomId == 0x1A && p1->ackArrived,
          "relay records client 1 arrived in the host's room");
    check(p2 && p2->ackRoomId == 0x05, "relay records client 2's diverged room");

    std::cout << "\n=== New room ===\n";
    host->sendRoomTransition(RoomTransition {2, 4, 0x1B, 1, 0, 0, 0});
    check(waitFor([&] { return c1Seen.rooms.size() == 2; }) && relay.manifestSize() == 0,
          "a new transition clears the cached enemy set");

    std::cout << "\n=== Host leaves ===\n";
    host->disconnect();
    live = {c1.get(), c2.get()};
    check(waitFor([&] { return !relay.currentRoom().has_value(); }),
          "host leaving ends the world (no host migration)");

    c1->disconnect();
    c2->disconnect();
    relay.stop();
    enet_deinitialize();

    std::cout << "\n=======================================\n"
              << (g_errors == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED: " + std::to_string(g_errors))
              << "\n";
    return g_errors == 0 ? 0 : 1;
}
