#include "SteamBrokerNative.hpp"
#include "kh2coop/SteamBroker.hpp"
#include "kh2coop/SteamPipe.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <map>
#include <cmath>

namespace kh2coop::steambroker {
namespace {
// Shipped SteamAPI x64 flat ABI, version012/004. Field layout pinned against
// Valve steamnetworkingtypes.h (see docs/STEAM_TRANSPORT.md). Never vtable calls.
#pragma pack(push,1)
struct Identity { std::int32_t type,size; std::uint8_t data[128]; };
struct Address { std::uint8_t ip[16]; std::uint16_t port; };
#pragma pack(pop)
struct Info {
    Identity identity; std::int64_t user; std::uint32_t listener;
    Address address; std::uint16_t pad; std::uint32_t remotePop,relayPop;
    std::int32_t state,reason; char debug[128],description[128];
    std::int32_t flags; std::uint32_t reserved[63];
};
struct Change { std::uint32_t handle; Info info; std::int32_t oldState; };
struct Message {
    void* data; std::int32_t size; std::uint32_t handle; Identity identity;
    std::int64_t user,time,number; void (*freeData)(Message*); void (*release)(Message*);
    std::int32_t channel,flags; std::int64_t extra; std::uint16_t lane,pad;
};
struct Config { std::int32_t key,type; union {std::int64_t number; void* pointer;} value; };
struct Auth {int availability;char debug[256];};
struct Relay {int availability,measuring,config,any;char debug[256];};
struct Quality {int state,ping;float local,remote,outPackets,outBytes,inPackets,inBytes;int rate,pendingUnreliable,pendingReliable,unacked;std::int64_t queue;std::uint32_t reserved[16];};
static_assert(sizeof(Identity)==136 && sizeof(Info)==696 && offsetof(Info,flags)==440);
static_assert(offsetof(Change,info)==8 && sizeof(Change)==712);
static_assert(sizeof(Message)==216 && offsetof(Message,release)==184 && offsetof(Message,flags)==196);
static_assert(sizeof(Config)==16 && sizeof(Auth)==260 && sizeof(Relay)==272);
static_assert(sizeof(Quality)==120 && offsetof(Quality,queue)==48);

// Callback lifetime is process-wide and DLL is pinned in enabled mode. No API,
// logging, heap or game state in callback. Multi-thread dispatch is serialized;
// bounded contention/overflow fails the broker instead of dropping authority.
SRWLOCK g_lock=SRWLOCK_INIT;
std::array<Change,64> g_changes{};
std::size_t g_count=0;
std::atomic<bool> g_overflow{false};
int g_marker=0;
void __cdecl Changed(Change* c) noexcept {
    if(!c)return;
    if(!TryAcquireSRWLockExclusive(&g_lock)){g_overflow.store(true,std::memory_order_release);return;}
    if(g_count==g_changes.size())g_overflow.store(true,std::memory_order_release);
    else g_changes[g_count++]=*c;
    ReleaseSRWLockExclusive(&g_lock);
}
template<class T> bool load(HMODULE m,T& out,const char* key){auto p=GetProcAddress(m,key);static_assert(sizeof(p)==sizeof(out));std::memcpy(&out,&p,sizeof(out));return p!=nullptr;}
class Native final:public steam::Api {
    int (__cdecl *userHandle_)()=nullptr; int (__cdecl *pipeHandle_)()=nullptr;
    void* (__cdecl *getUser_)()=nullptr; void* (__cdecl *getUtils_)()=nullptr;
    void* (__cdecl *getSockets_)()=nullptr; void* (__cdecl *getNetworkUtils_)()=nullptr;
    bool (__cdecl *loggedOn_)(void*)=nullptr; std::uint64_t (__cdecl *steamId_)(void*)=nullptr;
    std::uint32_t (__cdecl *appId_)(void*)=nullptr;
    int (__cdecl *initAuth_)(void*)=nullptr; int (__cdecl *auth_)(void*,Auth*)=nullptr;
    void (__cdecl *initRelay_)(void*)=nullptr; int (__cdecl *relay_)(void*,Relay*)=nullptr;
    std::uint32_t (__cdecl *listen_)(void*,int,int,const Config*)=nullptr;
    std::uint32_t (__cdecl *connect_)(void*,const Identity*,int,int,const Config*)=nullptr;
    int (__cdecl *accept_)(void*,std::uint32_t)=nullptr;
    bool (__cdecl *close_)(void*,std::uint32_t,int,const char*,bool)=nullptr;
    bool (__cdecl *closeListener_)(void*,std::uint32_t)=nullptr;
    int (__cdecl *getConfig_)(void*,int,int,std::intptr_t,int*,void*,std::size_t*)=nullptr;
    bool (__cdecl *info_)(void*,std::uint32_t,Info*)=nullptr;
    int (__cdecl *send_)(void*,std::uint32_t,const void*,std::uint32_t,int,std::int64_t*)=nullptr;
    int (__cdecl *receive_)(void*,std::uint32_t,Message**,int)=nullptr;
    int (__cdecl *quality_)(void*,std::uint32_t,Quality*,int,void*)=nullptr;
    void (__cdecl *identitySet_)(Identity*,std::uint64_t)=nullptr;
    std::uint64_t (__cdecl *identityGet_)(const Identity*)=nullptr;
    void *user_=nullptr,*utils_=nullptr,*sockets_=nullptr,*nu_=nullptr;
    FILE* log_; bool healthy_=true;
    std::array<Config,4> options() const {
        std::array<Config,4> o{};o[0]={104,1,{0}}; // no ICE candidates, at creation
        o[1]={201,5,{0}};auto cb=&Changed;std::memcpy(&o[1].value.pointer,&cb,sizeof(cb));
        o[2]={9,1,{512*1024}}; // bounded Steam send queue
        o[3]={24,1,{15000}};
        return o;
    }
public:
    explicit Native(FILE* l):log_(l){}
    bool init(){
        const auto m=GetModuleHandleW(L"steam_api64.dll");if(!m)return false;
#define GET(f,n) if(!load(m,f,n))return false
        GET(userHandle_,"SteamAPI_GetHSteamUser");GET(pipeHandle_,"SteamAPI_GetHSteamPipe");
        GET(getUser_,"SteamAPI_SteamUser_v023");GET(getUtils_,"SteamAPI_SteamUtils_v010");
        GET(getSockets_,"SteamAPI_SteamNetworkingSockets_SteamAPI_v012");GET(getNetworkUtils_,"SteamAPI_SteamNetworkingUtils_SteamAPI_v004");
        GET(loggedOn_,"SteamAPI_ISteamUser_BLoggedOn");GET(steamId_,"SteamAPI_ISteamUser_GetSteamID");GET(appId_,"SteamAPI_ISteamUtils_GetAppID");
        GET(initAuth_,"SteamAPI_ISteamNetworkingSockets_InitAuthentication");GET(auth_,"SteamAPI_ISteamNetworkingSockets_GetAuthenticationStatus");
        GET(initRelay_,"SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess");GET(relay_,"SteamAPI_ISteamNetworkingUtils_GetRelayNetworkStatus");
        GET(listen_,"SteamAPI_ISteamNetworkingSockets_CreateListenSocketP2P");GET(connect_,"SteamAPI_ISteamNetworkingSockets_ConnectP2P");
        GET(accept_,"SteamAPI_ISteamNetworkingSockets_AcceptConnection");GET(close_,"SteamAPI_ISteamNetworkingSockets_CloseConnection");
        GET(closeListener_,"SteamAPI_ISteamNetworkingSockets_CloseListenSocket");GET(getConfig_,"SteamAPI_ISteamNetworkingUtils_GetConfigValue");
        GET(info_,"SteamAPI_ISteamNetworkingSockets_GetConnectionInfo");GET(send_,"SteamAPI_ISteamNetworkingSockets_SendMessageToConnection");
        GET(receive_,"SteamAPI_ISteamNetworkingSockets_ReceiveMessagesOnConnection");
        GET(quality_,"SteamAPI_ISteamNetworkingSockets_GetConnectionRealTimeStatus");
        GET(identitySet_,"SteamAPI_SteamNetworkingIdentity_SetSteamID64");GET(identityGet_,"SteamAPI_SteamNetworkingIdentity_GetSteamID64");
#undef GET
        if(!userHandle_()||!pipeHandle_())return false;
        user_=getUser_();utils_=getUtils_();sockets_=getSockets_();nu_=getNetworkUtils_();
        if(!user_||!utils_||!sockets_||!nu_||appId_(utils_)!=steam::AppId||!loggedOn_(user_)||!steam::validId(steamId_(user_)))return false;
        initAuth_(sockets_);initRelay_(nu_);return true;
    }
    std::uint64_t readyIdentity() override {
        Auth a{};Relay r{};if(!user_||appId_(utils_)!=steam::AppId||!loggedOn_(user_)||auth_(sockets_,&a)!=100||relay_(nu_,&r)!=100||r.config!=100||r.any!=100)return 0;
        return steamId_(user_);
    }
    std::uint32_t listen() override {auto o=options();const auto h=listen_(sockets_,steam::VirtualPort,static_cast<int>(o.size()),o.data());std::fprintf(log_,"[steam-broker] listen handle=%u iceCreation=0\n",h);return h;}
    std::uint32_t connect(std::uint64_t id) override {Identity identity{};identitySet_(&identity,id);auto o=options();const auto h=connect_(sockets_,&identity,steam::VirtualPort,static_cast<int>(o.size()),o.data());std::fprintf(log_,"[steam-broker] connect peer=%llu handle=%u iceCreation=0\n",id,h);return h;}
    bool iceOff(std::uint32_t h,bool listener) override {int type=0,value=-1;std::size_t n=sizeof(value);const auto r=getConfig_(nu_,104,listener?3:4,h,&type,&value,&n);const bool ok=(r==1||r==2)&&type==1&&n==4&&value==0;
        std::fprintf(log_,"[steam-broker] ice handle=%u listener=%u get=%d type=%d size=%zu value=%d verified=%u\n",h,unsigned(listener),r,type,n,value,unsigned(ok));return ok;}
    bool accept(std::uint32_t h) override {const auto r=accept_(sockets_,h);std::fprintf(log_,"[steam-broker] accept handle=%u result=%d\n",h,r);return r==1;}
    void close(std::uint32_t h,bool linger,std::uint32_t reason=0) override {const auto ok=close_(sockets_,h,1000+static_cast<int>(reason),"KH2 broker closed",linger);std::fprintf(log_,"[steam-broker] close handle=%u linger=%u reason=%u ok=%u\n",h,unsigned(linger),reason,unsigned(ok));}
    void closeListener(std::uint32_t h) override {std::fprintf(log_,"[steam-broker] close-listener handle=%u ok=%u\n",h,unsigned(closeListener_(sockets_,h)));}
    bool nextStatus(steam::Status& s) override {
        Change c{};AcquireSRWLockExclusive(&g_lock);if(!g_count){ReleaseSRWLockExclusive(&g_lock);return false;}
        c=g_changes[0];for(std::size_t i=1;i<g_count;++i)g_changes[i-1]=g_changes[i];--g_count;ReleaseSRWLockExclusive(&g_lock);
        const auto id=identityGet_(&c.info.identity);
        s={c.handle,c.info.listener,id,c.info.state,(c.info.flags&3)==0,(c.info.flags&16)!=0,
           c.info.reason>=1000&&c.info.reason<=1009?static_cast<std::uint32_t>(c.info.reason-1000):0};
        std::fprintf(log_,"[steam-broker] callback handle=%u peer=%llu state=%d flags=%d listener=%u\n",c.handle,id,c.info.state,c.info.flags,c.info.listener);return true;
    }
    bool healthy()const override{return healthy_&&!g_overflow.load(std::memory_order_acquire);}
    bool receive(std::uint32_t h,steam::Message& out) override {
        Message* m=nullptr;const auto n=receive_(sockets_,h,&m,1);if(n<0){healthy_=false;return false;}if(!n)return false;
        if(!m){healthy_=false;return false;}
        struct Release{Message* p;~Release(){if(p->release)p->release(p);}} release{m};
        if(!m->release||!m->data||m->size<0||static_cast<std::size_t>(m->size)>steam::MaxPacket){healthy_=false;return false;}
        Info current{};if(!info_(sockets_,h,&current)||current.state!=3||(current.flags&3)||(current.flags&16)==0){healthy_=false;return false;}
        out={m->handle,identityGet_(&m->identity),(m->flags&8)!=0,{}};const auto* b=static_cast<const std::uint8_t*>(m->data);out.bytes.assign(b,b+m->size);return true;
    }
    bool send(std::uint32_t h,std::span<const std::uint8_t>b,bool reliable)override {
        Info current{};if(!info_(sockets_,h,&current)||current.state!=3||(current.flags&3)||(current.flags&16)==0)return false;
        return send_(sockets_,h,b.data(),static_cast<std::uint32_t>(b.size()),reliable?9:1,nullptr)==1;
    }
    bool quality(std::uint32_t h,std::uint32_t& rtt,std::uint32_t& loss)override{
        Quality q{};if(quality_(sockets_,h,&q,0,nullptr)!=1||q.state!=3||q.ping<0||!std::isfinite(q.local)||q.local<0||q.local>1)return false;
        rtt=static_cast<std::uint32_t>(q.ping);loss=static_cast<std::uint32_t>((1.0f-q.local)*1000.0f+0.5f);return true;
    }
};
bool stopped(HANDLE h,DWORD ms=0){return !h||WaitForSingleObject(h,ms)!=WAIT_TIMEOUT;}
void run(FILE* log,HANDLE stop){
    HMODULE pinned=nullptr;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&g_marker),&pinned)){std::fprintf(log,"[steam-broker] refused pin\n");return;}
    Native api(log);const auto deadline=GetTickCount64()+60000;
    while(!stopped(stop)&&!api.init()){if(GetTickCount64()>=deadline){std::fprintf(log,"[steam-broker] unavailable existing-session\n");return;}if(stopped(stop,100))return;}
    while(!stopped(stop)&&!api.readyIdentity()){if(GetTickCount64()>=deadline){std::fprintf(log,"[steam-broker] unavailable auth-relay\n");return;}if(stopped(stop,100))return;}
    if(stopped(stop))return;
    std::fprintf(log,"[steam-broker] ready appId=%u identity=%llu modulePinnedUntilExit=1\n",steam::AppId,api.readyIdentity());
    while(!stopped(stop)){
        steam::Pipe pipe;if(!pipe.serve()){std::fprintf(log,"[steam-broker] refused pipe-create\n");return;}
        steam::Broker broker(api);bool attached=false;
        while(!stopped(stop,1)&&pipe.pump()){
            if(!pipe.connected())continue;
            if(!attached){attached=true;std::fprintf(log,"[steam-broker] runtime-attached\n");}
            steam::Frame f;for(unsigned n=0;n<32&&pipe.receive(f);++n){if(!broker.command(f,GetTickCount64()))break;}
            broker.tick(GetTickCount64());
            while(broker.pop(f)){if(f.op==steam::Op::Error)std::fprintf(log,"[steam-broker] refused %.*s\n",static_cast<int>(f.bytes.size()),f.bytes.data());if(!pipe.send(f))break;}
            if(broker.failed()){pipe.pump();break;}
        }
        broker.stop();pipe.close();std::fprintf(log,"[steam-broker] runtime-detached all-owned-sockets-closed\n");
        // Overflow leaves dispatch history incomplete: require process restart.
        if(!api.healthy())return;
    }
}
}
void Run(HANDLE stop) noexcept {
    char enabled[8]{};if(GetEnvironmentVariableA("KH2COOP_STEAM_BROKER",enabled,sizeof(enabled))!=1||enabled[0]!='1')return;
    wchar_t directory[32768]{};const auto n=GetEnvironmentVariableW(L"KH2COOP_LOG_DIR",directory,32768);
    if(!n||n>=32768)return;wchar_t path[32768]{};
    if(swprintf_s(path,L"%s\\steam-broker_%lu.log",directory,GetCurrentProcessId())<0)return;
    FILE* log=nullptr;if(_wfopen_s(&log,path,L"wx")||!log)return;setvbuf(log,nullptr,_IONBF,0);
    try{run(log,stop);}catch(...){std::fprintf(log,"[steam-broker] exception stopped\n");}std::fclose(log);
}
} // namespace kh2coop::steambroker
