#include "EventHoldNative.hpp"
#include "EventHoldInputState.hpp"
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
 {// VUH-2268: pure hold rules. A refused acquire changes nothing, a closed
  // pause waiting on the owner gives input back 10 s after the release, and a
  // new event supersedes that wait.
  using S=n::InputState;S s;S::Sample ok{};ok.now=1000;ok.safeToPause=true;ok.healthy=true;
  S::Sample unsafe=ok;unsafe.safeToPause=false;
  check(!s.Acquire(unsafe)&&s.Current()==S::Phase::Idle&&!s.Holding(),"unsafe acquire is skipped, not failed");
  check(s.Acquire(ok),"safe acquire");S::Sample at=ok;
  auto step=[&](unsigned menuNow,std::uint64_t now){at.menu=menuNow;at.now=now;return s.Tick(at);};
  step(255,1001);step(255,1002);step(10,1003);check(s.Current()==S::Phase::Paused,"paused");
  s.Wake();s.ReleaseReceived(2000);step(10,2001);step(10,2002);step(255,2003);
  check(s.Current()==S::Phase::OwnerWait,"waiting on owner after the pause closes");
  auto held=step(255,11999);check(held.neutral&&!held.released&&s.Holding(),"held before the release timeout");
  auto out=step(255,12000);check(out.released&&out.timedOut&&!s.Holding(),"input returned at the release timeout");
  check(s.Acquire(ok),"next hold works after a timeout");step(255,12001);step(255,12002);step(10,12003);
  s.Wake();step(10,12004);step(10,12005);step(255,12006);
  check(s.Current()==S::Phase::OwnerWait&&s.Supersede()&&!s.Holding()&&s.Acquire(ok),"a new event supersedes an owner wait");}
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
 if(mode=="elsewhere")n::OwnerAck(scope,1,true,false);
 check(runtime.Publish(acquire,GetTickCount64()),"acquire published");tick();
 if(mode=="unowned")check(buttons()==0x4d4d&&!n::HoldingInput()&&runtime.Reason()==e::Abort::None,"preexisting pause skips the hold without closing it or switching holds off");
 else if(mode=="elsewhere")check(buttons()==0x4d4d&&!n::HoldingInput()&&runtime.Reason()==e::Abort::None,"a client not converged in the host's room skips the hold without pausing");
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
 // One regression: a normal room change between two completed host events
 // must not poison the channel, and the second hold must not inherit Wake.
 transition.hostSourceSerial=14;transition.epoch=3;
 check(runtime.Publish(transition,GetTickCount64()),"ordinary transition between events published");tick();
 room.epoch=3;n::Observe(scope,e::Kind::Transition,14,3,&room,22);n::OwnerAck(scope,3,true,true);
 acquire.hostSourceSerial=16;acquire.epoch=3;
 check(runtime.Publish(acquire,GetTickCount64()),"second event acquire published");tick();
 check(buttons()==8&&n::HoldingInput()&&runtime.Reason()==e::Abort::None,"second event opens a fresh owned pause after ordinary transition");
 tick();menu=10;tick();tick();check(buttons()==0&&n::HoldingInput(),"second event stays paused without stale Wake");
 release.hostSourceSerial=18;release.epoch=3;
 check(runtime.Publish(release,GetTickCount64()),"same-room skipped event release published");tick();tick();menu=255;tick();
 n::Observe(scope,e::Kind::Acquire,16,3,nullptr,1);n::Observe(scope,e::Kind::Release,18,3,nullptr,22);
 n::OwnerAck(scope,3,true,true);tick();tick();tick();
 check(!n::HoldingInput()&&buttons()==0x4d4d,"second event releases only after fresh convergence and boundary");
 }
 }
 }
 n::Disable();VirtualFree(memory,0,MEM_RELEASE);std::printf("mode=%.*s checks=%u failures=%u\n",static_cast<int>(mode.size()),mode.data(),checks,failures);return failures?1:0;
}
