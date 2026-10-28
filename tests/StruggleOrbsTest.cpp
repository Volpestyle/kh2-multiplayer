// VUH-2466: the Struggle orb rows (protocol 34), the host-book / friend-display rules
// (inject/src/StruggleOrbs.hpp) and the detours' gate and installation (StruggleOrbsAdapter.hpp).
// No KH2: records are fake addresses and the natives are counting fakes.
#include "StruggleOrbs.hpp"
#include "StruggleOrbsAdapter.hpp"
#include "kh2coop/Codec.hpp"
#include "kh2coop/SpawnEmissionLedger.hpp"

#include <cmath>
#include <cstdio>
#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <string>
#include <vector>
#include <thread>
#include <future>
#include <atomic>

using namespace kh2coop;
namespace so = kh2coop::inject::struggleorbs;

namespace {
int checks = 0, failed = 0;
void check(bool ok, const std::string& name) { ++checks; if (!ok) { ++failed; std::printf("FAIL %s\n", name.c_str()); } }
bool throws(const std::function<void()>& f) { try { f(); } catch (const std::exception&) { return true; } return false; }

// 02/05 btl84 (TT05_MS401), epoch 2.
RoomTransition Room() { RoomTransition r{}; r.epoch = 2; r.worldId = 2; r.roomId = 5; r.mapProgram = 84; r.battleProgram = 84; r.eventProgram = 84; return r; }

SpawnEmission Birth(std::uint64_t seq, std::uint16_t id, std::uint8_t type = 0, std::uint16_t match = 1) {
    SpawnEmission e{}; e.location = Room(); e.sequence = seq; e.kind = SpawnEmissionKind::OrbBirth; e.orbId = id; e.orbType = type;
    e.orbMatch = match;
    e.point = {-150, -90, 0, 1}; e.velocity = {0.5f, 12.0f, -0.25f};
    return e;
}
SpawnEmission Collect(std::uint64_t seq, std::uint16_t id, OrbCollector c = OrbCollector::Host, std::uint16_t netId = 0,
                      std::uint16_t match = 1) {
    SpawnEmission e{}; e.location = Room(); e.sequence = seq; e.kind = SpawnEmissionKind::OrbCollect; e.orbId = id; e.collector = c;
    e.collectorNetId = netId; e.orbMatch = match;
    return e;
}
SpawnEmission Decode(const std::vector<std::uint8_t>& bytes) {
    const std::uint8_t* p; std::size_t n;
    if (decodePacketHeader(bytes.data(), bytes.size(), p, n) != PacketType::SpawnEmission) throw std::runtime_error("type");
    ByteReader r(p, n); SpawnEmission out; read(r, out); return out;
}

void Codec() {
    const auto birth = Birth(5, 9, 2);
    auto bytes = encode(birth);
    const std::uint8_t* p; std::size_t n;
    check(PROTOCOL_VERSION == 40 && decodePacketHeader(bytes.data(), bytes.size(), p, n) == PacketType::SpawnEmission &&
          n == SPAWN_EMISSION_BYTES && n == 71, "v34: an orb row is a 71-byte SpawnEmission");
    const auto back = Decode(bytes);
    check(back.kind == SpawnEmissionKind::OrbBirth && back.orbId == 9 && back.orbType == 2 && back.velocity == birth.velocity &&
          back.point == birth.point && back.orbMatch == 1 && encode(back) == bytes, "orb birth round trips");
    const auto take = Collect(6, 9, OrbCollector::Enemy, 1);
    const auto tback = Decode(encode(take));
    check(tback.kind == SpawnEmissionKind::OrbCollect && tback.collector == OrbCollector::Enemy && tback.collectorNetId == 1,
          "orb collection round trips with its enemy collector");
    check(validateScopedWorldPacket(bytes) == PacketType::SpawnEmission, "orb rows keep the material world-packet policy");
    const auto unresolved = Collect(7, 9, OrbCollector::Unresolved);
    check(Decode(encode(unresolved)).collector == OrbCollector::Unresolved && validSpawnEmission(unresolved),
          "an unresolved collector is still a collection on the wire");
    check(!validSpawnEmission(Collect(7, 9, OrbCollector::None)) && !validSpawnEmission(Collect(7, 9, OrbCollector::Enemy, 0)) &&
          validSpawnEmission(Collect(7, 9, OrbCollector::Enemy, 1)), "validSpawnEmission is what the codec takes");
    auto noMatch = birth; noMatch.orbMatch = 0;
    check(throws([&] { encode(noMatch); }) && !validSpawnEmission(noMatch), "an orb row names its match");
    auto collectNoMatch = take; collectNoMatch.orbMatch = 0;
    check(throws([&] { encode(collectNoMatch); }), "a collection names its match");
    // A v33 peer's 50-byte controller row is refused, as is a v34 row cut to that length.
    SpawnEmission controller{}; controller.location = Room(); controller.sequence = 1; controller.controllerKey = 99;
    controller.headerId = 750; controller.point = {1, 2, 3, 1};
    auto v33 = encode(controller); v33.resize(3 + 50); v33[1] = 50; v33[2] = 0;
    check(throws([&] { Decode(v33); }), "a v33 50-byte SpawnEmission is refused");
    check(Decode(encode(controller)).kind == SpawnEmissionKind::Controller, "a controller row stays a controller row");
    auto bad = birth; bad.collector = OrbCollector::Host;
    check(throws([&] { encode(bad); }), "a birth names no collector");
    bad = birth; bad.orbType = ORB_TYPES;
    check(throws([&] { encode(bad); }), "a birth's type is one of the four descriptors");
    bad = birth; bad.orbId = 0;
    check(throws([&] { encode(bad); }), "an orb row has an ID");
    bad = birth; bad.controllerKey = 1;
    check(throws([&] { encode(bad); }), "an orb row carries no controller identity");
    bad = birth; bad.netId = 3;
    check(throws([&] { encode(bad); }), "an orb row carries no combat netId");
    bad = birth; bad.velocity[1] = std::numeric_limits<float>::infinity();
    check(throws([&] { encode(bad); }), "a birth's scatter is finite");
    auto badTake = take; badTake.collectorNetId = 0;
    check(throws([&] { encode(badTake); }), "an enemy collector has its netId");
    badTake = Collect(6, 9, OrbCollector::Friend, 4);
    check(throws([&] { encode(badTake); }), "a player collector has none");
    badTake = Collect(6, 9, OrbCollector::None);
    check(throws([&] { encode(badTake); }), "a collection names its collector");
    auto withOrb = controller; withOrb.orbId = 1;
    check(throws([&] { encode(withOrb); }), "a controller row carries no orb fields");
    withOrb = controller; withOrb.orbMatch = 1;
    check(throws([&] { encode(withOrb); }), "a controller row carries no orb match");
    auto unknown = encode(birth); unknown[3 + 50] = 3;  // kind byte
    check(throws([&] { Decode(unknown); }), "an unknown kind is refused");
    auto collector = encode(take); collector[3 + 66] = 5;  // collector byte
    check(throws([&] { Decode(collector); }), "an unknown collector is refused");
}

void Ledger() {
    SpawnEmissionLedger l;
    SpawnEmission controller{}; controller.location = Room(); controller.sequence = 4; controller.point = {1, 2, 3, 1};
    check(l.Add(Birth(2, 1)) && l.Add(Collect(3, 1)) && l.Add(controller), "orb and controller rows share the ledger");
    check(l.Pending(0, 0) && l.Pending(0, 0)->kind == SpawnEmissionKind::Controller,
          "a controller lookup never answers with an orb row, even for key 0 header 0");
    check(l.Permit(0, 0, 0, true) && !l.Pending(0, 0), "the controller row is consumed alone");
    const auto orbs = l.TakeOrbs();
    check(orbs.size() == 2 && orbs[0].kind == SpawnEmissionKind::OrbBirth && orbs[1].kind == SpawnEmissionKind::OrbCollect,
          "the orb rows come out in arrival order");
    check(l.TakeOrbs().empty(), "each orb row is taken once");
    check(!l.Add(Birth(5, 1)) && !l.Add(Collect(6, 1)), "an orb is born once and collected once, whatever its sequence");
    check(l.Add(Birth(7, 2)), "another orb is new");
    check(!l.Add(Birth(7, 3)), "the sequence floor still holds");
}

void HostRules() {
    so::HostBook book;
    check(!book.Birth(0, 0x10).id && !book.Birth(0x1000, 0).id, "no manager or record, no ID");
    const auto a = book.Birth(0x1000, 0x10), b = book.Birth(0x1000, 0x20);
    check(a.id == 1 && b.id == 2 && a.match == 1 && b.match == 1 && book.LiveCount() == 2, "IDs count up; the first manager is match 1");
    check(book.Collect(0x1000, 0x20).id == 2 && !book.Collect(0x1000, 0x20).id, "a collection names the orb once");
    check(!book.Collect(0x2000, 0x10).id, "another manager's record is not this book's orb");
    check(book.Birth(0x1000, 0x10).id == 3 && book.LiveCount() == 1, "a record born again replaces its old orb");
    const auto c = book.Birth(0x3000, 0x10);
    check(c.id == 4 && c.match == 2 && book.LiveCount() == 1 && !book.Collect(0x1000, 0x10).id,
          "a replaced manager is the next match: its orbs go, the IDs carry on");
    book.Reset();
    check(book.Birth(0x3000, 0x10).id == 1 && book.Match() == 1, "only the room's end restarts the IDs");
    // Quill r2: the manager gone, then back at the same address, is the next match.
    book.Observe(0x3000);
    check(book.LiveCount() == 1 && book.Match() == 1, "observing the same manager changes nothing");
    book.Observe(0);
    check(book.LiveCount() == 0 && !book.Collect(0x3000, 0x10).id, "a manager seen gone takes its orbs: no late collection");
    const auto again = book.Birth(0x3000, 0x20);
    check(again.id == 2 && again.match == 2, "the same address after a loss is the next match, IDs carry on");
    check(!book.Collect(0x3000, 0x10).id && book.Collect(0x3000, 0x20).id == 2, "only the new match's orb is collected");
    check(so::DescriptorType(0x140000000, 0x140000000 + so::kDescriptorsRva) == 0 &&
          so::DescriptorType(0x140000000, 0x140000000 + so::kDescriptorsRva + 0x30) == 3 &&
          so::DescriptorType(0x140000000, 0x140000000 + so::kDescriptorsRva + 0x40) == -1 &&
          so::DescriptorType(0x140000000, 0x140000000 + so::kDescriptorsRva + 8) == -1 &&
          so::DescriptorType(0x140000000, 0x140000000 + so::kDescriptorsRva - 0x10) == -1,
          "only the four orb descriptors are orbs");
}

// A friend display with fake natives.
struct FakeFriend {
    so::ClientDisplay display;
    std::uintptr_t nextRecord = 0x7000;
    std::map<std::uintptr_t, std::uint16_t> shown;  // record -> orb
    std::vector<so::VanishRequest> flights;
    unsigned creates = 0;
    bool refuseCreate = false;
    std::vector<std::uintptr_t> reuse;  // records the native pool hands out again
    void Tick(std::uintptr_t manager) {
        display.Tick(manager,
            [&](const so::CreateRequest& r) -> std::uintptr_t {
                ++creates;
                if (refuseCreate) return 0;
                std::uintptr_t record = nextRecord;
                if (!reuse.empty()) { record = reuse.front(); reuse.erase(reuse.begin()); } else nextRecord += 0x100;
                shown[record] = r.id;
                return record;
            },
            [&](const so::VanishRequest& v) {
                flights.push_back(v);
                const auto it = shown.find(v.record);
                if (it == shown.end() || it->second != v.id) return false;
                shown.erase(it);
                return true;
            });
    }
};

void FriendRules() {
    {
        FakeFriend f;
        f.display.Receive({Birth(1, 1), Birth(2, 2)});
        f.Tick(0);
        check(f.creates == 0 && f.display.Queued() == 2, "rows wait while this game has no orb manager");
        f.Tick(0x1000);
        f.display.Receive({Collect(3, 1, OrbCollector::Friend)});
        f.Tick(0x1000);
        check(f.display.Shown() == 1 && f.flights.size() == 1 && f.flights[0].id == 1 &&
              f.flights[0].collector == OrbCollector::Friend, "a collected orb flies to its collector; the other stays");
    }
    {
        FakeFriend f;
        f.display.Receive({Birth(1, 1), Birth(2, 2), Collect(3, 1)});
        f.Tick(0x1000);
        check(f.creates == 1 && f.display.Shown() == 1 && f.flights.empty() && f.display.Stats().elided == 1,
              "a birth whose collection is already queued is never shown");
    }
    {
        FakeFriend f;
        f.display.Receive({Collect(1, 4, OrbCollector::Host)});
        f.Tick(0x1000);
        f.display.Receive({Birth(2, 4)});
        f.Tick(0x1000);
        check(f.creates == 0 && f.display.Shown() == 0, "an orb collected before its birth arrived is never shown");
    }
    {
        FakeFriend f;
        f.display.Receive({Birth(1, 1), Birth(2, 1)});
        f.Tick(0x1000);
        f.display.Receive({Collect(3, 1), Collect(4, 1)});
        f.Tick(0x1000);
        check(f.creates == 1 && f.flights.size() == 1 && f.display.Stats().skipped == 2,
              "a repeated birth or collection does nothing");
    }
    {
        FakeFriend f;
        f.refuseCreate = true;
        f.display.Receive({Birth(1, 1)});
        f.Tick(0x1000);
        f.display.Receive({Collect(2, 1)});
        f.Tick(0x1000);
        check(f.display.Stats().createFailed == 1 && f.flights.empty() && f.display.Shown() == 0,
              "a refused create shows nothing and its collection starts no flight");
    }
    {
        FakeFriend f;
        f.display.Receive({Birth(1, 1)});
        f.Tick(0x1000);
        f.display.Receive({Collect(2, 1)});
        f.Tick(0x2000);
        check(f.flights.empty() && f.display.Shown() == 0, "a new manager drops the old match's display orbs");
    }
    {
        FakeFriend f;
        std::vector<SpawnEmission> many;
        for (std::uint16_t i = 1; i <= 100; ++i) many.push_back(Birth(i, i));
        f.display.Receive(many);
        f.Tick(0x1000);
        check(f.creates == so::ClientDisplay::kPerTick && f.display.Queued() == 100 - so::ClientDisplay::kPerTick,
              "at most 64 native calls per frame");
        f.Tick(0x1000);
        check(f.creates == 100 && f.display.Queued() == 0 && f.display.Shown() == 100, "the rest follow next frame");
    }
    {
        FakeFriend f;  // the pool hands a record back only after its orb was freed
        f.display.Receive({Birth(1, 1)});
        f.Tick(0x1000);
        f.display.Receive({Collect(2, 1)});
        f.Tick(0x1000);
        f.reuse.push_back(f.flights.at(0).record);
        f.display.Receive({Birth(3, 2)});
        f.Tick(0x1000);
        f.display.Receive({Collect(4, 2)});
        f.Tick(0x1000);
        check(f.flights.size() == 2 && f.flights[1].id == 2 && f.flights[1].record == f.flights[0].record,
              "a reused record belongs to its newest orb");
    }
}

// Run 20261021-015611 (btl84, Hayner): the host logged its two orb counters every 120 frames, Roxas's
// side then Hayner's. The log has no per-orb rows, so this is one schedule consistent with each net
// change: a fall is native drops of 3 or 15 orbs (FUN_140410100), less pickups that make up the
// difference; a rise is pickups. Roxas's side alternates host and friend pickups; Hayner (netId 1)
// takes the rest. Each pickup takes the oldest orb on the field.
struct Counts { int frame, roxas, hayner; };
const Counts kBtl84[] = {{3720, 100, 100}, {3840, 94, 100}, {3960, 95, 105}, {4080, 89, 105}, {4200, 92, 106},
                         {4320, 74, 109},  {4440, 75, 122}, {4560, 75, 123}, {4680, 75, 124}, {4800, 74, 124},
                         {4920, 59, 135},  {5040, 56, 135}, {5160, 49, 142}, {5280, 47, 143}};

void Btl84Replay() {
    so::HostBook book;
    SpawnEmissionLedger ledger;
    FakeFriend f;
    const std::uintptr_t manager = 0x2AE0000;
    std::uintptr_t nextRecord = 0x9000;
    std::vector<std::uintptr_t> freeRecords;
    std::deque<std::uintptr_t> field;  // the host's live orbs, oldest first
    std::uint64_t seq = 0;
    int roxas = 100, hayner = 100, births = 0, collects = 0;
    bool friendTurn = false;
    std::vector<std::vector<std::uint8_t>> wire;
    const auto send = [&](const SpawnEmission& row) {
        wire.push_back(encode(row));
        check(ledger.Add(Decode(wire.back())), "the ledger takes each new row");
    };
    const auto drop = [&](int side, int n) {
        roxas -= side == 0 ? n : 0; hayner -= side == 1 ? n : 0;
        for (int i = 0; i < n; ++i) {
            std::uintptr_t record = nextRecord;
            if (!freeRecords.empty()) { record = freeRecords.back(); freeRecords.pop_back(); } else nextRecord += 0x100;
            const auto id = book.Birth(manager, record).id;
            field.push_back(record);
            auto row = Birth(++seq, id, static_cast<std::uint8_t>(side * 2));  // FUN_140410100: descriptor side*2
            send(row);
            ++births;
        }
    };
    const auto pick = [&](int side, int n) {
        for (int i = 0; i < n; ++i) {
            check(!field.empty(), "btl84: every pickup has an orb on the field");
            if (field.empty()) return;
            const auto record = field.front(); field.pop_front();
            const auto id = book.Collect(manager, record).id;
            OrbCollector who = OrbCollector::Enemy;
            if (side == 0) { who = friendTurn ? OrbCollector::Friend : OrbCollector::Host; friendTurn = !friendTurn; }
            send(Collect(++seq, id, who, who == OrbCollector::Enemy ? 1 : 0));
            freeRecords.push_back(record);  // freed after its flight; the pool may hand it out again
            ++collects;
            if (side == 0) ++roxas; else ++hayner;
        }
    };
    const auto fall = [&](int side, int by) {  // drops of 15 while they fit, then 3s, then pickups for the rest
        int dropped = 0;
        while (by - dropped >= 15) { drop(side, 15); dropped += 15; }
        while (dropped < by) { drop(side, 3); dropped += 3; }
        pick(side, dropped - by);
    };
    for (std::size_t i = 1; i < std::size(kBtl84); ++i) {
        const int dr = kBtl84[i].roxas - kBtl84[i - 1].roxas, dh = kBtl84[i].hayner - kBtl84[i - 1].hayner;
        if (dr < 0) fall(0, -dr);
        if (dh < 0) fall(1, -dh);
        if (dr > 0) pick(0, dr);
        if (dh > 0) pick(1, dh);
        // The friend applies what has arrived each sample, as its frame tick would.
        f.display.Receive(ledger.TakeOrbs());
        f.Tick(manager);
        f.Tick(manager);
        check(f.display.Shown() == field.size() && book.LiveCount() == field.size(),
              "btl84 frame " + std::to_string(kBtl84[i].frame) + ": the friend shows exactly the host's orbs on the field");
        check(roxas == kBtl84[i].roxas && hayner == kBtl84[i].hayner,
              "btl84 frame " + std::to_string(kBtl84[i].frame) + ": the schedule reproduces the host's counters");
    }
    check(field.size() == 10 && f.display.Shown() == 10, "btl84: 10 orbs left on the field at the end (200 - 47 - 143)");
    const auto& st = f.display.Stats();
    check(st.created + st.elided == static_cast<std::uint64_t>(births) &&
          st.vanished + st.elided == static_cast<std::uint64_t>(collects) && st.skipped == 0 && st.createFailed == 0 &&
          st.vanishFailed == 0, "btl84: every host orb shown and flown once, or (taken within the same sample) never shown");
    unsigned toFriend = 0, toHost = 0, toHayner = 0;
    for (const auto& v : f.flights)
        (v.collector == OrbCollector::Friend ? toFriend : v.collector == OrbCollector::Host ? toHost : toHayner) += 1;
    check(toFriend + toHost + toHayner == st.vanished && toHayner > 0 && toFriend > 0 && toHost > 0,
          "btl84: each flight goes to its collector (host, friend or Hayner)");
    // Late join or resync: the relay replays the room's cached rows; nothing is shown twice.
    for (const auto& bytes : wire) check(!ledger.Add(Decode(bytes)), "btl84: a replayed row is refused");
    FakeFriend late;
    SpawnEmissionLedger lateLedger;
    for (const auto& bytes : wire) lateLedger.Add(Decode(bytes));
    late.display.Receive(lateLedger.TakeOrbs());
    for (int i = 0; i < 4; ++i) late.Tick(manager);
    check(late.display.Shown() == 10 && late.creates == 10, "btl84: a friend joining late shows only the 10 orbs still out");
    std::printf("btl84 replay: births=%d collects=%d field=%zu shown=%llu elided=%llu flights host=%u friend=%u hayner=%u\n",
                births, collects, field.size(), static_cast<unsigned long long>(st.created),
                static_cast<unsigned long long>(st.elided), toHost, toFriend, toHayner);
}

// The room's ledger and both sides across a replaced orb manager (Quill r1: HostBook restarted at ID 1
// while the room's ledger refused it as a repeat). IDs now carry on through the room.
void ManagerReplacement() {
    so::HostBook book;
    SpawnEmissionLedger ledger;
    std::uint64_t seq = 0;
    const auto born = [&](std::uintptr_t manager, std::uintptr_t record) {
        const auto o = book.Birth(manager, record);
        return ledger.Add(Decode(encode(Birth(++seq, o.id, 0, o.match))));
    };
    const auto taken = [&](std::uintptr_t manager, std::uintptr_t record) {
        const auto o = book.Collect(manager, record);
        return o.id && ledger.Add(Decode(encode(Collect(++seq, o.id, OrbCollector::Host, 0, o.match))));
    };
    check(born(0xA000, 0x10) && born(0xA000, 0x20) && taken(0xA000, 0x10), "match 1 rows enter the room's ledger");
    check(born(0xB000, 0x10) && born(0xB000, 0x30), "a replaced manager's orbs enter the same ledger (new IDs)");
    check(!taken(0xA000, 0x20), "an orb of the replaced manager is not collected any more");
    const auto rows = ledger.TakeOrbs();
    check(rows.size() == 5 && rows[3].orbId == 3 && rows[3].orbMatch == 2, "IDs carry on into match 2");
    {   // The friend still on its first manager: match 2 waits; its next manager shows match 2.
        FakeFriend f;
        f.display.Receive(rows);
        f.Tick(0x5000);
        check(f.display.Showing() == 2 && f.display.Shown() == 2 && f.display.Stats().stale == 3,
              "a friend whose first manager appears after the replacement shows only the newest match");
        FakeFriend g;
        g.display.Receive({rows[0], rows[1], rows[2]});
        g.Tick(0x5000);
        check(g.display.Showing() == 1 && g.display.Shown() == 1 && g.creates == 1, "a friend on match 1 shows match 1's orbs");
        g.display.Receive({rows[3], rows[4]});
        g.Tick(0x5000);
        check(g.display.Shown() == 1 && g.display.Queued() == 2 && g.creates == 1,
              "match 2 rows wait while the friend's manager still shows match 1");
        g.Tick(0x6000);
        check(g.display.Showing() == 2 && g.display.Shown() == 2 && g.creates == 3 && g.display.Queued() == 0,
              "the friend's next manager shows match 2");
        FakeFriend a;  // Quill r2: A, then gone (no rows that frame), then A again
        a.display.Receive({rows[0], rows[1]});
        a.Tick(0xA000);
        check(a.display.Shown() == 2 && a.display.Showing() == 1, "A: match 1 shown");
        a.Tick(0);
        check(a.display.Shown() == 0 && a.display.Queued() == 0, "gone with an empty queue: every receipt retired");
        a.display.Receive({rows[2], rows[3], rows[4]});  // a late match-1 collection, then match 2
        a.Tick(0xA000);
        check(a.display.Showing() == 2 && a.flights.empty() && a.display.Stats().stale == 1 && a.display.Shown() == 2 &&
              a.creates == 4, "A again: a late old collection reaches no record; match 2 is shown");
        FakeFriend o;  // the same, observed without ticks (production observes every frame, rows or not)
        o.display.Receive({rows[0]});
        o.Tick(0xA000);
        o.display.Observe(0);
        o.display.Observe(0xA000);
        o.display.Receive({rows[3]});
        o.Tick(0xA000);
        check(o.display.Showing() == 2 && o.display.Shown() == 1 && o.creates == 2, "a loss seen by Observe alone starts the next match");
        FakeFriend u;  // Quill r3: A, 0, A before any row; then a delayed match-1 birth alone, then match 2
        u.display.Observe(0xA000);
        u.display.Observe(0);
        u.display.Observe(0xA000);
        u.display.Receive({rows[0]});
        u.Tick(0xA000);
        check(u.creates == 0 && u.display.Showing() == 0 && u.display.Unaligned() && u.display.Stats().refused == 1,
              "replaced before any row: a delayed old birth is not adopted as this manager's match");
        u.display.Receive({rows[3]});
        u.Tick(0xA000);
        check(u.creates == 0 && u.display.Queued() == 0 && u.display.Stats().refused == 2,
              "unaligned: the current match's rows are refused too, never queued forever");
        u.display.Reset();
        u.display.Receive({rows[3]});
        u.Tick(0xB000);
        check(!u.display.Unaligned() && u.display.Showing() == 2 && u.creates == 1, "the room's end clears it");
        FakeFriend first;  // a first manager with no rows yet is not a replacement
        first.display.Observe(0xA000);
        first.display.Receive({rows[3]});
        first.Tick(0xA000);
        check(!first.display.Unaligned() && first.creates == 1, "a first manager waiting for its first row stays aligned");
        FakeFriend h;  // the friend's manager replaced before the host's: late match 1 rows are dropped
        h.display.Receive({rows[0]});
        h.Tick(0x5000);
        h.Tick(0x6000);
        h.display.Receive({rows[1], rows[2], rows[3], rows[4]});
        h.Tick(0x6000);
        check(h.display.Showing() == 2 && h.display.Shown() == 2 && h.display.Stats().stale == 2,
              "rows of a match the friend's manager no longer shows are dropped");
    }
}

// MSVC requires external linkage for the thread-local map destructor.
} // namespace

// Counting fakes for the detours' environment.
struct FakeNative {
    static inline thread_local std::uint32_t thread = 0;
    static inline thread_local std::uintptr_t stack = 1;
    static inline thread_local unsigned reads = 0;
    static inline thread_local std::function<void()> readStep;
    static inline unsigned births = 0, collects = 0;
    static inline std::uint16_t lastNetId = 0;
    static inline std::uint8_t lastCollector = 0;
    static inline thread_local std::map<std::uintptr_t, std::uint64_t> memory;
    static inline const std::thread::id ownerThread = std::this_thread::get_id();
    static inline std::atomic<bool> wrongCallbackThread{false};
    static void Clear() {
        wrongCallbackThread = false;
        reads = births = collects = 0;
        lastNetId = 0; lastCollector = 0; memory.clear();
    }
    static std::uint32_t Thread() { return thread; }
    static std::uintptr_t Stack() { return stack; }
    static bool Read(std::uintptr_t a, void* out, std::size_t n) {
        ++reads;
        if (readStep) { auto f = std::move(readStep); f(); }
        if (n > sizeof(std::uint64_t)) { std::memset(out, 0, n); return true; }
        const auto it = memory.find(a);
        const std::uint64_t v = it == memory.end() ? 0 : it->second;
        std::memcpy(out, &v, n < sizeof(v) ? n : sizeof(v));
        return true;
    }
};

namespace {
const so::Callbacks kFakeCallbacks = [] {
    so::Callbacks c;
    c.hostBirth = [](std::uintptr_t, std::uintptr_t, std::uint8_t, const float*, const float*) {
        if (std::this_thread::get_id() != FakeNative::ownerThread) FakeNative::wrongCallbackThread = true;
        ++FakeNative::births; };
    c.hostCollect = [](std::uintptr_t, std::uintptr_t, std::uint8_t who, std::uint16_t id) {
        if (std::this_thread::get_id() != FakeNative::ownerThread) FakeNative::wrongCallbackThread = true;
        ++FakeNative::collects; FakeNative::lastCollector = who; FakeNative::lastNetId = id;
    };
    c.actorFor = [](std::uint8_t, std::uint16_t) -> std::uintptr_t { return 0; };
    return c;
}();
constexpr std::uintptr_t kBase = 0x140000000, kManager = 0x2AE0000, kRecord = 0x9000;

so::Scope OrbScope(so::Mode mode = so::Mode::Host) {
    return {11, 2, 3, 4, 55, kManager, mode};
}
std::vector<so::Collector> Collectors() {
    return {{0x111, so::kCollectorHost, 0}, {0x222, so::kCollectorFriend, 0}, {0x333, so::kCollectorEnemy, 3}};
}
so::adapter::Env Env() { return {kBase, &FakeNative::Thread, &FakeNative::Read, &kFakeCallbacks}; }
void Record(std::uintptr_t record = kRecord) {
    FakeNative::memory[kBase + so::kManagerRva] = kManager;
    FakeNative::memory[record + so::kRecordManager] = kManager;
    FakeNative::memory[record + so::kRecordDescriptor] = kBase + so::kDescriptorsRva;
    FakeNative::memory[record + so::kRecordUpdate] = 1;
    FakeNative::memory[record + so::kRecordCollector] = 0;
}
void AdapterGate() {
    namespace ad = so::adapter;
    auto env = Env();
    float point[4] {1, 2, 3, 1};
    // Inactive, unregistered and no-context entries never touch native memory/callbacks.
    for (int mode = 0; mode < 3; ++mode) {
        ad::Gate g; g.active.store(mode != 0); g.gameThread.store(mode == 2 ? 7 : 0);
        FakeNative::Clear(); FakeNative::thread = 9;
        unsigned originals = 0;
        const auto r = ad::Create(g, env, [&](std::uintptr_t, std::uintptr_t, const float*) { ++originals; return std::uintptr_t{0x4242}; },
                                  kManager, kBase + so::kDescriptorsRva, point);
        ad::Drop(g, env, [&] { ++originals; }); ad::Drop(g, env, [&] { ++originals; });
        ad::Pickup(g, env, [&] { ++originals; }, kRecord); ad::Pickup(g, env, [&] { ++originals; }, kRecord);
        check(r == 0x4242 && originals == 5 && FakeNative::reads == 0 && FakeNative::births == 0 && FakeNative::collects == 0,
              "unpublished: every detour calls native once without reads or callbacks");
        check(mode == 0 ? g.inactive == 5 && g.offThread == 0 : g.offThread == 5 && g.inactive == 0,
              "unpublished: inactive/foreign entries counted");
    }
    ad::Gate g; g.active = true; g.gameThread = 9;
    FakeNative::Clear(); FakeNative::thread = 17; Record();
    (void)g.Publish(9, OrbScope(so::Mode::Follow), {});
    unsigned originals = 0;
    ad::Drop(g, env, [&] { ++originals; });
    ad::Pickup(g, env, [&] { ++originals; }, kRecord);
    check(originals == 0 && g.dropsSuppressed == 1 && g.picksSuppressed == 1,
          "friend worker: cached policy suppresses drops/picks without a game-state callback");
    FakeNative::memory[kBase + so::kManagerRva] = kManager + 1;
    ad::Drop(g, env, [&] { ++originals; }); ad::Pickup(g, env, [&] { ++originals; }, kRecord);
    check(originals == 2, "stale manager: suppression never crosses a native manager replacement");
    Record();
    FakeNative::readStep = [&] { g.Reset(); };
    ad::Drop(g, env, [&] { ++originals; });
    check(originals == 3, "withdrawn during native read: old follow token cannot suppress a drop");
    (void)g.Publish(9, OrbScope(so::Mode::Follow), {});
    FakeNative::readStep = [&] { g.Reset(); };
    ad::Pickup(g, env, [&] { ++originals; }, kRecord);
    check(originals == 4, "withdrawn during native read: old follow token cannot suppress a pickup");
    (void)g.Publish(9, OrbScope(), Collectors());
    const auto create = [&] {
        return ad::Create(g, env, [&](std::uintptr_t m, std::uintptr_t d, const float* p) {
            ++originals;
            return ad::Init(g, env, [](std::uintptr_t r, std::uintptr_t, std::uintptr_t, const float*, std::uintptr_t) { return r; },
                            kRecord, m, d, p, 0); },
                          kManager, kBase + so::kDescriptorsRva, point);
    };
    check(create() == kRecord && FakeNative::births == 0 && g.queued == 1,
          "host worker: birth copies a receipt, no EnemySync callback");
    ad::Drain(g, env, g.Publish(9, OrbScope(), Collectors()));
    check(FakeNative::births == 1 && g.births == 1, "owner drain: birth callback once");
    const auto pick = [&](std::uintptr_t actor, bool switches) {
        Record();
        ad::Pickup(g, env, [&] { ++originals; if (switches) {
            ad::Flight(g, env, [&](std::uintptr_t r, std::uintptr_t a, int) {
                FakeNative::memory[r + so::kRecordUpdate] = 2;
                FakeNative::memory[r + so::kRecordCollector] = a;
            }, kRecord, actor, 0);
        } }, kRecord);
    };
    for (const auto actor : {0x111u, 0x222u, 0x333u, 0x444u}) {
        if (actor != 0x111) { (void)create(); ad::Drain(g, env, g.Publish(9, OrbScope(), Collectors())); }
        pick(actor, true);
        const auto before = FakeNative::collects;
        ad::Drain(g, env, g.Publish(9, OrbScope(), {})); // refreshed mapping must not rename captured collector
        check(FakeNative::collects == before + 1 && FakeNative::lastCollector ==
              (actor == 0x111 ? so::kCollectorHost : actor == 0x222 ? so::kCollectorFriend :
               actor == 0x333 ? so::kCollectorEnemy : so::kCollectorUnresolved), "pickup: captured collector survives refresh");
        (void)g.Publish(9, OrbScope(), Collectors());
    }
    check(FakeNative::lastNetId == 0 && g.unresolved == 1,
          "unmapped collector fades; worker never calls collectorOf");
    const auto queued = g.queued.load();
    pick(0x111, false); pick(0, true);
    check(g.queued == queued, "no flight or no collector: no collection");
    Record(); FakeNative::memory[kRecord + so::kRecordDescriptor] += 1;
    ad::Flight(g, env, [&](std::uintptr_t, std::uintptr_t, int) { ++originals; }, kRecord, 0x111, 0);
    check(g.queued == queued, "wrong descriptor: no collection");
    Record();
    check(throws([&] { ad::Pickup(g, env, [] { throw std::runtime_error("native unwind"); }, kRecord); }) && g.queued == queued,
          "native unwind: no receipt, no lock retained");
    (void)create(); ad::Drain(g, env, g.Publish(9, OrbScope(), Collectors()));
    const auto collected = FakeNative::collects;
    check(throws([&] { ad::Flight(g, env, [](std::uintptr_t, std::uintptr_t, int) {
        throw std::runtime_error("native flight unwind"); }, kRecord, 0x111, 0); }), "native flight unwind propagates");
    ad::Drain(g, env, g.Publish(9, OrbScope(), Collectors()));
    check(FakeNative::collects == collected && g.pending.empty(), "failed native flight neither emits a false collect nor stalls the prefix");

    env.stack = &FakeNative::Stack;
    const auto beforeFiber = g.queued.load();
    ad::Create(g, env, [&](std::uintptr_t m, std::uintptr_t d, const float* p) {
        FakeNative::stack = 2; // initializer on another native stack cannot borrow the outer TLS frame
        const auto r = ad::Init(g, env, [](std::uintptr_t r, std::uintptr_t, std::uintptr_t, const float*, std::uintptr_t) { return r; },
                               kRecord, m, d, p, 0);
        FakeNative::stack = 1; return r;
    }, kManager, kBase + so::kDescriptorsRva, point);
    check(g.queued == beforeFiber && !g.records.contains(kRecord),
          "another fiber's initializer cannot borrow this create call or leave the old incarnation current");
    env.stack = nullptr;

    // Every scope component fences queued receipts. An in-flight call returning after reset is stale.
    for (int field = 0; field < 7; ++field) {
        (void)create(); auto scope = OrbScope();
        switch(field) { case 0: ++scope.generation; break; case 1: ++scope.epoch; break;
            case 2: ++scope.transition; break; case 3: ++scope.load; break; case 4: ++scope.delivery; break;
            case 5: ++scope.manager; break; default: scope.mode = so::Mode::Follow; break; }
        check(g.Publish(9, scope, {}).empty(), "changed scope retires queued receipts");
        (void)g.Publish(9, OrbScope(), Collectors());
    }
    auto token = g.Capture(17); g.Reset(); (void)g.Publish(9, OrbScope(), Collectors());
    check(!g.ReserveBirth(token, kRecord, 0, {}) && g.stale >= 8, "reset fences an in-flight native call even at identical addresses");
    token = g.Capture(17);
    for (std::size_t i = 0; i < ad::Gate::kCapacity; ++i) {
        const auto receipt = g.ReserveBirth(token, kRecord + i * 0x80, 0, {});
        g.CompleteBirth(token, receipt, {}, true);
    }
    check(!g.ReserveBirth(token, kRecord + ad::Gate::kCapacity * 0x80, 0, {}) && g.overflow == 1, "bounded mailbox reports overflow");
    check(g.Publish(17, OrbScope(), {}).empty() && g.Publish(9, OrbScope(), {}).size() == ad::Gate::kCapacity,
          "worker cannot publish or steal owner receipts");
    for (std::size_t i = 0; i < static_cast<std::size_t>(ad::Site::Count); ++i) {
        const auto site = static_cast<ad::Site>(i);
        check(g.Trace(site, 17) && !g.Trace(site, 17) && g.Trace(site, 9), "each hook and NoteGameThread record TID changes");
    }
}

// Real native worker threads exercise the production adapter, sharing only Gate. Their fake native
// records are thread-local. Every callback stays on the owner, even while it publishes concurrently.
void WorkerMailbox() {
    namespace ad = so::adapter;
    ad::Gate g; g.active = true; g.gameThread = 9;
    (void)g.Publish(9, OrbScope(), Collectors());
    auto env = Env();
    FakeNative::Clear();
    std::atomic<bool> start{false};
    std::vector<std::thread> workers;
    for (unsigned w = 0; w < 3; ++w) workers.emplace_back([&, w] {
        FakeNative::thread = 17 + w;
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        for (unsigned i = 0; i < 100; ++i) {
            const auto record = kRecord + w * 0x10000 + i * 0x80;
            Record(record);
            float point[4] {1, 2, 3, 1};
            ad::Create(g, env, [&](std::uintptr_t m, std::uintptr_t d, const float* p) {
                return ad::Init(g, env, [](std::uintptr_t r, std::uintptr_t, std::uintptr_t, const float*, std::uintptr_t) { return r; },
                                record, m, d, p, 0); },
                       kManager, kBase + so::kDescriptorsRva, point);
            ad::Pickup(g, env, [&] {
                ad::Flight(g, env, [&](std::uintptr_t r, std::uintptr_t a, int) {
                    FakeNative::memory[r + so::kRecordUpdate] = 2;
                    FakeNative::memory[r + so::kRecordCollector] = a;
                }, record, 0x222, 0);
            }, record);
        }
    });
    start.store(true, std::memory_order_release);
    for (int i = 0; i < 100; ++i) ad::Drain(g, env, g.Publish(9, OrbScope(), Collectors()));
    for (auto& w : workers) w.join();
    ad::Drain(g, env, g.Publish(9, OrbScope(), Collectors()));
    check(FakeNative::births + g.elided == 300 && FakeNative::collects == FakeNative::births && g.queued == 600 && g.drained == 600 &&
          g.overflow == 0 && g.stale == 0 && g.offThread == 900, "three workers + concurrent owner: every incarnation drained or atomically elided once");
    check(!FakeNative::wrongCallbackThread, "all EnemySync callbacks executed on the owner thread");
}

// Installation with injected failures at every step, and detours called while it runs.
struct FakeOps {
    std::size_t failCreate = 99, failQueue = 99, failRemove = 99;
    bool failApply = false;
    std::vector<int> removed;
    unsigned created = 0, queued = 0;
    bool trampoline[7] {};  // a created hook's original trampoline; MH_RemoveHook frees it
    std::function<void()> duringApply;
    bool Create(std::size_t i) { if (i == failCreate) return false; ++created; trampoline[i] = true; return true; }
    bool Queue(std::size_t i) { if (i == failQueue) return false; ++queued; return true; }
    bool Apply() { if (duringApply) duringApply(); return !failApply; }
    bool Remove(std::size_t i) {
        removed.push_back(static_cast<int>(i));
        if (i == failRemove) return false;
        trampoline[i] = false;
        return true;
    }
};
void AdapterInstall() {
    namespace ad = so::adapter;
    ad::Env env;
    env.exeBase = kBase; env.thread = &FakeNative::Thread; env.read = &FakeNative::Read; env.cb = &kFakeCallbacks;
    for (std::size_t at = 0; at < 7; ++at) {
        ad::Gate g; g.gameThread.store(9); FakeOps ops; ops.failCreate = at;
        const auto r = ad::Install(g, 7, ops);
        check(!r.active && !g.active && std::string(r.failedAt) == "create" && r.created == at && ops.removed.size() == at &&
              r.rollbackFailures == 0, "install: a failed create " + std::to_string(at) + " removes the earlier ones; inactive");
    }
    for (std::size_t at = 0; at < 7; ++at) {
        ad::Gate g; FakeOps ops; ops.failQueue = at;
        const auto r = ad::Install(g, 7, ops);
        check(!r.active && !g.active && std::string(r.failedAt) == "queue" && ops.removed.size() == 7,
              "install: a failed queue " + std::to_string(at) + " removes all seven; inactive");
    }
    {
        ad::Gate g; g.gameThread.store(9); FakeOps ops; ops.failApply = true;
        unsigned originals = 0, freedCalls = 0;
        FakeNative::Clear(); FakeNative::thread = 9;
        // Quill r2: a thread enters the drop detour while Apply runs and pauses before its original call;
        // it resumes after Install has returned. The trampoline it calls must still exist.
        std::function<void()> paused;
        ops.duringApply = [&] {
            ad::Drop(g, env, [&] { ++originals; });  // passes straight through
            ad::Pickup(g, env, [&] { ++originals; }, kRecord);
            paused = [&] { if (ops.trampoline[1]) ++originals; else ++freedCalls; };
        };
        const auto r = ad::Install(g, 7, ops);
        check(!r.active && !g.active && std::string(r.failedAt) == "apply" && r.retained == 7 && ops.removed.empty() &&
              r.rollbackFailures == 0, "install: a failed apply keeps every hook, inactive, and removes none");
        check(originals == 2 && g.dropsSuppressed == 0 && g.picksSuppressed == 0 && FakeNative::reads == 0,
              "install: a detour reached while applying passes straight through");
        paused();
        bool all = true;
        for (bool t : ops.trampoline) all = all && t;
        check(originals == 3 && freedCalls == 0 && all,
              "install: a detour paused inside a failed apply resumes into a live trampoline");
        ad::Drop(g, env, [&] { ++originals; });
        check(originals == 4 && g.dropsSuppressed == 0, "install: after a failed apply the kept hooks pass through for good");
    }
    {
        ad::Gate g; FakeOps ops; ops.failQueue = 2; ops.failRemove = 3;
        const auto r = ad::Install(g, 7, ops);
        check(!r.active && r.retained == 0 && ops.removed.size() == 7 && r.rollbackFailures == 1 && ops.trampoline[3] && !ops.trampoline[0],
              "install: hooks never applied are reclaimed, and a removal that fails is counted");
    }
    {
        ad::Gate g; g.gameThread.store(9); FakeOps ops;
        bool openDuringApply = true;
        ops.duringApply = [&] { openDuringApply = g.active.load(); };
        const auto r = ad::Install(g, 7, ops);
        check(r.active && g.active && !openDuringApply && ops.created == 7 && ops.queued == 7 && ops.removed.empty(),
              "install: the gate opens only after all seven apply");
    }
}

// Both sides across A, 0, A at the same address (Quill r2), through the codec and the room's ledger.
void SameAddressReturn() {
    so::HostBook book;
    SpawnEmissionLedger ledger;
    FakeFriend f;
    std::uint64_t seq = 0;
    const auto send = [&](const SpawnEmission& row) { check(ledger.Add(Decode(encode(row))), "A0A: the ledger takes the row"); };
    auto o = book.Birth(0xA000, 0x10); send(Birth(++seq, o.id, 0, o.match));
    o = book.Birth(0xA000, 0x20); send(Birth(++seq, o.id, 0, o.match));
    f.display.Receive(ledger.TakeOrbs());
    f.Tick(0xA000);
    check(f.display.Shown() == 2, "A0A: match 1 shown on the friend");
    book.Observe(0); f.Tick(0);  // both games lose the manager
    check(!book.Collect(0xA000, 0x10).id, "A0A: the host collects nothing of the lost match");
    o = book.Birth(0xA000, 0x10); send(Birth(++seq, o.id, 0, o.match));  // the pool hands back the same record
    const auto taken = book.Collect(0xA000, 0x10);
    send(Collect(++seq, taken.id, OrbCollector::Friend, 0, taken.match));
    o = book.Birth(0xA000, 0x30); send(Birth(++seq, o.id, 0, o.match));
    f.display.Receive(ledger.TakeOrbs());
    f.Tick(0xA000);
    check(taken.match == 2 && f.display.Showing() == 2 && f.display.Shown() == 1 && f.flights.empty() && f.display.Stats().elided == 1,
          "A0A: the friend shows only the new match, and the reused record's old orb is never flown");
}

}  // namespace

int main() {
    Codec();
    Ledger();
    HostRules();
    FriendRules();
    Btl84Replay();
    ManagerReplacement();
    AdapterGate();
    WorkerMailbox();
    AdapterInstall();
    SameAddressReturn();
    std::printf("%d/%d checks PASS\n", checks - failed, checks);
    return failed ? 1 : 0;
}
