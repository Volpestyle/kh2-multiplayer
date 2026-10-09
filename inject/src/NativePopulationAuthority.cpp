#include "NativePopulationAuthority.hpp"
#include "NativePopulationAuthorityPins.hpp"
#include "NativeTraceFiber.hpp"
#include "Warp.hpp"
#include "kh2coop/KH2Offsets.hpp"
#include <Windows.h>
#include <intrin.h>
#include <cstring>

namespace kh2coop::inject::populationauthority {
namespace {
authoritytrace::Recorder<> recorder;
std::atomic<bool> requested{}, exposed{}, attempted{};
std::uintptr_t image{};
spawncontroller::LogFn logger{};
std::uint32_t frames{};
std::atomic<std::uint32_t> mask{};
bool Enabled(const char* name) {
    char value[2]{};return GetEnvironmentVariableA(name,value,2)==1 && value[0]=='1';
}
bool Read(std::uintptr_t address,void* destination,std::size_t bytes) {
    if (address<0x10000 || address>=0x800000000000ULL || bytes>0x800000000000ULL-address) return false;
    __try {std::memcpy(destination,reinterpret_cast<const void*>(address),bytes);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
template<class T> bool Read(std::uintptr_t address,T& value) {return Read(address,&value,sizeof(value));}
authoritytrace::Identity Identity() {
    authoritytrace::Identity out{};ULONG_PTR low=0,high=0;
    GetCurrentThreadStackLimits(&low,&high);
    const auto anchor=reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress());
    out.thread=GetCurrentThreadId();out.isFiber=IsThreadAFiber()!=FALSE;
    out.fiber=out.isFiber?reinterpret_cast<std::uintptr_t>(GetCurrentFiber()):0;
    out.stackHigh=high;out.valid=out.thread && high>low && anchor>=low && anchor<high;
    return out;
}
bool VerifyNativeSpans(std::uintptr_t base) {
    IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS64 nt{};
    if (!Read(base,dos) || dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<=0 || dos.e_lfanew>0x100000 ||
        !Read(base+static_cast<std::uintptr_t>(dos.e_lfanew),nt) || nt.Signature!=IMAGE_NT_SIGNATURE ||
        nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 || nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.FileHeader.TimeDateStamp!=authoritypins::TimeStamp || nt.OptionalHeader.SizeOfImage!=authoritypins::ImageSize) return false;
    std::array<std::uint8_t,2048> bytes{};
    for (const auto& pin:authoritypins::Pins) {
        if (pin.size>bytes.size() || !Read(base+pin.rva,bytes.data(),pin.size) || std::memcmp(bytes.data(),pin.body,pin.size)) return false;
        DWORD64 module=0;const auto* native=RtlLookupFunctionEntry(base+pin.rva,&module,nullptr);
        if (!pin.unwind) {if (native) return false;}
        else if (!native || module!=base || native->BeginAddress!=pin.rva || native->EndAddress!=pin.end ||
            native->UnwindData!=pin.unwind || !Read(base+pin.unwind,bytes.data(),64) || std::memcmp(bytes.data(),pin.metadata,64)) return false;
    }
    return true; // finite entry spans only; not creator-domain closure
}
authoritytrace::Snapshot Capture(std::uintptr_t controller,std::uintptr_t record,std::uintptr_t actor) {
    authoritytrace::Snapshot out{};out.controller=controller;out.record=record;out.actor=actor;
    if (actor) {
        if (Read(actor+offsets::actor::OBJENTRY_PTR,out.objectEntry)) out.validMask|=1;
        if (Read(actor+0x5C0,out.status)) out.validMask|=2;
        if (Read(actor+0x9E8,out.actorController)) out.validMask|=4;
        if (Read(actor+0x9F0,out.actorRecord)) out.validMask|=8;
        if (!controller && (out.validMask&4)) out.controller=controller=out.actorController;
        if (!record && (out.validMask&8)) out.record=record=out.actorRecord;
    }
    if (controller && Read(controller,out.controllerBytes)) {
        out.validMask|=16;
        std::memcpy(&out.header,out.controllerBytes.data()+8,sizeof(out.header));
        std::memcpy(&out.flags,out.controllerBytes.data()+4,4);
        std::memcpy(&out.currentCount,out.controllerBytes.data()+0x24,4);
        std::memcpy(&out.initialCount,out.controllerBytes.data()+0x28,4);
        if (Read(out.header,out.headerBytes)) {
            out.validMask|=32;
            std::uint16_t count=0;std::memcpy(&count,out.headerBytes.data()+4,2);
            if (count==5) for (unsigned i=0;i<5;++i)
                if (Read(out.header+44+i*64,out.definitionRecords[i])) out.validMask|=1u<<(8+i);
        }
    }
    if (record && Read(record,out.recordBytes)) {
        out.validMask|=64;std::memcpy(&out.nativeId,out.recordBytes.data()+0x1E,2);
    }
    if (spawncontroller::IsDiagnosticGameThread()) {
        out.load=warp::LoadSerial();out.transition=warp::TransitionSerial();out.validMask|=8192;
    }
    if (Read(image+offsets::NOW,out.location)) out.validMask|=128;
    return out;
}
} // namespace

bool Configure(std::uintptr_t base,spawncontroller::LogFn log) {
    const auto error=GetLastError();
    if (!Enabled("KH2COOP_POPULATION_AUTHORITY_TRACE")) {SetLastError(error);return false;}
    // This first profile is standalone. Never silently combine another memory
    // experiment or network role with this observation contract.
    const char* conflicts[]{"KH2COOP_SPAWN_TRACE","KH2COOP_SURVIVING_PACK_PREPARE",
        "KH2COOP_NATURAL_RESOURCE_TRACE","KH2COOP_LIFETIME_TRACE",
        "KH2COOP_NATIVE_SORA_PRIVATE_STATUS","KH2COOP_ENEMY_POPULATION",
        "KH2COOP_ENEMY_POPULATION_SPAWN","KH2COOP_SPAWN_PICK","KH2COOP_ENEMY_MIRROR",
        "KH2COOP_PLAYER_KIT","KH2COOP_PARTY_NATIVE","KH2COOP_ENEMY_TARGET_REMOTE"};
    for (const auto* flag:conflicts) if (Enabled(flag)) {
        if (log) log("[population-authority] refused conflict=%s",flag);
        SetLastError(error);return false;
    }
    if (!VerifyNativeSpans(base)) {
        if (log) log("[population-authority] refused native-entry-span-or-unwind-identity");
        SetLastError(error);return false;
    }
    if (attempted.exchange(true)) {recorder.Stop(authoritytrace::Loss::Install);SetLastError(error);return false;}
    image=base;logger=log;
    HMODULE module=nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&Configure),&module)) {
        recorder.Stop(authoritytrace::Loss::Install);requested=false;SetLastError(error);return false;
    }
    exposed=true;tracefiber::retained=true; // setup owns process-lifetime retention, even on partial enable
    recorder.Start(GetTickCount64());
    requested.store(true); // publish configured state only after all plain setup writes
    if (logger) logger("[population-authority] schema=1 configured=1 role=0 flsSetCalls=0 authority=UNKNOWN creator=UNKNOWN pending=UNKNOWN enrollment=UNKNOWN seconds=180 frames=10800");
    SetLastError(error);return true;
}
bool Requested() {return requested.load();}
bool Retained() {return exposed.load();}
void Coverage(std::uint32_t installedMask) {
    if (!Requested()) return;
    mask.fetch_or(installedMask);
    if (installedMask) {exposed=true;tracefiber::retained=true;}
    if (logger) logger("[population-authority] coverage mask=%u staticClosure=UNKNOWN pendingBaseline=UNKNOWN",mask.load());
}
bool PrepareTrampoline(std::uintptr_t rva,void* original) {
    if (!Requested() || !original) return false;
    const auto address=reinterpret_cast<std::uintptr_t>(original);
    std::array<std::uint8_t,64> bytes{};
    if (!Read(address,bytes)) return false;
    const authoritypins::Pin* pin=nullptr;
    for (const auto& candidate:authoritypins::Pins) if (candidate.rva==rva) pin=&candidate;
    if (!pin) return false;
    const bool leaf=rva==0x3FED10, push=rva==0x3D4A40, count=rva==0x3FED40;
    const unsigned stolen=leaf?6u:(push?6u:(count?8u:5u));
    const unsigned tail=leaf?20u:stolen;
    const std::uint8_t absolute[]{0xFF,0x25,0,0,0,0};
    std::uintptr_t continuation=0,relay=0;
    if (std::memcmp(bytes.data(),pin->body,leaf?4u:stolen) ||
        std::memcmp(bytes.data()+tail,absolute,6) || std::memcmp(bytes.data()+tail+14,absolute,6)) return false;
    std::memcpy(&continuation,bytes.data()+tail+6,8);std::memcpy(&relay,bytes.data()+tail+20,8);
    if (continuation!=image+rva+stolen || !relay) return false;
    if (leaf) {
        std::uintptr_t branch=0;
        if (bytes[4]!=0x74 || bytes[5]!=14 || std::memcmp(bytes.data()+6,absolute,6)) return false;
        std::memcpy(&branch,bytes.data()+12,8);
        if (branch!=image+rva+6+pin->body[5]) return false;
    }
    // Setup-only allocation. No FLS setup or allocation takes place in a hook.
    using Allocate2=PVOID(WINAPI*)(HANDLE,PVOID,SIZE_T,ULONG,ULONG,MEM_EXTENDED_PARAMETER*,ULONG);
    const auto procedure=GetProcAddress(GetModuleHandleW(L"KernelBase.dll"),"VirtualAlloc2");
    Allocate2 allocate=nullptr;std::memcpy(&allocate,&procedure,sizeof(allocate));
    if (!allocate || address>UINTPTR_MAX-UINT32_MAX) return false;
    SYSTEM_INFO system{};GetSystemInfo(&system);const auto gran=static_cast<std::uintptr_t>(system.dwAllocationGranularity);
    const auto low=((address+gran-1)/gran)*gran;
    auto high=address+UINT32_MAX;const auto maximum=reinterpret_cast<std::uintptr_t>(system.lpMaximumApplicationAddress);
    if (high>maximum) high=maximum;high=((high+1)/gran)*gran-1;
    if (high<low || high-low<4095) return false;
    MEM_ADDRESS_REQUIREMENTS requirements{reinterpret_cast<void*>(low),reinterpret_cast<void*>(high),0};
    MEM_EXTENDED_PARAMETER parameter{};parameter.Type=MemExtendedParameterAddressRequirements;parameter.Pointer=&requirements;
    auto* metadata=static_cast<std::uint8_t*>(allocate(nullptr,nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE,&parameter,1));
    if (!metadata) return false;
    // All inspected native entries use the ordinary x64 ABI. R11 is volatile
    // and carries no argument/result. Register jumps avoid FF25 epilogue scanning
    // after a stolen native stack adjustment (the existing private-status rule).
    const auto jump=[&](unsigned offset,std::uintptr_t destination) {
        bytes[offset]=0x49;bytes[offset+1]=0xBB;std::memcpy(bytes.data()+offset+2,&destination,8);
        bytes[offset+10]=0x41;bytes[offset+11]=0xFF;bytes[offset+12]=0xE3;bytes[offset+13]=0x90;
    };
    if (leaf) {std::uintptr_t branch=0;std::memcpy(&branch,bytes.data()+12,8);jump(6,branch);}
    jump(tail,continuation);jump(tail+14,relay);
    constexpr std::uint8_t leafUnwind[]{1,0,0,0};
    constexpr std::uint8_t pushUnwind[]{1,6,2,0,6,0x32,2,0x30};
    constexpr std::uint8_t countUnwind[]{1,4,1,0,4,0x42,0,0};
    std::uint8_t saveUnwind[]{1,5,2,0,5,0x34,static_cast<std::uint8_t>(pin->body[4]/8),0};
    const auto* unwind=leaf?leafUnwind:(push?pushUnwind:(count?countUnwind:saveUnwind));
    const std::size_t unwindBytes=leaf?sizeof(leafUnwind):8;
    std::memcpy(metadata,unwind,unwindBytes);std::memcpy(metadata+32,leafUnwind,4);
    auto* functions=reinterpret_cast<RUNTIME_FUNCTION*>(metadata+64);
    const auto relative=reinterpret_cast<std::uintptr_t>(metadata)-address;
    if (relative>UINT32_MAX-32) return false;
    functions[0]={0,tail+14,static_cast<DWORD>(relative)};
    functions[1]={tail+14,tail+28,static_cast<DWORD>(relative+32)};
    if (!RtlAddFunctionTable(functions,2,address)) return false;
    DWORD previous=0;
    if (!VirtualProtect(original,64,PAGE_EXECUTE_READWRITE,&previous)) return false;
    std::memcpy(original,bytes.data(),tail+28);
    DWORD ignored=0;
    const bool protectedAgain=VirtualProtect(original,64,previous,&ignored)!=FALSE;
    const bool flushed=FlushInstructionCache(GetCurrentProcess(),original,tail+28)!=FALSE;
    if (logger) logger("[population-authority] trampoline rva=%llX stolen=%u tail=%u metadata=registered retained=1",rva,stolen,tail);
    return protectedAgain && flushed;
}
Token Enter(Kind kind,std::uintptr_t controller,std::uintptr_t record,std::uintptr_t actor,std::uintptr_t caller) {
    if (!Requested() || !recorder.enabled()) return {};
    const auto error=GetLastError();
    const auto identity=Identity();const auto snapshot=Capture(controller,record,actor);
    const auto now=GetTickCount64();
    const auto token=recorder.Enter(kind,identity,caller,snapshot,now);
    SetLastError(error);return token;
}
Token EnterFactory(std::uint32_t rawId,const float* point,float yaw,std::uintptr_t caller) {
    if (!Requested() || !recorder.enabled()) return {};
    const auto error=GetLastError();const auto identity=Identity();auto snapshot=Capture(0,0,0);
    snapshot.rawObjectId=rawId;std::memcpy(&snapshot.yawBits,&yaw,sizeof(yaw));
    if (Read(reinterpret_cast<std::uintptr_t>(point),snapshot.pointBytes)) snapshot.validMask|=16384;
    const auto now=GetTickCount64();const auto token=recorder.Enter(Kind::Factory,identity,caller,snapshot,now);
    SetLastError(error);return token;
}
void Exit(Token token,Kind kind,std::uintptr_t controller,std::uintptr_t record,std::uintptr_t,
          std::uint64_t result,bool returned) {
    if (!token.entered) return;
    const auto error=GetLastError();
    // No post-disposal actor dereference. Five-root returned-actor observations
    // require the runner's independently fresh census; pointer return alone is
    // not a native membership/physical-absence witness.
    const auto identity=Identity();const auto snapshot=Capture(controller,record,0);
    const auto now=GetTickCount64();
    recorder.Exit(token,kind,identity,snapshot,now,result,returned);
    SetLastError(error);
}
void Frame() {
    if (!Requested()) return;
    const auto error=GetLastError();++frames;
    (void)recorder.Within(GetTickCount64(),frames);
    authoritytrace::Event event{};
    for (unsigned n=0;n<512 && recorder.Pop(event);++n) {
        const auto& s=event.snapshot;
        if (logger) logger("[population-authority] event seq=%llu invocation=%llu parent=%llu context=%llu kind=%u exit=%u returned=%u unwind=%u ms=%llu thread=%u fiber=%llX isFiber=%u stackHigh=%llX identityValid=%u depth=%u openObserved=%u caller=%llX result=%llX controller=%llX record=%llX actor=%llX objentry=%llX status=%llX actorController=%llX actorRecord=%llX header=%llX nativeId=%u initial=%u current=%u flags=%u validMask=%u load=%u transition=%u rawObjectId=%u yawBits=%u authority=UNKNOWN",
            event.sequence,event.invocation,event.parent,event.context,static_cast<unsigned>(event.kind),
            event.exit?1u:0u,event.returned?1u:0u,event.unwound?1u:0u,event.milliseconds,
            event.identity.thread,event.identity.fiber,event.identity.isFiber?1u:0u,event.identity.stackHigh,
            event.identity.valid?1u:0u,event.depth,event.observedOpen,event.caller,event.result,
            s.controller,s.record,s.actor,s.objectEntry,s.status,s.actorController,s.actorRecord,s.header,
            static_cast<unsigned>(s.nativeId),s.initialCount,s.currentCount,s.flags,s.validMask,s.load,s.transition,s.rawObjectId,s.yawBits);
        // Raw sampled bytes are emitted separately, with the same event serial.
        if (logger) {
            const auto emit=[&](const char* field,const auto& bytes) {
                char text[129]{};constexpr char hex[]="0123456789abcdef";
                for (std::size_t i=0;i<bytes.size();++i) {text[i*2]=hex[bytes[i]>>4];text[i*2+1]=hex[bytes[i]&15];}
                logger("[population-authority] bytes seq=%llu field=%s value=%s",event.sequence,field,text);
            };
            emit("controller",s.controllerBytes);emit("header",s.headerBytes);emit("record",s.recordBytes);emit("location",s.location);
            emit("point",s.pointBytes);
            constexpr const char* names[]{"definition0","definition1","definition2","definition3","definition4"};
            for (unsigned i=0;i<5;++i) emit(names[i],s.definitionRecords[i]);
        }
    }
    (void)recorder.Within(GetTickCount64(),frames); // drain/log work consumes the same deadline
    if (logger) logger("[population-authority] drain frame=%u mask=%u recording=%u loss=%u openObserved=%u owner=OBSERVED fiber=UNKNOWN creator=UNKNOWN pending=UNKNOWN enrollment=UNKNOWN creationAuthority=0",
        frames,mask.load(),recorder.enabled()?1u:0u,static_cast<unsigned>(recorder.loss()),recorder.observedOpen());
    SetLastError(error);
}
void Stop() {if (Requested()) recorder.Stop(authoritytrace::Loss::Install);}
} // namespace kh2coop::inject::populationauthority
