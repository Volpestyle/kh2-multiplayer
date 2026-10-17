#include "kh2coop/RouteSpawnPolicy.hpp"
#include "kh2coop/SpawnEmissionLedger.hpp"
#include <iostream>
#include <limits>
#include <vector>

using namespace kh2coop;
using namespace kh2coop::routespawn;
namespace {
int failures=0;
void check(bool pass,const char* label) { if(!pass){++failures;std::cerr<<"FAIL: "<<label<<'\n';} }
template<class H,class R,class Predicate>
void mutations(const H& header,const R& records,Predicate accepts) {
    check(accepts(header,records),"installed definition admitted");
    for(std::size_t i=0;i<header.size();++i)for(unsigned bit=0;bit<8;++bit) {
        auto changed=header;changed[i]^=static_cast<std::uint8_t>(1u<<bit);
        check(!accepts(changed,records),"every changed header bit refuses admission");
    }
    for(std::size_t i=0;i<records.size();++i)for(unsigned bit=0;bit<8;++bit) {
        auto changed=records;changed[i]^=static_cast<std::uint8_t>(1u<<bit);
        check(!accepts(header,changed),"every changed record bit refuses admission");
    }
    check(!accepts(std::span(header).first(header.size()-1),records),"short header refused");
    check(!accepts(header,std::span(records).first(records.size()-1)),"short records refused");
    std::vector<std::uint8_t> extra(records.begin(),records.end());extra.push_back(0);
    check(!accepts(header,extra),"additional record bytes refused");
}
void morale() {
    mutations(MoraleHeader978,MoraleRecords978,[](auto h,auto r){return MoraleGenerated(MoraleLocation,0x70345F62,h,r);});
    mutations(MoraleHeader999,MoraleRecords999,[](auto h,auto r){return MoraleGenerated(MoraleLocation,0x70345F62,h,r);});
    mutations(MoraleHeader1007,MoraleRecords1007,[](auto h,auto r){return MoraleGenerated(MoraleLocation,0x73345F62,h,r);});
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
