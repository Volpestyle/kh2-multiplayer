// VUH-2464 prologue seats: SeatState codec, the pure seat/hold policy, the checked Tram Common
// missing-party site, and the relay over real loopback ENet (any-seat authorship, owner stamping,
// roster pinning, sequence floor and echo to the author).
#include "kh2coop/NetworkClient.hpp"
#include "kh2coop/BlockedExitPolicy.hpp"
#include "kh2coop/EventScenes.hpp"
#include "kh2coop/PrologueSeats.hpp"
#include "kh2coop/RouteSpawnPolicy.hpp"
#include "kh2coop/SessionHost.hpp"
#include <enet/enet.h>
#include <chrono>
#include <iostream>
#include <memory>
#include <string_view>
#include <thread>
#include <vector>
using namespace kh2coop;
namespace {
int failures = 0;
void check(bool b, const char* s) { std::cout << (b ? "PASS " : "FAIL ") << s << '\n'; if (!b) ++failures; }

SeatState seat(const std::array<std::uint64_t, 3>& roster, std::uint32_t seq, std::uint16_t profile,
               std::array<std::uint16_t, 3> latched = {}, SeatPhase phase = SeatPhase::Declared,
               std::uint8_t owner = 0xFF, prologue::Location at = {}) {
    SeatState m;
    m.roster = roster; m.seq = seq; m.profile = profile; m.latched = latched; m.phase = phase; m.ownerSlot = owner;
    if (phase == SeatPhase::Bound) { m.worldId = at.world; m.roomId = at.room; m.eventProgram = at.evt; }
    return m;
}

void codec() {
    const auto m = seat({11, 12, 0}, 7, 0x5A, {0x5A, 0x5A, 0}, SeatPhase::Bound, 1, {2, 7, 95});
    const auto bytes = encode(m);
    const std::uint8_t* p = nullptr; std::size_t n = 0;
    check(decodePacketHeader(bytes.data(), bytes.size(), p, n) == PacketType::SeatState && n == SEAT_STATE_BYTES &&
          static_cast<unsigned>(PacketType::SeatState) == 53 && PROTOCOL_VERSION == 30, "SeatState is type 53, 72 bytes, protocol 30");
    ByteReader r(p, n); SeatState back; read(r, back);
    check(r.atEnd() && encode(back) == bytes && back.roster == m.roster && back.seq == 7 && back.profile == 0x5A &&
          back.latched == m.latched && back.phase == SeatPhase::Bound && back.ownerSlot == 1 && back.roomId == 7 &&
          back.eventProgram == 95, "exact roundtrip");
    check(isWorldPacket(PacketType::SeatState) && isScopedWorldPacket(PacketType::SeatState) &&
          !isMaterialWorldPacket(PacketType::SeatState), "scoped, non-material world packet");
    bool threw = false;
    try { auto bad = m; bad.phase = static_cast<SeatPhase>(3); (void)encode(bad); } catch (const std::exception&) { threw = true; }
    check(threw, "encoder refuses an unknown phase");
    threw = false;
    try { auto bad = m; bad.ownerSlot = 3; (void)encode(bad); } catch (const std::exception&) { threw = true; }
    check(threw, "encoder refuses an owner slot outside 0..2 (0xFF = unstamped)");
    auto longer = bytes; longer.push_back(0); threw = false;
    try { decodePacketHeader(longer.data(), longer.size(), p, n); } catch (const std::exception&) { threw = true; }
    check(threw, "trailing frame byte refused");
    // VUH-2555: StageEnd (54), a friend's own start of a host-led stage end.
    StageEnd stage; stage.seq = 3; stage.worldId = 2; stage.roomId = 4; stage.eventProgram = 77; stage.id = 0x313131; stage.flags = 9;
    stage.params = {2, 0x402, 0x4E004E, 0x4E, 1, 0xFFFFFFFF, 3, 0, 0};
    const auto stageBytes = encode(stage);
    check(decodePacketHeader(stageBytes.data(), stageBytes.size(), p, n) == PacketType::StageEnd && n == STAGE_END_PAYLOAD_SIZE &&
          static_cast<unsigned>(PacketType::StageEnd) == 54, "StageEnd is type 54, 52 bytes");
    ByteReader sr(p, n); StageEnd stageBack; read(sr, stageBack);
    check(sr.atEnd() && encode(stageBack) == stageBytes && stageBack.params == stage.params && stageBack.ownerSlot == 0xFF &&
          isScopedWorldPacket(PacketType::StageEnd) && !isMaterialWorldPacket(PacketType::StageEnd), "StageEnd exact roundtrip, scoped");
    threw = false;
    try { auto bad = stage; bad.seq = 0; (void)encode(bad); } catch (const std::exception&) { threw = true; }
    check(threw, "StageEnd refuses sequence 0");
    // The Day 1 Sandlot duel's two stage ends are host-led rows; the shop events are not.
    using namespace kh2coop::eventscenes;
    const auto* s1 = QualifiedStageEnd(2, 4, 77, 0x313131, 9, stage.params);
    const auto* s2 = QualifiedStageEnd(2, 4, 78, 0x323131, 8, {2, 0xD02, 0x7E007E, 0x7E, 1, 0xFFFFFFFF, 3, 0, 0});
    check(s1 && s1->hostLed && s2 && s2->hostLed && HostLedAt(2, 4, 77) == s1 && HostLedAt(2, 4, 78) == s2 &&
          !HostLedAt(2, 7, 95) && !HostLedAt(2, 4, 76), "the Sandlot duel's stage ends are host-led rows, nothing else");
    check(!QualifiedStart(2, 4, 77, 0x313131, 9, stage.params) && !QualifiedStart(2, 4, 78, 0x323131, 8, s2->params),
          "a host-led stage end is not an in-field event the host publishes for the friend to replay");
}

// VUH-2486: an unchecked prologue field before the first release holds only for a bounded,
// continuous stay at one location, then plays native solo with a note, once.
void unpinned() {
    using U = prologue::UnpinnedStart;
    const std::array<std::uint8_t, 10> basement {2, 0x15, 0, 0, 0, 0, 0, 0, 0, 0}, other {2, 0x13, 0, 0, 0, 0, 0, 0, 0, 0};
    U u;
    check(u.Tick(false, basement, 1000) == U::Step::Played && !u.Released(), "not unpinned: nothing held");
    check(u.Tick(true, basement, 1000) == U::Step::Hold, "an unchecked field holds at first");
    check(u.Tick(true, basement, 1000 + prologue::UnpinnedGraceMs - 1) == U::Step::Hold, "held until the grace ends");
    check(u.Tick(true, other, 1000 + prologue::UnpinnedGraceMs) == U::Step::Hold, "a new location restarts the wait");
    check(u.Tick(false, other, 5000) == U::Step::Played && u.Tick(true, other, 5001) == U::Step::Hold,
          "a transient (left before the grace) restarts the wait");
    check(u.Tick(true, other, 5001 + prologue::UnpinnedGraceMs) == U::Step::Released && u.Released(),
          "a continuous stay releases once");
    check(u.NoticeVisible(5001 + prologue::UnpinnedGraceMs) &&
          !u.NoticeVisible(5001 + prologue::UnpinnedGraceMs + prologue::UnpinnedNoticeMs), "the note shows for a while");
    check(u.Tick(true, basement, 99999) == U::Step::Played, "never held again after the release");
}

void blockedExit() {
    using namespace kh2coop::blockedexit;
    // VUH-2555, run 20261020-082907: in the Day 1 Sandlot duel the friend's own script ran ahead and asked
    // for the duel's btl 81 (the Sandlot, 02/04 door 0); its exit was blocked and it waited. The host's
    // battle then ends and it announces btl 78, the same room and door: the friend takes it (Matching).
    const Room sandlot81 {2, 4, 0}, sandlot78 {2, 4, 0};
    check(FromBlockedExit(true, sandlot81, sandlot78, false, true) == Issue::Matching,
          "a friend whose duel script asked for btl 81 takes the host's btl 78 in the same room");
    // A script that ran ahead into another room (the host announces Tram Common) still yields in the prologue.
    const Room tramCommon {2, 7, 0}, otherDoor {2, 4, 1};
    check(FromBlockedExit(true, sandlot81, tramCommon, false, true) == Issue::PrologueSuperseded &&
          FromBlockedExit(true, otherDoor, sandlot78, false, true) == Issue::PrologueSuperseded,
          "in the TT prologue the friend takes the host's room in place of another room its script asked for");
    check(FromBlockedExit(true, sandlot81, tramCommon, false, false) == Issue::No &&
          FromBlockedExit(true, Room {4, 1, 0}, Room {4, 2, 0}, false, true) == Issue::No,
          "outside the prologue, or outside TT, a different room waits for the safe field gate");
    check(FromBlockedExit(false, sandlot81, sandlot78, true, true) == Issue::No,
          "an exit blocked at an earlier load never issues");
    check(FromBlockedExit(true, Room {2, 1, 0}, tramCommon, true, false) == Issue::PrologueSite,
          "a title-joined friend takes the host's first checked prologue site (VUH-2464)");
}

void soloWait() {
    using S = prologue::SoloWait;
    S w;
    check(w.Tick(false, true, 1000) == S::Step::Off && w.Tick(true, false, 2000) == S::Step::Waiting &&
          w.SecondsLeft(2000) == 60, "a seat left alone waits, showing the seconds left");
    check(w.Tick(true, false, 2000 + prologue::SoloWaitMs - 1) == S::Step::Waiting &&
          w.Tick(true, false, 2000 + prologue::SoloWaitMs) == S::Step::Released && w.SecondsLeft(70000) == 0,
          "after 60 s it plays on solo");
    check(w.Tick(false, false, 80000) == S::Step::Released && w.Tick(true, false, 90000) == S::Step::Released,
          "the next checked site does not wait again while still alone");
    check(w.Tick(false, true, 100000) == S::Step::Off && w.Tick(true, false, 110000) == S::Step::Waiting,
          "a peer that came back and left again is waited for again");
}

void policy() {
    using prologue::Hold;
    const std::array<std::uint64_t, 3> two{11, 12, 0};
    prologue::Seats s;
    check(prologue::Agreement(s) == Hold::NoSession, "no generation: no session");
    check(prologue::Rebase(s, 3, {11, 0, 0}, 0) && prologue::Agreement(s) == Hold::Solo, "one connected seat holds as solo");
    check(prologue::Rebase(s, 3, {11, 12, 13}, 0) && prologue::Agreement(s) == Hold::TooMany, "three seats are outside the prologue layout");
    check(prologue::Rebase(s, 4, two, 0) && !prologue::Rebase(s, 4, two, 0), "rebase only on a new key");
    check(prologue::Agreement(s) == Hold::MissingProfile, "undeclared seats: missing profile");
    check(prologue::Accept(s, seat(two, 1, 0x5A, {}, SeatPhase::Declared, 0)), "host declares Roxas");
    check(!prologue::Accept(s, seat(two, 1, 0x5A, {}, SeatPhase::Declared, 0)), "repeated sequence ignored");
    check(!prologue::Accept(s, seat({11, 99, 0}, 2, 0x5A, {}, SeatPhase::Declared, 1)), "another roster refused");
    check(!prologue::Accept(s, seat(two, 2, 0x5A, {}, SeatPhase::Declared, 2)), "an empty slot cannot report");
    check(prologue::Agreement(s) == Hold::MissingProfile, "one seat still undeclared");
    check(prologue::Accept(s, seat(two, 1, 0x5B, {}, SeatPhase::Declared, 1)) && prologue::Agreement(s) == Hold::UnqualifiedProfile,
          "a Mickey friend is not admitted to the prologue layout: both seats hold");
    check(prologue::RefusedProfile(s) == 0x5B && std::string_view(prologue::ProfileDisplayName(0x5B)) == "Mickey",
          "the on-screen note names the refused Mickey");
    check(prologue::Accept(s, seat(two, 2, 0x5A, {}, SeatPhase::Declared, 1)) && prologue::Agreement(s) == Hold::NotAcknowledged,
          "declared Roxas/Roxas still needs both acknowledgements");
    check(prologue::RefusedProfile(s) == 0, "no note once every declared profile is admitted");
    std::array<std::uint16_t, 3> v{};
    check(prologue::Vector(s, v) && v == std::array<std::uint16_t, 3>{0x5A, 0x5A, 0}, "profile vector");
    std::uint16_t profile = 0; Hold why = Hold::None;
    check(!prologue::RemoteProfile(s, profile, why) && why == Hold::NotAcknowledged, "no clone before acknowledgement");
    prologue::Accept(s, seat(two, 2, 0x5A, v, SeatPhase::Acknowledged, 0));
    check(prologue::Agreement(s) == Hold::NotAcknowledged, "the friend has not acknowledged");
    prologue::Accept(s, seat(two, 3, 0x5A, {0x5A, 0x54, 0}, SeatPhase::Acknowledged, 1));
    check(prologue::Agreement(s) == Hold::NotAcknowledged, "an acknowledgement of a different vector does not count");
    prologue::Accept(s, seat(two, 4, 0x5A, v, SeatPhase::Acknowledged, 1));
    check(prologue::Agreement(s) == Hold::None && prologue::RemoteProfile(s, profile, why) && profile == 0x5A,
          "agreed: the host's clone is the friend's Roxas");
    const prologue::Location tram{2, 7, 95}, tram97{2, 7, 97};
    check(prologue::Release(s, tram, false) == Hold::LocalUnbound, "own construction must bind first");
    check(prologue::Release(s, tram, true) == Hold::RemoteUnbound, "and the friend's");
    prologue::Accept(s, seat(two, 5, 0x5A, v, SeatPhase::Bound, 1, tram97));
    check(prologue::Release(s, tram, true) == Hold::RemoteUnbound, "a binding at another location does not release");
    prologue::Accept(s, seat(two, 6, 0x5A, v, SeatPhase::Bound, 1, tram));
    check(prologue::Release(s, tram, true) == Hold::None, "released when both seats bound at the same location");
    check(prologue::Release(s, tram97, true) == Hold::RemoteUnbound, "the next same-room reload holds again until rebound");
    check(prologue::Rebase(s, 5, two, 0) && prologue::Agreement(s) == Hold::MissingProfile, "a new session generation forgets every seat");
    // VUH-1512: a Roxas host and a Sora friend, from each seat's view.
    const std::array<std::uint16_t, 3> rs{0x5A, 0x54, 0};
    for (std::uint8_t local = 0; local < 2; ++local) {
        prologue::Seats p;
        prologue::Rebase(p, 7, two, local);
        prologue::Accept(p, seat(two, 1, 0x5A, rs, SeatPhase::Acknowledged, 0));
        prologue::Accept(p, seat(two, 1, 0x54, rs, SeatPhase::Acknowledged, 1));
        std::uint16_t mine = 0, theirs = 0; Hold a = Hold::None, b = Hold::None;
        check(prologue::Agreement(p) == Hold::None && prologue::LocalProfile(p, mine, a) && prologue::RemoteProfile(p, theirs, b) &&
              mine == rs[local] && theirs == rs[1 - local],
              local ? "the Sora friend builds itself as Sora and the host as Roxas" : "the Roxas host builds the friend's clone as Sora");
    }
    {
        prologue::Seats p;
        prologue::Rebase(p, 9, two, 1);
        prologue::Accept(p, seat(two, 1, 0x5A, {0x5A, 0x5A, 0}, SeatPhase::Acknowledged, 0));
        prologue::Accept(p, seat(two, 1, 0x5A, {0x5A, 0x5A, 0}, SeatPhase::Acknowledged, 1));
        std::uint8_t owner = 9; prologue::SeatColor c {};
        check(prologue::RemoteSlot(p, owner) && owner == 0 && prologue::DuplicateColor(0x5A, 0x5A, owner, c) &&
              c == prologue::SeatColors[0], "a friend's clone of the host's Roxas wears the host's seat color");
        check(!prologue::DuplicateColor(0x54, 0x5A, 0, c) && !prologue::DuplicateColor(0x5A, 0x5A, 3, c),
              "a clone of a different body keeps its own colors");
        check(prologue::SeatColors[0] != prologue::SeatColors[1] && prologue::SeatColors[1] != prologue::SeatColors[2],
              "every seat's color is distinct");
    }
    prologue::Seats q;
    prologue::Rebase(q, 8, two, 1);
    std::uint16_t none = 1; Hold held = Hold::None;
    check(!prologue::LocalProfile(q, none, held) && none == 0 && held == Hold::MissingProfile, "no local body before agreement");
}

void site() {
    using namespace routespawn;
    const auto* tram = MissingPartySiteFor(TramCommon95Location, 808738658, 0x3FF727, TramCommon95Header, TramCommon95Records);
    check(tram && tram->prologue && tram->localObject == 90 && tram->localKey == 14 && TramCommon95Records.size() == 3 * 64 &&
          TramCommon95Header[4] == 3, "Tram Common evt95 is a checked prologue site (b_40 group88, three records)");
    const auto* bailey = MissingPartySiteFor(BaileyLocation, 808738658, 0x3FF727, BaileyHeader, BaileyRecords);
    check(bailey && !bailey->prologue && bailey->localObject == 84 && bailey->localKey == 1, "Bailey is unchanged: Sora local and clone");
    check(!MissingPartySiteFor(TramCommon95Location, 808738658, 0x3FF728, TramCommon95Header, TramCommon95Records), "exact caller only");
    check(!MissingPartySiteFor(TramCommon95Location, 808738659, 0x3FF727, TramCommon95Header, TramCommon95Records), "exact package only");
    check(!MissingPartySiteFor(BaileyLocation, 808738658, 0x3FF727, TramCommon95Header, TramCommon95Records), "pins never cross sites");
    bool everyBit = true;
    for (std::size_t i = 0; i < 10; ++i) {
        auto at = TramCommon95Location; at[i] ^= 1;
        everyBit = everyBit && !MissingPartySiteFor(at, 808738658, 0x3FF727, TramCommon95Header, TramCommon95Records);
    }
    for (std::size_t i = 0; i < TramCommon95Header.size(); ++i) {
        auto h = TramCommon95Header; h[i] ^= 1;
        everyBit = everyBit && !MissingPartySiteFor(TramCommon95Location, 808738658, 0x3FF727, h, TramCommon95Records);
    }
    for (std::size_t i = 0; i < TramCommon95Records.size(); ++i) {
        auto r = TramCommon95Records; r[i] ^= 1;
        everyBit = everyBit && !MissingPartySiteFor(TramCommon95Location, 808738658, 0x3FF727, TramCommon95Header, r);
    }
    check(everyBit, "every changed location, header or record bit refuses the site");
    check(!MissingPartySiteFor(TramCommon95Location, 808738658, 0x3FF727, TramCommon95Header,
                               std::span(TramCommon95Records).first(128)), "a shorter group refuses");
    // The same-room reloads after events 104 and 106: their own packages and groups, never another's.
    const auto* tram97 = MissingPartySiteFor(TramCommon97Location, 842293090, 0x3FF727, TramCommon97Header, TramCommon97Records);
    const auto* tram99 = MissingPartySiteFor(TramCommon99Location, 875847522, 0x3FF727, TramCommon99Header, TramCommon99Records);
    check(tram97 && tram97->prologue && tram97->localKey == 14 && tram99 && tram99->prologue && tram99->localKey == 14,
          "Tram Common evt97 (b_42 group132) and evt99 (b_44 group176) are checked prologue sites");
    check(!MissingPartySiteFor(TramCommon97Location, 808738658, 0x3FF727, TramCommon95Header, TramCommon95Records) &&
          !MissingPartySiteFor(TramCommon99Location, 842293090, 0x3FF727, TramCommon97Header, TramCommon97Records) &&
          !MissingPartySiteFor(TramCommon95Location, 875847522, 0x3FF727, TramCommon99Header, TramCommon99Records),
          "a reload's location never admits another program's package or group");
    bool reloadBits = true;
    for (std::size_t i = 0; i < TramCommon97Records.size(); ++i) {
        auto a = TramCommon97Records; a[i] ^= 1;
        auto b = TramCommon99Records; b[i] ^= 1;
        reloadBits = reloadBits && !MissingPartySiteFor(TramCommon97Location, 842293090, 0x3FF727, TramCommon97Header, a) &&
            !MissingPartySiteFor(TramCommon99Location, 875847522, 0x3FF727, TramCommon99Header, b);
    }
    check(reloadBits, "every changed reload record bit refuses its site");
    check(MissingPartySiteAt(TramCommon97Location) == tram97 && MissingPartySiteAt(TramCommon99Location) == tram99 &&
          !MissingPartySiteAt({2, 7, 0, 0, 0x62, 0, 0x62, 0, 0x62, 0}), "site lookup by NOW is exact");
    // The first room change (event 108 to the Sandlot) reuses b_40's key with its own group.
    const auto* sandlot = MissingPartySiteFor(Sandlot76Location, 808738658, 0x3FF727, Sandlot76Header, Sandlot76Records);
    check(sandlot && sandlot->prologue && sandlot->localObject == 90 && sandlot->localKey == 14 &&
          MissingPartySiteAt(Sandlot76Location) == sandlot,
          "the Sandlot evt76 (b_40 group49) is a checked prologue site");
    check(!MissingPartySiteFor(Sandlot76Location, 808738658, 0x3FF727, TramCommon95Header, TramCommon95Records) &&
          !MissingPartySiteFor(TramCommon95Location, 808738658, 0x3FF727, Sandlot76Header, Sandlot76Records),
          "the same package key never admits the other room's group");
    bool sandlotBits = true;
    for (std::size_t i = 0; i < Sandlot76Records.size(); ++i) {
        auto r = Sandlot76Records; r[i] ^= 1;
        sandlotBits = sandlotBits && !MissingPartySiteFor(Sandlot76Location, 808738658, 0x3FF727, Sandlot76Header, r);
    }
    check(sandlotBits, "every changed Sandlot record bit refuses its site");
    // VUH-2466: the Mansion Dusk ambush in the Basement Hall (b_40 group50).
    const auto* hall = MissingPartySiteFor(BasementHall134Location, 808738658, 0x3FF727, BasementHall134Header,
                                           BasementHall134Records);
    check(hall && hall->prologue && hall->localObject == 90 && hall->localKey == 14 &&
          MissingPartySiteAt(BasementHall134Location) == hall && std::string_view(hall->name) == "tt19-134",
          "the Basement Hall evt134 (b_40 group50) is a checked prologue site");
    check(!MissingPartySiteFor(BasementHall134Location, 808738658, 0x3FF727, Sandlot76Header, Sandlot76Records) &&
          !MissingPartySiteFor(Sandlot76Location, 808738658, 0x3FF727, BasementHall134Header, BasementHall134Records) &&
          !MissingPartySiteAt({2, 0x13, 0, 0, 0, 0, 0, 0, 3, 0}),
          "the hall's site needs its own group and battle program");
    bool hallBits = true;
    for (std::size_t i = 0; i < BasementHall134Records.size(); ++i) {
        auto r = BasementHall134Records; r[i] ^= 1;
        hallBits = hallBits && !MissingPartySiteFor(BasementHall134Location, 808738658, 0x3FF727, BasementHall134Header, r);
    }
    check(hallBits, "every changed hall record bit refuses its site");
    // VUH-2466: the Day-6 Back Alley Dusk ambush (evt73, headerId 90).
    const auto* alley = MissingPartySiteFor(BackAlley73Location, 808738658, 0x3FF727, BackAlley73Header, BackAlley73Records);
    check(alley && alley->prologue && alley->localObject == 90 && alley->localKey == 14 &&
          MissingPartySiteAt(BackAlley73Location) == alley && std::string_view(alley->name) == "tt03-73" &&
          !MissingPartySiteFor(BackAlley73Location, 808738658, 0x3FF727, BasementHall134Header, BasementHall134Records),
          "the Back Alley evt73 (headerId 90) is a checked prologue site with its own group");
    bool alleyBits = true;
    for (std::size_t i = 0; i < BackAlley73Records.size(); ++i) {
        auto r = BackAlley73Records; r[i] ^= 1;
        alleyBits = alleyBits && !MissingPartySiteFor(BackAlley73Location, 808738658, 0x3FF727, BackAlley73Header, r);
    }
    check(alleyBits, "every changed Back Alley record bit refuses its site");
    // VUH-2466: the Day-1 Sandlot bat duel (evt77): four records, the opponent 1656 included.
    const auto* duel = MissingPartySiteFor(Sandlot77Location, 825515874, 0x3FF727, Sandlot77Header, Sandlot77Records);
    check(duel && duel->prologue && Sandlot77Records.size() == 4 * 64 && std::string_view(duel->name) == "tt04-77" &&
          MissingPartySiteAt(Sandlot77Location) == duel &&
          !MissingPartySiteFor(Sandlot77Location, 808738658, 0x3FF727, Sandlot77Header, Sandlot77Records),
          "the Sandlot evt77 bat duel is a checked prologue site under its own key");
    const auto* duel2 = MissingPartySiteFor(Sandlot78Location, 842293090, 0x3FF727, Sandlot78Header, Sandlot78Records);
    check(duel2 && duel2->prologue && std::string_view(duel2->name) == "tt04-78" && MissingPartySiteAt(Sandlot78Location) == duel2 &&
          !MissingPartySiteFor(Sandlot78Location, 842293090, 0x3FF727, Sandlot77Header, Sandlot77Records),
          "the duel's second stage (evt78) is a checked prologue site with its own group");
    const auto* calling = MissingPartySiteFor(Calling33Location, 808476525, 0x3FF727, Calling33Header, Calling33Records);
    auto callingWrongProgram = Calling33Location; callingWrongProgram[6] = 0;
    check(calling && calling->prologue && Calling33Records.size() == 64 && std::string_view(calling->name) == "tt33-calling" &&
          MissingPartySiteAt(Calling33Location) == calling && !MissingPartySiteAt(callingWrongProgram) &&
          !MissingPartySiteFor(Calling33Location, 808738658, 0x3FF727, Calling33Header, Calling33Records),
          "the Dive's Station of Calling (raw566 alone) is a checked prologue site (VUH-2570)");
    // The receipt's bytes, read independently of the literal: raw566, x = 0x44FC8000 (2020.0), headerId 23.
    check(Calling33Records[0] == 0x36 && Calling33Records[1] == 0x02 && Calling33Records[5] == 0x80 &&
          Calling33Records[6] == 0xFC && Calling33Records[7] == 0x44 && Calling33Header[2] == 0x17 &&
          Bytes("AbF0")[0] == 0xAB && Bytes("AbF0")[1] == 0xF0,
          "pinned receipt bytes parse as logged, upper or lower case hex");
    const auto* mansion = MissingPartySiteFor(Mansion14Location, 808476525, 0x3FF727, Mansion14Header, Mansion14Records);
    auto mansionOtherProgram = Mansion14Location; mansionOtherProgram[8] = 0;
    check(mansion && mansion->prologue && std::string_view(mansion->name) == "tt14-1" &&
          MissingPartySiteAt(Mansion14Location) == mansion && !MissingPartySiteAt(mansionOtherProgram) &&
          Mansion14Header[2] == 40 && Mansion14Records.size() == 3 * 64 &&
          !StepAsideAt(Mansion14Location),
          "the Day 1 Old Mansion front (evt1) is a checked prologue site (VUH-2572)");
    const auto* woods = MissingPartySiteFor(Woods13Location, 808804197, 0x3FF727, Woods13Header, Woods13Records);
    auto woodsOtherDoor = Woods13Location; woodsOtherDoor[2] = 0;
    check(woods && woods->prologue && std::string_view(woods->name) == "tt13-22" && Woods13Header[2] == 57 &&
          StepAsideAt(Woods13Location) && !StepAsideAt(woodsOtherDoor) && !MissingPartySiteAt(woodsOtherDoor) &&
          StepAsideZ == 150.0f,
          "the Day 1 Woods (door 50, evt22) is a checked prologue site where seat 1 steps aside (VUH-2578)");
    const auto* dusk127 = MissingPartySiteFor(Mansion127Location, 825515874, 0x3FF727, Mansion127Header, Mansion127Records);
    check(dusk127 && dusk127->prologue && std::string_view(dusk127->name) == "tt14-127" && Mansion127Records.size() == 4 * 64 &&
          Mansion127Header[2] == 65 && Mansion127Records[3 * 64] == 0x3E && Mansion127Records[3 * 64 + 1] == 0x01 &&
          MissingPartySiteAt(Mansion127Location) == dusk127 && !StepAsideAt(Mansion127Location),
          "the Day 1 Old Mansion Dusk (btl127, the Dusk 318 pinned with the players) is a checked prologue site (VUH-2466)");
    const auto* dusk128 = MissingPartySiteFor(Mansion128Location, 842293090, 0x3FF727, Mansion128Header, Mansion128Records);
    check(dusk128 && dusk128->prologue && std::string_view(dusk128->name) == "tt14-128" && Mansion128Header[2] == 87 &&
          MissingPartySiteAt(Mansion128Location) == dusk128 &&
          !MissingPartySiteFor(Mansion128Location, 842293090, 0x3FF727, Mansion127Header, Mansion127Records),
          "the Mansion Dusk's second stage (btl128) is a checked prologue site with its own group (VUH-2466)");
    const auto* dusk79 = MissingPartySiteFor(Sandlot79Location, 859070306, 0x3FF727, Sandlot79Header, Sandlot79Records);
    const auto* serenity2 = MissingPartySiteFor(Serenity2Location, 808804197, 0x3FF727, Serenity2Header, Serenity2Records);
    const auto* serenity154 = MissingPartySiteFor(Serenity154Location, 808738658, 0x3FF727, Serenity154Header, Serenity154Records);
    auto serenitySave = Serenity2Location; serenitySave[2] = 0x63;
    check(dusk79 && std::string_view(dusk79->name) == "tt04-79" && Sandlot79Header[2] == 115 && Sandlot79Records.size() == 4 * 64 &&
          serenity2 && std::string_view(serenity2->name) == "tt32-2" && Serenity2Header[2] == 91 && Serenity2Records.size() == 6 * 64 &&
          !MissingPartySiteAt(serenitySave) &&
          serenity154 && std::string_view(serenity154->name) == "tt32-154" && Serenity154Header[2] == 47 &&
          Serenity154Records.size() == 6 * 64 && dusk79->prologue && serenity2->prologue && serenity154->prologue,
          "the Day 3 Sandlot Dusk, the Serenity arrival from it and the Serenity tutorial Dusks are checked prologue sites (VUH-2466)");
    const auto* alley4 = MissingPartySiteFor(BackAlley4Location, 808804197, 0x3FF727, BackAlley4Header, BackAlley4Records);
    auto alley4Short = std::vector<std::uint8_t>(BackAlley4Records.begin(), BackAlley4Records.begin() + 4 * 64);
    check(alley4 && alley4->prologue && std::string_view(alley4->name) == "tt03-4" && BackAlley4Records.size() == 18 * 64 &&
          BackAlley4Header[4] == 18 && MissingPartySiteAt(BackAlley4Location) == alley4 &&
          !MissingPartySiteFor(BackAlley4Location, 808804197, 0x3FF727, BackAlley4Header, alley4Short),
          "the Back Alley after the ambush (18 records, every one pinned) is a checked prologue site (VUH-2572)");
    const auto entrance6 = std::span<const std::uint8_t>(BackAlley4Records).subspan(6 * 64, 64);
    auto foreign = std::vector<std::uint8_t>(entrance6.begin(), entrance6.end()); foreign[8] ^= 1;
    check(SitePlayerRecordAt(*alley4, 0) && SitePlayerRecordAt(*alley4, 6) && !SitePlayerRecordAt(*alley4, 7) &&
          !SitePlayerRecordAt(*alley4, 18) && SiteHasPlayerRecord(*alley4, entrance6) &&
          !SiteHasPlayerRecord(*alley4, std::span<const std::uint8_t>(BackAlley4Records).subspan(7 * 64, 64)) &&
          !SiteHasPlayerRecord(*alley4, foreign),
          "a group's raw566 for any entrance (Back Alley door 52 takes index 6) is accepted, nothing else");
    const auto* hall2 = MissingPartySiteFor(BasementHall2Location, 808804197, 0x3FF727, BasementHall2Header, BasementHall2Records);
    check(hall2 && hall2->prologue && std::string_view(hall2->name) == "tt19-2" && BasementHall2Records.size() == 6 * 64 &&
          BasementHall2Header[2] == 94 && MissingPartySiteAt(BasementHall2Location) == hall2 &&
          MissingPartySiteAt(BasementHall134Location) != hall2,
          "the Basement Hall after the post-Axel chain (door 50, evt2) is a checked prologue site (VUH-2572)");
    check(MissingPartyAbort(0x3A7B1B, 1, 0) && MissingPartyAbort(0x3A7B1B, 14, 0) && !MissingPartyAbort(0x3A7B1B, 4, 11) &&
          !MissingPartyAbort(0x3A7B1C, 14, 0), "the fatal base-allocation contract covers Sora and Roxas form0 only");
    check(prologue::ProfileQualified(0x5A) && prologue::ProfileQualified(0x54) && !prologue::ProfileQualified(0x5B) &&
          !prologue::ProfileQualified(0x323) && !prologue::ProfileQualified(0x1234),
          "VUH-1512: Roxas and Sora are admitted to the prologue layout, nothing else");
    check(!prologue::ProfileRefusal(0x54) && !prologue::ProfileRefusal(0x5A) && prologue::ProfileRefusal(0x5B) &&
          prologue::ProfileRefusal(0x323) && prologue::ProfileRefusal(0x1234), "every refused profile has a reason");
    check(std::string_view(prologue::HoldName(prologue::Hold::Unpinned)) == "unpinned-field" &&
          std::string_view(prologue::HoldName(prologue::Hold::RemoteUnbound)) == "remote-unbound", "every hold has its logged reason");
}

void network() {
    SessionConfig config; config.bindAddress = "127.0.0.1"; config.port = 29879; config.gameBuild = "seat-test"; config.modHash = "none";
    SessionHost server(config); if (!server.start()) { check(false, "ENet host starts"); return; }
    std::array<std::unique_ptr<NetworkClient>, 2> clients;
    std::array<std::vector<SeatState>, 2> received;
    std::array<std::vector<StageEnd>, 2> stages;
    for (unsigned i = 0; i < 2; ++i) {
        ClientCallbacks cb;
        cb.onWorldEnvelope = [&received, &stages, i](const WorldEnvelope& e) {
            const std::uint8_t* p = nullptr; std::size_t n = 0; // the admitted inner packet
            const auto type = decodePacketHeader(e.packet.data(), e.packet.size(), p, n);
            if (type == PacketType::StageEnd) { ByteReader s(p, n); StageEnd m; read(s, m); stages[i].push_back(m); return; }
            if (type != PacketType::SeatState) return;
            ByteReader s(p, n); SeatState m; read(s, m); received[i].push_back(m);
        };
        clients[i] = std::make_unique<NetworkClient>("127.0.0.1", config.port, "seat-test", "none", "seat" + std::to_string(i), static_cast<SlotType>(i), cb);
        clients[i]->connect();
    }
    auto pump = [&] { for (unsigned n = 0; n < 30; ++n) { server.tick(); for (auto& c : clients) c->tick(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); } };
    pump(); pump();
    if (server.verifiedPeerCount() != 2) { check(false, "two peers admitted"); return; }
    const std::array<std::uint64_t, 3> roster{server.peerBySlot(SlotType::Player)->connectionId, server.peerBySlot(SlotType::Friend1)->connectionId, 0};
    // No room is needed: seats declare at the title / during the opening.
    check(clients[1]->sendNativeWorld(encode(seat(roster, 1, 0x5A)), {1, clients[1]->deliverySerial(), 0}, true), "friend authors its seat");
    check(clients[0]->sendNativeWorld(encode(seat(roster, 1, 0x5A)), {1, clients[0]->deliverySerial(), 1}, true), "host authors its seat");
    pump();
    const auto owners = [&](unsigned i) { unsigned mask = 0; for (const auto& m : received[i]) mask |= 1u << m.ownerSlot; return mask; };
    check(received[0].size() == 2 && received[1].size() == 2 && owners(0) == 3 && owners(1) == 3,
          "both seats reach both peers, each author's own copy included, owner slots stamped by the relay");
    clients[1]->sendNativeWorld(encode(seat(roster, 1, 0x5A)), {1, clients[1]->deliverySerial(), 0}, true); pump();
    check(received[0].size() == 2, "a repeated sequence is refused by the relay");
    clients[1]->sendNativeWorld(encode(seat({roster[0], roster[1] + 7, 0}, 2, 0x5A)), {1, clients[1]->deliverySerial(), 0}, true); pump();
    check(received[0].size() == 2, "a report for another roster is refused");
    clients[1]->sendNativeWorld(encode(seat(roster, 3, 0x5A, {}, SeatPhase::Declared, 0)), {1, clients[1]->deliverySerial(), 0}, true); pump();
    check(received[0].size() == 2, "an author cannot pre-stamp an owner slot (impersonation)");
    clients[1]->sendNativeWorld(encode(seat(roster, 4, 0x5A, {0x5A, 0x5A, 0}, SeatPhase::Acknowledged)), {1, clients[1]->deliverySerial(), 0}, true); pump();
    check(received[0].size() == 3 && received[0].back().ownerSlot == 1 && received[0].back().phase == SeatPhase::Acknowledged,
          "a newer acknowledgement is admitted and stamped");
    // VUH-2555: StageEnd goes from a client to the host only, at the host's admitted room.
    StageEnd stage; stage.seq = 1; stage.worldId = 2; stage.roomId = 4; stage.eventProgram = 77; stage.id = 0x313131; stage.flags = 9;
    stage.params = {2, 0x402, 0x4E004E, 0x4E, 1, 0xFFFFFFFF, 3, 0, 0};
    clients[1]->sendNativeWorld(encode(stage), {1, clients[1]->deliverySerial(), 0}, true); pump();
    check(stages[0].empty() && stages[1].empty(), "a stage end before the host's first room is refused");
    clients[0]->sendRoomTransition(RoomTransition {1, 2, 4, 0, 77, 77, 77}); pump();
    stage.seq = 2;
    clients[1]->sendNativeWorld(encode(stage), {1, clients[1]->deliverySerial(), 0}, true); pump();
    check(stages[0].size() == 1 && stages[0].back().ownerSlot == 1 && stages[0].back().id == 0x313131 && stages[1].size() == 1,
          "the friend's stage end reaches the host (and its own echo), owner slot stamped by the relay");
    clients[1]->sendNativeWorld(encode(stage), {1, clients[1]->deliverySerial(), 0}, true); pump();
    auto other = stage; other.seq = 3; other.eventProgram = 78;
    clients[1]->sendNativeWorld(encode(other), {1, clients[1]->deliverySerial(), 0}, true); pump();
    auto stamped = stage; stamped.seq = 4; stamped.ownerSlot = 0;
    clients[1]->sendNativeWorld(encode(stamped), {1, clients[1]->deliverySerial(), 0}, true); pump();
    auto fromHost = stage; fromHost.seq = 5;
    clients[0]->sendNativeWorld(encode(fromHost), {1, clients[0]->deliverySerial(), 1}, true); pump();
    check(stages[0].size() == 1 && stages[1].size() == 1,
          "a repeated sequence, another location, a pre-stamped owner and the host's own report are all refused");
    for (auto& c : clients) c->disconnect();
    server.stop();
}
} // namespace
int main() { if (enet_initialize()) return 2; codec(); unpinned(); soloWait(); blockedExit(); policy(); site(); network(); enet_deinitialize(); std::cout << "failures=" << failures << '\n'; return failures ? 1 : 0; }
