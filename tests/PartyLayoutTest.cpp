#include "PartyNetworkFixture.hpp"
#include <enet/enet.h>
#include <iostream>
#include <thread>
#include <chrono>
using namespace kh2coop;
namespace {
int failures=0;
void check(bool b,const char* s){std::cout<<(b?"PASS ":"FAIL ")<<s<<'\n';if(!b)++failures;}
void rules(){
    RoomTransition room{1,8,12,0,0,1,0};
    for(unsigned mask=0;mask<4;++mask)for(unsigned r=0;r<7;++r)for(bool reverse:{false,true}){
        std::array<std::uint64_t,3> ids{11,mask&1?12u:0u,mask&2?13u:0u};
        const auto rule=static_cast<PartyRule>(r);
        auto layout=defaultPartyLayout(room,1,PartyApplyReason::HostChoice,rule,partyForcedAlly(rule)?1234:0,ids,reverse?std::array<std::uint8_t,2>{2,1}:std::array<std::uint8_t,2>{1,2});
        check(layout && validPartyLayout(*layout,ids),"all authored rules / present-player sets / priorities satisfy policy");
        if(!layout)continue;
        const auto bytes=encode(*layout);const std::uint8_t* p=nullptr;std::size_t n=0;
        decodePacketHeader(bytes.data(),bytes.size(),p,n);ByteReader reader(p,n);PartyLayout decoded;read(reader,decoded);
        check(n==68 && reader.atEnd() && encode(decoded)==bytes,"party exact codec roundtrip");
    }
    std::array<std::uint64_t,3> ids{11,12,13};
    auto base=*defaultPartyLayout(room,1,PartyApplyReason::HostChoice,PartyRule::WorldOptional,0,ids);
    check(base.seats[1].playerSlot==1 && base.seats[2].playerSlot==2,"players replace optional ally and AI");
    auto one=*defaultPartyLayout(room,1,PartyApplyReason::HostChoice,PartyRule::Default,0,{11,12,0});
    check(one.seats[1].kind==PartyMemberKind::RemotePlayer && one.seats[2].kind==PartyMemberKind::Goofy,"default guest takes Donald seat alongside Goofy");
    auto bad=base;bad.seats[2]=bad.seats[1];check(!validPartyLayout(bad,ids),"one remote player cannot occupy two seats");
    bad=base;bad.seats[1]={PartyMemberKind::Donald,0xFF,0};check(!validPartyLayout(bad,ids),"AI cannot displace present player from legal seat");
    bad=base;bad.seats[1]={PartyMemberKind::WorldAlly,0xFF,1234};check(!validPartyLayout(bad,ids),"optional world ally is benched by chosen policy");
    bad=base;bad.seats[0]={};check(!validPartyLayout(bad,ids),"host local primary cannot disappear");
    check(!defaultPartyLayout(room,1,PartyApplyReason::HostChoice,PartyRule::WorldFixed,0,ids),"forced ally requires actual model identity");
    check(!defaultPartyLayout(room,1,PartyApplyReason::HostChoice,static_cast<PartyRule>(7),0,ids),"unknown native rule refused, no guessed default");
    bad=base;bad.connections[2]=99;check(!validPartyLayout(bad,ids),"layout pins entire roster including benched peers");
    auto bytes=encode(base);bytes.push_back(0);bool rejected=false;
    try{const std::uint8_t* p;std::size_t n;decodePacketHeader(bytes.data(),bytes.size(),p,n);}catch(const std::exception&){rejected=true;}
    check(rejected,"trailing party frame refused");
    auto notification=encode(PartyReapply{room,9,PartyApplyReason::StoryForced});
    const std::uint8_t* p;std::size_t n;decodePacketHeader(notification.data(),notification.size(),p,n);ByteReader reader(p,n);PartyReapply reapply;read(reader,reapply);
    check(n==25 && reapply.afterVersion==9 && reapply.reason==PartyApplyReason::StoryForced,"reapply exact codec roundtrip");
}
void network(){
    SessionConfig config;config.bindAddress="127.0.0.1";config.port=29875;config.gameBuild="party-test";config.modHash="none";
    SessionHost server(config);if(!server.start()){check(false,"ENet host starts");return;}
    std::array<std::unique_ptr<NetworkClient>,3> clients;unsigned invalidations=0,layouts=0;
    for(unsigned i=0;i<3;++i){ClientCallbacks cb;cb.onPartyLayout=[&](const auto&){++layouts;};cb.onPartyReapply=[&](const auto&){++invalidations;};
        clients[i]=std::make_unique<NetworkClient>("127.0.0.1",config.port,"party-test","none","party"+std::to_string(i),static_cast<SlotType>(i),cb);clients[i]->connect();}
    auto pump=[&]{for(unsigned n=0;n<30;++n){server.tick();for(auto& c:clients)c->tick();std::this_thread::sleep_for(std::chrono::milliseconds(1));}};pump();pump();
    if(server.verifiedPeerCount()!=3){check(false,"three peers admitted");return;}
    partyNetworkChecks(server,{clients[0].get(),clients[1].get(),clients[2].get()},pump,check);
    check(layouts==9 && invalidations>=6,"typed apply callbacks are once per new version and carry invalidations");
    clients[2]->disconnect();pump();
    check(!server.partyLayout() && !clients[0]->partyLayout() && !clients[1]->partyLayout(),"departure retires layout without substituting native AI");
    clients[2]->connect();pump();pump();
    check(clients[2]->ready() && !clients[2]->partyLayout(),"rejoin cannot replay party from previous connection incarnation");
    std::array<std::uint64_t,3> ids{};for(unsigned i=0;i<3;++i)ids[i]=server.peerBySlot(static_cast<SlotType>(i))->connectionId;
    auto fresh=*defaultPartyLayout(*server.currentRoom(),4,PartyApplyReason::RosterChanged,PartyRule::Default,0,ids);
    clients[0]->sendPartyLayout(fresh);pump();check(clients[2]->partyLayout() && clients[2]->partyLayout()->connections==ids,"fresh host layout admits rejoined owner with new exact connection");
    for(auto& c:clients)c->disconnect();server.stop();
}
}
int main(){if(enet_initialize())return 2;rules();network();enet_deinitialize();std::cout<<"failures="<<failures<<'\n';return failures?1:0;}
