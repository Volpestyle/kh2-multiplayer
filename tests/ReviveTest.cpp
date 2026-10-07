#include "ReviveNetworkFixture.hpp"
#include "kh2coop/Revive.hpp"
#ifdef _WIN32
#include "kh2coop/AvatarBridge.hpp"
#include "kh2coop/WorldPump.hpp"
#endif
#include <enet/enet.h>
#include <chrono>
#include <iostream>
#include <thread>
using namespace kh2coop;
namespace {
int errors=0;
void check(bool ok,const char* text){std::cout<<(ok?"PASS ":"FAIL ")<<text<<'\n';if(!ok)++errors;}
void unitChecks(){
    ReviveRequest r{{7,5,4,0,0,1,0},42,11,12,9,1,2};
    const auto bytes=encode(r); const std::uint8_t* p=nullptr;std::size_t n=0;
    check(decodePacketHeader(bytes.data(),bytes.size(),p,n)==PacketType::ReviveRequest && n==50,"revive fixed wire payload");
    ByteReader reader(p,n);ReviveRequest copy;read(reader,copy);
    check(encode(copy)==bytes && reader.atEnd(),"revive exact nonzero codec roundtrip");
    for(int delta:{-1,1}){auto bad=bytes;if(delta<0)bad.pop_back();else bad.push_back(0);
        bool refused=false;try{decodePacketHeader(bad.data(),bad.size(),p,n);}catch(const std::exception&){refused=true;}
        check(refused,"truncated or trailing revive refused");}
    AvatarState avatar;avatar.worldId=5;avatar.roomId=4;avatar.hp=0;avatar.maxHp=20;
    LocalDownedState state{{1,2,0},7,5,4,0,0,1,0,9,1000,true};
    projectLocalDowned(avatar,state,{1,2,0},1000);
    check((avatar.flags&AvatarDowned) && avatar.downedEpoch==7 && avatar.downedEpisode==9,"checked native setter projection streams downed episode");
    auto wire=encode(avatar,PacketType::AvatarState);decodePacketHeader(wire.data(),wire.size(),p,n);
    ByteReader ar(p,n);AvatarState decoded;read(ar,decoded);
    check(decoded.downedEpisode==9 && decoded.downedDelivery==2 && decoded.downedEpoch==7 && (decoded.flags&AvatarDowned),"downed avatar codec roundtrip");
    projectLocalDowned(avatar,state,{2,2,0},1000);check(!avatar.downedEpoch && !(avatar.flags&AvatarDowned),"retired native generation clears streamed authority");
    projectLocalDowned(avatar,state,{1,3,0},1000);check(!avatar.downedEpoch,"retired delivery clears authority");
    projectLocalDowned(avatar,state,{1,2,0},2001);check(!avatar.downedEpoch,"stale setter fails closed");
    projectLocalDowned(avatar,state,{1,2,0},999);check(!avatar.downedEpoch,"future timestamp fails closed");
    state.downed=false;projectLocalDowned(avatar,state,{1,2,0},1001);
    check(avatar.downedEpoch==7 && !(avatar.flags&AvatarDowned),"alive setter clears downed but retains current epoch");state.downed=true;
    ReviveOwnerGate owner;
    WorldScope scope{"session",11,1,0,12,2,WorldSourceKind::Native};
    const std::array<std::uint64_t,3> ids{10,11,12};
    auto consume=[&]{return owner.Consume(r,scope,"session",{1,2,0},ids,2,state,1000);};
    r.targetEpisode=8;check(!consume(),"old downed episode cannot authorize new native death");r.targetEpisode=9;
    scope.targetDeliverySerial=1;check(!consume(),"owner rejects retired target delivery");scope.targetDeliverySerial=2;
    scope.sessionId="old";check(!consume(),"owner rejects retired session");scope.sessionId="session";
    state.downed=false;check(!consume(),"owner refuses standing target even after wire admission");state.downed=true;
    check(consume(),"fresh owner consume grants one native attempt");
    check(!consume(),"native attempt cannot be applied twice");
    r.seq++;r.requesterConnectionId=10;r.requesterSlot=0;scope.sourceConnectionId=10;
    check(!consume(),"competing teammate cannot consume already reserved native episode");
    r.targetEpisode=10;state.episode=10;check(consume(),"strictly newer native episode remains usable");
    AvatarState a,b;a.position.x=REVIVE_RANGE;
    check(reviveInRange(a,b),"range inclusive exact boundary");a.position.y=1;
    check(!reviveInRange(a,b),"range is three-dimensional");
#ifdef _WIN32
    AvatarBridge dll,runtime;
    check(dll.Open(GetCurrentProcessId())&&runtime.OpenExisting(GetCurrentProcessId()),"actual v4 mapping opens for owned test process");
    dll.SetLocalDownedState(state);LocalDownedState snapshot;
    check(runtime.ReadLocalDownedState(snapshot)&&snapshot.episode==10&&snapshot.downed,"named setter crosses actual bridge mapping");
    check(runtime.ReadLocalDownedState(snapshot),"unchanged state can be snapshotted without stale-value cache");
#endif
}
void enetChecks(){
    SessionConfig config;config.bindAddress="127.0.0.1";config.port=29874;
    config.gameBuild="revive-test";config.modHash="none";
    SessionHost server(config);if(!server.start()){check(false,"ENet bind");return;}
    std::array<std::vector<ReviveRequest>,3> received;
#ifdef _WIN32
    WorldBridge bridge;WorldPumpStats stats;WorldInbox inbox;
    check(bridge.Open(GetCurrentProcessId()),"owned test WorldBridge opens");
    const auto generation=bridge.AdvanceSessionGeneration();
    bridge.SetDeliverySerial(1);
#endif
    std::array<std::unique_ptr<NetworkClient>,3> clients;
    for(unsigned i=0;i<3;++i){ClientCallbacks cb;cb.onReviveRequest=[&,i](const auto& r){received[i].push_back(r);};
#ifdef _WIN32
        cb.onWorldEnvelope=[&,i](const WorldEnvelope& e){
            if(i==2 && e.packet.front()==static_cast<std::uint8_t>(PacketType::ReviveRequest))
                inbox.Receive(bridge,encode(e),stats);
        };
#endif
        clients[i]=std::make_unique<NetworkClient>("127.0.0.1",config.port,"revive-test","none","peer"+std::to_string(i),static_cast<SlotType>(i),cb);
        clients[i]->connect();}
    auto pump=[&]{for(unsigned i=0;i<30;++i){server.tick();for(auto& c:clients)c->tick();std::this_thread::sleep_for(std::chrono::milliseconds(1));}};
    pump();pump();check(server.verifiedPeerCount()==3,"three peers admitted through real ENet loopback");
    if(server.verifiedPeerCount()!=3)return;
    reviveNetworkChecks(server,{clients[0].get(),clients[1].get(),clients[2].get()},received,pump,check);
    // Standing requester and a different fresh downing, then no more avatar
    // publication. Receipt clocks (not sender's supplied time) expire authority.
    AvatarState a;a.worldId=5;a.roomId=4;a.downedEpoch=20;a.downedDelivery=1;a.hp=20;a.maxHp=20;
    clients[1]->sendAvatar(a);a.flags=AvatarDowned;a.hp=0;a.downedEpisode=40;clients[2]->sendAvatar(a);pump();
    ReviveRequest r{{20,5,4,0,0,1,0},100,server.peerBySlot(SlotType::Friend1)->connectionId,
        server.peerBySlot(SlotType::Friend2)->connectionId,40,1,2};
    const auto before=received[2].size();std::this_thread::sleep_for(std::chrono::milliseconds(1002));
    const auto originalReceipt=server.peerBySlot(SlotType::Friend2)->reviveAvatarMs;
    a.seq=1;clients[2]->sendAvatar(a);pump();
    check(server.peerBySlot(SlotType::Friend2)->reviveAvatarMs==originalReceipt,"replayed avatar cannot refresh revive authority");
    clients[1]->sendReviveRequest(r);pump();check(received[2].size()==before,"fresh heartbeat cannot replace expired avatar positions");
#ifdef _WIN32
    // Drain earlier deliveries, then drive the real runtime world pump and
    // inbound ring. This tests the DLL contract without loading any DLL/game.
    std::vector<std::uint8_t> packet;while(bridge.ReceiveFromRuntime(packet)){}
    a.seq=0;a.flags=0;a.hp=20;clients[1]->sendAvatar(a);
    a.flags=AvatarDowned;a.hp=0;a.downedEpisode=50;clients[2]->sendAvatar(a);pump();
    r.seq=101;r.targetEpisode=50;
    check(bridge.SendToRuntime(encode(r),ProducerWorldContext{generation,1,0}),"native request queues with captured context");
    pumpDllToNet(bridge,*clients[1],stats);pump();
    check(stats.toNet==1 && bridge.ReceiveFromRuntime(packet),"runtime pump delivers accepted request back to owner ring");
    const std::uint8_t* payload=nullptr;std::size_t length=0;
    decodePacketHeader(packet.data(),packet.size(),payload,length);ByteReader reader(payload,length);WorldEnvelope envelope;read(reader,envelope);
    decodePacketHeader(envelope.packet.data(),envelope.packet.size(),payload,length);ByteReader inner(payload,length);ReviveRequest delivered;read(inner,delivered);
    std::array<std::uint64_t,3> connections{};for(unsigned i=0;i<3;++i)connections[i]=server.peerBySlot(static_cast<SlotType>(i))->connectionId;
    LocalDownedState downed{{generation,1,0},20,5,4,0,0,1,0,50,GetTickCount64(),true};
    ReviveOwnerGate owner;unsigned nativeAttempts=0;
    auto apply=[&]{if(owner.Consume(delivered,envelope.scope,envelope.scope.sessionId,{generation,1,0},connections,2,downed,GetTickCount64()))++nativeAttempts;};
    apply();apply();check(nativeAttempts==1,"actual routed owner envelope authorizes exactly one mocked native attempt");
    bridge.SendToRuntime(encode(r),ProducerWorldContext{generation+1,1,0});pumpDllToNet(bridge,*clients[1],stats);
    check(stats.retiredOutgoing==1 && stats.toNet==1,"retired DLL request dropped before transport");
    a.downedEpisode=51;clients[2]->sendAvatar(a);pump();r.seq=102;r.targetEpisode=51;
    const auto deliveredBefore=received[2].size();
    clients[1]->sendReviveRequest(r);
    // Let relay authorize while the target runtime is deliberately not pumped.
    for(unsigned i=0;i<30;++i){clients[1]->tick();server.tick(1);}
    a.flags=0;a.hp=20;clients[2]->sendAvatar(a);pump();
    check(received[2].size()==deliveredBefore && !bridge.ReceiveFromRuntime(packet),
          "owner now alive rejects queued approval before callback or DLL ring");
#endif
    for(auto& c:clients)c->disconnect();server.stop();
    SessionHost newer(config);check(newer.start(),"fresh host for protocol compatibility refusal");
    NetworkClient old("127.0.0.1",config.port,"revive-test","none","old",SlotType::Player,{},RuntimeMode::CampaignCoop,"",10);
    old.connect();for(unsigned i=0;i<60;++i){newer.tick();old.tick();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    check(newer.verifiedPeerCount()==0 && !old.ready(),"protocol10 peer cannot join current session");old.disconnect();newer.stop();
}
}
int main(){if(enet_initialize()!=0)return 2;unitChecks();enetChecks();enet_deinitialize();std::cout<<"failures="<<errors<<'\n';return errors?1:0;}
