// Flat ABI mock only; no Steam DLL/session/game is loaded or called.
#include <windows.h>
#include <cstring>
#include <map>
#include <string>
#include <iostream>
#include <thread>
namespace {
std::map<std::string,FARPROC> exports;
unsigned moduleCalls=0;
HMODULE WINAPI fakeModule(LPCWSTR){++moduleCalls;return reinterpret_cast<HMODULE>(1);}
FARPROC WINAPI fakeExport(HMODULE,LPCSTR key){const auto i=exports.find(key);return i==exports.end()?nullptr:i->second;}
}
#define GetModuleHandleW fakeModule
#define GetProcAddress fakeExport
#include "../inject/src/SteamBrokerNative.cpp"
#undef GetModuleHandleW
#undef GetProcAddress
using namespace kh2coop::steambroker;
namespace {
int failures=0,creations=0,closes=0,initializations=0;bool optionsGood=true;int configValue=0,configType=1,configResult=1;std::uint32_t app=2552430;int pipeId=1,relayState=100,sendResult=1;bool isLoggedOn=true,infoReady=false;unsigned identityCalls=0;
void check(bool v,const char* text){std::cout<<(v?"PASS ":"FAIL ")<<text<<'\n';if(!v)++failures;}
template<class T>void bind(const char* n,T fn){FARPROC p=nullptr;static_assert(sizeof(p)==sizeof(fn));std::memcpy(&p,&fn,sizeof(p));exports[n]=p;}
int __cdecl integer(){return 1;}int __cdecl pipeHandle(){return pipeId;}void* __cdecl pointer(){return reinterpret_cast<void*>(1);}
bool __cdecl logged(void*){return isLoggedOn;}std::uint64_t __cdecl identity(void*){++identityCalls;return 76561198000000001ull;}
std::uint32_t __cdecl appid(void*){return app;}
int __cdecl initAuth(void*){++initializations;return 100;}void __cdecl initRelay(void*){++initializations;}
int __cdecl auth(void*,Auth* a){a->availability=100;return 100;}int __cdecl relay(void*,Relay* r){r->availability=r->config=r->any=relayState;return relayState;}
void options(int n,const Config* o){++creations;optionsGood=optionsGood&&n==4&&o[0].key==104&&o[0].type==1&&o[0].value.number==0&&o[1].key==201&&o[1].value.pointer&&o[2].key==9&&o[2].value.number==524288&&o[3].key==24&&o[3].value.number==15000;}
std::uint32_t __cdecl listen(void*,int p,int n,const Config* o){options(n,o);optionsGood=optionsGood&&p==27795;return 20;}
std::uint32_t __cdecl connect(void*,const Identity* id,int p,int n,const Config* o){options(n,o);optionsGood=optionsGood&&p==27795&&id->type==16&&id->size==8;return 21;}
int __cdecl accept(void*,std::uint32_t){return 1;}
bool __cdecl close(void*,std::uint32_t,int,const char*,bool){++closes;return true;}
bool __cdecl closeListen(void*,std::uint32_t){++closes;return true;}
int __cdecl config(void*,int key,int scope,std::intptr_t,int* type,void* data,std::size_t* n){optionsGood=optionsGood&&key==104&&(scope==3||scope==4);*type=configType;*n=4;std::memcpy(data,&configValue,4);return configResult;}
bool __cdecl info(void*,std::uint32_t,Info* out){out->state=3;out->flags=16;return infoReady;}
int __cdecl send(void*,std::uint32_t,const void*,std::uint32_t,int,std::int64_t*){return sendResult;}
int __cdecl receive(void*,std::uint32_t,Message**,int){return 0;}
int __cdecl quality(void*,std::uint32_t,Quality* q,int n,void*){q->state=3;q->ping=50;q->local=0.98f;return n==0?1:0;}
void __cdecl setid(Identity* i,std::uint64_t id){i->type=16;i->size=8;std::memcpy(i->data,&id,8);}
std::uint64_t __cdecl getid(const Identity* i){std::uint64_t id=0;if(i->type==16&&i->size==8)std::memcpy(&id,i->data,8);return id;}
}
int main(){
    SetEnvironmentVariableA("KH2COOP_STEAM_BROKER","0");Run(nullptr);check(moduleCalls==0,"default-off gate precedes every Steam lookup/call");
    bind("SteamAPI_GetHSteamUser",integer);bind("SteamAPI_GetHSteamPipe",pipeHandle);
    for(auto n:{"SteamAPI_SteamUser_v023","SteamAPI_SteamUtils_v010","SteamAPI_SteamNetworkingSockets_SteamAPI_v012","SteamAPI_SteamNetworkingUtils_SteamAPI_v004"})bind(n,pointer);
    bind("SteamAPI_ISteamUser_BLoggedOn",logged);bind("SteamAPI_ISteamUser_GetSteamID",identity);bind("SteamAPI_ISteamUtils_GetAppID",appid);
    bind("SteamAPI_ISteamNetworkingSockets_InitAuthentication",initAuth);bind("SteamAPI_ISteamNetworkingSockets_GetAuthenticationStatus",auth);
    bind("SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess",initRelay);bind("SteamAPI_ISteamNetworkingUtils_GetRelayNetworkStatus",relay);
    bind("SteamAPI_ISteamNetworkingSockets_CreateListenSocketP2P",listen);bind("SteamAPI_ISteamNetworkingSockets_ConnectP2P",connect);
    bind("SteamAPI_ISteamNetworkingSockets_AcceptConnection",accept);bind("SteamAPI_ISteamNetworkingSockets_CloseConnection",close);
    bind("SteamAPI_ISteamNetworkingSockets_CloseListenSocket",closeListen);bind("SteamAPI_ISteamNetworkingUtils_GetConfigValue",config);
    bind("SteamAPI_ISteamNetworkingSockets_GetConnectionInfo",info);bind("SteamAPI_ISteamNetworkingSockets_SendMessageToConnection",send);
    bind("SteamAPI_ISteamNetworkingSockets_ReceiveMessagesOnConnection",receive);bind("SteamAPI_SteamNetworkingIdentity_SetSteamID64",setid);bind("SteamAPI_SteamNetworkingIdentity_GetSteamID64",getid);
    bind("SteamAPI_ISteamNetworkingSockets_GetConnectionRealTimeStatus",quality);
    FILE* log=nullptr;tmpfile_s(&log);if(!log)return 2;
    Native api(log);app=480;check(!api.init()&&initializations==0,"wrong app refuses before auth/relay calls");app=2552430;
    check(api.init()&&api.readyIdentity()==76561198000000001ull,"existing correct app/interface readiness");
    check(api.listen()==20&&api.connect(76561198000000002ull)==21&&creations==2&&optionsGood,"ICE zero and callback configured AT both real P2P creation calls");
    check(api.iceOff(20,true)&&api.iceOff(21,false),"listen and connection handle configuration verified");
    configResult=2;check(api.iceOff(21,false),"effective inherited zero is accepted");configResult=1;
    configValue=-1;check(!api.iceOff(21,false),"default ICE value refused");configValue=0;configType=5;check(!api.iceOff(21,false),"wrong config type refused");configType=1;configResult=-1;check(!api.iceOff(21,false),"unsupported config refused");configResult=1;
    Change c{};c.handle=21;c.info.state=3;c.info.flags=16;setid(&c.info.identity,76561198000000002ull);Changed(&c);kh2coop::steam::Status s;
    check(api.nextStatus(s)&&s.handle==21&&s.identity==76561198000000002ull&&s.authenticated&&s.relay,"callback exact ABI and authenticated relay facts");
    c.info.flags=3;Changed(&c);api.nextStatus(s);check(!s.authenticated&&!s.relay,"unauthenticated unencrypted flags refused facts");
    for(unsigned i=0;i<65;++i)Changed(&c);check(!api.healthy(),"callback overflow fails closed");
    api.close(21,false);api.closeListener(20);check(closes==2,"own handles explicitly close");
    std::uint32_t rtt=0,loss=0;check(api.quality(21,rtt,loss)&&rtt==50&&loss==20,"SDK ping and observed delivery-quality stats mapped");
    api.resetSession();check(api.healthy()&&!api.nextStatus(s),"closed-session reset clears overflow and lost queue history");
    bool ordered=true;c.info.flags=16;
    for(unsigned i=0;i<48;++i){c.handle=i+1;Changed(&c);}
    for(unsigned i=0;i<32;++i){ordered=api.nextStatus(s)&&s.handle==i+1&&ordered;}
    for(unsigned i=0;i<32;++i){c.handle=i+49;Changed(&c);}
    for(unsigned i=32;i<80;++i){ordered=api.nextStatus(s)&&s.handle==i+1&&ordered;}
    check(ordered&&api.healthy()&&!api.nextStatus(s),"O(1) callback ring wraps while preserving exact FIFO");
    api.resetSession();std::atomic<bool> callbackStarted=false;
    AcquireSRWLockExclusive(&g_lock);
    std::thread callback([&]{callbackStarted.store(true);Changed(&c);});
    while(!callbackStarted.load())std::this_thread::yield();
    const bool noFalseOverflow=!g_overflow.load();ReleaseSRWLockExclusive(&g_lock);callback.join();
    check(noFalseOverflow&&api.healthy()&&api.nextStatus(s),"callback lock contention waits for O(1) section without latching loss");
    relayState=1;check(api.readyIdentity()==0&&api.currentIdentity()==76561198000000001ull,"relay measuring only blocks initial readiness, not established identity");relayState=100;
    isLoggedOn=false;check(api.currentIdentity()==0,"established logged-off state refused");isLoggedOn=true;
    infoReady=true;sendResult=25;const std::uint8_t byte=1;
    check(!api.send(21,std::span(&byte,1),true)&&api.congested(),"actual SDK LimitExceeded distinguished from closed peer");
    sendResult=8;check(!api.send(21,std::span(&byte,1),true)&&!api.congested(),"other SDK send failure is not backpressure");
    sendResult=1;check(api.send(21,std::span(&byte,1),true)&&!api.congested(),"successful retry clears send backpressure state");
    const auto calls=identityCalls;pipeId=0;
    check(!api.pipeAlive()&&api.currentIdentity()==0&&identityCalls==calls,"closed game Steam pipe refuses before interface identity IPC");
    const auto previousCloses=closes,previousCreations=creations;
    api.close(21,false);api.closeListener(20);
    check(api.listen()==0&&api.connect(76561198000000002ull)==0&&closes==previousCloses&&creations==previousCreations,"shutdown guard avoids calls even during broker destructor cleanup");
    pipeId=1;check(!api.pipeAlive(),"observed shutdown remains terminal for old API pointers");
    std::fclose(log);std::cout<<"failures="<<failures<<'\n';return failures?1:0;
}
