// VUH-2897: CombatAdmissionAck (protocol 39) and the RoomTransition admission challenge:
// exact framing, and the relay's admission over loopback ENet (client -> relay -> host only).
#include "kh2coop/Codec.hpp"
#include "kh2coop/NetworkClient.hpp"
#include "kh2coop/SessionHost.hpp"
#include <enet/enet.h>
#include <array>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
using namespace kh2coop;
namespace {
int errors = 0;
void check(bool ok, const char* text) { std::cout << (ok ? "PASS " : "FAIL ") << text << '\n'; if (!ok) ++errors; }
template <class F> bool throws(F f) { try { f(); } catch (const std::exception&) { return true; } return false; }

CombatAdmissionAck sample() {
    CombatAdmissionAck m;
    m.location = {20, 2, 0x14, 0, 137, 137, 137};
    m.challenge = 0xA5A5000011112222ull; m.seq = 1; m.requesterConnectionId = 77;
    return m;
}

void codecChecks() {
    const auto m = sample();
    const auto bytes = encode(m);
    const std::uint8_t* p = nullptr; std::size_t n = 0;
    check(decodePacketHeader(bytes.data(), bytes.size(), p, n) == PacketType::CombatAdmissionAck &&
          n == COMBAT_ADMISSION_ACK_BYTES && bytes.size() == n + 3, "ack frames as type 57 with a 41-byte payload");
    ByteReader r(p, n); CombatAdmissionAck d; read(r, d);
    check(r.atEnd() && d.location.epoch == 20 && d.location.battleProgram == 137 && d.location.eventProgram == 137 &&
          d.challenge == m.challenge && d.seq == 1 && d.requesterConnectionId == 77 && d.ownerSlot == 0xFF,
          "ack round-trips the full destination, challenge, sequence and requester");
    check(validateScopedWorldPacket(bytes) == PacketType::CombatAdmissionAck, "ack is a scoped world packet");
    auto truncated = bytes; truncated.pop_back();
    check(throws([&] { decodePacketHeader(truncated.data(), truncated.size(), p, n); }), "truncated ack refused");
    auto suffixed = bytes; suffixed.push_back(0);
    check(throws([&] { decodePacketHeader(suffixed.data(), suffixed.size(), p, n); }), "suffixed frame refused");
    auto longer = bytes; longer.push_back(0); longer[1] = static_cast<std::uint8_t>(n + 1);
    check(throws([&] { decodePacketHeader(longer.data(), longer.size(), p, n); }), "a longer declared payload refused");
    for (int field = 0; field < 4; ++field) {
        auto bad = m;
        if (field == 0) bad.location.epoch = 0; else if (field == 1) bad.challenge = 0;
        else if (field == 2) bad.seq = 0; else bad.requesterConnectionId = 0;
        check(throws([&] { (void)encode(bad); }), "zero epoch/challenge/sequence/requester never encodes");
    }
    auto badOwner = m; badOwner.ownerSlot = 3;
    check(throws([&] { (void)encode(badOwner); }), "an out-of-range owner slot never encodes");

    RoomTransition room {20, 2, 0x14, 0, 137, 137, 137};
    room.admissionChallenge = 0x1234567890ABCDEFull;
    const auto packet = encode(room);
    check(decodePacketHeader(packet.data(), packet.size(), p, n) == PacketType::RoomTransition, "room frames");
    ByteReader rr(p, n); RoomTransition back; readRoomTransitionPacket(rr, back);
    check(rr.atEnd() && back.admissionChallenge == room.admissionChallenge && back.epoch == 20,
          "the RoomTransition packet carries the admission challenge");
    ByteReader old(p, n - 8); RoomTransition stale;
    check(throws([&] { readRoomTransitionPacket(old, stale); }), "a v38 room packet without the trailer is refused");
    check(PROTOCOL_VERSION == 39, "protocol version is 39");
}

void relayChecks() {
    SessionConfig config; config.bindAddress = "127.0.0.1"; config.port = 29897;
    config.gameBuild = "combat-admission-test"; config.modHash = "none";
    std::string lastReason;
    SessionCallbacks relayCallbacks; relayCallbacks.onLog = [&](const std::string& line) {
        const std::string key = "CombatAdmissionAck refused peer="; const auto at = line.find(key);
        if (at == std::string::npos) return;
        const auto reason = line.find(" reason=", at);
        lastReason = reason == std::string::npos ? "" : line.substr(reason + 8);
    };
    SessionHost server(config, relayCallbacks);
    if (!server.start()) { check(false, "relay bind"); return; }
    std::array<std::vector<CombatAdmissionAck>, 3> received;
    std::array<std::vector<std::uint64_t>, 3> sources;
    std::array<std::unique_ptr<NetworkClient>, 3> clients;
    auto make = [&](unsigned i) {
        ClientCallbacks cb;
        cb.onWorldEnvelope = [&, i](const WorldEnvelope& e) {
            if (e.packet.empty() || e.packet.front() != static_cast<std::uint8_t>(PacketType::CombatAdmissionAck)) return;
            const std::uint8_t* p; std::size_t n; decodePacketHeader(e.packet.data(), e.packet.size(), p, n);
            ByteReader r(p, n); CombatAdmissionAck m; read(r, m);
            received[i].push_back(m); sources[i].push_back(e.scope.sourceConnectionId);
        };
        clients[i] = std::make_unique<NetworkClient>("127.0.0.1", config.port, "combat-admission-test", "none",
                                                     "ca" + std::to_string(i), static_cast<SlotType>(i), cb);
        clients[i]->connect();
    };
    auto pump = [&] { for (unsigned k = 0; k < 30; ++k) { server.tick(); for (auto& c : clients) if (c) c->tick();
                                                          std::this_thread::sleep_for(std::chrono::milliseconds(1)); } };
    make(0); make(1); pump(); pump();
    check(server.verifiedPeerCount() == 2, "host and friend admitted");
    if (server.verifiedPeerCount() != 2) { for (auto& c : clients) if (c) c->disconnect(); server.stop(); return; }
    const auto friendId = server.peerBySlot(SlotType::Friend1)->connectionId;
    const auto hostId = server.peerBySlot(SlotType::Player)->connectionId;
    RoomTransition room {20, 2, 0x14, 0, 137, 137, 137};
    room.admissionChallenge = 0xA5A5000011112222ull;
    clients[0]->sendRoomTransition(room); pump();

    auto ack = sample(); ack.location = room; ack.requesterConnectionId = friendId; ack.challenge = room.admissionChallenge;
    std::uint64_t hostSource = 1000; // a host envelope carries its source serial
    auto send = [&](unsigned slot, const CombatAdmissionAck& m) {
        return clients[slot]->sendNativeWorld(encode(m), ProducerWorldContext{1, clients[slot]->deliverySerial(),
                                                                              slot ? 0 : ++hostSource}, true);
    };
    auto total = [&] { return received[0].size() + received[1].size() + received[2].size(); };
    auto refused = [&](unsigned slot, const CombatAdmissionAck& m, const char* reason, const char* label) {
        const auto before = total(); const auto rejected = server.rejectedWorldMessages(); lastReason.clear();
        send(slot, m); pump();
        const bool ok = total() == before && server.rejectedWorldMessages() == rejected + 1 && lastReason == reason;
        check(ok, label);
        if (!ok) std::cout << "  relay reason=" << lastReason << " expected=" << reason << '\n';
    };

    check(send(1, ack), "friend submits its ack through its native world envelope"); pump();
    check(received[0].size() == 1 && received[1].empty() && received[0][0].ownerSlot == 1 &&
          sources[0][0] == friendId && received[0][0].challenge == room.admissionChallenge,
          "relay stamps the friend's slot and forwards to the verified host alone");
    refused(1, ack, "sequence-floor", "duplicate sequence refused");
    auto bad = ack; bad.seq = 10; bad.requesterConnectionId = friendId + 1;
    refused(1, bad, "requester-peer", "forged requester refused");
    bad = ack; bad.seq = 11; bad.location.epoch = 19;
    refused(1, bad, "room", "wrong epoch refused");
    bad = ack; bad.seq = 12; bad.location.battleProgram = 138;
    refused(1, bad, "room", "wrong destination refused");
    bad = ack; bad.seq = 13; bad.challenge ^= 1;
    refused(1, bad, "challenge", "wrong challenge refused");
    bad = ack; bad.seq = 14; bad.ownerSlot = 2;
    refused(1, bad, "wire-owner-not-ff", "a pre-stamped owner refused");
    bad = ack; bad.seq = 15; bad.requesterConnectionId = hostId;
    refused(0, bad, "host-author", "the host's own ack refused");

    // A new announcement (same room, next epoch, new challenge) retires the old ack.
    RoomTransition next = room; next.epoch = 21; next.admissionChallenge = 0x0F0F0F0F12345678ull;
    clients[0]->sendRoomTransition(next); pump();
    bad = ack; bad.seq = 16;
    refused(1, bad, "room", "an ack for the previous incarnation is refused after a new announcement");
    bad = ack; bad.seq = 17; bad.location = next; bad.challenge = room.admissionChallenge;
    refused(1, bad, "challenge", "the previous challenge cannot satisfy the new announcement");
    RoomTransition plain = next; plain.epoch = 22; plain.admissionChallenge = 0;
    clients[0]->sendRoomTransition(plain); pump();
    bad = ack; bad.seq = 18; bad.location = plain; bad.challenge = 1;
    refused(1, bad, "challenge", "no ack is admitted for an announcement without a barrier");
    RoomTransition fresh = plain; fresh.epoch = 23; fresh.admissionChallenge = 0x77;
    clients[0]->sendRoomTransition(fresh); pump();
    auto good = ack; good.seq = 19; good.location = fresh; good.challenge = fresh.admissionChallenge;
    check(send(1, good), "fresh ack submitted"); pump();
    check(received[0].size() == 2 && received[0].back().location.epoch == 23 && received[0].back().ownerSlot == 1,
          "a valid ack after refusals still forwards once");
    make(2); pump(); pump();
    if (server.verifiedPeerCount() == 3) {
        auto late = good; late.seq = 1; late.requesterConnectionId = server.peerBySlot(SlotType::Friend2)->connectionId;
        check(send(2, late), "late joiner submits"); pump();
        check(received[0].size() == 3 && received[0].back().ownerSlot == 2 && received[2].empty(),
              "relay forwards a late joiner stamped as its own slot (the host lease decides it is not required)");
    } else check(false, "third seat admitted");
    for (auto& c : clients) if (c) c->disconnect();
    pump();
    server.stop();
}
} // namespace

int main() {
    if (enet_initialize() != 0) return 2;
    codecChecks();
    relayChecks();
    enet_deinitialize();
    std::cout << (errors ? "FAILED " : "OK ") << errors << '\n';
    return errors ? 1 : 0;
}
