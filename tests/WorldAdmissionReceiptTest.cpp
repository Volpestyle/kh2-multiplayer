// Real ENet transport plus directed calls on its actually admitted client.
// No KH2, no production bypass API, no claim of runtime/native consumption.
#include "WorldWireFixture.hpp"
// Compile the actual implementation with the fixture's test-only visibility.
// MSVC access qualifiers participate in mangling, so direct boundary calls
// cannot link a public test declaration against a private library definition.
#include "../common/src/NetworkClient.cpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
using namespace kh2coop;
int checks=0,failures=0;
void check(bool yes,const char* label){++checks;failures+=!yes;std::cout<<(yes?"PASS ":"FAIL ")<<label<<'\n';}
std::string field(const std::string& row,const std::string& key){const auto p=" "+key+"=";auto at=row.find(p);if(at==std::string::npos)return {};at+=p.size();return row.substr(at,row.find(' ',at)-at);}
void save(const std::string& path,const std::vector<std::uint8_t>& bytes){std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));}
int main(){
    if(enet_initialize())return 2;
    {
        ENetAddress address{};enet_address_set_host(&address,"127.0.0.1");address.port=17981;
        auto* server=enet_host_create(&address,1,3,0,0);check(server!=nullptr,"owned synthetic relay endpoint starts");if(!server)return 2;
        ENetPeer* peer=nullptr;std::vector<std::string> rows,order;std::vector<WorldEnvelope> delivered;unsigned bodies=0,typed=0;
        std::ofstream raw("receiver-raw.log");
        ClientCallbacks cb;cb.onCausalDiagnostic=[&](const std::string& row){rows.push_back(row);order.push_back("receipt");raw<<row<<'\n';raw.flush();return raw.good();};
        cb.onWorldEnvelope=[&](const WorldEnvelope& e){delivered.push_back(e);order.push_back("envelope");};
        cb.onWorldPacket=[&](const auto&){++bodies;order.push_back("body");};
        cb.onRoomTransition=[&](const auto&){++typed;order.push_back("typed");};
        cb.onEnemyHp=[&](const auto&){++typed;order.push_back("typed");};
        cb.onEventHold=[&](const auto&){++typed;order.push_back("typed");};
        NetworkClient client("127.0.0.1",address.port,"receiver-control","m","self",SlotType::Friend1,cb,RuntimeMode::CampaignCoop,"none");
        const auto pump=[&]{ENetEvent e{};while(enet_host_service(server,&e,0)>0){if(e.type==ENET_EVENT_TYPE_CONNECT)peer=e.peer;else if(e.type==ENET_EVENT_TYPE_RECEIVE)enet_packet_destroy(e.packet);}client.tick(0);};
        const auto wait=[&](auto predicate){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(4);while(!predicate()&&std::chrono::steady_clock::now()<end){pump();std::this_thread::sleep_for(std::chrono::milliseconds(1));}return predicate();};
        const auto send=[&](const std::vector<std::uint8_t>& wire){if(!peer)return false;auto* packet=enet_packet_create(wire.data(),wire.size(),ENET_PACKET_FLAG_RELIABLE);if(!packet)return false;if(enet_peer_send(peer,0,packet)){enet_packet_destroy(packet);return false;}enet_host_flush(server);return true;};
        client.connect();check(wait([&]{return peer&&client.isConnected();}),"actual ENet transport established");
        SessionState roster;roster.sessionId=std::string(32,'a');roster.gameBuild="receiver-control";roster.modHash="m";
        SessionActor host;host.slot=SlotType::Player;host.ownerPeerId="host";host.connectionId=0x1234567812345678ULL;
        SessionActor self;self.slot=SlotType::Friend1;self.ownerPeerId="self";self.connectionId=0x2345678923456789ULL;roster.actors={host,self};
        send(encode(roster));send(encode(WorldBinding{roster.sessionId,host.connectionId,self.connectionId,1,1}));
        check(wait([&]{return client.worldReady();}),"actual roster/current binding admitted");
        WorldScope scope{roster.sessionId,host.connectionId,1,0xABCDEF123456789ULL,self.connectionId,1};
        auto body=encode(RoomTransition{7,5,6,3,4,6,8});auto wire=encode(WorldEnvelope{scope,body});
        save("accepted-body.bin",body);save("accepted-wire.bin",wire);
        check(send(wire)&&wait([&]{return typed==1;}),"actual outer ENet envelope reaches accepted inner body callback");
        check(order==std::vector<std::string>{"receipt","envelope","body","typed"},"receipt precedes existing envelope/body/typed callback order exactly once");
        const auto& row=rows.back();
        check(field(row,"wireSHA")==causalSha(wire)&&field(row,"payloadSHA")==causalSha(body)&&field(row,"wireBytes")==std::to_string(wire.size())&&field(row,"bytes")==std::to_string(body.size()),"exact original wire and inner body identities recorded");
        check(field(row,"session")==roster.sessionId&&field(row,"scopeSession")==scope.sessionId&&field(row,"host")==std::to_string(host.connectionId)&&field(row,"self")==std::to_string(self.connectionId)&&field(row,"source")==std::to_string(scope.sourceConnectionId)&&field(row,"hostSource")==std::to_string(scope.hostSourceSerial)&&field(row,"target")==std::to_string(scope.targetConnectionId)&&field(row,"sourceDelivery")=="1"&&field(row,"targetDelivery")=="1"&&field(row,"delivery")=="1"&&field(row,"scopeKind")=="0"&&field(row,"slot")=="1"&&field(row,"reliable")=="1", "actual full-width binding/scope/reliability retained");
        check(field(row,"callbackDelivered")=="unproven"&&field(row,"nativeConsumed")=="unproven","receipt does not invent downstream consumption");
        check(delivered.back().packet==body&&delivered.back().scope.sourceConnectionId==scope.sourceConnectionId,"existing callback receives unchanged exact body and scope");

        const auto inject=[&](const auto& bytes,bool reliable=true){client.onReceive(bytes.data(),bytes.size(),reliable);};
        // Existing readBool accepts nonzero 2. Re-encoding the typed EventHold
        // would normalize it to 1, so this distinguishes original bytes from
        // a reconstructed typed message without changing admission rules.
        const auto canonicalHold=encode(EventHold{7,true,8});auto noncanonicalHold=canonicalHold;noncanonicalHold[7]=2;
        const auto noncanonicalWire=worldfixture::uncheckedEnvelope(scope,noncanonicalHold);
        const auto typedBeforeConditioner=typed;
        client.setLinkConditions({},LinkConditions{20,0,0.0f,3});
        check(send(noncanonicalWire)&&wait([&]{return typed==typedBeforeConditioner+1;}),
              "actual inbound conditioner delivers preserved outer bytes after ENet receive lifetime");
        client.setLinkConditions({},{});
        check(field(rows.back(),"payloadSHA")==causalSha(noncanonicalHold)&&field(rows.back(),"payloadSHA")!=causalSha(canonicalHold)&&field(rows.back(),"wireSHA")==causalSha(noncanonicalWire)&&delivered.back().packet==noncanonicalHold,
              "accepted noncanonical bool preserves original body/wire identity, not typed re-encoding");
        save("noncanonical-body.bin",noncanonicalHold);save("noncanonical-wire.bin",noncanonicalWire);
        const auto blocked=[&](const std::vector<std::uint8_t>& bytes,const char* label,bool reliable=true){const auto events=client.worldAdmissionDiagnostics().highWater;const auto callbacks=bodies;inject(bytes,reliable);check(client.worldAdmissionDiagnostics().highWater==events&&bodies==callbacks,label);};
        auto bad=wire;bad.pop_back();blocked(bad,"truncated outer envelope rejected before receipt and callbacks");
        bad=wire;bad.push_back(0);blocked(bad,"outer suffix rejected before receipt and callbacks");
        auto brokenBody=body;brokenBody.pop_back();blocked(worldfixture::uncheckedEnvelope(scope,brokenBody),"malformed inner frame rejected before receipt and callbacks");
        auto changed=scope;changed.sessionId=std::string(32,'b');blocked(worldfixture::uncheckedEnvelope(changed,body),"foreign session rejected");
        changed=scope;++changed.targetConnectionId;blocked(worldfixture::uncheckedEnvelope(changed,body),"wrong target connection rejected");
        changed=scope;++changed.targetDeliverySerial;blocked(worldfixture::uncheckedEnvelope(changed,body),"wrong target delivery rejected");
        changed=scope;++changed.sourceConnectionId;blocked(worldfixture::uncheckedEnvelope(changed,body),"retired source connection rejected");
        changed=scope;++changed.sourceDeliverySerial;blocked(worldfixture::uncheckedEnvelope(changed,body),"retired source delivery rejected");
        client.worldSourceFloor_=scope.hostSourceSerial;blocked(wire,"host publication at retired source floor rejected");client.worldSourceFloor_=0;
        client.worldQuarantined_=true;blocked(wire,"quarantined material envelope rejected");client.worldQuarantined_=false;
        blocked(body,"unscoped world packet rejected");
        auto hp=encode(EnemyHp{7,{{1,17,20}},20});inject(encode(WorldEnvelope{scope,hp}));
        blocked(encode(WorldEnvelope{scope,encode(EnemyHp{7,{{1,1,20}},19})}),"stale HP sequence rejected before receipt");
        blocked(encode(WorldEnvelope{scope,hp}),"equal unreliable HP rejected before receipt",false);
        blocked(encode(WorldEnvelope{scope,encode(EnemyHp{8,{{1,1,20}},21})}),"wrong-epoch HP rejected before receipt");

        // Directed current bindings intentionally use different source/target
        // delivery values; production admission must retain both distinctly.
        client.remoteDeliverySerials_[0]=9;client.worldBinding_->deliverySerial=13;
        changed=scope;changed.sourceDeliverySerial=9;changed.targetDeliverySerial=13;
        const auto distinctWire=encode(WorldEnvelope{changed,encode(EventHold{7,false,8})});inject(distinctWire);
        check(field(rows.back(),"sourceDelivery")=="9"&&field(rows.back(),"targetDelivery")=="13"&&field(rows.back(),"delivery")=="13","source delivery never substituted with current target delivery");
        save("distinct-wire.bin",distinctWire);
        const auto callbacksBefore=bodies;
        const auto eventsBefore=client.worldAdmissionDiagnostics().highWater;
        client.callbacks_.onCausalDiagnostic=[](const auto&){return false;};inject(distinctWire);
        check(bodies==callbacksBefore+1&&client.worldAdmissionDiagnostics().highWater==eventsBefore+1&&client.worldAdmissionDiagnostics().dropped==1&&client.worldAdmissionDiagnostics().unavailable,"failed diagnostic flush keeps real callback behavior and creates sticky gap");
        client.callbacks_.onCausalDiagnostic=[](const auto&)->bool{throw 1;};inject(distinctWire);
        check(bodies==callbacksBefore+2&&client.worldAdmissionDiagnostics().dropped==2,"throwing diagnostic sink keeps callback behavior and sticky gap");
        client.callbacks_.onCausalDiagnostic=cb.onCausalDiagnostic;inject(distinctWire);client.sealWorldAdmissionDiagnostics("after-failure");
        check(field(rows.back(),"dropped")=="2"&&field(rows.back(),"unavailable")=="1"&&field(rows.back(),"highWater")==field(rows.back(),"flushedHighWater"),"later successful event and seal do not erase sticky gaps");

        client.callbacks_.onCausalDiagnostic={};client.worldAdmissionDiagnostics_={};const auto count=bodies;
        inject(distinctWire);client.sealWorldAdmissionDiagnostics("quiet");
        check(bodies==count+1&&!client.worldAdmissionDiagnostics().highWater&&!client.worldAdmissionDiagnostics().seals&&!client.worldAdmissionDiagnostics().dropped,"no-sink actual receiver retains callbacks with zero diagnostic state");
        client.callbacks_.onCausalDiagnostic=cb.onCausalDiagnostic;client.sealWorldAdmissionDiagnostics("begin");client.sealWorldAdmissionDiagnostics("interval");
        check(field(rows.back(),"highWater")=="0"&&field(rows.back(),"flushedHighWater")=="0"&&field(rows.back(),"dropped")=="0"&&field(rows.back(),"sealSeq")=="2","quiet receiver stream seals without any admitted event");
        client.disconnect();enet_host_destroy(server);
    }
    enet_deinitialize();std::cout<<"checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}
