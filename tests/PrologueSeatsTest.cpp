// VUH-2464 prologue seats: SeatState codec, the pure seat/hold policy, the checked Tram Common
// missing-party site, and the relay over real loopback ENet (any-seat authorship, owner stamping,
// roster pinning, sequence floor and echo to the author).
#include "kh2coop/NetworkClient.hpp"
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
    check(decodePacketHeader(bytes.data(), bytes.size(), p, n) == PacketType::SeatState && n == 45 &&
          static_cast<unsigned>(PacketType::SeatState) == 53 && PROTOCOL_VERSION == 27, "SeatState is type 53, 45 bytes, protocol 27");
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
    check(prologue::Accept(s, seat(two, 1, 0x54, {}, SeatPhase::Declared, 1)) && prologue::Agreement(s) == Hold::UnqualifiedProfile,
          "a Sora friend is not admitted to the prologue layout yet (VUH-1512)");
    check(prologue::Accept(s, seat(two, 2, 0x5A, {}, SeatPhase::Declared, 1)) && prologue::Agreement(s) == Hold::NotAcknowledged,
          "declared Roxas/Roxas still needs both acknowledgements");
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
    check(MissingPartyAbort(0x3A7B1B, 1, 0) && MissingPartyAbort(0x3A7B1B, 14, 0) && !MissingPartyAbort(0x3A7B1B, 4, 11) &&
          !MissingPartyAbort(0x3A7B1C, 14, 0), "the fatal base-allocation contract covers Sora and Roxas form0 only");
    check(prologue::ProfileQualified(0x5A) && !prologue::ProfileQualified(0x54) && !prologue::ProfileQualified(0x5B) &&
          !prologue::ProfileQualified(0x323), "only Roxas is admitted to the prologue layout in this slice");
    check(std::string_view(prologue::HoldName(prologue::Hold::Unpinned)) == "unpinned-field" &&
          std::string_view(prologue::HoldName(prologue::Hold::RemoteUnbound)) == "remote-unbound", "every hold has its logged reason");
}

void network() {
    SessionConfig config; config.bindAddress = "127.0.0.1"; config.port = 29879; config.gameBuild = "seat-test"; config.modHash = "none";
    SessionHost server(config); if (!server.start()) { check(false, "ENet host starts"); return; }
    std::array<std::unique_ptr<NetworkClient>, 2> clients;
    std::array<std::vector<SeatState>, 2> received;
    for (unsigned i = 0; i < 2; ++i) {
        ClientCallbacks cb;
        cb.onWorldEnvelope = [&received, i](const WorldEnvelope& e) {
            const std::uint8_t* p = nullptr; std::size_t n = 0; // the admitted inner packet
            if (decodePacketHeader(e.packet.data(), e.packet.size(), p, n) != PacketType::SeatState) return;
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
    for (auto& c : clients) c->disconnect();
    server.stop();
}
} // namespace
int main() { if (enet_initialize()) return 2; codec(); unpinned(); policy(); site(); network(); enet_deinitialize(); std::cout << "failures=" << failures << '\n'; return failures ? 1 : 0; }
