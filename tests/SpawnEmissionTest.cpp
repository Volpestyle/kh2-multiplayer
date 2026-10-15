#include "kh2coop/Codec.hpp"
#include "kh2coop/SpawnEmissionLedger.hpp"
#include "kh2coop/NetworkClient.hpp"
#include "kh2coop/SessionHost.hpp"
#include <enet/enet.h>
#include <chrono>
#include <iostream>
#include <limits>
#include <memory>
#include <thread>
using namespace kh2coop;
namespace {
int errors = 0;
void check(bool ok, const char* label) { std::cout << (ok ? "PASS: " : "FAIL: ") << label << '\n'; if (!ok) ++errors; }
SpawnEmission sample() { return {{7,4,0x15,0,0,26,0},1,1234,750,0,{-7,0,-405,1}}; }
template<class F> bool throws(F f) { try { f(); return false; } catch (const std::exception&) { return true; } }
void codec() {
    auto e = sample(); auto bytes = encode(e);
    const std::uint8_t* p; std::size_t n;
    check(decodePacketHeader(bytes.data(),bytes.size(),p,n)==PacketType::SpawnEmission && n==48, "emission packet 50 has 48 payload bytes");
    ByteReader r(p,n); SpawnEmission out; read(r,out);
    check(encode(out)==bytes && r.atEnd(), "exact emission round trip including historical trigger point");
    check(isScopedWorldPacket(PacketType::SpawnEmission) && isMaterialWorldPacket(PacketType::SpawnEmission) && validateScopedWorldPacket(bytes)==PacketType::SpawnEmission,
          "emissions travel through authenticated world envelopes");
    for (int delta : {-1,1}) {
        auto bad=bytes; if(delta<0)bad.pop_back();else bad.push_back(0);
        check(throws([&]{decodePacketHeader(bad.data(),bad.size(),p,n);}), "truncated and trailing frames refused");
    }
    e.sequence=0; check(throws([&]{encode(e);}), "zero sequence refused");
    e=sample();e.recordIndex=256; check(throws([&]{encode(e);}), "out of range record refused");
    e=sample();e.point[0]=std::numeric_limits<float>::quiet_NaN(); check(throws([&]{encode(e);}), "nonfinite trigger refused");
}
void ledger() {
    SpawnEmissionLedger l; auto a=sample(); auto b=a; b.sequence=2;b.recordIndex=1;b.point[2]=-493;
    check(!l.Permit(a.controllerKey,a.headerId,0,false), "friend-only controller blocked without announcement");
    check(l.Add(a)&&l.Add(b)&&!l.Add(a), "reliable replay does not duplicate permission");
    check(l.Pending(a.controllerKey,750)->point[2]==-405, "late intro release selects first emission point, not current host position");
    check(!l.Permit(a.controllerKey,750,1,false)&&!l.Permit(a.controllerKey+1,750,0,false)&&!l.Permit(a.controllerKey,751,0,false), "controller, header and record all bind permission and ordering");
    check(l.Permit(a.controllerKey,750,0,false)&&l.Permit(a.controllerKey,750,0,false), "allocation failure retains permission");
    check(l.Permit(a.controllerKey,750,0,true)&&!l.Permit(a.controllerKey,750,0,false), "successful spawn consumes exactly one announcement");
    check(l.Pending(a.controllerKey,750)->point[2]==-493 && l.Permit(a.controllerKey,750,1,true), "next host emission selects its own point");
    a.sequence=3;check(l.Add(a)&&l.Permit(a.controllerKey,750,0,true), "host refill authorizes another incarnation of the same record");
    l.Clear();check(!l.Pending(a.controllerKey,750)&&l.Add(sample()), "room or session reset retires receipts and replay floor");
    l.Clear();for(std::uint64_t i=1;i<=SPAWN_EMISSION_CAP;++i){a.sequence=i;l.Add(a);}
    a.sequence=SPAWN_EMISSION_CAP+1;check(!l.Add(a), "bounded receipt storage refuses overflow");
}
void relay() {
    SessionConfig config;config.bindAddress="127.0.0.1";config.port=29882;config.gameBuild="emission-test";config.modHash="none";
    SessionHost host(config,{});if(!host.start()){check(false,"relay bind");return;}
    std::array<std::unique_ptr<NetworkClient>,3> clients;
    std::array<unsigned,3> received{};
    auto make=[&](unsigned i){ClientCallbacks cb;cb.onWorldEnvelope=[&,i](const WorldEnvelope& e){
        if(e.packet.front()==static_cast<std::uint8_t>(PacketType::SpawnEmission))++received[i];};
        clients[i]=std::make_unique<NetworkClient>("127.0.0.1",config.port,"emission-test","none","e"+std::to_string(i),static_cast<SlotType>(i),cb);clients[i]->connect();};
    auto pump=[&]{for(int k=0;k<70;++k){host.tick();for(auto& c:clients)if(c)c->tick();std::this_thread::sleep_for(std::chrono::milliseconds(1));}};
    make(0);make(1);pump();pump();
    auto e=sample();clients[0]->sendRoomTransition(e.location);pump();
    std::uint64_t source = 10;
    auto send=[&](unsigned i,const SpawnEmission& m){clients[i]->sendNativeWorld(encode(m),ProducerWorldContext{1,clients[i]->deliverySerial(),i==0?++source:0},true);pump();};
    send(0,e);check(received[1]==1&&received[0]==0,"host emission forwards reliably to friend only");
    auto before=host.rejectedWorldMessages();send(0,e);check(host.rejectedWorldMessages()==before+1&&received[1]==1,"duplicate sequence refused by relay");
    auto forged=e;forged.sequence=2;before=host.rejectedWorldMessages();send(1,forged);check(host.rejectedWorldMessages()==before+1,"friend cannot announce emission");
    auto stale=forged;--stale.location.epoch;before=host.rejectedWorldMessages();send(0,stale);check(host.rejectedWorldMessages()==before+1,"stale room cannot authorize emission");
    make(2);pump();pump();check(received[2]==1,"late join cache retains host trigger point");
    ++e.location.epoch;clients[0]->sendRoomTransition(e.location);pump();send(0,e);
    check(received[1]==2&&received[2]==2,"next room clears emission history and accepts its sequence");
    for(auto& c:clients)if(c)c->disconnect();host.stop();
}
}
int main(){if(enet_initialize()!=0)return 2;codec();ledger();relay();enet_deinitialize();std::cout<<"failures="<<errors<<'\n';return errors?1:0;}
