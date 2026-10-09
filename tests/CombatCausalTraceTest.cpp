// Production observer and NativeHitTrace callbacks over copied facts only.
// No KH2, hook installation, named mapping, transport or native memory access.
#include "CombatCausalTrace.hpp"
#include <cstdio>
#include <new>
#include <cstdlib>
#include <vector>
#include "kh2coop/Codec.hpp"
namespace cc=kh2coop::inject::combatcausal;
namespace ht=kh2coop::inject::nativehittrace;
namespace { unsigned checks=0,failed=0; std::uint64_t clockNs=1;
void Check(bool ok,const char* name){++checks;if(!ok){++failed;std::printf("FAIL %s\n",name);}}
std::uint64_t Clock() noexcept{return ++clockNs;}
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
}
int main(){ht::RegisterOwnerThread();ht::Configure(true,ht::AllHooks,ht::AllHooks);
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
 s=Scope();Admit(local,s);for(unsigned i=0;i<cc::QueueCount+1;++i)local.Packet(s,cc::Kind::HpPublish,1,i+1,100,100,hp.data(),static_cast<unsigned>(hp.size()),true,false,12+i);
 Check(local.Retired()&&local.Loss()==1&&local.Dropped()==1,"fixed ring overflow records loss and permanently retires");
 // Actual production EndApply default-off guard and enabled path, no original callback modified.
 auto started=cc::engine.Started();cc::Configure(false,Clock);s=Scope();auto off=Emit(s);(void)off;
 Check(cc::engine.Started()==started&&!cc::Requested(),"absent/false flag leaves observer state untouched");
 Admit(cc::engine,s);cc::currentScope=s;cc::Configure(true,Clock);auto enabled=Emit(s);(void)enabled;
 Check(Has(cc::engine,cc::Kind::Hit,true),"production EndApply invokes default-off observer only when opted in");
 Admit(cc::engine,s);cc::currentScope=s;auto queued=Emit(s);(void)queued;
 Check(!cc::engine.Retired(),"logger-fault control begins with live admitted queued receipt");
 cc::Drain(ThrowLogger);Check(cc::engine.Retired(),"diagnostic logger exception contained and retires coverage");
 ht::Shutdown();std::printf("combat-causal checks=%u failed=%u nativeCoverageQualified=0 acceptance=0\n",checks,failed);return failed?1:0;}
