// VUH-2342: MissionState codec and the mission mirror's pure rules (inject/src/MissionMirror.hpp).
#include "kh2coop/Codec.hpp"
#include "MissionMirror.hpp"
#include "PartyRow.hpp"
#include "BossLeave.hpp"
#include "NativeEnemyDeath.hpp"
#include <fstream>
#include <map>
#include <string>
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
    check(PROTOCOL_VERSION == 39 && bytes[0] == 52 && bytes.size() == 3 + MISSION_STATE_BYTES && MISSION_STATE_BYTES == 151,
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
    check(MR::PublishWhileHeld(true, false, "gameplay gate"), "the host publishes its mission through a mid-mission event (VUH-2606)");
    check(!MR::PublishWhileHeld(false, false, "gameplay gate") && !MR::PublishWhileHeld(true, true, "gameplay gate") &&
          !MR::PublishWhileHeld(true, false, "instance changed") && !MR::PublishWhileHeld(true, false, "location read") &&
          !MR::PublishWhileHeld(true, false, nullptr),
          "only the host's gameplay-gate hold publishes; other census failures and the friend don't");
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
    check(BL::Fallback(50, 12, false, false, 307), "Thorn copy uses checked boss leave after a refused lethal");
    check(!BL::Fallback(50, 12, false, false, 308), "other type-12 objects cannot gain boss-leave authority");
    check(!BL::Fallback(50, 12, true, false, 307), "Thorn cannot bypass the record-authority write fence");
    check(!BL::Fallback(50, 12, false, true, 307), "Thorn leave is attempted once per occurrence");
    check(BL::Fallback(221, 3, false, false), "VUH-2367: a boss copy that refused the lethal fades out");
    check(!BL::Fallback(0, 3, false, false), "a boss the lethal killed needs no fallback");
    check(!BL::Fallback(221, 4, false, false), "an ordinary enemy keeps the failed lethal as it was");
    check(!BL::Fallback(221, 3, true, false), "never inside a resync's record-authority write fence");
    check(!BL::Fallback(221, 3, false, true), "one attempt per local spawn");
}

void nativeDeath() {
    namespace ND=kh2coop::inject::nativedeath;
    ND::Receipt before;
    before.available=true;before.netId=5;
    before.scope={2,3,3,2,1,1,1,{2,19,0,134,134,134}};
    before.identity={0x10000,0x20000,0x30000,0x40000,0x50000,309};
    auto after=before;after.flags=4;
    check(ND::Completed(before,after),"native death notification recognizes a positive-HP scripted terminal");
    check(!ND::Completed(before,before),"alive removal without a native dead edge earns no death");
    check(!ND::Completed(after,after),"a repeated notification is not a new death");
    auto wrong=after;wrong.available=false;
    check(!ND::Completed(before,wrong),"unavailable poststate refuses death");
    wrong=before;wrong.available=false;
    check(!ND::Completed(wrong,after),"unavailable entry refuses death");
    wrong=after;++wrong.netId;
    check(!ND::Completed(before,wrong),"another announced occurrence refuses death");
    for(unsigned i=0;i<13;++i) {
        wrong=after;
        switch(i) {
        case 0: ++wrong.scope.generation;break;
        case 1: ++wrong.scope.epoch;break;
        case 2: ++wrong.scope.load;break;
        case 3: ++wrong.scope.transition;break;
        case 4: ++wrong.scope.hostConnection;break;
        case 5: ++wrong.scope.hostDelivery;break;
        case 6: ++wrong.scope.writerDelivery;break;
        default: ++wrong.scope.location[i-7];break;
        }
        check(!ND::Completed(before,wrong) && !ND::Pending(after,wrong.scope,true,false,false),
              "changed owner scope refuses completion and publication");
    }
    for(unsigned i=0;i<6;++i) {
        wrong=after;
        switch(i) {
        case 0: ++wrong.identity.actor;break;
        case 1: ++wrong.identity.objentry;break;
        case 2: ++wrong.identity.status;break;
        case 3: ++wrong.identity.controller;break;
        case 4: ++wrong.identity.record;break;
        default: ++wrong.identity.objectId;break;
        }
        check(!ND::Completed(before,wrong),"changed native roots refuse completion");
    }
    check(ND::Pending(after,after.scope,true,false,false),"held census can drain a copied real death");
    check(!ND::Pending(after,after.scope,false,false,false) && !ND::Pending(after,after.scope,true,true,false) &&
          !ND::Pending(after,after.scope,true,false,true),"unannounced, already sent and retired rows never publish");
    check(MR::EndRosterReady(true,true,false),"complete empty or settled scoped roster permits end");
    check(!MR::EndRosterReady(true,true,true) && !MR::EndRosterReady(true,false,false) &&
          !MR::EndRosterReady(false,true,false),"living, unavailable and changed-mission rosters defer end");
}

// Exact event excerpts from Juno's host logs. This replays the already observed
// funnel receipts through the production pending rule. The base run has no
// such receipt: no test invents one or claims native-hook/live coverage for it.
void terminalLogs() {
    namespace ND=kh2coop::inject::nativedeath;
    for(const char* name:{"193245","194057","194738","180147","200433","201309"}) {
        std::ifstream input(std::string(KH2COOP_TERMINAL_FIXTURES)+"/"+name+".log");
        check(input.good(),"retained host log excerpt is readable");
        std::map<unsigned,ND::Receipt> pending;
        std::map<unsigned,bool> sent;
        unsigned captured=0,heldDrain=0,observedLeaves=0;
        bool ended=false,lateBeforeEnd=false;
        std::string line;
        while(std::getline(input,line)) {
            if(line.find("host native kill netId=")!=std::string::npos) {
                const auto id=static_cast<unsigned>(std::stoul(line.substr(line.find("netId=")+6)));
                auto& receipt=pending[id];receipt.available=true;receipt.netId=id;++captured;
            }
            if(line.find("despawned, not refilled, left=1")!=std::string::npos)++observedLeaves;
            const bool held=line.find("native census unavailable: gameplay gate")!=std::string::npos;
            const bool endSnapshot=line.find("[mission-mirror] host frame=")!=std::string::npos &&
                line.find("end=3/")!=std::string::npos;
            if(held || endSnapshot) {
                for(const auto& [id,receipt]:pending)if(ND::Pending(receipt,receipt.scope,true,sent[id],false)) {
                    if(held)++heldDrain;
                    EnemyDeath death;death.epoch=3;death.netId=static_cast<std::uint16_t>(id);
                    const auto bytes=encode(death);
                    const std::uint8_t* payload=nullptr;std::size_t size=0;
                    check(decodePacketHeader(bytes.data(),bytes.size(),payload,size)==PacketType::EnemyDeath,
                          "retained terminal uses the enemy-death packet");
                    ByteReader reader(payload,size);EnemyDeath packet;read(reader,packet);
                    check(packet.left==0,"a retained native terminal encodes as a death, never a leave");
                    sent[id]=true;
                    if(std::string(name)=="194738" && id==5 && !ended)lateBeforeEnd=true;
                }
                if(endSnapshot)ended=true;
            }
        }
        if(std::string(name)=="194738")check(captured==6 && heldDrain>0 && lateBeforeEnd,
            "run 194738 final funnel receipt drains during the event before its end snapshot");
        if(std::string(name)=="194057")check(captured==6,"run 194057 retains all six actual funnel receipts");
        if(std::string(name)=="193245")check(captured==4 && observedLeaves==1 && !sent[5] && !sent[6],
            "base trace exposes missing wave-two native receipts; absence never fabricates two kills");
        if(std::string(name)=="180147")check(observedLeaves>0,"Calling trace retains the same alive-disappearance symptom");
        if(std::string(name)=="200433")check(captured==4 && observedLeaves==1 && !ended && !sent[5] && !sent[6],
            "host-down rerun loses a wave-two body before a wipe, with no mission-complete: no invented kills");
        if(std::string(name)=="201309")check(captured==4 && observedLeaves==1 && ended && !sent[5] && !sent[6],
            "friend-down rerun loses wave-two terminal evidence across mission-complete: no invented kills");
    }
    for(const char* name:{"193245","194057","194738","200433","201309"}) {
        std::ifstream input(std::string(KH2COOP_TERMINAL_FIXTURES)+"/"+name+"-friend.log");
        check(input.good(),"retained friend log excerpt is readable");
        bool living=true,empty=false,cap=false;unsigned deferred=0;
        std::string line;
        while(std::getline(input,line)) {
            if(line.find("[statehash] role=client epoch=3 ")!=std::string::npos &&
               line.find("room=02/13 ")!=std::string::npos) {
                const auto count=std::stoul(line.substr(line.find(" count=")+7));
                living=count>0;
                if(!living)empty=MR::EndRosterReady(true,true,false);
            }
            if(line.find("client end deferred file=TT19")!=std::string::npos) {
                ++deferred;
                check(!MR::EndRosterReady(true,true,living),
                      "actual hall deferral follows a living-roster sample, not an empty one");
            }
            if(line.find("client end like the host file=TT19")!=std::string::npos &&
               line.find("path=frame-cap")!=std::string::npos)cap=true;
        }
        if(std::string(name)=="194738")check(deferred==64 && cap && empty,
            "friend-down trace records fallback replay before final settlement, then permits the empty roster");
        if(std::string(name)=="194057")check(empty && !deferred,
            "host-down trace settles natively without a roster deferral");
    }
}
}  // namespace
int main() {
    {
        auto m=sample();m.endTrap=16;
        check(throws([&]{encode(m);}), "boss end observation refuses an unrelated mission");
        m.file={};std::memcpy(m.file.data(),"TT34_MS304.bar",14);
        check(decode(encode(m)).endTrap==16, "Thorn host boss end observation roundtrips without changing payload size");
        std::size_t boss=0;while(boss<MR::kTrapCount && MR::kTraps[boss].kind!=MR::Kind::BossEnd)++boss;
        check(boss<MR::kTrapCount && MR::EndTrapIndex(16)<0, "boss end is observed, never a replayable end trap");
        check(MR::DropLocalCall(boss,true,false,false,true) &&
              !MR::DropLocalCall(boss,true,false,false,false) &&
              MR::DropLocalCall(boss,false,false,true,true), "only qualified boss following/end guard drops local 4/16");
    }
    codec();
    rules();
    partyRows();
    bossLeave();
    nativeDeath();
    terminalLogs();
    std::cout << (errors ? "FAILED" : "ALL PASSED") << '\n';
    return errors ? 1 : 0;
}
