// Flat ABI mock only; no Steam DLL/session/game is loaded or called.
#include <windows.h>
#include <cstring>
#include <map>
#include <string>
#include <iostream>
#include <thread>
#include <vector>
#include <deque>
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
int failures=0,creations=0,closes=0,initializations=0,acceptCalls=0;bool optionsGood=true;int configValue=0,configType=1,configResult=1;std::uint32_t app=2552430;int pipeId=1,relayState=100,sendResult=1;bool isLoggedOn=true,infoReady=false;unsigned identityCalls=0,infoCalls=0;int infoState=3,infoFlags=16,infoReason=0;
void check(bool v,const char* text){std::cout<<(v?"PASS ":"FAIL ")<<text<<'\n';if(!v)++failures;}
template<class T>void bind(const char* n,T fn){FARPROC p=nullptr;static_assert(sizeof(p)==sizeof(fn));std::memcpy(&p,&fn,sizeof(p));exports[n]=p;}
int __cdecl integer(){return 1;}int __cdecl pipeHandle(){return pipeId;}void* __cdecl pointer(){return reinterpret_cast<void*>(1);}
bool __cdecl logged(void*){return isLoggedOn;}std::uint64_t __cdecl identity(void*){++identityCalls;return 76561198000000001ull;}
std::uint32_t __cdecl appid(void*){return app;}
int __cdecl initAuth(void*){++initializations;return 100;}void __cdecl initRelay(void*){++initializations;}
int __cdecl auth(void*,Auth* a){a->availability=100;return 100;}int __cdecl relay(void*,Relay* r){r->availability=r->config=r->any=relayState;return relayState;}
void (__cdecl *registeredCallback)(Change*)=nullptr;
std::vector<Change> pending;unsigned pumpCalls=0;
void __cdecl pump(void*){++pumpCalls;auto events=std::move(pending);pending.clear();for(auto& event:events)registeredCallback(&event);}
void options(int n,const Config* o){std::memcpy(&registeredCallback,&o[1].value.pointer,sizeof(registeredCallback));++creations;optionsGood=optionsGood&&n==4&&o[0].key==104&&o[0].type==1&&o[0].value.number==0&&o[1].key==201&&o[1].value.pointer&&o[2].key==9&&o[2].value.number==524288&&o[3].key==24&&o[3].value.number==30000;}
std::uint32_t __cdecl listen(void*,int p,int n,const Config* o){options(n,o);optionsGood=optionsGood&&p==27795;return 20;}
std::uint32_t __cdecl connect(void*,const Identity* id,int p,int n,const Config* o){options(n,o);optionsGood=optionsGood&&p==27795&&id->type==16&&id->size==8;return 21;}
int __cdecl accept(void*,std::uint32_t){++acceptCalls;return 1;}
int closeReason=0;bool closeLinger=false;std::string closeDebug;
bool __cdecl close(void*,std::uint32_t,int reason,const char* debug,bool linger){++closes;closeReason=reason;closeDebug=debug;closeLinger=linger;return true;}
bool __cdecl closeListen(void*,std::uint32_t){++closes;return true;}
int __cdecl config(void*,int key,int scope,std::intptr_t,int* type,void* data,std::size_t* n){optionsGood=optionsGood&&key==104&&(scope==3||scope==4);*type=configType;*n=4;std::memcpy(data,&configValue,4);return configResult;}
bool __cdecl info(void*,std::uint32_t,Info* out){++infoCalls;out->state=infoState;out->flags=infoFlags;out->reason=infoReason;return infoReady;}
int __cdecl send(void*,std::uint32_t,const void*,std::uint32_t,int,std::int64_t*){return sendResult;}
std::deque<Message*> queuedMessages;unsigned releases=0,receiveCalls=0;
void releaseMessage(Message*){++releases;}
int __cdecl receive(void*,std::uint32_t,Message** out,int){++receiveCalls;if(queuedMessages.empty())return 0;*out=queuedMessages.front();queuedMessages.pop_front();return 1;}
int pendingReliable=0,unackedReliable=0;
int __cdecl quality(void*,std::uint32_t,Quality* q,int n,void*){q->pendingReliable=pendingReliable;q->unacked=unackedReliable;q->state=3;q->ping=50;q->local=0.98f;return n==0?1:0;}
void __cdecl setid(Identity* i,std::uint64_t id){i->type=16;i->size=8;std::memcpy(i->data,&id,8);}
std::uint64_t __cdecl getid(const Identity* i){std::uint64_t id=0;if(i->type==16&&i->size==8)std::memcpy(&id,i->data,8);return id;}
std::string logText(FILE* log){std::fflush(log);const auto pos=std::ftell(log);std::rewind(log);std::string text(static_cast<std::size_t>(pos),'\0');std::fread(text.data(),1,text.size(),log);std::fseek(log,pos,SEEK_SET);return text;}
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
    bind("SteamAPI_ISteamNetworkingSockets_RunCallbacks",pump);
    FILE* log=nullptr;tmpfile_s(&log);if(!log)return 2;
    Native api(log);app=480;check(!api.init()&&initializations==0,"wrong app refuses before auth/relay calls");app=2552430;
    check(api.init()&&api.readyIdentity()==76561198000000001ull,"existing correct app/interface readiness");
    check(initializations==2&&logText(log).find("relay-init phase=existing-session")!=std::string::npos,
          "auth and relay warmup called once before readiness");
    relayState=1;check(api.readyIdentity()==0,"relay measuring blocks readiness");relayState=100;
    check(api.readyIdentity()!=0&&logText(log).find("relay-status availability=1")!=std::string::npos&&
          logText(log).find("relay-status availability=100")!=std::string::npos,
          "relay status changes are logged before initial ready gate");
    check(api.listen()==20&&api.connect(76561198000000002ull)==21&&creations==2&&optionsGood,"ICE zero and callback configured AT both real P2P creation calls");
    infoReady=true;infoState=1;infoFlags=0;api.pollDiagnostics(1000);kh2coop::steam::Status polled;
    check(logText(log).find("stateName=Connecting oldState=0 flags=0 endReason=0 callbacksReceived=0")!=std::string::npos&&
          !api.nextStatus(polled),"connection poll exposes Connecting with no callback without manufacturing admission");
    const auto polls=infoCalls;api.pollDiagnostics(1500);check(infoCalls==polls,"connection diagnostics rate limited to once per second");
    infoState=2;api.pollDiagnostics(2000);
    check(logText(log).find("stateName=FindingRoute oldState=1")!=std::string::npos,"poll observes FindingRoute even without callback delivery");
    infoState=3;infoFlags=16;api.pollDiagnostics(3000);
    check(logText(log).find("stateName=Connected oldState=2")!=std::string::npos&&!api.nextStatus(polled),
          "polled Connected is diagnostic only, never a substitute for callback admission");
    for(int state=1;state<=3;++state){
        Change queued{};queued.handle=21;queued.info.state=state;queued.oldState=state-1;
        queued.info.flags=state==3?16:0;setid(&queued.info.identity,76561198000000002ull);
        pending.push_back(queued);
        check(!api.nextStatus(polled),"SDK pending status is not delivered without sockets pump");
        const auto before=pumpCalls;const auto tick=std::uint64_t(4000+state*16);
        api.pumpCallbacks(tick);
        check(pumpCalls==before+1&&api.nextStatus(polled)&&polled.state==state,
              "broker sockets pump dispatches registered config201 pending status");
        api.pumpCallbacks(tick+15);check(pumpCalls==before+1,"sockets pump rate limited to 16ms");
    }
    check(api.reliableDrained(21),"native reliable drain sees empty acknowledged queue");
    pendingReliable=1;check(!api.reliableDrained(21),"native reliable drain waits for queued bytes");
    pendingReliable=0;unackedReliable=1;check(!api.reliableDrained(21),"native reliable drain waits for unacknowledged bytes");unackedReliable=0;
    std::uint8_t gameBytes[]={0x4b,0x53,1,0,1,0,0,0,42};
    std::uint8_t endBytes[]={0x4b,0x53,1,0,1,1,0,0,5};
    Message lastGameplay{},sessionEnd{};
    lastGameplay.data=gameBytes;lastGameplay.size=sizeof(gameBytes);lastGameplay.handle=21;lastGameplay.flags=8;lastGameplay.release=releaseMessage;setid(&lastGameplay.identity,76561198000000002ull);
    sessionEnd=lastGameplay;sessionEnd.data=endBytes;
    queuedMessages={&lastGameplay,&sessionEnd};infoState=4;infoFlags=0;infoReason=1005;
    Change ended{};ended.handle=21;ended.info.state=4;ended.info.reason=1005;setid(&ended.info.identity,76561198000000002ull);Changed(&ended);
    check(api.nextStatus(polled)&&polled.reason==5,"native dequeues ClosedByPeer1005 before the retained reliable receive backlog");
    const auto beforeReceive=receiveCalls;kh2coop::steam::Message received;
    check(!api.receive(22,received)&&receiveCalls==beforeReceive&&queuedMessages.size()==2,"terminal receive cannot adopt an unowned or never-authenticated handle");
    check(api.receive(21,received)&&received.bytes.back()==42&&received.reliable,"native ClosedByPeer drains queued gameplay from the previously authenticated connection");
    check(api.receive(21,received)&&received.bytes==std::vector<std::uint8_t>(std::begin(endBytes),std::end(endBytes))&&releases==2,"native terminal receive preserves session-end FIFO and releases each message once");
    check(!api.receive(21,received)&&queuedMessages.empty(),"native terminal receive reports empty only after queued messages drain");
    infoState=3;infoFlags=16;infoReason=0;
    api.close(21,true,5);check(closeReason==1005&&closeLinger&&closeDebug=="KH2 host left","native host leave uses app-specific reason, debug string and linger");
    const auto workerPumps=pumpCalls;
    std::thread wrongThread([&]{api.pumpCallbacks(5000);});wrongThread.join();
    check(pumpCalls==workerPumps,"foreign thread cannot invoke broker sockets pump");
    check(logText(log).find("callback-pump started")!=std::string::npos,
          "explicit pump start has a thread/pipe receipt");
    check(api.iceOff(20,true)&&api.iceOff(21,false),"listen and connection handle configuration verified");
    configResult=2;check(api.iceOff(21,false),"effective inherited zero is accepted");configResult=1;
    configValue=-1;check(!api.iceOff(21,false),"default ICE value refused");configValue=0;configType=5;check(!api.iceOff(21,false),"wrong config type refused");configType=1;configResult=-1;check(!api.iceOff(21,false),"unsupported config refused");configResult=1;
    Change c{};c.handle=21;c.info.state=3;c.info.flags=16;setid(&c.info.identity,76561198000000002ull);Changed(&c);kh2coop::steam::Status s;
    check(api.nextStatus(s)&&s.handle==21&&s.identity==76561198000000002ull&&s.authenticated&&s.relay,"callback exact ABI and authenticated relay facts");
    for(int state=1;state<=5;++state){c.oldState=state-1;c.info.state=state;c.info.reason=5003;Changed(&c);
        check(api.nextStatus(s)&&s.state==state,"each lifecycle callback state is dequeued without filtering");}
    const auto allStates=logText(log);
    check(allStates.find("stateName=Connecting")!=std::string::npos&&allStates.find("stateName=FindingRoute")!=std::string::npos&&
          allStates.find("stateName=ClosedByPeer")!=std::string::npos&&
          allStates.find("stateName=ProblemDetectedLocally oldState=4 endReason=5003")!=std::string::npos,
          "callback receipts retain lifecycle names, old state and raw SDK end reason");
    c.info.state=1;c.info.reason=0;c.info.flags=2;Changed(&c);
    check(api.nextStatus(s)&&s.authenticated&&s.flags==2&&!s.relay,
          "Connecting flag2 is encryption pending, not unauthenticated identity");
    c.info.state=3;c.info.flags=3;Changed(&c);api.nextStatus(s);check(!s.authenticated&&!s.relay,"unauthenticated unencrypted flags refused facts");
    for(unsigned i=0;i<65;++i)Changed(&c);check(!api.healthy(),"callback overflow fails closed");
    const auto ownedBefore=closes;api.close(21,false);api.closeListener(20);check(closes==ownedBefore+2,"own handles explicitly close");
    const auto closedPolls=infoCalls;api.pollDiagnostics(4000);check(infoCalls==closedPolls,"closed handles removed from diagnostics without polling foreign handles");
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
    api.resetSession();
    {
        kh2coop::steam::Broker broker(api);kh2coop::steam::Frame host{kh2coop::steam::Op::Host};
        constexpr std::uint64_t allowedPeer=76561198000000002ull,foreignPeer=76561198000000003ull;
        for(unsigned i=0;i<8;++i)host.bytes.push_back(static_cast<std::uint8_t>(allowedPeer>>(8*i)));
        check(broker.command({kh2coop::steam::Op::Hello},100)&&broker.command(host,100),
              "actual IPC Host payload configures native broker allowlist");
        kh2coop::steam::Frame out;while(broker.pop(out)){}
        Change incoming{};incoming.handle=30;incoming.info.listener=20;incoming.info.state=1;incoming.info.flags=2;
        setid(&incoming.info.identity,allowedPeer);pending.push_back(incoming);
        const auto acceptedBefore=acceptCalls;api.pumpCallbacks(6000);
        check(broker.tick(101)&&acceptCalls==acceptedBefore+1&&!broker.pop(out),
              "real adapter flag2 callback reaches actual broker Accept without early admission");
        check(logText(log).find("decision action=accept reason=allowlisted-authenticated-connecting handle=30 peer=76561198000000002 allowlistSize=1 flags=2")!=std::string::npos,
              "actual native accept log retains identity, allowlist size and reason");
        incoming.handle=31;setid(&incoming.info.identity,foreignPeer);pending.push_back(incoming);api.pumpCallbacks(6016);
        const auto closedBefore=closes;
        check(broker.tick(102)&&closes==closedBefore+1&&acceptCalls==acceptedBefore+1,
              "actual native broker refuses non-allowlisted Connecting");
        check(logText(log).find("decision action=close reason=incoming-not-allowlisted handle=31 peer=76561198000000003 allowlistSize=1 flags=2")!=std::string::npos,
              "actual native close log names refusal, allowlist size and SDK peer");
        incoming.handle=30;setid(&incoming.info.identity,allowedPeer);incoming.info.state=3;incoming.info.flags=18;
        pending.push_back(incoming);api.pumpCallbacks(6032);
        check(!broker.tick(103)&&logText(log).find("reason=connection-not-authenticated-relay-only handle=30")!=std::string::npos,
              "encryption is still mandatory at Connected with a logged close");
    }
    api.resetSession();
    relayState=1;check(api.readyIdentity()==0&&api.currentIdentity()==76561198000000001ull,"relay measuring only blocks initial readiness, not established identity");relayState=100;
    isLoggedOn=false;check(api.currentIdentity()==0,"established logged-off state refused");isLoggedOn=true;
    infoReady=true;sendResult=25;const std::uint8_t byte=1;
    check(!api.send(21,std::span(&byte,1),true)&&api.congested(),"actual SDK LimitExceeded distinguished from closed peer");
    sendResult=8;check(!api.send(21,std::span(&byte,1),true)&&!api.congested(),"other SDK send failure is not backpressure");
    sendResult=1;check(api.send(21,std::span(&byte,1),true)&&!api.congested(),"successful retry clears send backpressure state");
    const auto calls=identityCalls;pipeId=0;
    const auto endedPumps=pumpCalls;api.pumpCallbacks(7000);
    check(pumpCalls==endedPumps,"closed Steam pipe prevents sockets pump");
    check(!api.pipeAlive()&&api.currentIdentity()==0&&identityCalls==calls,"closed game Steam pipe refuses before interface identity IPC");
    const auto previousCloses=closes,previousCreations=creations;
    api.close(21,false);api.closeListener(20);
    check(api.listen()==0&&api.connect(76561198000000002ull)==0&&closes==previousCloses&&creations==previousCreations,"shutdown guard avoids calls even during broker destructor cleanup");
    pipeId=1;check(!api.pipeAlive(),"observed shutdown remains terminal for old API pointers");
    std::fclose(log);std::cout<<"failures="<<failures<<'\n';return failures?1:0;
}
