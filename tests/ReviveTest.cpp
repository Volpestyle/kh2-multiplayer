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
// VUH-1515: host-detected enemy hit on a remote player's host clone.
void remoteHitUnitChecks(){
    RemoteHit h{{7,5,4,0,0,1,0},42,10,12,2,3,309,0x1234,25};
    const auto bytes=encode(h); const std::uint8_t* p=nullptr;std::size_t n=0;
    check(PacketType::RemoteHit==static_cast<PacketType>(45) && decodePacketHeader(bytes.data(),bytes.size(),p,n)==PacketType::RemoteHit &&
          n==REMOTE_HIT_PAYLOAD_SIZE && n==55,"remote hit fixed 55-byte wire payload, packet type 45");
    ByteReader reader(p,n);RemoteHit copy;read(reader,copy);
    check(encode(copy)==bytes && reader.atEnd() && copy.seq==42 && copy.hostConnectionId==10 && copy.targetConnectionId==12 &&
          copy.targetSlot==2 && copy.netId==3 && copy.objectId==309 && copy.attackId==0x1234 && copy.damage==25 &&
          sameResyncRoom(copy.location,h.location),"remote hit exact codec roundtrip in field order");
    for(int delta:{-1,1}){auto bad=bytes;if(delta<0)bad.pop_back();else bad.push_back(0);
        bool refused=false;try{decodePacketHeader(bad.data(),bad.size(),p,n);}catch(const std::exception&){refused=true;}
        check(refused,"truncated or trailing remote hit frame refused");}
    for(int delta:{-1,1}){auto bad=bytes;if(delta<0)bad.pop_back();else bad.push_back(0);bad[1]=static_cast<std::uint8_t>(bad.size()-3);
        bool refused=false;try{decodePacketHeader(bad.data(),bad.size(),p,n);}catch(const std::exception&){refused=true;}
        check(refused,"remote hit with self-consistent wrong length refused");}
    {std::vector<std::uint8_t> shortPayload(54,0);ByteReader shortReader(shortPayload.data(),shortPayload.size());RemoteHit out;
     bool refused=false;try{read(shortReader,out);}catch(const std::exception&){refused=true;}
     check(refused,"remote hit reader refuses wrong payload length");}
    check(isWorldPacket(PacketType::RemoteHit) && isScopedWorldPacket(PacketType::RemoteHit) && !isMaterialWorldPacket(PacketType::RemoteHit) &&
          validateScopedWorldPacket(bytes)==PacketType::RemoteHit,"remote hit is a scoped, non-material world packet");
    check(PROTOCOL_VERSION==15,"protocol15 retains RemoteHit");
}
void remoteHitEnetChecks(){
    SessionConfig config;config.bindAddress="127.0.0.1";config.port=29876;
    config.gameBuild="remote-hit-test";config.modHash="none";
    std::string lastReason;
    SessionCallbacks relayCallbacks;relayCallbacks.onLog=[&](const std::string& m){
        const std::string key="RemoteHit refused reason=";const auto at=m.find(key);
        if(at!=std::string::npos){const auto start=at+key.size();lastReason=m.substr(start,m.find(' ',start)-start);}
    };
    SessionHost server(config,relayCallbacks);if(!server.start()){check(false,"remote hit ENet bind");return;}
    std::array<std::vector<RemoteHit>,3> received;
#ifdef _WIN32
    WorldBridge bridge;WorldPumpStats stats;WorldInbox inbox;
    check(bridge.Open(GetCurrentProcessId()),"remote hit owned test WorldBridge opens");
    const auto generation=bridge.AdvanceSessionGeneration();
    bridge.SetDeliverySerial(1);
#endif
    std::array<std::unique_ptr<NetworkClient>,3> clients;
    std::array<unsigned,3> envelopes{};
    for(unsigned i=0;i<3;++i){ClientCallbacks cb;cb.onRemoteHit=[&,i](const RemoteHit& r){received[i].push_back(r);};
        cb.onWorldEnvelope=[&,i](const WorldEnvelope& e){
            if(e.packet.front()!=static_cast<std::uint8_t>(PacketType::RemoteHit))return;
            ++envelopes[i];
#ifdef _WIN32
            if(i==2)inbox.Receive(bridge,encode(e),stats);
#endif
        };
        clients[i]=std::make_unique<NetworkClient>("127.0.0.1",config.port,"remote-hit-test","none","hit"+std::to_string(i),static_cast<SlotType>(i),cb);
        clients[i]->connect();}
    auto pump=[&]{for(unsigned i=0;i<30;++i){server.tick();for(auto& c:clients)c->tick();std::this_thread::sleep_for(std::chrono::milliseconds(1));}};
    pump();pump();check(server.verifiedPeerCount()==3,"remote hit: three peers admitted through real ENet loopback");
    if(server.verifiedPeerCount()!=3){for(auto& c:clients)c->disconnect();server.stop();return;}
    const RoomTransition room{20,5,4,0,0,1,0};
    clients[0]->sendRoomTransition(room);pump();
    EnemyManifest manifest{20,true,{}};
    for(std::uint16_t id=1;id<=3;++id){EnemyManifestEntry e;e.netId=id;e.battleProgram=1;e.spawnIndex=id;e.objectId=300u+id;manifest.entries.push_back(e);}
    clients[0]->sendEnemyManifest(manifest);pump();
    for(unsigned i=1;i<3;++i)clients[i]->sendTransitionAck({room.epoch,5,4,true});
    clients[0]->sendEnemyDeath({20,3});pump();
    std::array<std::uint64_t,3> ids{};
    for(unsigned i=0;i<3;++i)ids[i]=server.peerBySlot(static_cast<SlotType>(i))->connectionId;
    auto total=[&]{return received[0].size()+received[1].size()+received[2].size();};
    RemoteHit hit{room,1,ids[0],ids[2],2,1,301,77,25};
    clients[0]->sendRemoteHit(hit);pump();
    check(total()==1 && received[2].size()==1 && envelopes[2]==1 && envelopes[0]==0 && envelopes[1]==0 &&
          received[2][0].damage==25 && received[2][0].netId==1 && received[2][0].attackId==77 && received[2][0].seq==1,
          "host remote hit forwarded only to the target owner, unchanged");
    // Every refusal is the relay's (rejectedWorld_ +1) and names the expected first failing check.
    auto refusedBy=[&](const char* reason,const char* label,const auto& send){
        const auto before=total();const auto rejected=server.rejectedWorldMessages();lastReason.clear();
        send();pump();
        check(total()==before && server.rejectedWorldMessages()==rejected+1 && lastReason==reason,label);
        if(lastReason!=reason)std::cout<<"  relay reason="<<lastReason<<" expected="<<reason<<'\n';
    };
    auto refused=[&](const char* reason,const char* label){refusedBy(reason,label,[&]{++hit.seq;clients[0]->sendRemoteHit(hit);});};
    {   // Non-host sender: a friend cannot author a remote hit, even echoing host identity.
        RemoteHit forged=hit;forged.seq=1000;
        refusedBy("sender-not-verified-host","non-host sender refused",[&]{
            check(clients[1]->sendNativeWorld(encode(forged),ProducerWorldContext{1,clients[1]->deliverySerial(),0},true),"friend can submit raw remote hit to relay");});
        const auto before=total();clients[1]->sendRemoteHit(forged);pump();check(total()==before,"non-host NetworkClient does not send remote hit");
    }
    refusedBy("sequence","duplicate sequence refused",[&]{clients[0]->sendRemoteHit(hit);});
    refusedBy("sequence","zero sequence refused",[&]{auto stale=hit;stale.seq=0;clients[0]->sendRemoteHit(stale);});
    hit.location.epoch=19;refused("room-mismatch","old room epoch refused");hit.location=room;
    hit.location.eventProgram=1;refused("room-mismatch","same world/room with different full tuple refused");hit.location=room;
    hit.hostConnectionId=ids[1];refused("host-connection","forged host connection refused");hit.hostConnectionId=ids[0];
    hit.targetSlot=0;hit.targetConnectionId=ids[0];refused("target-slot","target slot 0 (host) refused");
    hit.targetSlot=3;refused("target-slot","target slot 3 refused");hit.targetSlot=2;hit.targetConnectionId=ids[2];
    hit.targetConnectionId=ids[1];refused("target-connection","wrong target connection refused");hit.targetConnectionId=ids[2];
    hit.damage=0;refused("damage","zero damage refused");hit.damage=-5;refused("damage","negative damage refused");
    hit.damage=10000;refused("damage","damage 10000 refused");hit.damage=25;
    hit.netId=9;refused("enemy-unknown","unknown netId refused");hit.netId=1;
    hit.objectId=999;refused("enemy-unknown","netId with mismatched objectId refused");hit.objectId=301;
    hit.netId=3;hit.objectId=303;refused("enemy-dead","dead netId refused");hit.netId=1;hit.objectId=301;
    clients[2]->sendTransitionAck({20,5,4,false});pump();refused("target-not-arrived","target not arrived refused");
    clients[2]->sendTransitionAck({20,5,4,true});pump();
    clients[0]->sendEventHold({20,true,0});pump();refused("event-hold","active host event hold refuses remote hit");
    clients[0]->sendEventHold({20,false,0});pump();
    {   // A refused report consumes its sequence: replaying it after the cause clears fails.
        hit.damage=0;refused("damage","refused remote hit before replay control");hit.damage=25;
        refusedBy("sequence","refused remote hit sequence cannot become valid later",[&]{clients[0]->sendRemoteHit(hit);});
    }
    ++hit.seq;hit.damage=REMOTE_HIT_MAX_DAMAGE;clients[0]->sendRemoteHit(hit);pump();
    check(total()==2 && received[2].size()==2 && received[2].back().damage==REMOTE_HIT_MAX_DAMAGE,"fresh remote hit at 9999 damage delivered");
    hit.damage=1;hit.targetSlot=1;hit.targetConnectionId=ids[1];hit.netId=2;hit.objectId=302;++hit.seq;
    clients[0]->sendRemoteHit(hit);pump();
    check(total()==3 && received[1].size()==1 && received[2].size()==2 && received[0].empty(),"slot 1 target receives only its own remote hit");
#ifdef _WIN32
    // Host DLL -> runtime pump -> NetworkClient -> relay -> target NetworkClient
    // -> target DLL inbox, exactly as a relay-delivered ReviveRequest.
    std::vector<std::uint8_t> packet;while(bridge.ReceiveFromRuntime(packet)){}
    hit.targetSlot=2;hit.targetConnectionId=ids[2];hit.netId=1;hit.objectId=301;hit.damage=40;++hit.seq;
    check(bridge.SendToRuntime(encode(hit),ProducerWorldContext{generation,1,100000}),"host native remote hit queues with captured context");
    const auto toNetBefore=stats.toNet;
    pumpDllToNet(bridge,*clients[0],stats);pump();
    check(stats.toNet==toNetBefore+1 && bridge.ReceiveFromRuntime(packet),"runtime pump delivers host remote hit to target DLL ring");
    const std::uint8_t* payload=nullptr;std::size_t length=0;
    const auto outer=decodePacketHeader(packet.data(),packet.size(),payload,length);ByteReader reader(payload,length);WorldEnvelope envelope;read(reader,envelope);
    decodePacketHeader(envelope.packet.data(),envelope.packet.size(),payload,length);ByteReader inner(payload,length);RemoteHit delivered;read(inner,delivered);
    check(outer==PacketType::WorldEnvelope && envelope.scope.kind==WorldSourceKind::Native && envelope.scope.sourceConnectionId==ids[0] &&
          envelope.scope.targetConnectionId==ids[2] && envelope.scope.hostSourceSerial==100000 &&
          delivered.damage==40 && delivered.seq==hit.seq && delivered.targetSlot==2,
          "target DLL inbox receives host-sourced scoped WorldEnvelope with the remote hit");
    bridge.SendToRuntime(encode(hit),ProducerWorldContext{generation+1,1,100001});pumpDllToNet(bridge,*clients[0],stats);pump();
    check(!bridge.ReceiveFromRuntime(packet),"retired host DLL remote hit dropped before transport");
#endif
    for(auto& c:clients)c->disconnect();server.stop();
}
// VUH-1515 review B2: host statement of which remote clones it targets/covers.
void targetAuthorityUnitChecks(){
    TargetAuthority a{{7,5,4,0,0,1,0},42,10,0x06,0x1,1};
    const auto bytes=encode(a); const std::uint8_t* p=nullptr;std::size_t n=0;
    check(PacketType::TargetAuthority==static_cast<PacketType>(46) && decodePacketHeader(bytes.data(),bytes.size(),p,n)==PacketType::TargetAuthority &&
          n==TARGET_AUTHORITY_PAYLOAD_SIZE && n==38,"target authority fixed 38-byte wire payload, packet type 46");
    ByteReader reader(p,n);TargetAuthority copy;read(reader,copy);
    check(encode(copy)==bytes && reader.atEnd() && copy.seq==42 && copy.hostConnectionId==10 && copy.slotMask==0x06 &&
          copy.familyMask==1 && copy.mode==1 && sameResyncRoom(copy.location,a.location),"target authority exact codec roundtrip in field order");
    for(int delta:{-1,1}){auto bad=bytes;if(delta<0)bad.pop_back();else bad.push_back(0);
        bool refused=false;try{decodePacketHeader(bad.data(),bad.size(),p,n);}catch(const std::exception&){refused=true;}
        check(refused,"truncated or trailing target authority frame refused");}
    for(int delta:{-1,1}){auto bad=bytes;if(delta<0)bad.pop_back();else bad.push_back(0);bad[1]=static_cast<std::uint8_t>(bad.size()-3);
        bool refused=false;try{decodePacketHeader(bad.data(),bad.size(),p,n);}catch(const std::exception&){refused=true;}
        check(refused,"target authority with self-consistent wrong length refused");}
    for(std::size_t len:{std::size_t{37},std::size_t{39}}){std::vector<std::uint8_t> payload(len,0);ByteReader r(payload.data(),payload.size());TargetAuthority out;
        bool refused=false;try{read(r,out);}catch(const std::exception&){refused=true;}
        check(refused,"target authority reader refuses wrong payload length");}
    check(isWorldPacket(PacketType::TargetAuthority) && isScopedWorldPacket(PacketType::TargetAuthority) && !isMaterialWorldPacket(PacketType::TargetAuthority) &&
          validateScopedWorldPacket(bytes)==PacketType::TargetAuthority,"target authority is a scoped, non-material world packet");
}
void targetAuthorityEnetChecks(){
    SessionConfig config;config.bindAddress="127.0.0.1";config.port=29877;
    config.gameBuild="target-authority-test";config.modHash="none";
    std::string lastReason;
    SessionCallbacks relayCallbacks;relayCallbacks.onLog=[&](const std::string& m){
        const std::string key="TargetAuthority refused reason=";const auto at=m.find(key);
        if(at!=std::string::npos){const auto start=at+key.size();lastReason=m.substr(start,m.find(' ',start)-start);}
    };
    SessionHost server(config,relayCallbacks);if(!server.start()){check(false,"target authority ENet bind");return;}
    std::array<std::vector<TargetAuthority>,3> received;
    std::array<unsigned,3> envelopes{},rooms{};
#ifdef _WIN32
    WorldBridge bridge;WorldPumpStats stats;WorldInbox inbox;
    check(bridge.Open(GetCurrentProcessId()),"target authority owned test WorldBridge opens");
    const auto generation=bridge.AdvanceSessionGeneration();
    bridge.SetDeliverySerial(1);
#endif
    std::array<std::unique_ptr<NetworkClient>,3> clients;
    auto make=[&](unsigned i){ClientCallbacks cb;cb.onTargetAuthority=[&,i](const TargetAuthority& r){received[i].push_back(r);};
        cb.onRoomTransition=[&,i](const RoomTransition&){++rooms[i];};
        cb.onWorldEnvelope=[&,i](const WorldEnvelope& e){
            if(e.packet.front()!=static_cast<std::uint8_t>(PacketType::TargetAuthority))return;
            ++envelopes[i];
#ifdef _WIN32
            if(i==2)inbox.Receive(bridge,encode(e),stats);
#endif
        };
        clients[i]=std::make_unique<NetworkClient>("127.0.0.1",config.port,"target-authority-test","none","ta"+std::to_string(i),static_cast<SlotType>(i),cb);
        clients[i]->connect();};
    auto pump=[&]{for(unsigned k=0;k<30;++k){server.tick();for(auto& c:clients)if(c)c->tick();std::this_thread::sleep_for(std::chrono::milliseconds(1));}};
    make(0);make(1);pump();pump();
    check(server.verifiedPeerCount()==2,"target authority: host and first friend admitted");
    if(server.verifiedPeerCount()!=2){for(auto& c:clients)if(c)c->disconnect();server.stop();return;}
    const RoomTransition room{20,5,4,0,0,1,0};
    clients[0]->sendRoomTransition(room);pump();
    const auto hostId=server.peerBySlot(SlotType::Player)->connectionId;
    TargetAuthority authority{room,1,hostId,0x02,0x1,0};
    clients[0]->sendTargetAuthority(authority);pump();
    check(received[1].size()==1 && envelopes[1]==1 && received[0].empty() && envelopes[0]==0 && received[1][0].slotMask==0x02,
          "target authority forwarded to the connected friend, not back to the host");
    // Late joiner catches up on the room but never receives a cached or replayed authority.
    make(2);pump();pump();
    check(server.verifiedPeerCount()==3 && rooms[2]>=1 && received[2].empty() && envelopes[2]==0,
          "late joiner gets room state but no target authority replay");
    if(server.verifiedPeerCount()!=3){for(auto& c:clients)if(c)c->disconnect();server.stop();return;}
    auto total=[&]{return received[0].size()+received[1].size()+received[2].size();};
    authority.slotMask=0x06;authority.mode=1;++authority.seq;clients[0]->sendTargetAuthority(authority);pump();
    check(total()==3 && received[1].size()==2 && received[2].size()==1 && received[0].empty() && envelopes[0]==0 &&
          received[2][0].slotMask==0x06 && received[2][0].mode==1 && received[2][0].seq==authority.seq,
          "target authority forwarded to both clients and not back to the host");
    auto refusedBy=[&](const char* reason,const char* label,const auto& send){
        const auto before=total();const auto rejected=server.rejectedWorldMessages();lastReason.clear();
        send();pump();
        check(total()==before && server.rejectedWorldMessages()==rejected+1 && lastReason==reason,label);
        if(lastReason!=reason)std::cout<<"  relay reason="<<lastReason<<" expected="<<reason<<'\n';
    };
    auto refused=[&](const char* reason,const char* label){refusedBy(reason,label,[&]{++authority.seq;clients[0]->sendTargetAuthority(authority);});};
    {   TargetAuthority forged=authority;forged.seq=1000;
        refusedBy("sender-not-verified-host","non-host sender refused",[&]{
            check(clients[1]->sendNativeWorld(encode(forged),ProducerWorldContext{1,clients[1]->deliverySerial(),0},true),"friend can submit raw target authority to relay");});
        const auto before=total();clients[1]->sendTargetAuthority(forged);pump();check(total()==before,"non-host NetworkClient does not send target authority");
    }
    refusedBy("sequence","duplicate target authority sequence refused",[&]{clients[0]->sendTargetAuthority(authority);});
    refusedBy("sequence","zero target authority sequence refused",[&]{auto stale=authority;stale.seq=0;clients[0]->sendTargetAuthority(stale);});
    authority.location.epoch=19;refused("room-mismatch","old room epoch target authority refused");authority.location=room;
    authority.location.eventProgram=1;refused("room-mismatch","different full room tuple target authority refused");authority.location=room;
    authority.hostConnectionId=hostId+1;refused("host-connection","forged host connection target authority refused");authority.hostConnectionId=hostId;
    authority.slotMask=0x07;refused("slot-mask","slotMask bit 0 (host slot) refused");
    authority.slotMask=0x08;refused("slot-mask","slotMask 0x08 refused");authority.slotMask=0x06;
    authority.familyMask=0x3;refused("family-mask","familyMask bit 1 refused");authority.familyMask=0x1;
    authority.mode=2;refused("mode","mode 2 refused");authority.mode=0;
    {   // A refused statement consumes its sequence.
        authority.mode=2;refused("mode","refused target authority before replay control");authority.mode=0;
        refusedBy("sequence","refused target authority sequence cannot become valid later",[&]{clients[0]->sendTargetAuthority(authority);});
    }
    clients[0]->sendEventHold({20,true,0});pump();
    // S8: held during an active event hold (the owner then ages out and stops cancelling).
    authority.slotMask=0x04;authority.familyMask=0;refused("event-hold","target authority held during an active event hold");
    clients[0]->sendEventHold({20,false,0});pump();
#ifdef _WIN32
    // Host DLL -> runtime pump -> NetworkClient -> relay -> client NetworkClient -> client DLL inbox.
    std::vector<std::uint8_t> packet;while(bridge.ReceiveFromRuntime(packet)){}
    authority.slotMask=0x06;authority.familyMask=1;authority.mode=0;++authority.seq;
    const auto before=total();
    check(bridge.SendToRuntime(encode(authority),ProducerWorldContext{generation,1,100000}),"host native target authority queues with captured context");
    const auto toNetBefore=stats.toNet;
    pumpDllToNet(bridge,*clients[0],stats);pump();
    check(stats.toNet==toNetBefore+1 && total()==before+2 && bridge.ReceiveFromRuntime(packet),"runtime pump delivers host target authority to client DLL ring");
    const std::uint8_t* payload=nullptr;std::size_t length=0;
    const auto outer=decodePacketHeader(packet.data(),packet.size(),payload,length);ByteReader reader(payload,length);WorldEnvelope envelope;read(reader,envelope);
    decodePacketHeader(envelope.packet.data(),envelope.packet.size(),payload,length);ByteReader inner(payload,length);TargetAuthority delivered;read(inner,delivered);
    check(outer==PacketType::WorldEnvelope && envelope.scope.kind==WorldSourceKind::Native && envelope.scope.sourceConnectionId==hostId &&
          envelope.scope.targetConnectionId==server.peerBySlot(SlotType::Friend2)->connectionId && envelope.scope.hostSourceSerial==100000 &&
          delivered.seq==authority.seq && delivered.slotMask==0x06 && delivered.familyMask==1 && delivered.mode==0,
          "client DLL inbox receives host-sourced scoped WorldEnvelope with the target authority");
    bridge.SendToRuntime(encode(authority),ProducerWorldContext{generation+1,1,100001});pumpDllToNet(bridge,*clients[0],stats);pump();
    check(!bridge.ReceiveFromRuntime(packet),"retired host DLL target authority dropped before transport");
#endif
    for(auto& c:clients)if(c)c->disconnect();server.stop();
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
int main(){if(enet_initialize()!=0)return 2;unitChecks();remoteHitUnitChecks();enetChecks();remoteHitEnetChecks();targetAuthorityUnitChecks();targetAuthorityEnetChecks();enet_deinitialize();std::cout<<"failures="<<errors<<'\n';return errors?1:0;}
