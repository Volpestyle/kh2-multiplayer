// Included inside the existing test namespace; no game or transport calls.
void PracticeClientControls(){
 namespace dp=kh2coop::inject::damagepolicy;
 for(unsigned sample=0;sample<2;++sample){
  auto retained=RetainedPracticeClient(sample);auto e=EmitCompanion(retained);
  auto s=Scope(true);s.native=e.before.context;
  for(unsigned i=0;i<3;++i)s.roster[i]=e.policy.roster[i];
  auto key=RetainedPracticeClientKey(sample);
  Check(!cc::PlayerEnemyHit(e,true),"retained client direct Stat remains outside original player predicate");
  Check(!cc::HostPlayerDirectStatEnemyHit(e,s)&&!cc::HostCompanionEnemyHit(e,s),"client never acquires host/companion authority");
  Check(cc::ClientPlayerDirectStatEnemyHit(e,s),"practice05 real ClientClaim decision/direct Stat suppression PASS");
  Check(e.children[0].applySequence==e.sequence&&e.before.victim.hp==e.after.victim.hp&&e.after.hit.damage==0,
        "actual callback binds direct parent; client amount zero and local HP unchanged");
  for(unsigned m=0;m<36;++m){auto bad=e;auto scope=s;
   switch(m){
    case 0:bad.policy.recorded=false;break;
    case 1:bad.policy.decision.reason=dp::Reason::HostLocalAttack;break;
    case 2:bad.policy.decision.action=dp::Action::Native;break;
    case 3:bad.policy.decision.supported=false;break;
    case 4:bad.policy.facts.role=dp::Role::Host;break;
    case 5:scope.native.role=1;scope.native.slot=0;break;
    case 6:bad.policy.claimAttempted=false;break;
    case 7:bad.policy.claimQueued=false;break;
    case 8:bad.policy.zeroAttempted=false;break;
    case 9:bad.policy.zeroResult=dp::ZeroResult::Fault;break;
    case 10:bad.policy.revalidationPassed=false;break;
    case 11:bad.policy.revalidationAttempted=false;break;
    case 12:bad.before.hit.owner++;break;
    case 13:bad.policy.authority.source.actor++;break;
    case 14:bad.policy.roster[1]++;break;
    case 15:bad.policy.authority.context.frame++;break;
    case 16:bad.policy.facts.source=dp::ActorClass::RemoteRepresentation;break;
    case 17:bad.children[0].applySequence++;break;
    case 18:bad.children[0].callerRva++;break;
    case 19:bad.children[0].actor++;break;
    case 20:bad.children[0].delta=-3;break;
    case 21:bad.children[0].result--;break;
    case 22:bad.after.victim.hp--;break;
    case 23:bad.after.hit.damage=3;break;
    case 24:bad.before.hit.syncDrop=false;break;
    case 25:bad.after.hit.syncDrop=false;break;
    case 26:bad.policy.authority.hit.syncDrop=true;break;
    case 27:bad.takeCalls=1;break;
    case 28:bad.childCount=2;break;
    case 29:bad.before.source.type=1;bad.after.source.type=1;break;
    case 30:bad.before.hit.canonicalPlayer++;break;
    case 31:bad.policy.facts.hit.amount++;break;
    case 32:bad.lossStable=false;break;
    case 33:bad.unwound=true;break;
    case 34:bad.children[0].takeSequence=99;break;
    case 35:bad.callerRva=0x3D613C;break;
   }
   Check(!cc::ClientPlayerDirectStatEnemyHit(bad,scope),"client policy/authority/zero/parent mutant REFUSE");
  }
  // Queue association controls use the real codec and real Engine, not bool claims.
  for(unsigned m=0;m<17;++m){auto holder=std::make_unique<cc::Engine>();auto& ledger=*holder;cc::Target targets[5];
   for(unsigned i=0;i<5;++i){targets[i]=Target(i+1);targets[i].hp=targets[i].maxHp=20;}
   auto& t=targets[key.netId-1];t.actor=e.before.victim.actor;t.objentry=e.before.victim.objentry;
   t.status=e.before.victim.status;t.hp=e.before.victim.hp;
   auto initial=s;--initial.native.frame;ledger.Population(initial,targets,5,1);ledger.Population(s,targets,5,2);
   cc::Receipt r;while(ledger.Pop(r)){}
   auto k=key;auto qs=s;auto event=e.sequence;bool queued=true;
   switch(m){case 2:event++;break;case 3:k.connection++;break;case 4:k.epoch++;break;
    case 5:k.netId=k.netId==3?4:3;break;case 6:k.objectId++;break;case 7:k.attackId++;break;
    case 8:k.damage++;break;case 9:queued=false;break;case 10:qs.native.frame++;break;}
   kh2coop::HitClaim claim{};claim.epoch=k.epoch;claim.seq=k.sequence;claim.netId=static_cast<std::uint16_t>(k.netId);
   claim.objectId=k.objectId;claim.requesterConnectionId=k.connection;claim.attackId=k.attackId;claim.damage=k.damage;
   claim.attackerSlot=static_cast<kh2coop::SlotType>(s.native.slot);
   auto bytes=kh2coop::encode(claim);if(m==11)bytes[0]=23;
   if(m!=1)ledger.Claim(qs,k,event,bytes.data(),static_cast<unsigned>(bytes.size()),queued,3);
   if(m==12)ledger.Claim(qs,k,event,bytes.data(),static_cast<unsigned>(bytes.size()),true,4);
   if(m==13){++k.sequence;claim.seq=k.sequence;bytes=kh2coop::encode(claim);
    ledger.Claim(qs,k,event,bytes.data(),static_cast<unsigned>(bytes.size()),true,4);}
   if(m==14){ledger.Hit(s,e,4);while(ledger.Pop(r)){}
    ++k.sequence;claim.seq=k.sequence;bytes=kh2coop::encode(claim);
    ledger.Claim(qs,k,event,bytes.data(),static_cast<unsigned>(bytes.size()),true,5);}
   auto hit=e;if(m==15){--hit.after.victim.hp;}if(m==16){++hit.policy.roster[1];}
   ledger.Hit(s,hit,6);bool qualified=false;
   while(ledger.Pop(r))if(r.kind==cc::Kind::Hit)qualified=r.locallyQualified;
   Check(qualified==(m==0),"client exact queued claim one-shot association; mismatched/missing/duplicate/reused REFUSE");
   if(m==0){ledger.Population(s,targets,5,7);
    Check(!ledger.Retired(),"client witness cannot reduce expected enemy HP or create host authority");
    ledger.Hit(s,e,8);Check(Has(ledger,cc::Kind::Hit,false),"client event cannot qualify twice without a new Apply");}
   if(m==15)Check(ledger.Retired(),"client native HP change still hits unchanged UnknownDelta guard");
  }
  // Exact production ordering: claim observes the active Apply token before
  // direct Stat and EndApply; no token is borrowed from a completed event.
  auto orderedHolder=std::make_unique<cc::Engine>();auto& ordered=*orderedHolder;cc::Target rows[5];for(unsigned i=0;i<5;++i){rows[i]=Target(i+1);rows[i].hp=rows[i].maxHp=20;}
  auto& target=rows[key.netId-1];target.actor=retained.before.victim.actor;
  target.objentry=retained.before.victim.objentry;target.status=retained.before.victim.status;target.hp=retained.before.victim.hp;
  auto initial=s;--initial.native.frame;ordered.Population(initial,rows,5,1);ordered.Population(s,rows,5,2);
  cc::Receipt receipt;while(ordered.Pop(receipt)){}
  ht::ApplyToken apply{};ht::BeginApply(apply,retained.callerRva,true,retained.before);apply.event.policy=retained.policy;
  kh2coop::HitClaim claim{};claim.epoch=key.epoch;claim.seq=key.sequence;claim.netId=static_cast<std::uint16_t>(key.netId);
  claim.objectId=key.objectId;claim.requesterConnectionId=key.connection;claim.attackId=key.attackId;claim.damage=key.damage;
  claim.attackerSlot=static_cast<kh2coop::SlotType>(s.native.slot);auto bytes=kh2coop::encode(claim);
  const auto active=ht::CurrentApplySequence();
  ordered.Claim(s,key,active,bytes.data(),static_cast<unsigned>(bytes.size()),true,3);
  const auto& c=retained.children[0];ht::ChildToken stat{};
  ht::BeginStat(stat,c.actor,c.delta,c.stat,c.react,c.callerRva,true,c.before);ht::EndStat(stat,true,c.result,&c.after);
  ht::EndApply(apply,true,retained.rawResult,&retained.after);ht::Event actual{};
  Check(ht::PopEvent(actual)&&actual.sequence==active&&actual.children[0].applySequence==active,
        "real client queue callback ordering binds original active Apply and direct Stat");
  ordered.Hit(s,actual,4);Check(Has(ordered,cc::Kind::Hit,true),"active Apply queued once then real EndApply suppression qualifies");
  ordered.Population(s,rows,5,5);Check(!ordered.Retired(),"ordered client path leaves local HP ledger unchanged");
  for(unsigned i=0;i<=cc::CauseCount;++i){claim.seq=key.sequence+i+1;auto k=key;k.sequence=claim.seq;
   bytes=kh2coop::encode(claim);
   ordered.Claim(s,k,actual.sequence+i+1,bytes.data(),static_cast<unsigned>(bytes.size()),true,10+i);
   while(ordered.Pop(receipt)){}
  }
  Check(ordered.Retired(),"bounded pending Apply/claim association exhaustion refuses, never overwrites authority");
 }
}
