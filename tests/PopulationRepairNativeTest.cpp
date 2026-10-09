// Actual population integration functions over owned read-only stand-ins. Never attaches to KH2.
#include "kh2coop/Codec.hpp"
#include "kh2coop/PopulationCutJson.hpp"
#include "NativeSpawnController.hpp"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <unordered_map>
using namespace kh2coop;
namespace kh2coop::inject {
static bool owner=true;
namespace spawncontroller {bool IsDiagnosticGameThread(){return owner;}}
namespace testrepair {
enum class Role {Off,Host,Client};enum class ResyncWriteFence {None,Failed};
static Role g_role=Role::Client;
static Role CurrentRole();
static ResyncWriteFence g_resyncWriteFence=ResyncWriteFence::None;
static bool safe=true,complete=true,catalogComplete=true,memberComplete=true,swapOnFinal=false;
static unsigned identityReads=0,catalogReads=0,memberReads=0;
static int producerMutation=-1;
static uintptr_t memberController=400,memberRecord=500;
static std::uint32_t load=2,transition=1,generation=3;
namespace warp {
static std::uint32_t LoadSerial(){return load;}
static std::uint32_t TransitionSerial(){return transition;}
static bool HostTransitionArrived(std::uint32_t){return true;}
}
struct Bridge {
    std::uint64_t delivery=4,hostDelivery=5,host=10,self=20;
    std::uint64_t ConnectionId(unsigned slot)const{return slot==0?host:self;}
    std::uint64_t DeliverySerial()const{return delivery;}
    std::uint64_t PeerDeliverySerial(unsigned)const{return hostDelivery;}
    unsigned LocalSlot()const{return 2;}
} g_bridge;
static std::uint32_t WorldSessionGeneration(){return generation;}
static RoomTransition location{2,5,6,0,1,1,0};
static bool SafeNativeGameplay(){return safe;}
static bool ReadLocationChecked(RoomTransition& out){out=location;return true;}
struct NativeEnemy {uintptr_t actor=100,objentry=200,status=300;std::uint32_t objectId=302;int hp=20;std::uint32_t flags120=0;};
struct NativeCensus {
    std::uint32_t load=2,transition=1;RoomTransition location{2,5,6,0,1,1,0};
    bool watchedActorPresent=true;std::vector<NativeEnemy> enemies{{}};
};
static NativeCensus currentCensus;
static NativeCensus CaptureNativeCensus(uintptr_t actor=0);
static bool CensusMatchesInstance(const NativeCensus& c){return complete && c.load==load && c.transition==transition;}
static bool CensusHasActor(const NativeCensus& c,uintptr_t actor){return std::any_of(c.enemies.begin(),c.enemies.end(),[&](const auto& e){return e.actor==actor;});}
struct Spawn {
    std::uint16_t spawnIndex=0;std::uint32_t objectId=302;
    uintptr_t actor=100,objentry=200,status=300,controller=400,record=500;bool present=true,identityRead=true,announced=true,deathSent=false;
    int netId=0;bool populationPointCaptured=true;std::array<float,4> populationBirthPoint{10,0,20,1};
};
struct Instance {bool live=true;std::vector<Spawn> spawns{{}};std::unordered_map<uintptr_t,std::size_t> byActor{{100,0}};} g_inst;
static const NativeEnemy* FindNativeEnemy(const NativeCensus& c,const Spawn& s){for(const auto& e:c.enemies)if(e.actor==s.actor)return &e;return nullptr;}
struct Identity {uintptr_t actor,objentry,status;};
static Identity nativeIdentityFixture{100,200,300};
static Identity ReadPopulationIdentity(uintptr_t actor){++identityReads;auto result=nativeIdentityFixture;if(swapOnFinal && identityReads>=2)++result.status;result.actor=actor;return result;}
static uintptr_t nativeController=400,nativeRecord=500;
static int dispatchMutation=-1;
static unsigned nativeCalls=0,recaptures=0,tracks=0;
static bool helperReady=true,nativeFault=false,failAfterCall=false,removeOnCall=false,reenterOnCall=false,resetOnRoleRead=false;
static void MutateAtDispatch();
template<class T> static bool ReadNative(uintptr_t address,T& out){
    if(address==300) {
        if(currentCensus.enemies.empty())return false;
        out=static_cast<T>(currentCensus.enemies[0].hp);MutateAtDispatch();return true;
    }
    out=static_cast<T>(address==100+0x9E8?nativeController:address==100+0x9F0?nativeRecord:0);
    return out!=0;
}
static bool ReadNativeEnemy(uintptr_t actor,NativeEnemy& out,bool& enemy){
    for(const auto& e:currentCensus.enemies)if(e.actor==actor){out=e;enemy=true;return true;}
    enemy=false;return false;
}
static bool SameNativeIdentity(const NativeEnemy& a,const NativeEnemy& b){
    return a.actor==b.actor && a.objentry==b.objentry && a.status==b.status && a.objectId==b.objectId;
}
struct HostEnemy {std::uint32_t objectId=302;bool dead=true;};
struct Host {bool manifestComplete=true,arrived=true;std::uint32_t epoch=2;std::map<std::uint16_t,HostEnemy> enemies{{1,{}}};} g_host;
static bool g_populationRequested=true,g_manifestSent=true;
static std::uint32_t g_epoch=2,g_mirrorFrame=100;
static std::uint64_t g_clientManifestRevision=7;
static constexpr uintptr_t g_exeBase=0x140000000,RVA_ADMISSION_USED=0x2A0F830;
static std::optional<RoomTransition> g_lastRoomPacket=location;
static std::optional<int> g_nativeResync;
static std::vector<std::string> logs;
static void Log(const char* format,...) {char buffer[4096];va_list args;va_start(args,format);std::vsnprintf(buffer,sizeof(buffer),format,args);va_end(args);logs.emplace_back(buffer);}
static auto g_log=&Log;
static std::vector<std::vector<std::uint8_t>> sent;
static bool Send(const std::vector<std::uint8_t>& bytes){sent.push_back(bytes);return true;}
using RecordCatalog=spawncontroller::NativeRecordCatalog;
static RecordCatalog nativeCatalog;
static bool FreshRecordCatalog(const NativeCensus&,RecordCatalog& out){
    out=nativeCatalog;
    if(++catalogReads==2) {
        // Mutations happen after the member was captured, with identical controller tables.
        switch(producerMutation) {
        case 0:++nativeController;break;
        case 1:++nativeRecord;break;
        case 2:++nativeIdentityFixture.status;break;
        case 3:++nativeIdentityFixture.objentry;break;
        case 4:++g_inst.spawns[0].controller;break;
        case 5:++g_inst.spawns[0].record;break;
        case 6:++g_inst.spawns[0].actor;break;
        case 7:g_inst.spawns[0].announced=false;break;
        case 8:g_inst.spawns[0].identityRead=false;break;
        case 9:g_inst.spawns[0].objectId=4;break;
        case 10:++load;break;
        case 11:++transition;break;
        case 12:++generation;break;
        case 13:++location.eventProgram;break;
        case 14:complete=false;break;
        case 15:owner=false;break;
        case 16:safe=false;break;
        case 17:g_inst.spawns[0].deathSent=true;break;
        }
    }
    return catalogComplete;
}
static bool SameRecordCatalogSample(const RecordCatalog&,const RecordCatalog&){return catalogComplete;}
static bool RecordMember(const RecordCatalog&,const NativeCensus&,uintptr_t actor,spawncontroller::NativeRecordMembership& out){
    ++memberReads;out.actor=actor;out.tableIndex=0;out.recordIndex=0;
    out.controller={memberController,memberController};out.record={memberRecord,memberRecord};
    if(memberReads==2) {
        if(producerMutation==18)out.recordIndex=1;
        if(producerMutation==19)++out.controller[1];
        if(producerMutation==20)++out.record[1];
        if(producerMutation==21)out.tableIndex=1;
    }
    return memberComplete;
}
static std::uint32_t RecordObjectId(const std::array<std::uint8_t,64>& bytes){return population_cut_detail::object(bytes);}
#include "EnemyPopulationRepair.inl"
static Role CurrentRole(){if(resetOnRoleRead){resetOnRoleRead=false;ResetPopulationRepair();}return g_role;}
static NativeCensus CaptureNativeCensus(uintptr_t actor){
    ++recaptures;auto c=currentCensus;
    c.watchedActorPresent=std::any_of(c.enemies.begin(),c.enemies.end(),[&](const auto& e){return e.actor==actor;});return c;
}
bool PopulationDeathHelperReady(){return helperReady;}
std::vector<std::size_t> TrackSpawns(const NativeCensus& census){
    ++tracks;for(auto& spawn:g_inst.spawns)spawn.present=CensusHasActor(census,spawn.actor);return {};
}
bool InvokePopulationNativeDeath(uintptr_t actor,int hp){
    ++nativeCalls;
    if(reenterOnCall){auto c=currentCensus;ConsumePopulationDisposals(100,c);}
    if(nativeFault)return false;
    for(auto& e:currentCensus.enemies)if(e.actor==actor)e.hp-=hp;
    if(removeOnCall)currentCensus.enemies.clear();
    if(failAfterCall)complete=false;
    return true;
}
static void MutateAtDispatch(){
    const auto mutation=dispatchMutation;dispatchMutation=-1;
    switch(mutation){
    case 0:++nativeIdentityFixture.status;break;
    case 1:++nativeIdentityFixture.objentry;break;
    case 2:++nativeController;break;
    case 3:++nativeRecord;break;
    case 4:++load;break;
    case 5:++transition;break;
    case 6:++generation;break;
    case 7:++location.eventProgram;break;
    case 8:++g_bridge.delivery;break;
    case 9:++g_clientManifestRevision;break;
    case 10:++g_populationCut->entries[0].terminalSequence;break;
    case 11:g_host.enemies[1].dead=false;break;
    case 12:g_inst.spawns[0].netId=1;break;
    case 13:owner=false;break;
    case 14:safe=false;break;
    case 15:ResetPopulationRepair();break;
    case 16:++currentCensus.enemies[0].objectId;break;
    case 17:currentCensus.enemies[0].hp=0;break;
    case 18:complete=false;break;
    }
}
static bool logged(const char* text){return std::any_of(logs.begin(),logs.end(),[&](const auto& line){return line.find(text)!=std::string::npos;});}

static PopulationCut certificate() {
    PopulationCut cut;cut.epoch=2;cut.hostLoad=2;cut.hostTransition=1;cut.sequence=100;cut.location={5,6,0,1,1,0};
    NativeRecordContentDefinition d;d.location=cut.location;d.groupKey=808476514;d.layoutSha256.fill(1);d.header[4]=1;
    d.records.resize(1);d.records[0][0]=0x2e;d.records[0][1]=1;cut.definitions.push_back(d);
    cut.entries.push_back({1,0,0,302,80,80,90,true,{10,0,20,1}});return cut;
}
static void baseline() {
    producerMutation=-1;catalogReads=memberReads=0;memberController=400;memberRecord=500;sent.clear();
    dispatchMutation=-1;nativeCalls=recaptures=tracks=0;helperReady=true;
    nativeFault=failAfterCall=removeOnCall=reenterOnCall=resetOnRoleRead=false;
    g_populationDeathAttempts.clear();g_populationConsumerActive=false;
    g_populationOutcomeSequence=g_populationTicketSequence=g_populationOutcomeLoss=0;
    ResetPopulationRepair();owner=safe=complete=catalogComplete=memberComplete=true;swapOnFinal=false;identityReads=0;
    load=2;transition=1;generation=3;g_bridge={};g_role=Role::Client;g_resyncWriteFence=ResyncWriteFence::None;
    nativeIdentityFixture={100,200,300};nativeController=400;nativeRecord=500;currentCensus={};g_inst={};g_host={};g_nativeResync.reset();logs.clear();
    location={2,5,6,0,1,1,0};g_lastRoomPacket=location;g_clientManifestRevision=7;
    const auto cut=certificate();nativeCatalog={};nativeCatalog.entryCount=1;nativeCatalog.entries[0].content=cut.definitions[0];nativeCatalog.entries[0].controller=400;
    ReceivePopulationCut(cut,WorldScope{"session",10,5,1000,20,4,WorldSourceKind::Native});TickPopulationRepair(100,currentCensus);
}
static void producerBaseline() {
    baseline();ResetPopulationRepair();g_role=Role::Host;catalogReads=memberReads=0;identityReads=0;sent.clear();
    // Two valid same-family ordinals make final ordinal substitution a real comparison test.
    nativeCatalog.entries[0].content.records.push_back(nativeCatalog.entries[0].content.records[0]);
    nativeCatalog.entries[0].content.header[4]=2;
}
} // namespace testrepair
} // namespace kh2coop::inject
static int failures=0;
#define CHECK(x) do {if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);++failures;}}while(0)
int main() {
    using namespace kh2coop::inject::testrepair;
    baseline();CHECK(g_populationInstalled && g_populationDisposals.size()==1 && PopulationStaleAuthorized(100));
    CHECK(logged("population-cut-installed-json") && logged("disposal-outcome-json") && logged("stale-native-json"));
    for(int mutation=0;mutation<22;++mutation) {
        baseline();
        switch(mutation) {
        case 0:kh2coop::inject::owner=false;break;
        case 1:safe=false;break;
        case 2:++nativeIdentityFixture.status;break;
        case 3:++nativeIdentityFixture.objentry;break;
        case 4:++load;break;
        case 5:++transition;break;
        case 6:++generation;break;
        case 7:++g_bridge.delivery;break;
        case 8:++g_bridge.host;break;
        case 9:++g_bridge.hostDelivery;break;
        case 10:++location.eventProgram;break;
        case 11:++g_clientManifestRevision;break;
        case 12:g_resyncWriteFence=ResyncWriteFence::Failed;break;
        case 13:g_inst.spawns[0].netId=6;break;
        case 14:catalogComplete=false;break;
        case 15:g_host.enemies[1].dead=false;break;
        case 16:swapOnFinal=true;identityReads=0;break;
        case 17:++nativeController;break;
        case 18:++nativeRecord;break;
        case 19:nativeCatalog.entryCount=2;nativeCatalog.entries[1]=nativeCatalog.entries[0];break;
        case 20:currentCensus.enemies[0].objectId=4;break;
        case 21:++g_populationCut->sequence;break;
        }
        CHECK(!PopulationStaleAuthorized(100));
        ConsumePopulationDisposals(100,currentCensus);CHECK(nativeCalls==0);
    }
    // A bit-28-clear native Shadow must be consumed on the owner frame without the removal predicate.
    baseline();CHECK(currentCensus.enemies[0].flags120==0);
    const auto capturesBefore=recaptures;
    CHECK(!ConsumePopulationDisposals(100,currentCensus));
    CHECK(nativeCalls==1 && tracks==1 && recaptures>capturesBefore);
    CHECK(currentCensus.enemies[0].hp==0 && g_populationDisposals[0].dispatched);
    CHECK(logged("call-returned") && !logged("\"action\":\"disposed\""));
    CHECK(ConsumePopulationDisposals(101,currentCensus));CHECK(nativeCalls==1);
    currentCensus.enemies.clear();TickPopulationRepair(102,currentCensus);
    CHECK(g_populationDisposals.empty() && logged("\"action\":\"disposed\""));
    // A fresh accepted cut reauthorizes the same terminal roots, never the native one-shot.
    for(int mode=0;mode<3;++mode){
        baseline();nativeFault=mode==1;failAfterCall=mode==2;
        CHECK(!ConsumePopulationDisposals(100,currentCensus));CHECK(nativeCalls==1);
        complete=true;nativeFault=failAfterCall=false;
        ResetPopulationRepair();auto cut=certificate();cut.sequence=101;
        ReceivePopulationCut(cut,WorldScope{"session",10,5,1001,20,4,WorldSourceKind::Native});
        TickPopulationRepair(101,currentCensus);CHECK(g_populationDisposals.size()==1);
        CHECK(ConsumePopulationDisposals(101,currentCensus));CHECK(nativeCalls==1);
        CHECK(g_populationDisposals[0].dispatched && logged("one-shot-poisoned"));
        currentCensus.enemies.clear();TickPopulationRepair(102,currentCensus);
        CHECK(logged("\"action\":\"disposed\""));
    }
    // Every final HP-read adversary runs through the actual consumer and dispatch checks.
    for(int mutation=0;mutation<19;++mutation){
        baseline();dispatchMutation=mutation;
        ConsumePopulationDisposals(100,currentCensus);
        if(nativeCalls!=0 || !g_populationDeathAttempts.empty())std::printf("dispatch mutation=%d escaped refusal\n",mutation);
        CHECK(nativeCalls==0 && g_populationDeathAttempts.empty());CHECK(logged("refused"));
    }
    baseline();g_populationDeathAttempts.resize(POPULATION_MAX_ENTRIES*2);
    CHECK(ConsumePopulationDisposals(100,currentCensus));CHECK(nativeCalls==0 && logged("attempt-journal-cap"));
    baseline();helperReady=false;CHECK(ConsumePopulationDisposals(100,currentCensus));
    CHECK(nativeCalls==0 && logged("native-helper-unqualified"));
    baseline();currentCensus.enemies[0].hp=0;CHECK(ConsumePopulationDisposals(100,currentCensus));
    CHECK(nativeCalls==0 && logged("native-hp-nonpositive") && !logged("\"action\":\"disposed\""));
    baseline();removeOnCall=true;CHECK(!ConsumePopulationDisposals(100,currentCensus));
    CHECK(nativeCalls==1 && tracks==1 && currentCensus.enemies.empty());
    CHECK(!logged("\"action\":\"disposed\""));TickPopulationRepair(101,currentCensus);
    CHECK(logged("\"action\":\"disposed\""));
    baseline();reenterOnCall=true;CHECK(!ConsumePopulationDisposals(100,currentCensus));
    CHECK(nativeCalls==1 && logged("reentrant-consumer"));
    baseline();resetOnRoleRead=true;TickPopulationRepair(101,currentCensus);
    CHECK(g_populationDisposals.empty() && nativeCalls==0 && logged("cancelled"));
    baseline();g_populationOutcomeSequence=POPULATION_OUTCOME_CAP;
    SealPopulationDisposals("shutdown",101);CHECK(g_populationOutcomeLoss>0 && logged("receipt-loss"));
    baseline();g_populationDisposals.clear();g_populationCut->entries[0].terminal=false;
    g_populationCut->entries[0].terminalSequence=0;g_host.enemies[1].dead=false;
    TickPopulationRepair(101,currentCensus);CHECK(!PopulationStaleAuthorized(100) && !g_populationLeasePoints.empty());
    float point[4]={0,0,0,1};CHECK(CopyPopulationActivation(point,400) && point[0]==10);
    g_populationLeaseTicks=600;point[0]=0;CHECK(!CopyPopulationActivation(point,400) && point[0]==0);
    baseline();ResetPopulationRepair();CHECK(!PopulationStaleAuthorized(100) && g_populationLeasePoints.empty());
    producerBaseline();
    const auto beforeBirth=g_populationSourceSequence;
    CapturePopulationBirths(currentCensus);
    CHECK(g_populationBirths.size()==1 && g_populationSourceSequence==beforeBirth+1);
    CHECK((g_populationBirths[0].roots==std::array<uintptr_t,5>{100,200,300,400,500}));
    CHECK(g_populationBirths[0].occurrence.activationPoint==g_inst.spawns[0].populationBirthPoint);
    PublishPopulationCut(currentCensus);CHECK(sent.size()==1);
    CapturePopulationBirths(currentCensus);CHECK(g_populationBirths.size()==1 && g_populationSourceSequence==beforeBirth+1);
    for(int mutation=0;mutation<22;++mutation) {
        producerBaseline();producerMutation=mutation;
        if(mutation==21) {
            nativeCatalog.entryCount=2;nativeCatalog.entries[1]=nativeCatalog.entries[0];
            ++nativeCatalog.entries[1].content.groupKey;
        }
        const auto floor=g_populationSourceSequence;
        CapturePopulationBirths(currentCensus);
        CHECK(g_populationBirths.empty() && g_populationSourceSequence==floor);
        PublishPopulationCut(currentCensus);CHECK(sent.empty());
    }
    for(int mutation=0;mutation<6;++mutation) {
        producerBaseline();
        switch(mutation) {
        case 0:++g_inst.spawns[0].controller;break;
        case 1:++g_inst.spawns[0].record;break;
        case 2:++memberController;break;
        case 3:++memberRecord;break;
        case 4:g_inst.spawns[0].announced=false;break;
        case 5:g_inst.spawns[0].controller=0;nativeController=memberController=0;break;
        }
        const auto floor=g_populationSourceSequence;
        CapturePopulationBirths(currentCensus);
        CHECK(g_populationBirths.empty() && g_populationSourceSequence==floor);
        PublishPopulationCut(currentCensus);CHECK(sent.empty());
    }
    // Capture before announcement is refused, but the retained birth point can qualify after announcement.
    producerBaseline();g_inst.spawns[0].announced=false;CapturePopulationBirths(currentCensus);
    CHECK(g_populationBirths.empty());g_inst.spawns[0].announced=true;CapturePopulationBirths(currentCensus);
    CHECK(g_populationBirths.size()==1);
    for(int mutation=0;mutation<6;++mutation) {
        producerBaseline();CapturePopulationBirths(currentCensus);CHECK(g_populationBirths.size()==1);
        switch(mutation) {
        case 0:++nativeController;break;
        case 1:++nativeRecord;break;
        case 2:++g_inst.spawns[0].record;break;
        case 3:g_inst.spawns[0].announced=false;break;
        case 4:++nativeIdentityFixture.status;break;
        case 5:++generation;break;
        }
        PublishPopulationCut(currentCensus);CHECK(sent.empty());
    }
    // A terminal certificate retains its historical roots without dereferencing a removed actor.
    producerBaseline();CapturePopulationBirths(currentCensus);g_inst.spawns[0].deathSent=true;
    g_inst.spawns[0].present=false;nativeController=nativeRecord=0;currentCensus.enemies.clear();
    PublishPopulationCut(currentCensus);CHECK(sent.size()==1 && g_populationBirths[0].occurrence.terminal);
    std::printf("PopulationRepairNativeTest: %s\n",failures?"FAIL":"PASS");return failures?1:0;
}
