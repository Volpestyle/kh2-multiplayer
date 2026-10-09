// Actual production detours, owned synthetic image/menu/SAVE, deterministic MinHook.
// Compile freely; execute only under the rig's ctest.lock with rig.lock absent.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <intrin.h>
#include "MinHook.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <cstdarg>
#include <string>
namespace harness {
using Fn=std::int32_t(__fastcall*)(const std::uint8_t*,std::int32_t);
struct Hook {void* target{};void* detour{};bool enabled{};int pending=-1;};
Hook hooks[7]; unsigned created=0,createCalls=0,enableCalls=0,removed=0,applyCalls=0;
int failCreate=-1,failEnable=-1;bool failApply=false;
std::uintptr_t caller=0;int lastAlias=-99,aliasCalls=0,keyCalls=0;
void* ReturnAddress(){return reinterpret_cast<void*>(caller);}
std::int32_t __fastcall Selector(const std::uint8_t* row,int seat){return row[seat]&31;}
std::int32_t __fastcall Key(const std::uint8_t* row,int seat){return row[seat]==18?0:row[seat]+1;}
std::int32_t __fastcall Find(const std::uint8_t* row,int key){for(int i=0;i<4;++i)if(Key(row,i)==key)return i;return -1;}
// Independent native boundary doubles: alias accepts ORIGINAL row seat, while
// key accepts COMPACT entry index. They intentionally have different contracts.
std::int32_t __fastcall Alias(const std::uint8_t*,int seat){++aliasCalls;lastAlias=seat;return seat==0?0x20000236:(seat==1?0x319:(seat==2?0x31A:(seat==3?0x3EE:0)));}
std::int32_t __fastcall RawKey(const std::uint8_t* menu,int index){++keyCalls;int count=0;std::memcpy(&count,menu,4);if(index<0||index>=count)return 0;std::int16_t key=0;std::memcpy(&key,menu+10+index*32,2);return key;}
std::vector<std::string> receipts;
void Log(const char* format,...){char text[2048]{};va_list args;va_start(args,format);vsnprintf_s(text,sizeof(text),_TRUNCATE,format,args);va_end(args);if(std::strncmp(text,"[partyempty-items]",18)==0||std::strncmp(text,"[partyempty-party]",18)==0||std::strncmp(text,"[partyempty-abilities-",22)==0)receipts.emplace_back(text);SetLastError(0xBADF00D);}
unsigned admissionCalls=0;
std::uint8_t __fastcall Admission(int){++admissionCalls;return 1;}
std::uint64_t __fastcall Feedback(int code,std::uint8_t){return static_cast<std::uint64_t>(code);}
MH_STATUS WINAPI Create(void* target,void* detour,void** original){const auto call=createCalls++;if(static_cast<int>(call)==failCreate)return MH_ERROR_MEMORY_ALLOC;if(created>=7)return MH_ERROR_ALREADY_CREATED;const void* originals[]{reinterpret_cast<void*>(Selector),reinterpret_cast<void*>(Key),reinterpret_cast<void*>(Find),reinterpret_cast<void*>(Alias),reinterpret_cast<void*>(RawKey),reinterpret_cast<void*>(Admission),reinterpret_cast<void*>(Admission)};hooks[created]={target,detour,false,-1};*original=const_cast<void*>(originals[created]);++created;return MH_OK;}
MH_STATUS WINAPI Enable(void* target){const auto call=enableCalls++;if(static_cast<int>(call)==failEnable)return MH_ERROR_MEMORY_PROTECT;for(auto& h:hooks)if(h.target==target){h.pending=1;return MH_OK;}return MH_ERROR_NOT_CREATED;}
MH_STATUS WINAPI Disable(void* target){for(auto& h:hooks)if(h.target==target||target==MH_ALL_HOOKS)h.pending=0;return MH_OK;}
MH_STATUS WINAPI Apply(){++applyCalls;if(failApply&&applyCalls==1){for(unsigned i=0;i<created;++i)hooks[i].enabled=true;return MH_ERROR_MEMORY_ALLOC;}for(auto& h:hooks)if(h.pending!=-1){h.enabled=h.pending!=0;h.pending=-1;}return MH_OK;}
MH_STATUS WINAPI Remove(void*){++removed;return MH_OK;}
}
#define _ReturnAddress harness::ReturnAddress
#define MH_CreateHook harness::Create
#define MH_QueueEnableHook harness::Enable
#define MH_QueueDisableHook harness::Disable
#define MH_ApplyQueued harness::Apply
#define MH_RemoveHook harness::Remove
#include "PartyEmptySeat.cpp"
#undef _ReturnAddress
#undef MH_CreateHook
#undef MH_QueueEnableHook
#undef MH_QueueDisableHook
#undef MH_ApplyQueued
#undef MH_RemoveHook
#include "PartyMenuNativeContract.inc"
namespace {
namespace empty=kh2coop::inject::partyempty;
unsigned checks=0,failures=0;
void Check(bool ok,const char* label){++checks;failures+=ok?0u:1u;std::printf("%s %s\n",ok?"PASS":"FAIL",label);}
constexpr std::uintptr_t sites[]{0x3E3670,0x3E3680,0x3E3830,0x2FC5B0,0x2FC6D0,0x34EE00,0x305FE0};
constexpr std::uint8_t selector[]{0x48,0x63,0xC2,0x0F,0xB6,0x04,0x08,0x83,0xE0,0x1F,0xC3,0xCC,0xCC,0xCC,0xCC,0xCC};
constexpr std::uint8_t key[]{0x48,0x83,0xEC,0x28,0x48,0x63,0xC2,0x0F,0xB6,0x14,0x08,0x83,0xE2,0x1F,0x83,0xFA,0x12};
constexpr std::uint8_t find[]{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7C,0x24,0x20,0x41,0x56};
constexpr std::uint8_t alias[]{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x20};
constexpr std::uint8_t rawkey[]{0x85,0xD2,0x78,0x11,0x3B,0x11,0x7D,0x0D,0x48,0x63,0xC2,0x48,0xC1,0xE0,0x05,0x0F,0xBF,0x44,0x08,0x0A,0xC3};
constexpr std::uint8_t admission[]{0x40,0x53,0x48,0x83,0xEC,0x20,0x8B,0xD9};
void Seed(std::uint8_t* image){SeedParty(image);SeedAbilities(image);const std::uint8_t* bytes[]{selector,key,find,alias,rawkey,admission};const std::size_t sizes[]{sizeof(selector),sizeof(key),sizeof(find),sizeof(alias),sizeof(rawkey),sizeof(admission)};for(unsigned i=0;i<6;++i)std::memcpy(image+sites[i],bytes[i],sizes[i]);const std::uint8_t c0[]{0xE8,0xE3,0x5F,0xFF,0xFF},c1[]{0xE8,0x66,0x22,0xF9,0xFF},c2[]{0xE8,0xA9,0x1F,0xF9,0xFF};std::memcpy(image+0x3065C8,c0,5);std::memcpy(image+0x36A465,c1,5);std::memcpy(image+0x36A722,c2,5);const std::uint8_t a0[]{0x4C,0x8D,0x0D,0x62,0x0C,0,0},a2[]{0x41,0xFF,0xD1},a3[]{0x8B,0x05,0x16,0x87,0x8F,0,0xC3};std::memcpy(image+0x34E197,a0,7);std::memcpy(image+0x2F62E9,a2,3);std::memcpy(image+0x2F5F30,a3,7);
const std::uint8_t argument[]{0x8B,0xCF},inputCall[]{0xE8,0xC8,0x80,0xFA,0xFF},resultCall[]{0x8B,0xC8,0xE8,0x01,0xD7,0xF9,0xFF},resultTest[]{0x84,0xC0},refusalBranch[]{0x0F,0x85,0x54,0x01,0,0},feedbackChoice[]{0xB9,0x02,0,0,0,0x84,0xC0,0xBA,0x04,0,0,0,0x0F,0x45,0xD1,0x8B,0xCA};
std::memcpy(image+0x34E1A1,argument,sizeof(argument));std::memcpy(image+0x34E1A3,inputCall,sizeof(inputCall));std::memcpy(image+0x34E1A8,resultCall,sizeof(resultCall));std::memcpy(image+0x34E1AF,resultTest,sizeof(resultTest));std::memcpy(image+0x34E1B1,refusalBranch,sizeof(refusalBranch));std::memcpy(image+0x2F62EC,feedbackChoice,sizeof(feedbackChoice));}
void Entry(std::uint8_t* menu,int i,std::int16_t seat,std::int16_t status){std::memcpy(menu+8+i*32,&seat,2);std::memcpy(menu+10+i*32,&status,2);const std::uintptr_t ptr=0x12345678;std::memcpy(menu+0x20+i*32,&ptr,8);}
}
#include "PartyMenuNativeInput.inc"
int main(int argc,char** argv){
 auto* image=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,0x2C2B000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));if(!image)return 2;
 const auto base=reinterpret_cast<std::uintptr_t>(image);Seed(image);auto* members=reinterpret_cast<std::uint16_t*>(image+0x2A25300);members[0]=0x5A;members[1]=0;members[2]=0x54;
 auto* save=image+kh2coop::inject::playerkit::RVA_SAVE;constexpr std::size_t saveSize=0x10FC0;std::memset(save,0xA5,saveSize);const std::vector<std::uint8_t> before(save,save+saveSize);
 const char* mode=argc>1?argv[1]:"positive";
 if(std::strncmp(mode,"signature-",10)==0){const int i=std::atoi(mode+10);const std::uintptr_t addresses[]{sites[0],sites[1],sites[2],sites[3],sites[4],sites[5],0x3065C8,0x36A465,0x36A722,0x34E197,0x2F62E9,0x2F5F30,0x34E1A1,0x34E1A3,0x34E1A8,0x34E1AF,0x34E1B1,0x2F62EC};if(i<0||i>=18)return 2;image[addresses[i]]^=1;}
 if(std::strncmp(mode,"create-",7)==0)harness::failCreate=std::atoi(mode+7);
 if(std::strncmp(mode,"enable-",7)==0)harness::failEnable=std::atoi(mode+7);
 if(std::strcmp(mode,"apply")==0)harness::failApply=true;
 if(std::strncmp(mode,"party-signature-",16)==0){const int i=std::atoi(mode+16);if(i<0||i>=static_cast<int>(std::size(partyGuardAddresses)))return 2;image[partyGuardAddresses[i]]^=1;}
 if(std::strncmp(mode,"abilities-signature-",20)==0){const int i=std::atoi(mode+20);if(i<0||i>=static_cast<int>(std::size(abilitiesGuardAddresses)))return 2;image[abilitiesGuardAddresses[i]]^=1;}
 const bool trace=std::strcmp(mode,"trace")==0;
 SetEnvironmentVariableA("KH2COOP_ITEMS_ADMISSION_TRACE",trace?"1":(std::strcmp(mode,"trace-nonexact")==0?"10":nullptr));
 SetEnvironmentVariableA("KH2COOP_PARTY_ADMISSION_TRACE",trace?"1":nullptr);
 SetEnvironmentVariableA("KH2COOP_ABILITIES_ADMISSION_TRACE",trace?"1":(std::strcmp(mode,"trace-nonexact")==0?"10":nullptr));
 const bool installed=empty::Install(base,harness::Log);
 const bool paired=std::strncmp(mode,"paired-",7)==0;
 const bool positive=std::strcmp(mode,"positive")==0||trace||std::strcmp(mode,"trace-nonexact")==0||paired;
 Check(installed==positive&&empty::Ready()==positive,"installation result follows deterministic mode");
 if(paired){
  const std::uint16_t companion=std::strcmp(mode,"paired-donald")==0?0x5C:0x5D;
  members[0]=0x5A;members[1]=companion;members[2]=0x54;empty::ArmCompanionTuple({0x5A,companion,0x54});
  std::array<std::uint8_t,0x100> menu{};const int count=3;std::memcpy(menu.data(),&count,4);
  Entry(menu.data(),0,0,14);Entry(menu.data(),1,1,companion==0x5C?2:3);Entry(menu.data(),2,2,1);
  const auto menuPtr=reinterpret_cast<std::uintptr_t>(menu.data());std::memcpy(image+0xBEEC28,&menuPtr,8);
  const std::uint32_t page=1;std::memcpy(image+0xBEE64C,&page,4);
  using AdmissionFn=std::uint8_t(__fastcall*)(std::int32_t);
  const auto admissionHook=reinterpret_cast<AdmissionFn>(harness::hooks[5].detour);
  harness::caller=base+0x2F62EC;Check(admissionHook(2)==1&&admissionHook(1)==1&&admissionHook(-5)==1,"dense chosen player native AI and -5 shortcut keep native Items delegation");
  const auto history=reinterpret_cast<harness::Fn>(harness::hooks[4].detour);
  for(const auto caller:{0x36A46Au,0x36A727u}){harness::caller=base+caller;Check(history(menu.data(),0)==0&&history(menu.data(),2)==0&&history(menu.data(),1)==(companion==0x5C?2:3),"both chosen companion-history callers skip players and preserve AI");}
  const auto selectorHook=reinterpret_cast<harness::Fn>(harness::hooks[0].detour);const std::array<std::uint8_t,4> row{0,1,2,18};
  Check(selectorHook(row.data(),1)==1,"dense chosen row receives no empty projection");
  const std::array<std::uint16_t,3> native{0x54,0x5C,0x5D};
  Check(!empty::Shutdown()&&empty::Ready(),"paired A2 missing proof retains six guards");
  Check(!empty::Shutdown(&native)&&empty::Ready(),"paired A2 still-owned tuple refuses native proof mismatch");
  DWORD previous=0,ignored=0;Check(VirtualProtect(members,6,PAGE_NOACCESS,&previous)!=0,"paired A2 unreadable setup");
  Check(!empty::Shutdown(&native)&&empty::Ready(),"paired A2 read fault retains ownership/readiness");
  Check(VirtualProtect(members,6,previous,&ignored)!=0,"paired A2 restore readable page");
  harness::caller=base+0x36A46A;Check(history(menu.data(),2)==0,"paired history remains guarded after failed six-hook teardown");
  members[0]=native[0];members[1]=native[1];members[2]=native[2];
  Check(empty::Shutdown(&native)&&!empty::Ready(),"paired positive all-three restoration retires six guards");
  for(const auto& h:harness::hooks)Check(!h.enabled,"paired positive restoration disables every guard including Items");
  Check(harness::receipts.empty(),"dense paired path emits no empty-seat refusal witness");
  Check(std::memcmp(save,before.data(),saveSize)==0,"paired complete synthetic SAVE remains unchanged");
  std::printf("checks=%u failures=%u mode=%s\n",checks,failures,mode);return failures?1:0;
 }
 if(!positive){if((std::strncmp(mode,"signature-",10)==0||std::strncmp(mode,"party-signature-",16)==0||std::strncmp(mode,"abilities-signature-",20)==0))Check(harness::created==0&&!empty::Ready(),"signature mismatch refuses before hook creation and readiness");Check(harness::removed==0,"failure retains all created trampolines");Check(empty::RetainsMinHookResources()==(harness::created!=0),"retained resource receipt follows actual created callbacks");for(unsigned i=0;i<harness::created;++i)Check(!harness::hooks[i].enabled,"rollback disables every created hook");
  std::array<std::uint8_t,0x100> menu{};const int count=2;std::memcpy(menu.data(),&count,4);Entry(menu.data(),1,2,1);empty::Arm(0x5A,0x54);
  if(harness::created>3){harness::caller=base+0x3065CD;const auto fn=reinterpret_cast<harness::Fn>(harness::hooks[3].detour);Check(fn(menu.data(),1)==0x319,"retained alias callback delegates after failed install");}
  if(harness::created>4){harness::caller=base+0x36A46A;const auto fn=reinterpret_cast<harness::Fn>(harness::hooks[4].detour);Check(fn(menu.data(),1)==1,"retained history callback delegates after failed install");}
  const bool hadRetained=empty::RetainsMinHookResources();
  const bool retried=empty::Install(base,nullptr);
  Check(!hadRetained||!retried,"retired installation cannot reuse retained resources");
  if(std::strcmp(mode,"create-0")==0)Check(!hadRetained&&retried&&empty::Ready(),"first-create refusal with no retained callbacks permits a clean retry");}
 else {
  const auto aliasHook=reinterpret_cast<harness::Fn>(harness::hooks[3].detour);const auto historyHook=reinterpret_cast<harness::Fn>(harness::hooks[4].detour);
  std::array<std::uint8_t,0x100> menu{};int count=2;std::memcpy(menu.data(),&count,4);Entry(menu.data(),0,0,14);Entry(menu.data(),1,2,1);const auto menuBefore=menu;
  harness::caller=base+0x3065CD;Check(harness::Alias(menu.data(),1)==0x319,"legacy compact index1 selects alias319 for missing original seat1");Check(harness::RawKey(menu.data(),1)==1&&((1-2)*0x30)==-48,"legacy local player key derives negative companion history index");
  Check(aliasHook(menu.data(),1)==0x319,"unowned alias delegates compact argument unchanged");empty::Arm(0x5A,0x54);
  Check(aliasHook(menu.data(),1)==0x31A&&harness::lastAlias==2,"owned pause local compact1 translates to originalseat2");Check(aliasHook(menu.data(),0)==0x20000236&&harness::lastAlias==0,"owned pause remote compact0 preserves originalseat0");
  SetLastError(0x12345678);aliasHook(menu.data(),1);Check(GetLastError()==0x12345678,"alias wrapper preserves native caller last error");
  for(const int bad:{0,-1,5}){std::memcpy(menu.data(),&bad,4);Check(aliasHook(menu.data(),1)==0x319,"invalid compact count leaves original argument unchanged");}std::memcpy(menu.data(),&count,4);
  Check(aliasHook(menu.data(),-1)==0&&harness::lastAlias==-1,"negative compact index delegates");Check(aliasHook(menu.data(),2)==0x31A&&harness::lastAlias==2,"outside compact count delegates original index");
  Entry(menu.data(),1,1,1);Check(aliasHook(menu.data(),1)==0x319,"missing originalseat1 cannot be projected");Entry(menu.data(),1,3,1);Check(aliasHook(menu.data(),1)==0x319,"unrelated originalseat3 cannot be projected");Entry(menu.data(),1,-1,1);Check(aliasHook(menu.data(),1)==0x319,"negative saved seat cannot be projected");Entry(menu.data(),1,2,1);
  std::uintptr_t nullStatus=0;std::memcpy(menu.data()+0x40,&nullStatus,8);Check(aliasHook(menu.data(),1)==0x319,"missing status pointer prevents portrait projection");Entry(menu.data(),1,2,1);
  Check(aliasHook(nullptr,1)==0x319,"unreadable menu delegates without wrapper dereference fault");
  harness::caller=base+0x3211A9;Check(aliasHook(menu.data(),1)==0x319&&harness::lastAlias==1,"unqualified Party caller delegates without global renumbering");
  for(const auto caller:{0x36A46Au,0x36A727u}){harness::caller=base+caller;Check(historyHook(menu.data(),0)==0&&historyHook(menu.data(),1)==0,"both companion loops skip raw Roxas14 and Sora1");Entry(menu.data(),1,2,3);Check(historyHook(menu.data(),1)==3,"legitimate companion key remains native");Entry(menu.data(),1,2,-1);Check(historyHook(menu.data(),1)==-1,"unknown signed key remains native");Entry(menu.data(),1,2,1);}
  harness::caller=base+0x36A120;Check(historyHook(menu.data(),1)==1,"other history callers preserve player key");harness::caller=base+0x36A46A;SetLastError(0x76543210);const auto previousCalls=harness::keyCalls;historyHook(menu.data(),1);Check(harness::keyCalls==previousCalls+1&&GetLastError()==0x76543210,"history wrapper calls native reader once and preserves last error");
  harness::caller=base+0x3065CD;members[0]=0x54;Check(aliasHook(menu.data(),1)==0x319,"tuple mismatch prevents projection");members[0]=0x5A;
  using AdmissionFn=std::uint8_t(__fastcall*)(std::int32_t);
  const auto admissionHook=reinterpret_cast<AdmissionFn>(harness::hooks[5].detour);
  std::array<std::uint8_t,0x100> state{};const auto statePtr=reinterpret_cast<std::uintptr_t>(state.data()),menuPtr=reinterpret_cast<std::uintptr_t>(menu.data());
  std::memcpy(image+0xBF00F0,&statePtr,8);std::memcpy(image+0xBEEC28,&menuPtr,8);std::uint32_t page=1;std::memcpy(image+0xBEE64C,&page,4);harness::caller=base+0x2F62EC;
  for(const std::int16_t playerKey:{std::int16_t{1},std::int16_t{14}}){Entry(menu.data(),1,2,playerKey);Check(admissionHook(1)==0,"first Items nonzero compact Sora/Roxas refused");}
  if(trace){char expected[512]{};std::snprintf(expected,sizeof(expected),"caller=2F62EC state=1 selection=1 menu=%llX count=2 seat=2 key=1 status=12345678 owner=5A/0/54",static_cast<unsigned long long>(menuPtr));Check(harness::receipts.size()==2&&harness::receipts[0].find("refused seq=1 tick=")!=std::string::npos&&harness::receipts[0].find(expected)!=std::string::npos&&harness::receipts[1].find("key=14")!=std::string::npos,"opt-in exact receipt proves identified consumed refusal");}
  else Check(harness::receipts.empty(),"default/nonexact opt-in emits no diagnostic and leaves refusal unchanged");
  const auto traceBeforeDelegates=harness::receipts.size();
  Check(admissionHook(2)==1,"state1 last Stock row delegates outside compact character census");
  for(const int command:{-13,-11,-7,-6,-4,-2,-1,0})Check(admissionHook(command)==1,"Cancel/other negative commands and compact0 remain native");
  for(const std::int16_t ai:{std::int16_t{2},std::int16_t{3}}){Entry(menu.data(),1,2,ai);Check(admissionHook(1)==1,"native Donald/Goofy key delegates");}Entry(menu.data(),1,2,1);
  harness::caller=base+0x999;Check(admissionHook(1)==1,"unrelated admission caller delegates");harness::caller=base+0x2F62EC;
  page=0x10;std::memcpy(image+0xBEE64C,&page,4);Check(admissionHook(1)==1,"other native page delegates");page=1;std::memcpy(image+0xBEE64C,&page,4);
  members[0]=0x54;Check(admissionHook(1)==1,"physical ownership mismatch delegates");members[0]=0x5A;
  for(const int bad:{0,-1,5}){std::memcpy(menu.data(),&bad,4);Check(admissionHook(1)==1,"invalid native menu count delegates");}std::memcpy(menu.data(),&count,4);
  Check(admissionHook(2)==1,"out of range compact delegates");Entry(menu.data(),1,1,1);Check(admissionHook(1)==1,"unexpected original seat delegates");Entry(menu.data(),1,2,1);
  std::uintptr_t zero=0;std::memcpy(menu.data()+0x40,&zero,8);Check(admissionHook(1)==1,"null status delegates");Entry(menu.data(),1,2,1);
  Check(harness::receipts.size()==traceBeforeDelegates,"delegated Stock/first/Cancel/nativeAI/wrongcaller/page/tuple/malformed cases do not emit refusal receipts");
  SetLastError(0x12341234);const auto oldCalls=harness::admissionCalls;Check(admissionHook(1)==0&&harness::admissionCalls==oldCalls&&GetLastError()==0x12341234,"refusal preserves last error and never calls original admission");

  // Actual callback and retained native helper + feedback/refusal comparison.
  const auto partyHook=reinterpret_cast<AdmissionFn>(harness::hooks[6].detour);
  page=0;std::memcpy(image+0xBEE64C,&page,4);
  std::uint16_t mask=0x0F;std::memcpy(image+0xBEEC20,&mask,2);
  const auto oldReceiptCount=harness::receipts.size();
  Check(partyHook(3)==0,"ordinary root Party feature3 is refused under owned sparse-middle tuple");
  if(trace)Check(harness::receipts.back().find("[partyempty-party] refused seq=1 tick=")!=std::string::npos&&harness::receipts.back().find("state=0 selection=3 mask=F feature=3 owner=5A/0/54")!=std::string::npos,"Party consumed refusal receipt identifies root selection mask and owner");
  for(const std::uint16_t m:{std::uint16_t{8},std::uint16_t{9},std::uint16_t{10},std::uint16_t{12},std::uint16_t{0xFF}}){mask=m;std::memcpy(image+0xBEEC20,&mask,2);int selected=0;for(unsigned bit=0;bit<3;++bit)if(mask&(1<<bit))++selected;Check(partyHook(selected)==0,"Party nth-set-bit mapping refuses even when compact index differs from3");}
  mask=0x0F;std::memcpy(image+0xBEEC20,&mask,2);
  for(const int selected:{-4,-2,-1,0,2,4,8})Check(partyHook(selected)==1,"Party callback delegates Cancel invalid and other feature selections");
  page=1;std::memcpy(image+0xBEE64C,&page,4);Check(partyHook(3)==1,"Party callback delegates outside root state");page=0;std::memcpy(image+0xBEE64C,&page,4);
  harness::caller=base+0x111;Check(partyHook(3)==1,"unrelated callback caller delegates");harness::caller=base+0x2F62EC;
  members[0]=0x54;Check(partyHook(3)==1,"Party physical ownership mismatch delegates");members[0]=0x5A;
  DWORD protectedOld=0,protectedIgnored=0;Check(VirtualProtect(image+0xBEEC20,2,PAGE_NOACCESS,&protectedOld)!=0,"mask unreadable setup");Check(partyHook(3)==1,"Party unreadable state/mask delegates without fault");Check(VirtualProtect(image+0xBEEC20,2,protectedOld,&protectedIgnored)!=0,"mask readability restored");
  empty::ArmTuple({0x5A,0x54,0},2);members[1]=0x54;members[2]=0;Check(partyHook(3)==1,"trailing-empty layout outside sparse-middle scope delegates");members[1]=0;members[2]=0x54;empty::Arm(0x5A,0x54);
  SetLastError(0x23456789);Check(partyHook(3)==0&&GetLastError()==0x23456789,"Party refusal preserves last error including logger effects");
  // Seed real native input, feedback and comparison bodies plus only audio no-op.
  SeedNativeFlow(image);
  using NativeInputParty=std::uint64_t(__fastcall*)(int,char,std::uint8_t,AdmissionFn,std::uint8_t);
  const auto nativeParty=reinterpret_cast<NativeInputParty>(image+0x2F6270);
  using DecideRoot=unsigned(__fastcall*)(int,int);
  const auto rootDecision=reinterpret_cast<DecideRoot>(image+0x303900);
  const auto refusedSound=static_cast<int>(nativeParty(3,0,1,partyHook,0));
  Check(rootDecision(refusedSound,3)==2,"actual native feedback4 comparison takes common refusal tail before descriptor or state writes");
  const auto allowedSound=static_cast<int>(nativeParty(0,0,1,partyHook,0));
  Check(rootDecision(allowedSound,0)==1,"actual native feedback2 comparison reaches positive root transition boundary");
  for(const int cancel:{-2,-4}){const auto prior=harness::receipts.size();const auto sound=static_cast<int>(nativeParty(cancel,0,1,partyHook,0));Check(rootDecision(sound,cancel)==3&&harness::receipts.size()==prior,"actual native Cancel bypass preserves root cancel tail without refusal");}
  for(int repeat=0;repeat<30;++repeat)Check(partyHook(3)==0,"Party trace cap never changes refusal");
  unsigned partyReceipts=0;for(const auto& receipt:harness::receipts)if(receipt.find("[partyempty-party]")==0)++partyReceipts;
  Check(partyReceipts==(trace?16u:0u),"Party lifetime receipt cap exactly16 and default disabled");
  empty::NativeResolved();Check(rootDecision(static_cast<int>(nativeParty(3,0,1,partyHook,0)),3)==1,"restored native ownership delegates Party positive entry boundary");empty::Arm(0x5A,0x54);

  // New bounded Abilities root and Items(-5) ingress refusals share no Party receipt budget.
  const auto beforeAbilities=harness::receipts.size();
  Check(rootDecision(static_cast<int>(nativeParty(-1,0,1,partyHook,0)),-1)==2&&harness::receipts.size()==beforeAbilities,"ignored root input reaches native common tail without a consumed refusal witness");
  mask=0x0F;std::memcpy(image+0xBEEC20,&mask,2);
  Check(partyHook(1)==0,"Abilities root feature1 refused under exact sparse-middle ownership");
  for(const std::uint16_t m:{std::uint16_t{2},std::uint16_t{3},std::uint16_t{0xFE},std::uint16_t{0xFF}}){mask=m;std::memcpy(image+0xBEEC20,&mask,2);const int selected=(mask&1)?1:0;Check(partyHook(selected)==0,"Abilities nth-set-bit projection handles root mask without fixed compact index");}
  mask=0x0F;std::memcpy(image+0xBEEC20,&mask,2);
  Check(rootDecision(static_cast<int>(nativeParty(1,0,1,partyHook,0)),1)==2,"actual native root Abilities refusal reaches common tail before transition");
  for(const std::uint16_t absent:{std::uint16_t{0},std::uint16_t{1},std::uint16_t{4}}){std::memcpy(image+0xBEEC20,&absent,2);Check(partyHook(0)==1,"root masks without Abilities feature delegate");}std::memcpy(image+0xBEEC20,&mask,2);
  harness::caller=base+0x111;Check(partyHook(1)==1,"root Abilities wrong caller delegates");harness::caller=base+0x2F62EC;
  page=15;std::memcpy(image+0xBEE64C,&page,4);Check(partyHook(1)==1,"already-open Abilities state delegates outside root ingress");page=0;std::memcpy(image+0xBEE64C,&page,4);
  members[0]=0x54;Check(partyHook(1)==1,"root Abilities ownership mismatch delegates");members[0]=0x5A;

  page=1;std::memcpy(image+0xBEE64C,&page,4);
  Check(admissionHook(-5)==0,"Items state1 accepted -5 shortcut refused before Abilities builder");
  SeedItemsShortcutFlow(image);
  using DecideItems=unsigned(__fastcall*)(int,int);
  const auto itemsDecision=reinterpret_cast<DecideItems>(image+0x34E100);
  const auto ignoredBefore=harness::receipts.size();Check(itemsDecision(static_cast<int>(nativeParty(-1,0,1,admissionHook,0)),-1)==2&&harness::receipts.size()==ignoredBefore,"ignored Items input retains native tail without forging shortcut refusal witness");
  Check(itemsDecision(static_cast<int>(nativeParty(-5,0,1,admissionHook,0)),-5)==2,"actual native Items feedback/refusal branch dominates accepted -5 cleanup");
  for(const int cancel:{-2,-4}){const auto prior=harness::receipts.size();Check(itemsDecision(static_cast<int>(nativeParty(cancel,0,1,admissionHook,0)),cancel)==3&&harness::receipts.size()==prior,"actual native Items Cancel bypass keeps both original Cancel destinations");}
  for(const unsigned wrongPage:{0u,2u,15u}){page=wrongPage;std::memcpy(image+0xBEE64C,&page,4);Check(admissionHook(-5)==1,"Items -5 delegates root equipment and already-open Abilities states");}page=1;std::memcpy(image+0xBEE64C,&page,4);
  harness::caller=base+0x111;Check(admissionHook(-5)==1,"Items -5 wrong caller delegates");harness::caller=base+0x2F62EC;
  members[0]=0x54;Check(admissionHook(-5)==1,"Items -5 ownership mismatch delegates");members[0]=0x5A;
  empty::ArmTuple({0x5A,0x54,0},2);members[1]=0x54;members[2]=0;Check(admissionHook(-5)==1,"Items -5 trailing-empty layout delegates outside bounded scope");members[1]=0;members[2]=0x54;empty::Arm(0x5A,0x54);
  Check(VirtualProtect(image+0xBEEC20,2,PAGE_NOACCESS,&protectedOld)!=0,"Items -5 unreadable mask setup");Check(admissionHook(-5)==1,"Items -5 unavailable bracket reads delegate without fault");Check(VirtualProtect(image+0xBEEC20,2,protectedOld,&protectedIgnored)!=0,"Items -5 readability restored");
  SetLastError(0x3456789A);Check(admissionHook(-5)==0&&GetLastError()==0x3456789A,"Items -5 refusal including logger preserves LastError");
  empty::NativeResolved();Check(itemsDecision(static_cast<int>(nativeParty(-5,0,1,admissionHook,0)),-5)==1,"native restored Items -5 reaches original accepted shortcut boundary");empty::Arm(0x5A,0x54);
  for(int repeat=0;repeat<30;++repeat)Check(admissionHook(-5)==0,"Items Abilities receipt cap never changes refusal");
  page=0;std::memcpy(image+0xBEE64C,&page,4);for(int repeat=0;repeat<30;++repeat)Check(partyHook(1)==0,"root Abilities receipt cap never changes refusal");
  unsigned rootAbilities=0,itemsAbilities=0;
  for(const auto& receipt:harness::receipts){if(receipt.find("[partyempty-abilities-root]")==0)++rootAbilities;if(receipt.find("[partyempty-abilities-items]")==0)++itemsAbilities;}
  Check(rootAbilities==(trace?16u:0u)&&itemsAbilities==(trace?16u:0u),"Abilities root and shortcut have separate capped counters and exact opt-in");
  if(trace){Check(harness::receipts[beforeAbilities].find("[partyempty-abilities-root] refused seq=1")!=std::string::npos&&harness::receipts[beforeAbilities].find("state=0 selection=1 mask=F feature=1 owner=5A/0/54")!=std::string::npos,"Abilities root positive consumed receipt identifies exact admitted domain");bool shortcutReceipt=false;for(const auto& receipt:harness::receipts)shortcutReceipt=shortcutReceipt||(receipt.find("[partyempty-abilities-items] refused seq=1")!=std::string::npos&&receipt.find("state=1 selection=-5 mask=F feature=1 owner=5A/0/54")!=std::string::npos);Check(shortcutReceipt,"Abilities Items positive consumed receipt identifies shortcut domain");}
  harness::receipts.resize(oldReceiptCount); // isolate existing Items-only trace assertions
  page=1;std::memcpy(image+0xBEE64C,&page,4);
  // Execute the original pinned 2F6270 input handler with a feedback-only boundary double.
  std::memcpy(image+0x2F6270,nativeInput,sizeof(nativeInput));const auto feedback=reinterpret_cast<std::uintptr_t>(harness::Feedback);std::uint8_t jump[]{0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};std::memcpy(jump+2,&feedback,8);std::memcpy(image+0x2EB740,jump,sizeof(jump));FlushInstructionCache(GetCurrentProcess(),image,0x400000);
  using NativeInput=std::uint64_t(__fastcall*)(int,char,std::uint8_t,AdmissionFn,std::uint8_t);const auto input=reinterpret_cast<NativeInput>(image+0x2F6270);
  Check(input(1,0,1,admissionHook,1)==4,"actual native input callback refusal selects feedback4");Check(input(0,0,1,admissionHook,1)==2,"actual native input allowed row selects feedback2");Check(input(-2,0,1,admissionHook,1)==3,"actual native Cancel preserves feedback3");
  for(int repeat=0;repeat<40;++repeat)Check(admissionHook(1)==0,"diagnostic cap cannot change admission refusal");
  Check(harness::receipts.size()==(trace?16u:0u),"process lifetime diagnostic cap is exactly16 or disabled");
  empty::NativeResolved();Check(input(1,0,1,admissionHook,1)==2,"physical native restore retires admission and restores original feedback2");empty::Arm(0x5A,0x54);harness::caller=base+0x2F62EC;admissionHook(1);Check(harness::receipts.size()==(trace?16u:0u),"new ownership/load does not renew diagnostic cap");harness::caller=base+0x3065CD;
  Check(!empty::Shutdown()&&empty::Ready(),"physical empty ownership retains guards on shutdown");Check(aliasHook(menu.data(),1)==0x31A,"guard remains after intent-independent shutdown refusal");
  empty::NativeResolved();Check(aliasHook(menu.data(),1)==0x319,"native resolver retirement restores delegation");harness::caller=base+0x36A727;Check(historyHook(menu.data(),1)==1,"native resolver retirement restores original history key");members[1]=0x5C;Check(empty::Shutdown()&&!empty::Ready(),"positive physical restore retires readiness");Check(harness::removed==0&&empty::RetainsMinHookResources(),"shutdown retains trampoline lifetime");for(const auto& h:harness::hooks)Check(!h.enabled,"retirement disables each menu and row detour");Check(!empty::Install(base,nullptr),"retired guards refuse reinitialization");
  Check(menu==menuBefore,"menu entries remain entirely read-only");
 }
 Check(std::memcmp(save,before.data(),saveSize)==0,"entire synthetic SAVE remains unchanged");
 Check(positive||harness::receipts.empty(),"failed signatures/install cannot emit consumed-refusal diagnostic");
 std::printf("checks=%u failures=%u mode=%s\n",checks,failures,mode);return failures?1:0;
}
