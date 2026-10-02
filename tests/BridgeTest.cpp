// BridgeTest — the DLL <-> runtime plumbing for world events, no KH2.
//
// Unit: PacketRing (order, wraparound, full ring refuses instead of
// overwriting, a producer thread against a consumer thread).
// End to end: a fake host "DLL" pushes an encoded RoomTransition into its
// WorldBridge; the host runtime pump forwards it through the relay; the
// client runtime hands it to the client's WorldBridge, where a fake client
// "DLL" decodes it. A client DLL's HitClaim travels back to the host DLL with
// the attacker slot stamped. Non-world packets from a DLL are rejected.
// Exit code 0 = all checks passed.

#include "kh2coop/Codec.hpp"
#include "kh2coop/NetworkClient.hpp"
#include "kh2coop/PacketRing.hpp"
#include "kh2coop/SessionHost.hpp"
#include "kh2coop/WorldBridge.hpp"
#include "kh2coop/WorldPump.hpp"

#include <enet/enet.h>

#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
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

std::vector<std::uint8_t> bytesOf(std::uint32_t seed, std::size_t n) {
    std::vector<std::uint8_t> v(n);
    for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<std::uint8_t>(seed * 31 + i);
    return v;
}

void testPacketRing() {
    std::cout << "\n=== PacketRing ===\n";
    constexpr std::uint32_t cap = 256;
    std::vector<std::uint8_t> mem(PacketRing::bytesFor(cap));
    PacketRing::format(mem.data(), cap);
    PacketRing ring(mem.data());

    std::vector<std::uint8_t> out;
    check(!ring.pop(out), "empty ring pops nothing");
    check(ring.push(bytesOf(1, 5)) && ring.push(bytesOf(2, 9)), "push two records");
    check(ring.pop(out) && out == bytesOf(1, 5) && ring.pop(out) && out == bytesOf(2, 9) &&
              !ring.pop(out),
          "records come out in order with exact lengths");

    // Cycle odd sizes many times to force every wrap position.
    bool intact = true;
    for (std::uint32_t i = 0; i < 2000; ++i) {
        const auto rec = bytesOf(i, 1 + (i * 7) % 60);
        intact &= ring.push(rec) && ring.pop(out) && out == rec;
    }
    check(intact, "2000 push/pop cycles across wraparound stay intact");

    int accepted = 0;
    while (ring.push(bytesOf(3, 40))) ++accepted;
    check(accepted > 0 && ring.dropped() > 0, "a full ring refuses pushes (counted)");
    int drained = 0;
    while (ring.pop(out)) {
        if (out != bytesOf(3, 40)) intact = false;
        ++drained;
    }
    check(drained == accepted && intact, "nothing was overwritten when full");
    check(!ring.push(bytesOf(4, 200)), "a record larger than half the ring is refused");

    // Producer and consumer on different threads.
    std::vector<std::uint8_t> mem2(PacketRing::bytesFor(4096));
    PacketRing::format(mem2.data(), 4096);
    PacketRing a(mem2.data()), b(mem2.data());
    constexpr int kCount = 20000;
    std::thread producer([&] {
        for (int i = 0; i < kCount;) {
            if (a.push(bytesOf(static_cast<std::uint32_t>(i), 1 + i % 100))) ++i;
        }
    });
    int got = 0;
    bool ordered = true;
    const auto deadline = nowMs() + 10000;
    while (got < kCount && nowMs() < deadline) {
        if (b.pop(out)) {
            ordered &= out == bytesOf(static_cast<std::uint32_t>(got), 1 + got % 100);
            ++got;
        }
    }
    producer.join();
    check(got == kCount && ordered, "20000 records across threads, in order, intact");
}

} // namespace

int main() {
    testPacketRing();
    if (enet_initialize() != 0) return 2;

    std::cout << "\n=== WorldBridge end to end ===\n";
    SessionConfig cfg;
    cfg.port = 17797;
    cfg.maxPeers = 3;
    cfg.gameBuild = "bridge-build";
    cfg.contentHash = "bridge-content";
    cfg.modHash = "bridge-mod";
    cfg.sessionId = "bridge-test";
    SessionHost relay(cfg, {});
    check(relay.start(), "relay starts");

    const DWORD hostPid = 0x7FF10000u + (GetCurrentProcessId() & 0xFFFFu);
    const DWORD clientPid = hostPid + 1;
    // Each "runtime" and its "DLL" open the same named mapping.
    WorldBridge hostRuntimeSide, hostDll, clientRuntimeSide, clientDll;
    check(hostRuntimeSide.Open(hostPid) && hostDll.Open(hostPid) &&
              clientRuntimeSide.Open(clientPid) && clientDll.Open(clientPid),
          "bridges open for both machines");

    WorldPumpStats hostStats, clientStats;
    ClientCallbacks hostCb, clientCb;
    hostCb.onWorldPacket = [&](const std::vector<std::uint8_t>& p) {
        forwardToDll(hostRuntimeSide, p, hostStats);
    };
    clientCb.onWorldPacket = [&](const std::vector<std::uint8_t>& p) {
        forwardToDll(clientRuntimeSide, p, clientStats);
    };
    NetworkClient host("127.0.0.1", cfg.port, cfg.gameBuild, cfg.modHash, "host",
                       SlotType::Player, std::move(hostCb), RuntimeMode::CampaignCoop,
                       cfg.contentHash);
    NetworkClient client("127.0.0.1", cfg.port, cfg.gameBuild, cfg.modHash, "client",
                         SlotType::Friend2, std::move(clientCb), RuntimeMode::CampaignCoop,
                         cfg.contentHash);
    host.connect();
    client.connect();

    auto pump = [&](int ms) {
        const auto end = nowMs() + ms;
        while (nowMs() < end) {
            relay.tick(0);
            host.tick(0);
            client.tick(0);
            pumpDllToNet(hostRuntimeSide, host, hostStats);
            pumpDllToNet(clientRuntimeSide, client, clientStats);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };
    // Evaluates cond once per poll (cond may consume, e.g. pop a ring).
    auto waitFor = [&](const std::function<bool()>& cond) {
        const auto end = nowMs() + 3000;
        while (nowMs() < end) {
            if (cond()) return true;
            pump(5);
        }
        return false;
    };
    check(waitFor([&] { return relay.verifiedPeerCount() == 2; }), "host and client verified");

    // Host DLL detects a transition and emits it.
    check(hostDll.SendToRuntime(encode(RoomTransition {7, 4, 0x1A, 3, 1, 2, 0})),
          "host DLL queues a RoomTransition");
    std::vector<std::uint8_t> pkt;
    RoomTransition got;
    const bool arrived = waitFor([&] {
        if (!clientDll.ReceiveFromRuntime(pkt)) return false;
        const std::uint8_t* payload = nullptr;
        std::size_t size = 0;
        if (decodePacketHeader(pkt.data(), pkt.size(), payload, size) !=
            PacketType::RoomTransition) {
            return false;
        }
        ByteReader r(payload, size);
        read(r, got);
        return true;
    });
    check(arrived && got.epoch == 7 && got.roomId == 0x1A && got.door == 3,
          "client DLL decodes the host's transition from its bridge");

    // Client DLL claims a hit; host DLL receives it with the real slot.
    HitClaim claim;
    claim.epoch = 7;
    claim.netId = 5;
    claim.damage = 33;
    claim.attackerSlot = SlotType::Player; // forged
    clientDll.SendToRuntime(encode(claim));
    HitClaim hostGot;
    const bool claimed = waitFor([&] {
        if (!hostDll.ReceiveFromRuntime(pkt)) return false;
        const std::uint8_t* payload = nullptr;
        std::size_t size = 0;
        if (decodePacketHeader(pkt.data(), pkt.size(), payload, size) != PacketType::HitClaim) {
            return false;
        }
        ByteReader r(payload, size);
        read(r, hostGot);
        return true;
    });
    check(claimed && hostGot.netId == 5 && hostGot.damage == 33 &&
              hostGot.attackerSlot == SlotType::Friend2,
          "host DLL receives the client's hit claim with the real attacker slot");

    // A DLL can't push arbitrary traffic (e.g. a forged session state) out.
    hostDll.SendToRuntime(encodePacket(PacketType::SessionState, {1, 2, 3}));
    hostDll.SendToRuntime(std::vector<std::uint8_t> {0xFF});
    pump(100);
    check(hostStats.rejected == 2, "non-world and malformed DLL packets are rejected");
    check(hostStats.toNet >= 1 && clientStats.toNet >= 1 && clientStats.toDll >= 1 &&
              hostStats.toDll >= 1,
          "pump counters account for both directions");

    host.disconnect();
    client.disconnect();
    relay.stop();
    enet_deinitialize();

    std::cout << "\n=======================================\n"
              << (g_errors == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED: " + std::to_string(g_errors))
              << "\n";
    return g_errors == 0 ? 0 : 1;
}
