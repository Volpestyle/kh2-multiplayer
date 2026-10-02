// AvatarRelayTest — VUH-1492 offline gate: owner-authoritative avatars travel
// client -> relay -> other clients under simulated latency and loss, and the
// receivers' interpolated puppets track the owners' true paths.
//
// Unit checks: AvatarState / clock codec round-trips, LinkConditioner,
// AvatarInterpolator. End to end: a SessionHost plus 3 NetworkClients on
// loopback, each link conditioned to 50 ms each way (100 ms owner -> viewer)
// with 10 ms jitter and 2% loss, two clients with skewed clocks. Each client
// moves on its own circle; every other client renders it 150 ms in the past
// and compares against the analytic truth.
//
// Distance units are KH2 world units (~1 cm), so 50 units ~ 0.5 m.
// Exit code 0 = all checks passed.

#include "kh2coop/AvatarBridge.hpp"
#include "kh2coop/AvatarInterpolator.hpp"
#include "kh2coop/AvatarSync.hpp"
#include "kh2coop/Codec.hpp"
#include "kh2coop/LinkConditioner.hpp"
#include "kh2coop/NetworkClient.hpp"
#include "kh2coop/SessionHost.hpp"

#include <enet/enet.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <memory>
#include <thread>

using namespace kh2coop;

namespace {

int g_errors = 0;

void check(bool ok, const std::string& what) {
    std::cout << (ok ? "  PASS: " : "  FAIL: ") << what << "\n";
    if (!ok) ++g_errors;
}

std::uint64_t steadyMs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

float dist(const Vec3& a, const Vec3& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// ---------------------------------------------------------------------------

void testCodec() {
    std::cout << "\n=== Codec ===\n";
    AvatarState a;
    a.seq = 42;
    a.serverTimeMs = 123456789012ULL;
    a.ownerSlot = SlotType::Friend2;
    a.character = 3;
    a.colorVariant = 2;
    a.worldId = 4;
    a.roomId = 0x1A;
    a.position = {1.5f, -2.0f, 300.25f};
    a.rotationY = 1.25f;
    a.velocity = {3.0f, 0.0f, -4.0f};
    a.motionId = 151;
    a.motionTime = 12.5f;
    a.motionSpeed = 1.5f;
    a.flags = AvatarAirborne | AvatarDowned;
    a.hp = 77; a.maxHp = 120; a.mp = 9; a.maxMp = 15;

    const auto pkt = encode(a, PacketType::AvatarRelay);
    const std::uint8_t* payload = nullptr;
    std::size_t size = 0;
    const auto type = decodePacketHeader(pkt.data(), pkt.size(), payload, size);
    ByteReader r(payload, size);
    AvatarState b;
    read(r, b);
    check(type == PacketType::AvatarRelay, "avatar relay packet type");
    check(b.seq == a.seq && b.serverTimeMs == a.serverTimeMs &&
              b.ownerSlot == a.ownerSlot && b.character == 3 &&
              b.colorVariant == 2 && b.worldId == 4 && b.roomId == 0x1A,
          "avatar identity/room fields round-trip");
    check(dist(a.position, b.position) == 0.0f && b.rotationY == a.rotationY &&
              dist(a.velocity, b.velocity) == 0.0f,
          "avatar transform round-trips");
    check(b.motionId == 151 && b.motionTime == 12.5f && b.motionSpeed == 1.5f,
          "avatar motion round-trips");
    check(b.flags == a.flags && b.hp == 77 && b.maxHp == 120 && b.mp == 9 &&
              b.maxMp == 15,
          "avatar flags and stats round-trip");

    const auto pong = encode(ClockPong {111, 222});
    decodePacketHeader(pong.data(), pong.size(), payload, size);
    ByteReader r2(payload, size);
    ClockPong p;
    read(r2, p);
    check(p.clientSendMs == 111 && p.serverMs == 222, "clock pong round-trips");

    bool threw = false;
    try { encode(a, PacketType::Heartbeat); } catch (const std::exception&) { threw = true; }
    check(threw, "encode(AvatarState) rejects a non-avatar packet type");
}

void testLinkConditioner() {
    std::cout << "\n=== LinkConditioner ===\n";
    LinkConditioner lc;
    lc.configure({50, 10, 0.0f, 7});
    for (std::uint8_t i = 0; i < 20; ++i) lc.enqueue(1000, {i}, false);
    check(lc.popDue(1049).empty(), "nothing delivered before latency elapses");
    auto out = lc.popDue(1060);
    bool ordered = !out.empty();
    for (std::size_t i = 0; i < out.size(); ++i) ordered &= out[i].bytes[0] == i;
    check(out.size() == 20 && ordered, "all delivered by latency+jitter, in order");

    LinkConditioner lossy;
    lossy.configure({0, 0, 0.25f, 99});
    int unreliableKept = 0, reliableKept = 0;
    for (int i = 0; i < 4000; ++i) {
        unreliableKept += lossy.enqueue(0, {1}, false) ? 1 : 0;
        reliableKept += lossy.enqueue(0, {2}, true) ? 1 : 0;
    }
    const double lossObserved = 1.0 - unreliableKept / 4000.0;
    check(std::abs(lossObserved - 0.25) < 0.03, "unreliable loss rate ~25%");
    check(reliableKept == 4000, "reliable packets are never dropped");

    LinkConditioner a, b;
    a.configure({0, 0, 0.5f, 5});
    b.configure({0, 0, 0.5f, 5});
    bool same = true;
    for (int i = 0; i < 200; ++i) same &= a.enqueue(0, {0}, false) == b.enqueue(0, {0}, false);
    check(same, "same seed -> same drop pattern");
}

AvatarState at(std::uint64_t t, float x, std::uint32_t motion, float mtime, float rot = 0.0f) {
    AvatarState s;
    s.serverTimeMs = t;
    s.position = {x, 0.0f, 0.0f};
    s.motionId = motion;
    s.motionTime = mtime;
    s.rotationY = rot;
    return s;
}

void testInterpolator() {
    std::cout << "\n=== AvatarInterpolator ===\n";
    AvatarInterpolator in(64, 100);
    check(!in.sample(5).has_value(), "empty buffer -> no sample");
    in.push(at(1000, 0.0f, 2, 0.0f, 3.0f));
    in.push(at(1100, 100.0f, 2, 10.0f, -3.0f));
    check(!in.push(at(1050, 999.0f, 2, 0.0f)), "out-of-order snapshot rejected");

    auto mid = in.sample(1050);
    check(mid && std::abs(mid->position.x - 50.0f) < 1e-3f, "midpoint position lerps");
    check(mid && std::abs(mid->motionTime - 5.0f) < 1e-3f, "same-motion time lerps");
    // 3.0 -> -3.0 rad crosses +-pi: shortest arc goes through pi, not 0.
    check(mid && std::abs(std::abs(mid->rotationY) - 3.14159f) < 0.01f,
          "rotation takes the shortest arc");

    auto ahead = in.sample(1150);
    check(ahead && std::abs(ahead->position.x - 150.0f) < 1e-3f, "extrapolates 50 ms");
    auto capped = in.sample(1600);
    check(capped && std::abs(capped->position.x - 200.0f) < 1e-3f, "extrapolation capped at 100 ms");
    auto before = in.sample(10);
    check(before && before->position.x == 0.0f, "before oldest -> oldest");

    in.push(at(1200, 200.0f, 7, 0.0f));
    auto sw = in.sample(1150);
    check(sw && sw->motionId == 2 && std::abs(sw->motionTime - 10.0f) < 1e-3f,
          "motion switch holds the earlier motion until the boundary");
}

void testAvatarSync() {
    std::cout << "\n=== AvatarSync ===\n";
    AvatarSync sync(SlotType::Friend1, {100, 1000});
    const auto owners = sync.puppetOwners();
    check(owners[0] == 0 && owners[1] == 2, "friend1's puppets are slots 0 and 2, in order");

    auto snap = [](SlotType owner, std::uint64_t t, float x, std::uint16_t room,
                   std::uint8_t flags = 0) {
        AvatarState s;
        s.ownerSlot = owner;
        s.serverTimeMs = t;
        s.position = {x, 0.0f, 0.0f};
        s.worldId = 4;
        s.roomId = room;
        s.flags = flags;
        return s;
    };
    check(!sync.onRemote(snap(SlotType::Friend1, 1000, 0.0f, 26)), "own echo ignored");
    sync.onRemote(snap(SlotType::Player, 1000, 0.0f, 26));
    sync.onRemote(snap(SlotType::Player, 1100, 100.0f, 26));
    sync.onRemote(snap(SlotType::Friend2, 1000, 0.0f, 27));
    sync.onRemote(snap(SlotType::Friend2, 1100, 0.0f, 27));

    auto t = sync.sample(1150, 4, 26);
    check(t[0].active && std::abs(t[0].pose.position.x - 50.0f) < 1e-3f &&
              t[0].pose.ownerSlot == SlotType::Player,
          "puppet 0 shows the host at render time (server - 100 ms)");
    check(!t[1].active, "puppet 1 hidden: its owner is in another room");

    t = sync.sample(3000, 4, 26);
    check(!t[0].active, "puppet hidden once its stream is stale");

    sync.onRemote(snap(SlotType::Player, 3000, 0.0f, 26, AvatarInCutscene));
    sync.onRemote(snap(SlotType::Player, 3100, 0.0f, 26, AvatarInCutscene));
    t = sync.sample(3150, 4, 26);
    check(!t[0].active, "puppet hidden while its owner is in a cutscene");
}

void testAvatarBridge() {
    std::cout << "\n=== AvatarBridge (shared memory) ===\n";
    const DWORD fakePid = 0x7FFF0000u + (GetCurrentProcessId() & 0xFFFFu);
    AvatarBridge dll, runtime;
    check(dll.Open(fakePid) && runtime.Open(fakePid), "both sides open the same mapping");

    AvatarState out;
    check(!runtime.TryReadLocal(out), "no local avatar before the DLL publishes");
    AvatarState local;
    local.seq = 7;
    local.position = {1.0f, 2.0f, 3.0f};
    local.motionId = 2;
    dll.PublishLocal(local);
    check(runtime.TryReadLocal(out) && out.seq == 7 && out.position.z == 3.0f &&
              out.motionId == 2,
          "runtime reads the DLL's local avatar");
    check(!runtime.TryReadLocal(out), "unchanged slot reads as no new data");

    PuppetPose pose;
    pose.active = 1;
    pose.pose.ownerSlot = SlotType::Friend2;
    pose.pose.position = {9.0f, 0.0f, 0.0f};
    runtime.PublishPuppet(1, pose);
    PuppetPose got;
    check(!dll.TryReadPuppet(0, got), "puppet 0 untouched");
    check(dll.TryReadPuppet(1, got) && got.active == 1 &&
              got.pose.ownerSlot == SlotType::Friend2 && got.pose.position.x == 9.0f,
          "DLL reads puppet 1's pose");
}

// ---------------------------------------------------------------------------

constexpr std::uint16_t kPort = 17795;
constexpr float kRadius = 300.0f;    // units
constexpr float kOmega = 2.0f;       // rad/s -> ~600 units/s, a run
constexpr std::uint64_t kRenderDelayMs = 150;

Vec3 truth(int owner, std::uint64_t serverMs) {
    const float t = static_cast<float>(serverMs % 1000000) / 1000.0f;
    const float ang = kOmega * t + owner * 2.0f;
    return {owner * 1000.0f + kRadius * std::cos(ang), 0.0f, kRadius * std::sin(ang)};
}

void testEndToEnd() {
    std::cout << "\n=== End to end: 3 clients, 100 ms owner->viewer, 2% loss ===\n";
    SessionConfig cfg;
    cfg.port = kPort;
    cfg.maxPeers = 3;
    cfg.gameBuild = "avatar-build";
    cfg.contentHash = "avatar-content";
    cfg.modHash = "avatar-mod";
    cfg.sessionId = "avatar-test";
    SessionHost host(cfg, {});
    check(host.start(), "relay starts");

    struct Viewer {
        std::array<AvatarInterpolator, 3> interp;
        std::array<int, 3> received {};
        int ownEcho {0};
        int wrongRoom {0};
        double errSum {0.0};
        std::uint64_t errCount {0};
        float errMax {0.0f};
    };
    std::array<Viewer, 3> viewers;
    std::array<std::unique_ptr<NetworkClient>, 3> clients;
    const std::array<std::int64_t, 3> skew {0, 5000, -3000};

    for (int i = 0; i < 3; ++i) {
        ClientCallbacks cb;
        cb.onAvatarState = [&viewers, i](const AvatarState& s) {
            auto& v = viewers[i];
            const auto owner = static_cast<int>(s.ownerSlot);
            if (owner == i) { ++v.ownEcho; return; }
            if (owner < 0 || owner > 2) return;
            if (s.worldId != 4 || s.roomId != 0x1A) ++v.wrongRoom;
            ++v.received[owner];
            v.interp[owner].push(s);
        };
        clients[i] = std::make_unique<NetworkClient>(
            "127.0.0.1", kPort, cfg.gameBuild, cfg.modHash,
            "avatar_" + std::to_string(i), static_cast<SlotType>(i), std::move(cb),
            RuntimeMode::CampaignCoop, cfg.contentHash);
        const LinkConditions link {50, 10, 0.02f, static_cast<std::uint32_t>(11 + i)};
        clients[i]->setLinkConditions(link, LinkConditions {50, 10, 0.02f,
                                      static_cast<std::uint32_t>(21 + i)});
        clients[i]->setClockSkewMs(skew[i]);
        clients[i]->connect();
    }

    const auto pump = [&](int ms) {
        const auto end = steadyMs() + ms;
        while (steadyMs() < end) {
            host.tick(0);
            for (auto& c : clients) c->tick(0);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };

    // Connect, verify, and let clock sync settle.
    const auto deadline = steadyMs() + 5000;
    while (steadyMs() < deadline && host.verifiedPeerCount() < 3) pump(10);
    check(host.verifiedPeerCount() == 3, "3 clients verified");
    pump(1500);

    for (int i = 0; i < 3; ++i) {
        const auto err = static_cast<long long>(clients[i]->estimatedServerTimeMs()) -
                         static_cast<long long>(steadyMs());
        char buf[160];
        std::snprintf(buf, sizeof(buf),
                      "client %d clock estimate within 30 ms of host (skew %lld ms, err %lld ms, rtt %u ms)",
                      i, static_cast<long long>(skew[i]), err, clients[i]->roundTripMs());
        check(clients[i]->hasClockSync() && std::llabs(err) <= 30, buf);
    }

    // Stream for 6 s at ~60 Hz; measure after a 1 s warm-up.
    const auto start = steadyMs();
    std::uint64_t nextSend = start;
    while (steadyMs() < start + 6000) {
        const auto now = steadyMs();
        if (now >= nextSend) {
            for (int i = 0; i < 3; ++i) {
                AvatarState s;
                s.serverTimeMs = clients[i]->estimatedServerTimeMs();
                s.position = truth(i, s.serverTimeMs);
                s.worldId = 4;
                s.roomId = 0x1A;
                s.motionId = 2; // RUN
                // Lie about ownership: the relay must overwrite it.
                s.ownerSlot = SlotType::Player;
                clients[i]->sendAvatar(s);
            }
            nextSend += 16;
        }
        host.tick(0);
        for (auto& c : clients) c->tick(0);

        if (now > start + 1000) {
            for (int v = 0; v < 3; ++v) {
                const auto render = clients[v]->estimatedServerTimeMs() - kRenderDelayMs;
                for (int o = 0; o < 3; ++o) {
                    if (o == v) continue;
                    const auto s = viewers[v].interp[o].sample(render);
                    if (!s) continue;
                    const float e = dist(s->position, truth(o, render));
                    viewers[v].errSum += e;
                    viewers[v].errCount += 1;
                    if (e > viewers[v].errMax) viewers[v].errMax = e;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    for (int v = 0; v < 3; ++v) {
        const auto& vw = viewers[v];
        const double mean = vw.errCount ? vw.errSum / static_cast<double>(vw.errCount) : 1e9;
        char buf[200];
        std::snprintf(buf, sizeof(buf),
                      "viewer %d: received %d/%d/%d, mean err %.1f, max err %.1f units",
                      v, vw.received[0], vw.received[1], vw.received[2], mean, vw.errMax);
        check(vw.ownEcho == 0, "viewer " + std::to_string(v) + " never receives its own avatar");
        check(vw.wrongRoom == 0, "viewer " + std::to_string(v) + " sees the owners' room ids");
        bool gotOthers = true;
        for (int o = 0; o < 3; ++o) {
            if (o != v) gotOthers &= vw.received[o] > 250; // ~360 sent, 2-4% lost
        }
        check(gotOthers, std::string(buf));
        check(mean < 15.0 && vw.errMax < 50.0f,
              "viewer " + std::to_string(v) + " puppets within 50 units (~0.5 m) of truth");
    }
    check(host.relayedAvatarCount() > 900, "relay forwarded the streams");

    for (auto& c : clients) c->disconnect();
    host.stop();
}

} // namespace

int main() {
    if (enet_initialize() != 0) {
        std::cerr << "enet_initialize failed\n";
        return 2;
    }
    testCodec();
    testLinkConditioner();
    testInterpolator();
    testAvatarSync();
    testAvatarBridge();
    testEndToEnd();
    enet_deinitialize();

    std::cout << "\n=======================================\n"
              << (g_errors == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED: " + std::to_string(g_errors))
              << "\n";
    return g_errors == 0 ? 0 : 1;
}
