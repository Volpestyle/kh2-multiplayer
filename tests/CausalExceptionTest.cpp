#include "WorldWireFixture.hpp"
#include "kh2coop/PeerState.hpp"
#include "kh2coop/DesyncCapture.hpp"
#include <map>
#include <set>
#include <new>
#include <cstdlib>
#define private public
#include "kh2coop/SessionHost.hpp"
#undef private
#define main existingForcedMain
#include "ForcedResyncTest.cpp"
#undef main

// One-shot, test-only allocation failure. No production allocator or seam changes.
static int allocationFault = 0;
void* operator new(std::size_t size) {
    const int fault=allocationFault; allocationFault=0;
    if(fault==1)throw std::bad_alloc();
    if(fault==2)throw 17;
    if(void* p=std::malloc(size?size:1))return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete[](void* p,std::size_t) noexcept { std::free(p); }

void actualCacheException(bool enabled, std::uint16_t port) {
    Rig r(port);check(r.prime(),"actual loopback cache fixture ready");
    std::vector<std::string> rows;
    if(enabled)r.relay->callbacks_.onCausalDiagnostic=[&](const std::string& row){rows.push_back(row);std::cout<<row<<std::endl;return std::cout.good();};
    r.relay->sealCacheDiagnostics("begin");
    EnemyManifest m;m.epoch=7;m.replace=false;
    for(unsigned i=0;i<1409;++i){auto e=snapshot().enemies[0].identity;e.netId=static_cast<std::uint16_t>(i+2);m.entries.push_back(e);}
    r.clients[0]->sendEnemyManifest(m);check(r.barrier(),"first legal manifest admitted through actual transport");
    for(auto& e:m.entries)e.netId+=1409;
    r.clients[0]->sendEnemyManifest(m);check(r.barrier(),"second legal manifest admitted through actual transport");
    check(r.relay->manifest_.entries.size()==2819,"actual merged cache has 2819 entries");
    std::cout<<"ACTUAL_MERGED_CACHE_ENTRIES="<<r.relay->manifest_.entries.size()<<" diagnostics="<<enabled<<std::endl;
    bool threw=false;
    r.relay->callbacks_.onLog=[&](const std::string& row){std::cout<<row<<std::endl;if(row.find("Packet decode error")!=std::string::npos&&row.find("resync: invalid bounded record")!=std::string::npos)threw=true;};
    r.clients[2]->disconnect();check(r.wait([&]{return r.relay->peerBySlot(SlotType::Friend2)==nullptr;}),"actual target disconnected");
    check(r.clients[2]->connect(),"actual target reconnect initiated");
    check(r.wait([&]{return threw;}),"original envelope exception reaches existing receive handler unchanged");
    r.relay->sealCacheDiagnostics();
    const auto& d=r.relay->cacheDiagnostics();
    check(enabled ? (d.dropped==1&&d.unavailable&&d.highWater==2&&d.flushed==2) : (!d.dropped&&!d.unavailable&&!d.highWater&&!d.flushed&&!d.seals),
          "actual cached envelope exception invalidates installed coverage; disabled counters remain zero");
    std::cout<<"REPRODUCED_SILENT_CACHE_ATTEMPT_GAP="<<(enabled&&threw&&!d.dropped&&!d.unavailable&&d.highWater==d.flushed)<<std::endl;

    // Synthetic boundary only: body encoding throws before sendTo is entered.
    r.relay->manifest_.entries.resize(65536);
    const auto droppedBefore=d.dropped;const auto highBefore=d.highWater;
    bool bodyThrew=false;
    r.relay->callbacks_.onLog=[&](const std::string& row){std::cout<<row<<std::endl;if(row.find("Packet decode error")!=std::string::npos&&row.find("EnemyManifest too large")!=std::string::npos)bodyThrew=true;};
    r.clients[2]->disconnect();check(r.wait([&]{return r.relay->peerBySlot(SlotType::Friend2)==nullptr;}),"synthetic body control target disconnects");
    check(r.clients[2]->connect(),"synthetic body control target reconnects");
    check(r.wait([&]{return bodyThrew;}),"synthetic oversized cached body preserves original exception to receive handler");
    check(enabled ? (d.dropped==droppedBefore+1&&d.unavailable&&d.highWater==highBefore+2) : (!d.dropped&&!d.unavailable&&!d.highWater&&!d.seals),
          "pre-sendTo body exception is covered without changing no-sink behavior");
    r.relay->sealCacheDiagnostics();
    if(enabled)check(rows.back().find("dropped=2 unavailable=1")!=std::string::npos,"later successful seal preserves sticky body and envelope gaps");
}

void requestExceptions(bool enabled,std::uint16_t port) {
    Rig r(port);check(r.prime(),"request exception loopback fixture ready");
    auto& host=*r.clients[0];std::vector<std::string> rows;
    if(enabled)host.callbacks_.onCausalDiagnostic=[&](const std::string& row){rows.push_back(row);std::cout<<row<<std::endl;return std::cout.good();};
    ResyncRequest generated;
    const auto nextBefore=host.nextResyncRequest_;
    bool generatorThrew=false;
    allocationFault=1;
    try{host.requestWorldResync(2,&generated,ResyncRequestOrigin::OperatorMailbox);}catch(const std::bad_alloc&){generatorThrew=true;}
    allocationFault=0;
    check(generatorThrew&&host.nextResyncRequest_==nextBefore&&!generated.key.requestId,"generator allocation failure propagates without inventing allocated key");
    const auto& d=host.requestDiagnostics();
    check(enabled ? (d.dropped==1&&d.unavailable&&!d.highWater) : (!d.dropped&&!d.unavailable&&!d.highWater),"generator escaping exception accounts sticky gap only when sink installed");
    ResyncRequest request;request.key={host.avatarSessionId_,host.avatarConnections_[0],nextBefore};request.room=*host.hostRoom_;request.targetMask=2;
    for(std::size_t i=0;i<3;++i)request.connections[i]=host.avatarConnections_[i];
    allocationFault=1;const bool directResult=host.sendResyncRequest(request);allocationFault=0;
    check(!directResult&&!host.requestedResync_,"direct standard encoding exception retains original false return and no request mutation");
    check(enabled ? (d.highWater==1&&d.dropped==1&&rows.back().find("disposition=submission-failed")!=std::string::npos&&rows.back().find("deadlineAvailable=0")!=std::string::npos) : (!d.highWater&&!d.dropped),
          "existing caught direct exception emits failed supplied key outcome without silent gap");
    bool directThrew=false;
    allocationFault=2;
    try{host.sendResyncRequest(request);}catch(int value){directThrew=value==17;}
    allocationFault=0;
    check(directThrew&&!host.requestedResync_,"direct nonstandard exception propagates unchanged");
    check(enabled ? (d.highWater==1&&d.dropped==2&&d.unavailable) : (!d.highWater&&!d.dropped&&!d.unavailable),"escaping direct exception invalidates installed coverage only");
    host.sealRequestDiagnostics();
    if(enabled)check(rows.back().find("dropped=2 unavailable=1")!=std::string::npos,"request exception gaps persist in flushed seal");
}

int main(){
    if(enet_initialize()!=0)return 2;
    actualCacheException(true,19965);actualCacheException(false,19966);
    requestExceptions(true,19967);requestExceptions(false,19968);
    enet_deinitialize();
    std::cout<<checks-failures<<" PASS, "<<failures<<" FAIL\n";return failures?1:0;
}
