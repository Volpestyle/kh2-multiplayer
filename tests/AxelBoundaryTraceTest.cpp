#include <Windows.h>
#include <MinHook.h>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
#include "../inject/src/AxelBoundaryTrace.hpp"

namespace trace = kh2coop::inject::axeltrace;
namespace d = trace::detail;
static std::vector<std::string> output;
static unsigned checks = 0, failures = 0, creates = 0, enables = 0, removes = 0, failEnable = 0;
static unsigned nativeCalls = 0;
static std::uintptr_t lastArg = 0;
static void Check(bool pass, const char* what) {
    ++checks; if (!pass) { ++failures; std::printf("FAIL %s\n", what); }
}
static void Log(const char* fmt, ...) {
    char line[4096]; va_list args; va_start(args, fmt);
    vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, args); va_end(args);
    output.emplace_back(line);
    SetLastError(999); // Observers must preserve the native caller's value.
}
MH_STATUS WINAPI MH_CreateHook(LPVOID target, LPVOID, LPVOID* original) {
    ++creates; *original = target; return MH_OK;
}
MH_STATUS WINAPI MH_EnableHook(LPVOID) { return ++enables == failEnable ? MH_ERROR_MEMORY_PROTECT : MH_OK; }
MH_STATUS WINAPI MH_DisableHook(LPVOID) { return MH_OK; }
MH_STATUS WINAPI MH_RemoveHook(LPVOID) { ++removes; return MH_OK; }
static void Native(std::uintptr_t arg) { ++nativeCalls; lastArg = arg; SetLastError(123); }
static void Unwind(std::uintptr_t) { RaiseException(0xE0002897, 0, 0, nullptr); }
static bool RunUnwind() {
    __try { d::Intro(987); }
    __except (GetExceptionCode() == 0xE0002897 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return true; }
    return false;
}
static bool Has(const char* text) {
    for (const auto& row : output) if (row.find(text) != std::string::npos) return true;
    return false;
}
int main() {
    auto* image = static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2B00000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if (!image) return 2;
    const auto base = reinterpret_cast<std::uintptr_t>(image);
    const unsigned char dispatch[] = {0x45,0x33,0xC0,0x33,0xD2,0xE9,0x36,0x01,0,0};
    const unsigned char intro[] = {0x40,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xF9,0x48,0x8B,0x0D,0x68,0x54,0x66,0x02};
    const unsigned char unfreeze[] = {0x8B,0xC1,0x48,0x8D,0x15,0xFF,0x7E,0x65,0x02,0x48,0xC1,0xE8,0x05,0x83,0xE1,0x1F};
    std::memcpy(image+0x14FC20,dispatch,sizeof(dispatch));
    std::memcpy(image+0x3AC000,intro,sizeof(intro));
    std::memcpy(image+0x3BF2E0,unfreeze,sizeof(unfreeze));
    SetEnvironmentVariableA("KH2COOP_ENEMY_MIRROR_TRACE",nullptr);
    trace::Install(base,Log); Check(creates == 0 && !trace::Enabled(),"off means no hooks");
    SetEnvironmentVariableA("KH2COOP_ENEMY_MIRROR_TRACE","1");
    image[0x14FC20] ^= 1;
    trace::Install(base,Log); Check(creates == 0 && !trace::Enabled(),"mismatched pin refuses all hooks");
    image[0x14FC20] ^= 1;
    failEnable = 2;
    trace::Install(base,Log); Check(removes == 3 && !trace::Enabled(),"partial enable removes all created hooks");
    failEnable = 0;
    trace::Install(base,Log); Check(trace::Enabled(),"matching pins enable observer");
    d::dispatch = Native; d::intro = Native; d::unfreeze = Native;
    auto* now = reinterpret_cast<std::uint16_t*>(image + kh2coop::offsets::NOW);
    now[0] = 0x1302; now[3] = 134;
    const std::uint16_t wrong[] = {2,0x14,0,1,138,0}, target[] = {2,0x14,0,1,137,0};
    trace::Request(wrong,true); Check(!d::active.load(),"wrong battle program cannot arm");
    trace::Owner owner; owner.frame=42; owner.session=123; owner.delivery=55; owner.connection=56;
    trace::Publish(owner);
    SetLastError(88); trace::Request(target,false);
    Check(GetLastError()==88 && d::active.load(),"exact observed native exit arms without altering LastError");
    const auto start = d::start;
    trace::Request(target,true); Check(d::start==start,"issue does not restart deadline");
    *reinterpret_cast<std::uintptr_t*>(image+0x716868)=456;
    // Every native read lies in a read-only owned image. An accidental trace
    // write faults; none of these fake native leaves write the image either.
    DWORD previous=0;
    VirtualProtect(image,0x2B00000,PAGE_READONLY,&previous);
    d::Dispatch(456); Check(nativeCalls==1 && lastArg==456 && GetLastError()==123,"field callback forwards once and preserves native LastError");
    d::Dispatch(789); Check(nativeCalls==2 && lastArg==789,"other task group still forwards once");
    d::Intro(987); d::Unfreeze(1);
    Check(nativeCalls==4 && lastArg==1 && GetLastError()==123,"intro and group-release forward unchanged");
    Check(Has("field-enter") && Has("field-return") && Has("intro-update-return") && Has("unfreeze-return"),"actual adapters emit paired boundaries");
    Check(Has("frame=42") && Has("delivery=55") && Has("connection=56"),"copied owner provenance retained");
    const auto hp=trace::BeginHp(1234,-10); trace::EndHp(hp,1234,90);
    Check(Has("value=-10") && Has("value=90"),"HP trace distinguishes requested delta and native result");
    d::intro=Unwind;
    Check(RunUnwind() && Has("intro-update-unwind"),"native SEH unwinds through observer unchanged");
    d::intro=Native;
    const auto oldDrops=d::dropped.load();
    AcquireSRWLockExclusive(&d::lock); SetLastError(99); trace::Point("contended"); ReleaseSRWLockExclusive(&d::lock);
    Check(d::dropped.load()==oldDrops+1 && GetLastError()==99,"contention never blocks and records loss");
    d::start=GetTickCount64()-15001; trace::Input();
    Check(!d::active.load() && Has("trace-deadline"),"independent input observes bounded deadline");
    trace::Request(target,true); Check(!d::active.load(),"finished one-shot trace cannot rearm");
    trace::Shutdown(); Check(!trace::Enabled(),"shutdown disables observer");
    VirtualFree(image,0,MEM_RELEASE);
    std::printf("%u checks, %u failures\n",checks,failures);
    return failures ? 1 : 0;
}
