// VUH-2252: ScriptCall wire format, relay forwarding and the DLL's send-on-change rules.
#include "kh2coop/Codec.hpp"
#include "kh2coop/NetworkClient.hpp"
#include "kh2coop/SessionHost.hpp"
#include "ScriptMirror.hpp"
#include <enet/enet.h>
#include <array>
#include <chrono>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <thread>
#include <vector>
using namespace kh2coop;
namespace R = kh2coop::inject::scriptmirror::rules;
namespace {
int errors = 0;
void check(bool ok, const char* text) { std::cout << (ok ? "PASS " : "FAIL ") << text << '\n'; if (!ok) ++errors; }

std::uint64_t flt(float f) { std::uint32_t b; std::memcpy(&b, &f, 4); return R::Tagged(R::kTagFloat, b); }
std::uint64_t num(std::uint32_t v) { return R::Tagged(R::kTagInt, v); }

ScriptCall sample() {
    ScriptCall m;
    m.location = {7, 2, 0x19, 0, 0, 149, 0};
    m.seq = 42; m.hostConnectionId = 10; m.hostFrame = 900;
    ScriptCallEntry tilt; tilt.netId = 3; tilt.objectId = 0x1FF; tilt.table = 8; tilt.index = 2;
    tilt.args = {flt(0.25f), flt(1.5f), flt(-0.125f), flt(1.0f)};
    ScriptCallEntry tex; tex.netId = 3; tex.objectId = 0x1FF; tex.table = 1; tex.index = 85; tex.args = {num(4), 0, 0, 0};
    m.entries = {tilt, tex};
    return m;
}
bool refusedRead(const std::vector<std::uint8_t>& bytes) {
    try { const std::uint8_t* p; std::size_t n; decodePacketHeader(bytes.data(), bytes.size(), p, n); ByteReader r(p, n); ScriptCall out; read(r, out); }
    catch (const std::exception&) { return true; }
    return false;
}
bool refusedWrite(const ScriptCall& m) { try { encode(m); } catch (const std::exception&) { return true; } return false; }

void codecChecks() {
    const auto m = sample();
    const auto bytes = encode(m);
    const std::uint8_t* p = nullptr; std::size_t n = 0;
    check(PacketType::ScriptCall == static_cast<PacketType>(49) && decodePacketHeader(bytes.data(), bytes.size(), p, n) == PacketType::ScriptCall &&
          n == SCRIPT_CALL_HEADER_BYTES + 2u * SCRIPT_CALL_ENTRY_BYTES, "script call is packet type 49, 37 + 45 * n payload bytes");
    ByteReader reader(p, n); ScriptCall copy; read(reader, copy);
    check(encode(copy) == bytes && reader.atEnd() && copy.seq == 42 && copy.hostConnectionId == 10 && copy.hostFrame == 900 &&
          copy.entries.size() == 2 && copy.entries[0].args == m.entries[0].args && copy.entries[1].index == 85 &&
          sameResyncRoom(copy.location, m.location), "script call exact codec roundtrip in field order");
    for (int delta : {-1, 1}) {
        auto bad = bytes; if (delta < 0) bad.pop_back(); else bad.push_back(0);
        check(refusedRead(bad), "truncated or trailing script call frame refused");
        bad[1] = static_cast<std::uint8_t>(bad.size() - 3); bad[2] = static_cast<std::uint8_t>((bad.size() - 3) >> 8);
        check(refusedRead(bad), "script call with a self-consistent length off the entry grid refused");
    }
    check(isWorldPacket(PacketType::ScriptCall) && isScopedWorldPacket(PacketType::ScriptCall) &&
          !isMaterialWorldPacket(PacketType::ScriptCall) && !isEphemeralWorldPacket(PacketType::ScriptCall) &&
          validateScopedWorldPacket(bytes) == PacketType::ScriptCall, "script call is a scoped, non-material world packet");
    auto bad = m; bad.entries.clear(); check(refusedWrite(bad), "empty script call refused");
    bad = m; bad.entries.assign(SCRIPT_CALL_MAX_ENTRIES + 1, m.entries[1]); check(refusedWrite(bad), "33 entries refused");
    bad = m; bad.seq = 0; check(refusedWrite(bad), "zero sequence refused");
    bad = m; bad.entries[1].index = 121; check(refusedWrite(bad), "a trap outside the list (target_search) refused");
    bad = m; bad.entries[1].netId = 0; check(refusedWrite(bad), "netId 0 refused");
    bad = m; bad.entries[1].args[1] = num(1); check(refusedWrite(bad), "a nonzero unused value refused");
    bad = m; bad.entries[1].args[0] = R::Tagged(R::kTagAddress, 5); check(refusedWrite(bad), "a handle value refused");
    bad = m; bad.entries[0].args[2] = num(1); check(refusedWrite(bad), "an int inside a vector refused");
    bad = m; bad.entries[0].args[0] = flt(std::numeric_limits<float>::quiet_NaN()); check(refusedWrite(bad), "NaN refused");
    bad = m; bad.entries[0].args[0] = flt(2.0e5f); check(refusedWrite(bad), "out-of-bound float refused");
    // Same refusals on read: patch one byte of a valid frame (entry 1 index low byte at 37 + 45 + 7).
    auto withEffect = m; withEffect.entries[1].effect = 9; check(refusedWrite(withEffect), "an effect key on an actor call refused");
    auto patched = bytes; patched[3 + SCRIPT_CALL_HEADER_BYTES + SCRIPT_CALL_ENTRY_BYTES + 7] = 121;
    check(refusedRead(patched), "reader refuses a trap outside the list");
    patched = bytes; patched[3 + SCRIPT_CALL_HEADER_BYTES - 1] = 0;
    check(refusedRead(patched), "reader refuses a zero entry count");
}

void rulesAgreeWithCodec() {
    bool shapes = true, values = true, converted = true;
    for (std::size_t i = 0; i < R::kTrapCount; ++i) {
        const auto& t = R::kTraps[i];
        const auto shape = scriptCallShape(t.table, t.index);
        const unsigned scriptArgs = t.kind == R::Kind::EffectStartPos ? 5u : 1u + (t.vector ? 1u : t.values);
        const bool pinned = t.body && t.bodySize > 0 && (t.flags & 0xFFFF) == scriptArgs &&
                            R::FindTrap(t.table, t.index) == static_cast<int>(i);
        if (t.replay == R::Replay::None) {  // never on the wire itself: it travels as another trap
            const int wire = R::FindWire(t.wireTable, t.wireIndex);
            converted = converted && pinned && shape.kind == ScriptCallKind::None && wire >= 0 &&
                        R::kTraps[wire].kind != R::Kind::EffectStartPos && R::kTraps[wire].replay != R::Replay::None;
            continue;
        }
        shapes = shapes && pinned && shape.argc == t.values && (shape.kind == ScriptCallKind::Vector4) == t.vector &&
                 shape.kind != ScriptCallKind::None && shape.effect == R::EffectRecord(i) &&
                 t.wireTable == t.table && t.wireIndex == t.index;
        const std::uint64_t cases[][4] = {{0, 0, 0, 0}, {num(1), 0, 0, 0}, {flt(0.5f), 0, 0, 0}, {flt(1), flt(2), flt(3), flt(4)},
                                          {num(1), num(2), 0, 0}, {flt(1), num(2), flt(3), flt(4)}, {R::Tagged(R::kTagAddress, 1), 0, 0, 0},
                                          {num(1), num(2), num(3), 0}, {flt(0.5f), num(1), 0, 0}};
        for (const auto& v : cases) {
            ScriptCallEntry e; e.netId = 1; e.table = t.table; e.index = t.index; e.effect = R::EffectRecord(i) ? 7u : 0u;
            for (int k = 0; k < 4; ++k) e.args[static_cast<std::size_t>(k)] = v[k];
            values = values && R::ValidValues(i, v) == validScriptCallEntry(e);
            auto wrongEffect = e; wrongEffect.effect = e.effect ? 0u : 7u;
            values = values && !validScriptCallEntry(wrongEffect);
        }
    }
    check(shapes, "every DLL trap sent as itself is on the wire list with the same shape, effect id rule, argument count and a pinned body");
    check(converted, "start_pos and set_dir never go on the wire; each travels as a trap that does");
    check(values, "DLL value rules and wire validation agree on every trap; an effect id is required exactly for effect calls");
    check(R::FindTrap(1, 121) < 0 && scriptCallShape(1, 121).kind == ScriptCallKind::None, "target_search is not mirrored");
}

R::Call call(std::uint16_t netId, std::uint8_t trap, std::uint64_t v0 = 0) {
    R::Call c; c.netId = netId; c.objectId = 0x1FF; c.trap = trap;
    if (R::kTraps[trap].vector) { c.values[0] = v0 ? v0 : flt(0); c.values[1] = flt(0); c.values[2] = flt(0); c.values[3] = flt(1); }
    else if (R::kTraps[trap].values) c.values[0] = v0;
    return c;
}
void senderChecks() {
    using O = R::Sender::Outcome;
    const auto rot = static_cast<std::uint8_t>(R::FindTrap(1, 1)), xyz = static_cast<std::uint8_t>(R::FindTrap(8, 2));
    const auto texOn = static_cast<std::uint8_t>(R::FindTrap(1, 85)), texOff = static_cast<std::uint8_t>(R::FindTrap(1, 86));
    const auto hide = static_cast<std::uint8_t>(R::FindTrap(6, 45)), show = static_cast<std::uint8_t>(R::FindTrap(6, 64));
    R::Sender s;
    check(s.Observe(call(3, xyz, flt(0.2f))) == O::Queued && s.Observe(call(3, xyz, flt(0.2f))) == O::Repeat,
          "an identical queued call is a repeat");
    check(s.Observe(call(3, xyz, flt(0.3f))) == O::Replaced && s.Pending() == 1, "a changed value replaces the queued one");
    check(s.Observe(call(3, rot, flt(1.0f))) == O::Replaced && s.Pending() == 1, "set_rot and set_xyzrot share the rotation key");
    check(s.Observe(call(4, rot, flt(1.0f))) == O::Queued && s.Pending() == 2, "another enemy has its own key");
    R::Call out[32];
    check(s.Peek(out, 32) == 2 && out[0].netId == 3 && out[0].trap == rot && out[1].netId == 4, "queued calls flush in call order");
    s.Commit(2);
    check(s.Pending() == 0 && s.Observe(call(3, rot, flt(1.0f))) == O::Repeat && s.Observe(call(4, rot, flt(1.0f))) == O::Repeat,
          "every frame's identical call after a send is not re-sent");
    check(s.Observe(call(3, rot, flt(1.5f))) == O::Queued, "a change after the send queues again");
    check(s.Observe(call(3, texOn, num(4))) == O::Queued && s.Observe(call(3, texOn, num(5))) == O::Queued &&
          s.Observe(call(3, texOff, num(4))) == O::Replaced, "texture animations are keyed by id; stop replaces its start");
    check(s.Observe(call(3, hide)) == O::Queued && s.Observe(call(3, show)) == O::Replaced, "shadow hide and show share a key");
    std::size_t n = s.Peek(out, 32);
    check(n == 4, "four keys pending");
    s.Commit(1);  // only the first went out: the rest stay queued for the next packet
    check(s.Pending() == 3 && s.Observe(call(3, rot, flt(1.5f))) == O::Repeat, "a partial commit keeps the rest queued");
    s.Reset();
    check(s.Pending() == 0 && s.Observe(call(3, rot, flt(1.0f))) == O::Queued, "reset (room change) forgets every baseline");
    R::Sender full;
    bool ok = true;
    for (std::uint16_t id = 1; id <= R::Sender::kPendingCap; ++id) ok = ok && full.Observe(call(id, rot, flt(1))) == O::Queued;
    check(ok && full.Observe(call(999, rot, flt(1))) == O::Full, "pending is bounded");
    // Per-second counts: 120 frames of the same tilt from 3 enemies cost 3 records.
    R::Sender boss; unsigned queued = 0, repeats = 0;
    for (int frame = 0; frame < 120; ++frame) {
        for (std::uint16_t id = 1; id <= 3; ++id) { const auto o = boss.Observe(call(id, xyz, flt(0.4f))); queued += o == O::Queued; repeats += o == O::Repeat; }
        boss.Commit(boss.Pending());
    }
    check(queued == 3 && repeats == 357, "a boss repeating the same call every frame sends it once");
}

void holdChecks() {
    const auto rot = static_cast<std::uint8_t>(R::FindTrap(1, 1));
    R::Hold h;
    check(h.Put(call(3, rot, flt(1)), 10) && h.Put(call(3, rot, flt(2)), 11) && h.Size() == 1, "hold keeps the latest value per key");
    check(h.Put(call(4, rot, flt(1)), 12) && h.Size() == 2, "hold keeps one item per key");
    std::size_t expired = 0; std::vector<std::uint16_t> applied;
    auto ready3 = [&](const R::Call& c) { if (c.netId != 3) return false; applied.push_back(c.netId); return true; };
    check(h.Drain(20, ready3, expired) == 1 && applied.size() == 1 && h.Size() == 1 && expired == 0, "drain applies ready copies only");
    check(h.Drain(12 + R::Hold::kHoldFrames + 1, ready3, expired) == 0 && expired == 1 && h.Size() == 0, "an unbound copy's call expires");
    h.Put(call(3, rot, flt(1)), 30); h.Erase(R::KeyOf(call(3, rot, flt(9))));
    check(h.Size() == 0, "erase drops the held value for a key a newer call already set");
}

R::Call effectCall(std::uint32_t effect, std::uint8_t table, std::uint16_t index, float v0 = 0) {
    R::Call c; c.netId = 5; c.objectId = 0x161; c.effect = effect;
    c.trap = static_cast<std::uint8_t>(R::FindWire(table, index));
    const auto& t = R::kTraps[c.trap];
    if (t.vector) { c.values[0] = flt(v0); c.values[1] = flt(0); c.values[2] = flt(0); c.values[3] = flt(1); }
    else for (unsigned k = 0; k < t.values; ++k) c.values[k] = num(k + 1);
    return c;
}
void effectChecks() {
    using O = R::Sender::Outcome;
    R::Sender s;
    check(s.Observe(effectCall(1, 1, 21)) == O::Queued && s.Observe(effectCall(1, 1, 21)) == O::Queued,
          "an effect start is an event: two identical starts both queue");
    check(s.Observe(effectCall(1, 0, 38, 10)) == O::Queued && s.Observe(effectCall(1, 0, 38, 10)) == O::Repeat &&
          s.Observe(effectCall(1, 0, 38, 11)) == O::Replaced, "an effect's position is sent on change");
    check(s.Observe(effectCall(2, 0, 38, 11)) == O::Queued && s.Observe(effectCall(1, 0, 40, 11)) == O::Queued,
          "each effect and each field has its own key");
    check(s.Observe(effectCall(1, 0, 51)) == O::Queued && s.Observe(effectCall(1, 0, 51)) == O::Queued,
          "kills are events too");
    R::Call out[32];
    const std::size_t n = s.Peek(out, 32);
    check(n == 7 && out[0].trap == R::FindWire(1, 21) && out[2].values[0] == flt(11) && out[6].trap == R::FindWire(0, 51),
          "starts, fields and kills flush in call order");
    s.Commit(n);
    check(s.Observe(effectCall(1, 0, 38, 11)) == O::Repeat && s.Observe(effectCall(1, 1, 21)) == O::Queued,
          "a sent field is a baseline; a sent event is not");
    // A moving effect: 120 frames of set_pos that changes every 4 frames costs 30 records.
    R::Sender moving; unsigned queued = 0;
    for (int frame = 0; frame < 120; ++frame) {
        queued += moving.Observe(effectCall(9, 0, 38, static_cast<float>(frame / 4))) == O::Queued;
        moving.Commit(moving.Pending());
    }
    check(queued == 30, "a travelling effect sends one record per new position");

    R::HostEffects host;
    const auto a = host.Add(0x100, 0x5000, 0x6000, 5, 0x161), b = host.Add(0x200, 0x5100, 0x6100, 6, 0x162);
    check(a && b > a && host.Find(0x100)->id == a && host.Find(0x200)->netId == 6 && !host.Find(0x300), "host effects get fresh ids by handle");
    const auto again = host.Add(0x100, 0x5200, 0x6200, 5, 0x161);
    check(again > b && host.Find(0x100)->id == again && host.Find(0x100)->part == 0x6200, "a reused handle gets a new id");
    host.EraseOwner(5);
    check(!host.Find(0x100) && host.Find(0x200), "kill_all drops only that enemy's effects");
    R::HostEffects full;
    for (std::uint32_t h = 1; h <= R::HostEffects::kCap; ++h) full.Add(h, h, h, 1, 1);
    full.Add(9999, 1, 1, 1, 1);
    check(full.Size() == R::HostEffects::kCap && !full.Find(1) && full.Find(9999), "host effects are bounded, oldest out first");

    R::ClientEffects client;
    client.Put(a, {0x11, 0x5000, 0x6000, 5}); client.Put(b, {0x12, 0x5100, 0x6100, 6});
    check(client.Find(a)->handle == 0x11 && client.Find(b)->netId == 6 && !client.Find(999), "client pairs a host id with its own effect");
    client.EraseOwner(6); client.Erase(a);
    check(!client.Find(a) && !client.Find(b), "client pairs end with the effect or its enemy");

    R::Hold h;
    h.Put(effectCall(4, 1, 21), 10);
    h.Put(effectCall(4, 0, 38, 1), 10);
    h.Put(effectCall(4, 0, 38, 2), 11);
    check(h.Size() == 2 && h.HoldsStart(4) && !h.HoldsStart(5) && h.HoldsNetId(5) && !h.HoldsNetId(6),
          "a held start keeps its later field (latest value) behind it");
    std::vector<std::uint8_t> order; std::size_t expired = 0; bool ready = false;
    auto apply = [&](const R::Call& c) { if (!ready) return false; order.push_back(c.trap); return true; };
    check(h.Drain(12, apply, expired) == 0 && h.Size() == 2, "nothing drains before the copy is driven");
    ready = true;
    check(h.Drain(13, apply, expired) == 2 && order.size() == 2 && order[0] == R::FindWire(1, 21) && order[1] == R::FindWire(0, 38),
          "once driven, the start lands before its field");
    R::Hold blocked; std::size_t exp2 = 0;
    blocked.Put(call(5, static_cast<std::uint8_t>(R::FindTrap(1, 1)), flt(1)), 1);  // netId 5, not ready
    blocked.Put(effectCall(4, 1, 21), 1);                                          // netId 5 too: waits behind it
    auto onlyEffects = [&](const R::Call& c) { return c.effect != 0; };
    check(blocked.Drain(2, onlyEffects, exp2) == 0 && blocked.Size() == 2, "a later record for a netId waits behind an earlier one");
}

void enetChecks() {
    SessionConfig config; config.bindAddress = "127.0.0.1"; config.port = 29881;
    config.gameBuild = "script-call-test"; config.modHash = "none";
    std::string lastReason;
    SessionCallbacks relayCallbacks; relayCallbacks.onLog = [&](const std::string& m) {
        const std::string key = "ScriptCall refused reason="; const auto at = m.find(key);
        if (at != std::string::npos) { const auto start = at + key.size(); lastReason = m.substr(start, m.find(' ', start) - start); }
    };
    SessionHost server(config, relayCallbacks); if (!server.start()) { check(false, "script call ENet bind"); return; }
    std::array<std::vector<ScriptCall>, 3> received;
    std::array<std::unique_ptr<NetworkClient>, 3> clients;
    auto make = [&](unsigned i) {
        ClientCallbacks cb;
        cb.onWorldEnvelope = [&, i](const WorldEnvelope& e) {
            if (e.packet.front() != static_cast<std::uint8_t>(PacketType::ScriptCall)) return;
            const std::uint8_t* p; std::size_t n; decodePacketHeader(e.packet.data(), e.packet.size(), p, n);
            ByteReader r(p, n); ScriptCall c; read(r, c); received[i].push_back(c);
        };
        clients[i] = std::make_unique<NetworkClient>("127.0.0.1", config.port, "script-call-test", "none", "sc" + std::to_string(i),
                                                     static_cast<SlotType>(i), cb);
        clients[i]->connect();
    };
    auto pump = [&] { for (unsigned k = 0; k < 30; ++k) { server.tick(); for (auto& c : clients) if (c) c->tick(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); } };
    make(0); make(1); pump(); pump();
    check(server.verifiedPeerCount() == 2, "script call: host and first friend admitted");
    if (server.verifiedPeerCount() != 2) { for (auto& c : clients) if (c) c->disconnect(); server.stop(); return; }
    const RoomTransition room{20, 2, 0x19, 0, 0, 149, 0};
    clients[0]->sendRoomTransition(room); pump();
    EnemyManifest manifest; manifest.epoch = room.epoch;
    EnemyManifestEntry enemy; enemy.netId = 3; enemy.objectId = 0x1FF; manifest.entries.push_back(enemy);
    clients[0]->sendEnemyManifest(manifest); pump();
    const auto hostId = server.peerBySlot(SlotType::Player)->connectionId;
    auto calls = sample(); calls.location = room; calls.seq = 1; calls.hostConnectionId = hostId;
    clients[0]->sendScriptCall(calls); pump();
    check(received[1].size() == 1 && received[0].empty() && received[1][0].entries.size() == 2 && received[1][0].entries[0].table == 8,
          "script call forwarded to the friend, not back to the host");
    make(2); pump(); pump();
    check(server.verifiedPeerCount() == 3 && received[2].empty(), "late joiner gets no script call replay");
    ++calls.seq; clients[0]->sendScriptCall(calls); pump();
    check(received[1].size() == 2 && received[2].size() == 1 && received[0].empty(), "script call forwarded to both clients");
    auto total = [&] { return received[0].size() + received[1].size() + received[2].size(); };
    auto refusedBy = [&](const char* reason, const char* label, const auto& send) {
        const auto before = total(); const auto rejected = server.rejectedWorldMessages(); lastReason.clear();
        send(); pump();
        check(total() == before && server.rejectedWorldMessages() == rejected + 1 && lastReason == reason, label);
        if (lastReason != reason) std::cout << "  relay reason=" << lastReason << " expected=" << reason << '\n';
    };
    {   auto forged = calls; forged.seq = 1000;
        refusedBy("sender-not-verified-host", "non-host script call refused", [&] {
            clients[1]->sendNativeWorld(encode(forged), ProducerWorldContext{1, clients[1]->deliverySerial(), 0}, true); });
    }
    refusedBy("sequence", "duplicate script call sequence refused", [&] { clients[0]->sendScriptCall(calls); });
    auto stale = calls; ++calls.seq; stale.seq = calls.seq; stale.location.epoch = 19;
    refusedBy("room-mismatch", "old room epoch script call refused", [&] { clients[0]->sendScriptCall(stale); });
    auto forgedHost = calls; ++calls.seq; forgedHost.seq = calls.seq; forgedHost.hostConnectionId = hostId + 1;
    refusedBy("host-connection", "forged host connection script call refused", [&] { clients[0]->sendScriptCall(forgedHost); });
    ++calls.seq; clients[0]->sendScriptCall(calls); pump();
    check(received[1].size() == 3 && received[2].size() == 2, "a valid script call after refusals still forwards");
    for (auto& c : clients) if (c) c->disconnect();
    server.stop();
}
} // namespace

int main() {
    if (enet_initialize() != 0) return 2;
    codecChecks();
    rulesAgreeWithCodec();
    senderChecks();
    holdChecks();
    effectChecks();
    enetChecks();
    enet_deinitialize();
    std::cout << "failures=" << errors << '\n';
    if (!errors) std::cout << "ALL CHECKS PASSED\n";
    return errors ? 1 : 0;
}
