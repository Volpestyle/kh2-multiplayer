#include "NativeResourceTrace.hpp"
#include "NativeSpawnController.hpp"
#include "NativeTraceFiber.hpp"
#include <Windows.h>
#include <MinHook.h>
#include <intrin.h>
#include <atomic>
#include <cstring>
#include <limits>
#include <type_traits>

namespace kh2coop::inject::resourcetrace {
namespace {
constexpr uintptr_t TargetRva=0x107240;
// Saved PE SHA256 9002B2DE6A1F91A790BD0673DE125D1CF833F7942BFEC827CDCF6BA64D5849ED.
// Exact [107240,107618) body SHA256 008B33D87F1AAC9D4E0070AC8A513876A5FD9BE9B156C341B3550E8A4036A40E.
constexpr std::array<std::uint8_t,984> kBody { 
    0x40,0x53,0x55,0x56,0x57,0x41,0x57,0x48,0x81,0xEC,0x90,0x01,0x00,0x00,0x48,0x8B,
    0x05,0xA3,0x1F,0x65,0x00,0x48,0x33,0xC4,0x48,0x89,0x84,0x24,0x70,0x01,0x00,0x00,
    0x48,0x83,0x79,0x08,0x00,0x49,0x8B,0xF8,0x48,0x8B,0xF2,0x48,0x8B,0xD9,0x75,0x60,
    0x48,0xBD,0xB3,0x94,0xD6,0x26,0xE8,0x0B,0x2E,0x11,0x66,0x0F,0x1F,0x44,0x00,0x00,
    0xE8,0x61,0x3A,0x33,0x00,0x48,0x05,0x10,0x27,0x00,0x00,0x48,0x6B,0xC8,0x64,0x48,
    0x8B,0xC5,0x48,0xF7,0xE9,0x48,0xC1,0xFA,0x1A,0x48,0x8B,0xC2,0x48,0xC1,0xE8,0x3F,
    0x48,0x03,0xD0,0x69,0xC2,0x00,0xCA,0x9A,0x3B,0x48,0x89,0x54,0x24,0x20,0x2B,0xC8,
    0x89,0x4C,0x24,0x28,0x48,0x8D,0x4C,0x24,0x40,0x0F,0x28,0x44,0x24,0x20,0x66,0x0F,
    0x7F,0x44,0x24,0x40,0xE8,0x23,0x3A,0x33,0x00,0x48,0x83,0x7B,0x08,0x00,0x74,0xB0,
    0x4C,0x8D,0x83,0x58,0x01,0x00,0x00,0x48,0x8B,0xC6,0x4C,0x2B,0xC6,0x0F,0x1F,0x00,
    0x0F,0xB6,0x10,0x42,0x0F,0xB6,0x0C,0x00,0x2B,0xD1,0x75,0x07,0x48,0xFF,0xC0,0x85,
    0xC9,0x75,0xED,0x4C,0x89,0xA4,0x24,0x88,0x01,0x00,0x00,0x4C,0x89,0xB4,0x24,0x80,
    0x01,0x00,0x00,0x85,0xD2,0x0F,0x84,0xDD,0x02,0x00,0x00,0x4C,0x8D,0x25,0x2E,0xD0,
    0xAC,0x02,0x48,0x85,0xFF,0x75,0x14,0x49,0x8B,0xD4,0x48,0x8B,0xCE,0xE8,0x2C,0xA7,
    0x36,0x00,0x48,0x85,0xC0,0x0F,0x84,0xC7,0x01,0x00,0x00,0x33,0xD2,0x48,0x8D,0x8B,
    0x58,0x01,0x00,0x00,0x41,0xB8,0x04,0x01,0x00,0x00,0xE8,0x2D,0xA7,0x36,0x00,0x48,
    0x8B,0xD3,0x48,0x8B,0xCE,0x48,0x2B,0xD6,0x0F,0x1F,0x84,0x00,0x00,0x00,0x00,0x00,
    0x0F,0xB6,0x01,0x88,0x84,0x0A,0x58,0x01,0x00,0x00,0x48,0x8D,0x49,0x01,0x84,0xC0,
    0x75,0xEE,0x33,0xC0,0x4C,0x8D,0xB3,0x2C,0x01,0x00,0x00,0x49,0x89,0x06,0x48,0xC7,
    0xC5,0xFF,0xFF,0xFF,0xFF,0x49,0x89,0x46,0x08,0x48,0x89,0x44,0x24,0x30,0x48,0x89,
    0x44,0x24,0x38,0x48,0x8B,0xC5,0x48,0x85,0xFF,0x75,0x15,0x0F,0x1F,0x44,0x00,0x00,
    0x48,0xFF,0xC0,0x41,0x80,0x3C,0x04,0x00,0x75,0xF6,0xEB,0x0D,0x0F,0x1F,0x40,0x00,
    0x48,0xFF,0xC0,0x80,0x3C,0x07,0x00,0x75,0xF7,0xFF,0xC0,0x48,0x8B,0xD5,0x48,0x63,
    0xC8,0x48,0x03,0xCE,0x48,0xFF,0xC2,0x80,0x3C,0x11,0x00,0x75,0xF7,0x4C,0x8D,0x44,
    0x24,0x30,0xE8,0x69,0xE5,0x03,0x00,0x44,0x8B,0x43,0x14,0x33,0xD2,0x48,0x89,0x6B,
    0x18,0x45,0x85,0xC0,0x7E,0x64,0x48,0x8B,0x4B,0x08,0x4C,0x8B,0x4C,0x24,0x38,0x4C,
    0x8B,0x54,0x24,0x30,0x0F,0x10,0x09,0x0F,0x10,0x41,0x10,0x66,0x48,0x0F,0x7E,0xC8,
    0x0F,0x11,0x4C,0x24,0x40,0x0F,0x11,0x44,0x24,0x50,0x49,0x3B,0xC2,0x75,0x07,0x4C,
    0x39,0x4C,0x24,0x48,0x74,0x0D,0xFF,0xC2,0x48,0x83,0xC1,0x20,0x41,0x3B,0xD0,0x7C,
    0xD3,0xEB,0x27,0x48,0x8B,0x54,0x24,0x50,0x45,0x33,0xC0,0x8B,0x4B,0x10,0x48,0x89,
    0x53,0x18,0xFF,0x15,0xE0,0x45,0x47,0x00,0x83,0xF8,0xFF,0x75,0x0D,0x48,0x8D,0x0D,
    0x9C,0xEB,0x47,0x00,0xFF,0x15,0xBE,0x44,0x47,0x00,0x48,0x83,0x7B,0x18,0xFF,0x0F,
    0x85,0xB4,0x00,0x00,0x00,0x33,0xD2,0x48,0x8D,0x4C,0x24,0x60,0x41,0xB8,0x04,0x01,
    0x00,0x00,0xE8,0x15,0xA6,0x36,0x00,0x48,0x8D,0x54,0x24,0x60,0x48,0x8B,0xCE,0x48,
    0x2B,0xD6,0x0F,0xB6,0x01,0x88,0x04,0x11,0x48,0x8D,0x49,0x01,0x84,0xC0,0x75,0xF2,
    0xBA,0x2F,0x00,0x00,0x00,0x48,0x8D,0x4C,0x24,0x60,0xE8,0xC9,0xA5,0x36,0x00,0x48,
    0x8B,0xF8,0x0F,0xB6,0x00,0x84,0xC0,0x74,0x1D,0x0F,0x1F,0x80,0x00,0x00,0x00,0x00,
    0x0F,0xB6,0xC8,0xFF,0x15,0x47,0x46,0x47,0x00,0x88,0x07,0x48,0x8D,0x7F,0x01,0x0F,
    0xB6,0x07,0x84,0xC0,0x75,0xEA,0x48,0x8D,0x44,0x24,0x60,0x48,0x2B,0xF0,0x66,0x90,
    0x44,0x0F,0xB6,0x00,0x0F,0xB6,0x14,0x30,0x44,0x2B,0xC2,0x75,0x07,0x48,0xFF,0xC0,
    0x85,0xD2,0x75,0xEC,0x45,0x85,0xC0,0x74,0x15,0x48,0x8B,0x03,0x48,0x8D,0x54,0x24,
    0x60,0x45,0x33,0xC0,0x48,0x8B,0xCB,0xFF,0x10,0xE9,0x0C,0x01,0x00,0x00,0x33,0xD2,
    0x48,0x8D,0x8B,0x58,0x01,0x00,0x00,0x41,0xB8,0x04,0x01,0x00,0x00,0xE8,0x7A,0xA5,
    0x36,0x00,0x32,0xC0,0xE9,0xF1,0x00,0x00,0x00,0x8B,0x4B,0x10,0x41,0xB8,0x10,0x00,
    0x00,0x00,0x49,0x8B,0xD6,0xFF,0x15,0x8D,0x45,0x47,0x00,0x83,0xBB,0x30,0x01,0x00,
    0x00,0x00,0x41,0x0F,0x10,0x06,0x0F,0x11,0x83,0x48,0x01,0x00,0x00,0x0F,0x84,0xC5,
    0x00,0x00,0x00,0x48,0x8B,0x03,0x48,0x8B,0xCB,0xFF,0x50,0x08,0x48,0x63,0x8B,0x30,
    0x01,0x00,0x00,0xBF,0x30,0x00,0x00,0x00,0x8B,0xC7,0x48,0xF7,0xE1,0x48,0x0F,0x40,
    0xC5,0x48,0x8B,0xC8,0xE8,0x3B,0x25,0x33,0x00,0x8B,0x8B,0x30,0x01,0x00,0x00,0x48,
    0x8B,0xD0,0x48,0x89,0x83,0x40,0x01,0x00,0x00,0x44,0x8D,0x04,0x49,0x8B,0x4B,0x10,
    0x41,0xC1,0xE0,0x04,0xFF,0x15,0x2E,0x45,0x47,0x00,0x41,0x8B,0x4E,0x08,0x41,0x8B,
    0x46,0x04,0x85,0xC9,0x79,0x11,0x41,0x8B,0x16,0x8D,0x0C,0x40,0x83,0xC2,0x0F,0x83,
    0xE2,0xF0,0xC1,0xE1,0x04,0xEB,0x06,0x8D,0x14,0x40,0xC1,0xE2,0x04,0x48,0x8B,0x83,
    0x40,0x01,0x00,0x00,0x03,0xD1,0x8D,0x4A,0x10,0xBA,0x01,0x00,0x00,0x00,0x89,0x48,
    0x20,0x39,0x93,0x30,0x01,0x00,0x00,0x7E,0x3F,0x0F,0x1F,0x80,0x00,0x00,0x00,0x00,
    0x48,0x8B,0x8B,0x40,0x01,0x00,0x00,0x48,0x03,0xCF,0x44,0x8B,0x41,0xFC,0x45,0x85,
    0xC0,0x79,0x0E,0x8B,0x41,0xF8,0x83,0xC0,0x0F,0x83,0xE0,0xF0,0x03,0x41,0xF0,0xEB,
    0x06,0x8B,0x41,0xF0,0x41,0x03,0xC0,0xFF,0xC2,0x89,0x41,0x20,0x48,0x83,0xC7,0x30,
    0x3B,0x93,0x30,0x01,0x00,0x00,0x7C,0xC8,0xB0,0x01,0x4C,0x8B,0xB4,0x24,0x80,0x01,
    0x00,0x00,0x4C,0x8B,0xA4,0x24,0x88,0x01,0x00,0x00,0x48,0x8B,0x8C,0x24,0x70,0x01,
    0x00,0x00,0x48,0x33,0xCC,0xE8,0x56,0x24,0x33,0x00,0x48,0x81,0xC4,0x90,0x01,0x00,
    0x00,0x41,0x5F,0x5F,0x5E,0x5D,0x5B,0xC3,
 };
using NativeFn=std::uint64_t(__fastcall*)(void*,const char*,const char*);
NativeFn g_original=nullptr;
uintptr_t g_base=0;
void* g_target=nullptr;
std::uint8_t* g_trampoline=nullptr;
std::uint8_t* g_metadata=nullptr;
RUNTIME_FUNCTION* g_table=nullptr;
HMODULE g_pinnedModule=nullptr;
std::atomic<bool> g_retained{false},g_recording{false},g_identity{false};
std::atomic<InstallStatus> g_status{InstallStatus::Disabled};
std::atomic<std::uint64_t> g_generation{1},g_serial{0};
std::atomic<std::uint64_t> g_entered{0},g_returned{0},g_unwound{0},g_dropped{0},g_unparented{0},g_foreign{0},g_published{0};
std::atomic<DWORD> g_ownerThread{0};
SRWLOCK g_queueLock=SRWLOCK_INIT;
std::array<ResourceObservation,QueueCap> g_queue{};
std::size_t g_queueRead=0,g_queueCount=0;
struct Local {
    std::array<Parent,DepthCap> parents{};
    std::array<ResourceObservation,ChildCap> rows{};
    std::uint32_t constructionDepth=0,callbackDepth=0;
    std::size_t count=0;
    std::uint64_t generation=0,outer=0,callback=0,dropped=0;
};
tracefiber::Store<Local> g_localStorage;
static_assert(std::is_trivially_copyable_v<Local> && std::is_trivially_destructible_v<Local>);
void Increment(std::atomic<std::uint64_t>& value) {
    auto n=value.load(std::memory_order_relaxed);
    while(n!=UINT64_MAX && !value.compare_exchange_weak(n,n+1,std::memory_order_relaxed)){}
}
std::uint64_t Serial() {
    auto n=g_serial.load();
    while(n!=UINT64_MAX) { if(g_serial.compare_exchange_weak(n,n+1))return n+1; }
    g_recording=false;return 0;
}
bool Read(uintptr_t address,void* out,std::size_t bytes) {
    if(!address || address>UINTPTR_MAX-bytes || address<0x10000 || address+bytes>0x0000800000000000ULL)return false;
    auto at=address;
    while(at<address+bytes) {
        MEMORY_BASIC_INFORMATION region{};
        if(!VirtualQuery(reinterpret_cast<void*>(at),&region,sizeof(region)) || region.State!=MEM_COMMIT ||
            (region.Protect&(PAGE_GUARD|PAGE_NOACCESS)) || !(region.Protect&
            (PAGE_READONLY|PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))return false;
        const auto end=reinterpret_cast<uintptr_t>(region.BaseAddress)+region.RegionSize;
        if(end<=at)return false;at=end;
    }
    __try {std::memcpy(out,reinterpret_cast<void*>(address),bytes);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void String(uintptr_t ptr,StringSample& out) {
    out.pointerNonNull=ptr!=0;if(!ptr)return;
    for(std::size_t i=0;i<out.bytes.size();++i) {
        if(ptr>UINTPTR_MAX-i || !Read(ptr+i,&out.bytes[i],1))return;
        if(!out.bytes[i]){out.readable=true;out.terminated=true;out.length=static_cast<std::uint16_t>(i);return;}
        out.length=static_cast<std::uint16_t>(i+1);
    }
    out.readable=true;out.truncated=true;
}
void Sample(ResourceObservation& row) {
    row.vtableRead=Read(row.package,&row.sampledVtable,8);
    if(row.vtableRead && row.sampledVtable)row.slotRead=Read(row.sampledVtable,&row.sampledSlot0,8);
    row.countRead=Read(g_base+0x79CFD8,&row.sampledCount,4);
    row.indexRead=Read(g_base+0x79CFD4,&row.sampledIndex,4);
    row.modeRead=Read(g_base+0x716750,&row.sampledMode,4);
    if(row.indexRead && row.sampledIndex>=0 && row.sampledIndex<64)
        row.currentPackageRead=Read(g_base+0x79CFE0+static_cast<uintptr_t>(row.sampledIndex)*8,&row.sampledCurrentPackage,8);
    String(row.filename,row.filenameSample);String(row.optionalRoot,row.rootSample);
}
ResourceObservation* Enter(Local& l,uintptr_t package,uintptr_t filename,uintptr_t root,uintptr_t caller) {
    Increment(g_entered);
    if(!g_recording.load())return nullptr;
    const auto owner=g_ownerThread.load();const auto thread=GetCurrentThreadId();
    if(owner && thread!=owner)Increment(g_foreign);
    if(!l.constructionDepth || l.generation!=g_generation.load()) {Increment(g_unparented);return nullptr;}
    if(l.constructionDepth>DepthCap || l.callbackDepth>DepthCap || l.count>=ChildCap) {
        Increment(g_dropped);if(l.dropped!=UINT64_MAX)++l.dropped;return nullptr;
    }
    const auto parent=l.parents[l.constructionDepth-1];
    if(!parent.available){Increment(g_unparented);return nullptr;}
    const auto serial=Serial();if(!serial)return nullptr;
    auto& row=l.rows[l.count++];row={};
    row.invocation=serial;row.parentCallback=l.callback;row.parent=parent;
    row.generation=l.generation;row.outerBoundary=l.outer;row.depth=l.callbackDepth;row.threadId=thread;
    row.actualEnteredTarget=reinterpret_cast<uintptr_t>(g_target);row.installationIdentityVerified=g_identity.load();
    row.caller=caller;row.package=package;row.filename=filename;row.optionalRoot=root;
    if(caller==g_base+0x106A17)row.callerKind=Caller::LookupZeroRoot;
    else if(caller==g_base+0x1074D9)row.callerKind=Caller::Recursive;
    else if(caller==g_base+0x106AA6)row.callerKind=Caller::AlternateRoot;
    Sample(row);return &row;
}
__declspec(noinline) std::uint64_t __fastcall Hook(void* package,const char* filename,const char* root) {
    const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    const DWORD beforeError=GetLastError();
    auto* local=g_localStorage.Current();
    if(!local){Increment(g_dropped);SetLastError(beforeError);return g_original(package,filename,root);}
    auto& l=*local;
    if(l.callbackDepth==UINT32_MAX){g_localStorage.Reject(local);Increment(g_dropped);SetLastError(beforeError);return g_original(package,filename,root);}
    const auto previousDepth=l.callbackDepth;const auto previousCallback=l.callback;
    if(l.callbackDepth!=UINT32_MAX)++l.callbackDepth;
    ResourceObservation* const row=Enter(l,reinterpret_cast<uintptr_t>(package),reinterpret_cast<uintptr_t>(filename),reinterpret_cast<uintptr_t>(root),caller);
    const auto invocation=row?row->invocation:0;
    l.callback=invocation; // overflow/unparented calls hide an ancestor
    std::uint64_t result=0;bool normal=false;
    SetLastError(beforeError);
    __try {result=g_original(package,filename,root);normal=true;}
    __finally {
        const DWORD afterError=GetLastError();
        const bool current=g_localStorage.Same(local) && l.callbackDepth==previousDepth+1 && l.callback==invocation;
        if(current && row && row->invocation==invocation){row->normalReturn=normal;row->unwound=!normal;if(normal){row->rawRax=result;row->al=static_cast<std::uint8_t>(result);}}
        normal?Increment(g_returned):Increment(g_unwound);
        if(current){l.callbackDepth=previousDepth;l.callback=previousCallback;}else {g_localStorage.Abandon(local);Increment(g_dropped);}
        SetLastError(afterError);
    }
    return result;
}
using Alloc2Fn=PVOID(WINAPI*)(HANDLE,PVOID,SIZE_T,ULONG,ULONG,MEM_EXTENDED_PARAMETER*,ULONG);
bool Pin() {
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&Hook),&g_pinnedModule)!=FALSE;
}
std::uint8_t* Allocate(uintptr_t t) {
    const auto address=GetProcAddress(GetModuleHandleW(L"KernelBase.dll"),"VirtualAlloc2");
    Alloc2Fn alloc=nullptr;static_assert(sizeof(alloc)==sizeof(address));std::memcpy(&alloc,&address,sizeof(alloc));
    if(!alloc)return nullptr;
    SYSTEM_INFO info{};GetSystemInfo(&info);const auto gran=static_cast<uintptr_t>(info.dwAllocationGranularity);
    if(t>UINTPTR_MAX-UINT32_MAX || t>UINTPTR_MAX-gran)return nullptr;
    auto high=t+UINT32_MAX;const auto maximum=reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress);
    if(high>maximum)high=maximum;high=((high+1)/gran)*gran-1;
    const auto low=((t+gran-1)/gran)*gran;if(high<low || high-low<4095)return nullptr;
    MEM_ADDRESS_REQUIREMENTS req{reinterpret_cast<void*>(low),reinterpret_cast<void*>(high),0};
    MEM_EXTENDED_PARAMETER param{};param.Type=MemExtendedParameterAddressRequirements;param.Pointer=&req;
    return static_cast<std::uint8_t*>(alloc(nullptr,nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE,&param,1));
}
bool Register(RUNTIME_FUNCTION* table,DWORD64 base){return RtlAddFunctionTable(table,1,base)!=FALSE;}
// Install-only seams permit faithful owned-memory failure controls; callbacks
// use none of these services. Production always supplies these native functions.
struct InstallOps {
    bool(*pin)()=&Pin;
    std::uint8_t*(*allocate)(uintptr_t)=&Allocate;
    bool(*registerTable)(RUNTIME_FUNCTION*,DWORD64)=&Register;
    MH_STATUS(WINAPI*enable)(LPVOID)=&MH_EnableHook;
};
void Rollback(bool created,bool registered) {
    // Only reachable before any possible exposure. On unexpected removal
    // failure retain dependencies and prevent global MinHook teardown/restart.
    if(registered && !RtlDeleteFunctionTable(g_table)){g_retained=true;g_status=InstallStatus::RollbackRetained;return;}
    if(created && MH_RemoveHook(g_target)!=MH_OK){g_retained=true;g_status=InstallStatus::RollbackRetained;return;}
    if(g_metadata)VirtualFree(g_metadata,0,MEM_RELEASE);
    g_metadata=nullptr;g_table=nullptr;g_trampoline=nullptr;g_original=nullptr;g_target=nullptr;
}
bool InstallValidatedTarget(uintptr_t base,void* target,const InstallOps& ops) {
    g_base=base;g_target=target;
    void* trampoline=nullptr;
    if(MH_CreateHook(target,reinterpret_cast<void*>(&Hook),&trampoline)!=MH_OK){g_status=InstallStatus::CreateFailed;g_target=nullptr;return false;}
    g_trampoline=static_cast<std::uint8_t*>(trampoline);g_original=reinterpret_cast<NativeFn>(trampoline);
    std::array<std::uint8_t,33> code{};uintptr_t continuation=0,detour=0;
    const std::uint8_t prefix[]{0x40,0x53,0x55,0x56,0x57,0xFF,0x25,0,0,0,0};
    const std::uint8_t jump[]{0xFF,0x25,0,0,0,0};
    bool valid=Read(reinterpret_cast<uintptr_t>(trampoline),code.data(),code.size());
    if(valid){std::memcpy(&continuation,code.data()+11,8);std::memcpy(&detour,code.data()+25,8);}
    valid=valid && std::memcmp(code.data(),prefix,11)==0 && std::memcmp(code.data()+19,jump,6)==0 &&
        continuation==reinterpret_cast<uintptr_t>(target)+5 && detour==reinterpret_cast<uintptr_t>(&Hook);
    if(!valid){g_status=InstallStatus::TrampolineMismatch;Rollback(true,false);return false;}
    g_metadata=ops.allocate(reinterpret_cast<uintptr_t>(trampoline));
    const auto u=reinterpret_cast<uintptr_t>(g_metadata),t=reinterpret_cast<uintptr_t>(trampoline);
    if(!g_metadata || u<t || u-t>UINT32_MAX || u%4){g_status=InstallStatus::AllocationFailed;Rollback(true,false);return false;}
    g_trampoline[5]=0x49;g_trampoline[6]=0xBB;std::memcpy(g_trampoline+7,&continuation,8);
    g_trampoline[15]=0x41;g_trampoline[16]=0xFF;g_trampoline[17]=0xE3;g_trampoline[18]=0x90;
    auto expected=code;expected[5]=0x49;expected[6]=0xBB;std::memcpy(expected.data()+7,&continuation,8);
    expected[15]=0x41;expected[16]=0xFF;expected[17]=0xE3;expected[18]=0x90;
    std::array<std::uint8_t,33> actual{};
    if(!FlushInstructionCache(GetCurrentProcess(),trampoline,19) ||
        !Read(reinterpret_cast<uintptr_t>(trampoline),actual.data(),actual.size()) || actual!=expected){
        g_status=InstallStatus::TrampolineMismatch;Rollback(true,false);return false;}
    const std::uint8_t unwind[]{1,5,4,0,5,0x70,4,0x60,3,0x50,2,0x30};std::memcpy(g_metadata,unwind,12);
    g_table=reinterpret_cast<RUNTIME_FUNCTION*>(g_metadata+16);*g_table={0,18,static_cast<DWORD>(u-t)};
    if(!ops.registerTable(g_table,t)){g_status=InstallStatus::RegistrationFailed;Rollback(true,false);return false;}
    if(!ops.pin()){g_status=InstallStatus::PinFailed;Rollback(true,true);return false;}
    // Publish retention before an enable attempt can make any entry observable.
    // Even a reported enable failure can be partially exposed: never roll back.
    g_retained.store(true);
    if(ops.enable(target)!=MH_OK){g_status=InstallStatus::EnableFailedRetained;return false;}
    g_status=InstallStatus::Ready;g_recording=true;return true;
}
} // namespace

bool Initialize(uintptr_t exeBase,bool requested) {
    const auto error=GetLastError();bool result=false;
    if(g_retained.load()){g_status=InstallStatus::ReinitializationRejected;SetLastError(error);return false;}
    if(!requested){g_status=InstallStatus::Disabled;SetLastError(error);return true;}
    if(tracefiber::PrepareRequested()){g_status=InstallStatus::DiagnosticProfileRejected;SetLastError(error);return false;}
    if(!g_localStorage.Init(reinterpret_cast<const void*>(&Initialize))){g_status=InstallStatus::FiberStorageUnavailable;SetLastError(error);return false;}
    g_identity=false;
    std::array<std::uint8_t,984> bytes{};
    if(!exeBase || exeBase>UINTPTR_MAX-0x79D1E0 || !Read(exeBase+TargetRva,bytes.data(),bytes.size()))g_status=InstallStatus::IdentityUnavailable;
    else if(bytes!=kBody)g_status=InstallStatus::IdentityMismatch;
    else {g_identity=true;result=InstallValidatedTarget(exeBase,reinterpret_cast<void*>(exeBase+TargetRva),InstallOps{});}
    SetLastError(error);return result;
}
// Existing EntityHook adapter now also retains legacy FLS diagnostic dependencies.
bool RetainsMinHookResources(){return g_retained.load() || tracefiber::retained.load();}
bool RejectReinitialization(){return g_retained.load() || tracefiber::retained.load();}
void StopRecording() {
    const auto error=GetLastError();g_recording=false;
    auto n=g_generation.load();while(n!=UINT64_MAX && !g_generation.compare_exchange_weak(n,n+1)){}
    AcquireSRWLockExclusive(&g_queueLock);g_queueRead=0;g_queueCount=0;ReleaseSRWLockExclusive(&g_queueLock);
    g_status=InstallStatus::Retired;SetLastError(error);
}
ConstructionToken BeginConstruction() {
    const auto error=GetLastError();ConstructionToken token{};auto* local=g_localStorage.Current();
    if(!local){Increment(g_dropped);SetLastError(error);return token;}auto& l=*local;
    if(!g_recording.load()){SetLastError(error);return token;}
    if(!l.constructionDepth){l.count=0;l.dropped=0;l.generation=g_generation.load();l.outer=Serial();}
    token={l.generation,l.outer,l.constructionDepth,true,reinterpret_cast<uintptr_t>(local)};
    if(l.constructionDepth==UINT32_MAX){Increment(g_dropped);token.entered=false;SetLastError(error);return token;}
    ++l.constructionDepth;
    if(l.constructionDepth<=DepthCap){
        auto& parent=l.parents[l.constructionDepth-1];parent={};
        spawncontroller::NativeConstructionLineage lineage;
        if(spawncontroller::CopyNativeConstructionLineage(lineage) && lineage.captured && lineage.candidateThreadParent &&
            !lineage.overflow && !lineage.ambiguous && lineage.threadId==GetCurrentThreadId()) {
            parent.serial=lineage.serial;parent.coverage=lineage.coverage;parent.wrapperSequence=lineage.wrapperSequence;
            parent.controller=lineage.controller;parent.record=lineage.record;parent.threadId=lineage.threadId;
            parent.recordIndex=lineage.samples[0].recordIndex;parent.recordIndexAvailable=lineage.samples[0].recordIndexAvailable;
            parent.recordBytes=lineage.samples[0].recordBytes;parent.recordBytesAvailable=lineage.samples[0].recordRead;
            parent.available=true;DWORD unknown=0;g_ownerThread.compare_exchange_strong(unknown,lineage.threadId);
        }
    } else {Increment(g_dropped);if(l.dropped!=UINT64_MAX)++l.dropped;}
    SetLastError(error);return token;
}
void EndConstruction(ConstructionToken token,bool normalReturn) {
    const auto error=GetLastError();
    if(!token.entered){SetLastError(error);return;}
    auto* local=g_localStorage.Current();
    if(!local || reinterpret_cast<uintptr_t>(local)!=token.context){g_localStorage.Abandon(reinterpret_cast<const Local*>(token.context));Increment(g_dropped);SetLastError(error);return;}auto& l=*local;
    if(l.constructionDepth!=token.previousDepth+1 || token.outerBoundary!=l.outer || token.generation!=l.generation){
        Increment(g_dropped);g_localStorage.Abandon(local);SetLastError(error);return;}
    l.constructionDepth=token.previousDepth;
    if(l.constructionDepth){SetLastError(error);return;}
    AcquireSRWLockExclusive(&g_queueLock);
    if(g_recording.load() && token.generation==g_generation.load()) {
        for(std::size_t i=0;i<l.count;++i){
            if(g_queueCount==QueueCap){Increment(g_dropped);continue;}
            auto row=l.rows[i];row.outerNormalReturn=normalReturn;row.outerUnwound=!normalReturn;row.boundaryDropped=l.dropped;
            g_queue[(g_queueRead+g_queueCount)%QueueCap]=row;++g_queueCount;Increment(g_published);
        }
    }
    ReleaseSRWLockExclusive(&g_queueLock);l.count=0;SetLastError(error);
}
bool Pop(ResourceObservation& out) {
    const auto error=GetLastError();out={};AcquireSRWLockExclusive(&g_queueLock);
    const bool have=g_recording.load() && g_queueCount!=0;
    if(have){out=g_queue[g_queueRead];g_queueRead=(g_queueRead+1)%QueueCap;--g_queueCount;}
    ReleaseSRWLockExclusive(&g_queueLock);SetLastError(error);return have;
}
Statistics GetStatistics() {
    return {g_status.load(),g_recording.load(),g_retained.load(),g_pinnedModule!=nullptr,g_identity.load(),
        g_entered.load(),g_returned.load(),g_unwound.load(),g_dropped.load(),g_unparented.load(),g_foreign.load(),g_published.load(),g_generation.load(),
        g_localStorage.refused.load(),g_localStorage.firstReason.load()};
}
} // namespace kh2coop::inject::resourcetrace
