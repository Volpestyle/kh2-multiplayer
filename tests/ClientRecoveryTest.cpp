#include "WorldWireFixture.hpp"
// Production scheduler + real ENet recovery controls; no KH2/native callbacks.
#include "kh2coop/ClientRecovery.hpp"
#include "kh2coop/SessionHost.hpp"
#include "kh2coop/ProgressMirror.hpp"
#include <enet/enet.h>
#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>
using namespace kh2coop;
namespace {
int failures = 0, checks = 0;
void check(bool ok, const char* label) {
    ++checks; if (!ok) ++failures;
    std::cout << (ok ? "PASS: " : "FAIL: ") << label << '\n';
}
using Recovery = ClientRecovery;
using Action = Recovery::Action;
using State = Recovery::State;
ClientCloseInfo closeInfo(DisconnectReason reason) {
    ClientCloseInfo info; info.reason = reason; info.rawCode = static_cast<std::uint32_t>(reason); return info;
}
void admit(Recovery& r, std::uint64_t now = 0, std::uint64_t id = 0x100000002ULL) {
    (void)r.start(now); r.connected(now); r.admitted(now, id);
}
void testScheduler() {
    std::cout << "=== Production bounded recovery scheduler ===\n";
    Recovery initial(SlotType::Friend1);
    check(initial.tick(0) == Action::None && initial.start(100) == Action::Connect && initial.start(100) == Action::None,
          "initial connection is explicit and emitted exactly once");
    check(initial.tick(4099) == Action::None && initial.tick(4100) == Action::Disconnect && initial.state() == State::Terminal,
          "initial transport timeout at exactly 4 seconds is terminal");
    check(initial.tick(100000) == Action::None && initial.attempts() == 0, "initial timeout never schedules a retry");
    Recovery immediate(SlotType::Friend1); (void)immediate.start(0); immediate.initiationFailed(1);
    check(immediate.state() == State::Terminal && !immediate.terminalReason().empty() && immediate.tick(10000) == Action::None,
          "initial initiation failure is visible terminal, without retry");
    Recovery handshake(SlotType::Friend1); (void)handshake.start(0); handshake.connected(3999);
    check(handshake.tick(7998) == Action::None && handshake.tick(7999) == Action::Disconnect && handshake.state() == State::Terminal,
          "transport success starts a separate exact 4-second roster deadline");
    Recovery late(SlotType::Friend1); (void)late.start(0); late.connected(4000);
    check(late.state() == State::Terminal && late.tick(4000) == Action::Disconnect, "late connect callback cannot evade transport deadline");
    Recovery lateRoster(SlotType::Friend1); (void)lateRoster.start(0); lateRoster.connected(1); lateRoster.admitted(4001, 2);
    check(lateRoster.state() == State::Terminal && lateRoster.tick(4001) == Action::Disconnect, "late roster cannot evade verification deadline");
    for (const auto reason : {DisconnectReason::TransportLost, DisconnectReason::PeerIdleTimeout, DisconnectReason::HandshakeTimeout,
                              DisconnectReason::SlotOccupied, DisconnectReason::LobbyFull}) {
        Recovery r(SlotType::Friend1); admit(r); r.closed(100, closeInfo(reason));
        check(r.state() == State::RetryWait && r.tick(1099) == Action::None && r.tick(1100) == Action::Connect && r.attempts() == 1,
              "allowed typed transient permits one bounded previously admitted friend retry");
        Recovery unpinned(SlotType::Friend1); (void)unpinned.start(0); unpinned.closed(1, closeInfo(reason));
        check(unpinned.state() == State::Terminal, "same transient does not authorize initial unpinned retry");
    }
    for (const auto reason : {DisconnectReason::Incompatible, DisconnectReason::AdmissionRejected, DisconnectReason::HostSessionEnded,
                              DisconnectReason::RelayStopping, DisconnectReason::SessionChanged, static_cast<DisconnectReason>(999)}) {
        Recovery r(SlotType::Friend2); admit(r); r.closed(100, closeInfo(reason));
        check(r.state() == State::Terminal && r.tick(100000) == Action::None, "terminal or unknown close never retries");
    }
    for (const auto reason : {DisconnectReason::Incompatible, DisconnectReason::AdmissionRejected, DisconnectReason::SessionChanged,
                              DisconnectReason::TransportLost, static_cast<DisconnectReason>(123)}) {
        Recovery r(SlotType::Friend1); admit(r); r.closed(100, closeInfo(DisconnectReason::TransportLost));
        (void)r.tick(1100); r.connected(1101); r.rejected(1200, reason);
        check(r.state() == State::Terminal && r.tick(6000) == Action::Disconnect && r.tick(6001) == Action::None,
              "terminal HelloReject wins over delayed disconnect and roster deadline");
    }
    Recovery occupied(SlotType::Friend1); admit(occupied); occupied.closed(100, closeInfo(DisconnectReason::TransportLost));
    (void)occupied.tick(1100); occupied.connected(1101); occupied.rejected(1200, DisconnectReason::SlotOccupied);
    occupied.closed(1250, closeInfo(DisconnectReason::SlotOccupied));
    check(occupied.tick(1300) == Action::Disconnect && occupied.tick(3199) == Action::None && occupied.tick(3200) == Action::Connect && occupied.attempts() == 2,
          "retryable occupied rejection closes once and preserves one backoff despite later close callback");
    Recovery host(SlotType::Player); admit(host); host.closed(100, closeInfo(DisconnectReason::TransportLost));
    check(host.state() == State::Terminal, "Player never automatically rejoins");
    Recovery local(SlotType::Friend1); admit(local); auto info = closeInfo(DisconnectReason::TransportLost); info.local = true;
    local.closed(100, info); check(local.state() == State::Terminal, "local closure cannot authorize recovery");
    Recovery cancelled(SlotType::Friend2); admit(cancelled); cancelled.closed(100, closeInfo(DisconnectReason::TransportLost)); cancelled.shutdown();
    check(cancelled.tick(10000) == Action::None && cancelled.start(10000) == Action::None, "explicit shutdown cancels and cannot restart implicitly");

    Recovery budget(SlotType::Friend1); admit(budget); std::uint64_t now = 100;
    budget.closed(now, closeInfo(DisconnectReason::TransportLost));
    const std::array<std::uint64_t, 5> delays {1000, 2000, 4000, 8000, 8000};
    for (std::size_t i = 0; i < delays.size(); ++i) {
        check(budget.tick(now + delays[i] - 1) == Action::None, "retry does not precede its specified backoff");
        now += delays[i];
        check(budget.tick(now) == Action::Connect && budget.attempts() == i + 1 && budget.tick(now) == Action::None,
              "specified retry emits only one outstanding attempt");
        budget.connected(now); budget.admitted(now, 0x200000000ULL + i);
        check(budget.state() == State::Admitted && budget.attempts() == i + 1, "short admission flare does not reset episode budget");
        now += 10; budget.closed(now, closeInfo(DisconnectReason::PeerIdleTimeout));
    }
    check(budget.state() == State::Terminal && budget.attempts() == 5 && budget.tick(now + 100000) == Action::None,
          "five rejoin attempts exhaust episode despite successful roster flares");
    Recovery paused(SlotType::Friend1); admit(paused); paused.closed(100, closeInfo(DisconnectReason::TransportLost));
    check(paused.tick(20000) == Action::Connect && paused.tick(20000) == Action::None && paused.attempts() == 1,
          "long loop pause produces one attempt, never a catch-up burst");
    check(paused.tick(1000) == Action::None && paused.tick(23999) == Action::None && paused.tick(24000) == Action::Disconnect,
          "clock rewind cannot extend attempt or create rapid reconnects");
    check(paused.tick(25999) == Action::None && paused.tick(26000) == Action::Connect, "timed-out attempt schedules next delay internally");
    paused.connected(26000); paused.admitted(26000, 0x200000002ULL);
    check(paused.tick(60099) == Action::None && paused.attempts() == 0, "stable admitted membership resets old episode budget after ten seconds");

    Recovery episode(SlotType::Friend1); admit(episode); episode.closed(100, closeInfo(DisconnectReason::TransportLost));
    check(episode.tick(60100) == Action::None && episode.state() == State::Terminal && episode.attempts() == 0,
          "60-second episode expires while waiting without a late connect");
    Recovery activeExpiry(SlotType::Friend1); admit(activeExpiry); activeExpiry.closed(100, closeInfo(DisconnectReason::TransportLost));
    check(activeExpiry.tick(59000) == Action::Connect, "late but bounded retry can start before episode deadline");
    activeExpiry.connected(59001); activeExpiry.admitted(59002, 0x300000002ULL);
    check(activeExpiry.tick(60100) == Action::Disconnect && activeExpiry.state() == State::Terminal,
          "brief late admission does not evade the 60-second episode deadline");
    Recovery identity(SlotType::Friend1); admit(identity); identity.closed(100, closeInfo(DisconnectReason::TransportLost));
    (void)identity.tick(1100); identity.connected(1100); identity.admitted(1100, 0x100000002ULL);
    check(identity.state() == State::Terminal && identity.tick(1100) == Action::Disconnect, "rejoin must change the complete self connection identity");
    Recovery stable(SlotType::Friend1); admit(stable); stable.closed(100, closeInfo(DisconnectReason::TransportLost));
    (void)stable.tick(1100); stable.connected(1100); stable.admitted(1100, 0x200000002ULL);
    stable.admitted(10999, 0x200000002ULL);
    check(stable.tick(11099) == Action::None && stable.attempts() == 1, "repeated roster does not prematurely clear budget");
    check(stable.tick(11100) == Action::None && stable.attempts() == 0, "exactly ten seconds of admitted membership clears budget");
    stable.closed(11101, closeInfo(DisconnectReason::TransportLost));
    check(stable.tick(12101) == Action::Connect && stable.attempts() == 1, "later independent network loss starts a fresh bounded episode");
    stable.shutdown(); stable.connected(12102); stable.admitted(12103, 5);
    check(stable.state() == State::Terminal && stable.tick(13000) == Action::None, "callbacks after explicit shutdown cannot rearm recovery");
}
std::uint64_t nowMs() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
struct WorldSeen {
    std::vector<PacketType> order;
    std::vector<ProgressUpdate> progress;
    std::vector<RoomTransition> rooms;
    std::vector<EventHold> holds;
    std::vector<EnemyManifest> manifests;
    std::vector<EnemyHp> hp;
    std::vector<EnemyDeath> deaths;
    unsigned activation = 0, avatars = 0;
};
ClientCallbacks worldCallbacks(WorldSeen& seen) {
    ClientCallbacks cb;
    cb.onWorldPacket = [&](const auto& packet) { const std::uint8_t* p = nullptr; std::size_t n = 0;
        seen.order.push_back(decodePacketHeader(packet.data(), packet.size(), p, n)); };
    cb.onProgressUpdate = [&](const auto& v) { seen.progress.push_back(v); };
    cb.onRoomTransition = [&](const auto& v) { seen.rooms.push_back(v); };
    cb.onEventHold = [&](const auto& v) { seen.holds.push_back(v); };
    cb.onEnemyManifest = [&](const auto& v) { seen.manifests.push_back(v); };
    cb.onEnemyHp = [&](const auto& v) { seen.hp.push_back(v); };
    cb.onEnemyDeath = [&](const auto& v) { seen.deaths.push_back(v); };
    cb.onActivationRequest = [&](const auto&) { ++seen.activation; };
    cb.onHostActivationPoint = [&](const auto&) { ++seen.activation; };
    cb.onAvatarState = [&](const auto&) { ++seen.avatars; };
    return cb;
}
SessionConfig config(std::uint16_t port) {
    SessionConfig c; c.bindAddress = "127.0.0.1"; c.port = port; c.maxPeers = 3;
    c.gameBuild = "recovery-build"; c.modHash = "recovery-mod"; c.contentHash = "recovery-content";
    c.sessionId = "same-label-is-not-incarnation"; return c;
}
SessionResumePin pinFor(const SessionState& ss, const char* local) {
    SessionResumePin pin; pin.sessionId = ss.sessionId; pin.localPeerId = local; pin.localSlot = SlotType::Friend1;
    for (const auto& actor : ss.actors) if (actor.slot == SlotType::Player) {
        pin.hostPeerId = actor.ownerPeerId; pin.hostConnectionId = actor.connectionId;
    }
    return pin;
}
std::uint64_t idFor(const SessionState& ss, SlotType slot) {
    for (const auto& actor : ss.actors) if (actor.slot == slot) return actor.connectionId;
    return 0;
}

void testExistingHostRecovery() {
    std::cout << "=== Actual three-peer timeout, scheduled rejoin and full bootstrap ===\n";
    auto cfg = config(17821); cfg.heartbeatTimeoutMs = 250; cfg.pendingPeerTimeoutMs = 1000;
    SessionHost relay(cfg, {}); const bool started = relay.start();
    check(started, "recovery relay starts on loopback"); if (!started) return;
    WorldSeen seen;
    Recovery recovery(SlotType::Friend1);
    NetworkClient* friendPtr = nullptr;
    std::optional<SessionResumePin> pin;
    std::uint64_t initialSelf = 0, recoveredSelf = 0;
    unsigned admittedCount = 0, closeCount = 0;
    ClientCloseInfo lastClose;
    std::vector<ActivationRequest> hostRequests;
    ClientCallbacks hostCb; hostCb.onActivationRequest = [&](const auto& r) { hostRequests.push_back(r); };
    NetworkClient host("127.0.0.1", cfg.port, cfg.gameBuild, cfg.modHash, "host", SlotType::Player,
                       std::move(hostCb), RuntimeMode::CampaignCoop, cfg.contentHash);
    NetworkClient survivor("127.0.0.1", cfg.port, cfg.gameBuild, cfg.modHash, "survivor", SlotType::Friend2,
                           {}, RuntimeMode::CampaignCoop, cfg.contentHash);
    auto cb = worldCallbacks(seen);
    cb.onConnected = [&] { recovery.connected(nowMs()); };
    cb.onRejected = [&](const HelloReject& r) { recovery.rejected(nowMs(), static_cast<DisconnectReason>(r.code)); };
    cb.onClosed = [&](const ClientCloseInfo& info) {
        ++closeCount; lastClose = info; seen = {}; recovery.closed(nowMs(), info);
    };
    cb.onSessionState = [&](const SessionState& ss) {
        if (!friendPtr || !friendPtr->ready()) return;
        const auto id = idFor(ss, SlotType::Friend1);
        if (!pin) { pin = pinFor(ss, "friend"); initialSelf = id; }
        if (id != initialSelf) recoveredSelf = id;
        ++admittedCount; recovery.admitted(nowMs(), id);
    };
    NetworkClient friendClient("127.0.0.1", cfg.port, cfg.gameBuild, cfg.modHash, "friend", SlotType::Friend1,
                               std::move(cb), RuntimeMode::CampaignCoop, cfg.contentHash);
    friendPtr = &friendClient;
    bool driveFriend = true;
    const auto perform = [&](Action action) {
        if (action == Action::Connect) {
            const bool pinned = friendClient.SetResumePin(pin);
            check(pinned, "recovery attempt installs the retained pin before transport start");
            if (!pinned || !friendClient.connect()) recovery.initiationFailed(nowMs());
        } else if (action == Action::Disconnect) friendClient.disconnect();
    };
    const auto pump = [&] {
        host.tick(0); survivor.tick(0);
        host.sendHeartbeat(); survivor.sendHeartbeat();
        if (driveFriend) {
            friendClient.tick(0);
            perform(recovery.tick(nowMs()));
            if (friendClient.ready()) friendClient.sendHeartbeat();
        }
        relay.tick(0);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    };
    const auto wait = [&](const std::function<bool()>& pred, std::uint64_t timeout = 4000) {
        const auto deadline = nowMs() + timeout;
        while (nowMs() < deadline && !pred()) pump();
        return pred();
    };
    host.connect();
    check(wait([&] { return host.worldReady(); }), "authoritative host verifies before friend recovery fixture");
    survivor.connect(); perform(recovery.start(nowMs()));
    check(wait([&] { return relay.verifiedPeerCount() == 3 && friendClient.worldReady() && survivor.worldReady(); }),
          "all three peers admitted before populated-world drop");
    const auto hostId = idFor(relay.sessionState(), SlotType::Player);
    const auto sessionId = relay.sessionState().sessionId;
    check(pin && pin->sessionId == sessionId && pin->hostConnectionId == hostId && initialSelf != 0,
          "friend pins actual admitted opaque session and host identity");
    const RoomTransition room {17, 4, 26, 3, 0x1234, 0x2345, 0x3456};
    EnemyManifestEntry a; a.netId = 11; a.objectId = 309; a.spawnIndex = 1;
    EnemyManifestEntry b; b.netId = 12; b.objectId = 311; b.spawnIndex = 2;
    host.sendRoomTransition(room);
    host.sendEventHold({17, true, 0x3456});
    host.sendEnemyManifest({17, true, {a, b}});
    host.sendEnemyHp({17, {{11, 13, 20}, {12, 0, 30}}, 1});
    host.sendEnemyDeath({17, 12});
    host.sendProgressUpdate({1, true, {{0x1C00, {1, 2, 3}}}});
    host.sendProgressUpdate({2, false, {{0x1C01, {9}}}});
    check(wait([&] { return seen.progress.size() == 2 && !seen.rooms.empty() && !seen.holds.empty() &&
        !seen.manifests.empty() && !seen.hp.empty() && !seen.deaths.empty(); }), "nonempty full world cache is established before drop");
    friendClient.sendActivationRequest({room, {111, 222}, 1, 1});
    check(wait([&] { return !hostRequests.empty(); }), "ephemeral challenge exists before the disconnected friend");
    if (!hostRequests.empty()) host.sendHostActivationPoint({hostRequests.back(), 1, {1, 2, 3, 1}});
    check(wait([&] { return seen.activation == 1; }), "original activation response delivered before drop");
    driveFriend = false; // no tick/ClockPing/heartbeat from this friend; others remain active
    check(wait([&] { return relay.verifiedPeerCount() == 2; }), "only Friend1 expires while host and other friend keep sending");
    check(relay.peerBySlot(SlotType::Player) && relay.peerBySlot(SlotType::Friend2) &&
          idFor(relay.sessionState(), SlotType::Player) == hostId && relay.currentRoom() && relay.currentRoom()->epoch == 17,
          "host identity and populated world survive friend timeout");
    driveFriend = true;
    check(wait([&] { return closeCount == 1; }) && lastClose.reason == DisconnectReason::PeerIdleTimeout && lastClose.hadReady,
          "actual close identifies the recoverable peer-only timeout");
    check(recovery.state() == State::RetryWait && !friendClient.ready(), "supervisor waits instead of reconnecting immediately");
    check(wait([&] { return recoveredSelf != 0 && seen.deaths.size() == 1; }, 6000), "scheduled retry alone rejoins and receives complete bootstrap without host resend");
    check(closeCount == 1 && admittedCount >= 2 && initialSelf != recoveredSelf && friendClient.ready() &&
          recovery.state() == State::Admitted && recovery.attempts() == 1 && relay.verifiedPeerCount() == 3,
          "one actual bounded rejoin obtains fresh self identity and remains admitted");
    check(relay.sessionState().sessionId == sessionId && idFor(relay.sessionState(), SlotType::Player) == hostId,
          "rejoin resumes the original host session, not a replacement world");
    const std::vector<PacketType> expected {PacketType::ProgressUpdate, PacketType::RoomTransition, PacketType::EventHold,
        PacketType::EnemyManifest, PacketType::EnemyHp, PacketType::EnemyDeath};
    check(seen.order == expected, "bootstrap order is full progress, room, hold, manifest, HP, death exactly once");
    check(seen.progress.size() == 1 && seen.progress[0].full && seen.progress[0].version == 2 &&
          seen.progress[0].spans.size() == 1 && seen.progress[0].spans[0].offset == 0x1C00 &&
          seen.progress[0].spans[0].bytes == std::vector<std::uint8_t>({1, 9, 3}), "rejoin full progress contains the merged pre-drop delta");
    check(seen.rooms.size() == 1 && (encode(seen.rooms[0]) == encode(room)) && seen.holds.size() == 1 &&
          seen.holds[0].active && seen.holds[0].eventProgram == 0x3456, "rejoin preserves exact room/program and hold values");
    check(seen.manifests.size() == 1 && seen.manifests[0].replace && seen.manifests[0].entries.size() == 2 &&
          seen.manifests[0].entries[0].objectId == 309 && seen.manifests[0].entries[1].objectId == 311,
          "rejoin gets exact replacing typed manifest");
    bool hpExact = seen.hp.size() == 1 && seen.hp[0].entries.size() == 2;
    if (hpExact) {
        unsigned ids = 0;
        for (const auto& e : seen.hp[0].entries) {
            hpExact &= (e.netId == 11 && e.hp == 13 && e.maxHp == 20) || (e.netId == 12 && e.hp == 0 && e.maxHp == 30);
            ids |= e.netId == 11 ? 1u : (e.netId == 12 ? 2u : 0u);
        }
        hpExact &= ids == 3 && seen.hp[0].epoch == 17;
    }
    check(hpExact && seen.deaths.size() == 1 && seen.deaths[0].epoch == 17 && seen.deaths[0].netId == 12,
          "rejoin retains checked cached HP and recorded death values");
    check(seen.activation == 0, "rejoin bootstrap does not replay old activation lease or challenge");
    host.disconnect();
    check(wait([&] { return closeCount == 2; }) && lastClose.reason == DisconnectReason::HostSessionEnded &&
          recovery.state() == State::Terminal, "actual authoritative host loss terminates recovered friend without another retry");
    recovery.shutdown(); friendClient.disconnect(); survivor.disconnect(); relay.stop();
}

void testRestartedRelayPin() {
    std::cout << "=== Same-config restarted relay cannot satisfy old incarnation pin ===\n";
    auto cfg = config(17822);
    auto relay = std::make_unique<SessionHost>(cfg, SessionCallbacks {});
    const bool started = relay->start();
    check(started, "restart-pin original relay starts"); if (!started) return;
    WorldSeen seen; std::vector<ClientCloseInfo> closed;
    SessionState lastSession;
    auto cb = worldCallbacks(seen);
    cb.onSessionState = [&](const auto& ss) { lastSession = ss; };
    cb.onClosed = [&](const auto& c) { closed.push_back(c); };
    NetworkClient client("127.0.0.1", cfg.port, cfg.gameBuild, cfg.modHash, "friend", SlotType::Friend1,
                         std::move(cb), RuntimeMode::CampaignCoop, cfg.contentHash);
    NetworkClient host("127.0.0.1", cfg.port, cfg.gameBuild, cfg.modHash, "host", SlotType::Player,
                       {}, RuntimeMode::CampaignCoop, cfg.contentHash);
    const auto pump = [&] { relay->tick(0); host.tick(0); client.tick(0); std::this_thread::sleep_for(std::chrono::milliseconds(1)); };
    const auto wait = [&](const std::function<bool()>& pred) {
        const auto end = nowMs() + 3000; while (nowMs() < end && !pred()) pump(); return pred();
    };
    host.connect(); check(wait([&] { return host.worldReady(); }), "restart-pin original host admitted");
    client.connect(); check(wait([&] { return client.ready(); }), "restart-pin original friend admitted");
    const auto pin = pinFor(lastSession, "friend");
    const auto oldSelf = idFor(lastSession, SlotType::Friend1);
    // Deliberately discard transport shutdown notifications, modelling a caller
    // that only knows transport was lost. No graceful-stop signal authorizes the
    // resume here. Both subsequent relay handshakes/packets are real ENet.
    client.disconnect(); host.disconnect(); relay->stop(); relay.reset();
    seen = {}; closed.clear(); lastSession = {};
    std::uint64_t repeatedSelf = 0;
    SessionCallbacks restartedCallbacks;
    restartedCallbacks.onPeerJoined = [&](const std::string&, SlotType slot) {
        if (slot == SlotType::Friend1) {
            const auto* peer = relay->peerBySlot(slot);
            if (peer) repeatedSelf = peer->connectionId;
        }
    };
    relay = std::make_unique<SessionHost>(cfg, std::move(restartedCallbacks));
    const bool restarted = relay->start();
    check(restarted, "same-config restarted relay starts on the same endpoint"); if (!restarted) return;
    host.connect(); check(wait([&] { return host.worldReady(); }), "same peer/slot host joins restarted relay");
    check(relay->sessionState().sessionId != pin.sessionId && idFor(relay->sessionState(), SlotType::Player) == pin.hostConnectionId,
          "fresh opaque world incarnation differs even when small host connection ID repeats");
    host.sendRoomTransition({17, 4, 26, 3, 1, 2, 0});
    host.sendProgressUpdate({1, true, {{0x1C00, {42}}}});
    check(wait([&] { return relay->currentRoom().has_value() && relay->progressBytes() == 1; }), "replacement world has a nonempty bootstrap ready to send");
    check(client.SetResumePin(pin) && client.connect(), "old valid pin is installed before reconnecting to replacement relay");
    check(wait([&] { return !closed.empty(); }), "replacement relay is rejected by actual client pin validation");
    check(closed.size() == 1 && closed[0].reason == DisconnectReason::SessionChanged && closed[0].local && !client.ready(),
          "pin mismatch produces exactly one terminal typed close before readiness");
    check(repeatedSelf != 0 && repeatedSelf == oldSelf, "new relay can repeat the previous small self connection ID without aliasing session");
    check(seen.order.empty() && seen.progress.empty() && seen.rooms.empty(),
          "mismatching session emits no world/raw callback from queued bootstrap (no avatar sent here)");
    client.disconnect(); host.disconnect(); relay->stop();
}

void testProtocolFourRefused() {
    auto cfg = config(17823); SessionHost relay(cfg, {});
    const bool started = relay.start();
    check(started, "v4 refusal relay starts with free capacity"); if (!started) return;
    std::vector<ClientCloseInfo> closed; std::string rejection;
    ClientCallbacks cb; cb.onClosed = [&](const auto& c) { closed.push_back(c); };
    cb.onRejected = [&](const auto& r) { rejection = r.reason; };
    NetworkClient client("127.0.0.1", cfg.port, cfg.gameBuild, cfg.modHash, "legacy-v4", SlotType::Friend1,
                         std::move(cb), RuntimeMode::CampaignCoop, cfg.contentHash, 4);
    client.connect(); const auto deadline = nowMs() + 3000;
    while (nowMs() < deadline && closed.empty()) { relay.tick(0); client.tick(0); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    check(PROTOCOL_VERSION > 4 && closed.size() == 1 && closed[0].reason == DisconnectReason::Incompatible &&
          rejection == "Protocol mismatch: client=4 server=" + std::to_string(PROTOCOL_VERSION) && !client.ready() && relay.verifiedPeerCount() == 0,
          "v4 cannot advertise old incarnation semantics to the current recovery client");
    client.disconnect(); relay.stop();
}
void testCallbackQuarantineAndCloseLatch() {
    std::cout << "=== Actual ENet callback quarantine and rejection cause latch ===\n";
    ENetAddress address {};
    enet_address_set_host(&address, "127.0.0.1"); address.port = 17824;
    std::unique_ptr<ENetHost, decltype(&enet_host_destroy)> server(
        enet_host_create(&address, 2, 2, 0, 0), &enet_host_destroy);
    check(server != nullptr, "quarantine endpoint binds loopback");
    if (!server) return;
    ENetPeer* peer = nullptr;
    unsigned connections = 0, legacyCloses = 0, sessionCallbacks = 0;
    std::uint64_t lastPing = 0;
    std::array<unsigned, 6> delivered {}; // raw world, room, avatar, actor, enemy, event
    bool fieldsMatch = true;
    std::vector<ClientCloseInfo> closed;
    std::vector<HelloReject> rejected;
    const RoomTransition room {71, 4, 26, 2, 0x1234, 0x2345, 0x3456};
    AvatarRelay avatar; avatar.ownerConnectionId = 0x100000001ULL;
    avatar.avatar.ownerSlot = SlotType::Player; avatar.avatar.seq = 1;
    avatar.avatar.serverTimeMs = 1234; avatar.avatar.position.x = 42;
    ActorSnapshot actor; actor.snapshotId = 31; actor.actor.actorId = 71; actor.actor.hp = 55;
    EnemySnapshot enemy; enemy.snapshotId = 32; enemy.enemy.netId = 12; enemy.enemy.objectId = 309; enemy.enemy.hp = 18;
    EventMessage event; event.snapshotId = 33; event.type = EventType::RewardGranted; event.payloadJson = "{\"witness\":1}";
    const std::vector<std::vector<std::uint8_t>> witnesses {
        encode(room), encode(avatar), encode(actor), encode(enemy), encode(event)};
    ClientCallbacks cb;
    cb.onWorldPacket = [&](const auto& packet) { ++delivered[0]; fieldsMatch &= packet == witnesses[0]; };
    cb.onRoomTransition = [&](const auto& value) { ++delivered[1]; fieldsMatch &= encode(value) == witnesses[0]; };
    cb.onAvatarState = [&](const auto& value) { ++delivered[2]; fieldsMatch &= value.ownerConnectionId == avatar.ownerConnectionId &&
        value.avatar.seq == 1 && value.avatar.position.x == 42; };
    cb.onActorSnapshot = [&](const auto& value) { ++delivered[3]; fieldsMatch &= value.snapshotId == 31 && value.actor.actorId == 71 && value.actor.hp == 55; };
    cb.onEnemySnapshot = [&](const auto& value) { ++delivered[4]; fieldsMatch &= value.snapshotId == 32 && value.enemy.netId == 12 && value.enemy.hp == 18; };
    cb.onEvent = [&](const auto& value) { ++delivered[5]; fieldsMatch &= value.snapshotId == 33 && value.type == EventType::RewardGranted && value.payloadJson == event.payloadJson; };
    cb.onSessionState = [&](const auto&) { ++sessionCallbacks; };
    cb.onDisconnected = [&] { ++legacyCloses; };
    cb.onClosed = [&](const auto& info) { closed.push_back(info); };
    cb.onRejected = [&](const auto& value) { rejected.push_back(value); };
    NetworkClient client("127.0.0.1", address.port, "quarantine-build", "m", "quarantine-client",
                         SlotType::Friend1, std::move(cb), RuntimeMode::CampaignCoop, "c");
    const auto pump = [&] {
        ENetEvent incoming {};
        while (enet_host_service(server.get(), &incoming, 0) > 0) {
            if (incoming.type == ENET_EVENT_TYPE_CONNECT) { peer = incoming.peer; ++connections; }
            if (incoming.type == ENET_EVENT_TYPE_DISCONNECT && peer == incoming.peer) peer = nullptr;
            if (incoming.type == ENET_EVENT_TYPE_RECEIVE) {
                try {
                    const std::uint8_t* payload = nullptr; std::size_t n = 0;
                    if (decodePacketHeader(incoming.packet->data, incoming.packet->dataLength, payload, n) == PacketType::ClockPing) {
                        ByteReader reader(payload, n); ClockPing ping; read(reader, ping); lastPing = ping.clientSendMs;
                    }
                } catch (const std::exception&) { }
                enet_packet_destroy(incoming.packet);
            }
        }
        client.tick(0);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    };
    const auto wait = [&](const std::function<bool()>& condition) {
        const auto deadline = nowMs() + 2000;
        while (nowMs() < deadline && !condition()) pump();
        return condition();
    };
    const auto settle = [&] { const auto deadline = nowMs() + 40; while (nowMs() < deadline) pump(); };
    const auto send = [&](const std::vector<std::uint8_t>& bytes) {
        if (!peer) return false;
        ENetPacket* packet = enet_packet_create(bytes.data(), bytes.size(), ENET_PACKET_FLAG_RELIABLE);
        if (!packet) return false;
        if (enet_peer_send(peer, 0, packet) < 0) { enet_packet_destroy(packet); return false; }
        enet_host_flush(server.get()); return true;
    };
    std::uint64_t syntheticSource = 0;
    const auto sendWitnesses = [&] {
        bool queued = true;
        for (const auto& packet : witnesses) {
            const auto type = static_cast<PacketType>(packet.front());
            if (isScopedWorldPacket(type)) {
                WorldScope scope {"00112233445566778899aabbccddeeff", avatar.ownerConnectionId, 1,
                                  ++syntheticSource, 0x200000002ULL, 1};
                if (type == PacketType::ActorSnapshot || type == PacketType::EnemySnapshot || type == PacketType::EventMessage)
                    scope.kind = WorldSourceKind::Simulation;
                queued &= send(worldfixture::uncheckedEnvelope(scope, packet));
            } else queued &= send(packet);
        }
        return queued;
    };
    const auto connect = [&] {
        const auto previous = connections;
        return client.connect() && wait([&] { return connections == previous + 1 && client.isConnected(); });
    };
    SessionState roster; roster.sessionId = "00112233445566778899aabbccddeeff";
    roster.gameBuild = "quarantine-build"; roster.modHash = "m";
    SessionActor host; host.slot = SlotType::Player; host.ownerPeerId = "host"; host.connectionId = avatar.ownerConnectionId;
    SessionActor self; self.slot = SlotType::Friend1; self.ownerPeerId = "quarantine-client"; self.connectionId = 0x200000002ULL;
    roster.actors = {host, self};
    const auto sendBinding = [&] { return send(encode(WorldBinding{roster.sessionId, host.connectionId, self.connectionId, 1, 1})); };
    const SessionResumePin pin {roster.sessionId, host.ownerPeerId, host.connectionId, self.ownerPeerId, self.slot};

    check(connect() && wait([&] { return lastPing != 0; }), "quarantine transport connects and emits clock ping before admission");
    // Same reliable channel: observing this Pong proves all preceding witness
    // packets reached the receiver boundary before the valid roster is sent.
    check(sendWitnesses() && send(encode(ClockPong {lastPing, nowMs()})), "all pre-roster witness packets and clock barrier are actually queued");
    check(wait([&] { return client.hasClockSync(); }) && !client.ready() && delivered == std::array<unsigned, 6> {},
          "pre-roster raw/typed world, avatar, actor, enemy and event callbacks stay zero while clock passes");
    check(send(encode(roster)) && sendBinding() && sendWitnesses() && wait([&] { return delivered == std::array<unsigned, 6> {1, 1, 1, 1, 1, 1}; }) &&
          client.ready() && fieldsMatch && sessionCallbacks == 1,
          "same exact witness packets reach every callback with checked values after valid admission");
    client.disconnect(); settle();
    check(closed.empty() && legacyCloses == 0, "explicit local disconnect produces neither typed nor legacy close callback");

    for (int variant = 0; variant < 3; ++variant) {
        delivered = {}; closed.clear(); sessionCallbacks = 0;
        check(client.SetResumePin(pin) && connect(), "resumed quarantine attempt installs actual pin before connecting");
        auto invalid = roster;
        if (variant == 0) invalid.sessionId = "ffeeddccbbaa99887766554433221100";
        if (variant == 1) invalid.actors.erase(invalid.actors.begin()); // host missing
        auto packet = encode(invalid);
        if (variant == 2) packet.pop_back(); // declared roster payload truncated
        check(send(packet) && sendWitnesses(), "invalid resumed roster precedes actually queued world/avatar/replica witnesses");
        check(wait([&] { return !closed.empty(); }), "invalid resumed roster reaches terminal closure");
        settle();
        const char* label = variant == 0 ? "mismatched pin closes9 before every authoritative callback" :
                            variant == 1 ? "missing host roster closes9 before every authoritative callback" :
                                           "truncated resumed roster closes9 before every authoritative callback";
        check(closed.size() == 1 && closed[0].reason == DisconnectReason::SessionChanged && closed[0].rawCode == 9 &&
              closed[0].local && !client.ready() && delivered == std::array<unsigned, 6> {} && sessionCallbacks == 0, label);
        const auto before = legacyCloses;
        client.disconnect(); settle();
        check(closed.size() == 1 && legacyCloses == before, "explicit cleanup cannot add another close after local pin rejection");
    }

    delivered = {}; closed.clear(); rejected.clear(); lastPing = 0;
    check(client.SetResumePin(std::nullopt) && connect(), "fresh rejection-latch transport connects without a resume pin");
    check(send(encode(roster)) && sendBinding() && wait([&] { return client.worldReady(); }),
          "rejection-latch control starts with admitted membership before refusal");
    const HelloReject reject {1, "synthetic explicit incompatibility"};
    check(send(encode(reject)) && wait([&] { return rejected.size() == 1; }) && closed.empty(),
          "actual HelloReject1 is observed before any disconnect notification");
    check(!client.hasClockSync() && wait([&] { return lastPing != 0; }),
          "fresh rejection-latch transport has no clock sample before the ordered barrier");
    check(sendWitnesses() && send(encode(ClockPong {lastPing, nowMs()})),
          "post-rejection witness family and same-channel clock barrier are actually sent");
    check(wait([&] { return client.hasClockSync(); }) && delivered == std::array<unsigned, 6> {} && !client.ready(),
          "clock barrier proves post-rejection witnesses reached the boundary without authoritative callbacks");
    const auto legacyBefore = legacyCloses;
    if (peer) { enet_peer_disconnect(peer, 0); enet_host_flush(server.get()); }
    check(wait([&] { return !closed.empty(); }), "ENet disconnect0 arrives after explicit rejection");
    settle();
    check(closed.size() == 1 && legacyCloses == legacyBefore + 1 && closed[0].reason == DisconnectReason::Incompatible &&
          closed[0].rawCode == 0 && !closed[0].local && closed[0].hadReady && closed[0].rejection &&
          closed[0].rejection->code == reject.code && closed[0].rejection->reason == reject.reason,
          "one typed close preserves HelloReject1 and its value despite raw disconnect0");
    client.disconnect(); settle();
    check(closed.size() == 1 && legacyCloses == legacyBefore + 1, "explicit stop after remote rejection remains callback silent");
}
} // namespace
int main() {
    testScheduler();
    if (enet_initialize() != 0) return 2;
    testExistingHostRecovery(); testRestartedRelayPin(); testProtocolFourRefused(); testCallbackQuarantineAndCloseLatch();
    enet_deinitialize();
    std::cout << (failures ? "CHECKS FAILED: " : "ALL CHECKS PASSED: ") << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
