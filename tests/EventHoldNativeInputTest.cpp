#include "EventHoldNative.hpp"
#include "kh2coop/KH2Offsets.hpp"
#include <cstdio>
#include <cstring>
#include <string_view>
namespace kh2coop::inject::spawncontroller { bool IsDiagnosticGameThread(){return true;} }
namespace n=kh2coop::inject::eventholdnative;
namespace e=kh2coop::eventhold;
using namespace kh2coop;
unsigned checks=0,failures=0;
void check(bool yes,const char* name){++checks;if(!yes){++failures;std::printf("FAIL %s\n",name);}}
int main(int argc,char** argv){
 const std::string_view mode=argc>1?argv[1]:"normal";
 const auto memory=VirtualAlloc(nullptr,0x3000000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
 check(memory!=nullptr,"owned fake native region allocated");if(!memory)return 1;
 const auto base=reinterpret_cast<uintptr_t>(memory);unsigned char data[64]{};
 *reinterpret_cast<uintptr_t*>(base+offsets::INPUT_STRUCT_PTR)=reinterpret_cast<uintptr_t>(data);
 auto& menu=*reinterpret_cast<unsigned char*>(base+offsets::OPEN_MENU);
 menu=255;*reinterpret_cast<unsigned char*>(base+offsets::IN_FIELD)=1;
 SetEnvironmentVariableA("KH2COOP_EVENT_HOLD_CONTROL",mode=="off"?nullptr:"1");n::Install(base,nullptr);
 auto fresh=[&]{std::memset(data+offsets::input::RAW_SLOT0,77,6);};
 auto tick=[&]{fresh();const bool entered=n::EnterInput();if(entered){n::ApplyInput(data);n::LeaveInput();}};
 auto buttons=[&]{return *reinterpret_cast<unsigned short*>(data+offsets::input::RAW_SLOT0);};
 if(mode=="off"){fresh();check(!n::Enabled()&&!n::ApplyInput(data)&&buttons()==0x4d4d,"default off leaves fresh physical untouched");}
 else{
 e::Channel runtime;check(runtime.Open(GetCurrentProcessId()),"independent runtime view opened");
 e::Scope scope{};std::memcpy(scope.session,"0123456789abcdef0123456789abcdef",32);scope.generation=2;scope.slot=1;scope.hostConnection=1;scope.selfConnection=2;scope.hostDelivery=1;scope.targetDelivery=1;
 check(runtime.Bind(scope,GetTickCount64()),"runtime binding");
 RoomTransition room{};room.epoch=1;room.worldId=4;room.roomId=26;
 n::Observe(scope,e::Kind::Transition,3,1,&room,0);n::OwnerAck(scope,1,true,true);
 check(runtime.Arm(GetTickCount64()),"test owner baseline arm");tick();
 e::Command acquire{};acquire.scope=scope;acquire.hostSourceSerial=8;acquire.kind=e::Kind::Acquire;acquire.epoch=1;acquire.eventProgram=1;
 if(mode=="unowned")menu=10;
 check(runtime.Publish(acquire,GetTickCount64()),"acquire published");tick();
 if(mode=="unowned")check(buttons()==0x4d4d&&!n::HoldingInput()&&runtime.Reason()!=e::Abort::None,"preexisting pause is not closed or neutralized");
 else{
 check(buttons()==8&&n::HoldingInput(),"one native Start opens owned pause");tick();check(buttons()==0,"one release edge");menu=10;tick();
 for(unsigned i=0;i<5;++i){tick();check(buttons()==0&&n::HoldingInput(),"pause held without repeated Start");}
 if(mode=="abort"){runtime.Invalidate(e::Abort::Shutdown);tick();check(buttons()==0x4d4d&&!n::HoldingInput()&&menu==10,"abort restores physical without blindly closing menu");}
 else{
 e::Command transition{};transition.scope=scope;transition.kind=e::Kind::Transition;transition.hostSourceSerial=7;transition.epoch=2;transition.world=8;transition.room=12;transition.door=50;transition.eventProgram=22;
 check(runtime.Publish(transition,GetTickCount64()),"successor transition with earlier source published");tick();check(buttons()==8&&n::HoldingInput(),"wake closes owned pause but holds gameplay");tick();menu=255;tick();
 e::Command release{};release.scope=scope;release.kind=e::Kind::Release;release.hostSourceSerial=12;release.epoch=2;release.eventProgram=22;
 check(runtime.Publish(release,GetTickCount64()),"release published");tick();
 n::OwnerAck(scope,2,true,true);tick();check(n::HoldingInput()&&buttons()==0,"transport release and unconsumed owner facts do not release input");
 room.epoch=2;room.worldId=8;room.roomId=12;room.door=50;room.eventProgram=22;
 n::Observe(scope,e::Kind::Acquire,8,1,nullptr,1);n::Observe(scope,e::Kind::Transition,7,2,&room,22);n::Observe(scope,e::Kind::Release,12,2,nullptr,22);
 n::OwnerAck(scope,2,true,false);tick();check(n::HoldingInput()&&buttons()==0,"owner applied order without convergence stays held");
 n::OwnerAck(scope,2,true,true);tick();check(n::HoldingInput()&&buttons()==0,"first converged boundary stays neutral");tick();check(!n::HoldingInput()&&buttons()==0,"last held sample discarded on final release");tick();check(buttons()==0x4d4d,"next callback restores newly collected physical input");
 }
 }
 }
 n::Disable();VirtualFree(memory,0,MEM_RELEASE);std::printf("mode=%.*s checks=%u failures=%u\n",static_cast<int>(mode.size()),mode.data(),checks,failures);return failures?1:0;
}
