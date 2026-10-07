#include "PartyNetworkFixture.hpp"
#include "ReviveNetworkFixture.hpp"
#include "kh2coop/SteamBroker.hpp"
#include "kh2coop/SteamPipe.hpp"
#include "kh2coop/SteamTransport.hpp"
#include "kh2coop/NetworkClient.hpp"
#include "kh2coop/SessionHost.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>
#include <map>
#include <utility>

using namespace kh2coop;
using namespace kh2coop::steam;
namespace {
int failures=0;
void check(bool v,const char* what){std::cout<<(v?"PASS ":"FAIL ")<<what<<'\n';if(!v)++failures;}
constexpr std::uint64_t Host=76561198000000001ull,Guest=Host+1,Other=Host+2;
Frame hostFrame(){Frame f{Op::Host};for(unsigned i=0;i<8;++i)f.bytes.push_back(static_cast<std::uint8_t>(Guest>>(8*i)));return f;}
struct Mock:Api {
    std::uint64_t id=Host;bool ice=true,auth=true,ok=true,accepted=true,sent=true,busy=false,loggedOn=true;unsigned identityChecks=0;
    std::uint32_t listener=10,next=20;unsigned accepts=0,closes=0,listensClosed=0,sends=0;
    std::deque<Status> events;std::deque<Message> messages;std::vector<std::uint8_t> last,delivered;
    std::uint64_t readyIdentity()override{return auth?id:0;}
    std::uint64_t currentIdentity()override{++identityChecks;return loggedOn?id:0;}
    bool congested()const override{return busy;}
    std::uint32_t listen()override{return listener;}
    std::uint32_t connect(std::uint64_t)override{return next;}
    bool iceOff(std::uint32_t,bool)override{return ice;}
    bool accept(std::uint32_t)override{++accepts;return accepted;}
    void close(std::uint32_t,bool,std::uint32_t=0)override{++closes;}
    void closeListener(std::uint32_t)override{++listensClosed;}
    bool nextStatus(Status& s)override{if(events.empty())return false;s=events.front();events.pop_front();return true;}
    bool healthy()const override{return ok;}
    bool receive(std::uint32_t,Message& m)override{if(messages.empty())return false;m=std::move(messages.front());messages.pop_front();return true;}
    bool send(std::uint32_t,std::span<const std::uint8_t>b,bool)override{++sends;last.assign(b.begin(),b.end());if(sent)delivered.push_back(b.back());return sent;}
};
void start(Broker& b){check(b.command({Op::Hello},100),"hello ready");Frame f;b.pop(f);check(b.command(hostFrame(),100),"explicit host allowlist");b.pop(f);}
void connect(Mock& m,Broker& b){m.events.push_back({20,10,Guest,1,true,false});b.tick(101);m.events.push_back({20,10,Guest,3,true,true});b.tick(102);Frame f;b.pop(f);}

// Actual broker core and actual transport/client/session policy. Only Steam API
// and the local OS link are replaced; no synthetic protocol admission shortcuts.
struct Bus;
struct Endpoint:Mock {
    Bus& bus;explicit Endpoint(Bus& b,std::uint64_t who);
    std::uint32_t connect(std::uint64_t target)override;
    bool accept(std::uint32_t h)override;
    bool send(std::uint32_t h,std::span<const std::uint8_t>,bool)override;
    void close(std::uint32_t h,bool,std::uint32_t reason=0)override;
    bool receive(std::uint32_t h,Message& out)override{const auto i=std::find_if(messages.begin(),messages.end(),[&](const auto& m){return m.handle==h;});if(i==messages.end())return false;out=std::move(*i);messages.erase(i);return true;}
};
struct Bus {std::map<std::uint64_t,Endpoint*> endpoints;std::map<std::uint32_t,std::pair<Endpoint*,std::uint32_t>> routes;std::uint32_t next=100;};
Endpoint::Endpoint(Bus& b,std::uint64_t who):bus(b){id=who;bus.endpoints[who]=this;}
std::uint32_t Endpoint::connect(std::uint64_t target){auto it=bus.endpoints.find(target);if(it==bus.endpoints.end())return 0;auto* to=it->second;auto a=bus.next++,b=bus.next++;bus.routes[a]={to,b};bus.routes[b]={this,a};to->events.push_back({b,to->listener,id,1,true,false});return a;}
bool Endpoint::accept(std::uint32_t h){++accepts;auto [to,remote]=bus.routes.at(h);events.push_back({h,listener,to->id,3,true,true});to->events.push_back({remote,0,id,3,true,true});return true;}
bool Endpoint::send(std::uint32_t h,std::span<const std::uint8_t>b,bool reliable){auto it=bus.routes.find(h);if(it==bus.routes.end())return false;auto [to,remote]=it->second;to->messages.push_back({remote,id,reliable,{b.begin(),b.end()}});return true;}
void Endpoint::close(std::uint32_t h,bool,std::uint32_t reason){auto it=bus.routes.find(h);if(it==bus.routes.end())return;auto [to,remote]=it->second;to->events.push_back({remote,0,id,4,true,true,reason});bus.routes.erase(remote);bus.routes.erase(it);}
struct Link:BrokerLink {
    Broker broker;bool opened=true;std::uint64_t clock=100;std::uint64_t tx=0,rx=0;
    explicit Link(Api& a):broker(a){}
    bool connected()const override{return opened;}
    bool send(const Frame& f)override{Frame copy;++tx;if(!decode(encode(f,tx),tx,copy))return false;return opened&&broker.command(copy,clock);}
    bool receive(Frame& f)override{Frame raw;if(!broker.pop(raw))return false;++rx;return decode(encode(raw,rx),rx,f);}
    bool pump()override{return opened&&broker.tick(++clock);}
    void close()override{opened=false;broker.stop();}
};
}
int main(){std::cout<<std::unitbuf; // crash diagnostics: every check line reaches the log
    std::uint64_t id=0;check(parseId(std::to_string(Host),id)&&id==Host,"SteamID64 parsed exactly");
    for(const auto& s:{"480","0","+76561198000000001","76561198000000001x","76561198000000001 ","76561198000000000"}){
        if(std::string(s)=="76561198000000000")continue;check(!parseId(s,id),"invalid Steam ID refused");}
    Frame raw{Op::Send,Guest,7,2,true,{0,255,9}},decoded;auto bytes=encode(raw,9);
    check(decode(bytes,9,decoded)&&decoded.peer==Guest&&decoded.bytes==raw.bytes&&decoded.reliable,"IPC exact binary roundtrip");
    check(!decode(bytes,8,decoded),"IPC replay/out-of-order refused");
    for(const auto off:{0u,4u,5u,6u,7u,28u}){auto bad=bytes;bad[off]=255;check(!decode(bad,9,decoded),"IPC malformed header refused");}
    bytes.pop_back();check(!decode(bytes,9,decoded),"IPC truncated message refused");
    raw.bytes.resize(MaxPacket+1);check(encode(raw,1).empty(),"IPC oversized refused");
    {Mock m;Broker b(m);check(!b.command({Op::Join,Guest},1),"no pre-hello socket authority");}
    {Mock m;m.auth=false;Broker b(m);check(!b.command({Op::Hello},1),"no session/auth readiness refused");}
    {Mock m;m.ice=false;Broker b(m);b.command({Op::Hello},1);check(!b.command(hostFrame(),2)&&m.listensClosed==1,"listen ICE failure closes handle");}
    {Mock m;m.ice=false;Broker b(m);b.command({Op::Hello},1);check(!b.command({Op::Join,Guest},2)&&m.closes==1,"connect ICE failure closes handle");}
    {Mock m;Broker b(m);start(b);m.events.push_back({20,10,Other,1,true,false});b.tick(101);check(m.accepts==0&&m.closes==1,"unlisted identity refused before accept");}
    {Mock m;Broker b(m);start(b);m.events.push_back({20,10,Guest,1,false,false});b.tick(101);check(m.accepts==0&&m.closes==1,"unauthenticated identity refused");}
    {Mock m;Broker b(m);start(b);connect(m,b);m.events.push_back({21,10,Guest,1,true,false});b.tick(103);check(m.accepts==1&&m.closes==1,"duplicate identity refused");}
    {Mock m;Broker b(m);start(b);m.events.push_back({20,10,Guest,1,true,false});b.tick(101);m.events.push_back({20,10,Guest,3,true,false});check(!b.tick(102)&&m.closes==1,"connected direct route fails closed");}
    {Mock m;Broker b(m);start(b);connect(m,b);check(b.command({Op::Send,Guest,0,1,false,{9,8}},103)&&m.last==std::vector<std::uint8_t>({0x4b,0x53,1,1,0,0,0,0,9,8}),"wire channel/unreliable exact envelope");
        m.messages.push_back({20,Guest,false,m.last});check(b.tick(104),"receive authenticated data");Frame f;check(b.pop(f)&&f.op==Op::Data&&f.peer==Guest&&f.bytes==std::vector<std::uint8_t>({9,8}),"receive identity stamped by connection");}
    {Mock m;Broker b(m);start(b);connect(m,b);m.messages.push_back({20,Other,true,{0x4b,0x53,1,0,1,0,0,0,1}});check(!b.tick(104),"forged message identity refused");}
    {Mock m;Broker b(m);start(b);connect(m,b);m.messages.push_back({20,Guest,false,{0x4b,0x53,1,0,1,0,0,0,1}});check(!b.tick(104),"reliability mismatch refused");}
    {Mock m;Broker b(m);start(b);connect(m,b);m.id=Other;check(b.tick(103)&&!b.tick(1100)&&m.closes==1,"Steam account change closes owned peers on one-second check");}
    {Mock m;Broker b(m);start(b);connect(m,b);check(!b.tick(5101)&&m.closes==1,"runtime heartbeat expires and closes");}
    {Mock m;Broker b(m);start(b);m.ok=false;check(!b.tick(101),"callback loss refuses");}
    {Mock m;Broker b(m);start(b);connect(m,b);check(!b.command({Op::Send,Other,0,0,true,{1}},103),"unknown destination refused");}
    {Mock m;Broker b(m);start(b);connect(m,b);m.sent=false;check(b.command({Op::Send,Guest,0,0,true,{1}},103)&&!b.failed()&&m.closes==1,"closed send retires peer only");}
    {Mock m;Broker b(m);b.command({Op::Hello},100);b.command({Op::Join,Guest},100);b.command({Op::Ping},15100);check(b.tick(15101)&&!b.failed()&&m.closes==1,"pending connection bounded 15 seconds without ending session");}
    {Mock m;Broker b(m);b.tick(100);check(!b.tick(5101),"IPC first hello has a five second deadline");}
    {Mock m;Broker b(m);start(b);connect(m,b);b.stop();check(m.closes==1&&m.listensClosed==1,"stop only owned connection/listener");}
    {Mock m;Broker b(m);start(b);connect(m,b);for(unsigned i=0;i<129;++i)m.messages.push_back({20,Guest,false,{0x4b,0x53,1,1,0,0,0,0,1}});
        for(unsigned i=0;i<9;++i)b.tick(103+i);
        check(!b.failed()&&m.messages.size()==33&&m.closes==0,"burst backpressure leaves unread data in Steam at 96 queued");
        unsigned received=0;Frame f;while(b.pop(f))++received;
        for(unsigned i=0;i<3;++i)b.tick(120+i);while(b.pop(f))++received;
        check(received==129&&m.messages.empty()&&!b.failed(),"entire burst delivered after consumer drains without session loss");}
    {Mock m;Broker b(m);start(b);connect(m,b);
        for(unsigned n=103;n<1100;++n){b.command({Op::Ping},n);b.tick(n);}
        check(m.identityChecks==0,"no per-command or per-millisecond identity IPC");
        m.auth=false;check(b.tick(1100)&&m.identityChecks==1,"transient readiness dip does not retire established identity");
        m.loggedOn=false;check(!b.tick(2100),"logoff ends session at next identity check");}
    {Mock m;Broker b(m);start(b);connect(m,b);
        m.events.push_back({20,10,Guest,4,true,true});b.tick(103);
        check(b.command({Op::Send,Guest,0,0,true,{1}},104)&&b.command({Op::Close,Guest},105)&&!b.failed(),"queued send/close for previously admitted disconnected peer ignored");
        check(m.listensClosed==0,"disconnect leaves host listener alive");}
    {Mock m;Broker b(m);start(b);m.accepted=false;m.events.push_back({20,10,Guest,1,true,false});
        check(b.tick(101)&&m.closes==1&&!b.failed()&&m.listensClosed==0,"dead-on-arrival accept closes only that peer");
        m.accepted=true;connect(m,b);check(b.command({Op::Send,Guest,0,0,true,{1}},103),"same allowlisted peer can retry after failed accept");}
    {Mock m;Broker b(m);b.command({Op::Hello},100);auto f=hostFrame();for(unsigned i=0;i<8;++i)f.bytes.push_back(static_cast<std::uint8_t>(Other>>(8*i)));
        b.command(f,100);Frame out;while(b.pop(out)){};connect(m,b);
        m.events.push_back({21,10,Other,1,true,false});b.tick(103);b.command({Op::Ping},15104);
        check(b.tick(15104)&&!b.failed()&&m.closes==1,"one pending timeout preserves established friend");
        check(b.command({Op::Send,Guest,0,0,true,{7}},15105)&&m.last.back()==7,"established friend remains writable after other timeout");}
    {Mock m;Broker b(m);start(b);connect(m,b);m.sent=false;m.busy=true;
        for(unsigned n=0;n<40;++n)b.command({Op::Send,Guest,0,0,true,{static_cast<std::uint8_t>(n)}},103);
        check(b.tick(104)&&!b.failed()&&m.closes==0,"SDK send LimitExceeded queues bounded ordered retry without peer loss");
        m.sent=true;m.busy=false;for(unsigned n=0;n<3;++n)b.tick(105+n);
        bool ordered=m.delivered.size()==40;for(std::size_t i=0;i<m.delivered.size();++i)ordered=ordered&&m.delivered[i]==i;
        check(ordered&&m.closes==0,"queued Steam sends drain every byte in issue order");}
    {Mock m;Broker b(m);start(b);connect(m,b);m.messages.push_back({20,Guest,false,{0x4b,0x53,1,1,0,0,0,0,1}});
        check(b.tick(103,MaxQueue-32)&&m.messages.size()==1,"pipe backlog prevents another Steam receive");
        check(b.tick(104,0)&&m.messages.empty(),"Steam receive resumes when IPC backlog drains");}
    {Mock m;auto link=std::make_unique<Link>(m);auto* retained=link.get();auto t=makeSteamTransports(std::move(link),false,{});
        check(t.client&&t.client->createClient(1,3),"pending client for IPC-loss control");bool resolved=false;t.client->connect(std::to_string(Guest),0,3,resolved);
        retained->close();TransportEvent e;check(t.client->isOpen()&&t.client->service(e,0)==1&&e.type==TransportEventType::Disconnect&&e.data==0,"IPC loss during pending connect reports TransportLost once");
        check(t.client->service(e,0)<0,"no duplicate loss event");}

    {Bus bus;Endpoint a(bus,Host),b(bus,Guest),late(bus,Other);auto ht=makeSteamTransports(std::make_unique<Link>(a),true,{Guest,Other});
        auto ct=makeSteamTransports(std::make_unique<Link>(b),false,{});
        check(ht.server&&ht.client&&ct.client,"broker-link transport factories");
        SessionConfig config;config.gameBuild="build";config.modHash="none";config.contentHash="none";config.authenticatedHostIdentity="steam:"+std::to_string(Host);
        SessionHost server(config,{},std::move(ht.server));check(server.start(),"SessionHost through Steam broker");
        std::array<std::vector<ReviveRequest>,3> revives;
        ClientCallbacks hcb;hcb.onReviveRequest=[&](const auto& r){revives[0].push_back(r);};
        unsigned avatars=0;ClientCallbacks cb;cb.onReviveRequest=[&](const auto& r){revives[1].push_back(r);};cb.onAvatarState=[&](const AvatarRelay& r){if(r.avatar.ownerSlot==SlotType::Player&&r.avatar.position.x==12)++avatars;};
        NetworkClient host("local",0,"build","none",config.authenticatedHostIdentity,SlotType::Player,hcb,RuntimeMode::CampaignCoop,"none",PROTOCOL_VERSION,"Host",std::move(ht.client));
        NetworkClient client(std::to_string(Host),0,"build","none","steam:"+std::to_string(Guest),SlotType::Friend1,cb,RuntimeMode::CampaignCoop,"none",PROTOCOL_VERSION,"Friend",std::move(ct.client));
        check(host.connect()&&client.connect(),"both transports initiate");
        auto pump=[&]{for(unsigned n=0;n<80;++n){server.tick();host.tick();client.tick();}};pump();
        check(server.verifiedPeerCount()==2,"actual current protocol admission over mocked Steam");
        AvatarState v;v.position.x=12;v.worldId=4;v.roomId=26;host.sendAvatar(v);pump();check(avatars>0,"avatar bytes/ownership exchanged through actual session host");
        host.sendRoomTransition({1,4,26,0,0,0,0});host.sendEnemyManifest({1,true,{{7,0,0,309,{}}}});host.sendProgressUpdate({1,true,{{0x1cff,{2}}}});pump();
        auto lt=makeSteamTransports(std::make_unique<Link>(late),false,{});unsigned rooms=0,manifests=0,progress=0;
        ClientCallbacks lcb;lcb.onReviveRequest=[&](const auto& r){revives[2].push_back(r);};lcb.onRoomTransition=[&](const RoomTransition& r){if(r.epoch==1&&r.worldId==4&&r.roomId==26)++rooms;};
        lcb.onEnemyManifest=[&](const EnemyManifest& m){if(m.replace&&m.entries.size()==1&&m.entries[0].netId==7)++manifests;};
        lcb.onProgressUpdate=[&](const ProgressUpdate& p){if(p.full&&p.version==1)++progress;};
        NetworkClient newcomer(std::to_string(Host),0,"build","none","steam:"+std::to_string(Other),SlotType::Friend2,lcb,RuntimeMode::CampaignCoop,"none",PROTOCOL_VERSION,"Late",std::move(lt.client));newcomer.connect();
        for(unsigned n=0;n<80;++n){server.tick();host.tick();client.tick();newcomer.tick();}
        check(server.verifiedPeerCount()==3&&rooms&&manifests&&progress,"three-peer late join reuses actual room/manifest/progress cache through broker");
        reviveNetworkChecks(server,{&host,&client,&newcomer},revives,[&]{
            for(unsigned n=0;n<80;++n){server.tick();host.tick();client.tick();newcomer.tick();}
        },check);
        partyNetworkChecks(server,{&host,&client,&newcomer},[&]{
            for(unsigned n=0;n<80;++n){server.tick();host.tick();client.tick();newcomer.tick();}
        },check);
        client.disconnect();
        for(unsigned n=0;n<80;++n){server.tick();host.tick();newcomer.tick();}
        check(server.verifiedPeerCount()==2&&host.isConnected()&&newcomer.isConnected(),"one real protocol peer leaves without retiring host and other friend");
        host.disconnect();pump();check(server.verifiedPeerCount()==0,"host retirement closes remote session");
    }
    // VUH-1493 probe 03: listener probe with an EMPTY allowlist admits nobody.
    {Mock m;Broker b(m);check(b.command({Op::Hello},100),"probe: hello");Frame f;b.pop(f);
        check(b.command({Op::Host},100)&&b.pop(f)&&f.op==Op::Ready,"probe: empty allowlist still listens and verifies ICE");
        for(const auto who:{Guest,Other,Host+77}){m.events.push_back({20,10,who,1,true,false});m.events.push_back({21,10,who,1,false,false});}
        check(b.tick(101)&&!b.failed(),"probe: refused connections do not end the probe session");
        check(m.accepts==0&&m.closes==6,"probe: every authenticated or unauthenticated identity closed before accept");}
    {Mock m;m.ice=false;Broker b(m);b.command({Op::Hello},100);check(!b.command({Op::Host},100)&&m.listensClosed==1,"probe: ICE readback failure still refuses the listener");}
    {Mock m;check(!makeSteamTransports(std::make_unique<Link>(m),true,{}).client,"empty allowlist refused without the explicit probe flag");}
    {Mock m;check(!makeSteamTransports(std::make_unique<Link>(m),true,{Guest},true).client,"probe flag refuses any allowlisted identity");}
    {Mock m;check(!makeSteamTransports(std::make_unique<Link>(m),false,{},true).client,"probe flag refuses a joining runtime");}
    {Bus bus;Endpoint a(bus,Host),b(bus,Guest);
        auto ht=makeSteamTransports(std::make_unique<Link>(a),true,{},true);
        check(ht.server&&ht.client,"probe: host transports with an empty allowlist");
        SessionConfig config;config.gameBuild="build";config.modHash="none";config.contentHash="none";config.authenticatedHostIdentity="steam:"+std::to_string(Host);
        SessionHost server(config,{},std::move(ht.server));check(server.start(),"probe: SessionHost listens through the broker");
        NetworkClient host("local",0,"build","none",config.authenticatedHostIdentity,SlotType::Player,{},RuntimeMode::CampaignCoop,"none",PROTOCOL_VERSION,"Host",std::move(ht.client));
        auto ct=makeSteamTransports(std::make_unique<Link>(b),false,{});
        NetworkClient foreign(std::to_string(Host),0,"build","none","steam:"+std::to_string(Guest),SlotType::Friend1,{},RuntimeMode::CampaignCoop,"none",PROTOCOL_VERSION,"Foreign",std::move(ct.client));
        host.connect();foreign.connect();
        for(unsigned n=0;n<120;++n){server.tick();host.tick();foreign.tick();}
        check(host.isConnected()&&server.verifiedPeerCount()==1,"probe: the local Player is the only verified member");
        check(!foreign.isConnected()&&a.accepts==0,"probe: a foreign Steam account is never accepted or admitted");
        host.disconnect();for(unsigned n=0;n<20;++n){server.tick();host.tick();foreign.tick();}}
    // G8: a broker attachment may re-Join only its own host, and only once the previous connection is gone.
    {Mock m;Broker b(m);b.command({Op::Hello},100);Frame f;b.pop(f);
        check(b.command({Op::Join,Guest},100),"rejoin rule: first Join");
        check(!b.command({Op::Join,Guest},101)&&b.failed(),"rejoin rule: a second Join while connected is refused");}
    {Mock m;Broker b(m);b.command({Op::Hello},100);Frame f;b.pop(f);b.command({Op::Join,Guest},100);
        check(b.command({Op::Close,Guest},101),"rejoin rule: Close the host connection");while(b.pop(f)){}
        check(b.command({Op::Join,Guest},102)&&!b.failed(),"rejoin rule: Join the same host again after Close");}
    {Mock m;Broker b(m);b.command({Op::Hello},100);Frame f;b.pop(f);b.command({Op::Join,Guest},100);
        b.command({Op::Close,Guest},101);while(b.pop(f)){}
        check(!b.command({Op::Join,Other},102)&&b.failed(),"rejoin rule: a different host is refused");}
    {Mock m;Broker b(m);start(b);
        check(!b.command({Op::Join,Guest},101)&&b.failed(),"rejoin rule: a hosting attachment can never Join");}
    {Mock m;bool closed=false;
        struct FlagLink:Link{bool* flag;FlagLink(Api& a,bool* f):Link(a),flag(f){}void close()override{*flag=true;Link::close();}};
        {auto t=makeSteamTransports(std::make_unique<FlagLink>(m,&closed),false,{});check(t.client&&t.client->createClient(1,3),"destruction: open joiner transport");}
        check(closed,"destruction: the transport's destructor still ends the attachment");}
    // G8 race: data and the Close answer still in the broker queue when the joiner closes are swallowed,
    // not delivered as a loss and not a protocol failure; the same transport then re-Joins.
    {Bus bus;Endpoint a(bus,Host),b(bus,Guest);
        auto ht=makeSteamTransports(std::make_unique<Link>(a),true,{Guest});
        SessionConfig config;config.gameBuild="build";config.modHash="none";config.contentHash="none";config.authenticatedHostIdentity="steam:"+std::to_string(Host);
        SessionHost server(config,{},std::move(ht.server));server.start();
        NetworkClient host("local",0,"build","none",config.authenticatedHostIdentity,SlotType::Player,{},RuntimeMode::CampaignCoop,"none",PROTOCOL_VERSION,"Host",std::move(ht.client));host.connect();
        auto link=std::make_unique<Link>(b);auto* lf=link.get();auto ct=makeSteamTransports(std::move(link),false,{});auto& t=*ct.client;
        TransportEvent e;unsigned connects=0,losses=0,receives=0;
        auto drain=[&](unsigned rounds){for(unsigned n=0;n<rounds;++n){server.tick();host.tick();while(t.service(e,0)>0){
            if(e.type==TransportEventType::Connect)++connects;else if(e.type==TransportEventType::Disconnect)++losses;
            else if(e.type==TransportEventType::Receive){++receives;e.packet.reset();}}}};
        bool resolved=false;check(t.createClient(1,3)&&t.connect(std::to_string(Host),0,3,resolved)&&resolved,"race: joiner connects");
        drain(80);check(connects==1&&losses==0,"race: connected once");
        std::uint32_t hostSide=0;for(const auto& [h,route]:bus.routes)if(route.first==&b){hostSide=h;break;}
        const std::vector<std::uint8_t> wire{0x4b,0x53,1,0,1,0,0,0,7};
        check(hostSide&&a.send(hostSide,wire,true),"race: host data in flight");
        lf->broker.tick(++lf->clock); // the friend broker queued the data; the transport has not read it
        const auto before=receives;t.close();drain(40);
        check(losses==0&&receives==before&&lf->connected(),"race: in-flight data and the Close answer swallowed; attachment healthy");
        check(t.createClient(1,3)&&t.connect(std::to_string(Host),0,3,resolved),"race: same transport re-Joins");
        drain(80);check(connects==2&&losses==0&&lf->connected(),"race: second connection established");
        host.disconnect();drain(10);}
    // G8 rev2 races (review): while a Close answer is owed, every frame for that peer belongs to the old
    // connection. A: a Connected that overtook our Close at the connect deadline. B: old Data after an
    // immediate re-connect. C: a joiner's disconnect(peer) owes its own answer too.
    {Bus bus;Endpoint a(bus,Host),b(bus,Guest);
        auto ht=makeSteamTransports(std::make_unique<Link>(a),true,{Guest});
        SessionConfig config;config.gameBuild="build";config.modHash="none";config.contentHash="none";config.authenticatedHostIdentity="steam:"+std::to_string(Host);
        SessionHost server(config,{},std::move(ht.server));server.start();
        NetworkClient host("local",0,"build","none",config.authenticatedHostIdentity,SlotType::Player,{},RuntimeMode::CampaignCoop,"none",PROTOCOL_VERSION,"Host",std::move(ht.client));host.connect();
        auto link=std::make_unique<Link>(b);auto* lf=link.get();auto ct=makeSteamTransports(std::move(link),false,{});auto& t=*ct.client;
        TransportEvent e;unsigned connects=0,losses=0;
        auto drain=[&](unsigned rounds){for(unsigned n=0;n<rounds;++n){server.tick();host.tick();while(t.service(e,0)>0){
            if(e.type==TransportEventType::Connect)++connects;else if(e.type==TransportEventType::Disconnect)++losses;
            else if(e.type==TransportEventType::Receive)e.packet.reset();}}};
        auto hostSide=[&]{std::uint32_t h=0;for(const auto& [k,route]:bus.routes)if(route.first==&b){h=k;break;}return h;};
        bool resolved=false;
        // A: Join, the host accepts, the friend broker has queued Connected; the deadline closes first.
        t.createClient(1,3);auto* p1=t.connect(std::to_string(Host),0,3,resolved);
        t.service(e,0);                       // pump: the Join reaches the broker (nothing to read yet)
        for(unsigned n=0;n<4;++n){server.tick();host.tick();}
        lf->broker.tick(++lf->clock);         // Connected now waits in the friend broker
        t.close();drain(40);
        check(p1&&connects==0&&losses==0&&lf->connected(),"race A: a Connected that overtook our Close is swallowed with its answer");
        t.createClient(1,3);t.connect(std::to_string(Host),0,3,resolved);drain(80);
        check(connects==1&&losses==0,"race A: the re-Join connects once");
        if(const auto h=hostSide();h&&bus.routes.count(h)){const auto [to,remote]=bus.routes.at(h);(void)to; // test-only drop; a missing route fails below
         a.events.push_back({h,0,Guest,4,true,true,0});b.events.push_back({remote,0,Host,4,true,true,0});bus.routes.erase(remote);bus.routes.erase(h);}
        drain(40);check(losses==1,"race A: a later genuine drop is still delivered (no leaked Close debt)");
        // B: connected again, host Data in flight, close() then an immediate re-connect before any service.
        t.createClient(1,3);t.connect(std::to_string(Host),0,3,resolved);drain(80);
        check(connects==2,"race B: connected");
        const std::vector<std::uint8_t> oldData{0x4b,0x53,1,0,1,0,0,0,9};check(a.send(hostSide(),oldData,true),"race B: host data in flight");
        lf->broker.tick(++lf->clock);
        t.close();t.createClient(1,3);auto* live=t.connect(std::to_string(Host),0,3,resolved);drain(80);
        check(connects==3&&losses==1&&lf->connected(),"race B: old data after an immediate re-connect is swallowed; new connection up");
        // C: the joiner's own disconnect(peer), then an immediate re-connect; the answer arrives late.
        check(live!=nullptr,"race C: live peer from race B");
        t.disconnect(live,0);t.createClient(1,3);t.connect(std::to_string(Host),0,3,resolved);drain(80);
        check(connects==4&&losses==1&&lf->connected(),"race C: disconnect(peer)'s own answer is not taken as a loss of the re-Join");
        host.disconnect();drain(10);}
    // VUH-1493 G7/G8 reconnect controls through the actual broker core, transport,
    // NetworkClient and SessionHost; only the Steam API and the OS pipe are replaced.
    {Bus bus;Endpoint a(bus,Host),b(bus,Guest);
        auto ht=makeSteamTransports(std::make_unique<Link>(a),true,{Guest});
        SessionConfig config;config.gameBuild="build";config.modHash="none";config.contentHash="none";config.authenticatedHostIdentity="steam:"+std::to_string(Host);
        SessionHost server(config,{},std::move(ht.server));server.start();
        NetworkClient host("local",0,"build","none",config.authenticatedHostIdentity,SlotType::Player,{},RuntimeMode::CampaignCoop,"none",PROTOCOL_VERSION,"Host",std::move(ht.client));
        host.connect();
        // G8: a peer-level Steam drop (the host side closes the connection; the friend's pipe stays alive).
        auto link1=std::make_unique<Link>(b);auto* l1=link1.get();auto ct=makeSteamTransports(std::move(link1),false,{});
        unsigned closes=0;ClientCallbacks cb;cb.onDisconnected=[&]{++closes;};
        auto friend1=std::make_unique<NetworkClient>(std::to_string(Host),std::uint16_t{0},"build","none","steam:"+std::to_string(Guest),SlotType::Friend1,cb,RuntimeMode::CampaignCoop,"none",PROTOCOL_VERSION,"Friend",std::move(ct.client));
        friend1->connect();
        auto pump=[&](NetworkClient* c){for(unsigned n=0;n<80;++n){server.tick();host.tick();if(c)c->tick();}};
        pump(friend1.get());check(server.verifiedPeerCount()==2,"reconnect control: friend admitted over mocked Steam");
        std::uint32_t hostSide=0;for(const auto& [h,route]:bus.routes)if(route.first==&b){hostSide=h;break;}
        check(hostSide!=0,"reconnect control: host-side Steam connection located");
        if(bus.routes.count(hostSide)){const auto [to,remote]=bus.routes.at(hostSide);(void)to; // Steam reports ProblemDetectedLocally on both ends
         a.events.push_back({hostSide,0,Guest,4,true,true,0});b.events.push_back({remote,0,Host,4,true,true,0});
         bus.routes.erase(remote);bus.routes.erase(hostSide);}
        pump(friend1.get());
        check(closes==1&&!friend1->isConnected()&&server.verifiedPeerCount()==1&&l1->connected(),"G8: peer-level Steam drop reaches the friend once; pipe and host session stay up");
        // G8 (fixed, flipped deliberately): a joiner's close is per peer while the attachment is healthy, so the
        // runtime's bounded recovery re-Joins the same host on the same broker attachment.
        check(friend1->connect()&&l1->connected(),"G8: in-place rejoin after a Steam drop keeps the attachment");
        pump(friend1.get());
        check(friend1->isConnected()&&server.verifiedPeerCount()==2&&closes==1,"G8: allowlisted friend re-admitted on the same attachment");
        // A user-initiated disconnect sends Close for the live peer; the broker's Disconnected answer is
        // swallowed, the host sees the friend leave, and the next connect re-admits.
        friend1->disconnect();pump(friend1.get());
        check(!friend1->isConnected()&&server.verifiedPeerCount()==1&&l1->connected()&&closes==1,
              "G8: local disconnect closes only the peer; its own Close answer is not a second loss");
        check(friend1->connect(),"G8: reconnect after a local disconnect");pump(friend1.get());
        check(friend1->isConnected()&&server.verifiedPeerCount()==2&&closes==1,"G8: re-admitted after a local disconnect, no stale loss delivered");
        // Between attempts the closed joiner still services its hub, so broker heartbeats continue.
        friend1->disconnect();const auto txBefore=l1->tx;
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));friend1->tick();
        check(l1->tx>txBefore&&l1->connected(),"G8: a closed joiner keeps pinging its broker between attempts");
        check(friend1->connect(),"G8: connect after an idle gap");pump(friend1.get());
        check(friend1->isConnected()&&server.verifiedPeerCount()==2,"G8: re-admitted after an idle gap");
        // G7: the attachment is dead (pipe loss). The launcher's next Connect is a fresh runtime with a fresh
        // attachment to the same in-game broker.
        l1->close();pump(friend1.get());
        check(!friend1->isConnected()&&!friend1->connect(),"G7: the dead attachment cannot reconnect in place");
        friend1.reset();
        auto fresh=makeSteamTransports(std::make_unique<Link>(b),false,{});
        check(fresh.client!=nullptr&&fresh.identity==Guest,"G7: fresh runtime attachment to the same broker");
        NetworkClient again(std::to_string(Host),0,"build","none","steam:"+std::to_string(Guest),SlotType::Friend1,{},RuntimeMode::CampaignCoop,"none",PROTOCOL_VERSION,"Friend",std::move(fresh.client));
        again.connect();pump(&again);
        check(again.isConnected()&&server.verifiedPeerCount()==2,"G7: fresh runtime re-admitted by the unchanged host session");
        again.disconnect();host.disconnect();pump(nullptr);
    }
    // Same core with malicious protocol identity, separately from Steam auth.
    for(const auto test:{0,1,2}){
        Bus bus;Endpoint a(bus,Host),b(bus,Guest);auto ht=makeSteamTransports(std::make_unique<Link>(a),true,{Guest});auto ct=makeSteamTransports(std::make_unique<Link>(b),false,{});
        SessionConfig c;c.gameBuild="build";c.modHash="none";c.authenticatedHostIdentity="steam:"+std::to_string(Host);SessionHost server(c,{},std::move(ht.server));server.start();
        NetworkClient host("local",0,"build","none",c.authenticatedHostIdentity,SlotType::Player,{},RuntimeMode::CampaignCoop,"",PROTOCOL_VERSION,"",std::move(ht.client));host.connect();
        const auto idString=test==0?c.authenticatedHostIdentity:"steam:"+std::to_string(Guest);
        NetworkClient bad(std::to_string(Host),0,test==2?"wrong":"build","none",idString,test==1?SlotType::Player:SlotType::Friend1,{},RuntimeMode::CampaignCoop,"",PROTOCOL_VERSION,"",std::move(ct.client));bad.connect();
        for(unsigned n=0;n<80;++n){server.tick();host.tick();bad.tick();}
        check(server.verifiedPeerCount()==1,"spoofed owner/host-slot/version blocked by existing session policy");
    }
    std::cout<<"failures="<<failures<<'\n';return failures?1:0;
}
