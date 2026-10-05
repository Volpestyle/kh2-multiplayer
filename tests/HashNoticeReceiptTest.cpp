#include "WorldWireFixture.hpp"
#include "kh2coop/SessionHost.hpp"
#include "kh2coop/AppliedStateHash.hpp"
#include <fstream>
#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
using namespace kh2coop;
int checks=0, failures=0;
void check(bool yes,const char* label){++checks;failures+=!yes;std::cout<<(yes?"PASS ":"FAIL ")<<label<<'\n';}
std::string field(const std::string& row,const std::string& key){const auto p=" "+key+"=";auto at=row.find(p);if(at==std::string::npos)return {};at+=p.size();return row.substr(at,row.find(' ',at)-at);}
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
    std::array<std::unique_ptr<NetworkClient>,2> clients;unsigned notices=0;
    for(unsigned i=0;i<2;++i){ClientCallbacks cc;if(!i)cc.onDesyncNotice=[&](const auto&){++notices;};
        clients[i]=std::make_unique<NetworkClient>("127.0.0.1",cfg.port,cfg.gameBuild,cfg.modHash,"hash"+std::to_string(i),static_cast<SlotType>(i),cc,RuntimeMode::CampaignCoop,cfg.contentHash);check(clients[i]->connect(),"owned client connects");}
    const auto wait=[&](auto predicate){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(4);while(!predicate()&&std::chrono::steady_clock::now()<end){relay.tick(0);for(auto& c:clients){c->tick(0);c->sendHeartbeat();}std::this_thread::sleep_for(std::chrono::milliseconds(1));}return predicate();};
    check(wait([&]{return clients[0]->worldReady()&&clients[1]->worldReady();}),"real bindings admitted");
    clients[0]->sendRoomTransition({7,5,6,3,4,6,8});
    check(wait([&]{return relay.currentRoom().has_value();}),"room established");
    StateHash host{7,5,6,111,222},empty{7,5,6,hashAppliedEnemies({}),222},changed{7,5,6,333,222};
    clients[0]->sendStateHash(host);check(wait([&]{auto p=relay.peerBySlot(SlotType::Player);return p&&p->hasHash;}),"host hash received");
    clients[1]->sendStateHash(empty);check(wait([&]{auto p=relay.peerBySlot(SlotType::Friend1);return p&&p->hashReceiptSeq==1;}),"client first complete-empty fixture hash received");
    check(!notices,"one mismatch is not a notice");
    clients[0]->sendStateHash(host);check(wait([&]{return notices==1;}),"second host publication compares cached latest client and emits exactly one notice");
    std::string notice;std::vector<std::string> compares;
    for(const auto& row:rows){if(row.starts_with("[relay-hash]")&&field(row,"action")=="notice")notice=row;if(field(row,"action")=="compare")compares.push_back(row);}
    check(compares.size()==2&&field(compares[0],"receiveSeq")=="1"&&field(compares[1],"receiveSeq")=="1"&&field(notice,"receiveSeq")=="1","both actual mismatches identify the same cached client publication honestly");
    check(field(notice,"previousCompare")==field(compares[0],"seq")&&field(notice,"compare")==field(compares[1],"seq")&&field(notice,"latestReceived")=="1","notice links both real comparisons and latest received identity");
    check(field(notice,"enemiesHash")==std::to_string(empty.enemiesHash)&&field(notice,"connection")==std::to_string(clients[1]->worldBinding()->selfConnectionId)&&field(notice,"hostEnemiesHash")=="111","exact H and full current connection are preserved at notice");
    check(field(notice,"sourceConnection")==field(notice,"connection")&&field(notice,"scopeAvailable")=="1"&&field(notice,"sourceDelivery")=="1"&&field(notice,"hostSource")=="0"&&field(notice,"hostSourceDelivery")=="1"&&field(notice,"hostHostSource")!="0","exact admitted envelope provenance survives both cached comparisons; client source remains honestly zero");
    clients[1]->sendStateHash(changed);check(wait([&]{return relay.peerBySlot(SlotType::Friend1)->hashReceiptSeq==2;}),"new nonempty hash received");
    const auto before=rows.size();clients[0]->sendStateHash(host);check(wait([&]{return rows.size()>before+1;}),"host triggers next cached comparison");
    std::string latest;for(const auto& row:rows)if(field(row,"action")=="compare")latest=row;
    check(field(latest,"receiveSeq")=="2"&&field(latest,"enemiesHash")=="333"&&notices==1,"newest received nonzero hash is used, repeated fields remain coalesced");
    clients[1]->sendStateHash(host);check(wait([&]{return relay.peerBySlot(SlotType::Friend1)->mismatchStreak==0;}),"agreement resets mismatch streak");
    clients[1]->sendStateHash(empty);check(wait([&]{return relay.peerBySlot(SlotType::Friend1)->mismatchStreak==1;}),"new empty mismatch first comparison");
    clients[1]->sendStateHash(changed);check(wait([&]{return notices==2;}),"second mismatch cites newer hash at actual emission");
    for(const auto& row:rows)if(row.starts_with("[relay-hash]")&&field(row,"action")=="notice")notice=row;
    check(field(notice,"enemiesHash")=="333"&&field(notice,"receiveSeq")=="5","cannot qualify outside from older empty publication when notice uses newer nonempty H");
    for(auto& c:clients)c->disconnect();relay.stop();
    check(field(rows.back(),"action")=="shutdown"&&field(rows.back(),"dropped")=="0"&&field(rows.back(),"highWater")==field(rows.back(),"flushedHighWater"),"actual flush-backed terminal seal complete");
    CausalStream stream;stream.emit({},"control",[](auto&){});stream.seal({},"control","interval");check(!stream.highWater&&!stream.seals,"absent sink has no counters");
    stream.emit([](const auto&){return false;},"control",[](auto&){});check(stream.unavailable&&stream.dropped==1&&!stream.flushed,"false flush result is sticky gap");
    stream.emit(sink,"control",[](auto&){throw 1;});check(stream.dropped==2&&stream.highWater==2,"formatter failure consumes sequence and marks gap");
    stream.highWater=CausalStream::Limit;stream.emit(sink,"control",[](auto&){});check(stream.highWater==CausalStream::Limit&&stream.dropped==3,"bounded sequence never wraps");
    stream.seals=UINT64_MAX;stream.seal(sink,"control","interval");check(stream.seals==UINT64_MAX&&stream.dropped==4,"seal sequence never wraps");
    enet_deinitialize();std::cout<<"checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}
