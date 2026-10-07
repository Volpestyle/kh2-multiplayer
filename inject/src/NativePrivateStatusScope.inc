// Permanent FLS POD ownership copied from the reviewed NativeLifetimeTrace implementation.
constexpr unsigned ContextCap=128;
struct Frame { Selection event{}; std::uint64_t serial{}; uintptr_t anchor{}; bool visible{}; };
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
    g_guardCount.fetch_add(1);g_ready=false;g_faults.fetch_add(1);
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
Selection* Parent(FiberContext* c,uintptr_t anchor) {
    if(!c || !c->depth)return nullptr;
    auto& frame=c->frames[c->depth-1];
    if(frame.anchor<=anchor){Guard(GuardReason::ParentAnchor,SampleStack(),0,c);return nullptr;}
    return frame.visible?&frame.event:nullptr;
}
Scope PushScope(const Selection* event,uintptr_t anchor) {
    auto* c=ContextFor();if(!c)return {};
    auto s=SampleStack();
    if(anchor<s.low || anchor>=s.high || (c->depth && c->frames[c->depth-1].anchor<=anchor)) {
        Guard(GuardReason::ParentAnchor,s,0,c);return {};
    }
    if(c->depth>=DepthCap){g_faults.fetch_add(1);Guard(GuardReason::Depth,s,0,c);return {};}
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
