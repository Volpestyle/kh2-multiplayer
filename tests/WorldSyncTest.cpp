// WorldSyncTest — host-authored world messages through the relay (offline
// half of VUH-1495/1496/1498/1502/1503), no KH2.
//
// Checks: only the host (slot Player) can author room transitions, cutscene
// holds and enemy state; the relay forwards them to everyone else; a hit
// claim reaches only the host with the sender's real slot; a late joiner is
// caught up on room, hold, enemies, HP and deaths; transition acks are
// recorded per peer; the host leaving ends the world.
// Exit code 0 = all checks passed.

#include "kh2coop/AppliedStateHash.hpp"
#include "kh2coop/Codec.hpp"
#include "WorldWireFixture.hpp"
#include "kh2coop/ProgressAllowList.hpp"
#include "kh2coop/ProgressMirror.hpp"
#include "kh2coop/SessionHost.hpp"

#include <enet/enet.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <map>
#include <limits>
#include <optional>
#include <thread>
#include <vector>
#include <fstream>
#include <sstream>

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
    std::vector<WorldEnvelope> envelopes;
    unsigned disconnects = 0;
    std::vector<SessionState> sessions;
    std::vector<RoomTransition> rooms;
    std::vector<EventHold> holds;
    std::vector<EnemyManifest> manifests;
    std::vector<EnemyHp> hps;
    std::vector<EnemyDeath> deaths;
    std::vector<HitClaim> claims;
    std::vector<ProgressUpdate> progress;
    std::vector<DesyncNotice> desyncs;
    std::vector<ActivationRequest> activationRequests;
    std::vector<HostActivationPoint> activationPoints;
};

ClientCallbacks callbacksFor(Seen& seen) {
    ClientCallbacks cb;
    cb.onWorldEnvelope = [&seen](const auto& e){ seen.envelopes.push_back(e); };
    cb.onDisconnected = [&seen] { ++seen.disconnects; };
    cb.onSessionState = [&seen](const SessionState& m) { seen.sessions.push_back(m); };
    cb.onRoomTransition = [&seen](const RoomTransition& m) { seen.rooms.push_back(m); };
    cb.onEventHold = [&seen](const EventHold& m) { seen.holds.push_back(m); };
    cb.onEnemyManifest = [&seen](const EnemyManifest& m) { seen.manifests.push_back(m); };
    cb.onEnemyHp = [&seen](const EnemyHp& m) { seen.hps.push_back(m); };
    cb.onEnemyDeath = [&seen](const EnemyDeath& m) { seen.deaths.push_back(m); };
    cb.onHitClaim = [&seen](const HitClaim& m) { seen.claims.push_back(m); };
    cb.onProgressUpdate = [&seen](const ProgressUpdate& m) { seen.progress.push_back(m); };
    cb.onDesyncNotice = [&seen](const DesyncNotice& m) { seen.desyncs.push_back(m); };
    cb.onActivationRequest = [&seen](const ActivationRequest& m) { seen.activationRequests.push_back(m); };
    cb.onHostActivationPoint = [&seen](const HostActivationPoint& m) { seen.activationPoints.push_back(m); };
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

void testHitClaimCodec() {
    HitClaim claim;
    claim.epoch = 17; claim.seq = 0x12345678; claim.netId = 12; claim.objectId = 309;
    claim.requesterConnectionId = 0x123456789ABCDEF0ULL;
    claim.attackId = 0x41; claim.damage = 23; claim.attackerPosition = {1.25f, -2, 3};
    claim.attackerSlot = SlotType::Friend2;
    const auto packet = encode(claim);
    const std::uint8_t* payload = nullptr;
    std::size_t size = 0;
    check(decodePacketHeader(packet.data(), packet.size(), payload, size) == PacketType::HitClaim && size == 43,
          "v3 hit claim has exactly 43 payload bytes");
    ByteReader reader(payload, size);
    HitClaim decoded;
    read(reader, decoded);
    check(reader.atEnd() && decoded.epoch == claim.epoch && decoded.seq == claim.seq &&
              decoded.netId == claim.netId && decoded.objectId == claim.objectId &&
              decoded.requesterConnectionId == claim.requesterConnectionId && decoded.attackId == claim.attackId &&
              decoded.damage == claim.damage && decoded.attackerPosition.x == claim.attackerPosition.x &&
              decoded.attackerPosition.y == claim.attackerPosition.y && decoded.attackerPosition.z == claim.attackerPosition.z &&
              decoded.attackerSlot == claim.attackerSlot, "typed connection-scoped hit claim roundtrips exactly");
    const auto rejects = [&](const std::vector<std::uint8_t>& bytes) {
        try {
            const std::uint8_t* data = nullptr;
            std::size_t length = 0;
            decodePacketHeader(bytes.data(), bytes.size(), data, length);
            ByteReader r(data, length);
            HitClaim m;
            read(r, m);
            return false;
        } catch (const std::exception&) { return true; }
    };
    check(rejects(encodePacket(PacketType::HitClaim, std::vector<std::uint8_t>(31))),
          "old v2 hit-claim payload cannot decode as v3");
    auto malformed = packet;
    malformed.pop_back();
    check(rejects(malformed), "truncated v3 claim is rejected");
    malformed = packet;
    malformed.push_back(0);
    check(rejects(malformed), "trailing frame bytes are rejected");
    malformed[1] = 44;
    check(rejects(malformed), "trailing declared payload bytes are rejected");
}

void testActivationCodec() {
    const ActivationRequest request {{7, 5, 6, 0, 1, 1, 0}, {12, 34}, 56, 1};
    const HostActivationPoint point {request, 78, {12.5f, -260.0f, 1950.0f, 1.0f}};
    const auto packet = encode(point);
    validateActivationPacket(packet);
    const std::uint8_t* payload = nullptr;
    std::size_t size = 0;
    decodePacketHeader(packet.data(), packet.size(), payload, size);
    HostActivationPoint decoded;
    ByteReader reader(payload, size);
    read(reader, decoded);
    check(packet.size() == 68 && encode(request).size() == 44 &&
              decoded.request.incarnation == request.incarnation &&
              decoded.request.requestSeq == request.requestSeq && decoded.sourceSeq == 78 &&
              decoded.position == point.position && decoded.request.location.eventProgram == 0,
          "activation fixed schema preserves challenge identity and native float4");
    const auto rejects = [](const std::vector<std::uint8_t>& bytes) {
        try { validateActivationPacket(bytes); } catch (const std::exception&) { return true; }
        return false;
    };
    auto truncated = packet;
    truncated.pop_back();
    auto trailing = packet;
    trailing.push_back(0);
    auto oversized = trailing;
    ++oversized[1];
    auto nonfinite = packet;
    // Last native component is w; IEEE +infinity must be rejected too.
    nonfinite[64] = 0; nonfinite[65] = 0; nonfinite[66] = 0x80; nonfinite[67] = 0x7F;
    auto unset = encode(request);
    for (std::size_t i = 3; i < 7; ++i) unset[i] = 0;
    check(rejects(truncated) && rejects(trailing) && rejects(oversized) &&
              rejects(nonfinite) && rejects(unset),
          "activation rejects truncated/trailing payloads, nonfinite w and unset epoch");
    bool writeRejected = false;
    auto invalid = point;
    invalid.position[0] = std::numeric_limits<float>::quiet_NaN();
    try { (void)encode(invalid); } catch (const std::exception&) { writeRejected = true; }
    check(writeRejected, "native nonfinite activation cannot be encoded");
}

void testAppliedEnemyHash() {
    std::cout << "\n=== Applied native enemy hash ===\n";
    const std::vector<AppliedEnemyState> host {{1, 0x210, 153}, {2, 0x210, 160}, {3, 0x2A0, 40}};
    const auto baseline = hashAppliedEnemies(host);
    auto local = host;
    std::reverse(local.begin(), local.end());
    check(hashAppliedEnemies(local) == baseline,
          "nonempty native populations compare equally independent of actor enumeration order");
    local[0].hp -= 7;
    check(hashAppliedEnemies(local) != baseline,
          "deliberately mismatched local HP changes the applied hash");
    local = host;
    local.pop_back();
    check(hashAppliedEnemies(local) != baseline,
          "a missing matched live actor changes the applied hash");
    local = host;
    local.push_back({0, 0x210, 20});
    const auto extra = hashAppliedEnemies(local);
    check(extra != baseline, "an unmatched client-only living spawn changes the applied hash");
    local.push_back({0, 0x210, 20});
    check(hashAppliedEnemies(local) != extra,
          "identical unmatched spawns retain multiplicity");
    local = host;
    local.push_back(host.front());
    check(hashAppliedEnemies(local) != baseline,
          "two native actors bound to one host ID cannot collapse into one record");
    local = host;
    local[0].netId = 0;
    check(hashAppliedEnemies(local) != baseline,
          "an unbound native actor is distinguished from its matched host copy");
    local = host;
    local[0].objectId += 1;
    check(hashAppliedEnemies(local) != baseline,
          "a different native enemy object changes the applied hash");
    local = host;
    local[0].netId += 8;
    check(hashAppliedEnemies(local) != baseline,
          "a different host binding changes the applied hash");
    local = host;
    local.push_back({4, 0x210, 0});
    local.push_back({0, 0x210, -1});
    check(hashAppliedEnemies(local) == baseline,
          "observed dead actors are excluded consistently, matched or unmatched");
    const std::vector<AppliedEnemyState> afterDeath {{2, 0x210, 160}, {3, 0x2A0, 40}};
    check(hashAppliedEnemies(host) != hashAppliedEnemies(afterDeath),
          "a failed native death retaining positive HP differs from the host's dead population");
    local = host;
    local[0].hp = 0;
    check(hashAppliedEnemies(local) == hashAppliedEnemies(afterDeath),
          "native death success agrees even while its zero-HP actor remains in the list");
    check(hashAppliedEnemies({}) != baseline,
          "an empty enemy set cannot stand in for the nonempty checkpoint");
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

    // Verified SAVE-body boundaries (D8: per-player state stays off).
    const auto allow = verifiedProgressAllowList();
    bool sorted = true;
    for (std::size_t i = 1; i < allow.size(); ++i) {
        sorted &= allow[i - 1].offset + allow[i - 1].length <= allow[i].offset;
    }
    ProgressMirror policy(allow);
    check(sorted && !policy.allowed(0x24F8) && !policy.allowed(0x353C) &&
              !policy.allowed(0x3580) && policy.allowed(0x1CFF) && policy.allowed(0x0010),
          "verified allow list: no overlaps; stats/party/inventory excluded; story included");
    check(!policy.allowed(0x000F) && policy.allowed(0x1C8F) && policy.allowed(0x1C90) &&
              policy.allowed(0x1EEF) && !policy.allowed(0x1EF0),
          "room program and story ranges stop at the verified SAVE boundaries");
    check(!policy.allowed(0x22F7) && policy.allowed(0x22F8) && policy.allowed(0x238F) &&
              !policy.allowed(0x2390),
          "visited-room bytes start at 0x22F8 without the stale eight-byte shift");
    check(!policy.allowed(0x23AB) && policy.allowed(0x23AC) && policy.allowed(0x23DF) &&
              !policy.allowed(0x23E0),
          "chest flags include only the verified 0x34-byte span");
    check(verifiedProgressByteMask(0x23AC) == 0xFE &&
              verifiedProgressByteMask(0x23DF) == 0x0F &&
              verifiedProgressByteMask(0x23AD) == 0xFF &&
              verifiedProgressByteMask(0x1CFF) == 0xFF &&
              verifiedProgressByteMask(0x23E0) == 0 &&
              verifiedProgressByteMask(0x24F8) == 0,
          "chest boundary masks preserve adjacent personal bits and reject disallowed bytes");
}

void testWorldCacheEpoch() {
    std::uint64_t hpSequence = 0; // Explicit authored source order, including rejected probes.
    std::cout << "\n=== World cache keeps its actual room epoch ===\n";
    SessionConfig cfg;
    cfg.port = 17799;
    cfg.bindAddress = "127.0.0.1";
    cfg.gameBuild = "hp-cache-build";
    cfg.contentHash = "hp-cache-content";
    cfg.modHash = "hp-cache-mod";
    SessionHost relay(cfg);
    const bool started = relay.start();
    check(started, "HP cache relay starts on loopback");
    if (!started) return;
    Seen hostSeen, friendSeen, lateSeen;
    NetworkClient host("127.0.0.1", cfg.port, cfg.gameBuild, cfg.modHash, "hp-host",
                       SlotType::Player, callbacksFor(hostSeen), RuntimeMode::CampaignCoop, cfg.contentHash);
    NetworkClient friendClient("127.0.0.1", cfg.port, cfg.gameBuild, cfg.modHash, "hp-friend",
                               SlotType::Friend1, callbacksFor(friendSeen), RuntimeMode::CampaignCoop, cfg.contentHash);
    NetworkClient late("127.0.0.1", cfg.port, cfg.gameBuild, cfg.modHash, "hp-late",
                       SlotType::Friend2, callbacksFor(lateSeen), RuntimeMode::CampaignCoop, cfg.contentHash);
    const auto wait = [&](const std::function<bool()>& condition) {
        const auto deadline = nowMs() + 3000;
        while (nowMs() < deadline && !condition()) {
            for (auto* client : {&host, &friendClient, &late}) {
                client->sendHeartbeat();
                client->tick(0);
            }
            relay.tick(0);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return condition();
    };
    host.connect();
    friendClient.connect();
    const bool admitted = wait([&] { return host.worldReady() && friendClient.worldReady(); });
    check(admitted, "HP cache host and friend have admitted rosters before publication");
    if (!admitted) return;

    std::uint32_t progressBarrier = 0;
    const auto orderedBarrier = [&]() {
        const auto marker = ++progressBarrier;
        host.sendProgressUpdate(ProgressUpdate {marker, false, {}});
        return wait([&] { return !friendSeen.progress.empty() &&
                                friendSeen.progress.back().version == marker; });
    };
    const auto rejoinCache = [&]() {
        const auto count = relay.verifiedPeerCount();
        friendClient.disconnect();
        if (!wait([&] { return relay.verifiedPeerCount() + 1 == count; })) return false;
        friendClient.connect();
        return wait([&] { return friendClient.worldReady() && relay.verifiedPeerCount() == count; });
    };
    auto rejectedBefore = relay.rejectedWorldMessages();
    worldfixture::send(host, encode(EnemyHp {12, {{1, 999, 1000}}, ++hpSequence}), true);
    check(orderedBarrier() && relay.rejectedWorldMessages() == rejectedBefore + 1,
          "HP without a current room is rejected before an ordered host barrier");

    host.sendRoomTransition(RoomTransition {11, 4, 26, 0, 0, 3, 0});
    host.sendEnemyManifest(EnemyManifest {11, true, {entry(1, 0, 309), entry(2, 1, 309)}});
    check(wait([&] { return !friendSeen.manifests.empty() && friendSeen.manifests.back().epoch == 11; }),
          "old nonempty manifest is received before priming periodic HP");
    host.sendEnemyHp(EnemyHp {11, {{1, 41, 100}, {2, 51, 100}}, ++hpSequence});
    check(wait([&] { return !friendSeen.hps.empty() && friendSeen.hps.back().epoch == 11; }),
          "actual unreliable old-room HP is positively received");
    host.sendRoomTransition(RoomTransition {12, 4, 27, 0, 0, 3, 0});
    check(wait([&] { return !friendSeen.rooms.empty() && friendSeen.rooms.back().epoch == 12; }),
          "new room retires the old manifest before the delayed HP controls");
    rejectedBefore = relay.rejectedWorldMessages();
    worldfixture::send(host, encode(EnemyHp {12, {{1, 998, 1000}}, ++hpSequence}), true);
    check(orderedBarrier() && relay.rejectedWorldMessages() == rejectedBefore + 1,
          "current-room HP without its current manifest is rejected");
    host.sendEnemyManifest(EnemyManifest {12, true, {entry(1, 0, 309), entry(2, 1, 309)}});
    check(wait([&] { return !friendSeen.manifests.empty() && friendSeen.manifests.back().epoch == 12; }),
          "new manifest is established before current periodic HP");
    host.sendEnemyHp(EnemyHp {12, {{1, 72, 120}, {2, 82, 120}}, ++hpSequence});
    check(wait([&] { return !friendSeen.hps.empty() && friendSeen.hps.back().epoch == 12; }),
          "actual unreliable current-room HP is positively received");
    host.sendEventHold(EventHold {12, true, 0x33});
    host.sendEnemyDeath(EnemyDeath {12, 2});
    check(orderedBarrier() && !friendSeen.holds.empty() && friendSeen.holds.back().active &&
              !friendSeen.deaths.empty() && friendSeen.deaths.back().netId == 2,
          "current hold and death positively prime the reconstruction cache");

    const auto rejectCached = [&](const std::vector<std::uint8_t>& packet, const std::string& label) {
        const auto holds = friendSeen.holds.size(), manifests = friendSeen.manifests.size();
        const auto deaths = friendSeen.deaths.size();
        const auto rooms = friendSeen.rooms.size(), progress = friendSeen.progress.size();
        worldfixture::send(host, packet, true);
        check(orderedBarrier() && friendSeen.holds.size() == holds &&
                  friendSeen.manifests.size() == manifests && friendSeen.deaths.size() == deaths &&
                  friendSeen.rooms.size() == rooms && friendSeen.progress.size() == progress + 1, label);
    };
    for (const std::uint32_t epoch : {11u, 13u, 0u}) {
        const auto label = std::string("epoch ") + std::to_string(epoch);
        rejectCached(encode(EventHold {epoch, false, 0x99}), label + " hold is rejected before ordered positive barrier");
        rejectCached(encode(EnemyManifest {epoch, true, {entry(99, 9, 311)}}),
                     label + " replacement manifest is rejected before ordered positive barrier");
        rejectCached(encode(EnemyDeath {epoch, 1}), label + " death is rejected before ordered positive barrier");
    }
    rejectCached(encode(EnemyManifest {11, false, {entry(99, 9, 311)}}),
                 "stale appended manifest cannot alter the current cache");
    const auto rejectMalformed = [&](const auto& message, PacketType type, const char* label) {
        ByteWriter writer;
        write(writer, message);
        auto payload = writer.take();
        payload.pop_back();
        rejectCached(encodePacket(type, payload), std::string(label) + " truncated payload is rejected atomically");
        writer = ByteWriter{};
        write(writer, message);
        writer.writeU8(0xEE);
        rejectCached(encodePacket(type, writer.data()), std::string(label) + " trailing payload is rejected atomically");
        auto frame = encode(message);
        frame.pop_back();
        rejectCached(frame, std::string(label) + " truncated frame is rejected");
        frame = encode(message);
        frame.push_back(0xEE);
        rejectCached(frame, std::string(label) + " extra frame byte is rejected");
    };
    rejectMalformed(EventHold {12, false, 0x99}, PacketType::EventHold, "hold");
    rejectMalformed(EnemyManifest {12, true, {entry(99, 9, 311), entry(98, 8, 311)}},
                    PacketType::EnemyManifest, "two-entry manifest");
    rejectMalformed(EnemyDeath {12, 1}, PacketType::EnemyDeath, "death");
    rejectMalformed(RoomTransition {99, 5, 6, 0, 0, 0, 0}, PacketType::RoomTransition, "room");
    rejectMalformed(ProgressUpdate {500, true, {{0x1CFF, {9}}, {0x1D00, {8}}}},
                    PacketType::ProgressUpdate, "two-span progress");
    check(relay.currentRoom() && relay.currentRoom()->epoch == 12 && relay.progressBytes() == 0,
          "malformed room/progress prefixes do not mutate their caches or version policy");

    // Send each negative on channel 0, followed by a valid HP marker on that
    // same inbound channel. The relay forwards both HP records on channel 1.
    // Receiving its positive sequenced marker fences earlier forwarded HP;
    // no sleep-only absence assertion and no later marker overwrites netId 1.
    std::int32_t hpBarrier = 82;
    const auto rejectPacket = [&](const std::vector<std::uint8_t>& packet, const char* label) {
        const auto before = friendSeen.hps.size();
        const auto marker = ++hpBarrier;
        worldfixture::send(host, packet, true);
        worldfixture::send(host, encode(EnemyHp {12, {{2, marker, 120}}, ++hpSequence}), true);
        const bool received = wait([&] {
            return friendSeen.hps.size() > before && friendSeen.hps.back().epoch == 12 &&
                   friendSeen.hps.back().entries.size() == 1 &&
                   friendSeen.hps.back().entries.front().netId == 2 &&
                   friendSeen.hps.back().entries.front().hp == marker;
        });
        check(received && friendSeen.hps.size() == before + 1, label);
    };
    rejectPacket(encode(EnemyHp {11, {{1, 901, 1000}}, ++hpSequence}), "delayed old-room HP is not forwarded before the positive HP barrier");
    rejectPacket(encode(EnemyHp {13, {{1, 902, 1000}}, ++hpSequence}), "future-room HP is not forwarded before the positive HP barrier");
    rejectPacket(encode(EnemyHp {0, {{1, 903, 1000}}, ++hpSequence}), "unset-epoch HP is not forwarded before the positive HP barrier");
    ByteWriter trailing;
    write(trailing, EnemyHp {12, {{1, 904, 1000}}, ++hpSequence});
    trailing.writeU8(0xEE);
    rejectPacket(encodePacket(PacketType::EnemyHp, trailing.data()),
                 "declared trailing HP payload is rejected as a whole");
    ByteWriter truncated;
    truncated.writeU32(12);
    truncated.writeU64(++hpSequence);
    truncated.writeU16(2);
    truncated.writeU16(1); truncated.writeI32(905); truncated.writeI32(1000);
    truncated.writeU16(2); // Complete first entry, incomplete second entry.
    rejectPacket(encodePacket(PacketType::EnemyHp, truncated.data()),
                 "truncated second HP entry cannot forward a decoded prefix");
    auto brokenHpFrame = encode(EnemyHp {12, {{1, 906, 1000}}, ++hpSequence});
    brokenHpFrame.pop_back();
    rejectPacket(brokenHpFrame, "truncated HP frame cannot mutate or forward cache data");
    brokenHpFrame = encode(EnemyHp {12, {{1, 907, 1000}}, ++hpSequence});
    brokenHpFrame.push_back(0xEE);
    rejectPacket(brokenHpFrame, "extra HP frame byte cannot mutate or forward cache data");

    const auto correctCache = [&](const EnemyHp& hp) {
        return hp.epoch == 12 && hp.entries.size() == 2 &&
               hp.entries[0].netId == 1 && hp.entries[0].hp == 72 && hp.entries[0].maxHp == 120 &&
               hp.entries[1].netId == 2 && hp.entries[1].hp == hpBarrier && hp.entries[1].maxHp == 120;
    };
    const auto beforeResync = friendSeen.hps.size();
    const auto manifestsBeforeResync = friendSeen.manifests.size(), holdsBeforeResync = friendSeen.holds.size();
    const auto deathsBeforeResync = friendSeen.deaths.size();
    check(rejoinCache(), "targeted cache probe rejoins selected friend on surviving host");
    check(orderedBarrier() && friendSeen.hps.size() == beforeResync + 1 && correctCache(friendSeen.hps.back()),
          "targeted friend-rejoin cache retains actual current HP; stale/future/malformed prefixes are not relabeled");
    const auto correctManifest = [](const EnemyManifest& manifest) {
        return manifest.epoch == 12 && manifest.replace && manifest.entries.size() == 2 &&
               manifest.entries[0].netId == 1 && manifest.entries[0].objectId == 309 &&
               manifest.entries[1].netId == 2 && manifest.entries[1].objectId == 309;
    };
    check(friendSeen.manifests.size() == manifestsBeforeResync + 1 && correctManifest(friendSeen.manifests.back()) &&
              friendSeen.holds.size() == holdsBeforeResync + 1 && friendSeen.holds.back().epoch == 12 &&
              friendSeen.holds.back().active && friendSeen.holds.back().eventProgram == 0x33 &&
              friendSeen.deaths.size() == deathsBeforeResync + 1 && friendSeen.deaths.back().epoch == 12 &&
              friendSeen.deaths.back().netId == 2,
          "targeted friend-rejoin preserves current manifest/hold/death without stale replacements or added deaths");
    late.connect();
    check(wait([&] { return late.ready() && !lateSeen.hps.empty() && !lateSeen.deaths.empty(); }) && correctCache(lateSeen.hps.back()),
          "late join reconstructs only current-epoch HP after all rejected payloads");
    // Its own same-channel host progress barrier ensures all bootstrap deaths
    // have arrived, including any erroneously inserted stale netId 1.
    check(orderedBarrier() && wait([&] { return !lateSeen.progress.empty() &&
                                               lateSeen.progress.back().version == progressBarrier; }) &&
              lateSeen.manifests.size() == 1 && correctManifest(lateSeen.manifests.back()) &&
              lateSeen.holds.size() == 1 && lateSeen.holds.back().epoch == 12 &&
              lateSeen.holds.back().active && lateSeen.holds.back().eventProgram == 0x33 &&
              lateSeen.deaths.size() == 1 && lateSeen.deaths.back().epoch == 12 && lateSeen.deaths.back().netId == 2,
          "late join reconstructs exactly the original current manifest/hold/death cache");

    // Existing semantics deliberately retained: current-epoch unknown IDs may
    // arrive before an appended manifest, and duplicate entries are last-wins.
    const auto compatibilityBefore = friendSeen.hps.size();
    worldfixture::send(host, encode(EnemyHp {12, {{3, 10, 120}, {3, 33, 120}}, ++hpSequence}), true);
    check(wait([&] { return friendSeen.hps.size() > compatibilityBefore; }) &&
              friendSeen.hps.back().entries.size() == 2 && friendSeen.hps.back().entries.back().hp == 33,
          "same-epoch pre-append HP and duplicate payload forwarding retain existing semantics");
    host.sendEnemyManifest(EnemyManifest {12, false, {entry(3, 2, 311)}});
    check(rejoinCache(), "targeted cache probe rejoins selected friend on surviving host");
    check(orderedBarrier() && !friendSeen.hps.empty() && friendSeen.hps.back().epoch == 12 && friendSeen.hps.back().entries.size() == 3 &&
              friendSeen.hps.back().entries.back().netId == 3 && friendSeen.hps.back().entries.back().hp == 33,
          "same-epoch appended member retains the last duplicate HP value in reconstruction");
    host.disconnect();
    friendClient.disconnect();
    late.disconnect();
    relay.stop();
}

void testHostHeartbeatExpiry() {
    std::cout << "\n=== Host heartbeat expiry ends old connections ===\n";
    for (bool allStale : {false, true}) {
        SessionConfig cfg;
        cfg.port = allStale ? 17798 : 17797;
        cfg.bindAddress = "127.0.0.1";
        cfg.maxPeers = 3;
        cfg.heartbeatTimeoutMs = 300;
        cfg.gameBuild = "expiry-build";
        cfg.contentHash = "expiry-content";
        cfg.modHash = "expiry-mod";
        cfg.sessionId = "expiry-session";
        std::map<std::string, unsigned> left;
        SessionCallbacks serverCallbacks;
        serverCallbacks.onPeerLeft = [&](const std::string& id) { ++left[id]; };
        SessionHost relay(cfg, std::move(serverCallbacks));
        check(relay.start(), "expiry relay starts on loopback");
        Seen hostSeen, friendSeen;
        NetworkClient host("127.0.0.1", cfg.port, cfg.gameBuild, cfg.modHash, "expiry-host",
                           SlotType::Player, callbacksFor(hostSeen), RuntimeMode::CampaignCoop, cfg.contentHash);
        NetworkClient friendClient("127.0.0.1", cfg.port, cfg.gameBuild, cfg.modHash, "expiry-friend",
                                   SlotType::Friend1, callbacksFor(friendSeen), RuntimeMode::CampaignCoop, cfg.contentHash);
        const auto pump = [&](int duration, bool hostActive, bool friendActive) {
            const auto end = nowMs() + duration;
            while (nowMs() < end) {
                if (hostActive) { host.sendHeartbeat(); host.tick(0); }
                if (friendActive) { friendClient.sendHeartbeat(); friendClient.tick(0); }
                relay.tick(0);
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        };
        const auto wait = [&](const std::function<bool()>& condition, bool hostActive, bool friendActive) {
            const auto deadline = nowMs() + 2000;
            while (nowMs() < deadline && !condition()) pump(5, hostActive, friendActive);
            return condition();
        };
        if (allStale) {
            // Host first makes expiry gather it before the friend. Its teardown
            // must not notify the later gathered friend a second time.
            host.connect();
            check(wait([&] { return relay.verifiedPeerCount() == 1; }, true, false), "expiry host verifies first");
            friendClient.connect();
        } else {
            friendClient.connect();
            check(wait([&] { return relay.verifiedPeerCount() == 1; }, false, true), "a friend can join before any host");
            pump(350, false, true);
            check(friendClient.isConnected() && relay.verifiedPeerCount() == 1 && !relay.currentRoom(),
                  "pre-host friend heartbeats preserve the waiting connection");
            host.connect();
        }
        check(wait([&] { return relay.verifiedPeerCount() == 2 && host.worldReady() && friendClient.worldReady(); }, true, true),
              "expiry host and friend verified with client-side roster admission");
        host.sendRoomTransition(RoomTransition {7, 5, 6, 0, 1, 1, 0});
        host.sendEnemyManifest(EnemyManifest {7, true, {entry(1, 0, 309)}});
        host.sendProgressUpdate(ProgressUpdate {9, true, {{0x1CFF, {8}}}});
        const bool populated = wait([&] { return relay.currentRoom() && relay.manifestSize() == 1 && relay.progressBytes() == 1; }, true, true);
        check(populated, "expiry fixture has populated world and progress state");
        if (!populated) {
            // A failed setup is already a failed check; never start the host-loss
            // control from an empty world where !currentRoom() would pass at once.
            host.disconnect(); friendClient.disconnect(); relay.stop();
            continue;
        }
        pump(40, true, true); // Flush setup packets before deliberately going silent.
        if (allStale) {
            std::this_thread::sleep_for(std::chrono::milliseconds(350));
            relay.tick(0); // Both peers are gathered before the host is removed.
        } else {
            check(wait([&] { return !relay.currentRoom(); }, false, true),
                  "silent host expires while friend heartbeats remain active");
        }
        check(relay.verifiedPeerCount() == 0 && relay.peers().empty() && relay.sessionState().actors.empty() &&
                  !relay.currentRoom() && relay.manifestSize() == 0 && relay.progressBytes() == 0 && relay.isRunning(),
              "host expiry clears the old session but keeps the relay listening");
        check(wait([&] { return !host.isConnected() && !friendClient.isConnected(); }, true, true),
              "both old clients observe the host-expiry disconnect");
        pump(50, true, true);
        check(left["expiry-host"] == 1 && left["expiry-friend"] == 1 &&
                  hostSeen.disconnects == 1 && friendSeen.disconnects == 1,
              "expiry teardown and late disconnect events notify each peer exactly once");
        host.disconnect();
        friendClient.disconnect();
        hostSeen = {};
        friendSeen = {};
        host.connect();
        friendClient.connect();
        check(wait([&] { return relay.verifiedPeerCount() == 2 && host.worldReady() && friendClient.worldReady(); }, true, true),
              "fresh connections can join and receive roster admission after host expiry");
        pump(50, true, true);
        check(friendSeen.rooms.empty() && friendSeen.manifests.empty() && friendSeen.progress.empty() &&
                  !relay.currentRoom() && relay.manifestSize() == 0 && relay.progressBytes() == 0,
              "fresh connections after expiry receive no old room, enemies, or progress");
        host.sendRoomTransition(RoomTransition {1, 4, 26, 0, 0, 0, 0});
        host.sendProgressUpdate(ProgressUpdate {1, true, {{0x1CFF, {2}}}});
        check(wait([&] { return friendSeen.rooms.size() == 1 && friendSeen.progress.size() == 1; }, true, true) &&
                  friendSeen.rooms.back().epoch == 1 && friendSeen.progress.back().version == 1,
              "fresh host after expiry publishes independent room and progress state");
        host.disconnect();
        friendClient.disconnect();
        relay.stop();
    }
}

} // namespace

int main() {
    std::uint64_t hpSequence = 0; // Host-lifetime HP order survives room/replacement controls.
    if (enet_initialize() != 0) return 2;
    testHitClaimCodec();
    testActivationCodec();
    testAppliedEnemyHash();
    testProgressMirror();

    SessionConfig cfg;
    cfg.port = 17796;
    cfg.bindAddress = "127.0.0.1";
    cfg.maxPeers = 3;
    cfg.gameBuild = "world-build";
    cfg.contentHash = "world-content";
    cfg.modHash = "world-mod";
    cfg.sessionId = "world-test";
    std::map<std::string, unsigned> left;
    std::vector<std::string> cacheRows;
    std::ofstream cacheRaw("cache-raw.log",std::ios::binary);
    SessionCallbacks serverCallbacks;
    serverCallbacks.onCausalDiagnostic = [&](const std::string& row){
        cacheRows.push_back(row);cacheRaw<<row<<'\n';cacheRaw.flush();return cacheRaw.good();
    };
    serverCallbacks.onPeerLeft = [&](const std::string& id) { ++left[id]; };
    SessionHost relay(cfg, std::move(serverCallbacks));
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
    check(waitFor([&] { return relay.verifiedPeerCount() == 2 && host->worldReady() && c1->worldReady(); }), "host + client verified");

    HitClaim claim;
    claim.epoch = 1;
    claim.seq = 10;
    claim.netId = 1;
    claim.objectId = 0x210;
    const auto originalConnectionId = relay.peerBySlot(SlotType::Friend1)->connectionId;
    claim.requesterConnectionId = originalConnectionId;
    check(originalConnectionId != 0 && relay.peerBySlot(SlotType::Player)->connectionId != originalConnectionId,
          "verified connections receive distinct nonzero identities");
    check(waitFor([&] { return !c1Seen.sessions.empty(); }) &&
              std::any_of(c1Seen.sessions.back().actors.begin(), c1Seen.sessions.back().actors.end(),
                  [&](const SessionActor& a) { return a.slot == SlotType::Friend1 && a.connectionId == originalConnectionId; }),
          "connection identity travels in the decoded session roster");
    claim.attackId = 0x41;
    claim.damage = 25;
    claim.attackerPosition = {10.0f, 0.0f, 20.0f};
    claim.attackerSlot = SlotType::Friend2; // lie: the relay must stamp Friend1
    const auto rejectsClaim = [&](const HitClaim& rejected, const char* description) {
        const auto rejectsBefore = relay.rejectedWorldMessages();
        const auto hostBefore = hostSeen.claims.size();
        const auto c1Before = c1Seen.claims.size(), c2Before = c2Seen.claims.size();
        c1->sendHitClaim(rejected);
        const bool processed = waitFor([&] {
            return relay.rejectedWorldMessages() != rejectsBefore || hostSeen.claims.size() != hostBefore;
        });
        pump(20);
        check(processed && relay.rejectedWorldMessages() == rejectsBefore + 1 &&
                  hostSeen.claims.size() == hostBefore && c1Seen.claims.size() == c1Before &&
                  c2Seen.claims.size() == c2Before, description);
    };
    rejectsClaim(claim, "hit claim before the first room is rejected without forwarding");

    // An unset host epoch must not authorize a claim even when both are zero.
    host->sendRoomTransition(RoomTransition {0, 4, 0x1A, 2, 0x10, 0x11, 0});
    check(waitFor([&] { return !c1Seen.rooms.empty(); }) &&
              relay.currentRoom() && relay.currentRoom()->epoch == 0,
          "unset-epoch room reaches the relay for the hit-claim rejection control");
    claim.epoch = 0;
    rejectsClaim(claim, "matching zero room and claim epochs are rejected");
    c1Seen.rooms.clear(); // Start the ordinary room assertions after this setup control.
    claim.epoch = 1;

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

    host->sendEnemyHp(EnemyHp {1, {{1, 40, 120}, {2, 120, 120}}, ++hpSequence});
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
    claim.epoch = 0;
    rejectsClaim(claim, "zero hit-claim epoch is rejected in an established room");
    claim.epoch = 2;
    rejectsClaim(claim, "future hit-claim epoch is rejected");
    claim.epoch = 1;
    const auto firstClaimsBefore = hostSeen.claims.size();
    c1->sendHitClaim(claim);
    check(waitFor([&] { return hostSeen.claims.size() == firstClaimsBefore + 1; }) &&
              hostSeen.claims.back().epoch == 1 &&
              hostSeen.claims.back().attackerSlot == SlotType::Friend1 &&
              hostSeen.claims.back().damage == 25 &&
              hostSeen.claims.back().attackId == 0x41,
          "the host receives the claim stamped with the real attacker slot");
    check(c1Seen.claims.empty(), "the claimant does not get its claim echoed");
    rejectsClaim(claim, "duplicate claim sequence is rejected");
    auto badClaim = claim;
    badClaim.seq = 9;
    rejectsClaim(badClaim, "older claim sequence is rejected");
    badClaim.seq = 0;
    rejectsClaim(badClaim, "zero claim sequence is rejected");
    const auto invalidClaim = [&](const std::function<void(HitClaim&)>& alter, const char* label) {
        auto invalid = claim;
        invalid.seq = claim.seq + 1;
        alter(invalid);
        rejectsClaim(invalid, label);
    };
    invalidClaim([](HitClaim& m) { m.requesterConnectionId = 0; }, "unset requester connection is rejected");
    invalidClaim([&](HitClaim& m) { m.requesterConnectionId = relay.peerBySlot(SlotType::Player)->connectionId; },
                 "another peer's connection identity is rejected");
    invalidClaim([](HitClaim& m) { m.netId = 99; }, "unknown enemy identity is rejected");
    invalidClaim([](HitClaim& m) { m.netId = 0; }, "zero enemy identity is rejected");
    invalidClaim([](HitClaim& m) { m.objectId = 0; }, "unset enemy type is rejected");
    invalidClaim([](HitClaim& m) { m.objectId = 0x211; }, "mismatched enemy type is rejected");
    invalidClaim([](HitClaim& m) { m.netId = 2; }, "recorded dead enemy is rejected");
    invalidClaim([](HitClaim& m) { m.damage = 0; }, "zero damage is rejected");
    invalidClaim([](HitClaim& m) { m.damage = -1; }, "negative damage is rejected");
    invalidClaim([](HitClaim& m) { m.attackerPosition.x = std::numeric_limits<float>::infinity(); },
                 "infinite attacker position is rejected");
    invalidClaim([](HitClaim& m) { m.attackerPosition.z = std::numeric_limits<float>::quiet_NaN(); },
                 "NaN attacker position is rejected");
    host->sendEnemyManifest(EnemyManifest {1, false, {entry(1, 8, 0x210)}});
    check(waitFor([&] { return relay.manifestSize() == 5; }), "ambiguous target fixture reaches relay");
    invalidClaim([](HitClaim&) {}, "duplicate manifest netId is rejected even with the same type");
    host->sendEnemyManifest(EnemyManifest {1, true, {entry(1, 0, 0x210), entry(2, 1, 0x210),
                                                    entry(3, 2, 0x2A0), entry(4, 3, 0x210)}});
    host->sendEnemyHp(EnemyHp {1, {{1, 40, 120}, {2, 120, 120}}, ++hpSequence});
    host->sendEnemyDeath(EnemyDeath {1, 2});
    check(waitFor([&] { return relay.manifestSize() == 4; }), "original world cache restored after ambiguity control");


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
    ActivationRequest activation {goa, {123, 456}, 1, 2}; // forged slot must be overwritten
    c1->sendActivationRequest(activation);
    check(waitFor([&] { return hostSeen.activationRequests.size() == 1; }) &&
              hostSeen.activationRequests.back().requesterSlot == 1 &&
              hostSeen.activationRequests.back().incarnation == activation.incarnation,
          "activation challenge reaches only host with verified requester slot");
    HostActivationPoint activationPoint {hostSeen.activationRequests.back(), 1,
                                        {45.0f, -260.0f, 1950.0f, 1.0f}};
    c1->sendHostActivationPoint(activationPoint);
    pump(50);
    check(c1Seen.activationPoints.empty() && hostSeen.activationPoints.empty(),
          "non-host cannot author activation point");
    host->sendHostActivationPoint(activationPoint);
    check(waitFor([&] { return c1Seen.activationPoints.size() == 1; }) &&
              c1Seen.activationRequests.empty() && hostSeen.activationPoints.empty(),
          "host activation response returns solely to its requester");
    host->sendHostActivationPoint(activationPoint);
    pump(50);
    check(c1Seen.activationPoints.size() == 1, "duplicate activation response is not routed twice");
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
    check(c2Seen.activationPoints.empty() && c2Seen.activationRequests.empty(),
          "late join receives no cached activation point or challenge");
    auto secondClaim = claim;
    secondClaim.requesterConnectionId = relay.peerBySlot(SlotType::Friend2)->connectionId;
    const auto secondClaimsBefore = hostSeen.claims.size();
    c2->sendHitClaim(secondClaim);
    check(waitFor([&] { return hostSeen.claims.size() == secondClaimsBefore + 1; }) &&
              hostSeen.claims.back().attackerSlot == SlotType::Friend2 &&
              hostSeen.claims.back().requesterConnectionId == secondClaim.requesterConnectionId &&
              secondClaim.requesterConnectionId != originalConnectionId,
          "another connection can independently use the same sequence with its own authenticated identity");
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

    const auto getField=[](const std::string& row,const std::string& key){
        const auto prefix=key+"=";auto at=row.find(prefix);if(at==std::string::npos)return std::string{};
        at+=prefix.size();return row.substr(at,row.find(' ',at)-at);
    };
    const auto* cachedTarget=relay.peerBySlot(SlotType::Friend2);
    unsigned exactCached=0;bool roomReuseDiffers=false;
    for(const auto& row:cacheRows){
        if(getField(row,"target")!=std::to_string(cachedTarget->connectionId)||
           getField(row,"disposition")!="enet-submitted")continue;
        bool matched=false;
        for(const auto& env:c2Seen.envelopes){
            if(getField(row,"wireSHA")==causalSha(encode(env))&&getField(row,"payloadSHA")==causalSha(env.packet)&&
               getField(row,"hostSource")==std::to_string(env.scope.hostSourceSerial)&&
               getField(row,"targetDelivery")==std::to_string(env.scope.targetDeliverySerial)){
                matched=true;
                if(static_cast<PacketType>(env.packet.front())==PacketType::RoomTransition){
                    for(const auto& ordinary:c1Seen.envelopes)
                        if(static_cast<PacketType>(ordinary.packet.front())==PacketType::RoomTransition&&ordinary.packet==env.packet)
                            roomReuseDiffers=ordinary.scope.hostSourceSerial!=env.scope.hostSourceSerial;
                }
            }
        }
        check(matched,"cache receipt identifies exact actual received envelope and payload bytes");++exactCached;
    }
    check(exactCached>=6,"cache provenance covers progress room hold manifest HP death sends");
    check(roomReuseDiffers,"actual cached room can carry later source serial than original publication");
    relay.sealCacheDiagnostics();
    check(getField(cacheRows.back(),"dropped")=="0"&&getField(cacheRows.back(),"highWater")==getField(cacheRows.back(),"flushedHighWater"),
          "actual flushed relay seal closes all cache sends without gaps");
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

    rejectsClaim(claim, "previous-room hit claim is rejected after the host advances epoch");
    claim.epoch = 2;
    auto unmanifestedClaim = claim;
    ++unmanifestedClaim.seq;
    rejectsClaim(unmanifestedClaim, "new epoch does not authorize a claim before its manifest exists");
    host->sendEnemyManifest(EnemyManifest {2, true, {entry(1, 0, 0x210)}});
    check(waitFor([&] { return relay.manifestSize() == 1; }), "new room manifest authorizes its typed target");
    rejectsClaim(claim, "room transition does not reset the connection sequence watermark");
    ++claim.seq;
    auto staleEpochClaim = claim;
    staleEpochClaim.epoch = 1;
    rejectsClaim(staleEpochClaim,
                 "old epoch alone rejects an otherwise valid fresh-sequence typed claim in the new room");
    const auto currentClaimsBefore = hostSeen.claims.size();
    c1->sendHitClaim(claim);
    check(waitFor([&] { return hostSeen.claims.size() == currentClaimsBefore + 1; }) &&
              hostSeen.claims.back().epoch == claim.epoch && hostSeen.claims.back().seq == claim.seq &&
              hostSeen.claims.back().netId == claim.netId && hostSeen.claims.back().damage == claim.damage &&
              hostSeen.claims.back().attackId == claim.attackId &&
              hostSeen.claims.back().attackerPosition.x == claim.attackerPosition.x &&
              hostSeen.claims.back().attackerPosition.y == claim.attackerPosition.y &&
              hostSeen.claims.back().attackerPosition.z == claim.attackerPosition.z &&
              hostSeen.claims.back().attackerSlot == SlotType::Friend1 &&
              c1Seen.claims.empty() && c2Seen.claims.empty(),
          "current epoch accepts the same sequence rejected for old epoch, with intact payload and authenticated slot");

    const auto requestsBefore = hostSeen.activationRequests.size();
    activation.requestSeq = 2;
    c1->sendActivationRequest(activation); // old epoch
    activation.location = *relay.currentRoom();
    ++activation.location.eventProgram; // current epoch, wrong sixth field
    c1->sendActivationRequest(activation);
    pump(50);
    check(hostSeen.activationRequests.size() == requestsBefore,
          "relay rejects old epoch and full-location activation mismatches");
    activation.location = *relay.currentRoom();
    c1->sendActivationRequest(activation);
    check(waitFor([&] { return hostSeen.activationRequests.size() > requestsBefore; }),
          "current-epoch activation challenge reaches host");
    activationPoint.request = hostSeen.activationRequests.back();
    activationPoint.sourceSeq = 2;
    auto wrongRecipient = activationPoint;
    wrongRecipient.request.requesterSlot = 2;
    host->sendHostActivationPoint(wrongRecipient);
    auto wrongRoom = activationPoint;
    ++wrongRoom.request.location.door;
    host->sendHostActivationPoint(wrongRoom);
    pump(50);
    check(c1Seen.activationPoints.size() == 1 && c2Seen.activationPoints.empty(),
          "response cannot change requester or a full-location field");
    pump(static_cast<int>(ACTIVATION_LEASE_MS));
    host->sendHostActivationPoint(activationPoint);
    pump(50);
    check(c1Seen.activationPoints.size() == 1,
          "relay drops a response whose challenge routing lease expired");

    std::cout << "\n=== Desync detection and resync ===\n";
    const StateHash good {2, 4, 0x1B, hashAppliedEnemies({{1, 0x210, 153}, {2, 0x210, 160}}), 20};
    host->sendStateHash(good);
    c1->sendStateHash(good);
    pump(300);
    check(relay.desyncNoticeCount() == 0 && hostSeen.desyncs.empty(),
          "matching nonempty applied enemy hashes raise nothing");
    const StateHash diverged {2, 4, 0x1B, hashAppliedEnemies({{1, 0x210, 152}, {2, 0x210, 160}}), 20};
    c2->sendStateHash(diverged);
    pump(150);
    check(relay.desyncNoticeCount() == 0, "a single mismatch (e.g. mid-load) is tolerated");
    c2->sendStateHash(diverged);
    check(waitFor([&] { return !hostSeen.desyncs.empty(); }) &&
              hostSeen.desyncs.back().slot == SlotType::Friend2 &&
              hostSeen.desyncs.back().fields == DesyncEnemies &&
              hostSeen.desyncs.back().epoch == 2,
          "a persistent mismatch is reported to everyone, naming client 2 and the enemy set");
    c2->sendStateHash(diverged);
    c2->sendStateHash(diverged);
    pump(300);
    check(relay.desyncNoticeCount() == 1, "the same desync isn't reported twice");

    const auto c2RoomsBefore = c2Seen.rooms.size();
    const auto c1RoomsBefore = c1Seen.rooms.size();
    check(!c1->requestWorldResync(4), "a non-host cannot submit fresh host resync");
    check(c2Seen.rooms.size() == c2RoomsBefore, "a non-host resync request is dropped");
    c2->disconnect();
    check(waitFor([&] { return relay.verifiedPeerCount() == 2; }), "selected cache-probe friend departs");
    c2->connect();
    check(waitFor([&] { return c2->worldReady(); }), "selected cache-probe friend rejoins unchanged host");
    check(waitFor([&] { return c2Seen.rooms.size() > c2RoomsBefore; }) &&
              c2Seen.rooms.back().epoch == 2 && c1Seen.rooms.size() == c1RoomsBefore,
          "selected friend rejoin re-sends the current room to client 2 only");
    check(c2Seen.activationPoints.empty(), "friend rejoin does not replay activation points");

    activation.location = *relay.currentRoom();
    activation.requestSeq = 3;
    const auto beforeReload = hostSeen.activationRequests.size();
    c1->sendActivationRequest(activation);
    check(waitFor([&] { return hostSeen.activationRequests.size() > beforeReload; }),
          "challenge exists before same-room reload");
    activationPoint.request = hostSeen.activationRequests.back();
    activationPoint.sourceSeq = 3;
    auto reloadedRoom = *relay.currentRoom();
    ++reloadedRoom.epoch;
    host->sendRoomTransition(reloadedRoom);
    check(waitFor([&] { return relay.currentRoom()->epoch == reloadedRoom.epoch; }),
          "same-room reload advances host epoch");
    host->sendHostActivationPoint(activationPoint);
    pump(50);
    check(c1Seen.activationPoints.size() == 1,
          "same-room reload invalidates pending old activation response");

    activation.location = reloadedRoom;
    activation.requestSeq = 4;
    const auto beforeRejoin = hostSeen.activationRequests.size();
    c1->sendActivationRequest(activation);
    check(waitFor([&] { return hostSeen.activationRequests.size() > beforeRejoin; }),
          "challenge exists before requester disconnect");
    activationPoint.request = hostSeen.activationRequests.back();
    activationPoint.sourceSeq = 4;
    c1->disconnect();
    live = {host.get(), c2.get()};
    check(waitFor([&] { return relay.verifiedPeerCount() == 2; }), "requester disconnect is observed");
    c1 = makeClient("c1", SlotType::Friend1, c1Seen);
    live = {host.get(), c1.get(), c2.get()};
    check(waitFor([&] { return relay.verifiedPeerCount() == 3 && c1->worldReady(); }), "requester rejoins same slot");
    host->sendHostActivationPoint(activationPoint);
    pump(50);
    check(c1Seen.activationPoints.size() == 1,
          "rejoined slot cannot inherit old connection's challenge");

    host->sendEnemyManifest(EnemyManifest {reloadedRoom.epoch, true, {entry(1, 0, 309)}});
    check(waitFor([&] { return relay.manifestSize() == 1; }), "reconnect typed target reaches relay");
    claim.epoch = reloadedRoom.epoch;
    claim.objectId = 309;
    claim.seq = 1;
    rejectsClaim(claim, "old echoed connection is rejected after same-slot same-peer reconnect");
    claim.requesterConnectionId = relay.peerBySlot(SlotType::Friend1)->connectionId;
    check(claim.requesterConnectionId > originalConnectionId, "reconnect receives a fresh monotonic identity");
    auto rejoinedClaimsBefore = hostSeen.claims.size();
    c1->sendHitClaim(claim);
    check(waitFor([&] { return hostSeen.claims.size() == rejoinedClaimsBefore + 1; }) &&
              hostSeen.claims.back().requesterConnectionId == claim.requesterConnectionId,
          "new connection restarts its claim sequence without aliasing the old connection");
    claim.seq = (std::numeric_limits<std::uint32_t>::max)();
    rejoinedClaimsBefore = hostSeen.claims.size();
    c1->sendHitClaim(claim);
    check(waitFor([&] { return hostSeen.claims.size() == rejoinedClaimsBefore + 1; }),
          "largest sequence is accepted once");
    claim.seq = 1;
    rejectsClaim(claim, "sequence wrap is rejected until a new connection is established");

    std::cout << "\n=== Host leaves ===\n";
    host->sendEnemyManifest(EnemyManifest {reloadedRoom.epoch, true, {entry(1, 0, 309)}});
    host->sendEventHold(EventHold {reloadedRoom.epoch, true, 0x33});
    host->sendEnemyHp(EnemyHp {reloadedRoom.epoch, {{1, 153, 160}}, ++hpSequence});
    host->sendEnemyDeath(EnemyDeath {reloadedRoom.epoch, 1});
    activation.requestSeq = 5;
    const auto requestsBeforeLoss = hostSeen.activationRequests.size();
    c1->sendActivationRequest(activation);
    check(waitFor([&] { return relay.manifestSize() == 1 && hostSeen.activationRequests.size() > requestsBeforeLoss; }),
          "host-loss fixture has cached enemy state and an outstanding challenge");
    const auto hostLeftBefore = left["host"], c1LeftBefore = left["c1"], c2LeftBefore = left["c2"];
    const auto c1DisconnectsBefore = c1Seen.disconnects, c2DisconnectsBefore = c2Seen.disconnects;
    host->disconnect();
    live = {c1.get(), c2.get()};
    check(waitFor([&] { return !relay.currentRoom().has_value(); }),
          "host leaving ends the world (no host migration)");
    check(waitFor([&] { return !c1->isConnected() && !c2->isConnected(); }) &&
              relay.peers().empty() && relay.verifiedPeerCount() == 0 && relay.sessionState().actors.empty() &&
              !relay.peerBySlot(SlotType::Player) && !relay.peerBySlot(SlotType::Friend1) && !relay.peerBySlot(SlotType::Friend2) &&
              relay.manifestSize() == 0 && relay.progressBytes() == 0 && relay.isRunning(),
          "host departure disconnects both friends, clears peer/world state, and preserves the listening relay");
    const auto claimsBeforeLossProbe = hostSeen.claims.size();
    c1->sendHitClaim(claim);
    pump(80);
    check(hostSeen.claims.size() == claimsBeforeLossProbe && c1Seen.claims.empty() && c2Seen.claims.empty(),
          "a departed-session hit claim cannot reach another peer");
    check(left["host"] == hostLeftBefore + 1,
          "departing host callback occurs exactly once");
    check(left["c1"] == c1LeftBefore + 1 && left["c2"] == c2LeftBefore + 1 &&
              c1Seen.disconnects == c1DisconnectsBefore + 1 && c2Seen.disconnects == c2DisconnectsBefore + 1,
          "remaining peers and late disconnect events notify each friend exactly once");

    Seen freshHostSeen, freshFriendSeen;
    auto freshHost = makeClient("fresh-host", SlotType::Player, freshHostSeen);
    auto freshFriend = makeClient("fresh-friend", SlotType::Friend1, freshFriendSeen);
    live = {freshHost.get(), freshFriend.get(), c1.get(), c2.get()};
    check(waitFor([&] { return relay.verifiedPeerCount() == 2 && freshHost->worldReady() && freshFriend->worldReady() && relay.peerBySlot(SlotType::Player) &&
                              relay.peerBySlot(SlotType::Player)->peerId == "fresh-host" &&
                              relay.peerBySlot(SlotType::Friend1) && relay.peerBySlot(SlotType::Friend1)->peerId == "fresh-friend"; }),
          "fresh host and friend establish new connections without inheriting old membership");
    check(relay.peerBySlot(SlotType::Friend1) &&
              relay.peerBySlot(SlotType::Friend1)->connectionId > claim.requesterConnectionId,
          "host-loss teardown does not reuse connection identities for new slot occupants");
    pump(80);
    check(freshFriendSeen.rooms.empty() && freshFriendSeen.holds.empty() && freshFriendSeen.manifests.empty() &&
              freshFriendSeen.hps.empty() && freshFriendSeen.deaths.empty() && freshFriendSeen.progress.empty() &&
              freshFriendSeen.activationPoints.empty() && !relay.currentRoom(),
          "fresh join receives none of the departed host's cached world or progress");
    freshHost->sendRoomTransition(RoomTransition {1, 4, 26, 0, 0, 0, 0});
    freshHost->sendProgressUpdate(ProgressUpdate {1, true, {{0x1CFF, {2}}}});
    check(waitFor([&] { return freshFriendSeen.rooms.size() == 1 && freshFriendSeen.progress.size() == 1; }) &&
              freshFriendSeen.rooms.back().epoch == 1 && freshFriendSeen.progress.back().version == 1,
          "fresh host publishes a new epoch and progress version independently of the old session");
    freshHost->disconnect();
    freshFriend->disconnect();

    c1->disconnect();
    c2->disconnect();
    relay.stop();
    testWorldCacheEpoch();
    testHostHeartbeatExpiry();
    enet_deinitialize();

    std::cout << "\n=======================================\n"
              << (g_errors == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED: " + std::to_string(g_errors))
              << "\n";
    return g_errors == 0 ? 0 : 1;
}
