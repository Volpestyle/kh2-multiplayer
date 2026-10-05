#include "NativeLifetimeTrace.hpp"
#include "NativeLifetimePins.hpp"
#include <Windows.h>
#include <MinHook.h>
#include <intrin.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <type_traits>

namespace kh2coop::inject::lifetimetrace {
namespace {
using FactoryFn = uintptr_t (__fastcall*)(std::uint32_t,const float*,float);
// Opaque RAX is forwarded even though native destructor/release callers ignore it.
using PairFn = uintptr_t (__fastcall*)(void*,void*);
constexpr unsigned QueueCap=128, DepthCap=8;
constexpr uintptr_t Rvas[]{0x3DF930,0x419AB0,0x19C470};
struct HookState { void* target{}; std::uint8_t* trampoline{}; std::uint8_t* metadata{}; };
HookState g_hooks[3]{};
FactoryFn g_factory{};
PairFn g_destructor{},g_release{};
uintptr_t g_base{};
std::atomic<bool> g_requested{},g_recording{},g_retained{},g_attempted{};
std::atomic<std::uint32_t> g_verified{},g_installed{},g_failed{};
std::atomic<std::uint64_t> g_serial{},g_generation{1},g_published{},g_dropped{},g_foreign{},g_filtered{},g_unwound{},g_exceptions{},g_fiberBypassed{},g_fiberThreads{},g_scopeAbandoned{};
SRWLOCK g_queueLock=SRWLOCK_INIT;
std::array<Event,QueueCap> g_queue{};
unsigned g_read{},g_count{};
// FLS values point only into this permanently retained, never-reused POD pool.
// No Event*, Scope* or saved predecessor points into a native/fiber stack.
constexpr unsigned ContextCap=128;
struct Frame { Event event{}; std::uint64_t serial{}; uintptr_t anchor{}; bool visible{}; };
struct FiberContext {
    DWORD thread{}; uintptr_t key{}, stackHigh{}; unsigned depth{}; bool rejected{};
    std::array<Frame,DepthCap> frames{};
};
struct Scope { FiberContext* context{}; unsigned index{}; std::uint64_t serial{}; };
struct StackSample { DWORD thread{}; bool fiber{}; uintptr_t key{},low{},high{},current{},value{}; };
enum class GuardReason : unsigned { None, NoIndex, FlsGet, FlsSet, PoolFull, InvalidValue,
    InvalidBounds, OutsideStack, WrongThread, ChangedStack, ChangedFiberKey, Rejected,
    ParentAnchor, ReturnContext, ReturnOrder, Depth, PoolBusy };
struct GuardReceipt { GuardReason reason{}; StackSample sample{}; DWORD error{}; };
std::array<FiberContext,ContextCap> g_contexts{};
std::atomic<unsigned> g_contextCount{},g_guardState{};
std::atomic<std::uint64_t> g_scopeSerial{},g_guardCount{};
GuardReceipt g_firstGuard{};
DWORD g_fls=FLS_OUT_OF_INDEXES;
bool g_storageReady{};
thread_local bool g_storageBroken{},g_guardThreadSeen{};
struct StorageOps {
    DWORD(WINAPI* allocate)(PFLS_CALLBACK_FUNCTION)=&FlsAlloc;
    PVOID(WINAPI* get)(DWORD)=&FlsGetValue;
    BOOL(WINAPI* set)(DWORD,PVOID)=&FlsSetValue;
};
StorageOps g_storageOps{};
StackSample SampleStack() {
    ULONG_PTR low=0,high=0;GetCurrentThreadStackLimits(&low,&high);
    StackSample s{};s.thread=GetCurrentThreadId();s.fiber=IsThreadAFiber()!=FALSE;
    s.key=s.fiber?reinterpret_cast<uintptr_t>(GetCurrentFiber()):0;
    s.low=low;s.high=high;s.current=reinterpret_cast<uintptr_t>(_AddressOfReturnAddress());return s;
}
void Guard(GuardReason reason,const StackSample& s,DWORD error=0,FiberContext* context=nullptr) {
    g_guardCount.fetch_add(1);
    unsigned empty=0;
    if(g_guardState.compare_exchange_strong(empty,1)) {
        g_firstGuard={reason,s,error};g_guardState.store(2,std::memory_order_release);
    }
    if(!g_guardThreadSeen){g_guardThreadSeen=true;g_fiberThreads.fetch_add(1);}
    if(context){if(context->depth)g_scopeAbandoned.fetch_add(1);context->depth=0;context->rejected=true;}
}
bool PoolPointer(uintptr_t p) {
    const auto first=reinterpret_cast<uintptr_t>(g_contexts.data());
    return p>=first && p<first+sizeof(g_contexts) && (p-first)%sizeof(FiberContext)==0 &&
        (p-first)/sizeof(FiberContext)<g_contextCount.load(std::memory_order_acquire);
}
FiberContext* ContextFor() {
    auto s=SampleStack();
    if(g_storageBroken || g_fls==FLS_OUT_OF_INDEXES){Guard(GuardReason::NoIndex,s);return nullptr;}
    SetLastError(ERROR_SUCCESS);auto* raw=g_storageOps.get(g_fls);const auto error=GetLastError();
    s.value=reinterpret_cast<uintptr_t>(raw);
    // An untouched Windows fiber can return NULL/error before its FLS array exists.
    // NULL is only admitted after SetValue AND exact GetValue readback below; no TLS fallback.
    (void)error;
    // Validate pointer numerically before dereferencing any caller context.
    if(raw && !PoolPointer(s.value)){g_storageBroken=true;Guard(GuardReason::InvalidValue,s);return nullptr;}
    auto* context=static_cast<FiberContext*>(raw);
    if(s.high<=s.low){Guard(GuardReason::InvalidBounds,s,0,context);return nullptr;}
    if(s.current<s.low || s.current>=s.high){Guard(GuardReason::OutsideStack,s,0,context);return nullptr;}
    if(!context) {
        auto n=g_contextCount.load();
        if(n>=ContextCap){Guard(GuardReason::PoolFull,s);return nullptr;}
        if(!g_contextCount.compare_exchange_strong(n,n+1)){Guard(GuardReason::PoolBusy,s);return nullptr;}
        context=&g_contexts[n];context->thread=s.thread;context->key=s.key;context->stackHigh=s.high;
        if(!g_storageOps.set(g_fls,context)){g_storageBroken=true;Guard(GuardReason::FlsSet,s,GetLastError(),context);return nullptr;}
        SetLastError(ERROR_SUCCESS);auto* check=g_storageOps.get(g_fls);const auto checkError=GetLastError();
        if(check!=context){g_storageBroken=true;Guard(GuardReason::FlsGet,s,checkError,context);return nullptr;}
        s.value=reinterpret_cast<uintptr_t>(context);
    }
    if(context->thread!=s.thread){Guard(GuardReason::WrongThread,s);return nullptr;}
    if(context->rejected){Guard(GuardReason::Rejected,s);return nullptr;}
    if(context->stackHigh!=s.high){Guard(GuardReason::ChangedStack,s,0,context);return nullptr;}
    // ConvertThreadToFiber/ConvertFiberToThread may preserve the slot on the SAME stack.
    // Different nonzero keys are never merged, even if an address is reused.
    if(context->key && s.key && context->key!=s.key){Guard(GuardReason::ChangedFiberKey,s,0,context);return nullptr;}
    context->key=s.key;
    return context;
}
Event* Parent(FiberContext* c,uintptr_t anchor) {
    if(!c || !c->depth)return nullptr;
    auto& frame=c->frames[c->depth-1];
    if(frame.anchor<=anchor){Guard(GuardReason::ParentAnchor,SampleStack(),0,c);return nullptr;}
    return frame.visible?&frame.event:nullptr;
}
Scope PushScope(const Event* event,uintptr_t anchor) {
    auto* c=ContextFor();if(!c)return {};
    auto s=SampleStack();
    if(anchor<s.low || anchor>=s.high || (c->depth && c->frames[c->depth-1].anchor<=anchor)) {
        Guard(GuardReason::ParentAnchor,s,0,c);return {};
    }
    if(c->depth>=DepthCap){g_dropped.fetch_add(1);Guard(GuardReason::Depth,s,0,c);return {};}
    const auto i=c->depth++;auto& f=c->frames[i];f={};f.serial=g_scopeSerial.fetch_add(1)+1;
    f.anchor=anchor;f.visible=event!=nullptr;if(event)f.event=*event;
    return {c,i,f.serial};
}
bool RestoreScope(const Scope& scope) {
    auto* c=ContextFor();if(!c)return false;
    if(!scope.context || c!=scope.context){Guard(GuardReason::ReturnContext,SampleStack(),0,c);return false;}
    if(c->depth!=scope.index+1 || c->frames[scope.index].serial!=scope.serial) {
        Guard(GuardReason::ReturnOrder,SampleStack(),0,c);return false;
    }
    c->frames[scope.index]={};--c->depth;return true;
}
static_assert(std::is_trivially_copyable_v<Event> && std::is_trivially_destructible_v<Event>);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

bool Read(uintptr_t p,void* out,std::size_t n) {
    if(p<0x10000 || p>=0x0000800000000000ULL || n>0x0000800000000000ULL-p)return false;
    auto at=p;
    while(at<p+n) {
        MEMORY_BASIC_INFORMATION m{};
        if(!VirtualQuery(reinterpret_cast<void*>(at),&m,sizeof(m)) || m.State!=MEM_COMMIT ||
            (m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
        const auto end=reinterpret_cast<uintptr_t>(m.BaseAddress)+m.RegionSize;
        if(end<=at)return false;at=end;
    }
    __try { std::memcpy(out,reinterpret_cast<void*>(p),n);return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template<class T> bool Read(uintptr_t p,T& out) { return Read(p,&out,sizeof(out)); }
bool SameFiber(const Event& e) {
    const bool fiber=IsThreadAFiber()!=FALSE;
    return e.threadId==GetCurrentThreadId() && e.isFiber==fiber &&
        (!fiber || e.fiber==reinterpret_cast<uintptr_t>(GetCurrentFiber()));
}
void Publish(const Event& e) {
    if(!TryAcquireSRWLockExclusive(&g_queueLock)){g_dropped.fetch_add(1);return;}
    if(g_count==QueueCap)g_dropped.fetch_add(1);
    else {g_queue[(g_read+g_count)%QueueCap]=e;++g_count;g_published.fetch_add(1);}
    ReleaseSRWLockExclusive(&g_queueLock);
}
void CaptureActor(uintptr_t p,Actor& a) {
    a.pointer=p;
    if(Read(p,a.handlerHandle))a.readMask|=1;
    if(Read(p+0x918,a.object) && a.object && Read(a.object,a.objectId) && Read(a.object+4,a.type))a.readMask|=2;
    if(Read(p+0x9E8,a.controller) && Read(p+0x9F0,a.record))a.readMask|=4;
    if(Read(p+0x120,a.flags120))a.readMask|=8;
    if(a.record && Read(a.record+0x1E,a.recordId) && Read(a.record+0x1C,a.mode) && Read(a.record+0x30,a.stage))a.readMask|=16;
    uintptr_t object=0,controller=0,record=0;
    std::uint32_t id=0,handle=0;
    a.repeatedMetadataEqual=(a.readMask&7)==7 && Read(p,handle) && handle==a.handlerHandle &&
        Read(p+0x918,object) && object==a.object && Read(object,id) && id==a.objectId &&
        Read(p+0x9E8,controller) && controller==a.controller && Read(p+0x9F0,record) && record==a.record;
}
void CaptureWrapper(Wrapper& out) {
    spawncontroller::NativeConstructionLineage value;
    if(!spawncontroller::CopyNativeConstructionLineage(value))return;
    out.present=true;out.serial=value.serial;out.coverage=value.coverage;out.sequence=value.wrapperSequence;
    out.controller=value.controller;out.record=value.record;out.depth=value.depth;
    out.ambiguous=value.ambiguous || value.overflow || !value.candidateThreadParent;
    // Exactly an existing TLS receipt, never fiber ancestry or a birth identity.
}
bool Begin(Event& e,Kind kind,uintptr_t caller,uintptr_t anchor) {
    auto* context=ContextFor();
    if(!context || !g_recording.load(std::memory_order_acquire))return false;
    e.sequence=g_serial.fetch_add(1)+1;
    if(!e.sequence){g_recording=false;g_dropped.fetch_add(1);return false;}
    e.kind=kind;e.caller=caller;e.threadId=GetCurrentThreadId();e.tickMs=GetTickCount64();
    e.isFiber=IsThreadAFiber()!=FALSE;e.fiber=e.isFiber?reinterpret_cast<uintptr_t>(GetCurrentFiber()):0;
    e.generation=g_generation.load();e.installedMask=g_installed.load();e.droppedAtEntry=g_dropped.load();
    e.ownerThread=spawncontroller::IsDiagnosticGameThread();
    if(!e.ownerThread)g_foreign.fetch_add(1);
    if(auto* parent=Parent(context,anchor);parent && SameFiber(*parent)) {
        e.parent=parent->sequence;e.depth=parent->depth+1;
    }
    if(context->rejected)return false;
    if(e.depth>=DepthCap){g_dropped.fetch_add(1);return false;}
    if(e.ownerThread)e.stampAvailable=spawncontroller::CaptureDiagnosticStamp(g_base,e.stamp);
    return true;
}
int Fault(Event* e,DWORD code) {e->exceptionCode=code;g_exceptions.fetch_add(1);return EXCEPTION_CONTINUE_SEARCH;}
uintptr_t CallFactory(Event* e,std::uint32_t id,const float* p,float yaw) {
    __try {return g_factory(id,p,yaw);}
    __except(Fault(e,GetExceptionCode())) {return 0;} // CONTINUE_SEARCH: never handles native exception
}
uintptr_t CallPair(Event* e,PairFn original,void* a,void* b) {
    __try {return original(a,b);}
    __except(Fault(e,GetExceptionCode())) {return 0;}
}
void Finish(Event& e,bool returned,bool restored) {
    e.phase=Phase::Exit;e.originalReturned=returned;e.unwound=!returned;
    e.fiberExcludedDuringCall=!restored;
    e.retiredDuringCall=!g_recording.load() || e.generation!=g_generation.load();
    if(!returned)g_unwound.fetch_add(1);
    // Only our POD from here. NEVER actor/header/domain reads after original.
    Publish(e);
}
uintptr_t UnobservedFactory(std::uint32_t id,const float* p,float yaw,DWORD error,uintptr_t anchor) {
    const auto scope=PushScope(nullptr,anchor);
    if(!scope.context){g_fiberBypassed.fetch_add(1);SetLastError(error);return g_factory(id,p,yaw);}
    uintptr_t result=0;
    __try {SetLastError(error);result=g_factory(id,p,yaw);}
    __finally {const auto nativeError=GetLastError();RestoreScope(scope);SetLastError(nativeError);}
    return result;
}
uintptr_t UnobservedPair(PairFn original,void* a,void* b,DWORD error,uintptr_t anchor) {
    const auto scope=PushScope(nullptr,anchor);
    if(!scope.context){g_fiberBypassed.fetch_add(1);SetLastError(error);return original(a,b);}
    uintptr_t result=0;
    __try {SetLastError(error);result=original(a,b);}
    __finally {const auto nativeError=GetLastError();RestoreScope(scope);SetLastError(nativeError);}
    return result;
}
__declspec(noinline) uintptr_t __fastcall Factory(std::uint32_t id,const float* p,float yaw) {
    const auto error=GetLastError();const auto anchor=reinterpret_cast<uintptr_t>(_AddressOfReturnAddress());Event e{};
    if(!Begin(e,Kind::Factory,reinterpret_cast<uintptr_t>(_ReturnAddress()),anchor)) {return UnobservedFactory(id,p,yaw,error,anchor);}
    e.rawObjectId=id;e.pointPointer=reinterpret_cast<uintptr_t>(p);e.yaw=yaw;
    if(e.ownerThread) {
        e.pointAvailable=Read(e.pointPointer,e.point.data(),sizeof(e.point));
        e.pointFinite=e.pointAvailable && std::isfinite(yaw);
        for(const auto v:e.point)e.pointFinite=e.pointFinite && std::isfinite(v);
        CaptureWrapper(e.wrapper);
    }
    const auto scope=PushScope(&e,anchor);if(!scope.context){g_dropped.fetch_add(1);SetLastError(error);return g_factory(id,p,yaw);}
    Publish(e);bool returned=false;uintptr_t result=0;
    __try {SetLastError(error);result=CallFactory(&e,id,p,yaw);e.result=result;returned=true;}
    __finally {const auto nativeError=GetLastError();const auto restored=RestoreScope(scope);Finish(e,returned,restored);SetLastError(nativeError);}
    return result;
}
__declspec(noinline) uintptr_t __fastcall Destructor(void* handler,void* actor) {
    const auto error=GetLastError();const auto anchor=reinterpret_cast<uintptr_t>(_AddressOfReturnAddress());Event e{};
    if(!Begin(e,Kind::SelectedDestructor,reinterpret_cast<uintptr_t>(_ReturnAddress()),anchor)) {return UnobservedPair(g_destructor,handler,actor,error,anchor);}
    e.handler=reinterpret_cast<uintptr_t>(handler);e.actor=reinterpret_cast<uintptr_t>(actor);
    if(e.ownerThread) {
        if(e.actor)CaptureActor(e.actor,e.before);
        uintptr_t table=0,slot=0;
        e.selectedHandler=e.handler==g_base+0x7528E8 && Read(e.handler,table) && table==g_base+0x5D2D68 &&
            Read(table,slot) && slot==g_base+0x419AB0;
    }
    const auto scope=PushScope(&e,anchor);if(!scope.context){g_dropped.fetch_add(1);SetLastError(error);return g_destructor(handler,actor);}
    Publish(e);bool returned=false;uintptr_t result=0;
    __try {SetLastError(error);result=CallPair(&e,g_destructor,handler,actor);e.result=result;returned=true;}
    __finally {const auto nativeError=GetLastError();const auto restored=RestoreScope(scope);Finish(e,returned,restored);SetLastError(nativeError);}
    return result;
}
__declspec(noinline) uintptr_t __fastcall Release(void* domain,void* actor) {
    const auto error=GetLastError();const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    const auto anchor=reinterpret_cast<uintptr_t>(_AddressOfReturnAddress());
    // Validate current FLS/stack before reading retained parent POD.
    auto* context=ContextFor();
    if(!context){g_filtered.fetch_add(1);return UnobservedPair(g_release,domain,actor,error,anchor);}
    // 152570 tail-jumps here, so the actual original return PC is retained.
    auto* parent=Parent(context,anchor);
    const bool match=g_recording.load() && parent && parent->kind==Kind::SelectedDestructor &&
        parent->selectedHandler && parent->actor==reinterpret_cast<uintptr_t>(actor) && parent->actor &&
        parent->ownerThread && SameFiber(*parent) && caller==g_base+0x419ACD;
    if(!match){g_filtered.fetch_add(1);return UnobservedPair(g_release,domain,actor,error,anchor);}
    Event e{};
    if(!Begin(e,Kind::Allocator,caller,anchor)){return UnobservedPair(g_release,domain,actor,error,anchor);}
    e.actor=reinterpret_cast<uintptr_t>(actor);e.domain=reinterpret_cast<uintptr_t>(domain);
    e.scopeMatched=true;e.before=parent->before; // pre-destructor receipt, not a new allocator-time read
    uintptr_t again=0,tableAgain=0,slotAgain=0;
    e.domainAvailable=Read(g_base+0x9BA920,e.currentDomain) && Read(e.domain,e.vtable) && Read(e.vtable+0x10,e.slot) &&
        Read(g_base+0x9BA920,again) && again==e.currentDomain && Read(e.domain,tableAgain) && tableAgain==e.vtable &&
        Read(e.vtable+0x10,slotAgain) && slotAgain==e.slot;
    e.domainMatches=e.domainAvailable && e.currentDomain==e.domain && e.vtable==g_base+0x5B2BB0 && e.slot==g_base+0x19C470;
    const auto scope=PushScope(&e,anchor);if(!scope.context){g_dropped.fetch_add(1);SetLastError(error);return g_release(domain,actor);}
    Publish(e);bool returned=false;uintptr_t result=0;
    __try {SetLastError(error);result=CallPair(&e,g_release,domain,actor);e.result=result;returned=true;}
    __finally {const auto nativeError=GetLastError();const auto restored=RestoreScope(scope);Finish(e,returned,restored);SetLastError(nativeError);}
    return result;
}

// Same permanent retention pattern as NativeResourceTrace. Installation only.
using Alloc2Fn=PVOID(WINAPI*)(HANDLE,PVOID,SIZE_T,ULONG,ULONG,MEM_EXTENDED_PARAMETER*,ULONG);
std::uint8_t* AllocateMetadata(uintptr_t t) {
    const auto proc=GetProcAddress(GetModuleHandleW(L"KernelBase.dll"),"VirtualAlloc2");
    Alloc2Fn alloc=nullptr;std::memcpy(&alloc,&proc,sizeof(alloc));if(!alloc)return nullptr;
    SYSTEM_INFO info{};GetSystemInfo(&info);const auto gran=static_cast<uintptr_t>(info.dwAllocationGranularity);
    if(t>UINTPTR_MAX-UINT32_MAX)return nullptr;
    auto high=t+UINT32_MAX;const auto maximum=reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress);
    if(high>maximum)high=maximum;high=((high+1)/gran)*gran-1;
    const auto low=((t+gran-1)/gran)*gran;if(high<low || high-low<4095)return nullptr;
    MEM_ADDRESS_REQUIREMENTS req{reinterpret_cast<void*>(low),reinterpret_cast<void*>(high),0};
    MEM_EXTENDED_PARAMETER param{};param.Type=MemExtendedParameterAddressRequirements;param.Pointer=&req;
    return static_cast<std::uint8_t*>(alloc(nullptr,nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE,&param,1));
}
bool PinModule() {
    HMODULE module=nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&Initialize),&module)!=FALSE;
}
bool PrepareStorage(const StorageOps& ops=StorageOps{}) {
    if(g_fls!=FLS_OUT_OF_INDEXES)return g_storageReady;
    if(!PinModule())return false;
    g_retained=true; // module + any successful FLS allocation are process-lifetime dependencies
    const auto slot=ops.allocate(nullptr);
    if(slot==FLS_OUT_OF_INDEXES){Guard(GuardReason::NoIndex,SampleStack(),GetLastError());return false;}
    g_storageOps=ops;g_fls=slot;
    g_storageReady=ContextFor()!=nullptr;return g_storageReady; // verifies Get/Set before exposure
}
bool RegisterTable(RUNTIME_FUNCTION* table,DWORD64 base) {return RtlAddFunctionTable(table,1,base)!=FALSE;}
struct InstallOps { // actual install services; private controls may fail these before exposure
    bool(*pin)()=&PinModule;
    std::uint8_t*(*allocate)(uintptr_t)=&AllocateMetadata;
    bool(*registerTable)(RUNTIME_FUNCTION*,DWORD64)=&RegisterTable;
    MH_STATUS(WINAPI*enable)(LPVOID)=&MH_EnableHook;
};
bool PrepareHook(unsigned index,void* target,const InstallOps& ops) {
    if(!ops.pin())return false;
    auto& h=g_hooks[index];h.target=target;
    void* detours[]{reinterpret_cast<void*>(&Factory),reinterpret_cast<void*>(&Destructor),reinterpret_cast<void*>(&Release)};
    void* original=nullptr;
    if(MH_CreateHook(target,detours[index],&original)!=MH_OK)return false;
    h.trampoline=static_cast<std::uint8_t*>(original);
    // From this point all dependencies are retained, even on reported failure.
    // No uncertain rollback and no global MinHook uninitialize after this latch.
    g_retained=true;
    const auto t=reinterpret_cast<uintptr_t>(original),dst=reinterpret_cast<uintptr_t>(target);
    std::array<std::uint8_t,47> actual{};if(!Read(t,actual.data(),index==1?47:33))return false;
    const std::uint8_t jump[]{0xFF,0x25,0,0,0,0};uintptr_t continuation=0,relay=0;
    unsigned relayOffset=19;
    if(index==1) {
        const std::uint8_t prefix[]{0x48,0x85,0xD2,0x75,0x0E,0xFF,0x25,0,0,0,0};uintptr_t nullTarget=0;
        std::memcpy(&nullTarget,actual.data()+11,8);std::memcpy(&continuation,actual.data()+25,8);relayOffset=33;
        if(std::memcmp(actual.data(),prefix,sizeof(prefix)) || nullTarget!=dst+0x22 ||
            std::memcmp(actual.data()+19,jump,6))return false;
    } else {
        const std::uint8_t prefix[]{0x48,0x89,0x5C,0x24,static_cast<std::uint8_t>(index==0?0x10:8)};
        if(std::memcmp(actual.data(),prefix,5) || std::memcmp(actual.data()+5,jump,6))return false;
        std::memcpy(&continuation,actual.data()+11,8);
    }
    std::memcpy(&relay,actual.data()+relayOffset+6,8);
    if(continuation!=dst+5 || std::memcmp(actual.data()+relayOffset,jump,6) || relay!=reinterpret_cast<uintptr_t>(detours[index]))return false;
    h.metadata=ops.allocate(t);const auto u=reinterpret_cast<uintptr_t>(h.metadata);
    if(!u || u<t || u-t>UINT32_MAX || u%4)return false;
    // Avoid FF25 epilogue interpretation after a relocated save. R11 is dead
    // at all three inspected native entries. Relay bytes remain untouched.
    auto expected=actual;expected[5]=0x49;expected[6]=0xBB;std::memcpy(expected.data()+7,&continuation,8);
    expected[15]=0x41;expected[16]=0xFF;expected[17]=0xE3;
    unsigned end=18;
    if(index==1) {expected[3]=0x74;expected[4]=0x0D;expected[18]=0xC3;end=19;for(unsigned i=19;i<33;++i)expected[i]=0x90;}
    else expected[18]=0x90;
    std::memcpy(h.trampoline,expected.data(),relayOffset);
    if(!FlushInstructionCache(GetCurrentProcess(),original,relayOffset) ||
        !Read(t,actual.data(),relayOffset+14) || std::memcmp(actual.data(),expected.data(),relayOffset+14))return false;
    if(index==1) {const std::uint8_t unwind[]{1,0,0,0};std::memcpy(h.metadata,unwind,4);}
    else {const std::uint8_t unwind[]{1,5,2,0,5,0x34,static_cast<std::uint8_t>(index==0?2:1),0};std::memcpy(h.metadata,unwind,8);}
    auto* table=reinterpret_cast<RUNTIME_FUNCTION*>(h.metadata+16);*table={0,end,static_cast<DWORD>(u-t)};
    if(!ops.registerTable(table,t))return false;
    if(index==0)g_factory=reinterpret_cast<FactoryFn>(original);
    else if(index==1)g_destructor=reinterpret_cast<PairFn>(original);
    else g_release=reinterpret_cast<PairFn>(original);
    // Pointers and retained metadata must exist before enable can expose entry.
    if(ops.enable(target)!=MH_OK)return false;
    g_installed.fetch_or(1u<<index);return true;
}
bool Verify(unsigned i,uintptr_t base) {
    const auto& pin=pins::Bodies[i];std::array<std::uint8_t,2048> bytes{};
    if(pin.size>bytes.size() || !Read(base+Rvas[i],bytes.data(),pin.size) || std::memcmp(bytes.data(),pin.bytes,pin.size))return false;
    DWORD64 image=0;const auto* f=RtlLookupFunctionEntry(base+Rvas[i],&image,nullptr);
    if(!f || image!=base || f->BeginAddress!=Rvas[i] || f->EndAddress!=pin.end || f->UnwindData!=pin.unwindRva)return false;
    std::array<std::uint8_t,64> unwind{};
    return pin.unwindSize<=unwind.size() && Read(base+pin.unwindRva,unwind.data(),pin.unwindSize) &&
        !std::memcmp(unwind.data(),pin.unwind,pin.unwindSize);
}
} // namespace

bool Initialize(uintptr_t base) {
    const auto error=GetLastError();char setting[2]{};
    const bool requested=GetEnvironmentVariableA("KH2COOP_LIFETIME_TRACE",setting,2)==1 && setting[0]=='1';
    if(!requested){SetLastError(error);return true;}
    g_requested=true;
    if(g_attempted.exchange(true)){SetLastError(error);return false;}
    g_base=base;
    bool image=false;IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS64 nt{};
    if(base && Read(base,dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>0 && dos.e_lfanew<0x100000 &&
        Read(base+static_cast<uintptr_t>(dos.e_lfanew),nt)) image=nt.Signature==IMAGE_NT_SIGNATURE &&
        nt.FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64 && nt.OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
        nt.FileHeader.TimeDateStamp==pins::TimeDateStamp && nt.OptionalHeader.SizeOfImage==pins::SizeOfImage;
    for(unsigned i=0;i<3;++i) {
        if(image && Verify(i,base))g_verified.fetch_or(1u<<i);
        else g_failed.fetch_or(1u<<i);
    }
    if(g_verified.load() && !PrepareStorage()){g_failed.fetch_or(7);SetLastError(error);return false;}
    for(unsigned i=0;i<3;++i)if((g_verified.load()&(1u<<i)) && !PrepareHook(i,reinterpret_cast<void*>(base+Rvas[i]),InstallOps{}))g_failed.fetch_or(1u<<i);
    g_recording=g_installed.load()!=0; // Partial coverage is explicitly emitted, never complete.
    const bool complete=g_installed.load()==7;SetLastError(error);return complete;
}
void StopRecording() {const auto error=GetLastError();g_recording=false;g_generation.fetch_add(1);SetLastError(error);}
bool RetainsMinHookResources() {return g_retained.load();}
Statistics GetStatistics() {return {g_verified.load(),g_installed.load(),g_failed.load(),g_serial.load(),g_published.load(),
    g_dropped.load(),g_foreign.load(),g_filtered.load(),g_unwound.load(),g_exceptions.load(),g_fiberBypassed.load(),g_fiberThreads.load(),g_scopeAbandoned.load(),g_requested.load(),g_recording.load(),g_retained.load()};}
bool Pop(Event& out) {
    const auto error=GetLastError();bool have=false;
    if(spawncontroller::IsDiagnosticGameThread() && TryAcquireSRWLockExclusive(&g_queueLock)) {
        if(g_count){out=g_queue[g_read];g_read=(g_read+1)%QueueCap;--g_count;have=true;}
        ReleaseSRWLockExclusive(&g_queueLock);
    }
    SetLastError(error);return have;
}
void Drain(spawncontroller::LogFn log,unsigned maximum) {
    if(!log || !spawncontroller::IsDiagnosticGameThread() || !g_requested.load())return;
    const auto error=GetLastError();Event e{};if(maximum>8)maximum=8;
    for(unsigned i=0;i<maximum && Pop(e);++i) {
        log("[lifetimetrace] seq=%llu parent=%llu kind=%u phase=%u tid=%u owner=%u fiber=%llX depth=%u gen=%llu coverage=%u droppedBefore=%llu tick=%llu caller=%llX actor=%llX handler=%llX rawId=%u pointPtr=%llX pointRead=%u finite=%u point=%.9g,%.9g,%.9g,%.9g yaw=%.9g result=%llX selected=%u scope=%u domain=%llX currentDomain=%llX vtable=%llX slot=%llX domainRead=%u domainMatch=%u returned=%u unwind=%u exception=%08X retired=%u fiberExcludedDuringCall=%u ancestryProven=0",
            e.sequence,e.parent,static_cast<unsigned>(e.kind),static_cast<unsigned>(e.phase),e.threadId,e.ownerThread,e.fiber,e.depth,e.generation,e.installedMask,e.droppedAtEntry,e.tickMs,e.caller,e.actor,e.handler,e.rawObjectId,e.pointPointer,e.pointAvailable,e.pointFinite,e.point[0],e.point[1],e.point[2],e.point[3],e.yaw,e.result,e.selectedHandler,e.scopeMatched,e.domain,e.currentDomain,e.vtable,e.slot,e.domainAvailable,e.domainMatches,e.originalReturned,e.unwound,e.exceptionCode,e.retiredDuringCall,e.fiberExcludedDuringCall);
        log("[lifetimetrace] metadata seq=%llu phase=%u mask=%u repeated=%u obj=%llX id=%u type=%u handle=%08X flags120=%08X controller=%llX record=%llX recordId=%u mode=%u stage=%u wrapperPresent=%u wrapperSerial=%llu wrapperCoverage=%llu wrapperSeq=%llu wrapperController=%llX wrapperRecord=%llX wrapperDepth=%u wrapperAmbiguous=%u stampRead=%u transition=%u load=%u now=%u,%u,%u,%u,%u,%u,%u,%u,%u,%u",
            e.sequence,static_cast<unsigned>(e.phase),e.before.readMask,e.before.repeatedMetadataEqual,e.before.object,e.before.objectId,e.before.type,e.before.handlerHandle,e.before.flags120,e.before.controller,e.before.record,e.before.recordId,e.before.mode,e.before.stage,e.wrapper.present,e.wrapper.serial,e.wrapper.coverage,e.wrapper.sequence,e.wrapper.controller,e.wrapper.record,e.wrapper.depth,e.wrapper.ambiguous,e.stampAvailable,e.stamp.transition,e.stamp.load,e.stamp.location[0],e.stamp.location[1],e.stamp.location[2],e.stamp.location[3],e.stamp.location[4],e.stamp.location[5],e.stamp.location[6],e.stamp.location[7],e.stamp.location[8],e.stamp.location[9]);
    }
    static bool firstGuardLogged=false;
    if(!firstGuardLogged && g_guardState.load(std::memory_order_acquire)==2) {
        firstGuardLogged=true;const auto& r=g_firstGuard;const auto& s=r.sample;
        log("[lifetimetrace] firstGuard reason=%u tid=%u fiberFlag=%u fiberKey=%llX stackLow=%llX stackHigh=%llX currentSlot=%llX flsValue=%llX flsIndex=%u error=%u contexts=%u ancestryProven=0",
            static_cast<unsigned>(r.reason),s.thread,s.fiber,s.key,s.low,s.high,s.current,s.value,g_fls,r.error,g_contextCount.load());
    }
    static std::uint64_t lastEntered=UINT64_MAX,lastDropped=UINT64_MAX,lastFiber=UINT64_MAX;
    const auto s=GetStatistics();
    if(s.entered!=lastEntered || s.dropped!=lastDropped || s.fiberBypassed!=lastFiber) {lastEntered=s.entered;lastDropped=s.dropped;lastFiber=s.fiberBypassed;
        log("[lifetimetrace] stats verified=%u installed=%u failed=%u entered=%llu published=%llu dropped=%llu foreign=%llu filtered=%llu unwound=%llu exceptions=%llu fiberBypassed=%llu fiberThreads=%llu scopeAbandoned=%llu recording=%u retained=%u guardCount=%llu contexts=%u flsIndex=%u",s.verified,s.installed,s.failed,s.entered,s.published,s.dropped,s.foreign,s.filtered,s.unwound,s.exceptions,s.fiberBypassed,s.fiberThreads,s.scopeAbandoned,s.recording,s.retained,g_guardCount.load(),g_contextCount.load(),g_fls);}
    SetLastError(error);
}
} // namespace kh2coop::inject::lifetimetrace
