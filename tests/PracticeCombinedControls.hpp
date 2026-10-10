// Included inside the existing native copied-facts test namespace.
void PracticePlayerControls(){
 auto retained=RetainedPracticePlayer(0);auto e=EmitCompanion(retained);
 auto s=Scope();s.native=e.before.context;
 for(unsigned i=0;i<3;++i)s.roster[i]=e.policy.roster[i];
 Check(!cc::PlayerEnemyHit(e,false),"practice05 direct Stat still outside original player predicate");
 Check(!cc::HostCompanionEnemyHit(e,s),"practice05 canonical Sora never companion");
 Check(cc::HostPlayerDirectStatEnemyHit(e,s),"practice05 actual Apply/direct Stat with copied HostLocalAttack policy PASS");
 Check(e.children[0].applySequence==e.sequence,"practice05 parent established by actual callbacks, not historical self-claim");
 for(unsigned m=0;m<33;++m){auto bad=e;auto scope=s;
  switch(m){
   case 0:bad.policy.recorded=false;break;
   case 1:bad.policy.decision.reason=kh2coop::inject::damagepolicy::Reason::HostCompanionAttack;break;
   case 2:bad.policy.decision.supported=false;break;
   case 3:scope.native.role=2;scope.native.slot=1;break;
   case 4:bad.before.hit.owner++;break;
   case 5:bad.policy.authority.source.actor++;break;
   case 6:bad.policy.roster[1]++;break;
   case 7:bad.policy.authority.context.frame++;break;
   case 8:bad.policy.facts.source=kh2coop::inject::damagepolicy::ActorClass::NativeCompanion;break;
   case 9:bad.policy.facts.role=kh2coop::inject::damagepolicy::Role::Client;break;
   case 10:bad.children[0].applySequence++;break;
   case 11:bad.children[0].callerRva++;break;
   case 12:bad.children[0].actor++;break;
   case 13:bad.children[0].before.status++;break;
   case 14:bad.children[0].delta=-4;break;
   case 15:bad.children[0].result++;break;
   case 16:bad.takeCalls=1;break;
   case 17:bad.childCount=2;break;
   case 18:bad.callerRva=0x3D613C;break;
   case 19:bad.before.source.type=1;bad.after.source.type=1;break;
   case 20:bad.before.hit.canonicalPlayer++;break;
   case 21:bad.after.source.status++;break;
   case 22:bad.policy.facts.hit.amount++;break;
   case 23:bad.policy.authority.hit.attack++;break;
   case 24:bad.lossStable=false;break;
   case 25:bad.unwound=true;break;
   case 26:bad.policy.zeroAttempted=true;break;
   case 27:bad.children[0].takeSequence=99;break;
   case 28:bad.policy.claimAttempted=true;break;
   case 29:bad.before.hit.syncDrop=true;break;
   case 30:bad.before.hit.head++;break;
   case 31:bad.after.victim.hp++;break;
   case 32:bad.policy.decision.action=kh2coop::inject::damagepolicy::Action::ZeroHp;break;
  }
  char name[128];std::snprintf(name,sizeof(name),"canonical direct Stat mutant %u policy/owner/client/parent/delta REFUSE",m);
  Check(!cc::HostPlayerDirectStatEnemyHit(bad,scope),name);
 }
 cc::Engine ledger;cc::Target targets[5];
 for(unsigned i=0;i<5;++i){targets[i]=Target(i+1);targets[i].hp=targets[i].maxHp=20;}
 targets[0].actor=e.before.victim.actor;targets[0].objentry=e.before.victim.objentry;targets[0].status=e.before.victim.status;
 auto initial=s;--initial.native.frame;
 ledger.Population(initial,targets,5,1);ledger.Population(s,targets,5,2);
 cc::Receipt r;while(ledger.Pop(r)){}
 ledger.Hit(s,e,3);
 Check(Has(ledger,cc::Kind::Hit,true)&&!ledger.Retired(),"practice05 host player direct Stat accounts once");
 auto hp=kh2coop::encode(kh2coop::EnemyHp{static_cast<std::uint32_t>(s.native.epoch),{{1,17,20}},1});
 ledger.Packet(s,cc::Kind::HpPublish,1,1,17,20,hp.data(),static_cast<unsigned>(hp.size()),true,false,4);
 Check(ledger.Pop(r)&&r.locallyQualified&&r.causeCount==1&&r.causes[0]==e.sequence,"practice05 normal HP publication retains exact source event");
 ledger.Hit(s,e,5);Check(ledger.Retired(),"duplicate direct Stat still hard retires");
}
void DisappearanceControls(){
 auto s=Scope();cc::Target rows[5];for(unsigned i=0;i<5;++i)rows[i]=Target(i+1);
 cc::DisappearanceTracker tracker;
 Check(tracker.Next(s,true,rows,4)==-1,"unarmed disappearance diagnostics inert");
 Check(tracker.Arm(s,rows,5),"diagnostic original five targets arm once");
 Check(!tracker.Arm(s,rows,5),"diagnostics cannot reanchor");
 Check(tracker.Next(s,true,rows,5)==-1,"complete original census no disappearance");
 Check(tracker.Next(s,false,rows+1,4)==-1,"incomplete census is not removal proof");
 auto foreign=s;++foreign.native.loadSerial;
 Check(tracker.Next(foreign,true,rows+1,4)==-1,"foreign lifecycle cannot consume one-shot");
 Check(tracker.Next(s,true,rows+1,4)==0&&tracker.Original(0).hp==100,"practice05 live original target missing selected once");
 Check(tracker.Next(s,true,rows+1,4)==-1,"repeat disappearance bounded no second row");
 unsigned count=1;while(tracker.Next(s,true,nullptr,0)>=0)++count;
 Check(count==5,"diagnostic total bounded by five originals");
 cc::Engine ledger;Admit(ledger,s);ledger.Population(s,rows+1,4,7);
 cc::Receipt refusal;bool mapping=false;while(ledger.Pop(refusal))if(refusal.kind==cc::Kind::Retired && refusal.reason==cc::Reason::Mapping)mapping=true;
 Check(ledger.Retired()&&mapping,"live target disappearance still emits exact Mapping retirement");
}
