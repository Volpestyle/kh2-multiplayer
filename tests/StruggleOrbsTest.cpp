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
    check(PROTOCOL_VERSION == 34 && decodePacketHeader(bytes.data(), bytes.size(), p, n) == PacketType::SpawnEmission &&
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

// Counting fakes for the detours' environment.
struct FakeNative {
    static inline std::uint32_t thread = 0;
    static inline unsigned reads = 0, live = 0, follows = 0, births = 0, collects = 0, collectorCalls = 0;
    static inline bool following = false, hosting = true;
    static inline std::uint8_t collector = so::kCollectorHost;
    static inline std::uint16_t netId = 0, lastNetId = 0;
    static inline std::uint8_t lastCollector = 0;
    static inline std::map<std::uintptr_t, std::uint64_t> memory;
    static void Clear() {
        reads = live = follows = births = collects = collectorCalls = 0; following = false; hosting = true;
        collector = so::kCollectorHost; netId = lastNetId = 0; lastCollector = 0; memory.clear();
    }
    static std::uint32_t Thread() { return thread; }
    static bool Read(std::uintptr_t a, void* out, std::size_t n) {
        ++reads;
        const auto it = memory.find(a);
        const std::uint64_t v = it == memory.end() ? 0 : it->second;
        std::memcpy(out, &v, n < sizeof(v) ? n : sizeof(v));
        return true;
    }
};
const so::Callbacks kFakeCallbacks = [] {
    so::Callbacks c;
    c.hostLive = [] { ++FakeNative::live; return FakeNative::hosting; };
    c.clientFollow = [] { ++FakeNative::follows; return FakeNative::following; };
    c.hostBirth = [](std::uintptr_t, std::uintptr_t, std::uint8_t, const float*, const float*) { ++FakeNative::births; };
    c.hostCollect = [](std::uintptr_t, std::uintptr_t, std::uint8_t who, std::uint16_t id) {
        ++FakeNative::collects; FakeNative::lastCollector = who; FakeNative::lastNetId = id;
    };
    c.collectorOf = [](std::uintptr_t, std::uint16_t& id) { ++FakeNative::collectorCalls; id = FakeNative::netId; return FakeNative::collector; };
    c.actorFor = [](std::uint8_t, std::uint16_t) -> std::uintptr_t { return 0; };
    return c;
}();
constexpr std::uintptr_t kBase = 0x140000000, kManager = 0x2AE0000, kRecord = 0x9000;

void AdapterGate() {
    namespace ad = so::adapter;
    ad::Env env;
    env.exeBase = kBase; env.thread = &FakeNative::Thread; env.read = &FakeNative::Read; env.cb = &kFakeCallbacks;
    float point[4] {1, 2, 3, 1};
    // Every detour body, refused three ways: inactive, no game thread noted yet, a foreign thread.
    for (int mode = 0; mode < 3; ++mode) {
        ad::Gate g;
        g.active.store(mode != 0);
        g.gameThread.store(mode == 2 ? 7 : 0);
        FakeNative::Clear(); FakeNative::thread = 9; FakeNative::following = true;
        unsigned originals = 0;
        const auto r = ad::Create(g, env, [&](std::uintptr_t, std::uintptr_t, const float*) { ++originals; return std::uintptr_t{0x4242}; },
                                  kManager, kBase + so::kDescriptorsRva, point);
        ad::Drop(g, env, [&] { ++originals; });
        ad::Drop(g, env, [&] { ++originals; });
        ad::Pickup(g, env, [&] { ++originals; }, kRecord);
        ad::Pickup(g, env, [&] { ++originals; }, kRecord);
        const char* why[] = {"inactive", "unregistered", "foreign-thread"};
        check(r == 0x4242 && originals == 5 && FakeNative::reads == 0 && FakeNative::live == 0 && FakeNative::follows == 0 &&
              FakeNative::births == 0 && FakeNative::collects == 0 && g.dropsSuppressed == 0 && g.picksSuppressed == 0,
              std::string("gate ") + why[mode] + ": each of the five detours calls its original once, reads nothing, calls nothing");
        check(mode == 0 ? g.inactive == 5 && g.offThread == 0 : g.offThread == 5 && g.inactive == 0,
              std::string("gate ") + why[mode] + ": counted once per refused call");
    }
    ad::Gate g;
    g.active.store(true); g.gameThread.store(9);
    FakeNative::Clear(); FakeNative::thread = 9;
    {   // Friend: drops and pickups are skipped, the allocator still runs.
        FakeNative::following = true; FakeNative::hosting = false;
        unsigned originals = 0;
        ad::Drop(g, env, [&] { ++originals; });
        ad::Pickup(g, env, [&] { ++originals; }, kRecord);
        check(originals == 0 && g.dropsSuppressed == 1 && g.picksSuppressed == 1 && FakeNative::reads == 0,
              "friend: its own drop and pickup are skipped without a native read");
        const auto r = ad::Create(g, env, [&](std::uintptr_t, std::uintptr_t, const float*) { ++originals; return std::uintptr_t{kRecord}; },
                                  kManager, kBase + so::kDescriptorsRva, point);
        check(r == kRecord && originals == 1 && FakeNative::births == 0, "friend: the allocator runs and books nothing");
    }
    {   // Host: a birth at the current manager is booked; another manager's or a non-orb descriptor's is not.
        FakeNative::Clear(); FakeNative::thread = 9; FakeNative::memory[kBase + so::kManagerRva] = kManager;
        const auto create = [&](std::uintptr_t manager, std::uintptr_t descriptor) {
            return ad::Create(g, env, [](std::uintptr_t, std::uintptr_t, const float*) { return std::uintptr_t{kRecord}; },
                              manager, descriptor, point);
        };
        (void)create(kManager, kBase + so::kDescriptorsRva + 0x10);
        (void)create(kManager + 8, kBase + so::kDescriptorsRva);
        (void)create(kManager, kBase + so::kDescriptorsRva + 0x40);
        check(FakeNative::births == 1, "host: only an orb descriptor at the current manager is a birth");
    }
    {   // Host pickups: a switch to the flight is a collection, with its collector or Unresolved.
        const auto pick = [&](std::uint8_t who, std::uint16_t id, bool switches) {
            FakeNative::Clear(); FakeNative::thread = 9; FakeNative::collector = who; FakeNative::netId = id;
            FakeNative::memory[kBase + so::kManagerRva] = kManager;
            FakeNative::memory[kRecord + so::kRecordManager] = kManager;
            FakeNative::memory[kRecord + so::kRecordUpdate] = 1;
            FakeNative::memory[kRecord + so::kRecordCollector] = 0x7777;
            unsigned originals = 0;
            ad::Pickup(g, env, [&] { ++originals; if (switches) FakeNative::memory[kRecord + so::kRecordUpdate] = 2; }, kRecord);
            return originals;
        };
        check(pick(so::kCollectorFriend, 0, true) == 1 && FakeNative::collects == 1 && FakeNative::lastCollector == so::kCollectorFriend,
              "host: the friend's clone took it");
        check(pick(so::kCollectorEnemy, 3, true) == 1 && FakeNative::lastCollector == so::kCollectorEnemy && FakeNative::lastNetId == 3,
              "host: an enemy took it, by netId");
        const auto before = g.unresolved.load();
        check(pick(so::kCollectorNone, 0, true) == 1 && FakeNative::collects == 1 && FakeNative::lastCollector == so::kCollectorUnresolved,
              "host: a collector it cannot name is still a collection, unresolved");
        check(pick(so::kCollectorEnemy, 0, true) == 1 && FakeNative::lastCollector == so::kCollectorUnresolved &&
              FakeNative::lastNetId == 0 && g.unresolved.load() == before + 2, "host: an enemy without a netId is unresolved");
        check(pick(so::kCollectorHost, 0, false) == 1 && FakeNative::collects == 0, "host: no switch, no collection");
    }
}

// Installation with injected failures at every step, and detours called while it runs.
struct FakeOps {
    std::size_t failCreate = 99, failQueue = 99, failRemove = 99;
    bool failApply = false;
    std::vector<int> removed;
    unsigned created = 0, queued = 0;
    std::function<void()> duringApply;
    bool Create(std::size_t i) { if (i == failCreate) return false; ++created; return true; }
    bool Queue(std::size_t i) { if (i == failQueue) return false; ++queued; return true; }
    bool Apply() { if (duringApply) duringApply(); return !failApply; }
    bool Remove(std::size_t i) { removed.push_back(static_cast<int>(i)); return i != failRemove; }
};
void AdapterInstall() {
    namespace ad = so::adapter;
    ad::Env env;
    env.exeBase = kBase; env.thread = &FakeNative::Thread; env.read = &FakeNative::Read; env.cb = &kFakeCallbacks;
    for (std::size_t at = 0; at < 5; ++at) {
        ad::Gate g; g.gameThread.store(9); FakeOps ops; ops.failCreate = at;
        const auto r = ad::Install(g, 5, ops);
        check(!r.active && !g.active && std::string(r.failedAt) == "create" && r.created == at && ops.removed.size() == at &&
              r.rollbackFailures == 0, "install: a failed create " + std::to_string(at) + " removes the earlier ones; inactive");
    }
    for (std::size_t at = 0; at < 5; ++at) {
        ad::Gate g; FakeOps ops; ops.failQueue = at;
        const auto r = ad::Install(g, 5, ops);
        check(!r.active && !g.active && std::string(r.failedAt) == "queue" && ops.removed.size() == 5,
              "install: a failed queue " + std::to_string(at) + " removes all five; inactive");
    }
    {
        ad::Gate g; g.gameThread.store(9); FakeOps ops; ops.failApply = true; ops.failRemove = 2;
        unsigned originals = 0;
        FakeNative::Clear(); FakeNative::thread = 9; FakeNative::following = true;
        ops.duringApply = [&] {  // a partly applied set: a detour runs on the game thread
            ad::Drop(g, env, [&] { ++originals; });
            ad::Pickup(g, env, [&] { ++originals; }, kRecord);
        };
        const auto r = ad::Install(g, 5, ops);
        check(!r.active && !g.active && std::string(r.failedAt) == "apply" && r.rollbackFailures == 1 && ops.removed.size() == 5,
              "install: a failed apply keeps the gate shut and counts the removal that failed");
        check(originals == 2 && g.dropsSuppressed == 0 && g.picksSuppressed == 0 && FakeNative::reads == 0 && FakeNative::follows == 0,
              "install: a detour reached while applying passes straight through");
    }
    {
        ad::Gate g; g.gameThread.store(9); FakeOps ops;
        bool openDuringApply = true;
        ops.duringApply = [&] { openDuringApply = g.active.load(); };
        const auto r = ad::Install(g, 5, ops);
        check(r.active && g.active && !openDuringApply && ops.created == 5 && ops.queued == 5 && ops.removed.empty(),
              "install: the gate opens only after all five apply");
    }
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
    AdapterInstall();
    std::printf("%d/%d checks PASS\n", checks - failed, checks);
    return failed ? 1 : 0;
}
