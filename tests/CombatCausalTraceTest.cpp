// Production observer and NativeHitTrace callbacks over copied facts only.
// No KH2, hook installation, named mapping, transport or native memory access.
#include "CombatCausalTrace.hpp"
#include "CausalDisappearance.hpp"
#include <cstdio>
#include <Windows.h>
#include <type_traits>
#include <new>
#include <memory>
#include <cstdlib>
#include <vector>
#include "kh2coop/Codec.hpp"
namespace cc=kh2coop::inject::combatcausal;
namespace ht=kh2coop::inject::nativehittrace;
#include "CompanionRetainedFixture.hpp"
#include "PracticePlayerRetainedFixture.hpp"
#include "PracticeClientRetainedFixture.hpp"
static_assert(static_cast<unsigned>(cc::WireType::HitClaim)==static_cast<unsigned>(kh2coop::PacketType::HitClaim));
static_assert(static_cast<unsigned>(cc::WireType::EnemyHp)==static_cast<unsigned>(kh2coop::PacketType::EnemyHp));
static_assert(static_cast<unsigned>(cc::WireType::EnemyDeath)==static_cast<unsigned>(kh2coop::PacketType::EnemyDeath));
namespace { unsigned checks=0,failed=0; std::uint64_t clockNs=1;
void Check(bool ok,const char* name){++checks;if(!ok){++failed;std::printf("FAIL %s\n",name);}}
std::uint64_t Clock() noexcept{return ++clockNs;}
std::uint64_t PublishedClock() noexcept{return 0xABC123;}
DWORD WINAPI PublicationReader(void* value){
    const auto end=GetTickCount64()+2000;
    while(!cc::Requested()&&GetTickCount64()<end)SwitchToThread();
    *static_cast<bool*>(value)=cc::Requested()&&cc::Now()==0xABC123;return 0;
}
static_assert(std::is_same_v<decltype(cc::clockFn),std::atomic<cc::Clock>>, "clock pointer publication must remain atomic");
void ThrowLogger(const char*,...){throw 7;}
cc::Engine local;
cc::Scope Scope(bool client=false){cc::Scope s{};auto& n=s.native;n.available=true;n.readMask=ht::ContextComplete;
 n.frame=1;n.generation=1;n.epoch=9;n.loadSerial=3;n.transitionSerial=2;n.role=client?2:1;n.slot=client?1:0;
 n.connectionId=client?101:100;n.hostConnectionId=100;const std::uint16_t loc[]={5,6,0,1,1,0};std::memcpy(n.location,loc,sizeof(loc));
 s.delivery=4;s.sessionSalt=55;s.roster[0]=100;s.roster[1]=101;s.peerDelivery[0]=1;s.peerDelivery[1]=1;return s;}
cc::Target Target(unsigned id=1){return {0x1000+id,0x2000+id,0x3000+id,0x4000+id,0x5000+id,id,302,4,100,100};}
void Admit(cc::Engine& e,cc::Scope& s){e.~Engine();new(&e)cc::Engine;cc::Target targets[5];for(unsigned i=0;i<5;++i)targets[i]=Target(i+1);
 e.Population(s,targets,5,1);Check(!e.Admitted(),"first census not admission");++s.native.frame;e.Population(s,targets,5,2);Check(e.Admitted(),"two exact consecutive censuses admit");cc::Receipt r;while(e.Pop(r)){};}
ht::ApplyFacts Facts(const cc::Scope& s){ht::ApplyFacts a{};a.context=s.native;auto t=Target();
 a.victim={t.actor,t.objentry,t.status,302,ht::ActorComplete,2,100,100,0x5F4D,4};
 a.source={0xA000,0xB000,0xC000,84,ht::ActorComplete,1,100,100,0x5F50,0};
 a.hit.hit=0xD000;a.hit.attack=0xE000;a.hit.owner=a.source.actor;a.hit.canonicalPlayer=a.source.actor;
 a.hit.head=a.source.actor;a.hit.tracked=a.source.actor;a.hit.flags=0;a.hit.attackHandle=1;a.hit.atkpHandle=2;
 a.hit.ownerHandle=3;a.hit.attackId=65;a.hit.readMask=ht::HitComplete;a.hit.damage=7;a.hit.kind=1;return a;}
ht::Event Emit(const cc::Scope& s,bool client=false){auto before=Facts(s),after=before;
 before.hit.syncDrop=client;after.hit.syncDrop=client;after.hit.flags|=2;after.victim.hp=client?100:93;if(client)after.hit.damage=0;
 ht::ApplyToken apply{};ht::BeginApply(apply,0x3D613C,true,before);
 ht::ChildToken take{},stat{};
 if(!client){ht::BeginTake(take,before.victim.actor,-7,0,1,0x3D3CD5,true,before.victim);
 ht::BeginStat(stat,before.victim.actor,-7,0,257,0,false,before.victim);ht::EndStat(stat,true,93,&after.victim);ht::EndTake(take,true,&after.victim);}
 else {apply.event.policy.recorded=true;apply.event.policy.claimAttempted=true;apply.event.policy.claimQueued=true;}
 ht::EndApply(apply,true,0xABC,&after);ht::Event e{};Check(ht::PopEvent(e),"actual NativeHitTrace event emitted");return e;}
bool Has(cc::Engine& e,cc::Kind k,bool qualified){cc::Receipt r;bool found=false;while(e.Pop(r))if(r.kind==k&&r.locallyQualified==qualified)found=true;return found;}
ht::Event EmitCompanion(const ht::Event& retained){
 ht::ApplyToken apply{};ht::BeginApply(apply,retained.callerRva,true,retained.before);
 apply.event.policy=retained.policy;
 const auto& child=retained.children[0];ht::ChildToken stat{};
 ht::BeginStat(stat,child.actor,child.delta,child.stat,child.react,child.callerRva,true,child.before);
 ht::EndStat(stat,true,child.result,&child.after);
 ht::EndApply(apply,true,retained.rawResult,&retained.after);
 ht::Event emitted{};Check(ht::PopEvent(emitted),"retained companion actual Apply/direct-Stat callbacks emit");return emitted;
}
void CompanionControls(){
 for(unsigned sample=0;sample<2;++sample){
  auto retained=RetainedCompanion(sample);auto e=EmitCompanion(retained);
  auto s=Scope();s.native=e.before.context;for(unsigned i=0;i<3;++i)s.roster[i]=e.policy.roster[i];
  Check(!cc::PlayerEnemyHit(e,false),"retained companion still refuses unchanged player predicate");
  Check(cc::HostCompanionEnemyHit(e,s),sample==0?"causal08 serial101 companion witness PASS":"causal10 serial151 companion witness PASS");
  Check(!e.children[0].matching && e.children[0].applySequence==e.sequence && !e.children[0].takeSequence,
        "direct Apply relation explicit; original Take/Stat matching unchanged");
  for(unsigned m=0;m<28;++m){auto bad=e;auto scope=s;
   switch(m){
    case 0:bad.policy.recorded=false;break;
    case 1:bad.policy.decision.reason=kh2coop::inject::damagepolicy::Reason::UnknownSource;break;
    case 2:bad.policy.decision.supported=false;break;
    case 3:scope.native.role=2;scope.native.slot=1;break;
    case 4:bad.before.hit.owner++;break;
    case 5:bad.policy.authority.source.actor++;break;
    case 6:bad.policy.roster[1]++;break;
    case 7:bad.policy.authority.context.frame++;break;
    case 8:bad.policy.facts.source=kh2coop::inject::damagepolicy::ActorClass::RemoteRepresentation;break;
    case 9:bad.policy.facts.role=kh2coop::inject::damagepolicy::Role::Client;break;
    case 10:bad.children[0].applySequence++;break;
    case 11:bad.children[0].callerRva++;break;
    case 12:bad.children[0].actor++;break;
    case 13:bad.children[0].before.status++;break;
    case 14:bad.children[0].delta=2;break;
    case 15:bad.children[0].result++;break;
    case 16:bad.takeCalls=1;break;
    case 17:bad.childCount=2;break;
    case 18:bad.callerRva=0x3D613C;break;
    case 19:bad.before.source.type=0;bad.after.source.type=0;break;
    case 20:bad.before.hit.canonicalPlayer=bad.before.source.actor;break;
    case 21:bad.after.source.status++;break;
    case 22:bad.policy.facts.hit.amount++;break;
    case 23:bad.policy.authority.hit.attack++;break;
    case 24:bad.lossStable=false;break;
    case 25:bad.unwound=true;break;
    case 26:bad.policy.zeroAttempted=true;break;
    case 27:bad.children[0].takeSequence=99;break;
   }
   Check(!cc::HostCompanionEnemyHit(bad,scope),"companion policy/owner/client/masquerade/child mutant REFUSE");
  }
  cc::Engine ledger;cc::Target targets[5];for(unsigned i=0;i<5;++i){targets[i]=Target(i+1);targets[i].hp=targets[i].maxHp=20;}
  targets[0].actor=e.before.victim.actor;targets[0].objentry=e.before.victim.objentry;targets[0].status=e.before.victim.status;
  auto initial=s;--initial.native.frame;ledger.Population(initial,targets,5,1);ledger.Population(s,targets,5,2);
  Check(ledger.Admitted(),"companion fixture consecutive original population admitted");cc::Receipt ignored;while(ledger.Pop(ignored)){}
  ledger.Hit(s,e,3);Check(Has(ledger,cc::Kind::Hit,true)&&!ledger.Retired(),"retained companion accounts once in native causal ledger");
  auto hp=kh2coop::encode(kh2coop::EnemyHp{static_cast<std::uint32_t>(s.native.epoch),{{1,18,20}},1});
  ledger.Packet(s,cc::Kind::HpPublish,1,1,18,20,hp.data(),static_cast<unsigned>(hp.size()),true,false,4);
  Check(ledger.Pop(ignored)&&ignored.locallyQualified&&ignored.causeCount==1&&ignored.causes[0]==e.sequence,
        "companion source associates unchanged native publication");
  ledger.Hit(s,e,5);Check(ledger.Retired(),"duplicate companion event still retires unknown delta");
 }
 auto player=Emit(Scope());auto fake=player;fake.policy=RetainedCompanion(0).policy;
 Check(!cc::HostCompanionEnemyHit(fake,Scope()),"real player path cannot masquerade as companion");
}
#include "PracticeCombinedControls.hpp"
#include "PracticeClientControls.hpp"
}
kh2coop::PopulationCut LifecycleFixture() {
    kh2coop::PopulationCut c;c.membershipOnly=true;c.epoch=9;c.hostLoad=3;c.hostTransition=2;c.sequence=5;c.location={5,6,0,1,1,0};
    kh2coop::NativeRecordContentDefinition d;d.location=c.location;d.groupKey=1;d.layoutSha256.fill(1);d.header[4]=5;d.records.resize(5);
    for(auto& r:d.records){r[0]=0x2e;r[1]=1;}c.definitions.push_back(d);
    for(unsigned i=0;i<5;++i)c.entries.push_back({static_cast<std::uint16_t>(i+1),0,static_cast<std::uint16_t>(i),302,i+1,i+1,0,false,{float(i),0,0,1}});
    return c;
}
void LifecycleControls() {
    auto initial=LifecycleFixture();const auto first=kh2coop::encode(initial);
    auto removed=initial;removed.sequence=6;removed.entries[0].terminal=true;removed.entries[0].removed=true;removed.entries[0].terminalSequence=6;
    const auto retire=kh2coop::encode(removed);
    auto born=removed;born.sequence=7;auto row=initial.entries[0];row.netId=6;row.occurrence=row.birthSequence=7;born.entries.push_back(row);
    const auto birth=kh2coop::encode(born);
    for(unsigned mode=0;mode<10;++mode) {
        cc::Scope s=Scope();Admit(local,s);
        for(unsigned id=1;id<=5;++id)Check(local.BindBirth(s,Target(id),id,first.data(),static_cast<unsigned>(first.size()),3),"original source certificate binds");
        auto t=Target();auto badScope=s;auto proof=retire;auto event=std::uint64_t(6);auto oldBirth=std::uint64_t(1);
        switch(mode) {
        case 1:proof.clear();break;
        case 2:proof=first;break; // no removal proof
        case 3:event=1;break;
        case 4:oldBirth=2;break;
        case 5:++t.actor;break;
        case 6:--t.hp;break;
        case 7:++badScope.native.epoch;break;
        case 8:proof.back()^=1;break;
        case 9:proof=birth;break; // cut may contain ordered new birth, but retirement must still precede local admission
        }
        const bool accepted=local.RetireMember(badScope,1,oldBirth,event,t,proof.data(),static_cast<unsigned>(proof.size()),4);
        Check(accepted==(mode==0 || mode==9),"membership missing/reused/reordered/foreign proof refuses");
        if(!accepted)continue;
        Check(!local.Member(1)&&local.Removed(t),"old membership retired with positive unchanged HP");
        auto fresh=Target(6);fresh.controller=t.controller;fresh.record=t.record;
        Check(local.BindBirth(s,fresh,7,birth.data(),static_cast<unsigned>(birth.size()),5),"ordered independent new native body admitted");
        cc::Receipt receipt;bool oldRow=false,newRow=false,death=false;
        while(local.Pop(receipt)) {
            if(receipt.kind==cc::Kind::MembershipRetire)oldRow=receipt.locallyQualified&&receipt.beforeHp==100&&receipt.afterHp==100&&!receipt.attempted;
            if(receipt.kind==cc::Kind::MembershipBirth && receipt.target.netId==6)newRow=receipt.locallyQualified;
            death=death||receipt.kind==cc::Kind::DeathPublish||receipt.kind==cc::Kind::DeathApply;
        }
        Check(oldRow&&newRow&&!death,"membership lifecycle never grants kill credit/native death");
        auto payload=kh2coop::encode(kh2coop::EnemyHp{9,{{1,100,100}},2});
        local.Packet(s,cc::Kind::HpReceive,1,2,100,100,payload.data(),static_cast<unsigned>(payload.size()),true,false,6);
        bool unknown=false;while(local.Pop(receipt))if(receipt.kind==cc::Kind::HpReceive)unknown=receipt.reason==cc::Reason::RejectUnadmittedLifecycle&&receipt.payloadBytes==payload.size()&&!receipt.locallyQualified;
        Check(unknown,"retired-ID HP retains exact raw explicit rejection");
        cc::Key key{101,1,9,1,302,1,1};
        local.HostApply(s,key,t,100,99,true,true,true,7);Check(local.Retired(),"old claim cannot damage retired ID");
    }
    cc::Scope s=Scope();Admit(local,s);for(unsigned id=1;id<=5;++id)local.BindBirth(s,Target(id),id,first.data(),static_cast<unsigned>(first.size()),3);
    auto fresh=Target(6);fresh.controller=Target().controller;fresh.record=Target().record;
    Check(!local.BindBirth(s,fresh,7,birth.data(),static_cast<unsigned>(birth.size()),4),"new birth cannot precede actual retirement receipt");
    s=Scope();Admit(local,s);for(unsigned id=1;id<=5;++id)local.BindBirth(s,Target(id),id,first.data(),static_cast<unsigned>(first.size()),3);
    auto t=Target();local.RetireMember(s,1,1,6,t,retire.data(),static_cast<unsigned>(retire.size()),4);
    Check(!local.RetireMember(s,1,1,6,t,retire.data(),static_cast<unsigned>(retire.size()),5),"duplicate retirement hard refuses");
    s=Scope();Admit(local,s);for(unsigned id=1;id<=5;++id)local.BindBirth(s,Target(id),id,first.data(),static_cast<unsigned>(first.size()),3);
    local.RetireMember(s,1,1,6,t,retire.data(),static_cast<unsigned>(retire.size()),4);fresh.actor=t.actor;
    Check(!local.BindBirth(s,fresh,7,birth.data(),static_cast<unsigned>(birth.size()),5),"reused actor cannot acquire fresh ID");
    // Actual ca007 B frame3352 copied roots/HP/scope. Full source certificate
    // was NOT retained: positive proof below is explicitly constructed.
    for(unsigned withProof=0;withProof<2;++withProof) {
        local.~Engine();new(&local)cc::Engine;
        s=Scope();s.native.generation=2;s.native.epoch=2;s.native.frame=3350;
        s.native.connectionId=s.native.hostConnectionId=1;s.delivery=1;
        s.sessionSalt=4289048146971526317ULL;s.roster[0]=1;s.roster[1]=2;
        cc::Target actual[5];for(unsigned i=0;i<5;++i){actual[i]=Target(i+1);actual[i].hp=actual[i].maxHp=20;}
        actual[3]={0x7FF6F67E7F10ULL,0x7FF6F6C79F18ULL,0x7FF6F6C72940ULL,
                   0x7FF6F6D38340ULL,0x7FF6F6C36454ULL,4,302,4,20,20};
        local.Population(s,actual,5,1);++s.native.frame;local.Population(s,actual,5,2);
        auto copied=LifecycleFixture();copied.epoch=2;auto certificate=kh2coop::encode(copied);
        for(unsigned i=0;i<5;++i)local.BindBirth(s,actual[i],i+1,certificate.data(),static_cast<unsigned>(certificate.size()),3);
        copied.sequence=6;copied.entries[3].terminal=copied.entries[3].removed=true;copied.entries[3].terminalSequence=6;
        certificate=kh2coop::encode(copied);++s.native.frame;
        Check(local.RetireMember(s,4,4,6,actual[3],withProof?certificate.data():nullptr,
            withProof?static_cast<unsigned>(certificate.size()):0,4)==bool(withProof),
            "retained frame3352 refuses missing proof; constructed full source proof admits membership only");
        if(withProof)Check(local.Removed(actual[3])&&!local.Member(4),"retained live HP20 never converted to native death");
    }
}
int main(){
 bool published=false;HANDLE reader=CreateThread(nullptr,0,PublicationReader,&published,0,nullptr);
 Check(reader!=nullptr,"independent clock publication reader created");cc::Configure(true,PublishedClock);
 if(reader){Check(WaitForSingleObject(reader,3000)==WAIT_OBJECT_0,"clock reader bounded exit");CloseHandle(reader);}
 Check(published,"release/acquire enabled publication exposes original nonnull clock to reader");
 cc::enabled.store(false);cc::clockFn.store(nullptr);
 ht::RegisterOwnerThread();ht::Configure(true,ht::AllHooks,ht::AllHooks);
 auto s=Scope();Admit(local,s);auto hit=Emit(s);Check(cc::PlayerEnemyHit(hit,false),"actual Take/Stat event qualifies player-to-Shadow");
 Check(!hit.witness,"old enemy-to-player Witness remains false");
 for(unsigned mutant=0;mutant<15;++mutant){auto e=hit;switch(mutant){case 0:e.callerRva=0;break;case 1:e.before.source.actor=0;break;
 case 2:e.after.hit.canonicalPlayer=0;break;case 3:e.before.source.namePrefix=0x5F46;break;case 4:e.coverageStable=false;break;
 case 5:e.lossStable=false;break;case 6:e.children[1].delta=-8;break;case 7:e.children[0].callerRva=0;break;
 case 8:e.before.victim.objectId=317;break;case 9:e.after.victim.hp=100;break;case 10:e.nested=true;break;
 case 11:e.contextStable=false;break;case 12:e.before.hit.manualFilterOn=true;break;case 13:e.after.source.status=0;break;
 case 14:e.after.hit.kind=6;break;}Check(!cc::PlayerEnemyHit(e,false),"hit identity/native/coverage mutant refuses");}
 local.Hit(s,hit,3);Check(Has(local,cc::Kind::Hit,true),"real event enters ordered cause ledger");
 auto hp=kh2coop::encode(kh2coop::EnemyHp{9,{{1,93,100}},1});local.Packet(s,cc::Kind::HpPublish,1,1,93,100,hp.data(),static_cast<unsigned>(hp.size()),true,false,4);
 cc::Receipt receipt{};Check(local.Pop(receipt)&&receipt.locallyQualified&&receipt.causeCount==1&&receipt.causes[0]==hit.sequence&&
 receipt.payloadBytes==hp.size()&&std::memcmp(receipt.payload,hp.data(),hp.size())==0,"normal HP raw bytes and exact cause association");
 auto bad=s;++bad.native.loadSerial;local.Packet(bad,cc::Kind::HpPublish,1,2,93,100,hp.data(),static_cast<unsigned>(hp.size()),true,false,5);
 Check(local.Retired(),"same-room native load replacement retires");
 for(unsigned m=0;m<9;++m){s=Scope();Admit(local,s);cc::Target targets[5];for(unsigned i=0;i<5;++i)targets[i]=Target(i+1);
 switch(m){case 0:targets[0].hp=99;break;case 1:targets[0].controller++;break;case 2:targets[0].record++;break;case 3:targets[0].actor++;break;
 case 4:targets[1]=targets[0];break;case 5:s.delivery++;break;case 6:s.roster[1]++;break;case 7:s.native.available=false;break;case 8:targets[0].objectId=309;break;}
 local.Population(s,targets,5,6);Check(local.Retired(),"unexplained HP/roots/scope/family/duplicate permanently retires");}
 s=Scope();Admit(local,s);cc::Key key{101,1,9,1,302,65,7};auto t=Target();local.HostApply(s,key,t,100,93,true,true,true,7);
 Check(Has(local,cc::Kind::HostApply,true),"actual host apply facts associate connection/seq/target/delta");
 local.HostApply(s,key,t,93,86,true,true,true,8);Check(local.Retired(),"duplicate host claim refuses even with native delta");
 for(unsigned m=0;m<7;++m){s=Scope();Admit(local,s);auto raw=kh2coop::encode(kh2coop::EnemyHp{9,{{1,100,100}},1});
 switch(m){case 0:raw[0]=24;break;case 1:raw[1]++;break;case 2:raw[3]++;break;case 3:raw[7]++;break;
 case 4:raw[17]++;break;case 5:raw[19]--;break;case 6:raw[23]--;break;}
 local.Packet(s,cc::Kind::HpPublish,1,1,100,100,raw.data(),static_cast<unsigned>(raw.size()),true,false,8);
 Check(Has(local,cc::Kind::HpPublish,false),"raw opcode/framing/epoch/sequence/target/HP/maxHP mutant refuses");}
 s=Scope();Admit(local,s);local.Packet(s,cc::Kind::DeathPublish,1,0,0,100,hp.data(),static_cast<unsigned>(hp.size()),true,true,8);
 Check(local.Retired(),"grace/despawn never natural kill");
 s=Scope();Admit(local,s);local.Packet(s,cc::Kind::HpPublish,1,1,99,100,hp.data(),static_cast<unsigned>(hp.size()),true,false,8);
 Check(Has(local,cc::Kind::HpPublish,false),"unassociated normal HP publication refuses");
 s=Scope(true);Admit(local,s);auto friendHit=Emit(s,true);Check(cc::PlayerEnemyHit(friendHit,true),"friend actual event with native HP suppressed qualifies claim observation");
 local.Packet(s,cc::Kind::HpReceive,1,1,93,100,hp.data(),static_cast<unsigned>(hp.size()),true,false,9);
 Check(local.NeedsHpReadback(1,1),"new received native HP requires readback");
 local.Applied(s,cc::Kind::HpApply,t,1,100,93,93,true,true,true,10);Check(Has(local,cc::Kind::HpApply,true),"client actual checked native store associates source sequence");
 Check(!local.NeedsHpReadback(1,1),"source applied once");
 local.Applied(s,cc::Kind::HpApply,t,1,93,93,92,true,true,true,11);Check(local.Retired(),"failed poststore readback refuses");
 s=Scope();Admit(local,s);for(unsigned i=0;i<cc::QueueCount+1;++i){
 auto frame=kh2coop::encode(kh2coop::EnemyHp{9,{{1,100,100}},i+1});
 local.Packet(s,cc::Kind::HpPublish,1,i+1,100,100,frame.data(),static_cast<unsigned>(frame.size()),true,false,12+i);}
 Check(local.Retired()&&local.Loss()==1&&local.Dropped()==1,"fixed ring overflow records loss and permanently retires");
 // Actual production EndApply default-off guard and enabled path, no original callback modified.
 auto started=cc::engine.Started();cc::Configure(false,Clock);s=Scope();auto off=Emit(s);(void)off;
 Check(cc::engine.Started()==started&&!cc::Requested(),"absent/false flag leaves observer state untouched");
 Admit(cc::engine,s);cc::currentScope=s;cc::Configure(true,Clock);auto enabled=Emit(s);(void)enabled;
 Check(Has(cc::engine,cc::Kind::Hit,true),"production EndApply invokes default-off observer only when opted in");
 Admit(cc::engine,s);cc::currentScope=s;auto queued=Emit(s);(void)queued;
 Check(!cc::engine.Retired(),"logger-fault control begins with live admitted queued receipt");
 cc::Drain(ThrowLogger);Check(cc::engine.Retired(),"diagnostic logger exception contained and retires coverage");
 // Consumer observation failures cannot become complete rejection evidence.
 Admit(cc::engine,s);std::vector<std::uint8_t> oversized(cc::PayloadMax+1,1);
 cc::engine.Consumer(s,23,cc::Reason::RejectDecode,0,0,9,0,oversized.data(),static_cast<unsigned>(oversized.size()),20);
 bool prefix=false;cc::Receipt rejected;
 while(cc::engine.Pop(rejected))if(rejected.kind==cc::Kind::ConsumerRejected)prefix=rejected.payloadTruncated&&
     rejected.payloadOriginalBytes==cc::PayloadMax+1&&rejected.payloadBytes==cc::PayloadMax&&!rejected.locallyQualified;
 Check(prefix&&cc::engine.Retired(),"oversized consumer raw receipt retains explicit prefix and permanently refuses completeness");
 Admit(cc::engine,s);cc::currentScope=s;
 cc::engine.Consumer(s,23,cc::Reason::RejectDecode,0,0,9,0,hp.data(),static_cast<unsigned>(hp.size()),21);
 cc::Drain(ThrowLogger);Check(cc::engine.Retired(),"actual queued consumer-rejection logger failure is contained");
 cc::Configure(false,Clock);LifecycleControls();CompanionControls();PracticePlayerControls();PracticeClientControls();DisappearanceControls();
 ht::Shutdown();std::printf("combat-causal checks=%u failed=%u nativeCoverageQualified=0 acceptance=0\n",checks,failed);return failed?1:0;}
