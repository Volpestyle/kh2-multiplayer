#include "kh2coop/Codec.hpp"
#include "WorldWireFixture.hpp"
#include "kh2coop/SessionHost.hpp"
#include <enet/enet.h>
#include <chrono>
#include <array>
#include <stdexcept>
#include <iostream>
#include <limits>
#include <memory>
#include <thread>
using namespace kh2coop;
namespace {
int errors = 0;
void check(bool ok, const char* label) { std::cout << (ok ? "PASS: " : "FAIL: ") << label << '\n'; if (!ok) ++errors; }
template<class F> bool throws(F f) { try { f(); return false; } catch (const std::exception&) { return true; } }
FieldObjects sample() {
    FieldObjects m; m.location = {7, 4, 0x15, 2, 3, 26, 5}; m.sequence = 1; m.hostFrame = 234;
    FieldObjectEntry e; e.netId = 0x8001; e.objectId = 640; e.objectType = 255;
    e.anchor = {-5, 6, 7}; e.position = {8, 9, 10}; e.rotationY = .5f; e.motionId = 3; e.motionTime = 4;
    m.entries.push_back(e); return m;
}
FieldObjects decode(const std::vector<std::uint8_t>& bytes) {
    const std::uint8_t* p; std::size_t n;
    if (decodePacketHeader(bytes.data(), bytes.size(), p, n) != PacketType::FieldObjects) throw std::runtime_error("wrong type");
    ByteReader r(p, n); FieldObjects out; read(r, out); return out;
}
// Deliberately bypass writer checks to exercise hostile payload decoding too.
std::vector<std::uint8_t> unchecked(const FieldObjects& m) {
    ByteWriter w; write(w, m.location); w.writeU64(m.sequence); w.writeU32(m.hostFrame);
    w.writeU16(static_cast<std::uint16_t>(m.entries.size()));
    for (const auto& e : m.entries) {
        w.writeU16(e.netId); w.writeU32(e.objectId); w.writeU8(e.objectType);
        write(w, e.anchor); write(w, e.position); w.writeF32(e.rotationY); w.writeU32(e.motionId); w.writeF32(e.motionTime); w.writeU8(e.present ? 1 : 0);
    }
    return encodePacket(PacketType::FieldObjects, w.data());
}
void codec() {
    auto m = sample(); auto bytes = encode(m);
    check(PROTOCOL_VERSION == 25 && bytes[0] == 51 && bytes.size() == 3 + FIELD_OBJECT_HEADER_BYTES + FIELD_OBJECT_ENTRY_BYTES,
          "protocol25 packet51 has exact full-location header and entry size");
    check(encode(decode(bytes)) == bytes, "all fields round trip including anchor and unrestricted native type");
    check(isWorldPacket(PacketType::FieldObjects) && isScopedWorldPacket(PacketType::FieldObjects) &&
          !isMaterialWorldPacket(PacketType::FieldObjects) && !isEphemeralWorldPacket(PacketType::FieldObjects) &&
          validateScopedWorldPacket(bytes) == PacketType::FieldObjects, "periodic fields use scoped delivery without material/resync state");
    check(decode(bytes).entries[0].present, "ordinary field defaults to present");
    m.entries[0].present = false; const auto tombstone = decode(encode(m));
    check(!tombstone.entries[0].present && tombstone.entries[0].netId == m.entries[0].netId &&
          tombstone.entries[0].anchor.x == m.entries[0].anchor.x && encode(tombstone) == encode(m),
          "retired field tombstone retains occurrence and anchor for late binding");
    auto badPresence = bytes; badPresence.back() = 2;
    check(throws([&] { decode(badPresence); }), "presence flag accepts only strict zero or one");
    m.entries.clear(); check(decode(encode(m)).entries.empty(), "empty complete room roster is valid");
    m = sample(); m.entries[0].motionId = FIELD_OBJECT_NO_MOTION; m.entries[0].motionTime = 0;
    check(encode(decode(encode(m))) == encode(m), "no-motion sentinel with time zero is valid");
    m = sample();
    for (unsigned i = 1; i < FIELD_OBJECT_MAX_ENTRIES; ++i) { auto e = m.entries[0]; e.netId = static_cast<std::uint16_t>(0x8001u + i); m.entries.push_back(e); }
    check(decode(encode(m)).entries.size() == 64, "complete roster of 64 entries is accepted");
    auto extra = m.entries[0]; extra.netId = 0xFFFF; m.entries.push_back(extra);
    check(throws([&] { encode(m); }) && throws([&] { decode(unchecked(m)); }), "65 entries refused in full by writer and reader");
    const auto bad = [&](const auto& mutate, const char* label) {
        auto invalid = sample(); mutate(invalid);
        check(throws([&] { encode(invalid); }) && throws([&] { decode(unchecked(invalid)); }), label);
    };
    bad([](auto& x) { x.location.epoch = 0; }, "unset location refused");
    bad([](auto& x) { x.sequence = 0; }, "zero sequence refused");
    bad([](auto& x) { x.entries[0].netId = 1; }, "enemy namespace refused for field roster");
    bad([](auto& x) { x.entries[0].netId = 0x8000; }, "reserved field id refused");
    bad([](auto& x) { x.entries[0].objectId = 0; }, "zero object id refused");
    bad([](auto& x) { x.entries.push_back(x.entries[0]); }, "duplicate occurrence ids refused");
    bad([](auto& x) { x.entries[0].anchor.x = std::numeric_limits<float>::quiet_NaN(); }, "nonfinite anchor refused");
    bad([](auto& x) { x.entries[0].position.z = std::numeric_limits<float>::infinity(); }, "nonfinite position refused");
    bad([](auto& x) { x.entries[0].rotationY = std::numeric_limits<float>::infinity(); }, "nonfinite rotation refused");
    bad([](auto& x) { x.entries[0].motionTime = std::numeric_limits<float>::quiet_NaN(); }, "nonfinite motion time refused");
    bad([](auto& x) { x.entries[0].motionId = FIELD_OBJECT_NO_MOTION; }, "no-motion sentinel with nonzero time refused");
    bad([](auto& x) { x.entries[0].motionId = ENEMY_MOTION_MAX_MOTION_ID; }, "out of range motion refused");
    bad([](auto& x) { x.entries[0].motionTime = -1; }, "negative time refused");
    bad([](auto& x) { x.entries[0].anchor.y = ENEMY_MOTION_MAX_COORD + 1; }, "out of range anchor refused");
    bad([](auto& x) { x.entries[0].position.x = ENEMY_MOTION_MAX_COORD + 1; }, "out of range position refused");
    bad([](auto& x) { x.entries[0].rotationY = ENEMY_MOTION_MAX_ROTATION + 1; }, "out of range rotation refused");
    for (int delta : {-1, 1}) {
        auto invalid = bytes; if (delta < 0) invalid.pop_back(); else invalid.push_back(0);
        check(throws([&] { decode(invalid); }), "truncated or trailing frame refused");
    }
    auto invalid = bytes; invalid[3 + 28] = 2; // count says two, body has one
    check(throws([&] { decode(invalid); }), "count/body mismatch refused");
}
void relay() {
    SessionConfig config; config.bindAddress = "127.0.0.1"; config.port = 29883; config.gameBuild = "field-test"; config.modHash = "none";
    SessionHost relay(config); if (!relay.start()) { check(false, "field relay bind"); return; }
    std::array<std::unique_ptr<NetworkClient>, 3> clients;
    std::array<unsigned, 3> fields{}, scripts{}, retired{};
    auto make = [&](unsigned i) {
        ClientCallbacks cb; cb.onWorldEnvelope = [&, i](const WorldEnvelope& e) {
            if (e.packet.front() == static_cast<std::uint8_t>(PacketType::FieldObjects)) {
                ++fields[i]; for (const auto& field : decode(e.packet).entries) if (!field.present) ++retired[i];
            }
            if (e.packet.front() == static_cast<std::uint8_t>(PacketType::ScriptCall)) ++scripts[i];
        };
        clients[i] = std::make_unique<NetworkClient>("127.0.0.1", config.port, config.gameBuild, config.modHash,
            "field" + std::to_string(i), static_cast<SlotType>(i), cb); clients[i]->connect();
    };
    auto pump = [&] { for (int k = 0; k < 70; ++k) { relay.tick(0); for (auto& c : clients) if (c) c->tick(0); std::this_thread::sleep_for(std::chrono::milliseconds(1)); } };
    make(0); make(1); pump(); pump();
    check(relay.verifiedPeerCount() == 2, "host and friend admitted");
    if (relay.verifiedPeerCount() != 2) { for (auto& c : clients) if (c) c->disconnect(); relay.stop(); return; }
    auto m = sample(); clients[0]->sendRoomTransition(m.location); pump();
    std::uint64_t source = 100;
    auto send = [&](unsigned i, const auto& packet) {
        clients[i]->sendNativeWorld(encode(packet), ProducerWorldContext{1, clients[i]->deliverySerial(), i == 0 ? ++source : 0}, false); pump();
    };
    send(0, m); check(fields[1] == 1 && fields[0] == 0, "current native host field snapshot reaches friend raw delivery");
    auto refuse = [&](unsigned i, const auto& packet, const char* label) {
        const auto before = relay.rejectedWorldMessages(); const auto count = fields[1] + fields[2] + scripts[1] + scripts[2];
        send(i, packet);
        check(relay.rejectedWorldMessages() == before + 1 && fields[1] + fields[2] + scripts[1] + scripts[2] == count, label);
    };
    refuse(0, m, "equal field sequence refused");
    auto next = m; next.sequence = 2;
    refuse(1, next, "friend cannot forge host field roster");
    auto stale = next; --stale.location.epoch; refuse(0, stale, "stale epoch refused");
    stale = next; ++stale.location.mapProgram; refuse(0, stale, "same epoch with stale full location refused");
    make(2); pump(); pump(); check(fields[2] == 0, "late join never replays field roster");
    next.entries[0].present = false;
    send(0, next); check(fields[1] == 2 && fields[2] == 1 && retired[2] == 1,
                         "late join receives retired anchor in the next complete periodic snapshot");
    next.sequence = 3; next.entries[0].present = true; send(0, next);
    ScriptCall calls; calls.location = m.location; calls.seq = 1; calls.hostConnectionId = relay.peerBySlot(SlotType::Player)->connectionId;
    ScriptCallEntry call; call.netId = m.entries[0].netId; call.objectId = m.entries[0].objectId; call.table = 6; call.index = 45;
    calls.entries.push_back(call); send(0, calls);
    check(scripts[1] == 1 && scripts[2] == 1, "field script accepted from matching latest field identity");
    ++calls.seq; ++calls.entries[0].objectId; refuse(0, calls, "field script mismatched object identity refused");
    --calls.entries[0].objectId; ++calls.seq;
    next.sequence = 4; next.entries[0].present = false; send(0, next);
    refuse(0, calls, "retired field tombstone refuses script permission despite matching id and object");
    next.sequence = 5; next.entries.clear(); send(0, next);
    refuse(0, calls, "empty complete roster retires field script permission");
    next.sequence = 6; next.entries = m.entries; send(0, next); send(0, calls);
    check(scripts[1] == 2 && scripts[2] == 2, "refusal never advances script sequence floor");
    auto room = m.location; ++room.epoch; clients[0]->sendRoomTransition(room); pump();
    calls.location = room; ++calls.seq; refuse(0, calls, "room transition retires field roster");
    next.location = room; next.sequence = 5; refuse(0, next, "room transition retains connection sequence floor");
    next.sequence = 7; send(0, next); send(0, calls);
    check(scripts[1] == 3 && scripts[2] == 3, "fresh next-room roster restores only its own field script authority");
    // Bypass the relay with the established test-only wire fixture: raw client
    // delivery independently checks native host, full room, delivery and order.
    const auto binding = *clients[1]->worldBinding();
    WorldScope scope; scope.sessionId = binding.sessionId;
    scope.sourceConnectionId = calls.hostConnectionId; scope.sourceDeliverySerial = clients[0]->deliverySerial();
    scope.hostSourceSerial = ++source; scope.targetConnectionId = binding.selfConnectionId; scope.targetDeliverySerial = binding.deliverySerial;
    auto candidate = next; candidate.sequence = 9;
    auto inject = [&](const WorldScope& from, const FieldObjects& packet) {
        const auto bytes = worldfixture::uncheckedEnvelope(from, encode(packet));
        // Queue through the normal public tick path. Calling a private method
        // exposed by the fixture changes its MSVC access-qualified symbol.
        clients[1]->inbound_.enqueue(0, bytes, false);
        clients[1]->tick(0);
    };
    auto rejectedClient = [&](const WorldScope& from, const FieldObjects& packet, const char* label) {
        const auto before = fields[1]; inject(from, packet); check(fields[1] == before, label);
    };
    auto fake = scope; fake.kind = WorldSourceKind::Relay;
    rejectedClient(fake, candidate, "client rejects relay-authored field snapshot");
    fake = scope; fake.sourceConnectionId = binding.selfConnectionId; fake.sourceDeliverySerial = binding.deliverySerial; fake.hostSourceSerial = 0;
    rejectedClient(fake, candidate, "client rejects friend-authored field snapshot");
    fake = scope; ++fake.targetDeliverySerial;
    rejectedClient(fake, candidate, "client rejects stale delivery generation");
    auto wrongRoom = candidate; ++wrongRoom.location.door;
    rejectedClient(scope, wrongRoom, "client checks full location before raw delivery");
    auto old = candidate; old.sequence = next.sequence;
    rejectedClient(scope, old, "client rejects equal field sequence");
    const auto before = fields[1]; inject(scope, candidate);
    check(fields[1] == before + 1, "invalid client inputs never advance field sequence floor");
    rejectedClient(scope, candidate, "client rejects replay before raw callbacks");
    for (auto& c : clients) if (c) c->disconnect();
    relay.stop();
}
}
int main() { if (enet_initialize() != 0) return 2; codec(); relay(); enet_deinitialize(); std::cout << "failures=" << errors << '\n'; return errors ? 1 : 0; }
