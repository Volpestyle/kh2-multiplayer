// VUH-1786 PartyIntent: codec and policy, then the relay over real loopback ENet:
// host-only admission, version floor, per-target cache, echo to the host, survival across
// room changes, no relay replay to a joiner (host republishes), and retirement on host departure.
#include "kh2coop/NetworkClient.hpp"
#include "kh2coop/SessionHost.hpp"
#include <enet/enet.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
using namespace kh2coop;
namespace {
int failures = 0;
void check(bool b, const char* s) { std::cout << (b ? "PASS " : "FAIL ") << s << '\n'; if (!b) ++failures; }

PartyIntent makeIntent(std::uint64_t version, const std::array<std::uint64_t, 3>& ids, PartyIntentTarget target) {
    RoomTransition room{1, target.worldId, target.roomId, 0, 0, 0, target.eventProgram};
    const auto layout = defaultPartyLayout(room, version, PartyApplyReason::HostChoice, PartyRule::Default, 0, ids);
    PartyIntent m{};
    m.version = version; m.connections = ids; m.target = target; m.rule = PartyRule::Default; m.seats = layout->seats;
    for (unsigned i = 1; i < 3; ++i) m.kits[i] = m.seats[i].kind == PartyMemberKind::RemotePlayer ? PARTY_INTENT_KIT_SORA : 0;
    return m;
}
template<class T> bool roundtrip(const T& m, T& out, std::size_t& payload) {
    const auto bytes = encode(m); const std::uint8_t* p = nullptr; std::size_t n = 0;
    decodePacketHeader(bytes.data(), bytes.size(), p, n); ByteReader r(p, n); read(r, out); payload = n;
    return r.atEnd() && encode(out) == bytes;
}
bool sameIntent(const PartyIntent& a, const PartyIntent& b) {
    return a.version == b.version && a.connections == b.connections && a.target == b.target && a.rule == b.rule &&
        a.seats == b.seats && a.kits == b.kits;
}

void rules() {
    const std::array<std::uint64_t, 3> ids{11, 12, 13};
    const auto goa = makeIntent(1, ids, {4, 0x1A, 0});
    check(validPartyIntent(goa, ids), "three-player default intent is valid");
    check(static_cast<unsigned>(PacketType::PartyIntent) == 47 && PROTOCOL_VERSION == 16, "PartyIntent is type 47 at protocol 16");
    PartyIntent back{}; std::size_t n = 0;
    check(roundtrip(goa, back, n) && n == PARTY_INTENT_PAYLOAD && n == 63 && sameIntent(back, goa), "exact 63-byte codec roundtrip");
    auto two = makeIntent(2, {11, 12, 0}, {4, 0x0A, 0});
    check(validPartyIntent(two, {11, 12, 0}) && two.kits[1] == 0x54 && two.kits[2] == 0, "two players: remote seat kit Sora, Goofy seat kit 0");
    auto kit = goa; kit.kits[1] = 0x5A; check(validPartyIntent(kit, ids), "party kits: a Roxas remote seat is valid");
    kit = goa; kit.kits = {0x5A, 0x54, 0x5A}; check(validPartyIntent(kit, ids), "party kits: explicit seat-0 kit (Roxas host) is valid");
    kit = goa; kit.kits[0] = 0x54; check(validPartyIntent(kit, ids), "party kits: explicit seat-0 Sora is valid; legacy 0 stays valid");
    auto bad = goa; bad.kits[1] = 0x323; check(!validPartyIntent(bad, ids), "unqualified kit (dual-wield Roxas) refused");
    bad = goa; bad.kits[0] = 0x323; check(!validPartyIntent(bad, ids), "unqualified seat-0 kit (dual-wield Roxas) refused");
    bad = goa; bad.kits[2] = 0; check(!validPartyIntent(bad, ids), "a remote-player seat needs a kit");
    bad = two; bad.kits[2] = 0x54; check(!validPartyIntent(bad, {11, 12, 0}), "AI seat kit must be 0");
    bad = goa; bad.connections[2] = 99; check(!validPartyIntent(bad, ids), "intent pins the whole roster");
    bad = goa; bad.seats[2] = bad.seats[1]; check(!validPartyIntent(bad, ids), "one player cannot hold two seats");
    bad = goa; bad.version = 0; check(!validPartyIntent(bad, ids), "zero version refused");
    bad = goa; bad.target.roomId = 0xFFFF; check(!validPartyIntent(bad, ids), "unset target refused");
    bad = goa; bad.rule = static_cast<PartyRule>(9); check(!validPartyIntent(bad, ids), "unknown rule refused");
    bool threw = false; try { (void)encode(bad); } catch (const std::exception&) { threw = true; }
    check(threw, "encoder refuses an invalid intent");
    auto bytes = encode(goa); bytes.push_back(0); threw = false;
    try { const std::uint8_t* p; std::size_t len; decodePacketHeader(bytes.data(), bytes.size(), p, len); } catch (const std::exception&) { threw = true; }
    check(threw, "trailing intent frame refused");
    check(isWorldPacket(PacketType::PartyIntent) && isScopedWorldPacket(PacketType::PartyIntent) && !isMaterialWorldPacket(PacketType::PartyIntent),
          "intent is a scoped, non-material world packet");
}

void network() {
    SessionConfig config; config.bindAddress = "127.0.0.1"; config.port = 29877; config.gameBuild = "intent-test"; config.modHash = "none";
    SessionHost server(config); if (!server.start()) { check(false, "ENet host starts"); return; }
    std::array<std::unique_ptr<NetworkClient>, 3> clients; std::array<unsigned, 3> callbacks{};
    auto make = [&](unsigned i) {
        ClientCallbacks cb; cb.onPartyIntent = [&callbacks, i](const PartyIntent&) { ++callbacks[i]; };
        clients[i] = std::make_unique<NetworkClient>("127.0.0.1", config.port, "intent-test", "none", "intent" + std::to_string(i), static_cast<SlotType>(i), cb);
        clients[i]->connect();
    };
    for (unsigned i = 0; i < 3; ++i) make(i);
    auto pump = [&] { for (unsigned n = 0; n < 30; ++n) { server.tick(); for (auto& c : clients) if (c) c->tick(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); } };
    pump(); pump();
    if (server.verifiedPeerCount() != 3) { check(false, "three peers admitted"); return; }
    RoomTransition room{30, 4, 0x1A, 0, 0, 0, 0};
    clients[0]->sendRoomTransition(room); pump();
    std::array<std::uint64_t, 3> ids{};
    for (unsigned i = 0; i < 3; ++i) ids[i] = server.peerBySlot(static_cast<SlotType>(i))->connectionId;
    const auto goa = makeIntent(1, ids, {4, 0x1A, 0}), borough = makeIntent(2, ids, {4, 0x0A, 0});
    check(!clients[1]->sendPartyIntent(goa), "non-host convenience API refuses intent authorship");
    check(clients[0]->sendPartyIntent(goa) && clients[0]->sendPartyIntent(borough), "host publishes intents for two target rooms");
    pump();
    check(server.partyIntents().size() == 2, "relay caches one intent per target");
    check(std::all_of(clients.begin(), clients.end(), [](auto& c) { return c->partyIntents().size() == 2; }) && callbacks == std::array<unsigned, 3>{2, 2, 2},
          "both intents reach every peer, the host included (echo), once each");
    auto forged = goa; forged.version = 99;
    clients[1]->sendNativeWorld(encode(forged), {1, clients[1]->deliverySerial(), 0}, true); pump();
    check(server.partyIntents()[0].version == 1 && callbacks[2] == 2, "relay refuses a non-host intent through the raw world producer");
    clients[0]->sendPartyIntent(goa); pump();
    check(callbacks == std::array<unsigned, 3>{2, 2, 2}, "duplicate version refused (no second application)");
    clients[0]->sendNativeWorld(encode(makeIntent(3, {ids[0], ids[1], ids[2] + 1000}, {4, 0x1A, 0})), {1, clients[0]->deliverySerial(), 1}, true); pump();
    check(server.partyIntents()[0].version == 1, "intent for another roster refused");
    room.epoch = 31; room.roomId = 0x0A; clients[0]->sendRoomTransition(room); pump();
    check(server.partyIntents().size() == 2 && clients[2]->partyIntents().size() == 2 && !server.partyLayout(),
          "a room change keeps the intents (room-independent) while it retires the room-pinned layout");
    const auto newer = makeIntent(4, ids, {4, 0x1A, 0});
    clients[0]->sendPartyIntent(newer); pump();
    const auto& cached = server.partyIntents();
    check(cached.size() == 2 && std::any_of(cached.begin(), cached.end(), [](const PartyIntent& x) { return x.version == 4 && x.target.roomId == 0x1A; }),
          "a newer intent replaces its own target only");
    // A rejoin changes the roster (new connection id): cached intents pinned to the old roster are
    // never replayed (the relay has no replay path), and the host's republication for the new roster reaches it.
    clients[2]->disconnect(); pump(); clients[2].reset(); callbacks[2] = 0; make(2); pump(); pump();
    check(clients[2]->ready() && clients[2]->partyIntents().empty() && callbacks[2] == 0, "a rejoined peer receives no cached intent");
    for (unsigned i = 0; i < 3; ++i) ids[i] = server.peerBySlot(static_cast<SlotType>(i))->connectionId;
    clients[0]->sendPartyIntent(makeIntent(5, ids, {4, 0x0A, 0})); pump();
    check(clients[2]->partyIntents().size() == 1 && clients[1]->partyIntents().size() == 1 && callbacks[2] == 1,
          "the host's intent for the new roster reaches the rejoined peer");
    clients[0]->disconnect(); pump();
    check(server.partyIntents().empty(), "host departure retires every cached intent");
    for (auto& c : clients) if (c) c->disconnect();
    server.stop();
}
} // namespace
int main() { if (enet_initialize()) return 2; rules(); network(); enet_deinitialize(); std::cout << "failures=" << failures << '\n'; return failures ? 1 : 0; }
