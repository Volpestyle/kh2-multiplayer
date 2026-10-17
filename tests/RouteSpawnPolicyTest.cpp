#include "kh2coop/RouteSpawnPolicy.hpp"
#include "kh2coop/SpawnEmissionLedger.hpp"
#include "../inject/src/OrdinaryEnemyBinding.hpp"
#include "../inject/src/EnemyPopulation.hpp"
#include <iostream>
#include <limits>
#include <vector>

using namespace kh2coop;
using namespace kh2coop::routespawn;
namespace {
int failures=0;
void check(bool pass,const char* label) { if(!pass){++failures;std::cerr<<"FAIL: "<<label<<'\n';} }
template<class H,class R,class Predicate>
void mutations(const H& header,const R& records,Predicate accepts,bool activation=false) {
    check(accepts(header,records),"installed definition admitted");
    for(std::size_t i=0;i<header.size();++i)for(unsigned bit=0;bit<8;++bit) {
        auto changed=header;changed[i]^=static_cast<std::uint8_t>(1u<<bit);
        if(activation && i==0xE && bit==0)
            check(accepts(changed,records),"native activation 0/1 toggle remains admitted");
        else check(!accepts(changed,records),"every other changed header bit refuses admission");
    }
    for(std::size_t i=0;i<records.size();++i)for(unsigned bit=0;bit<8;++bit) {
        auto changed=records;changed[i]^=static_cast<std::uint8_t>(1u<<bit);
        check(!accepts(header,changed),"every changed record bit refuses admission");
    }
    check(!accepts(std::span(header).first(header.size()-1),records),"short header refused");
    std::vector<std::uint8_t> extraHeader(header.begin(),header.end());extraHeader.push_back(0);
    check(!accepts(extraHeader,records),"additional header bytes refused");
    check(!accepts(header,std::span(records).first(records.size()-1)),"short records refused");
    std::vector<std::uint8_t> extra(records.begin(),records.end());extra.push_back(0);
    check(!accepts(header,extra),"additional record bytes refused");
}
template<class H,class R>
void moraleDefinition(const H& header,const R& records,std::uint32_t key) {
    const auto accepts=[key](auto h,auto r){return MoraleGenerated(MoraleLocation,key,h,r);};
    for(unsigned state=0;state<=1;++state) {
        auto runtime=header;runtime[0xE]=static_cast<std::uint8_t>(state);
        // Probe03 measured exactly this byte at1 with no record differences.
        mutations(runtime,records,accepts,true);
        for(unsigned i=0;i<10;++i) {
            auto changed=MoraleLocation;changed[i]^=1;
            check(!MoraleGenerated(changed,key,runtime,records),"activation never relaxes exact location");
        }
        check(!MoraleGenerated(MoraleLocation,key==0x70345F62 ? 0x73345F62 : 0x70345F62,runtime,records),
              "activation never relaxes exact package");
        check(!MoraleGenerated(MoraleLocation,842162029,runtime,records),"activation never admits m_22");
    }
    for(unsigned state=2;state<256;++state) {
        auto invalid=header;invalid[0xE]=static_cast<std::uint8_t>(state);
        check(!accepts(invalid,records),"every activation value outside 0/1 is refused");
    }
}
void morale() {
    moraleDefinition(MoraleHeader978,MoraleRecords978,0x70345F62);
    moraleDefinition(MoraleHeader999,MoraleRecords999,0x70345F62);
    moraleDefinition(MoraleHeader1007,MoraleRecords1007,0x73345F62);
    for(unsigned i=0;i<10;++i) {
        auto changed=MoraleLocation;changed[i]^=1;
        check(!MoraleGenerated(changed,0x70345F62,MoraleHeader978,MoraleRecords978),"full location required");
    }
    check(!MoraleGenerated(MoraleLocation,0x73345F62,MoraleHeader978,MoraleRecords978),"package key cannot cross groups");
    check(!MoraleGenerated(MoraleLocation,842162029,MoraleHeader978,MoraleRecords978),"m_22 never admitted");
    check(!MoraleGenerated(MoraleLocation,0x70345F62,MoraleHeader978,MoraleRecords999),"same object ID cannot replace exact record");
    // Exercise the production reservation boundary with the new two-record group.
    SpawnEmissionLedger ledger;
    SpawnEmission first{{7,8,1,0,68,68,0},1,0x73345F62,1007,0,{1,2,3,1}};first.netId=6;
    auto second=first;second.sequence=2;second.recordIndex=1;second.netId=7;
    check(!ledger.Reserve(first.controllerKey,1007,0),"no autonomous client wave before host receipt");
    check(ledger.Add(first)&&ledger.Add(second),"two Nightwalker births announced");
    check(!ledger.Reserve(first.controllerKey,1007,1),"second record cannot overtake first");
    auto* reserved=ledger.Reserve(first.controllerKey,1007,0);
    check(reserved && reserved->netId==6,"first exact host occurrence reserved");
    check(ledger.Cancel(1)&&ledger.Reserve(first.controllerKey,1007,0),"normal native null return retries same receipt");
    check(ledger.Commit(1)&&!ledger.Reserve(first.controllerKey,1007,0),"nonnull or ambiguous return cannot retry spent occurrence");
    reserved=ledger.Reserve(first.controllerKey,1007,1);
    check(reserved && reserved->netId==7 && ledger.Commit(2),"second birth keeps independent identity");
    check(MoraleGeneratedMission(MoraleLocation,"MU01_MS102.bar",true,1,0,false),"exact morale mission admits lifetime scope");
    check(!MoraleGeneratedMission(BaileyLocation,"MU01_MS102.bar",true,1,0,false),"other room refuses morale lifetime");
    check(!MoraleGeneratedMission(MoraleLocation,"MU02_MS103A.bar",true,1,0,false),"other mission refuses morale lifetime");
    check(!MoraleGeneratedMission(MoraleLocation,"MU01_MS102.bar",false,1,0,false),"missing mission refuses morale lifetime");
    check(!MoraleGeneratedMission(MoraleLocation,"MU01_MS102.bar",true,0x15,0,false) &&
        MoraleGeneratedMission(MoraleLocation,"MU01_MS102.bar",true,0x15,0,true),"only adopted morale births can drain the ended mission");
    check(!MoraleGeneratedMission(MoraleLocation,"MU01_MS102.bar",true,1,2,true),"failed morale mission refuses authority");
    for(unsigned header: {978U,999U}) {
        check(MoraleGeneratedBirth(0x70345F62,header,0,1,302,4),"exact Shadow birth metadata");
        check(!MoraleGeneratedBirth(0x70345F62,header,1,1,302,4),"extra Shadow ordinal refused");
    }
    for(unsigned record=0;record<2;++record)
        check(MoraleGeneratedBirth(0x73345F62,1007,record,2,10,4),"exact two Nightwalker birth metadata");
    check(!MoraleGeneratedBirth(0x73345F62,1007,2,2,10,4) && !MoraleGeneratedBirth(0x73345F62,1007,0,1,10,4) &&
        !MoraleGeneratedBirth(0x70345F62,1007,0,2,10,4) && !MoraleGeneratedBirth(0x73345F62,1007,0,2,302,4) &&
        !MoraleGeneratedBirth(0x73345F62,1007,0,2,10,3),"ordinal/count/package/object/type negatives refuse lifetime");
    namespace ob=kh2coop::inject::ordinarybinding;
    ob::Local local{};local.identity={1,2,3,4,5,302};local.point={1000,0,0};local.available=local.living=true;local.birthId=6;
    ob::Host host{6,302,{0,0,0},false,false,false};MoraleGeneratedLease lease{100};
    auto bound=ob::Resolve(std::vector{local},std::vector{host},{1,2,100});
    check(bound[0].id==6,"LoD birth adopts exact occurrence despite distant native point");local.prior=bound[0].next;
    auto old=local;old.birthId=0;
    check(ob::Resolve(std::vector{old},std::vector{host},{1,2,101})[0].id==0,"probe04 reproduced: adopted one-use receipt falls back and loses binding");
    for(unsigned frame=101;frame<105;++frame) {
        check(lease.Observe(frame,true),"adopted LoD roots revalidated on each owner frame");
        auto next=ob::Resolve(std::vector{local},std::vector{host},{1,2,frame});
        check(next[0].id==6,"retained scoped birth ID keeps adopted actor bound across frames");local.prior=next[0].next;
    }
    host.dead=true;auto death=ob::Resolve(std::vector{local},std::vector{host},{1,2,105});
    check(lease.Observe(105,true) && death[0].id==6 && death[0].reason==ob::Reason::BoundDeath,
        "authenticated host death keeps continuous exact birth authority on friend");
    check(!lease.Observe(107,true) && !lease.Observe(108,true),"LoD frame gap permanently retires occurrence");
    MoraleGeneratedLease changed{100};check(!changed.Observe(101,false) && !changed.Observe(102,true),"changed LoD proof cannot recover by address reuse");
    namespace ep=kh2coop::inject::enemypop;
    check(!ep::BudgetAllows(40,39,15) && ep::BudgetAllows(40,24,15),
        "probe04 budget hypothesis control: orphan occupancy blocks weight15 until native headroom returns");
}
void bailey() {
    mutations(BaileyHeader,BaileyRecords,[](auto h,auto r){return Bailey(BaileyLocation,808738658,0x3FF727,h,r);});
    check(!Bailey(BaileyLocation,808738658,0x3FE83F,BaileyHeader,BaileyRecords),"update emitter cannot add a Sora");
    check(!Bailey(BaileyLocation,808738659,0x3FF727,BaileyHeader,BaileyRecords),"other package refused");
    for(unsigned i=0;i<10;++i) {
        auto changed=BaileyLocation;changed[i]^=1;
        check(!Bailey(changed,808738658,0x3FF727,BaileyHeader,BaileyRecords),"other room/program/door refused");
    }
    MissingPartyBudget budget;
    check(!budget.Claim(0)&&budget.count==0,"unknown load cannot spend record");
    for(unsigned load=1;load<=MissingPartyBudget::Capacity;++load) {
        check(budget.Claim(load),"new load spends one retained record");
        check(!budget.Claim(load)&&!budget.Claim(load-1),"duplicate, nested, stale and reconnect attempts stay spent");
        check(budget.count==load,"refusal cannot increment storage");
    }
    check(!budget.Claim(17)&&budget.count==16,"process lifetime pool never recycles native record pointers");
    check(MissingPartyLoadCurrent(4,4,5,4,false),"exact native finalizer completion admits construction stamp");
    check(!MissingPartyLoadCurrent(4,4,4,4,true),"actor cannot drive while construction load is pending");
    check(!MissingPartyLoadCurrent(4,4,4,4,false),"no completion observed");
    check(!MissingPartyLoadCurrent(4,4,6,4,false),"second completion cannot relabel old body");
    check(!MissingPartyLoadCurrent(4,4,5,5,false),"new transition retires selection before its load");
    check(!MissingPartyLoadCurrent(4,4,5,4,true),"pending teardown blocks old body");
    check(!MissingPartyLoadCurrent(UINT32_MAX,4,0,4,false),"serial wrap fails closed");
    check(!MissingPartyLoadCurrent(0,4,1,4,false),"unknown construction serial refused");
    check(SameConstructionStack(7,0x20000,7,0x10000,0x20000),"lost FLS on active native stack remains fenced");
    check(!SameConstructionStack(7,0x20000,8,0x10000,0x20000),"another thread cannot inherit the construction fence");
    check(!SameConstructionStack(7,0x20000,7,0x30000,0x40000),"parked fiber cannot fence another fiber on the same thread");
    check(SameConstructionStack(7,0x20000,7,0,0),"unavailable bounds fail closed on the known owner thread");
    check(!SameConstructionStack(0,0,7,0,0),"unknown context never claims a native stack");
    check(NewPrivateBirth(10,11,true,true),"later private birth retires old quarantine despite pointer reuse");
    check(!NewPrivateBirth(10,10,true,true),"same private birth cannot retire its own quarantine");
    check(!NewPrivateBirth(10,0,true,true)&&!NewPrivateBirth(0,11,true,true),"unknown serial grants no address-reuse escape");
    check(!NewPrivateBirth(10,11,false,true)&&!NewPrivateBirth(10,11,true,false),"unowned or faulted selection grants no escape");
    check(MissingPartyAbort(0x3A7B1B,1,0),"only observed normal Sora base allocation has fatal fallback");
    check(!MissingPartyAbort(0x3C0432,1,0)&&!MissingPartyAbort(0x3A7B1A,1,0),"fresh initializer and other callers remain Fault-only");
    check(!MissingPartyAbort(0x3A7B1B,2,0)&&!MissingPartyAbort(0x3A7B1B,1,1),"other status keys and forms remain Fault-only");
    MissingPartyBinding b{1,2,3,4,2,true,false,true,true,true};
    check(Bound(b),"distinct private clone and canonical ordinary local with both native attachments");
    auto c=b;c.clone=0;check(!Bound(c),"null clone refused");
    c=b;c.local=0;check(!Bound(c),"null authored local refused");
    c=b;c.clone=c.local;check(!Bound(c),"aliased actor refused");
    c=b;c.cloneStatus=0;check(!Bound(c),"missing status refused");
    c=b;c.localStatus=c.cloneStatus;check(!Bound(c),"shared status refused");
    c=b;c.privateOwned=false;check(!Bound(c),"shared lookup fallback cannot publish a puppet");
    c=b;c.localOwned=true;check(!Bound(c),"canonical private status refused");
    c=b;c.canonical=c.clone;check(!Bound(c),"added actor cannot remain canonical");
    c=b;c.cloneAttached=false;check(!Bound(c),"unattached clone refused");
    c=b;c.localAttached=false;check(!Bound(c),"changed original record/controller refused");
    c=b;c.current=false;check(!Bound(c),"unwind, stale load, disconnect or scope fault cannot publish");
}
}
int main() {
    morale();bailey();
    std::cout<<"Route spawn policy: "<<(failures?"FAIL":"PASS")<<" ("<<failures<<" failures)\n";
    return failures?1:0;
}
