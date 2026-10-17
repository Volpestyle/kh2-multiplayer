#include "kh2coop/RouteSpawnPolicy.hpp"
#include "kh2coop/SpawnEmissionLedger.hpp"
#include "EnemyMirrorState.hpp"
#include "EnemyPopulation.hpp"
#include "OrdinaryEnemyBinding.hpp"
#include "MissionMirror.hpp"
#include "NativeSpawnController.hpp"
#include <iostream>
#include <limits>
#include <vector>
using namespace kh2coop;
using namespace kh2coop::routespawn;
namespace ob=kh2coop::inject::ordinarybinding;
namespace em=kh2coop::inject::enemymirror;
namespace ep=kh2coop::inject::enemypop;
namespace mr=kh2coop::inject::missionmirror::rules;
namespace sc=kh2coop::inject::spawncontroller;
int failures=0;
void check(bool ok,const char* why){if(!ok){++failures;std::cerr<<"FAIL "<<why<<'\n';}}
int main(){
    check(BaileyCreeperMission(BaileyLocation,"HB08_MS102.bar",true,1,0,false),"only active native Bailey mission");
    check(!BaileyCreeperMission(MoraleLocation,"HB08_MS102.bar",true,1,0,false),"other room refused");
    check(!BaileyCreeperMission(BaileyLocation,"HB08_MS101.bar",true,1,0,false),"other mission refused");
    check(!BaileyCreeperMission(BaileyLocation,"HB08_MS102.bar",false,1,0,false),"missing mission refused");
    check(!BaileyCreeperMission(BaileyLocation,"HB08_MS102.bar",true,0x15,0,false),"ended mission grants no living authority");
    check(BaileyCreeperMission(BaileyLocation,"HB08_MS102.bar",true,0x15,0,true),"same mission may drain authenticated terminal deaths");
    check(!BaileyCreeperMission(BaileyLocation,"HB08_MS102.bar",true,1,2,true),"failed mission grants no authority");
    for(unsigned record=0;record<10;++record)
        check(BaileyCreeperGroup(BaileyLocation,808738658,BaileyCreeperHeader,BaileyCreeperRecords,record)==(record<5),"only five authored Creepers in header58");
    // Bailey-08 measured all five native births after 3FE320 changed +0xE
    // from the installed 0 to 1. This is activation state, not group identity.
    auto activation=BaileyCreeperHeader;activation[14]=1;
    for(unsigned record=0;record<5;++record) {
        check(BaileyCreeperGroup(BaileyLocation,808738658,activation,BaileyCreeperRecords,record),"bailey-08 activated group admits each exact Creeper ordinal");
        check(std::strcmp(sc::DescribeBaileyCreeperGroup(BaileyLocation,808738658,activation,BaileyCreeperRecords,record).reason,"ok")==0,"activation diagnostic agrees with admission");
    }
    for(std::size_t at=0;at<BaileyCreeperHeader.size();++at)for(unsigned bit=0;bit<8;++bit){
        auto h=BaileyCreeperHeader;h[at]^=static_cast<std::uint8_t>(1u<<bit);
        if(at==14 && bit==0)continue; // sole observed/native-proven runtime transition
        check(!BaileyCreeperGroup(BaileyLocation,808738658,h,BaileyCreeperRecords,0),"all header mutations refuse scope");
        const auto d=sc::DescribeBaileyCreeperGroup(BaileyLocation,808738658,h,BaileyCreeperRecords,0);
        check(std::strcmp(d.reason,"group-header")==0 && d.offset==static_cast<int>(at) && d.observed==h[at] &&
              d.expected==BaileyCreeperHeader[at],"refused header reports its exact first byte without relaxing the pin");
    }
    for(unsigned value=2;value<=255;++value) {
        auto h=BaileyCreeperHeader;h[14]=static_cast<std::uint8_t>(value);
        check(!BaileyCreeperGroup(BaileyLocation,808738658,h,BaileyCreeperRecords,0),"unknown activation state fails closed");
    }
    check(!BaileyCreeperGroup(BaileyLocation,808738658,std::span(activation).first(43),BaileyCreeperRecords,0),"short activated header refused");
    std::vector<std::uint8_t> extraHeader(activation.begin(),activation.end());extraHeader.push_back(0);
    check(!BaileyCreeperGroup(BaileyLocation,808738658,extraHeader,BaileyCreeperRecords,0),"long activated header refused");
    for(std::size_t at=0;at<activation.size();++at)if(at!=14)for(unsigned bit=0;bit<8;++bit) {
        auto h=activation;h[at]^=static_cast<std::uint8_t>(1u<<bit);
        check(!BaileyCreeperGroup(BaileyLocation,808738658,h,BaileyCreeperRecords,0),"every other byte stays strict on the activated header");
    }
    for(std::size_t at=0;at<BaileyCreeperRecords.size();++at)for(unsigned bit=0;bit<8;++bit){
        auto r=BaileyCreeperRecords;r[at]^=static_cast<std::uint8_t>(1u<<bit);
        check(!BaileyCreeperGroup(BaileyLocation,808738658,BaileyCreeperHeader,r,0),"all full-group mutations refuse scope");
        const auto d=sc::DescribeBaileyCreeperGroup(BaileyLocation,808738658,BaileyCreeperHeader,r,0);
        check(std::strcmp(d.reason,"group-records")==0 && d.offset==static_cast<int>(at) && d.observed==r[at] &&
              d.expected==BaileyCreeperRecords[at],"refused record group reports its exact first byte");
    }
    check(!BaileyCreeperGroup(BaileyLocation,808738659,BaileyCreeperHeader,BaileyCreeperRecords,0),"wrong package refused");
    // Production receipt ledger plus binder: far native positions are not correspondence.
    SpawnEmissionLedger ledger;SpawnEmission e{};e.location.epoch=4;e.sequence=8;e.controllerKey=808738658;e.headerId=58;e.recordIndex=0;e.netId=8;
    check(ledger.Add(e),"host receipt admitted once");check(!ledger.Add(e),"duplicate receipt refused");
    check(ledger.Reserve(808738658,58,0)!=nullptr,"exact pending occurrence reserved");
    check(ledger.Cancel(8),"normal null permits same occurrence retry");
    check(ledger.Reserve(808738658,58,0)!=nullptr&&ledger.Commit(8),"nonnull birth spends receipt once");
    check(ledger.Reserve(808738658,58,0)==nullptr,"spent receipt cannot create replacement");
    // Captured bailey-07 seq8..12: sent, received and emitted, but no action
    // ticket. Replay the known receipt boundary; native proof bytes were not
    // logged, so this cannot identify the refusing mission/native sub-check.
    SpawnEmissionLedger replay;
    constexpr std::array<std::uint16_t,5> observedIds{8,10,9,12,11};
    for(std::uint16_t i=0;i<observedIds.size();++i) {
        auto received=e;received.sequence=8+i;received.recordIndex=i;received.netId=observedIds[i];
        check(replay.Add(received),"bailey-07 receipt replay admitted");
        const auto* pending=replay.Reserve(808738658,58,i);
        check(pending && pending->sequence==8U+i && pending->netId==observedIds[i],"exact observed receipt reserved");
        check(replay.Commit(8U+i) && !replay.Reserve(808738658,58,i),"emitted occurrence is spent even when no action ticket follows");
    }
    ob::Local local{};local.identity={1,2,3,4,5,317};local.point={1000,0,0};local.available=true;local.living=true;
    ob::Host host{8,317,{0,0,0},false,false,false};
    auto held=ob::Resolve(std::vector{local},std::vector{host},{1,4,10});check(held[0].id==0,"no point fallback for separated birth");
    local.birthId=8;auto bound=ob::Resolve(std::vector{local},std::vector{host},{1,4,10});check(bound[0].id==8,"checked ticket binds exact netId despite placement");
    auto colocated=host;colocated.id=9;colocated.point=local.point;
    auto absent=ob::Resolve(std::vector{local},std::vector{colocated},{1,4,10});
    check(absent[0].id==0,"a local carrying birthId never point-matches a different colocated host");
    auto exact=ob::Resolve(std::vector{local},std::vector{colocated,host},{1,4,10});
    check(exact[0].id==8,"birthId selects its distant host instead of a colocated different host");
    local.prior=bound[0].next;local.birthId=0;host.dead=true;
    auto lost=ob::Resolve(std::vector{local},std::vector{host},{1,4,11});check(lost[0].id==0,"lost receipt cannot obtain terminal authority through a point mismatch");
    local.birthId=8;
    auto terminal=ob::Resolve(std::vector{local},std::vector{host},{1,4,11});check(terminal[0].id==8,"retained established receipt receives host terminal death");
    // The global denylist and generic factory repair never gain family-wide permission.
    check(!em::FamilyAllowed(317)&&!em::FamilyAllowed(76),"global denylist stays unchanged");
    EnemyMotion m{};m.epoch=4;m.sequence=1;m.hostFrame=10;m.entries={{8,317},{9,76}};
    em::Stream ordinary;check(ordinary.Ingest(m,10)&&ordinary.tracks()==0,"default stream still excludes both denied families");
    em::Stream scoped;check(scoped.Ingest(m,10,true)&&scoped.tracks()==1,"Bailey stream exception only buffers317");
    check(!ep::BudgetAllows(120,118,9)&&ep::BudgetAllows(120,111,9),"observed repair weight respects exact native headroom");
    check(!ep::BudgetAllows(120,std::numeric_limits<float>::quiet_NaN(),9),"unknown admission never bypassed");
    ep::Planner planner;planner.Rebase(4);ep::HostView missing{};missing.netId=8;missing.objectId=317;missing.loadedObject=true;missing.streamFresh=true;missing.allowed=em::FamilyAllowed(317);missing.admissionAvailable=true;
    (void)planner.Plan(std::vector{missing},10,true);check(planner.Plan(std::vector{missing},10+ep::kMissingFrames,true)==0,"scoped births never grant generic factory repair");
    BaileyCreeperLease lease{10};check(lease.Observe(10,true)&&lease.Observe(11,true),"continuous same-frame/current native roots remain eligible");
    check(!lease.Observe(13,true)&&!lease.Observe(14,true),"observation gap permanently retires occurrence");
    BaileyCreeperLease changed{10};check(!changed.Observe(10,false)&&!changed.Observe(11,true),"changed roots/session/mission never recover by pointer reuse");
    mr::Mission h{},f{};h.active=f.active=true;
    constexpr char missionFile[]="HB08_MS102.bar";
    static_assert(sizeof(missionFile)<=sizeof(h.file));
    std::memcpy(h.file,missionFile,sizeof(missionFile));std::memcpy(f.file,missionFile,sizeof(missionFile));h.flags=f.flags=mr::kInUse;
    check(mr::Follows(h,f)&&!mr::Ended(h)&&!mr::Ended(f),"both Bailey peers start the same native mission");
    h.flags|=mr::kEnded;check(mr::HostEndedUnknown(h),"unknown engine end never fabricates a replay trap");
    // Normal HB08 event2 selects4/3. Only4/3 and4/22 are replayable;
    // 4/16 also grants local rewards and remains outside the mirror allowlist.
    for(const auto trap: {3,22}) {
        h.endTrap=static_cast<std::uint8_t>(trap);f.flags=mr::kInUse;
        check(!mr::HostEndedUnknown(h)&&mr::Ended(h)&&!mr::Ended(f)&&mr::Follows(h,f),"known host end still needs replay on friend");
        f.flags|=mr::kEnded;check(mr::Ended(h)&&mr::Ended(f)&&!mr::Follows(h,f),"both native ends observed; no second replay");
        check(mr::DropLocalCall(static_cast<std::size_t>(mr::EndTrapIndex(h.endTrap)),false,false,true),"replayed completion blocks duplicate native end");
    }
    std::cout<<"Bailey Creeper authority: "<<(failures?"FAIL":"PASS")<<" ("<<failures<<" failures)\n";return failures?1:0;
}
