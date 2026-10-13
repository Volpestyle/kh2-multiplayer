#include "kh2coop/CoopDifficulty.hpp"
#include "kh2coop/AppliedStateHash.hpp"
#include "kh2coop/Codec.hpp"
#include "kh2coop/NetworkClient.hpp"
#include "kh2coop/SessionHost.hpp"
#include <enet/enet.h>
#include <chrono>
#include <cstring>
#include <functional>
#include <iostream>
#include <thread>

using namespace kh2coop;
namespace {
int failures = 0, checks = 0;
void check(bool ok, const char* label) {
    ++checks; if (!ok) ++failures;
    std::cout << (ok ? "PASS " : "FAIL ") << label << '\n';
}
void policy() {
    RoomDifficulty off; off.Lock(CoopDifficulty::Off, 3);
    const EnemyHealth vanilla{39,39}; const auto unscaled = off.Scale(vanilla,false);
    check(std::memcmp(&vanilla, &unscaled, sizeof(vanilla)) == 0 && off.Scale({0,39},true).hp == 0,
          "Off preserves native health bytes and death");
    check(!HealthWriteWithinMax(60,39) && HealthWriteWithinMax(60,39,63) &&
          !HealthWriteWithinMax(64,39,63),
          "host scaled/client unscaled: hold until max can be mirrored, never write current above max");
    RoomDifficulty room; room.Lock(CoopDifficulty::Normal,2);
    const auto scaled = room.Scale({20,39},false);
    check(scaled.hp == 32 && scaled.maxHp == 63, "normal scales wounded current and max HP with ceiling");
    room.Lock(CoopDifficulty::Hard,3);
    check(room.mode == CoopDifficulty::Normal && room.players == 2 && room.Scale({39,39},false).hp == 63,
          "mode and player count remain locked through mid-room roster changes");
    check(room.Scale({0,39},false).hp == 0 && room.Scale({-1,39},false).hp == -1, "scaling never resurrects terminal actors");
    check(HealthFactor(CoopDifficulty::Normal,1,false)==1000 && HealthFactor(CoopDifficulty::Normal,3,false)==2200 &&
          HealthFactor(CoopDifficulty::Hard,3,false)>2200 && HealthFactor(CoopDifficulty::Normal,2,true)==1300,
          "player tables and separate boss factors");
    check(ScaleHealthValue(INT32_MAX,2700)==INT32_MAX, "HP multiplication saturates without signed overflow");
    HealthScalingHistory history; HealthIdentity first{1,2,3,4,5,0}; history.Remember(first);
    check(room.Scale({63,63},false,history.Applied(first)).maxHp==63 &&
          room.Scale({39,39},false,history.Applied(first)).maxHp==39,
          "same spawn cannot scale twice after transport reset or a native max HP change");
    auto recycled=first; recycled[5]=1;
    check(!history.Applied(recycled), "existing spawn ordinal distinguishes recycled native roots");
    history.Clear(); check(!history.Applied(first), "native teardown clears once-only history");
    check(ParseDifficulty("off")==CoopDifficulty::Off && ParseDifficulty("garbage")==CoopDifficulty::Off &&
          ParseDifficulty("normal")==CoopDifficulty::Normal && ParseDifficulty("hard")==CoopDifficulty::Hard,
          "one environment setting defaults to Off");
}
void deathHashNotice() {
    // 20261013-012342: the relay compared its last host HP60 sample to the
    // client's post-death sample. These exact hashes exclude max HP entirely.
    const std::vector<AppliedEnemyState> wounded{{1,301,60},{2,301,60},{3,301,60}};
    const std::vector<AppliedEnemyState> dead{{1,301,0},{2,301,0},{3,301,0}};
    const std::vector<AppliedEnemyState> vanilla{{1,301,39},{2,301,39},{3,301,39}};
    check(hashAppliedEnemies(wounded) == 1804335653u && hashAppliedEnemies(dead) == 401581688u &&
          hashAppliedEnemies(dead) == hashAppliedEnemies({}) &&
          hashAppliedEnemies(vanilla) != 1804335653u,
          "observed desync is cached wounded HP versus successful deaths, not unscaled HP39");
}
void codec() {
    ClientHello hello; hello.coopDifficulty=CoopDifficulty::Hard;
    auto bytes=encode(hello); const std::uint8_t* payload=nullptr; std::size_t size=0;
    decodePacketHeader(bytes.data(),bytes.size(),payload,size); ByteReader hr(payload,size); ClientHello decoded; read(hr,decoded);
    check(hr.atEnd() && decoded.coopDifficulty==CoopDifficulty::Hard, "host difficulty handshake roundtrip");
    hello.protocolVersion = 17;
    bytes = encode(hello); // actual v17 hello, without difficulty byte
    decodePacketHeader(bytes.data(), bytes.size(), payload, size);
    ByteReader legacy(payload, size); read(legacy, decoded);
    check(legacy.atEnd() && decoded.protocolVersion == 17 && decoded.coopDifficulty == CoopDifficulty::Off,
          "v17 hello reaches the protocol rejection gate without a decode timeout");
    SessionState state; state.coopDifficulty=CoopDifficulty::Normal; bytes=encode(state);
    decodePacketHeader(bytes.data(),bytes.size(),payload,size); ByteReader sr(payload,size); SessionState advertised; read(sr,advertised);
    check(sr.atEnd() && advertised.coopDifficulty==CoopDifficulty::Normal, "advertised difficulty roundtrip");
    bytes.back()=255; bool refused=false;
    try { decodePacketHeader(bytes.data(),bytes.size(),payload,size); ByteReader bad(payload,size); read(bad,advertised); }
    catch (const std::exception&) { refused=true; }
    check(refused, "invalid advertised mode refused");
    RoomTransition transition; transition.difficultyPlayers = 2;
    bytes = encode(transition);
    decodePacketHeader(bytes.data(), bytes.size(), payload, size);
    ByteReader tr(payload, size); RoomTransition received; readRoomTransitionPacket(tr, received);
    check(tr.atEnd() && received.difficultyPlayers == 2,
          "room packet retains host's locked player count for late peers");
}
struct Seen { CoopDifficulty mode{CoopDifficulty::Off}; EnemyHealth hp{}; unsigned deaths{}, players{1}; };
ClientCallbacks callbacks(Seen& seen) {
    ClientCallbacks cb;
    cb.onSessionState=[&](const SessionState& state){seen.mode=state.coopDifficulty;};
    cb.onRoomTransition=[&](const RoomTransition& room){seen.players=room.difficultyPlayers;};
    cb.onEnemyHp=[&](const EnemyHp& health){for(const auto& e:health.entries)if(e.netId==1)seen.hp={e.hp,e.maxHp};};
    cb.onEnemyDeath=[&](const EnemyDeath& death){if(death.netId==1)++seen.deaths;};
    return cb;
}
void relay() {
    SessionConfig cfg; cfg.port=21941; cfg.bindAddress="127.0.0.1";
    cfg.gameBuild="difficulty-test"; cfg.modHash="difficulty-test"; cfg.contentHash="difficulty-test";
    SessionHost server(cfg); check(server.start(),"local test relay starts"); if(!server.isRunning())return;
    Seen hs,fs,ls;
    NetworkClient host("127.0.0.1",cfg.port,cfg.gameBuild,cfg.modHash,"difficulty-host",SlotType::Player,callbacks(hs),RuntimeMode::CampaignCoop,cfg.contentHash);
    NetworkClient friend1("127.0.0.1",cfg.port,cfg.gameBuild,cfg.modHash,"difficulty-friend",SlotType::Friend1,callbacks(fs),RuntimeMode::CampaignCoop,cfg.contentHash);
    NetworkClient late("127.0.0.1",cfg.port,cfg.gameBuild,cfg.modHash,"difficulty-late",SlotType::Friend2,callbacks(ls),RuntimeMode::CampaignCoop,cfg.contentHash);
    host.setCoopDifficulty(CoopDifficulty::Normal); friend1.setCoopDifficulty(CoopDifficulty::Hard); late.setCoopDifficulty(CoopDifficulty::Hard);
    auto wait=[&](const std::function<bool()>& done){
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while(!done() && std::chrono::steady_clock::now()<until){server.tick(0);for(auto* c:{&host,&friend1,&late}){c->tick(0);c->sendHeartbeat();}std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        return done();
    };
    host.connect(); friend1.connect();
    const bool ready=wait([&]{return host.worldReady() && friend1.worldReady();});
    check(ready && hs.mode==CoopDifficulty::Normal && fs.mode==CoopDifficulty::Normal,"only host setting controls advertised mode"); if(!ready)return;
    RoomTransition room{1,5,6,0,1,2,0}; room.difficultyPlayers=2;
    host.sendRoomTransition(room);
    EnemyManifest manifest; manifest.epoch=1; manifest.replace=true; EnemyManifestEntry entry;
    entry.netId=1; entry.objectId=301; entry.battleProgram=2; manifest.entries.push_back(entry); host.sendEnemyManifest(manifest);
    EnemyHp hp; hp.epoch=1; hp.sequence=1; hp.entries={{1,63,63}}; host.sendEnemyHp(hp);
    check(wait([&]{return fs.hp.hp==63 && fs.hp.maxHp==63;}),"existing HP path mirrors scaled current and max values");
    late.connect();
    check(wait([&]{return late.worldReady() && ls.hp.hp==63 && ls.hp.maxHp==63;}) && ls.mode==CoopDifficulty::Normal &&
          fs.players==2 && ls.players==2 && HealthFactor(ls.mode,ls.players,false)==1600,
          "late third player receives host mode and existing two-player room HP");
    host.sendEnemyDeath(EnemyDeath{1,1});
    check(wait([&]{return fs.deaths==1 && ls.deaths==1;}),"scaled enemy death agrees on both peers through existing path");
}
}
int main() {
    if(enet_initialize()!=0)return 2;
    policy(); deathHashNotice(); codec(); relay(); enet_deinitialize();
    std::cout << checks << " checks, " << failures << " failures\n"; return failures ? 1 : 0;
}
