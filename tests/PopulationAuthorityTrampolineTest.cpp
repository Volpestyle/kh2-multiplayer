#if !defined(_WIN32) || !defined(_MSC_VER)
#error Windows SEH/unwind control only
#endif
#include "../inject/src/NativePopulationAuthority.cpp"
#include <MinHook.h>
#include <cstdlib>
#include <iostream>

namespace kh2coop::inject::warp {
std::uint32_t LoadSerial() {return 0;}
std::uint32_t TransitionSerial() {return 0;}
}
namespace kh2coop::inject::spawncontroller {
bool IsDiagnosticGameThread() {return true;}
}
static void Require(bool value,const char* label) {if (!value) {std::cerr<<label<<'\n';std::abort();}}
static void Detour() {}
int main() {
    using namespace kh2coop::inject;
    constexpr std::size_t bytes=0x700000;
    auto* owned=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,bytes,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    Require(owned!=nullptr,"owned native-code scratch allocation");
    populationauthority::image=reinterpret_cast<std::uintptr_t>(owned);
    populationauthority::requested=true;
    Require(MH_Initialize()==MH_OK,"test-owned MinHook initialize");
    for (const auto& pin:authoritypins::Pins) {
        std::memcpy(owned+pin.rva,pin.body,pin.size);
        void* original=nullptr;
        Require(MH_CreateHook(owned+pin.rva,reinterpret_cast<void*>(&Detour),&original)==MH_OK,"actual MinHook relocation of saved entry bytes");
        Require(populationauthority::PrepareTrampoline(pin.rva,original),"actual qualifier accepts supported relocation and registers unwind");
        Require(!populationauthority::PrepareTrampoline(pin.rva,original),"rewritten trampoline cannot be re-registered");
        const bool leaf=pin.rva==0x3FED10, push=pin.rva==0x3D4A40, count=pin.rva==0x3FED40;
        const unsigned prefix=leaf?20u:(push?6u:(count?8u:5u));
        for (bool relay:{false,true}) {
            alignas(16) std::array<DWORD64,64> stack{};
            const auto entry=reinterpret_cast<DWORD64>(&stack[32]);
            stack[32]=0x112233445566ULL;stack[31]=0x33445566;stack[33]=stack[34]=0x33445566;
            CONTEXT context{};context.ContextFlags=CONTEXT_FULL;
            context.Rip=reinterpret_cast<DWORD64>(original)+(relay?prefix+14:prefix);
            context.Rsp=entry-(!relay && (push || count)?40:0);context.Rbx=0xBAD;
            DWORD64 base=0;const auto* function=RtlLookupFunctionEntry(context.Rip,&base,nullptr);
            Require(function && base==reinterpret_cast<DWORD64>(original),"registered trampoline/relay identity");
            PVOID handler=nullptr;DWORD64 establisher=0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER,base,context.Rip,const_cast<PRUNTIME_FUNCTION>(function),
                &context,&handler,&establisher,nullptr);
            Require(context.Rsp==entry+8 && context.Rip==stack[32],"actual Windows unwind restores caller stack and PC");
            const auto expectedRbx=(!relay && !leaf && !count)?0x33445566ULL:0xBADULL;
            Require(context.Rbx==expectedRbx,"native saved RBX recovered only where applicable");
        }
    }
    // Never enable hooks or execute saved native bodies. Registered unwind tables,
    // trampoline slots and owned scratch code remain valid until this test exits.
    std::cout<<"Authority trampoline actual-MinHook/Windows-unwind controls PASS\n";
}
