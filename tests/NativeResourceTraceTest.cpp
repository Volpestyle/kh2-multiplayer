// Headless Windows controls of the actual production translation unit. Each
// retained-install case runs in its own process; no reset/free of exposed code.
#if !defined(_WIN32) || !defined(_MSC_VER)
#error NativeResourceTraceTest requires Windows/MSVC SEH.
#endif
#include "../inject/src/NativeResourceTrace.cpp"
#include <cstdio>
#include <string_view>

using namespace kh2coop::inject;
namespace rt=kh2coop::inject::resourcetrace;
namespace {
unsigned passed=0,failed=0;
void Check(bool ok,const char* text){std::printf("%s: %s\n",ok?"PASS":"FAIL",text);ok?++passed:++failed;}
thread_local spawncontroller::NativeConstructionLineage lineage;
thread_local bool lineageAvailable=true;
std::atomic<unsigned> lineageCalls{0};
using Fn=std::uint64_t(__fastcall*)(void*,const char*,const char*);
Fn target=nullptr;
std::atomic<unsigned> calls{0};
std::uint64_t rawReturn=0xABCDEF1234567880ULL;
void* object=nullptr;
const char* name="obj/owned.a.us";
const char* optionalRoot="root";
bool inputsOkay=true,recurse=false,throwNative=false,stopInside=false;
unsigned desiredDepth=0;
DWORD inputLastError=0,outputLastError=0x1234;
// 497beb6 moved the per-thread scope into the retained FLS-owned store
// (NativeTraceFiber.hpp). Returns this fiber's retained Local, or nullptr if
// the store refused it (which the checks below then treat as a failure).
rt::Local* Scope(){return rt::g_localStorage.Current();}
std::uint32_t CallbackDepth(){auto* l=Scope();return l?l->callbackDepth:UINT32_MAX;}
std::uint32_t ConstructionDepth(){auto* l=Scope();return l?l->constructionDepth:UINT32_MAX;}
__declspec(noinline) std::uint64_t __fastcall Model(void* p,const char* n,const char* r){
    ++calls;inputsOkay=inputsOkay && p==object && n==name && r==optionalRoot;
    if(GetLastError()!=inputLastError)inputsOkay=false;
    if(stopInside)rt::StopRecording();
    if(throwNative && CallbackDepth()==desiredDepth){SetLastError(outputLastError);RaiseException(0xE0107240,0,0,nullptr);}
    if(recurse && CallbackDepth()<desiredDepth){SetLastError(inputLastError);if(target(p,n,r)!=rawReturn)inputsOkay=false;}
    SetLastError(outputLastError);return rawReturn;
}
struct OwnedTarget {std::uint8_t* code=nullptr;RUNTIME_FUNCTION* table=nullptr;};
OwnedTarget MakeTarget(){
    OwnedTarget t{};t.code=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    if(!t.code)return t;
    const std::uint8_t bytes[]{0x40,0x53,0x55,0x56,0x57,0x48,0x83,0xEC,0x28,0x49,0xBB,
        0,0,0,0,0,0,0,0,0x41,0xFF,0xD3,0x48,0x83,0xC4,0x28,0x5F,0x5E,0x5D,0x5B,0xC3};
    std::memcpy(t.code,bytes,sizeof(bytes));const auto model=reinterpret_cast<uintptr_t>(&Model);std::memcpy(t.code+11,&model,8);
    const std::uint8_t u[]{1,9,5,0,9,0x42,5,0x70,4,0x60,3,0x50,2,0x30,0,0};std::memcpy(t.code+256,u,sizeof(u));
    t.table=reinterpret_cast<RUNTIME_FUNCTION*>(t.code+512);*t.table={0,31,256};
    if(!RtlAddFunctionTable(t.table,1,reinterpret_cast<DWORD64>(t.code)))return {};
    FlushInstructionCache(GetCurrentProcess(),t.code,4096);target=reinterpret_cast<Fn>(t.code);return t;
}
void FreeUnexposed(OwnedTarget& t){if(t.table)RtlDeleteFunctionTable(t.table);if(t.code)VirtualFree(t.code,0,MEM_RELEASE);t={};}
void Lineage(std::uint64_t serial=101){
    lineage={};lineage.serial=serial;lineage.coverage=7;lineage.wrapperSequence=9;
    lineage.captured=true;lineage.candidateThreadParent=true;lineage.threadId=GetCurrentThreadId();
    lineage.controller=0x11110000;lineage.record=0x22220000;
    lineage.samples[0].recordIndexAvailable=true;lineage.samples[0].recordIndex=3;lineage.samples[0].recordRead=true;
    lineage.samples[0].recordBytes[0]=0x2E;lineage.samples[0].recordBytes[1]=1;
    lineageAvailable=true;
}
void Invoke(){SetLastError(inputLastError);const auto result=target(object,name,optionalRoot);Check(result==rawReturn && GetLastError()==outputLastError,"production detour preserves opaque RAX and last-error");}
void Throwing(){DWORD code=0;__try{SetLastError(inputLastError);target(object,name,optionalRoot);}
    __except((code=GetExceptionCode()),EXCEPTION_EXECUTE_HANDLER){}Check(code==0xE0107240,"production detour preserves original native SEH code");}
std::size_t Drain(rt::ResourceObservation* first=nullptr){std::size_t n=0;rt::ResourceObservation row;while(rt::Pop(row)){if(!n&&first)*first=row;++n;}return n;}
std::uint8_t* FailAllocate(uintptr_t){return nullptr;}
bool FailRegister(RUNTIME_FUNCTION*,DWORD64){return false;}
bool FailPin(){return false;}
bool latchAtEnable=false;
MH_STATUS WINAPI FailEnable(LPVOID p){latchAtEnable=rt::RetainsMinHookResources();return MH_EnableHook(static_cast<std::uint8_t*>(p)+128);}
DWORD WINAPI Foreign(void*){SetLastError(inputLastError);return target(object,name,optionalRoot)==rawReturn?0:1;}
}
namespace kh2coop::inject::spawncontroller {
bool CopyNativeConstructionLineage(NativeConstructionLineage& out){++lineageCalls;out=lineage;return lineageAvailable;}
}
int main(int argc,char** argv){
    const std::string_view mode=argc>1?argv[1]:"main";
    std::uint64_t package[2]{};object=package;
    Check(MH_Initialize()==MH_OK,"actual MinHook initialized by owning harness");
    if(mode=="identity"){
        SetLastError(91);Check(rt::Initialize(0,false) && GetLastError()==91 && !rt::RetainsMinHookResources(),"default-off performs no install and preserves last-error");
        Check(!rt::Initialize(0,true) && rt::GetStatistics().status==rt::InstallStatus::IdentityUnavailable,"unreadable identity fails before create");
        // 497beb6: a requested install initializes (and permanently retains) the
        // FLS scope store before identity checks; no MinHook hook is retained.
        Check(rt::g_localStorage.Ready() && !rt::g_retained.load() && rt::RetainsMinHookResources(),"requested install retains fiber storage but no hook before identity");
        auto* image=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,0x108000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
        Check(image!=nullptr,"owned runtime identity image allocated");if(!image)return 1;
        std::memcpy(image+rt::TargetRva,rt::kBody.data(),rt::kBody.size());image[rt::TargetRva+983]^=1;
        Check(!rt::Initialize(reinterpret_cast<uintptr_t>(image),true) && rt::GetStatistics().status==rt::InstallStatus::IdentityMismatch && !rt::g_retained.load(),"changed last body byte fails exact984-byte identity despite matching prefix");
        image[rt::TargetRva+983]^=1;
        Check(rt::Initialize(reinterpret_cast<uintptr_t>(image),true),"public production initializer qualifies exact984 bytes including14-byte prefix");
        Check(rt::RetainsMinHookResources() && rt::GetStatistics().modulePinned,"public initializer pins actual containing module before exposure");
        rt::StopRecording();Check(rt::RejectReinitialization() && !rt::Initialize(reinterpret_cast<uintptr_t>(image),true),"public exact-body install remains retained and refuses reinitialization");
        // Do not execute copied game bytes or free this retained allocation.
    } else {
        auto owned=MakeTarget();Check(owned.code!=nullptr,"owned callable target/frame allocated");if(!owned.code)return 1;
        rt::InstallOps ops{};
        if(mode=="allocate")ops.allocate=&FailAllocate;
        if(mode=="register")ops.registerTable=&FailRegister;
        if(mode=="pin")ops.pin=&FailPin;
        if(mode=="enable")ops.enable=&FailEnable;
        const bool installed=rt::InstallValidatedTarget(reinterpret_cast<uintptr_t>(owned.code),owned.code,ops);
        if(mode!="main"){
            Check(!installed,"injected install boundary failure is reported");
            if(mode=="enable"){
                Check(latchAtEnable && rt::RetainsMinHookResources() && rt::g_original && rt::g_metadata && rt::g_table,
                    "potential exposure latch precedes failed real enable; original/code/metadata retained");
                const auto before=rt::g_original;rt::StopRecording();Check(rt::g_original==before && rt::RejectReinitialization(),"failed-enable retirement retains pass-through and blocks restart");
                Check(MH_RemoveHook(owned.code+128)==MH_ERROR_NOT_CREATED,"failure adapter used real MH_ERROR_NOT_CREATED at an owned non-hook address");
            }else{
                Check(!rt::RetainsMinHookResources() && !rt::g_original && !rt::g_metadata && !rt::g_table,"preexposure failure rolls back only owned hook/metadata/original");
                Check(MH_RemoveHook(owned.code)==MH_ERROR_NOT_CREATED,"preexposure hook was actually removed");
                Check(MH_Uninitialize()==MH_OK,"preexposure owner may globally uninitialize");FreeUnexposed(owned);
            }
        }else{
            // The owned-target seam bypasses Initialize(), so arm the retained
            // FLS scope store as Initialize() would before any observation.
            Check(rt::g_localStorage.Init(reinterpret_cast<const void*>(&rt::Initialize)) && Scope() && Scope()->constructionDepth==0,
                "retained fiber scope store initialized and idle");
            Check(installed && rt::GetStatistics().modulePinned && rt::RetainsMinHookResources(),"actual production install pins module and retains real MinHook resources");
            // This private seam uses owned synthetic code, not native identity.
            Check(!rt::GetStatistics().installationIdentityVerified,"synthetic target seam does not invent native body identity");
            Check(rt::g_trampoline[5]==0x49 && rt::g_trampoline[6]==0xBB && rt::g_trampoline[15]==0x41 && rt::g_trampoline[17]==0xE3 && rt::g_trampoline[19]==0xFF && rt::g_trampoline[20]==0x25,"production actual trampoline correction retains relay at19");
            Lineage();inputLastError=79;
            auto boundary=rt::BeginConstruction();Invoke();
            rt::ResourceObservation row;
            Check(!rt::Pop(row),"child is not published inside native construction boundary");
            const auto copiedSerial=lineage.serial;lineage.serial=777;lineage.controller=0x33330000;
            rt::EndConstruction(boundary,true);
            Check(Drain(&row)==1 && row.parent.serial==copiedSerial && row.parent.controller==0x11110000 && row.parent.recordIndex==3 && row.parent.recordBytes[0]==0x2E,"entry-copied lineage fields are not reparented at return");
            Check(row.normalReturn && row.rawRax==rawReturn && row.al==0x80 && !row.dispatchOperandObserved && !row.completeRouting && !row.creationAuthority && !row.atomic && !row.lifetimeProven && !row.globalPendingExcluded && !row.fiberContinuityProven,"actual return remains distinct from false authority/routing fields");
            Check(row.filenameSample.terminated && row.rootSample.terminated && row.outerNormalReturn,"bounded filename/root copies retain explicit termination and outer boundary result");
            Lineage();boundary=rt::BeginConstruction();optionalRoot=nullptr;rawReturn=0x123456789ABCDE00ULL;Invoke();rt::EndConstruction(boundary,true);Drain(&row);
            Check(row.al==0 && row.rawRax==rawReturn && !row.rootSample.pointerNonNull,"null third input and raw upperRAX with ALzero survive");
            optionalRoot="root";rawReturn=0xAABBCCDDEEFF0001ULL;
            Lineage();boundary=rt::BeginConstruction();desiredDepth=2;recurse=true;const auto beforeCalls=calls.load();Invoke();recurse=false;rt::EndConstruction(boundary,true);
            rt::ResourceObservation rootRow{},childRow{};const bool two=rt::Pop(rootRow)&&rt::Pop(childRow)&&!rt::Pop(row);
            Check(two && childRow.parentCallback==rootRow.invocation && calls-beforeCalls==2,"actual recursive patched callback preserves parent ID and exactly once per call");
            Lineage();boundary=rt::BeginConstruction();desiredDepth=2;recurse=true;throwNative=true;Throwing();recurse=false;throwNative=false;rt::EndConstruction(boundary,false);
            Check(rt::Pop(rootRow)&&rt::Pop(childRow) && rootRow.unwound && childRow.unwound && !rootRow.normalReturn && !childRow.normalReturn && CallbackDepth()==0,"nested SEH marks each actual production callback unwind and restores depth");Drain();
            Lineage();boundary=rt::BeginConstruction();desiredDepth=10;recurse=true;Invoke();recurse=false;rt::EndConstruction(boundary,true);
            Check(Drain(&row)==8 && row.boundaryDropped==2,"eight callback-depth cap retains rows and counts deeper pass-throughs");
            Lineage();boundary=rt::BeginConstruction();for(unsigned i=0;i<65;++i){SetLastError(inputLastError);target(object,name,optionalRoot);}rt::EndConstruction(boundary,true);
            Check(Drain(&row)==64 && row.boundaryDropped==1,"aggregate64 child cap drops telemetry without dropping native call");
            Lineage();boundary=rt::BeginConstruction();lineageAvailable=false;auto hidden=rt::BeginConstruction();SetLastError(inputLastError);target(object,name,optionalRoot);rt::EndConstruction(hidden,true);rt::EndConstruction(boundary,true);
            Check(Drain()==0 && rt::GetStatistics().unparented!=0,"ineligible nested boundary hides eligible ancestor");
            Lineage();std::array<rt::ConstructionToken,9> scopes{};for(auto& s:scopes)s=rt::BeginConstruction();SetLastError(inputLastError);target(object,name,optionalRoot);
            for(auto i=scopes.size();i>0;--i)rt::EndConstruction(scopes[i-1],true);
            Check(Drain()==0 && ConstructionDepth()==0,"construction-depth overflow hides parent and restores all scopes");
            const auto services=lineageCalls.load();HANDLE worker=CreateThread(nullptr,0,Foreign,nullptr,0,nullptr);
            Check(worker && WaitForSingleObject(worker,5000)==WAIT_OBJECT_0,"foreign owned callback worker joined");if(worker)CloseHandle(worker);
            Check(rt::GetStatistics().foreign>0 && lineageCalls==services,"foreign/unparented callback does not invoke root lineage services");
            Lineage();for(unsigned b=0;b<5;++b){boundary=rt::BeginConstruction();for(unsigned i=0;i<64;++i){SetLastError(inputLastError);target(object,name,optionalRoot);}rt::EndConstruction(boundary,true);}
            Check(Drain()==256 && rt::GetStatistics().dropped>=64,"separate queue capacity bounds publication without changing native calls");
            Check(inputsOkay,"all actual native input pointers and incoming last-errors remained exact");
            Lineage();boundary=rt::BeginConstruction();stopInside=true;Invoke();stopInside=false;
            const auto originalBefore=rt::g_original;const auto tableBefore=rt::g_table;const auto metadataBefore=rt::g_metadata;
            rt::EndConstruction(boundary,true);
            Check(!rt::Pop(row) && rt::RetainsMinHookResources() && rt::g_original==originalBefore && rt::g_table==tableBefore && rt::g_metadata==metadataBefore,"stop during callback discards late publication and retains all exposed resources");
            const auto stoppedServices=lineageCalls.load();Invoke();auto stopped=rt::BeginConstruction();rt::EndConstruction(stopped,true);
            Check(lineageCalls==stoppedServices && !rt::GetStatistics().recording,"retired pass-through never touches cleared root lineage service");
            Check(!rt::Initialize(reinterpret_cast<uintptr_t>(owned.code),true) && rt::RejectReinitialization() && rt::g_original==originalBefore,"retained restart rejected without clearing original");
            DWORD64 base=0;Check(RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(rt::g_trampoline+15),&base,nullptr)==tableBefore,"registered actual trampoline remains valid after StopRecording");
            // Intentionally no MH_Uninitialize, RemoveHook, table delete or
            // VirtualFree after exposure. The test process ends with resources.
        }
    }
    std::printf("SUMMARY: %u PASS %u FAIL mode=%s\n",passed,failed,mode.data());return failed?1:0;
}
