"""Offline source wiring and unchanged hard-boundary checks, not native execution."""
import json
import subprocess
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
BASE="3670aa3967c4e6cca4d16dc5df09ec8f7a38f115"
checks=[]
def check(ok,label):
    checks.append({"check":label,"pass":bool(ok)})
    if not ok: raise AssertionError(label)
def source(path):return (ROOT/path).read_text(encoding="utf-8")
def original(path):return subprocess.check_output(["git","show",BASE+":"+path],cwd=ROOT).decode()
def body(text,name):
    at=text.index(name);start=text.index("{",at);depth=1;i=start+1
    while depth:
        if text[i]=="{":depth+=1
        elif text[i]=="}":depth-=1
        i+=1
    return text[at:i]

trace=source("inject/src/CombatCausalTrace.hpp");old=original("inject/src/CombatCausalTrace.hpp")
for name in ("inline bool PlayerEnemyHit(","inline bool HostCompanionEnemyHit(",
             "inline bool HostPlayerDirectStatEnemyHit(","inline bool ClientPlayerDirectStatEnemyHit("):
    check(body(trace,name)==body(old,name),name+" unchanged")
guard="if(l&&!r.locallyQualified&&e.before.victim.hp!=e.after.victim.hp)Retire(s,now,Reason::UnknownDelta);"
check(guard in trace and guard in old,"unknown-source guard unchanged")
for path in ("inject/src/NativeHitTrace.hpp","CMakeLists.txt"):
    check(source(path)==original(path),path+" unchanged; no hooks or registration delta")
binding=source("inject/src/OrdinaryEnemyBinding.hpp")
check(body(binding,"inline bool Continuous(")==body(original("inject/src/OrdinaryEnemyBinding.hpp"),"inline bool Continuous("),"3670 continuous native-death guard unchanged")
sync=source("inject/src/EnemySync.cpp");impl=source("inject/src/EnemyOrdinaryLifecycle.inl")
hp=sync[sync.index("bool lifecycleUnknown=false;"):sync.index("g_hostHpSequence = m.sequence;")]
check("RejectUnadmittedLifecycle" in hp and "if(lifecycleUnknown)continue;" in hp,"whole raw unknown-ID HP rejected before floor update")
predicate=hp[:hp.index("if(lifecycleUnknown)continue;")]
check("combatcausal::" not in predicate.replace("ObserveCombatConsumerOutcome(packet,combatcausal::Reason::RejectUnadmittedLifecycle,e.netId,m.sequence);", ""),"diagnostic membership never controls product HP admission")
check("!OrdinaryLifecycleRoom() &&" in sync and
      "t.actor==s.actor" in sync and "PopulationSpawnRoots(t)!=PopulationSpawnRoots(s)" in sync and
      "FindNativeEnemy(census,t)" in sync,"non-lifecycle metadata replacement requires same listed actor with fresh changed roots")
check("(void)combatcausal::engine.RetireMember" in impl and
      "if(!combatcausal::engine.RetireMember" not in impl,"observer retirement cannot veto authenticated product lifecycle")
check("SameClientClaimScope(productBefore,productAfter)" in impl and
      "!combatcausal::SameScope(before,CausalScope())" not in impl and
      "deliveries[i]!=g_bridge.PeerDeliverySerial" in impl,
      "receiver product scope bookends remain complete with observer OFF")
native=source("tests/NativeHitClaimTest.cpp")
check("SeedPreparationShadows(1000)" in native and "QueueHostWorld(encode(cut))" in native and
      "ClearPendingOrdinaryCuts(); // no lifecycle/source certificate" in native,
      "native BC positive fixtures use five owned actors plus decoded source certificate with isolated history")
check("missing BC lifecycle proof refuses HP and claims" in native and
      "actual lifecycle retirement/birth/HP gameplay identical" in native,
      "actual native consumer controls cover proof refusal and observer-independent new births")
host=body(sync,"bool HostFrame(")
check(host.index("PublishOrdinaryLifecycle(census)")<host.index("EnemyManifest m;")<host.rindex("PublishOrdinaryLifecycle(census)")<host.index("EnemyHp hp;"),"retire -> append -> birth -> HP order")
check("deathSent = true" not in impl and "deathSent=true" not in impl,"membership never silently marks deathSent")
check("spawn.ordinaryBinding.removed=true" in impl and "spawn.netId=-1" in impl,"client old membership quarantined before replacement")
check("if(l.prior.highWater>0 && best->id!=l.prior.highWater)" in binding,"no newest-point live relabel")
check("g_ordinaryGeneration && (generation!=g_ordinaryGeneration" in impl and "source.hostSourceSerial<=g_ordinarySource->hostSourceSerial" in impl,"original native and source lifecycle anchors retained")
check("old->lastSeenFrame+1!=g_hitTraceFrame" in impl and "SameRecordCatalogSample(before,after)" in impl and "const auto repeated=CaptureNativeCensus();" in impl,"membership proof requires adjacent native history and repeated complete census/content")
check("if(cut.membershipOnly)ReceiveOrdinaryLifecycle" in sync and "cut.membershipOnly" in source("inject/src/EnemyPopulationRepair.inl"),"membership separated from native-death repair authority")
print(json.dumps({"status":"PASS","checks":checks,"nativeExecuted":False,"acceptance":False},indent=2))
