#include "kh2coop/Protocol.hpp"
#include "CoopHudMailbox.hpp"
#include "kh2coop/AvatarBridge.hpp"
#include <atomic>
#include <climits>
#include <cstdio>
#include <cwchar>
#include <thread>

using namespace kh2coop;
using namespace kh2coop::inject;
namespace {
int checks = 0, failures = 0;
void Check(bool ok, const char* what) {
    ++checks; if (!ok) ++failures;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
}
SessionState Session() {
    SessionState s; s.sessionId = "session-a";
    for (std::uint8_t i = 0; i < 3; ++i) {
        SessionActor a; a.slot = static_cast<SlotType>(i); a.connectionId = 100u + i;
        a.ownerPeerId = i == 0 ? "James" : i == 1 ? "Alex" : "Rin";
        s.actors.push_back(a);
    }
    return s;
}
PuppetAuthority Authority(std::uint8_t slot = 0) {
    return {PuppetAuthorityMode::Network, slot, 7, {100, 101, 102}};
}
hudnames::Roster BindNames(const hudnames::Roster& r, const PuppetAuthority& a, std::uint64_t now) {
    return hudnames::Bind(r, a, now, "session-a", "session-a");
}
hud::Input Input(std::uint8_t slot) {
    hud::Input in;
    in.before = in.after = Authority(slot);
    in.names = BindNames(hudnames::FromSession(Session(), slot), in.after, 100);
    in.localAvailable = in.gameplayCurrent = true;
    in.local.worldId = 4; in.local.roomId = 26; in.local.hp = in.local.maxHp = 24;
    int index = 0;
    for (std::uint8_t owner = 0; owner < 3; ++owner) {
        if (owner == slot) continue;
        auto& r = in.remote[static_cast<std::size_t>(index++)];
        r.active = true; r.avatar = in.local;
        r.avatar.ownerSlot = static_cast<SlotType>(owner); r.avatar.hp = 20 + owner;
        r.provenance = {PuppetProducer::Network, slot, 7, 100u + owner, 100u + slot, 100};
    }
    return in;
}
void Names() {
    Check(hudnames::Sanitize(" James ") == hudnames::Sanitize("James"), "trim space");
    for (const auto name : {"", "    ", "Alex\nHost", "Alex\r", "Alex\t", "\x1b[31m", "\xc3\xa9"})
        Check(hudnames::Sanitize(name) == hudnames::Name {}, "empty/control/non-ASCII is unavailable");
    Check(hudnames::Sanitize(std::string_view("A\0B", 3)) == hudnames::Name {}, "embedded NUL refused");
    Check(hudnames::Sanitize(std::string(129, 'x')) == hudnames::Name {}, "over128 peer ID unavailable");
    const auto bounded = hudnames::Sanitize(std::string(128, 'x'));
    Check(bounded[19] == 'x' && bounded[20] == '.' && bounded[22] == '.' && bounded[23] == 0,
          "long printable name has explicit ellipsis and terminator");
    Check(hudnames::Sanitize("%s%n&[Host]")[0] == '%', "printable text stays literal data");
    for (unsigned variant = 0; variant < 9; ++variant) {
        auto s = Session();
        if (variant == 0) s.actors[1].ownerPeerId = "James";
        if (variant == 1) s.actors[1].ownerPeerId.clear();
        if (variant == 2) s.actors[1].ownerPeerId = "A\nB";
        if (variant == 3) s.actors[1].connectionId = 100;
        if (variant == 4) s.actors[1].slot = SlotType::Player;
        if (variant == 5) s.actors[1].slot = static_cast<SlotType>(9);
        if (variant == 6) s.actors[1].connectionId = 0;
        if (variant == 7) { s.actors[0].ownerPeerId = std::string(30, 'x'); s.actors[1].ownerPeerId = std::string(31, 'x'); }
        if (variant == 8) s.actors.push_back(s.actors[0]);
        Check(!hudnames::Valid(hudnames::FromSession(s, 0)), "ambiguous/malformed admission cannot name any row");
    }
    auto r = hudnames::FromSession(Session(), 0);
    r.names[1][23] = 'X'; Check(!hudnames::Valid(r), "unterminated shared text refused");
    r = hudnames::FromSession(Session(), 0); r.names[1][8] = 'X';
    Check(!hudnames::Valid(r), "nonzero tail after terminator refused");
    for (std::uint8_t local = 0; local < 3; ++local) {
        auto in = Input(local); const auto snapshot = hud::Project(in, 101, 42);
        const auto text = hud::Format(snapshot);
        Check(std::wcscmp(text.rows[0].name, L"James") == 0 &&
            std::wcscmp(text.rows[1].name, L"Alex") == 0 && std::wcscmp(text.rows[2].name, L"Rin") == 0,
            "bidirectional owner labels independent of local/friend slots");
        Check(std::wcsstr(text.rows[local].label, L"You") && std::wcsstr(text.rows[0].label, L"Host") &&
            snapshot.members[local].hp == 24, "Host You and owner HP preserved");
    }
    for (unsigned variant = 0; variant < 10; ++variant) {
        auto in = Input(1);
        if (variant == 0) --in.names.generation;
        if (variant == 1) ++in.names.connections[0];
        if (variant == 2) ++in.names.connections[1];
        if (variant == 3) ++in.names.connections[2];
        if (variant == 4) in.names.localSlot = 0;
        if (variant == 5) in.names.sampledAtMs = 102;
        if (variant == 6) in.names.sampledAtMs = 0;
        if (variant == 7) in.names = {};
        if (variant == 8) in.names.names[1][0] = '\n';
        if (variant == 9) in.names.names[1] = in.names.names[0];
        const auto out = hud::Project(in, 101, 42);
        Check(out.networkCurrent && out.members[1].hp == 24 && out.members[1].name == hudnames::Name {} &&
            std::wcscmp(hud::Format(out).rows[1].name, L"Name unavailable") == 0,
            "stale/mixed/untrusted metadata never replaces health authority or labels");
    }
    auto in = Input(1);
    Check(hud::Project(in, 1099, 42).members[1].name[0] == 'A', "name age999 accepted");
    Check(!hud::Project(in, 1100, 42).members[1].name[0], "name age1000 rejected");
    auto snapshot = hud::Project(in, 999, 42), prior = snapshot;
    hud::ExpireNames(snapshot, 1100);
    Check(snapshot.members[1].hp == 24 && !snapshot.members[1].name[0] &&
        !hud::SameDisplayScope(snapshot, prior), "Present expiry invalidates text without dropping valid HP");
    auto renamed = in; renamed.names.names[1] = hudnames::Sanitize("NewName");
    Check(!hud::SameDisplayScope(hud::Project(renamed, 101, 42), hud::Project(in, 101, 42)),
          "name change invalidates pixels before15Present cadence");
    in.after.generation++;
    Check(!hud::Project(in, 101, 42).networkCurrent, "owner authority bracket change retires whole snapshot");
    in = Input(0); in.before.connectionIds[1] = in.after.connectionIds[1] = 0;
    auto departed = Session(); departed.actors.erase(departed.actors.begin() + 1);
    in.names = BindNames(hudnames::FromSession(departed, 0), in.after, 100);
    snapshot = hud::Project(in, 101, 42);
    Check(snapshot.members[1].state == hud::RowState::Open && !snapshot.members[1].hpValid &&
        !snapshot.members[1].name[0] && !hud::Format(snapshot).rows[1].name[0] &&
        std::wcscmp(hud::Format(snapshot).rows[1].status, L"Open slot") == 0,
        "departure clears departed name/HP with old puppet sample retained");
    in = Input(0); in.remote[0].active = false;
    Check(!hud::Format(hud::Project(in, 101, 42)).rows[1].name[0] &&
        std::wcscmp(hud::Format(hud::Project(in, 101, 42)).rows[1].status, L"Waiting for avatar") == 0,
        "waiting status retained rather than displaying unsupported native friend name");
    auto reordered = Session();
    std::swap(reordered.actors[0], reordered.actors[2]);
    Check(hudnames::FromSession(reordered, 1).names == hudnames::FromSession(Session(), 1).names,
          "roster vector order cannot change owner row labels");
    const auto admitted = hudnames::FromSession(Session(), 0);
    Check(!hudnames::Valid(hudnames::Bind(admitted, Authority(), 200, "session-a", "session-b")),
          "same connection IDs in another session cannot renew old names");
    Check(!hudnames::Valid(hudnames::Bind(admitted, Authority(), 200, "", "")), "empty scope cannot renew");
    auto off = Authority(); off.mode = PuppetAuthorityMode::Unavailable;
    Check(!hudnames::Valid(BindNames(admitted, off, 200)), "unavailable authority cannot renew names");
    off = Authority(); off.generation = 0;
    Check(!hudnames::Valid(BindNames(admitted, off, 200)), "generation0 cannot renew names");
    auto a = Authority(); a.generation = 8;
    Check(BindNames(hudnames::FromSession(Session(), 0), a, 200).generation == 8,
          "same admitted binding can renew at new room generation");
    a.connectionIds[1]++;
    Check(!hudnames::Valid(BindNames(hudnames::FromSession(Session(), 0), a, 200)),
          "replacement cannot relabel old admitted owner");
}
void SlotAndConcurrency() {
    hudnames::Slot slot;
    hudnames::Roster out = hudnames::FromSession(Session(), 0);
    Check(!slot.TryRead(out) && !hudnames::Valid(out), "unpublished zero slot refuses with cleared output");
    const auto valid = BindNames(hudnames::FromSession(Session(), 0), Authority(), 100);
    Check(slot.TryWrite(valid) && slot.TryRead(out) && hudnames::Matches(out, Authority(), 101),
          "actual shared slot roundtrip");
    InterlockedExchange64(&slot.sequence, 3);
    InterlockedExchange(&slot.words[0], 999);
    Check(!slot.TryRead(out) && !hudnames::Valid(out), "partial odd publication cannot disclose prior/partial label");
    Check(!slot.TryWrite(valid), "concurrent publisher refuses occupied slot without completing another write");
    InterlockedExchange64(&slot.sequence, (std::numeric_limits<LONG64>::max)() - 1);
    Check(!slot.TryWrite(valid), "sequence exhaustion refuses without wrap");
    hudnames::Slot concurrent;
    std::atomic<bool> done {false}; std::atomic<unsigned> reads {0}, mismatches {0};
    std::thread reader([&] {
        do {
            hudnames::Roster r;
            if (concurrent.TryRead(r)) {
                ++reads;
                if (!hudnames::Valid(r) || r.generation != r.connections[0] ||
                    r.connections[1] != r.connections[0] + 1 || r.connections[2] != r.connections[0] + 2 ||
                    r.sampledAtMs != r.connections[0] || r.names[0][0] != (r.generation & 1 ? 'A' : 'B')) ++mismatches;
            }
        } while (!done.load());
    });
    for (std::uint32_t i = 100; i < 50100; ++i) {
        auto r = valid; r.connections = {i, i + 1u, i + 2u}; r.generation = i; r.sampledAtMs = i;
        r.names[0] = hudnames::Sanitize(i & 1 ? "Alpha" : "Beta");
        if (!concurrent.TryWrite(r)) ++mismatches;
    }
    done = true; reader.join();
    std::printf("concurrent publications=50000 successfulReads=%u mismatches=%u\n", reads.load(), mismatches.load());
    Check(reads > 0 && mismatches == 0, "concurrent atomic payload copies cannot mix names/binding");
    Check(concurrent.TryWrite({}) && concurrent.TryRead(out) && !hudnames::Valid(out),
          "explicit reset retires names with empty publication");
    Check(AVATAR_BRIDGE_VERSION == 4 && sizeof(hudnames::Roster) == 112 && sizeof(hudnames::Slot) == 128 &&
          offsetof(AvatarBridgeLayout, rosterNames) == offsetof(AvatarBridgeLayout, puppets) +
              sizeof(AvatarBridgeLayout::puppets), "v4 preserves roster layout before new downed slot");
    Check(PROTOCOL_VERSION == 13, "network protocol13 (v12 party contract plus v13 EnemyMotion)");
    std::printf("ABI avatarVersion=%u layoutBytes=%zu rosterOffset=%zu rosterPayload=%zu rosterSlot=%zu protocol=%u\n",
        AVATAR_BRIDGE_VERSION, sizeof(AvatarBridgeLayout), offsetof(AvatarBridgeLayout, rosterNames),
        sizeof(hudnames::Roster), sizeof(hudnames::Slot), PROTOCOL_VERSION);
}
}
int main() {
    Names(); SlotAndConcurrency();
    std::printf("RESULT %d checks %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
