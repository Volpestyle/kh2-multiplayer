// Same-room absolute HP ordering regression. Uses real ENet endpoints, no KH2.
// The separately frozen v6 baseline reproduced rollback. These v9 fixtures
// retain its value assertions with scoped inputs and actual rejoin cache probes.
#include "kh2coop/Codec.hpp"
#include "WorldWireFixture.hpp"
#include "kh2coop/SessionHost.hpp"
#include <enet/enet.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <limits>
#include <string>
#include <thread>
#include <vector>
using namespace kh2coop;
namespace {
int checks=0,failures=0;
void check(bool good,const char* label){++checks;if(!good)++failures;std::cout<<(good?"PASS: ":"FAIL: ")<<label<<'\n';}
template<class T> void setSequence(T& message,std::uint64_t sequence){
    if constexpr(requires(T& value){value.sequence=std::uint64_t{};})message.sequence=sequence;
    else {(void)message;(void)sequence;}
}
template<class T> std::uint64_t sequenceOf(const T& message){
    if constexpr(requires(const T& value){value.sequence;})return message.sequence;
    else {(void)message;return 0;}
}
EnemyHp hp(std::uint64_t sequence,std::uint16_t id,std::int32_t value){
    EnemyHp message;message.epoch=7;message.entries={{id,value,120}};setSequence(message,sequence);return message;
}
struct Seen{
    std::vector<EnemyHp> hps;
    std::map<std::uint16_t,std::int32_t> current;
    std::uint32_t progress=0;
    unsigned manifests=0;
};
ClientCallbacks callbacks(Seen& seen){
    ClientCallbacks cb;
    cb.onEnemyHp=[&](const EnemyHp& message){seen.hps.push_back(message);for(const auto& row:message.entries){seen.current[row.netId]=row.hp;std::cout<<"OBSERVED epoch="<<message.epoch<<" wireSequence="<<sequenceOf(message)<<" netId="<<row.netId<<" hp="<<row.hp<<'\n';}};
    cb.onEnemyManifest=[&](const EnemyManifest&){++seen.manifests;};
    cb.onProgressUpdate=[&](const ProgressUpdate& message){seen.progress=message.version;};return cb;
}
bool cachedNewest(const EnemyHp& message){
    if(message.epoch!=7||message.entries.size()!=2)return false;
    const auto one=std::find_if(message.entries.begin(),message.entries.end(),[](const auto& e){return e.netId==1;});
    const auto two=std::find_if(message.entries.begin(),message.entries.end(),[](const auto& e){return e.netId==2;});
    return one!=message.entries.end()&&two!=message.entries.end()&&one->hp==72&&one->maxHp==120&&two->hp==82&&two->maxHp==120;
}
void testSameRoomRollback(){
    SessionConfig config;config.port=17821;config.bindAddress="127.0.0.1";config.gameBuild="hp-order-build";config.modHash="hp-order-mod";config.contentHash="hp-order-content";
    SessionHost relay(config);check(relay.start(),"HP order relay starts on loopback");if(!relay.isRunning())return;
    Seen hostSeen,friendSeen,lateSeen;
    NetworkClient host("127.0.0.1",config.port,config.gameBuild,config.modHash,"order-host",SlotType::Player,callbacks(hostSeen),RuntimeMode::CampaignCoop,config.contentHash);
    NetworkClient friendClient("127.0.0.1",config.port,config.gameBuild,config.modHash,"order-friend",SlotType::Friend1,callbacks(friendSeen),RuntimeMode::CampaignCoop,config.contentHash);
    NetworkClient late("127.0.0.1",config.port,config.gameBuild,config.modHash,"order-late",SlotType::Friend2,callbacks(lateSeen),RuntimeMode::CampaignCoop,config.contentHash);
    const auto pump=[&]{relay.tick(0);for(auto* client:{&host,&friendClient,&late}){client->tick(0);client->sendHeartbeat();}};
    const auto wait=[&](const std::function<bool()>& condition){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(4);while(!condition()&&std::chrono::steady_clock::now()<deadline){pump();std::this_thread::sleep_for(std::chrono::milliseconds(1));}return condition();};
    host.connect();friendClient.connect();
    const bool admitted=wait([&]{return host.worldReady()&&friendClient.worldReady()&&relay.verifiedPeerCount()==2;});
    check(admitted,"host and friend have actual admitted rosters before authoritative sends");if(!admitted)return;
    host.sendRoomTransition(RoomTransition{7,4,26,0,0,3,0});
    EnemyManifest manifest;manifest.epoch=7;manifest.replace=true;
    for(std::uint16_t id:{std::uint16_t{1},std::uint16_t{2}}){EnemyManifestEntry row;row.netId=id;row.objectId=309;row.spawnIndex=static_cast<std::uint16_t>(id-1);row.battleProgram=3;manifest.entries.push_back(row);}
    host.sendEnemyManifest(manifest);
    check(wait([&]{return friendSeen.manifests==1&&relay.manifestSize()==2;}),"current-room typed manifest positively received");
    std::cout<<"SEND logicalSequence=2 netId=1 hp=72 protocol="<<PROTOCOL_VERSION<<'\n';
    worldfixture::send(host, encode(hp(2,1,72)),true);
    const bool newerObserved=wait([&]{return !friendSeen.hps.empty()&&friendSeen.current.contains(1)&&friendSeen.current.at(1)==72;});
    check(newerObserved,"newer logical seq2 HP72 positively reaches live callback before the old send");if(!newerObserved)return;
    const auto afterNewer=friendSeen.hps.size();
    std::cout<<"SEND logicalSequence=1 netId=1 hp=41 after confirmed newer callback\n";
    worldfixture::send(host, encode(hp(1,1,41)),true);
    // Both negative and barrier enter on reliable channel0. The periodic HP
    // forwarding leg remains channel1, so the independent reliable cache checks
    // below are required as well; no sleep-only absence can certify the cache.
    host.sendProgressUpdate(ProgressUpdate{1001,false,{}});
    check(wait([&]{return friendSeen.progress==1001;}),"ordered positive progress barrier proves relay processed the older host packet");
    worldfixture::send(host, encode(hp(3,2,82)),true);
    check(wait([&]{return friendSeen.current.contains(2)&&friendSeen.current.at(2)==82;}),"distinct positive seq3 HP marker arrives without rewriting netId1");
    const bool rollbackObserved=std::any_of(friendSeen.hps.begin()+static_cast<std::ptrdiff_t>(afterNewer),friendSeen.hps.end(),[](const EnemyHp& message){return std::any_of(message.entries.begin(),message.entries.end(),[](const auto& row){return row.netId==1&&row.hp==41;});});
    std::cout<<"LIVE rollbackObserved="<<rollbackObserved<<" currentNetId1="<<friendSeen.current.at(1)<<'\n';
    check(!rollbackObserved&&friendSeen.current.at(1)==72,"older same-room HP cannot roll back the positively established live value");
    const auto beforeReplay=friendSeen.hps.size();friendClient.disconnect();check(wait([&]{return relay.verifiedPeerCount()==1;}),"cache probe retires only selected friend");friendClient.connect();check(wait([&]{return friendClient.worldReady()&&friendSeen.hps.size()==beforeReplay+1;}),"selected friend rejoin receives reliable cache");host.sendProgressUpdate(ProgressUpdate{1002,false,{}});
    check(wait([&]{return friendSeen.progress==1002;})&&friendSeen.hps.size()==beforeReplay+1&&cachedNewest(friendSeen.hps.back()),"targeted friend-rejoin reliable cache replay retains newest HP72 rather than relabeling old HP41");
    late.connect();check(wait([&]{return late.ready()&&!lateSeen.hps.empty();}),"actual third peer is admitted and receives reliable cached HP");
    host.sendProgressUpdate(ProgressUpdate{1003,false,{}});
    check(wait([&]{return lateSeen.progress==1003;})&&lateSeen.hps.size()==1&&cachedNewest(lateSeen.hps.back())&&lateSeen.current.at(1)==72,"late join reconstructs exact newest same-room HP after stale delivery");
    late.disconnect();friendClient.disconnect();host.disconnect();relay.stop();
}
EnemyHp orderedHp(std::uint32_t epoch,std::uint64_t sequence,std::int32_t value){auto m=hp(sequence,1,value);m.epoch=epoch;return m;}
std::vector<std::uint8_t> uncheckedHp(std::uint32_t epoch,std::uint64_t sequence,std::int32_t value){ByteWriter w;w.writeU32(epoch);w.writeU64(sequence);w.writeU16(1);w.writeU16(1);w.writeI32(value);w.writeI32(120);return encodePacket(PacketType::EnemyHp,w.data());}
void testCodec(){
    constexpr std::uint64_t wide=0xFEDCBA9876543210ULL;const auto original=orderedHp(7,wide,72);const auto packet=encode(original);
    const std::uint8_t* payload=nullptr;std::size_t length=0;check(decodePacketHeader(packet.data(),packet.size(),payload,length)==PacketType::EnemyHp&&length==24,"HP wire exact size epoch4+sequence8+count2+entry10");
    ByteReader fields(payload,length);check(fields.readU32()==7&&fields.readU64()==wide&&fields.readU16()==1,"wire order explicitly preserves full64-bit sequence before entry count");
    ByteReader reader(payload,length);EnemyHp round;read(reader,round);check(round.sequence==wide&&round.entries.size()==1&&round.entries[0].hp==72,"HP codec roundtrip preserves wide ordering identity and values");
    bool refused=false;try{(void)encode(orderedHp(7,0,72));}catch(const std::exception&){refused=true;}check(refused,"production encoder refuses unset sequence0");
    auto zero=uncheckedHp(7,0,72);decodePacketHeader(zero.data(),zero.size(),payload,length);refused=false;try{ByteReader r(payload,length);EnemyHp m;read(r,m);}catch(const std::exception&){refused=true;}check(refused,"production decoder independently refuses wire sequence0");
    for(unsigned mode=0;mode<4;++mode){auto bad=packet;if(mode==0)bad.pop_back();else if(mode==1)bad.push_back(1);else{ByteWriter w;write(w,original);auto b=w.take();if(mode==2)b.pop_back();else b.push_back(1);bad=encodePacket(PacketType::EnemyHp,b);}refused=false;
        try{decodePacketHeader(bad.data(),bad.size(),payload,length);ByteReader r(payload,length);EnemyHp m;read(r,m);}catch(const std::exception&){refused=true;}check(refused,"HP frame/payload truncation and suffix rejected atomically");}
    ByteWriter legacy;legacy.writeU32(7);legacy.writeU16(1);legacy.writeU16(1);legacy.writeI32(72);legacy.writeI32(120);refused=false;try{ByteReader r(legacy.data());EnemyHp m;read(r,m);}catch(const std::exception&){refused=true;}check(refused,"old epoch/count/entries payload cannot decode as version7 ordered HP");
}
void testRelayWatermark(){
    SessionConfig cfg;cfg.port=17822;cfg.bindAddress="127.0.0.1";cfg.gameBuild="hp-floor";cfg.modHash="m";cfg.contentHash="c";SessionHost relay(cfg);check(relay.start(),"HP watermark relay starts");if(!relay.isRunning())return;
    Seen seen;NetworkClient host("127.0.0.1",cfg.port,cfg.gameBuild,cfg.modHash,"floor-host",SlotType::Player,{},RuntimeMode::CampaignCoop,cfg.contentHash);
    NetworkClient client("127.0.0.1",cfg.port,cfg.gameBuild,cfg.modHash,"floor-client",SlotType::Friend1,callbacks(seen),RuntimeMode::CampaignCoop,cfg.contentHash);
    const auto pump=[&]{relay.tick(0);host.tick(0);client.tick(0);host.sendHeartbeat();client.sendHeartbeat();};
    const auto wait=[&](const std::function<bool()>& cond){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(4);while(!cond()&&std::chrono::steady_clock::now()<end){pump();std::this_thread::sleep_for(std::chrono::milliseconds(1));}return cond();};
    host.connect();client.connect();check(wait([&]{return host.worldReady()&&client.worldReady();}),"watermark peers admitted before sending world");
    std::uint32_t epoch=7,marker=0;const auto barrier=[&]{const auto next=++marker;host.sendProgressUpdate(ProgressUpdate{next,false,{}});return wait([&]{return seen.progress==next;});};
    const auto manifest=[&]{EnemyManifest m;m.epoch=epoch;m.replace=true;EnemyManifestEntry e;e.netId=1;e.objectId=309;m.entries={e};host.sendEnemyManifest(m);check(barrier(),"replacement manifest processed before next HP probe");};
    host.sendRoomTransition(RoomTransition{epoch,4,26,0,0,3,0});manifest();std::uint64_t sequence=1;
    const auto positive=[&](std::uint64_t seq,std::int32_t value){const auto before=seen.hps.size();worldfixture::send(host, encode(orderedHp(epoch,seq,value)),true);check(wait([&]{return seen.hps.size()==before+1;})&&seen.hps.back().sequence==seq&&seen.current.at(1)==value,"otherwise valid next source sequence is admitted with exact value");};
    positive(sequence,71);
    const auto rejected=[&](const std::vector<std::uint8_t>& packet,bool count,const char* label){const auto before=seen.hps.size(),rejects=relay.rejectedWorldMessages();worldfixture::send(host, packet,true);check(barrier()&&(!count||relay.rejectedWorldMessages()==rejects+1),label);const auto cacheBefore=seen.hps.size();client.disconnect();check(wait([&]{return relay.verifiedPeerCount()==1;}),"only cache-probe friend departs");client.connect();check(wait([&]{return client.worldReady()&&seen.hps.size()==cacheBefore+1;}),"cache-probe friend rejoins unchanged host");check(barrier()&&seen.hps.size()==cacheBefore+1&&seen.hps.back().sequence==sequence,"rejected input leaves reliable cached sequence unchanged");check(cacheBefore==before,"rejected HP never adds typed delivery before reliable replay barrier");};
    rejected(uncheckedHp(epoch,0,9),false,"zero source sequence reaches processing barrier without authorization");positive(++sequence,72);
    auto wrong=orderedHp(epoch+1,0xFFFFFFFFFFFFFFF0ULL,9);rejected(encode(wrong),true,"future epoch high sequence is rejected without poisoning current watermark");positive(++sequence,73);
    auto malformed=uncheckedHp(epoch,0xFFFFFFFFFFFFFFF0ULL,9);malformed.pop_back();rejected(malformed,false,"malformed high sequence cannot advance watermark by decoding a prefix");positive(++sequence,74);
    auto suffix=uncheckedHp(epoch,0xFFFFFFFFFFFFFFF0ULL,9);suffix.push_back(1);rejected(suffix,false,"extra high-sequence frame byte cannot poison watermark");positive(++sequence,75);
    rejected(encode(orderedHp(epoch,sequence,9)),true,"equal source sequence is rejected by relay even from reliable host transport");
    manifest();const auto beforeReplace=relay.rejectedWorldMessages();worldfixture::send(host, encode(orderedHp(epoch,1,9)),true);check(barrier()&&relay.rejectedWorldMessages()==beforeReplace+1,"replacement manifest does not reset host lifetime watermark");positive(++sequence,76);
    ++epoch;host.sendRoomTransition(RoomTransition{epoch,4,27,0,0,3,0});manifest();const auto beforeRoom=relay.rejectedWorldMessages();worldfixture::send(host, encode(orderedHp(epoch,1,9)),true);check(barrier()&&relay.rejectedWorldMessages()==beforeRoom+1,"new room does not reset host lifetime watermark");positive(++sequence,77);
    sequence=(std::numeric_limits<std::uint64_t>::max)();positive(sequence,90);rejected(encode(orderedHp(epoch,1,9)),true,"maximal sequence cannot wrap into sequence1 in same host lifetime");
    const auto oldSession=relay.sessionState().sessionId;const auto oldHost=relay.peerBySlot(SlotType::Player)->connectionId;host.disconnect();check(wait([&]{return relay.verifiedPeerCount()==0&&!client.isConnected();}),"old host lifetime ends before resetting any sequence");client.disconnect();host.connect();client.connect();
    check(wait([&]{return host.worldReady()&&client.worldReady();})&&relay.sessionState().sessionId!=oldSession&&relay.peerBySlot(SlotType::Player)->connectionId!=oldHost,"new host session and connection identities establish distinct lifetime");epoch=7;marker=0;seen.progress=0;host.sendRoomTransition(RoomTransition{epoch,4,26,0,0,3,0});manifest();positive(1,71);
    host.disconnect();client.disconnect();relay.stop();
}
void testClientAdmission(){
    ENetAddress address{};enet_address_set_host(&address,"127.0.0.1");address.port=17823;auto* server=enet_host_create(&address,1,3,0,0);check(server!=nullptr,"client-admission synthetic ENet endpoint starts");if(!server)return;
    ENetPeer* peer=nullptr;Seen seen;std::vector<EnemyHp> raw;std::vector<std::string> callbackOrder;std::size_t rawHpInvocations=0;
    auto cb=callbacks(seen);const auto originalTyped=cb.onEnemyHp;
    cb.onWorldPacket=[&](const std::vector<std::uint8_t>& bytes){if(!bytes.empty()&&bytes[0]==static_cast<std::uint8_t>(PacketType::EnemyHp))++rawHpInvocations;const std::uint8_t* payload=nullptr;std::size_t length=0;if(decodePacketHeader(bytes.data(),bytes.size(),payload,length)==PacketType::EnemyHp){ByteReader r(payload,length);EnemyHp m;read(r,m);raw.push_back(m);callbackOrder.push_back("raw:"+std::to_string(m.sequence));}};
    cb.onEnemyHp=[&](const EnemyHp& m){callbackOrder.push_back("typed:"+std::to_string(m.sequence));originalTyped(m);};
    NetworkClient client("127.0.0.1",address.port,"client-gate","m","gate-self",SlotType::Friend1,cb,RuntimeMode::CampaignCoop,"none");
    const auto pump=[&]{ENetEvent e{};while(enet_host_service(server,&e,0)>0){if(e.type==ENET_EVENT_TYPE_CONNECT)peer=e.peer;else if(e.type==ENET_EVENT_TYPE_RECEIVE)enet_packet_destroy(e.packet);}client.tick(0);};
    const auto wait=[&](const std::function<bool()>& cond){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(4);while(!cond()&&std::chrono::steady_clock::now()<end){pump();std::this_thread::sleep_for(std::chrono::milliseconds(1));}return cond();};
    WorldScope syntheticScope{std::string(32,'a'),0x100000001ULL,1,0,0x200000002ULL,1};
    const auto send=[&](const std::vector<std::uint8_t>& bytes,bool reliable=true,std::uint8_t channel=0){if(!peer)return false;auto wire=bytes;if(!bytes.empty()&&isScopedWorldPacket(static_cast<PacketType>(bytes.front()))){++syntheticScope.hostSourceSerial;wire=worldfixture::uncheckedEnvelope(syntheticScope,bytes);}auto* packet=enet_packet_create(wire.data(),wire.size(),reliable?ENET_PACKET_FLAG_RELIABLE:0);if(!packet)return false;if(enet_peer_send(peer,channel,packet)!=0){enet_packet_destroy(packet);return false;}enet_host_flush(server);return true;};
    client.connect();check(wait([&]{return peer&&client.isConnected();}),"actual client transport established before checked roster");
    SessionState roster;roster.sessionId=std::string(32,'a');roster.gameBuild="client-gate";roster.modHash="m";
    SessionActor host;host.slot=SlotType::Player;host.ownerPeerId="gate-host";host.connectionId=0x100000001ULL;SessionActor self;self.slot=SlotType::Friend1;self.ownerPeerId="gate-self";self.connectionId=0x200000002ULL;roster.actors={host,self};
    const auto bind=[&]{syntheticScope.sessionId=roster.sessionId;syntheticScope.sourceConnectionId=roster.actors[0].connectionId;return send(encode(WorldBinding{roster.sessionId,roster.actors[0].connectionId,roster.actors[1].connectionId,1,1}));};
    check(send(encode(roster))&&bind()&&wait([&]{return client.worldReady();}),"valid full-width host/self roster admits callback authority");
    std::uint32_t marker=0,epoch=7;
    const auto barrier=[&](std::uint8_t channel=0){const auto next=++marker;return send(encode(ProgressUpdate{next,false,{}}),true,channel)&&wait([&]{return seen.progress==next;});};
    send(encode(RoomTransition{epoch,4,26,0,0,3,0}));check(barrier(),"current room arrives before client HP admission tests");
    const auto positive=[&](std::uint64_t sequence,std::int32_t value){const auto before=seen.hps.size(),beforeRaw=rawHpInvocations;check(send(encode(orderedHp(epoch,sequence,value)))&&barrier()&&seen.hps.size()==before+1&&rawHpInvocations==beforeRaw+1&&seen.current.at(1)==value&&seen.hps.back().sequence==sequence,"valid HP passes raw and typed callbacks exactly once with checked value");};
    const auto blocked=[&](const std::vector<std::uint8_t>& bytes,const char* label){const auto typed=seen.hps.size(),beforeRaw=rawHpInvocations;check(send(bytes)&&barrier()&&seen.hps.size()==typed&&rawHpInvocations==beforeRaw,label);};
    positive(2,72);blocked(uncheckedHp(epoch,0,9),"zero sequence is rejected before raw and typed delivery behind reliable barrier");
    blocked(encode(orderedHp(epoch,1,9)),"older reliable HP cannot bypass client sequence admission");
    blocked(encode(orderedHp(epoch+1,0xFFFFFFFFFFFFFFF0ULL,9)),"wrong epoch high sequence is rejected before raw/typed and cannot poison floor");positive(3,73);
    auto malformed=uncheckedHp(epoch,0xFFFFFFFFFFFFFFF0ULL,9);malformed.pop_back();blocked(malformed,"malformed high-sequence frame rejected before raw bytes escape");positive(4,74);
    // Reliability is a packet flag, not a channel-number shortcut. Use channel1
    // for both unreliable duplicate and reliable trusted cache replay.
    client.setLinkConditions({},LinkConditions{20,0,0.0f,3});const auto beforeEqual=seen.hps.size(),rawEqual=raw.size();
    auto replay=orderedHp(epoch,4,74);replay.entries.push_back({2,42,120});
    check(send(encode(orderedHp(epoch,4,9)),false,1)&&send(encode(replay),true,1)&&barrier(1)&&seen.hps.size()==beforeEqual+1&&raw.size()==rawEqual+1&&seen.hps.back().entries.size()==2&&seen.current.at(1)==74&&seen.current.at(2)==42,"conditioned equal-unreliable drops while equal-reliable cache union is delivered once on same channel");
    // Enqueue old10 in the real inbound conditioner, then bypass its delay with
    // new11. The old packet's queued progress witness must later be observed.
    client.setLinkConditions({},LinkConditions{800,0,0.0f,4});const auto beforeReorder=seen.hps.size(),rawReorder=raw.size();
    const auto delayedMarker=++marker;check(send(encode(orderedHp(epoch,10,40)))&&send(encode(ProgressUpdate{delayedMarker,false,{}})),"old HP and its ordered progress witness physically queued");
    const auto queueUntil=std::chrono::steady_clock::now()+std::chrono::milliseconds(50);while(std::chrono::steady_clock::now()<queueUntil){pump();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    client.setLinkConditions({},{});check(send(encode(orderedHp(epoch,11,81)))&&wait([&]{return seen.hps.size()>beforeReorder;}),"newer HP dispatched immediately while older packet remains conditioned");
    check(seen.hps.size()==beforeReorder+1&&raw.size()==rawReorder+1&&seen.hps.back().sequence==11&&seen.current.at(1)==81,"positive newer callback establishes actual reordered delivery before delayed old input");
    check(wait([&]{return seen.progress==delayedMarker;})&&seen.hps.size()==beforeReorder+1&&raw.size()==rawReorder+1&&seen.current.at(1)==81,"delayed older packet traverses conditioned barrier without raw or typed rollback");
    ++epoch;send(encode(RoomTransition{epoch,4,27,0,0,3,0}));EnemyManifest replacement;replacement.epoch=epoch;replacement.replace=true;send(encode(replacement));check(barrier(),"room and replacement manifest processed before old sequence probe");
    blocked(encode(orderedHp(epoch,10,9)),"room/manifest does not reset client host-lifetime floor");positive(12,82);
    send(encode(SessionState{}));check(wait([&]{return !client.ready();}),"invalid roster retires current authority");send(encode(roster));bind();check(wait([&]{return client.worldReady();}),"same host incarnation restores roster authority");
    blocked(encode(orderedHp(epoch,1,9)),"transient invalid roster cannot reset remembered host ordering floor");positive(13,83);
    ++roster.actors[0].connectionId;send(encode(roster));bind();send(encode(RoomTransition{epoch,4,27,0,0,3,0}));check(barrier(),"new authenticated host connection and room boundary received");positive(1,84);
    roster.sessionId=std::string(32,'b');send(encode(roster));bind();send(encode(RoomTransition{epoch,4,27,0,0,3,0}));check(barrier(),"new opaque session boundary received even with same numeric host ID");positive(1,85);
    positive((std::numeric_limits<std::uint64_t>::max)(),90);blocked(encode(orderedHp(epoch,1,9)),"client max sequence cannot wrap into smaller source order");
    bool paired=rawHpInvocations==raw.size()&&raw.size()==seen.hps.size()&&callbackOrder.size()==seen.hps.size()*2;for(std::size_t i=0;i<seen.hps.size()&&paired;++i){const auto sequence=std::to_string(seen.hps[i].sequence);paired=callbackOrder[i*2]=="raw:"+sequence&&callbackOrder[i*2+1]=="typed:"+sequence;}
    check(paired,"every admitted HP raw callback precedes exactly its typed callback; rejected inputs expose neither");
    client.disconnect();enet_host_destroy(server);
}
void testVersion6Refusal(){
    SessionConfig cfg;cfg.bindAddress="127.0.0.1";cfg.port=17824;cfg.gameBuild="ordered-wire";cfg.modHash="m";cfg.contentHash="c";SessionHost relay(cfg);check(relay.start(),"version9 ordered-HP gate relay starts with capacity");if(!relay.isRunning())return;
    std::vector<ClientCloseInfo> closed;std::string reason;ClientCallbacks cb;cb.onClosed=[&](const auto& info){closed.push_back(info);};cb.onRejected=[&](const HelloReject& r){reason=r.reason;};NetworkClient legacy("127.0.0.1",cfg.port,cfg.gameBuild,cfg.modHash,"legacy-v6",SlotType::Friend1,cb,RuntimeMode::CampaignCoop,cfg.contentHash,6);legacy.connect();
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(4);while(closed.empty()&&std::chrono::steady_clock::now()<end){relay.tick(0);legacy.tick(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    check(PROTOCOL_VERSION==9&&closed.size()==1&&closed[0].reason==DisconnectReason::Incompatible&&reason=="Protocol mismatch: client=6 server=9"&&!legacy.ready()&&relay.verifiedPeerCount()==0,"version6 cannot negotiate old unsequenced HP layout despite free capacity");legacy.disconnect();relay.stop();
}
}
int main(){if(enet_initialize()!=0){std::cerr<<"ENet initialization failed\n";return 2;}testSameRoomRollback();testCodec();testRelayWatermark();testClientAdmission();testVersion6Refusal();enet_deinitialize();std::cout<<checks-failures<<" PASS, "<<failures<<" FAIL\n";return failures?1:0;}
