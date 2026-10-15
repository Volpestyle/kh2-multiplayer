#include "WorldWireFixture.hpp"
#include "kh2coop/SessionHost.hpp"
#include "kh2coop/AppliedStateHash.hpp"
#include "kh2coop/AutomaticResyncNotice.hpp"
#include <fstream>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>
#include <algorithm>
#include <chrono>
using namespace kh2coop;
int checks=0, failures=0;
void check(bool yes,const char* label){++checks;failures+=!yes;std::cout<<(yes?"PASS ":"FAIL ")<<label<<'\n';}
std::string field(const std::string& row,const std::string& key){const auto p=" "+key+"=";auto at=row.find(p);if(at==std::string::npos)return {};at+=p.size();return row.substr(at,row.find(' ',at)-at);}
void waveDeathRegression() {
    SessionConfig cfg; cfg.bindAddress="127.0.0.1"; cfg.port=22121;
    SessionHost relay(cfg); check(relay.start(),"wave-death regression relay starts");
    NetworkClient host("127.0.0.1",cfg.port,cfg.gameBuild,cfg.modHash,"wave-host",SlotType::Player,{},RuntimeMode::CampaignCoop,cfg.contentHash);
    NetworkClient friend1("127.0.0.1",cfg.port,cfg.gameBuild,cfg.modHash,"wave-friend",SlotType::Friend1,{},RuntimeMode::CampaignCoop,cfg.contentHash);
    const auto wait=[&](auto predicate){
        const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while(!predicate()&&std::chrono::steady_clock::now()<end){
            relay.tick(0);host.tick(0);friend1.tick(0);host.sendHeartbeat();friend1.sendHeartbeat();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return predicate();
    };
    host.connect();friend1.connect();
    if(!wait([&]{return host.worldReady()&&friend1.worldReady();})){check(false,"wave-death peers ready");return;}
    host.sendRoomTransition({2,5,6,0,1,2,0});
    if(!wait([&]{return relay.currentRoom().has_value();})){check(false,"wave-death room ready");return;}
    StateHash full{2,5,6,hashAppliedEnemies({{1,301,63},{2,301,63},{3,301,63}}),2596620699u};
    full.nativeCensusComplete=true;full.nativeLivingCount=3;full.nativeCombatCount=3;
    auto wounded=full;wounded.enemiesHash=1804335653u; // recorded three Soldiers HP60
    auto empty=full;empty.enemiesHash=401581688u;empty.nativeLivingCount=0; // dying actors still listed
    std::uint64_t hostSeq=0,friendSeq=0;
    const auto publish=[&](NetworkClient& client,SlotType slot,const StateHash& hash,std::uint64_t& seq){
        client.sendStateHash(hash);++seq;
        return wait([&]{const auto* peer=relay.peerBySlot(slot);return peer&&peer->hashReceiptSeq==seq;});
    };
    bool delivered=publish(host,SlotType::Player,full,hostSeq)&&publish(friend1,SlotType::Friend1,full,friendSeq);
    // Observed ordering: HP60 client vs cached HP63 host, then empty client
    // vs cached HP60 host. Agreement arrives at the next native sample.
    delivered=delivered&&publish(friend1,SlotType::Friend1,wounded,friendSeq)&&
        publish(host,SlotType::Player,wounded,hostSeq)&&publish(friend1,SlotType::Friend1,empty,friendSeq)&&
        publish(host,SlotType::Player,empty,hostSeq)&&publish(friend1,SlotType::Friend1,empty,friendSeq);
    check(delivered&&relay.desyncNoticeCount()==0,"observed HP63/HP60/death sample ordering converges without false fields=2 notice");
    // The host returns to living and stays there while the friend stays empty.
    // Three rounds pass while the host's last four hashes still include empty
    // (lag), then the third counted sample reports.
    delivered=true;
    for(unsigned i=0;i<5;++i)delivered=delivered&&publish(host,SlotType::Player,wounded,hostSeq)&&publish(friend1,SlotType::Friend1,empty,friendSeq);
    check(delivered&&relay.desyncNoticeCount()==0,"enemy-only mismatches inside the lag window are debounced");
    delivered=publish(host,SlotType::Player,wounded,hostSeq)&&publish(friend1,SlotType::Friend1,empty,friendSeq);
    check(delivered&&relay.desyncNoticeCount()==1,"a real persistent living-versus-dead divergence still triggers on the third counted sample");
    host.disconnect();friend1.disconnect();relay.stop();
}
void hbPopulationDeathRegression() {
    SessionConfig cfg; cfg.bindAddress="127.0.0.1"; cfg.port=22122;
    SessionHost relay(cfg);
    if (!relay.start()) { check(false,"HB mismatch regression relay starts"); return; }
    NetworkClient host("127.0.0.1",cfg.port,cfg.gameBuild,cfg.modHash,"hb-host",SlotType::Player,{},RuntimeMode::CampaignCoop,cfg.contentHash);
    NetworkClient friend1("127.0.0.1",cfg.port,cfg.gameBuild,cfg.modHash,"hb-friend",SlotType::Friend1,{},RuntimeMode::CampaignCoop,cfg.contentHash);
    const auto wait=[&](auto predicate){
        const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while(!predicate()&&std::chrono::steady_clock::now()<end){
            relay.tick(0);host.tick(0);friend1.tick(0);host.sendHeartbeat();friend1.sendHeartbeat();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return predicate();
    };
    host.connect();friend1.connect();
    if(!wait([&]{return host.worldReady()&&friend1.worldReady();})){check(false,"HB mismatch peers ready");return;}
    host.sendRoomTransition({3,4,2,0,1,1,0});
    if(!wait([&]{return relay.currentRoom().has_value();})){check(false,"HB mismatch room ready");return;}
    std::uint64_t hostSeq=0,friendSeq=0;
    const auto publish=[&](NetworkClient& client,SlotType slot,const StateHash& hash,std::uint64_t& seq){
        client.sendStateHash(hash);++seq;
        return wait([&]{const auto* peer=relay.peerBySlot(slot);return peer&&peer->hashReceiptSeq==seq;});
    };
    // Preserved113901: two7/7 samples differ by bindings, then host frame3757
    // is live before kill and client1185 is empty after kill (dead actors6).
    StateHash live{3,4,2,0x50C3A129u,0xD0223C7Fu,true,7,7};
    auto unbound=live;unbound.enemiesHash=0x384EA84Cu;
    auto beforeKill=live;beforeKill.enemiesHash=0xC96A627Bu;
    auto afterKill=live;afterKill.enemiesHash=0x17EFA678u;afterKill.nativeLivingCount=0;afterKill.nativeCombatCount=6;
    auto empty=afterKill;empty.nativeCombatCount=0;
    bool delivered=publish(host,SlotType::Player,live,hostSeq);
    for(unsigned i=0;i<2;++i)delivered=delivered&&publish(friend1,SlotType::Friend1,unbound,friendSeq);
    delivered=delivered&&publish(host,SlotType::Player,beforeKill,hostSeq)&&publish(friend1,SlotType::Friend1,afterKill,friendSeq);
    check(delivered&&relay.desyncNoticeCount()==0&&relay.peerBySlot(SlotType::Friend1)->mismatchStreak==0,
          "HB7/7 binding lag against an unsettled host then7/0 death sample are not evidence");
    delivered=delivered&&publish(host,SlotType::Player,empty,hostSeq)&&publish(friend1,SlotType::Friend1,empty,friendSeq);
    check(delivered&&relay.desyncNoticeCount()==0,"observed HB empty lists converge cleanly");
    // First round settles the host; the next three count.
    for(unsigned i=0;i<4;++i){auto drift=unbound;drift.enemiesHash+=i;delivered=delivered&&publish(host,SlotType::Player,live,hostSeq)&&publish(friend1,SlotType::Friend1,drift,friendSeq);}
    check(delivered&&relay.desyncNoticeCount()==1,"changing client hashes against a settled host with persistent7/7 divergence still trigger");
    delivered=publish(friend1,SlotType::Friend1,live,friendSeq);
    for(unsigned i=0;i<4;++i){
        auto growing=beforeKill;growing.nativeLivingCount=7+i;growing.nativeCombatCount=7+i;growing.enemiesHash+=i;
        delivered=delivered&&publish(host,SlotType::Player,growing,hostSeq)&&publish(friend1,SlotType::Friend1,afterKill,friendSeq);
    }
    check(delivered&&relay.desyncNoticeCount()==2,"persistent unequal populations still trigger while the host's hash and count change");
    auto agreed=beforeKill;agreed.nativeLivingCount=10;agreed.nativeCombatCount=10;agreed.enemiesHash+=3;
    delivered=publish(friend1,SlotType::Friend1,agreed,friendSeq);
    for(unsigned i=0;i<8;++i){
        auto changing=unbound;changing.nativeLivingCount=(i%2)?9:10;changing.nativeCombatCount=10;changing.enemiesHash+=i;
        delivered=delivered&&publish(host,SlotType::Player,agreed,hostSeq)&&publish(friend1,SlotType::Friend1,changing,friendSeq);
    }
    check(delivered&&relay.desyncNoticeCount()==3,"alternating equal and unequal populations cannot conceal persistent enemy divergence");
    host.disconnect();friend1.disconnect();relay.stop();
}
// VUH-2270: three-player hashes recorded by run 20261014-205117 (TT 02/0D,
// active combat) and 20261014-203940 (BC 05/0B, Possessor death event).
struct Sample{std::uint32_t hash,living,combat;};
std::vector<Sample> expand(std::initializer_list<std::pair<Sample,unsigned>> runs){
    std::vector<Sample> out;for(const auto& [sample,count]:runs)out.insert(out.end(),count,sample);return out;
}
void routeCombatRegression() {
    SessionConfig cfg; cfg.bindAddress="127.0.0.1"; cfg.port=22123;
    SessionHost relay(cfg);
    if (!relay.start()) { check(false,"VUH-2270 relay starts"); return; }
    std::vector<DesyncNotice> seen;ClientCallbacks hc;hc.onDesyncNotice=[&](const auto& n){seen.push_back(n);};
    NetworkClient host("127.0.0.1",cfg.port,cfg.gameBuild,cfg.modHash,"route-host",SlotType::Player,hc,RuntimeMode::CampaignCoop,cfg.contentHash);
    NetworkClient friend1("127.0.0.1",cfg.port,cfg.gameBuild,cfg.modHash,"route-friend1",SlotType::Friend1,{},RuntimeMode::CampaignCoop,cfg.contentHash);
    NetworkClient friend2("127.0.0.1",cfg.port,cfg.gameBuild,cfg.modHash,"route-friend2",SlotType::Friend2,{},RuntimeMode::CampaignCoop,cfg.contentHash);
    std::array<NetworkClient*,3> peers{&host,&friend1,&friend2};
    std::array<std::uint64_t,3> seq{};
    const auto wait=[&](auto predicate){
        const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while(!predicate()&&std::chrono::steady_clock::now()<end){
            relay.tick(0);for(auto* p:peers){p->tick(0);p->sendHeartbeat();}
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return predicate();
    };
    for(auto* p:peers)p->connect();
    if(!wait([&]{return host.worldReady()&&friend1.worldReady()&&friend2.worldReady();})){check(false,"VUH-2270 peers ready");return;}
    std::uint32_t epoch=0;std::uint16_t world=0,room=0;
    const auto enter=[&](std::uint32_t e,std::uint16_t w,std::uint16_t r){
        epoch=e;world=w;room=r;host.sendRoomTransition({e,w,r,0,1,2,0});
        return wait([&]{return relay.currentRoom()&&relay.currentRoom()->epoch==e;});
    };
    const auto publish=[&](unsigned slot,const Sample& s){
        peers[slot]->sendStateHash(StateHash{epoch,world,room,s.hash,0x6A586217u,true,s.living,s.combat});++seq[slot];
        return wait([&]{const auto* p=relay.peerBySlot(static_cast<SlotType>(slot));return p&&p->hashReceiptSeq==seq[slot];});
    };
    const auto hostTT=expand({{{0x17EFA678u,0,0},1},{{0xA82D3BA3u,4,4},2},{{0x285DE5ABu,4,4},1},{{0x5F584DF3u,4,4},1},
        {{0xF6DDA44Bu,4,4},1},{{0x0FCC5B2Eu,4,4},3},{{0x495BD178u,3,4},1},{{0x665FCD27u,3,3},2},{{0x0FC25B92u,3,3},1},
        {{0x69BBCC07u,3,3},1},{{0x1C6F94D0u,2,3},1},{{0x1C6F94D0u,2,2},1},{{0x1FF8A798u,1,2},1},{{0x1FF8A798u,1,1},1},
        {{0x3237DB75u,1,1},1},{{0x17EFA678u,0,1},1},{{0x17EFA678u,0,0},2}});
    const auto friend1TT=expand({{{0x17EFA678u,0,0},1},{{0x285DE5ABu,4,4},1},{{0x5F584DF3u,4,4},1},{{0x43F79513u,4,4},1},
        {{0x0FCC5B2Eu,4,4},3},{{0x495BD178u,3,4},1},{{0x665FCD27u,3,3},2},{{0xA588CD6Fu,3,3},1},{{0x8092CADFu,3,3},1},
        {{0x1C6F94D0u,2,3},1},{{0x1C6F94D0u,2,2},1},{{0x1FF8A798u,1,2},1},{{0x1FF8A798u,1,1},1},{{0x41B31E42u,1,1},1},
        {{0x17EFA678u,0,1},1},{{0x17EFA678u,0,0},4}});
    const auto friend2TT=expand({{{0x17EFA678u,0,0},1},{{0x285DE5ABu,4,4},1},{{0x5F584DF3u,4,4},1},{{0x43F79513u,4,4},1},
        {{0x7B466EBAu,4,4},1},{{0x0FCC5B2Eu,4,4},2},{{0xAE644295u,3,4},1},{{0x665FCD27u,3,3},2},{{0xA588CD6Fu,3,3},1},
        {{0x8092CADFu,3,3},1},{{0x1C6F94D0u,2,3},1},{{0x1C6F94D0u,2,2},1},{{0x1FF8A798u,1,2},1},{{0x1FF8A798u,1,1},1},
        {{0xB399D3FDu,1,1},1},{{0x17EFA678u,0,1},1},{{0x17EFA678u,0,0},4}});
    check(hostTT.size()==22&&friend1TT.size()==22&&friend2TT.size()==22,"recorded TT 02/0D samples loaded");
    // Friend sample k shows host sample k+2's state. The relay compared it
    // with host sample k+1 (host 5F584DF3 vs friend 43F79513 at the notice):
    // friends one sample ahead of the cached host hash. Replay that skew,
    // friends in phase and one sample behind. Friends apply the host's HP, so
    // they can't run further ahead than a fresh host hash's age; a friend two
    // samples (~2 s) behind with HP the host never sampled may still report.
    std::uint32_t nextEpoch=2;
    for(std::size_t lead:{1u,2u,3u}){
        bool delivered=enter(nextEpoch++,2,0x0D);
        const auto before=relay.desyncNoticeCount();
        for(std::size_t k=0;k<lead;++k)delivered=delivered&&publish(0,hostTT[k]);
        for(std::size_t k=0;k<friend1TT.size();++k){
            // After the recording ends the host keeps hashing the cleared room.
            delivered=delivered&&publish(0,hostTT[std::min(k+lead,hostTT.size()-1)]);
            delivered=delivered&&publish(1,friend1TT[k])&&publish(2,friend2TT[k]);
        }
        const std::string label="TT 02/0D combat raises no notice with friends "+std::string(lead==1?"one sample ahead of the cached host (recorded)":
            lead==2?"in phase":"one sample behind");
        check(delivered&&relay.desyncNoticeCount()==before,label.c_str());
    }
    // BC 05/0B: the host hashes the living Possessor fight, then sends nothing
    // for ~10 s while the friends apply deaths and hash two living. Settle the
    // host first, the stricter case than the recorded one.
    bool delivered=enter(nextEpoch++,5,0x0B);
    auto before=relay.desyncNoticeCount();
    for(const Sample s:{Sample{0x239A6A44u,4,4},Sample{0xA8016BFBu,4,4},Sample{0x4CE2A1B7u,4,4},Sample{0x4CE2A1B7u,4,4}})
        delivered=delivered&&publish(0,s)&&publish(1,s)&&publish(2,s);
    for(const auto& s:expand({{{0x175290E1u,2,4},2},{{0x175290E1u,2,2},8}}))delivered=delivered&&publish(1,s)&&publish(2,s);
    check(delivered&&relay.desyncNoticeCount()==before,"friends applying deaths during a silent host boss event raise no notice");
    for(unsigned i=0;i<3;++i)delivered=delivered&&publish(0,{0xCAE36C8Cu,1,i?2u:4u})&&publish(1,{0xCAE36C8Cu,1,2})&&publish(2,{0xCAE36C8Cu,1,2});
    check(delivered&&relay.desyncNoticeCount()==before&&relay.peerBySlot(SlotType::Friend1)->mismatchStreak==0,"peers that agree after the event converge cleanly");
    // Real divergences still report against a fresh, settled host.
    delivered=enter(nextEpoch++,5,0x0B);
    const auto seenBefore=seen.size();
    for(unsigned i=0;i<4;++i)delivered=delivered&&publish(0,{0x4CE2A1B7u,4,4})&&publish(1,{0x239A6A44u,4,4})&&publish(2,{0x4CE2A1B7u,4,4});
    check(delivered&&wait([&]{return seen.size()==seenBefore+1;})&&seen.back().slot==SlotType::Friend1&&seen.back().fields==DesyncEnemies,
          "a persistent same-population HP divergence reports for the diverged friend only");
    delivered=enter(nextEpoch++,2,0x0D);
    const auto emptyHash=hashAppliedEnemies({});
    for(std::uint32_t i=0;i<3;++i)delivered=delivered&&publish(0,{0xA82D3BA3u+i,4,4})&&publish(1,{0xA82D3BA3u+i,4,4})&&publish(2,{emptyHash,0,0});
    check(delivered&&wait([&]{return seen.size()==seenBefore+2;})&&seen.back().slot==SlotType::Friend2&&seen.back().fields==(DesyncEnemies|DesyncMissingEnemies),
          "a missing living pack reports while the host's HP is still changing");
    for(auto* p:peers)p->disconnect();relay.stop();
}
int main(){
    if(enet_initialize())return 2;
    std::ofstream raw("relay-raw.log");std::vector<std::string> rows;
    const CausalSink sink=[&](const auto& row){rows.push_back(row);raw<<row<<'\n';raw.flush();return raw.good();};
    for(const char* setting:{"","0","01","true","2"," 1"}){
        _putenv_s("KH2COOP_CAUSAL_DIAGNOSTICS",setting);SessionConfig c;SessionCallbacks cb;cb.onHashDiagnostic=sink;
        SessionHost h(c,cb);h.sealHashDiagnostics();check(rows.empty()&&h.hashDiagnostics().seals==0,"only exact opt-in can activate callback/counters");
    }
    _putenv_s("KH2COOP_CAUSAL_DIAGNOSTICS","1");
    SessionConfig cfg;cfg.bindAddress="127.0.0.1";cfg.port=17974;cfg.gameBuild="hash-private";cfg.modHash="m";cfg.contentHash="c";
    SessionCallbacks cb;cb.onHashDiagnostic=sink;SessionHost relay(cfg,cb);check(relay.start(),"owned real ENet relay starts");
    std::array<std::unique_ptr<NetworkClient>,2> clients;unsigned notices=0,automaticRequests=0;std::vector<DesyncNotice> noticeRows;
    AutomaticResyncNotice automatic(true, [](const auto&) {});
    for(unsigned i=0;i<2;++i){ClientCallbacks cc;if(!i)cc.onDesyncNotice=[&](const auto& n){++notices;noticeRows.push_back(n);automatic.Notice(n,clients[0]->hostResyncContext(),false,1,[&](auto,auto*){++automaticRequests;return false;});};
        clients[i]=std::make_unique<NetworkClient>("127.0.0.1",cfg.port,cfg.gameBuild,cfg.modHash,"hash"+std::to_string(i),static_cast<SlotType>(i),cc,RuntimeMode::CampaignCoop,cfg.contentHash);check(clients[i]->connect(),"owned client connects");}
    const auto wait=[&](auto predicate){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(4);while(!predicate()&&std::chrono::steady_clock::now()<end){relay.tick(0);for(auto& c:clients){c->tick(0);c->sendHeartbeat();}std::this_thread::sleep_for(std::chrono::milliseconds(1));}return predicate();};
    check(wait([&]{return clients[0]->worldReady()&&clients[1]->worldReady();}),"real bindings admitted");
    clients[0]->sendRoomTransition({7,5,6,3,4,6,8});
    check(wait([&]{return relay.currentRoom().has_value();}),"room established");
    StateHash host{7,5,6,111,222},empty{7,5,6,hashAppliedEnemies({}),222},changed{7,5,6,333,222};
    // The relay counts enemy mismatches only against a settled host hash and
    // at most two client samples after it (VUH-2270), so the host publishes
    // twice up front and again between client samples, as its cadence does.
    std::uint64_t hostSeq=0,friendSeq=0;
    const auto hostSends=[&](const StateHash& hash){clients[0]->sendStateHash(hash);++hostSeq;return wait([&]{auto p=relay.peerBySlot(SlotType::Player);return p&&p->hashReceiptSeq==hostSeq;});};
    const auto friendSends=[&](const StateHash& hash){clients[1]->sendStateHash(hash);++friendSeq;return wait([&]{auto p=relay.peerBySlot(SlotType::Friend1);return p&&p->hashReceiptSeq==friendSeq;});};
    const auto streak=[&]{return relay.peerBySlot(SlotType::Friend1)->mismatchStreak;};
    check(hostSends(host)&&hostSends(host),"settled host hash received");
    check(friendSends(empty),"client first complete-empty fixture hash received");
    check(!relay.desyncNoticeCount()&&streak()==1,"one mismatch is not a notice");
    check(hostSends(host),"third host publication admitted");
    check(!relay.desyncNoticeCount()&&streak()==1,"cached client observation cannot advance the debounce");
    check(friendSends(empty),"second fresh enemy mismatch is debounced");
    check(!relay.desyncNoticeCount(),"two enemy-only mismatches do not emit a notice");
    friendSends(empty);check(wait([&]{return notices==1;}),"third fresh client publication emits exactly one notice");
    std::string notice;std::vector<std::string> compares;
    for(const auto& row:rows){if(row.starts_with("[relay-hash]")&&field(row,"action")=="notice")notice=row;if(field(row,"action")=="compare")compares.push_back(row);}
    check(compares.size()==3&&field(compares[0],"receiveSeq")=="1"&&field(compares[1],"receiveSeq")=="2"&&field(notice,"receiveSeq")=="3","three mismatch witnesses identify distinct fresh client publications");
    check(field(notice,"previousCompare")==field(compares[1],"seq")&&field(notice,"compare")==field(compares[2],"seq")&&field(notice,"latestReceived")=="1","notice links the latest real comparisons and received identity");
    check(field(notice,"enemiesHash")==std::to_string(empty.enemiesHash)&&field(notice,"connection")==std::to_string(clients[1]->worldBinding()->selfConnectionId)&&field(notice,"hostEnemiesHash")=="111","exact H and full current connection are preserved at notice");
    check(field(notice,"sourceConnection")==field(notice,"connection")&&field(notice,"scopeAvailable")=="1"&&field(notice,"sourceDelivery")=="1"&&field(notice,"hostSource")=="0"&&field(notice,"hostSourceDelivery")=="1"&&field(notice,"hostHostSource")!="0","exact admitted envelope provenance survives both cached comparisons; client source remains honestly zero");
    check(hostSends(host)&&friendSends(changed),"host sample then new nonempty client hash received");
    std::string latest;for(const auto& row:rows)if(field(row,"action")=="compare")latest=row;
    check(field(latest,"receiveSeq")=="4"&&field(latest,"enemiesHash")=="333"&&notices==1&&relay.desyncNoticeCount()==1,"newest received nonzero hash is used, repeated fields remain coalesced");
    check(friendSends(host)&&streak()==0,"agreement resets mismatch streak");
    check(hostSends(host)&&friendSends(empty)&&streak()==0,"a client that showed the host hash one sample ago is lag, not evidence");
    check(friendSends(empty)&&streak()==1,"new empty mismatch first counted comparison");
    check(hostSends(host)&&friendSends(changed),"newer client hash received");
    friendSends(changed);check(wait([&]{return notices==2;}),"third mismatch cites newer hash at actual emission");
    for(const auto& row:rows)if(row.starts_with("[relay-hash]")&&field(row,"action")=="notice")notice=row;
    check(field(notice,"enemiesHash")=="333"&&field(notice,"receiveSeq")==std::to_string(friendSeq),"cannot qualify outside from older empty publication when notice uses newer nonempty H");
    check(automaticRequests==0,"unknown/incomplete census summaries never automatically reload");
    host.nativeCensusComplete=true;host.nativeLivingCount=5;host.nativeCombatCount=5;
    changed.nativeCensusComplete=true;changed.nativeLivingCount=5;changed.nativeCombatCount=5;
    check(hostSends(host)&&hostSends(host)&&friendSends(host)&&streak()==0,"complete native population agrees before HP-lag control");
    check(hostSends(host)&&friendSends(changed)&&friendSends(changed)&&streak()==1,"HP-only mismatch counts after the one-sample lag grace");
    check(hostSends(host)&&friendSends(changed),"HP-only mismatch continues against a fresh settled host");
    friendSends(changed);check(wait([&]{return notices==3;}),"persistent HP-only mismatch remains a diagnostic notice");
    check(automaticRequests==0&&!noticeRows.empty()&&!(noticeRows.back().fields&DesyncMissingEnemies),"HP lag with all five actors cannot request a room reload");
    empty.nativeCensusComplete=true;empty.nativeLivingCount=0;
    check(hostSends(host)&&friendSends(empty),"first complete-empty observation received");
    check(notices==3&&relay.desyncNoticeCount()==3,"a client that had the host's population one sample ago is lag, not evidence");
    check(friendSends(empty)&&streak()==1,"empty-pack hint starts a fresh two-observation debounce");
    check(hostSends(host),"host replay admitted during empty debounce");
    check(notices==3,"cached empty observation still cannot trigger");
    friendSends(empty);
    check(wait([&]{return notices==4;}),"two fresh complete-empty reports identify missing living pack");
    check(automaticRequests==1&&noticeRows.back().fields==(DesyncEnemies|DesyncMissingEnemies),"only missing complete living pack reaches automatic request generator");
    check(friendSends(host)&&streak()==0,"agreement clears missing-pack hint");
    auto dead=empty;dead.nativeCombatCount=1;
    check(hostSends(host)&&friendSends(dead)&&friendSends(dead)&&hostSends(host)&&friendSends(dead),"dead-actor observations received");
    friendSends(dead);
    check(wait([&]{return notices==5;}),"complete zero-living census with a dead actor remains diagnostic");
    check(automaticRequests==1&&noticeRows.back().fields==DesyncEnemies,"dead or dying native actors cannot request a respawn");
    for(auto& c:clients)c->disconnect();relay.stop();
    check(field(rows.back(),"action")=="shutdown"&&field(rows.back(),"dropped")=="0"&&field(rows.back(),"highWater")==field(rows.back(),"flushedHighWater"),"actual flush-backed terminal seal complete");
    CausalStream stream;stream.emit({},"control",[](auto&){});stream.seal({},"control","interval");check(!stream.highWater&&!stream.seals,"absent sink has no counters");
    stream.emit([](const auto&){return false;},"control",[](auto&){});check(stream.unavailable&&stream.dropped==1&&!stream.flushed,"false flush result is sticky gap");
    stream.emit(sink,"control",[](auto&){throw 1;});check(stream.dropped==2&&stream.highWater==2,"formatter failure consumes sequence and marks gap");
    stream.highWater=CausalStream::Limit;stream.emit(sink,"control",[](auto&){});check(stream.highWater==CausalStream::Limit&&stream.dropped==3,"bounded sequence never wraps");
    stream.seals=UINT64_MAX;stream.seal(sink,"control","interval");check(stream.seals==UINT64_MAX&&stream.dropped==4,"seal sequence never wraps");
    waveDeathRegression();
    hbPopulationDeathRegression();
    routeCombatRegression();
    enet_deinitialize();std::cout<<"checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}
