// Production installer and callbacks over an owned synthetic image and MinHook double.
// Never execute outside the canonical ctest lease / operator-reserved native slot.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "MinHook.h"
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "LimitAdmission.hpp"
namespace harness {
struct Hook { void* target{}; void* detour{}; bool enabled{}; bool queued{}; void* relay{}; };
Hook hooks[2];unsigned created=0,creates=0,queues=0,applies=0,disables=0,pins=0,checks=0;
std::uintptr_t base=0;std::string mode;bool active=true;unsigned partial=0;
void Check(bool value){++checks;if(!value){std::fprintf(stderr,"FAIL mode=%s check=%u\n",mode.c_str(),checks);std::exit(1);}}
int __fastcall OriginalMenu(void*,std::uint16_t*,int){return 7;}
std::uintptr_t __fastcall OriginalUsable(std::uint32_t){return 123;}
std::uintptr_t __fastcall Lookup(std::uint32_t id){return id==77?999:0;}
bool Active(){return active;}
BOOL WINAPI Pin(DWORD,LPCSTR,HMODULE* out){++pins;*out=reinterpret_cast<HMODULE>(1);return mode!="pin";}
void Patch(Hook& hook,bool enabled){
    auto* target=static_cast<std::uint8_t*>(hook.target);
    if(enabled){target[0]=0xE9;auto delta=static_cast<std::int32_t>(reinterpret_cast<std::intptr_t>(hook.relay)-reinterpret_cast<std::intptr_t>(target+5));std::memcpy(target+1,&delta,4);}
    else{const auto i=static_cast<unsigned>(&hook-hooks);const std::uint8_t menu[]{0x48,0x89,0x5c,0x24,0x08};const std::uint8_t usable[]{0x48,0x89,0x5c,0x24,0x08};std::memcpy(target,i==0?menu:usable,5);}
    hook.enabled=enabled;
}
MH_STATUS WINAPI Create(void* target,void* detour,void** original){
    const auto call=creates++;
    if(mode=="create"+std::to_string(call))return MH_ERROR_MEMORY_ALLOC;
    if(mode=="foreign")return MH_ERROR_ALREADY_CREATED;
    if(mode=="reentrant")Check(!kh2coop::inject::limitadmission::Install(base,&Active,nullptr));
    auto& hook=hooks[created];hook.target=target;hook.detour=detour;
    hook.relay=reinterpret_cast<void*>(base+0x500000+created*32);
    const std::uint8_t prefix[]{0xFF,0x25,0,0,0,0};std::memcpy(hook.relay,prefix,6);std::memcpy(static_cast<std::uint8_t*>(hook.relay)+6,&detour,sizeof(detour));
    *original=created==0?reinterpret_cast<void*>(&OriginalMenu):reinterpret_cast<void*>(&OriginalUsable);
    ++created;return MH_OK;
}
MH_STATUS WINAPI QueueEnable(void* target){
    const auto call=queues++;if(mode=="queue"+std::to_string(call))return MH_ERROR_MEMORY_PROTECT;
    for(auto& h:hooks)if(h.target==target){h.queued=true;return MH_OK;}return MH_ERROR_NOT_CREATED;
}
MH_STATUS WINAPI QueueDisable(void* target){
    if(mode=="cleanup-fail"||mode=="cleanup-fallback")return MH_ERROR_MEMORY_PROTECT;
    for(auto& h:hooks)if(h.target==target||target==MH_ALL_HOOKS)h.queued=false;
    return MH_OK;
}
MH_STATUS WINAPI Disable(void* target){
    ++disables;if(mode=="cleanup-fail"||mode=="held")return MH_ERROR_MEMORY_PROTECT;
    for(auto& h:hooks)if(h.target==target){if(!h.enabled)return MH_ERROR_DISABLED;Patch(h,false);return MH_OK;}return MH_ERROR_NOT_CREATED;
}
MH_STATUS WINAPI Apply(){
    ++applies;
    if(mode=="held"&&applies>1)return MH_ERROR_MEMORY_PROTECT;
    if(applies==1&&(mode.rfind("apply",0)==0||mode=="cleanup-fail"||mode=="cleanup-fallback")){
        for(unsigned i=0;i<created;++i)Patch(hooks[i],(partial&(1u<<i))!=0);
        return MH_ERROR_MEMORY_PROTECT;
    }
    if(applies>1&&(mode=="cleanup-fail"||mode=="cleanup-fallback"))return MH_ERROR_MEMORY_PROTECT;
    for(unsigned i=0;i<created;++i)Patch(hooks[i],hooks[i].queued);
    if(applies==1){
        // Real detours execute in the physical-enable/publication window and must delegate.
        std::uint16_t command[]{77};
        if(hooks[0].enabled)Check(reinterpret_cast<int(__fastcall*)(void*,std::uint16_t*,int)>(hooks[0].detour)(nullptr,command,0)==7);
        if(hooks[1].enabled)Check(reinterpret_cast<std::uintptr_t(__fastcall*)(std::uint32_t)>(hooks[1].detour)(77)==123);
    }
    if(mode=="identity"&&applies==1)static_cast<std::uint8_t*>(hooks[0].relay)[0]=0;
    return MH_OK;
}
}
#define GetModuleHandleExA harness::Pin
#define MH_CreateHook harness::Create
#define MH_QueueEnableHook harness::QueueEnable
#define MH_QueueDisableHook harness::QueueDisable
#define MH_DisableHook harness::Disable
#define MH_ApplyQueued harness::Apply
#include "LimitAdmission.cpp"
#undef GetModuleHandleExA
#undef MH_CreateHook
#undef MH_QueueEnableHook
#undef MH_QueueDisableHook
#undef MH_DisableHook
#undef MH_ApplyQueued
int main(int argc,char** argv){
    using namespace harness;
    namespace gate=kh2coop::inject::limitadmission;
    if(argc!=2)return 2;mode=argv[1];
    std::vector<std::uint8_t> image(0x600000);base=reinterpret_cast<std::uintptr_t>(image.data());
    std::memcpy(image.data()+0x3D88E0,gate::MENU_BYTES,sizeof(gate::MENU_BYTES));
    std::memcpy(image.data()+0x3E7800,gate::USABLE_BYTES,sizeof(gate::USABLE_BYTES));
    std::memcpy(image.data()+0x3E7C30,gate::LOOKUP_BYTES,sizeof(gate::LOOKUP_BYTES));
    if(mode.rfind("signature-",0)==0){const int i=std::atoi(mode.c_str()+10);const unsigned at[]={0x3D88E0,0x3E7800,0x3E7C30};const unsigned size[]={sizeof(gate::MENU_BYTES),sizeof(gate::USABLE_BYTES),sizeof(gate::LOOKUP_BYTES)};Check(i>=0&&i<9);image[at[i/3]+(i%3==0?0:(i%3==1?size[i/3]/2:size[i/3]-1))]^=1;}
    if(mode.rfind("apply",0)==0)partial=static_cast<unsigned>(std::atoi(mode.c_str()+5));
    if(mode=="cleanup-fail"||mode=="cleanup-fallback")partial=3;
    const bool success=mode=="success"||mode=="reentrant"||mode=="lost-patch"||mode=="shutdown"||mode=="held"||mode=="owner-hold";
    Check(gate::Install(base,&Active,nullptr)==success);
    if(!success){
        Check(!gate::Ready()&&gate::RejectReinitialization());Check(gate::RetainsMinHookResources()==(created!=0));
        const auto oldCreates=creates;Check(!gate::Install(base,&Active,nullptr));Check(creates==oldCreates);
        if(mode=="foreign"||mode.rfind("signature",0)==0||mode=="pin")Check(created==0&&disables==0);
        if(mode!="cleanup-fail")for(unsigned i=0;i<created;++i)Check(!hooks[i].enabled);
        if(created){std::uint16_t command[]{77};Check(gate::Menu(nullptr,command,0)==7);}
        if(created==2)Check(gate::Usable(77)==123);
    }else{
        Check(gate::Ready()&&created==2&&pins==1);Check(gate::Install(base,&Active,nullptr));Check(!gate::Install(base+1,&Active,nullptr));
        // Native lookup and menu/usable originals are boundary doubles, not a claim of game execution.
        gate::g_lookup=&Lookup;std::uint16_t command[]{77};Check(gate::Menu(nullptr,command,0)==5);Check(gate::Usable(77)==0);
        active=false;Check(gate::Menu(nullptr,command,0)==7);Check(gate::Usable(77)==123);active=true;
        if(mode=="lost-patch"){Patch(hooks[1],false);Check(!gate::Install(base,&Active,nullptr));Check(!gate::Ready()&&gate::RejectReinitialization());Check(gate::Menu(nullptr,command,0)==5);}
        if(mode=="owner-hold"){gate::HoldForShutdown();Check(!gate::Ready());Check(gate::QueuePreserveHeld());Check(gate::Menu(nullptr,command,0)==5);}
        if(mode=="shutdown"||mode=="held"){
            gate::HoldForShutdown();gate::Shutdown();Check(!gate::Ready()&&gate::RejectReinitialization()&&gate::RetainsMinHookResources());
            Check(!gate::Install(base,&Active,nullptr));
            Check(gate::Menu(nullptr,command,0)==(mode=="held"?5:7));
            if(mode=="held"){QueueDisable(MH_ALL_HOOKS);Check(gate::QueuePreserveHeld());Check(hooks[0].queued&&hooks[1].queued);}
        }
    }
    std::printf("PASS %s assertions=%u\n",mode.c_str(),checks);return 0;
}
