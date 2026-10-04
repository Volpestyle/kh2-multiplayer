// Windows-only, production core linked separately. No game process or memory.
#include "NativeHitTrace.hpp"
#include "DamagePolicy.hpp"
#include <Windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

namespace ht = kh2coop::inject::nativehittrace;
namespace dp = kh2coop::inject::damagepolicy;
namespace {
unsigned g_checks = 0, g_failed = 0;
void Check(bool condition, const char* name) {
    ++g_checks;
    if (!condition) { ++g_failed; std::printf("FAIL %s\n", name); }
}
ht::ApplyFacts Facts() {
    ht::ApplyFacts f {};
    f.context.available = true; f.context.readMask = ht::ContextComplete;
    f.context.frame=71;f.context.generation=4;f.context.epoch=9;f.context.role=2;f.context.slot=2;
    f.context.connectionId=0x100000002ULL;f.context.hostConnectionId=0x200000003ULL;
    f.context.transitionSerial=3;f.context.loadSerial=2;
    f.context.location[0]=2;f.context.location[1]=8;f.context.location[2]=3;
    f.context.location[3]=0x1234;f.context.location[4]=0x2345;f.context.location[5]=0x3456;
    f.victim.actor=0x10100;f.victim.objentry=0x10200;f.victim.status=0x10300;
    f.victim.objectId=84;f.victim.readMask=ht::ActorComplete;f.victim.hp=100;f.victim.maxHp=120;f.victim.type=0;f.victim.namePrefix=0x5F50;
    f.source=f.victim;f.source.actor=0x20100;f.source.objentry=0x20200;f.source.status=0x20300;
    f.source.objectId=309;f.source.type=4;f.source.hp=160;f.source.maxHp=160;f.source.team=0x12340001;f.source.namePrefix=0x5F4D;
    f.hit.hit=0x30100;f.hit.attack=0x30200;f.hit.owner=f.source.actor;f.hit.attackHandle=19;f.hit.ownerHandle=20;
    f.hit.atkpHandle=21;f.hit.attackId=33;f.hit.readMask=ht::HitComplete;f.hit.kind=1;f.hit.damage=12;
    f.hit.canonicalPlayer=f.victim.actor;f.hit.head=f.victim.actor;f.hit.tracked=f.victim.actor;
    return f;
}
ht::ApplyFacts After(const ht::ApplyFacts& before) {
    auto after=before;after.victim.hp=93;after.hit.flags|=2;return after;
}
void EmptyQueue() { ht::Event event {}; while (ht::PopEvent(event)) {} }
ht::Event Emit(const ht::ApplyFacts& before, const ht::ApplyFacts& after, uintptr_t caller=0x3D613C,
    uintptr_t childActor=0, bool matchingTake=true, bool normal=true) {
    ht::ApplyToken parent {};ht::ChildToken take {}, stat {};
    const auto actor=childActor ? childActor : before.victim.actor;
    ht::BeginApply(parent,caller,true,before);
    if (matchingTake) ht::BeginTake(take,actor,-7,0,1,0x3D3CD5,true,before.victim);
    ht::BeginStat(stat,actor,-7,0,257,0,false,before.victim);
    ht::EndStat(stat,normal,after.victim.hp,normal ? &after.victim : nullptr);
    if (matchingTake) ht::EndTake(take,normal,normal ? &after.victim : nullptr);
    ht::EndApply(parent,normal,0x123456789ABCDEF0ULL,normal ? &after : nullptr);
    ht::Event event {};Check(ht::PopEvent(event),"emitted event present");return event;
}
ht::ApplyFacts g_before {}, g_after {};
unsigned g_applyCalls=0,g_takeCalls=0,g_statCalls=0,g_reads=0;
bool g_raise=false;
uintptr_t g_argVictim=0,g_argHit=0;
int g_argDelta=0,g_argStat=0,g_argReact=0;
constexpr DWORD TestException=0xE04B4832;
int __fastcall OriginalStat(void* actor,int delta,int stat,int react) {
    ++g_statCalls;g_argVictim=reinterpret_cast<uintptr_t>(actor);g_argDelta=delta;g_argStat=stat;g_argReact=react;
    if (g_raise) RaiseException(TestException,0,0,nullptr);
    return g_after.victim.hp;
}
int __fastcall StatAdapter(void* actor,int delta,int stat,int react) {
    if (!ht::CanCaptureChild()) return OriginalStat(actor,delta,stat,react);
    ht::ChildToken token {};int result=0;bool normal=false;
    ++g_reads;ht::BeginStat(token,reinterpret_cast<uintptr_t>(actor),delta,stat,react,0,false,g_before.victim);
    __try { result=OriginalStat(actor,delta,stat,react);normal=true; }
    __finally { if (normal) ++g_reads;ht::EndStat(token,normal,result,normal ? &g_after.victim : nullptr); }
    return result;
}
void __fastcall OriginalTake(void* actor,int delta,int stat,std::uint8_t react) {
    ++g_takeCalls;StatAdapter(actor,delta,stat,react);
}
void __fastcall TakeAdapter(void* actor,int delta,int stat,std::uint8_t react) {
    if (!ht::CanCaptureChild()) { OriginalTake(actor,delta,stat,react);return; }
    ht::ChildToken token {};bool normal=false;
    ++g_reads;ht::BeginTake(token,reinterpret_cast<uintptr_t>(actor),delta,stat,react,0x3D3CD5,true,g_before.victim);
    __try { OriginalTake(actor,delta,stat,react);normal=true; }
    __finally { if (normal) ++g_reads;ht::EndTake(token,normal,normal ? &g_after.victim : nullptr); }
}
uintptr_t __fastcall OriginalApply(void* victim,void* hit) {
    ++g_applyCalls;g_argHit=reinterpret_cast<uintptr_t>(hit);TakeAdapter(victim,-7,0,1);return 0xFEDCBA9876543210ULL;
}
uintptr_t __fastcall ApplyAdapter(void* victim,void* hit) {
    if (!ht::CanCaptureApply()) return OriginalApply(victim,hit);
    ht::ApplyToken token {};uintptr_t result=0;bool normal=false;
    ++g_reads;ht::BeginApply(token,0x3D613C,true,g_before);
    __try { result=OriginalApply(victim,hit);normal=true; }
    __finally { if (normal) ++g_reads;ht::EndApply(token,normal,result,normal ? &g_after : nullptr); }
    return result;
}
bool CatchApply() {
    __try { ApplyAdapter(reinterpret_cast<void*>(g_before.victim.actor),reinterpret_cast<void*>(g_before.hit.hit)); }
    __except(GetExceptionCode()==TestException ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return true; }
    return false;
}
bool CatchNested() {
    __try {
        ht::ApplyToken outer {};ht::BeginApply(outer,0x4444,true,g_before);
        __try { ApplyAdapter(reinterpret_cast<void*>(g_before.victim.actor),reinterpret_cast<void*>(g_before.hit.hit)); }
        __finally { ht::EndApply(outer,false,0,nullptr); }
    }
    __except(GetExceptionCode()==TestException ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return true; }
    return false;
}
DWORD WINAPI Foreign(void*) {
    ApplyAdapter(reinterpret_cast<void*>(g_before.victim.actor),reinterpret_cast<void*>(g_before.hit.hit));return 0;
}
std::vector<std::string> g_lines;
void Logger(const char* format,...) {
    char line[4096] {};va_list args;va_start(args,format);vsnprintf(line,sizeof(line),format,args);va_end(args);g_lines.emplace_back(line);
}
bool HasLine(const char* fragment) { for(const auto& line:g_lines) if(line.find(fragment)!=std::string::npos) return true;return false; }
void StdoutLogger(const char* format,...) {
    va_list args;va_start(args,format);std::vprintf(format,args);va_end(args);std::putchar('\n');
}
int EmitBaseline() {
    // Fresh-process production serializer control: no reconstructed log rows,
    // game reads, hooks or claims. The real public core observes fake originals.
    g_before=Facts();g_after=After(g_before);
    ht::RegisterOwnerThread();ht::Configure(true,ht::AllHooks,ht::AllHooks);
    ApplyAdapter(reinterpret_cast<void*>(g_before.victim.actor),reinterpret_cast<void*>(g_before.hit.hit));
    ht::Drain(StdoutLogger);
    return 0;
}

// Policy supplement controls execute the real observer/core/serializer and
// Evaluate/TryZeroHp over an owned hit record. Actor/context snapshots and the
// original/claim/revalidation callbacks remain synthetic. This is not execution
// of EntityHook, current puppet membership, native AI or an authenticated hit.
enum class PolicyZeroMode { Normal, Changed, Fault, NotAttempted };
unsigned g_policyOriginals=0,g_policyClaims=0;
int g_policyClaimAmount=0;
bool g_policyRaise=false,g_policyQueueClaim=true;
constexpr uintptr_t PolicyRawResult=0xABCDEF9876543210ULL;

template<typename T> void PolicyPut(unsigned char* record,std::size_t offset,T value) {
    std::memcpy(record+offset,&value,sizeof(value));
}
template<typename T> T PolicyGet(const unsigned char* record,std::size_t offset) {
    T value {};std::memcpy(&value,record+offset,sizeof(value));return value;
}
void PreparePolicyRecord(unsigned char* record,const ht::ApplyFacts& before) {
    std::memset(record,0,64);
    PolicyPut(record,0x18,before.hit.flags);PolicyPut(record,0x25,before.hit.stat);
    PolicyPut(record,0x28,before.hit.damage);
    PolicyPut(record,0x80,before.victim.hp);
}
ht::ApplyFacts PolicyFacts(unsigned char* record,bool remoteVictim) {
    auto f=Facts();f.hit.hit=reinterpret_cast<uintptr_t>(record);
    f.victim.status=reinterpret_cast<uintptr_t>(record+0x80);
    if(remoteVictim) {
        f.hit.canonicalPlayer=0x40100;f.hit.head=0x40100;f.hit.tracked=0x40100;
    } else {
        f.source.type=0;f.source.objectId=84;f.source.namePrefix=0x5F50;
        f.source.hp=100;f.source.maxHp=120;
    }
    return f;
}
ht::PolicyObservation PolicySnapshot(const ht::ApplyFacts& before,bool remoteVictim) {
    ht::PolicyObservation p {};p.recorded=true;p.authority=before;
    p.roster[0]=before.context.hostConnectionId;p.roster[1]=0x300000004ULL;
    p.roster[2]=before.context.connectionId;
    p.facts.ownerThread=true;p.facts.contextAvailable=true;p.facts.role=dp::Role::Client;
    p.facts.victim=remoteVictim ? dp::ActorClass::NonLocalPlayer : dp::ActorClass::LocalAvatar;
    p.facts.source=remoteVictim ? dp::ActorClass::Enemy : dp::ActorClass::NonLocalPlayer;
    p.facts.hit.flags=before.hit.flags;p.facts.hit.stat=before.hit.stat;
    p.facts.hit.amount=before.hit.damage;p.facts.hit.kind=before.hit.kind;
    p.facts.hit.readMask=dp::RequiredHitReads|dp::KindAvailable;
    p.decision=dp::Evaluate(p.facts);return p;
}
uintptr_t PolicyOriginal(unsigned char* record) {
    ++g_policyOriginals;
    if(g_policyRaise) RaiseException(TestException,0,0,nullptr);
    const auto flags=PolicyGet<std::uint32_t>(record,0x18);
    if(!(flags&2U)) {
        PolicyPut(record,0x18,flags|2U);
        if(PolicyGet<std::uint8_t>(record,0x25)==0) {
            const auto hp=PolicyGet<std::int32_t>(record,0x80);
            const auto amount=PolicyGet<std::int32_t>(record,0x28);
            PolicyPut(record,0x80,hp-amount);
        }
    }
    return PolicyRawResult;
}
uintptr_t PolicyAdapter(unsigned char* record,const ht::ApplyFacts& before,
                        ht::PolicyObservation policy,PolicyZeroMode mode=PolicyZeroMode::Normal) {
    ht::ApplyToken token {};ht::BeginApply(token,0x3D613C,true,before);
    uintptr_t result=0;bool normal=false;ht::ApplyFacts after {};
    // Public POD attachment records exactly the operations this fixture made.
    // It intentionally does not implement EntityHook's native membership reads.
    token.event.policy=policy;
    auto& observed=token.event.policy;
    if(policy.decision.action==dp::Action::ClaimThenZeroHp) {
        ++g_policyClaims;g_policyClaimAmount=policy.facts.hit.amount;
        observed.claimAttempted=true;observed.claimQueued=g_policyQueueClaim;
    }
    if(policy.decision.supported && policy.decision.action!=dp::Action::Native) {
        observed.revalidationAttempted=true;
        observed.revalidationPassed=mode!=PolicyZeroMode::NotAttempted;
        if(observed.revalidationPassed) {
            if(mode==PolicyZeroMode::Changed) PolicyPut(record,0x28,policy.facts.hit.amount+1);
            DWORD previous=0;
            const bool protect=mode==PolicyZeroMode::Fault;
            if(protect && !VirtualProtect(record,4096,PAGE_READONLY,&previous))
                RaiseException(TestException,0,0,nullptr);
            observed.zeroAttempted=true;
            observed.zeroResult=dp::TryZeroHp(reinterpret_cast<uintptr_t>(record),policy.facts.hit);
            if(protect) { DWORD discarded=0;VirtualProtect(record,4096,previous,&discarded); }
        }
    }
    __try {
        result=PolicyOriginal(record);normal=true;
        after=before;after.hit.flags=PolicyGet<std::uint32_t>(record,0x18);
        after.hit.damage=PolicyGet<std::int32_t>(record,0x28);
        after.victim.hp=PolicyGet<std::int32_t>(record,0x80);
    } __finally {
        ht::EndApply(token,normal && !AbnormalTermination(),result,
                     normal && !AbnormalTermination() ? &after : nullptr);
    }
    return result;
}
bool CatchPolicy(unsigned char* record,const ht::ApplyFacts& before,const ht::PolicyObservation& policy) {
    __try { PolicyAdapter(record,before,policy); }
    __except(GetExceptionCode()==TestException ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return true; }
    return false;
}
unsigned char* AllocatePolicyRecord() {
    return static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
}
int EmitPolicyVeto() {
    auto* record=AllocatePolicyRecord();
    if(!record) return 2;
    ht::RegisterOwnerThread();ht::Configure(true,ht::AllHooks,ht::AllHooks);
    for(const bool remoteVictim : {false,true}) {
        const auto before=PolicyFacts(record,remoteVictim);const auto p=PolicySnapshot(before,remoteVictim);
        PreparePolicyRecord(record,before);PolicyAdapter(record,before,p);
    }
    ht::Drain(StdoutLogger);VirtualFree(record,0,MEM_RELEASE);return 0;
}
void PolicyControls() {
    auto* record=AllocatePolicyRecord();Check(record!=nullptr,"policy owned record allocated");if(!record) return;
    ht::Configure(true,ht::AllHooks,ht::AllHooks);EmptyQueue();
    ht::Event event {};
    auto before=PolicyFacts(record,false);auto p=PolicySnapshot(before,false);
    PreparePolicyRecord(record,before);
    const auto originalCount=g_policyOriginals;
    const auto raw=PolicyAdapter(record,before,p);
    Check(ht::PopEvent(event),"policy event published through actual core");
    Check(raw==PolicyRawResult && event.rawResult==raw && g_policyOriginals==originalCount+1,
        "policy fixture preserves opaque original result and one original call");
    Check(event.policy.recorded && event.policy.decision.action==dp::Action::ZeroHp &&
        event.policy.decision.reason==dp::Reason::RemoteSource,"actual Evaluate remote source decision retained");
    Check(event.policy.revalidationAttempted && event.policy.revalidationPassed && event.policy.zeroAttempted &&
        event.policy.zeroResult==dp::ZeroResult::Zeroed,"actual zero result not inferred from decision");
    Check(event.before.victim.hp==100 && event.after.victim.hp==100 && event.after.hit.damage==0 &&
        (event.after.hit.flags&2U) && event.returned && !event.unwound,"zeroed original consumes record with checked fixture HP unchanged");
    Check(!event.witness && event.childCount==0,"policy veto is not an incoming damage witness");
    Check(event.policy.roster[0]==0x200000003ULL && event.policy.roster[1]==0x300000004ULL &&
        event.policy.roster[2]==0x100000002ULL && event.policy.authority.context.location[3]==0x1234,
        "policy copy preserves all roster high bits and full room programs");
    Check(event.policy.authority.hit.damage==12 && event.policy.facts.hit.amount==12 &&
        !event.policy.claimAttempted && !event.policy.claimQueued,"authority input retained separately from zeroed native post state");

    before=PolicyFacts(record,true);p=PolicySnapshot(before,true);PreparePolicyRecord(record,before);
    PolicyAdapter(record,before,p);Check(ht::PopEvent(event) && event.policy.decision.reason==dp::Reason::RemoteVictim &&
        event.before.victim.actor!=event.before.hit.canonicalPlayer && event.after.victim.hp==100,
        "noncanonical type0 victim veto and unchanged HP retained");

    before=PolicyFacts(record,false);p=PolicySnapshot(before,false);
    for(const auto mode : {PolicyZeroMode::Changed,PolicyZeroMode::Fault,PolicyZeroMode::NotAttempted}) {
        PreparePolicyRecord(record,before);PolicyAdapter(record,before,p,mode);
        Check(ht::PopEvent(event),"unsuccessful zero observation published");
        const auto expected=mode==PolicyZeroMode::Changed ? dp::ZeroResult::Changed :
            mode==PolicyZeroMode::Fault ? dp::ZeroResult::Fault : dp::ZeroResult::InvalidExpected;
        Check(event.policy.zeroResult==expected && event.policy.zeroAttempted==(mode!=PolicyZeroMode::NotAttempted) &&
            event.policy.revalidationPassed==(mode!=PolicyZeroMode::NotAttempted),"Changed Fault and unattempted remain distinct copied outcomes");
        Check(event.after.victim.hp<event.before.victim.hp,"failed or unattempted zero is not a fabricated HP veto");
    }
    // Observe the real claim decision and copied sink result; no network send.
    before=Facts();before.hit.hit=reinterpret_cast<uintptr_t>(record);before.victim.status=reinterpret_cast<uintptr_t>(record+0x80);
    before.victim.type=4;before.victim.objectId=309;before.victim.namePrefix=0x5F4D;
    before.source.type=0;before.source.objectId=84;before.source.namePrefix=0x5F50;
    before.hit.canonicalPlayer=before.source.actor;before.hit.head=before.source.actor;before.hit.tracked=before.source.actor;
    p=PolicySnapshot(before,false);p.facts.source=dp::ActorClass::LocalAvatar;p.facts.victim=dp::ActorClass::Enemy;
    p.decision=dp::Evaluate(p.facts);g_policyQueueClaim=false;PreparePolicyRecord(record,before);
    const auto claimCount=g_policyClaims;PolicyAdapter(record,before,p);
    Check(ht::PopEvent(event) && event.policy.claimAttempted && !event.policy.claimQueued &&
        g_policyClaims==claimCount+1 && g_policyClaimAmount==12 && event.policy.zeroResult==dp::ZeroResult::Zeroed,
        "failed synthetic claim preserves one attempt and actual successful zero");
    g_policyQueueClaim=true;PreparePolicyRecord(record,before);PolicyAdapter(record,before,p);
    Check(ht::PopEvent(event) && event.policy.claimAttempted && event.policy.claimQueued,
        "queued synthetic claim preserved without host acceptance inference");

    before=PolicyFacts(record,false);p=PolicySnapshot(before,false);PreparePolicyRecord(record,before);
    g_policyRaise=true;const auto beforeFault=g_policyOriginals;
    Check(CatchPolicy(record,before,p),"policy synthetic original SEH propagates");g_policyRaise=false;
    Check(ht::PopEvent(event) && event.policy.recorded && event.policy.zeroResult==dp::ZeroResult::Zeroed &&
        event.unwound && !event.returned && event.after.victim.readMask==0 && g_policyOriginals==beforeFault+1,
        "abnormal original keeps actual pre-call policy outcome without fabricated post snapshot");
    PreparePolicyRecord(record,before);PolicyAdapter(record,before,p);
    Check(ht::PopEvent(event) && event.depth==1 && event.parentSequence==0 && event.returned,"policy unwind restores next scope association");

    ht::ApplyToken outer {},inner {};ht::BeginApply(outer,0x3D613C,true,before);outer.event.policy=p;
    ht::BeginApply(inner,0x3D613C,true,before);inner.event.policy=p;inner.event.policy.roster[1]=0x400000005ULL;
    ht::EndApply(inner,true,1,&before);ht::EndApply(outer,true,2,&before);
    Check(ht::PopEvent(event) && event.parentSequence==outer.event.sequence && event.nested &&
        event.policy.roster[1]==0x400000005ULL,"nested child policy remains bound to own parent sequence");
    Check(ht::PopEvent(event) && event.parentSequence==0 && event.policy.roster[1]==0x300000004ULL,
        "nested supplement cannot overwrite outer copied roster");

    g_lines.clear();ht::ApplyToken unrecorded {};ht::BeginApply(unrecorded,0x3D613C,true,before);
    unrecorded.event.policy=p;unrecorded.event.policy.recorded=false;ht::EndApply(unrecorded,true,0,&before);ht::Drain(Logger);
    Check(HasLine("[hittrace] event schema=1") && !HasLine("[damagepolicy]"),"unrecorded policy emits no supplement and preserves historical hit schema");
    g_lines.clear();PreparePolicyRecord(record,before);PolicyAdapter(record,before,p);ht::Drain(Logger);
    Check(HasLine("[damagepolicy] event schema=1") && HasLine("[damagepolicy] context schema=1") &&
        HasLine("[damagepolicy] actor schema=1") && HasLine("[damagepolicy] input schema=1"),"actual serializer emits optional copied policy envelope");
    unsigned policyLines=0;unsigned long long hitSequence=0,policySequence=0;
    for(const auto& text:g_lines) {
        if(text.find("[damagepolicy]")==0) ++policyLines;
        if(text.find("[hittrace] event schema=1 ")==0)
            (void)sscanf_s(text.c_str(),"[hittrace] event schema=1 seq=%llu",&hitSequence);
        if(text.find("[damagepolicy] event schema=1 ")==0)
            (void)sscanf_s(text.c_str(),"[damagepolicy] event schema=1 seq=%llu",&policySequence);
    }
    Check(policyLines==5 && hitSequence!=0 && hitSequence==policySequence,
        "five optional rows share the actual parent event sequence");
    Check(HasLine("roster0=8589934595 roster1=12884901892 roster2=4294967298") &&
        HasLine("revalidationAttempted=1 revalidationPassed=1 zeroAttempted=1 zeroResult=0 claimAttempted=0 claimQueued=0"),
        "serializer preserves fullwidth roster and exact attempted zero outcome");

    EmptyQueue();const auto droppedBefore=ht::GetStats().dropped;
    ht::ApplyToken token {};
    for(unsigned i=0;i<ht::QueueCapacity+1;++i) {
        ht::BeginApply(token,0x3D613C,true,before);token.event.policy=p;ht::EndApply(token,true,0,&before);
    }
    Check(ht::GetStats().dropped==droppedBefore+1,"policy envelope stays in bounded existing event queue");
    unsigned retained=0;while(ht::PopEvent(event)) { if(event.policy.recorded && event.policy.roster[1]==p.roster[1]) ++retained; }
    Check(retained==ht::QueueCapacity,"queued policies preserve exact parent payload across queue saturation");
    ht::Shutdown();PreparePolicyRecord(record,before);
    const auto disabledOriginals=g_policyOriginals;PolicyAdapter(record,before,p);
    Check(g_policyOriginals==disabledOriginals+1 && PolicyGet<std::int32_t>(record,0x80)==100 &&
        PolicyGet<std::int32_t>(record,0x28)==0 && !ht::PopEvent(event),
        "trace opt-out leaves synthetic policy execution unchanged and publishes no event");
    VirtualFree(record,0,MEM_RELEASE);
}
}
int main(int argc,char** argv) {
    if(argc==2 && std::strcmp(argv[1],"--emit-baseline")==0) return EmitBaseline();
    if(argc==2 && std::strcmp(argv[1],"--emit-policy-veto")==0) return EmitPolicyVeto();
    g_before=Facts();g_after=After(g_before);
    ht::Configure(true,ht::AllHooks,ht::AllHooks);
    ApplyAdapter(reinterpret_cast<void*>(g_before.victim.actor),reinterpret_cast<void*>(g_before.hit.hit));
    Check(g_applyCalls==1 && g_takeCalls==1 && g_statCalls==1 && g_reads==0,"unknown thread: originals once, no snapshots");
    ht::Event event {};Check(!ht::PopEvent(event) && ht::GetStats().foreign>=1,"unknown thread counted without semantic event");
    ht::RegisterOwnerThread();Check(ht::IsOwnerThread(),"owner registration");
    const auto raw=ApplyAdapter(reinterpret_cast<void*>(g_before.victim.actor),reinterpret_cast<void*>(g_before.hit.hit));
    Check(raw==0xFEDCBA9876543210ULL && g_applyCalls==2 && g_takeCalls==2 && g_statCalls==2,"genuine opaque RAX and originals once");
    Check(g_argVictim==g_before.victim.actor && g_argHit==g_before.hit.hit && g_argDelta==-7 && g_argStat==0 && g_argReact==1,"genuine forwarded native arguments");
    Check(ht::PopEvent(event) && event.witness && event.rawResult==raw,"complete adjusted damage witness");
    Check(event.children[1].takeSequence==event.children[0].sequence && !event.children[1].callerAvailable,"DLL child return still has explicit Take scope");
    Check(event.before.context.connectionId==0x100000002ULL && event.before.context.location[3]==0x1234 && event.before.source.team==0x12340001,"full width copied identities/program/team");
    auto before=Facts();auto after=After(before);
    event=Emit(before,after);Check(event.witness,"baseline synthetic witness");
    before.hit.flags=1;after=After(before);event=Emit(before,after);Check(event.witness,"unrelated applied flags bit0 not bit1");
    before=Facts();before.hit.flags=2;after=After(before);event=Emit(before,after);Check(!event.witness,"already applied bit1 rejected");
    before=Facts();after=After(before);after.hit.flags=1;event=Emit(before,after);Check(!event.witness,"missing final applied bit1 rejected");
    before=Facts();after=After(before);event=Emit(before,after,0x3D613D);Check(!event.witness,"wrong direct caller unknown");
    event=Emit(before,after,0x3D613C,0x99999);Check(!event.witness && !event.children[0].matching,"wrong child actor unknown");
    event=Emit(before,after,0x3D613C,0,false);Check(!event.witness && !event.children[0].matching,"unscoped Stat unknown");
    before.victim.readMask&=~ht::ActorRepeated;after=After(before);event=Emit(before,after);Check(!event.witness,"partial victim metadata unknown");
    before=Facts();before.hit.readMask&=~ht::HitOwner;after=After(before);event=Emit(before,after);Check(!event.witness,"unresolved owner unknown");
    before=Facts();before.context.readMask&=~ht::ContextPhase;after=After(before);event=Emit(before,after);Check(!event.witness,"partial context unknown");
    before=Facts();after=After(before);after.context.connectionId+=0x100000000ULL;event=Emit(before,after);Check(!event.witness && !event.contextStable,"high connection bits distinguish session");
    after=After(before);after.context.location[3]+=0x100;event=Emit(before,after);Check(!event.contextStable,"high program bits distinguish room");
    after=After(before);++after.context.generation;event=Emit(before,after);Check(!event.contextStable,"generation change unknown");
    after=After(before);++after.context.epoch;event=Emit(before,after);Check(!event.contextStable,"epoch change unknown");
    after=After(before);++after.context.loadSerial;event=Emit(before,after);Check(!event.contextStable,"load serial change unknown");
    after=After(before);after.victim.status+=8;event=Emit(before,after);Check(!event.metadataStable,"status replacement unknown");
    after=After(before);after.victim.hp=92;event=Emit(before,after);Check(!event.witness,"adjusted delta must match HP");
    before=Facts();before.hit.canonicalPlayer+=8;after=After(before);event=Emit(before,after);Check(!event.witness,"noncanonical victim unknown");
    before=Facts();before.source.type=3;after=After(before);event=Emit(before,after);Check(!event.witness,"ordinary witness excludes boss");
    before=Facts();before.source.type=0;after=After(before);event=Emit(before,after);Check(!event.witness,"player source not enemy witness");
    before=Facts();before.source.namePrefix=0x5F46;after=After(before);event=Emit(before,after);Check(!event.witness,"existing F_ exclusion not ordinary enemy");
    before=Facts();before.source.readMask&=~ht::ActorName;after=After(before);event=Emit(before,after);Check(!event.witness,"unknown source name classification unavailable");
    before=Facts();before.source.hp=0;after=After(before);event=Emit(before,after);Check(!event.witness,"dead source not living enemy witness");
    before=Facts();before.source.maxHp=0;after=After(before);event=Emit(before,after);Check(!event.witness,"missing source max HP not living enemy witness");
    before=Facts();before.context.slot=0;after=After(before);event=Emit(before,after);Check(!event.witness,"client requires client-owned slot");
    before=Facts();before.context.loadSerial=0;after=After(before);event=Emit(before,after);Check(!event.witness,"missing load serial unknown");
    before=Facts();before.hit.kind=5;after=After(before);event=Emit(before,after);Check(!event.witness,"healing type excluded");
    before=Facts();before.hit.manualFilterOn=true;after=After(before);event=Emit(before,after);Check(!event.witness,"manual filter enabled unknown");
    before=Facts();before.hit.syncDrop=true;after=After(before);event=Emit(before,after);Check(!event.witness,"sync drop not ordinary victim witness");
    before=Facts();after=After(before);
    ht::Configure(true,7,3);event=Emit(before,after);Check(!event.witness && event.coverageMask==3,"partial installation coverage unknown");
    ht::Configure(true,7,7);
    ht::ApplyToken token {};ht::BeginApply(token,0x3D613C,true,before);ht::Configure(true,7,7);ht::EndApply(token,true,0,&after);
    Check(ht::PopEvent(event) && !event.coverageStable && !event.witness,"reconfiguration serial invalidates active scope");
    ht::BeginApply(token,0x3D613C,true,before);
    for(unsigned i=0;i<ht::ChildCapacity+1;++i) { ht::ChildToken c {};ht::BeginTake(c,before.victim.actor,-7,0,1,0,false,before.victim);ht::EndTake(c,true,&after.victim); }
    ht::EndApply(token,true,0,&after);Check(ht::PopEvent(event) && event.overflow && event.takeCalls==9 && event.childCount==8 && !event.witness,"child cap explicit no witness");
    ht::ApplyToken outer {},inner {};ht::BeginApply(outer,0x3D613C,true,before);ht::BeginApply(inner,0x3D613C,true,before);
    ht::EndApply(inner,true,0,&after);ht::EndApply(outer,true,0,&after);
    Check(ht::PopEvent(event) && event.nested && event.depth==2 && event.parentSequence!=0,"nested parent event explicit");
    Check(ht::PopEvent(event) && event.nested && event.depth==1,"outer scope marks nested ambiguity");
    ht::ApplyToken scopes[ht::MaxDepth+2] {};
    for(auto& scope:scopes) ht::BeginApply(scope,0x3D613C,true,before);
    for(unsigned i=ht::MaxDepth+2;i>0;--i) ht::EndApply(scopes[i-1],false,0,nullptr);
    Check(ht::GetStats().overflow>=3,"depth cap explicit");EmptyQueue();
    const auto oldReads=g_reads;const auto oldApply=g_applyCalls;const auto oldTake=g_takeCalls;const auto oldStat=g_statCalls;g_raise=true;
    Check(CatchApply(),"native SEH propagates to outer handler");
    Check(g_applyCalls==oldApply+1 && g_takeCalls==oldTake+1 && g_statCalls==oldStat+1 && g_reads==oldReads+3,"fault originals once and no post snapshots");
    Check(ht::PopEvent(event) && event.unwound && !event.returned && event.after.victim.readMask==0 && !event.witness,"fault event has no fabricated post state");
    Check(event.children[0].unwound && event.children[1].unwound && !event.children[1].returned,"child unwind chain explicit");
    Check(CatchNested(),"nested SEH propagates");
    Check(ht::PopEvent(event) && event.depth==2 && event.unwound,"nested fault child depth");
    Check(ht::PopEvent(event) && event.depth==1 && event.unwound,"nested fault outer depth");
    g_raise=false;ApplyAdapter(reinterpret_cast<void*>(g_before.victim.actor),reinterpret_cast<void*>(g_before.hit.hit));
    Check(ht::PopEvent(event) && event.depth==1 && event.parentSequence==0 && event.witness,"TLS restored after native unwind");
    const auto foreignReads=g_reads;const auto foreignApply=g_applyCalls;
    HANDLE thread=CreateThread(nullptr,0,Foreign,nullptr,0,nullptr);
    Check(thread!=nullptr,"foreign control thread created");
    if(thread) { WaitForSingleObject(thread,INFINITE);CloseHandle(thread); }
    Check(g_reads==foreignReads && g_applyCalls==foreignApply+1 && !ht::PopEvent(event),"foreign original once no snapshot or witness");
    for(unsigned i=0;i<ht::QueueCapacity+1;++i) { ht::BeginApply(token,0,true,before);ht::EndApply(token,true,0,&after); }
    Check(ht::GetStats().dropped==1,"queue cap drops explicitly");EmptyQueue();
    ht::BeginApply(outer,0,true,before);
    for(unsigned i=0;i<ht::QueueCapacity+1;++i) { ht::BeginApply(token,0,true,before);ht::EndApply(token,true,0,&after); }
    EmptyQueue();ht::EndApply(outer,true,0,&after);
    Check(ht::PopEvent(event) && !event.lossStable && !event.witness,"scope crossing loss cannot witness");
    ht::BeginApply(token,0,true,before);ht::Shutdown();ht::EndApply(token,true,0,&after);
    Check(ht::PopEvent(event) && !event.coverageStable,"shutdown preserves cleanup and invalidates coverage");
    ht::Configure(true,7,7);ht::BeginApply(token,0x3D613C,true,before);ht::EndApply(token,true,0,&after);ht::Drain(Logger);
    Check(HasLine("[hittrace] ready schema=1") && HasLine("[hittrace] summary schema=1") && HasLine("location=000200080003123423453456"),"schema serialized full-width location and coverage");
    Check(HasLine("connectionId=4294967298 hostConnectionId=8589934595"),"schema serializes full-width connection IDs");
    g_lines.clear();const auto drainedBefore=ht::GetStats().drained;
    for(unsigned i=0;i<ht::QueueCapacity;++i) { ht::BeginApply(token,0,true,before);ht::EndApply(token,true,0,&after); }
    for(unsigned i=0;i<8;++i) ht::Drain(Logger);
    const auto drainedAfter=ht::GetStats().drained;
    Check(drainedAfter==drainedBefore+128 && !ht::PopEvent(event),"eight bounded Drain batches consume full queue128");
    const auto finalDrained=std::string("drained=")+std::to_string(drainedAfter)+" ";
    Check(!g_lines.empty() && g_lines.back().find("[hittrace] summary schema=1")!=std::string::npos &&
        g_lines.back().find(finalDrained)!=std::string::npos,"last backlog summary contains final drained count");
    g_lines.clear();ht::CanCaptureChild();ht::Drain(Logger);Check(HasLine("[hittrace] summary schema=1"),"unmatched-only counter change emits summary");
    ht::Shutdown();const auto disabledCalls=g_applyCalls;ApplyAdapter(reinterpret_cast<void*>(g_before.victim.actor),reinterpret_cast<void*>(g_before.hit.hit));
    Check(g_applyCalls==disabledCalls+1 && !ht::PopEvent(event),"opt-out original pass-through");
    PolicyControls();
    std::printf("NativeHitTraceTest: %u checks, %u failed\n",g_checks,g_failed);return g_failed ? 1 : 0;
}
