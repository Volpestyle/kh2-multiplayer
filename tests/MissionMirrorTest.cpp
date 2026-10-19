// VUH-2342: MissionState codec and the mission mirror's pure rules (inject/src/MissionMirror.hpp).
#include "kh2coop/Codec.hpp"
#include "MissionMirror.hpp"
#include "PartyRow.hpp"
#include "BossLeave.hpp"
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace kh2coop;
namespace MR = kh2coop::inject::missionmirror::rules;
namespace {
int errors = 0;
void check(bool ok, const char* label) { std::cout << (ok ? "PASS: " : "FAIL: ") << label << '\n'; if (!ok) ++errors; }
void name(char (&out)[MR::kFileBytes], const char* file) { std::memset(out, 0, sizeof(out)); std::memcpy(out, file, std::strlen(file)); }
template<class F> bool throws(F f) { try { f(); return false; } catch (const std::exception&) { return true; } }
MissionState sample() {
    MissionState m; m.location = {3, 8, 1, 0, 68, 68, 0}; m.sequence = 9; m.hostFrame = 1200; m.active = true;
    std::memcpy(m.file.data(), "MU01_MS102.bar", 14); m.flags = 1;
    m.gauges[0] = {5, 100, 62.5f, 0}; m.gauges[1] = {1, 0, 0, 0};
    m.counters[0] = {1, 30, 0, 12};
    m.timerState = 2; m.timerMode = 1; m.timerValue = 3000; m.timerDuration = 7200;
    m.endTrap = 3; m.endArg = 41;
    return m;
}
MissionState decode(const std::vector<std::uint8_t>& bytes) {
    const std::uint8_t* p; std::size_t n;
    if (decodePacketHeader(bytes.data(), bytes.size(), p, n) != PacketType::MissionState) throw std::runtime_error("wrong type");
    ByteReader r(p, n); MissionState out; read(r, out); return out;
}
void codec() {
    auto m = sample(); const auto bytes = encode(m);
    check(PROTOCOL_VERSION == 27 && bytes[0] == 52 && bytes.size() == 3 + MISSION_STATE_BYTES && MISSION_STATE_BYTES == 151,
          "protocol25 packet52 has the exact 151-byte payload");
    check(encode(decode(bytes)) == bytes, "every field round trips");
    check(isWorldPacket(PacketType::MissionState) && isScopedWorldPacket(PacketType::MissionState) &&
          !isMaterialWorldPacket(PacketType::MissionState) && validateScopedWorldPacket(bytes) == PacketType::MissionState,
          "scoped world delivery, never material or resync state");
    MissionState none; none.location = m.location; none.sequence = 10;
    check(!decode(encode(none)).active, "no running mission is a valid state");
    auto bad = m; bad.sequence = 0; check(throws([&] { encode(bad); }), "sequence zero refused");
    bad = m; bad.location.epoch = 0; check(throws([&] { encode(bad); }), "epoch zero refused");
    bad = m; bad.file.fill('A'); check(throws([&] { encode(bad); }), "unterminated file name refused");
    bad = m; bad.file = {}; std::memcpy(bad.file.data(), "msn/MU01", 8); check(throws([&] { encode(bad); }), "a path, not a file, refused");
    bad = m; bad.file[20] = 'x'; check(throws([&] { encode(bad); }), "bytes after the terminator refused");
    bad = m; bad.gauges[0].gauge = 1e9f; check(throws([&] { encode(bad); }), "out-of-range gauge refused");
    bad = m; bad.timerState = 3; check(throws([&] { encode(bad); }), "unknown timer state refused");
    bad = none; bad.flags = 1; check(throws([&] { encode(bad); }), "inactive state with mission fields refused");
    bad = m; bad.endTrap = 11; check(throws([&] { encode(bad); }), "only the complete and end-signal traps can be an end");
    bad = m; bad.endTrap = 0; check(throws([&] { encode(bad); }), "an argument without an end trap refused");
    auto shortFrame = bytes; shortFrame.pop_back();
    check(throws([&] { decode(shortFrame); }), "a short frame is refused");
    auto activeFlag = bytes; activeFlag[3 + 16 + 8 + 4] = 2;
    check(throws([&] { decode(activeFlag); }), "active flag accepts only zero or one");
}
void rules() {
    MR::EndReplayDeferral defer;
    check(!defer.Capped(0) && !defer.Capped(0), "Bailey deferral starts at frame zero without counting repeated calls");
    check(!defer.Capped(179) && defer.Capped(180), "Bailey orphan waits at most 180 owner frames");
    check(defer.Capped(181), "Bailey cap remains reached until the mission resets");
    defer = {};
    check(!defer.Capped(900) && !defer.Capped(1079) && defer.Capped(1080), "new local mission gets a fresh full deferral budget");
    check(!defer.Capped(10) && !defer.Capped(189) && defer.Capped(190), "rewound owner frame restarts its bounded budget");
    char file[MR::kFileBytes];
    const char path[MR::kPathBytes] = "msn/us/MU01_MS102.bar";
    check(MR::FileOf(path, sizeof(path), file) && std::strcmp(file, "MU01_MS102.bar") == 0, "file is the part after the last slash");
    const char spanish[MR::kPathBytes] = "msn/sp/MU01_MS102.bar";
    char file2[MR::kFileBytes];
    check(MR::FileOf(spanish, sizeof(spanish), file2) && MR::SameFile(file, file2), "language folders don't matter");
    const char unterminated[4] = {'a', 'b', 'c', 'd'};
    check(!MR::FileOf(unterminated, sizeof(unterminated), file), "an unterminated path is refused");
    const char empty[MR::kPathBytes] = "msn/us/";
    check(!MR::FileOf(empty, sizeof(empty), file), "a path without a file is refused");

    MR::Mission host, local;
    host.active = local.active = true;
    name(host.file, "MU09_MS108.bar"); name(local.file, "MU09_MS108.bar");
    check(MR::Follows(host, local), "same file: follow");
    name(local.file, "MU01_MS102.bar");
    check(!MR::Follows(host, local), "different file: don't follow");
    name(local.file, "MU09_MS108.bar"); local.flags = MR::kEnded;
    check(!MR::Follows(host, local), "an ended local mission is left alone");
    local.flags = 0;
    host.active = false; check(!MR::Follows(host, local), "no host mission: don't follow");
    host.active = true;

    host.gauges[0] = {5, 100, 77.5f, 0}; local.gauges[0] = {5, 100, 90.0f, 0};
    check(MR::GaugeDiffers(host.gauges[0], local.gauges[0]), "a different gauge value is applied");
    local.gauges[0].flags = 0; check(!MR::GaugeDiffers(host.gauges[0], local.gauges[0]), "a slot this game doesn't use is left alone");
    local.gauges[0] = host.gauges[0]; check(!MR::GaugeDiffers(host.gauges[0], local.gauges[0]), "equal gauges are left alone");
    host.counters[1] = {1, 30, 0, 7}; local.counters[1] = {1, 30, 0, 5};
    check(MR::CountDiffers(host.counters[1], local.counters[1]), "a different count is applied");

    host.timerState = local.timerState = MR::kTimerRunning; host.timerMode = local.timerMode = MR::kTimerCountDown;
    host.timerDuration = local.timerDuration = 3600; host.timerValue = 3000; local.timerValue = 3010;
    check(MR::TimerStartDelta(host, local) == 0, "timers within the slack are left alone");
    local.timerValue = 3100;
    check(MR::TimerStartDelta(host, local) == -100, "counting down: 100 frames more left moves the start 100 earlier");
    host.timerMode = local.timerMode = 0;
    check(MR::TimerStartDelta(host, local) == 100, "counting up: 100 frames further moves the start 100 later");
    local.timerState = 0; check(MR::TimerStartDelta(host, local) == 0, "a stopped timer is left alone");

    MR::Mission ended = host; ended.flags = MR::kEnded; ended.result = 0; ended.endTrap = 0;
    check(MR::HostEndedUnknown(ended), "a host end with no trap seen is an unknown end: stop following");
    ended.endTrap = 3; check(!MR::HostEndedUnknown(ended), "a host end through 4/3 is replayed, not unknown");
    ended.endTrap = 0; ended.result = MR::kFailedResult; check(!MR::HostEndedUnknown(ended), "a failure is replayed, not unknown");
    const int complete = MR::EndTrapIndex(3), endSignal = MR::EndTrapIndex(22);
    check(complete >= 0 && endSignal >= 0 && MR::EndTrapIndex(11) < 0 && MR::EndTrapIndex(0) < 0,
          "the end traps are 4/3 and 4/22 only");
    check(!MR::DropLocalCall(2, false, false, false), "not following: the native call runs");
    check(MR::DropLocalCall(2, true, false, false), "following: a local set_gauge is dropped");
    check(MR::DropLocalCall(MR::kFailedTrap, true, false, false), "following: a local failure before the host's is dropped");
    check(!MR::DropLocalCall(MR::kFailedTrap, true, true, false), "following: a failure after the host's runs");
    check(MR::DropLocalCall(static_cast<std::size_t>(complete), true, false, false), "following: the mission's own completion is dropped");
    check(!MR::DropLocalCall(static_cast<std::size_t>(complete), false, false, false), "not following: completion runs natively");
    check(MR::DropLocalCall(static_cast<std::size_t>(endSignal), false, false, true) &&
          MR::DropLocalCall(MR::kFailedTrap, false, true, true) && !MR::DropLocalCall(2, false, false, true),
          "after the host's end was replayed, no second end or failure; values still pass");
    check(std::strcmp(MR::kTraps[MR::kFailedTrap].name, "mission_failed") == 0 && MR::kTraps[MR::kFailedTrap].index == 11,
          "kFailedTrap names trap 4/11");
}
void partyRows() {
    namespace PR = kh2coop::inject::partyrow;
    check(PR::StoryWorld(8) && PR::StoryWorld(0x0D) && !PR::StoryWorld(2) && !PR::StoryWorld(4) && !PR::StoryWorld(5) &&
          !PR::StoryWorld(19), "story rows: LoD and other worlds; never the clone's TT/HB/BC or past the table");
    check(PR::CompanionBytesValid(0x03020100u) && PR::CompanionBytesValid(0x12121200u) && PR::CompanionBytesValid(0x12030100u),
          "companions Donald, Goofy, the world character and empty are accepted");
    check(!PR::CompanionBytesValid(0x12020000u), "the clone selector 0 in a companion seat is refused");
    check(!PR::CompanionBytesValid(0x12040100u) && !PR::CompanionBytesValid(0x13020100u), "forms and out-of-range ids are refused");
    check(PR::Merge(0x1212125Au, 0x03020100u) == 0x0302015Au, "the friend keeps its own player byte, takes the host's companions");
    check(PR::Merge(0x12121200u, 0x12020100u) == 0x12020100u, "Sora-only LoD row takes the host's Donald and Goofy");
}
void bossLeave() {
    namespace BL = kh2coop::inject::bossleave;
    check(BL::Fallback(221, 3, false, false), "VUH-2367: a boss copy that refused the lethal fades out");
    check(!BL::Fallback(0, 3, false, false), "a boss the lethal killed needs no fallback");
    check(!BL::Fallback(221, 4, false, false), "an ordinary enemy keeps the failed lethal as it was");
    check(!BL::Fallback(221, 3, true, false), "never inside a resync's record-authority write fence");
    check(!BL::Fallback(221, 3, false, true), "one attempt per local spawn");
}
}  // namespace
int main() {
    codec();
    rules();
    partyRows();
    bossLeave();
    std::cout << (errors ? "FAILED" : "ALL PASSED") << '\n';
    return errors ? 1 : 0;
}
