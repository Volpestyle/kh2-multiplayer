// VUH-2466: the Struggle orb rows (protocol 34) and the host-book / friend-display rules
// (inject/src/StruggleOrbs.hpp). No KH2: records are fake addresses.
#include "StruggleOrbs.hpp"
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

SpawnEmission Birth(std::uint64_t seq, std::uint16_t id, std::uint8_t type = 0) {
    SpawnEmission e{}; e.location = Room(); e.sequence = seq; e.kind = SpawnEmissionKind::OrbBirth; e.orbId = id; e.orbType = type;
    e.point = {-150, -90, 0, 1}; e.velocity = {0.5f, 12.0f, -0.25f};
    return e;
}
SpawnEmission Collect(std::uint64_t seq, std::uint16_t id, OrbCollector c = OrbCollector::Host, std::uint16_t netId = 0) {
    SpawnEmission e{}; e.location = Room(); e.sequence = seq; e.kind = SpawnEmissionKind::OrbCollect; e.orbId = id; e.collector = c;
    e.collectorNetId = netId;
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
          n == SPAWN_EMISSION_BYTES, "v34: an orb row is a 69-byte SpawnEmission");
    const auto back = Decode(bytes);
    check(back.kind == SpawnEmissionKind::OrbBirth && back.orbId == 9 && back.orbType == 2 && back.velocity == birth.velocity &&
          back.point == birth.point && encode(back) == bytes, "orb birth round trips");
    const auto take = Collect(6, 9, OrbCollector::Enemy, 1);
    const auto tback = Decode(encode(take));
    check(tback.kind == SpawnEmissionKind::OrbCollect && tback.collector == OrbCollector::Enemy && tback.collectorNetId == 1,
          "orb collection round trips with its enemy collector");
    check(validateScopedWorldPacket(bytes) == PacketType::SpawnEmission, "orb rows keep the material world-packet policy");
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
    auto unknown = encode(birth); unknown[3 + 50] = 3;  // kind byte
    check(throws([&] { Decode(unknown); }), "an unknown kind is refused");
    auto collector = encode(take); collector[3 + 66] = 4;  // collector byte
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
    check(!book.Birth(0, 0x10) && !book.Birth(0x1000, 0), "no manager or record, no ID");
    const auto a = book.Birth(0x1000, 0x10), b = book.Birth(0x1000, 0x20);
    check(a == 1 && b == 2 && book.LiveCount() == 2, "IDs count up per manager");
    check(book.Collect(0x1000, 0x20) == 2 && book.Collect(0x1000, 0x20) == 0, "a collection names the orb once");
    check(book.Collect(0x2000, 0x10) == 0, "another manager's record is not this book's orb");
    check(book.Birth(0x1000, 0x10) == 3 && book.LiveCount() == 1, "a record born again replaces its old orb");
    check(book.Birth(0x3000, 0x10) == 1 && book.LiveCount() == 1, "the next match's manager starts a new book");
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
            const auto id = book.Birth(manager, record);
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
            const auto id = book.Collect(manager, record);
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

}  // namespace

int main() {
    Codec();
    Ledger();
    HostRules();
    FriendRules();
    Btl84Replay();
    std::printf("%d/%d checks PASS\n", checks - failed, checks);
    return failed ? 1 : 0;
}
