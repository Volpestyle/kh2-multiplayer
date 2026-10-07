#include "kh2coop/SteamTransport.hpp"
#include "kh2coop/SteamBroker.hpp"
#include "kh2coop/SteamPipe.hpp"
#include <chrono>
#include <deque>
#include <map>
#include <thread>

namespace kh2coop {
namespace {
std::uint64_t now(){return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
struct Hub {
    std::unique_ptr<steam::BrokerLink> pipe;
    std::uint64_t identity=0,lastPing=0;
    bool host=false,dead=false,listening=false,localConnected=false,configured=false;
    std::vector<std::uint64_t> allowed;
    std::map<std::uint64_t,TransportStats> quality;
    std::deque<steam::Frame> server,client,commands;
    void fail(){dead=true;pipe->close();}
    bool push(std::deque<steam::Frame>& q,steam::Frame f){if(q.size()>=steam::MaxQueue){fail();return false;}q.push_back(std::move(f));return true;}
    bool command(const steam::Frame& f){if(dead||commands.size()>=steam::MaxQueue){fail();return false;}commands.push_back(f);return true;}
    bool pump(){
        if(dead)return false;
        if(now()-lastPing>=1000){lastPing=now();if(!command({steam::Op::Ping}))return false;}
        // The owner loop must service within the existing five-second heartbeat.
        // Do not pull another burst into a saturated application or pipe queue.
        if(!pipe->pump()){fail();return false;}
        while(!commands.empty()&&pipe->queued()<steam::MaxQueue){
            if(!pipe->send(commands.front())){fail();return false;}commands.pop_front();
        }
        steam::Frame f;
        for(unsigned i=0;i<64&&(host?server:client).size()<steam::MaxQueue-32&&pipe->receive(f);++i){
            if(f.op==steam::Op::Error){fail();return false;}
            if(f.op==steam::Op::Ready){if(f.peer!=identity){fail();return false;}configured=true;continue;}
            if(f.op==steam::Op::Stats){
                if(f.bytes.size()!=8||!steam::validId(f.peer)||(quality.size()>=3&&!quality.count(f.peer))){fail();return false;}
                const auto read=[&](unsigned off){std::uint32_t n=0;for(unsigned i=0;i<4;++i)n|=std::uint32_t(f.bytes[off+i])<<(8*i);return n;};
                if(read(4)>1000){fail();return false;}quality[f.peer]={read(0),0,read(4),3};continue;
            }
            if(f.op!=steam::Op::Connected&&f.op!=steam::Op::Disconnected&&f.op!=steam::Op::Data){fail();return false;}
            if(!push(host?server:client,std::move(f)))return false;
        }return true;
    }
};
struct Peer {std::uint64_t id;bool connected=false,closed=false;};
class SteamTransport final:public Transport {
    std::shared_ptr<Hub> hub_;bool server_=false,local_=false,open_=false;
    std::map<std::uint64_t,std::unique_ptr<Peer>> peers_;
    std::map<std::uint64_t,unsigned> stale_; // Disconnected receipts owed for peers this joiner closed itself
    bool joiner()const{return !server_&&!local_;}
    void terminate(){ // the original close: the attachment ends (host, local Player, destruction)
        if(!open_)return;open_=false;
        if(local_){if(hub_->localConnected){hub_->push(hub_->server,{steam::Op::Disconnected,hub_->identity,1});hub_->localConnected=false;}}
        else {hub_->fail();}
        queue().clear();for(auto& [id,p]:peers_){(void)id;p->connected=false;p->closed=true;}
    }
    std::deque<steam::Frame>& queue(){return server_?hub_->server:hub_->client;}
    Peer* find(TransportPeer* p)const{ // non-const Peer: joiner disconnect() marks it closed
for(const auto& [id,v]:peers_){(void)id;if(reinterpret_cast<TransportPeer*>(v.get())==p)return v.get();}return nullptr;}
    Peer* peer(std::uint64_t id){auto& p=peers_[id];if(!p)p=std::make_unique<Peer>(Peer{id});return p.get();}
public:
    SteamTransport(std::shared_ptr<Hub> h,bool s,bool l):hub_(std::move(h)),server_(s),local_(l){}
    ~SteamTransport()override{terminate();}
    bool createClient(std::size_t n,std::size_t channels)override{if(server_||n!=1||channels!=3||hub_->dead)return false;open_=true;return true;}
    TransportOpenResult listen(const std::string&,std::uint16_t,std::size_t n,std::size_t channels)override{
        if(!server_||n>3||channels!=3||hub_->dead||hub_->listening)return TransportOpenResult::CreateFailed;
        steam::Frame f{steam::Op::Host};for(auto id:hub_->allowed)for(unsigned b=0;b<8;++b)f.bytes.push_back(static_cast<std::uint8_t>(id>>(8*b)));
        if(!hub_->command(f))return TransportOpenResult::CreateFailed;
        const auto end=now()+5000;while(!hub_->configured&&now()<end){if(!hub_->pump())break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        if(!hub_->configured){hub_->fail();return TransportOpenResult::CreateFailed;}hub_->listening=true;open_=true;return TransportOpenResult::Ok;
    }
    TransportPeer* connect(const std::string& address,std::uint16_t,std::size_t channels,bool& resolved)override{
        resolved=false;if(!open_||channels!=3||hub_->dead)return nullptr;
        std::uint64_t id=0;if(local_){if(address!="local"||!hub_->listening||hub_->localConnected)return nullptr;id=hub_->identity;
            hub_->localConnected=true;if(!hub_->push(hub_->server,{steam::Op::Connected,id})||!hub_->push(hub_->client,{steam::Op::Connected,id}))return nullptr;
        }else{if(!steam::parseId(address,id)||id==hub_->identity||!hub_->command({steam::Op::Join,id}))return nullptr;}
        resolved=true;auto* p=peer(id);p->closed=false;return reinterpret_cast<TransportPeer*>(p);
    }
    bool isOpen()const override{return open_;} // service must deliver loss before client retires
    // VUH-1493 G8: a joiner's close is per peer while its broker attachment is healthy. Each live peer gets
    // Op::Close; the transport stays open so service() keeps the broker heartbeat alive between rejoin
    // attempts, and the next createClient/connect re-Joins the same host. IPC errors still fail() the hub.
    void close()override{
        if(!open_)return;
        if(!joiner()||hub_->dead){terminate();return;}
        for(auto& [id,p]:peers_){
            if(p->closed)continue;
            p->connected=false;p->closed=true;
            if(!hub_->command({steam::Op::Close,id,1})){terminate();return;}
            ++stale_[id]; // the broker answers our Close with one Disconnected
        }
        // Keep queued frames: a Disconnected already queued for a peer we just closed is the broker's
        // retirement of that connection (it then answers our Close with nothing), so it must repay the debt.
    }
    int service(TransportEvent& e,std::uint32_t)override{
        e={};if(!open_)return 0;hub_->pump();
        if(hub_->dead){for(auto& [id,p]:peers_)if(!p->closed){p->connected=false;p->closed=true;e.type=TransportEventType::Disconnect;e.peer=reinterpret_cast<TransportPeer*>(p.get());e.data=0;return 1;}return -1;}
        auto& q=queue();steam::Frame f;Peer* p=nullptr;
        for(;;){
            if(q.empty())return 0;f=std::move(q.front());q.pop_front();p=peer(f.peer);
            // The broker answers each Close in order, before any later Join: while an answer is owed, every
            // frame for that peer (Connected, Data, Disconnected) belongs to the old connection.
            if(joiner()&&stale_[f.peer]){if(f.op==steam::Op::Disconnected)--stale_[f.peer];continue;}
            break;
        }
        e.peer=reinterpret_cast<TransportPeer*>(p);
        if(f.op==steam::Op::Connected){p->connected=true;p->closed=false;e.type=TransportEventType::Connect;}
        else if(f.op==steam::Op::Disconnected){p->connected=false;p->closed=true;e.type=TransportEventType::Disconnect;e.data=f.reason;}
        else if(f.op==steam::Op::Data&&p->connected){e.type=TransportEventType::Receive;auto* b=new std::vector<std::uint8_t>(std::move(f.bytes));e.packet=TransportPacket(b,[](void* v){delete static_cast<std::vector<std::uint8_t>*>(v);},b->data(),b->size(),f.reliable);}
        else{hub_->fail();return -1;}return 1;
    }
    bool send(TransportPeer* p,const std::uint8_t* b,std::size_t n,std::uint8_t ch,bool reliable)override{
        const auto* v=find(p);if(!open_||hub_->dead||!v||!v->connected||!b||!n||n>steam::MaxPacket-8||ch>=3)return false;
        steam::Frame f{steam::Op::Data,v->id,0,ch,reliable,{b,b+n}};
        if(local_)return hub_->push(hub_->server,std::move(f));
        if(server_&&v->id==hub_->identity)return hub_->push(hub_->client,std::move(f));
        f.op=steam::Op::Send;return hub_->command(f);
    }
    void disconnect(TransportPeer* p,std::uint32_t reason)override{
        auto* v=find(p);if(!v||hub_->dead)return;
        if(local_||(server_&&v->id==hub_->identity)){
            hub_->localConnected=false;hub_->push(hub_->server,{steam::Op::Disconnected,v->id,reason});hub_->push(hub_->client,{steam::Op::Disconnected,v->id,reason});
        }else if(joiner()){ // one Close per live peer; its answer is owed (see service)
            if(v->closed)return;
            v->connected=false;v->closed=true;
            if(!hub_->command({steam::Op::Close,v->id,reason})){terminate();return;}
            ++stale_[v->id];
        }else hub_->command({steam::Op::Close,v->id,reason});
    }
    void disconnectLater(TransportPeer* p,std::uint32_t reason)override{disconnect(p,reason);}
    TransportStats stats(TransportPeer* p)const override{const auto* v=find(p);if(v){const auto i=hub_->quality.find(v->id);if(i!=hub_->quality.end())return i->second;}return {0,0,0,3};}
    std::string pendingPeerLabel(TransportPeer* p)const override{const auto* v=find(p);return v?"steam:"+std::to_string(v->id):"steam:invalid";}
    std::string authenticatedIdentity(TransportPeer* p)const override{return pendingPeerLabel(p);}
};
}
SteamTransports makeSteamTransports(std::uint32_t pid,bool host,const std::vector<std::uint64_t>& allowed,bool listenerProbe){
    if(listenerProbe&&(!host||!allowed.empty()))return {};
    auto pipe=std::make_unique<steam::Pipe>();if(!pipe->attach(pid))return {};
    return makeSteamTransports(std::move(pipe),host,allowed,listenerProbe);
}
SteamTransports makeSteamTransports(std::unique_ptr<steam::BrokerLink> pipe,bool host,const std::vector<std::uint64_t>& allowed,bool listenerProbe){
    SteamTransports result;
    if(listenerProbe&&(!host||!allowed.empty()))return result; // probe: host with an empty allowlist only
    if(host&&((!listenerProbe&&allowed.empty())||allowed.size()>steam::MaxPeers))return result;
    auto hub=std::make_shared<Hub>();hub->host=host;hub->allowed=allowed;
    hub->pipe=std::move(pipe);if(!hub->pipe||!hub->pipe->connected()||!hub->pipe->send({steam::Op::Hello}))return result;
    const auto end=now()+5000;steam::Frame f;bool ready=false;
    while(now()<end){if(!hub->pipe->pump())return result;if(hub->pipe->receive(f)){ready=f.op==steam::Op::Ready&&steam::validId(f.peer);break;}std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    if(!ready)return result;hub->identity=f.peer;hub->lastPing=now();result.identity=f.peer;
    result.client=std::make_unique<SteamTransport>(hub,false,host);
    if(host)result.server=std::make_unique<SteamTransport>(hub,true,false);
    return result;
}
} // namespace kh2coop
