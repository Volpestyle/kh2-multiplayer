#include "EventHoldNative.hpp"
#include "EventHoldInputState.hpp"
#include "kh2coop/EventScenes.hpp"
#include "kh2coop/KH2Offsets.hpp"
#include <array>
#include <vector>
#include <cstdio>
#include <cstring>
#include <string_view>
bool gameThreadKnown=true;
thread_local bool gameThreadCurrent=true;
namespace kh2coop::inject::spawncontroller { bool IsDiagnosticGameThread(){return gameThreadKnown&&gameThreadCurrent;} bool DiagnosticGameThreadKnown(){return gameThreadKnown;} }
// VUH-2359: the native start itself is game memory; the stub records the replay and answers it.
bool replayStarts=true;unsigned replays=0;kh2coop::EventStart replayed{};std::uint16_t replayedProgram=0;
namespace kh2coop::inject::eventstartmirror {
bool Replay(const EventStart& s,std::uint16_t program) noexcept {++replays;replayed=s;replayedProgram=program;return replayStarts;}
}
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
 auto tick=[&]{fresh();const bool entered=n::EnterInput();if(entered){n::ServiceOrphanedPause(data);n::ApplyInput(data);n::LeaveInput();}};
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
  check(s.Current()==S::Phase::OwnerWait&&s.Supersede()&&!s.Holding()&&s.Acquire(ok),"a new event supersedes an owner wait");
  // In a fight Start opens the combat pause (8), not the menu (10): it is the
  // owned pause, held until wake and closed with Start like the menu.
  S c;check(c.Acquire(ok),"combat acquire");at=ok;
  auto cstep=[&](unsigned menuNow,std::uint64_t now){at.menu=menuNow;at.now=now;return c.Tick(at);};
  cstep(255,1001);cstep(255,1002);cstep(8,1003);check(c.Current()==S::Phase::Paused,"combat pause is the owned pause");
  auto cheld=cstep(8,1004);check(cheld.neutral&&!cheld.failed&&c.Holding(),"held in the combat pause");
  c.Wake();auto close=cstep(8,1005);check(close.start,"wake presses Start to close the combat pause");
  cstep(8,1006);cstep(255,1007);check(c.Current()==S::Phase::OwnerWait,"combat pause closed, waiting on owner");
  // VUH-2640 F2: an expired lifetime's own open pause is closed with the hold's close sequence: Start, a
  // neutral frame, then Circle/Start/Cross every UnwindStepMs, until the menu reads closed. Only the hold's
  // own pause (owned, 8 or 10, still showing); another menu before the first press, or a pause still open
  // after the last press, is left alone.
  {using O=n::OrphanedPause;O o;
   auto nat=[](unsigned m,std::int32_t ev=0,std::uintptr_t ctx=0,bool valid=true){O::Native v;v.valid=valid;v.menu=m;v.eventState=ev;v.context=ctx;return v;};
   check(!o.Arm(false,10,nat(10),1000)&&!o.Arm(true,7,nat(7),1000)&&!o.Arm(true,10,nat(255),1000)&&!o.Arm(true,10,nat(8),1000)&&
         !o.Arm(true,10,nat(10,0,0,false),1000)&&!o.Active(),
         "orphaned pause: only the hold's own pause, still showing, from a complete sample");
   check(o.Arm(true,8,nat(8),1000)&&o.Active(),"orphaned combat pause armed");
   auto first=o.Tick(nat(8),1000);check(first.neutral&&first.start&&!first.buttons,"first press: Start");
   auto rel=o.Tick(nat(8),1001);check(rel.neutral&&!rel.start&&!rel.buttons,"then a neutral frame");
   auto wait=o.Tick(nat(8),1002);check(wait.neutral&&!wait.start&&!wait.buttons,"held neutral until the next step");
   auto second=o.Tick(nat(8),1000+S::UnwindStepMs);check(second.buttons==S::CircleButton,"then Circle");
   o.Tick(nat(8),1001+S::UnwindStepMs);
   auto third=o.Tick(nat(8),1000+2*S::UnwindStepMs);check(third.start,"then Start");
   auto closed=o.Tick(nat(255),1001+2*S::UnwindStepMs);check(closed.done&&!closed.neutral&&!o.Active(),"menu closed: input returned");
   O f;f.Arm(true,10,nat(10),5);auto foreign=f.Tick(nat(4),6);check(foreign.gaveUp&&!foreign.neutral&&!f.Active(),"a different menu first: left alone");
   // Regression (Quill r1): after a press, a changed menu (a submenu Circle or Cross could act on), a partial
   // read, or a changed event context stops it with no press and no overlay.
   O cm;cm.Arm(true,10,nat(10),5);cm.Tick(nat(10),5);cm.Tick(nat(10),6);auto changed=cm.Tick(nat(11),5+S::UnwindStepMs);
   check(changed.gaveUp&&!changed.neutral&&!changed.buttons&&!changed.start&&!cm.Active(),"a changed menu after Start: stopped, nothing pressed");
   O r;r.Arm(true,10,nat(10),5);r.Tick(nat(10),5);auto partial=r.Tick(nat(10,0,0,false),6);
   check(partial.gaveUp&&!partial.neutral&&!r.Active(),"a partial native read: stopped, no overlay");
   O x;x.Arm(true,10,nat(10,0,0),5);auto ctx=x.Tick(nat(10,2,0x1234),5);
   check(ctx.gaveUp&&!ctx.neutral&&!x.Active(),"a changed event context: stopped");
   O s2;s2.Arm(true,10,nat(10),5);std::uint64_t t=5;unsigned presses=0;bool gaveUp=false;
   for(int i=0;i<1000&&s2.Active();++i){auto step2=s2.Tick(nat(10),t);presses+=step2.start||step2.buttons;gaveUp=step2.gaveUp;t+=S::UnwindStepMs;}
   check(gaveUp&&presses==S::UnwindPresses+1&&!s2.Active(),"a pause that never closes: bounded, then left alone");}
  // VUH-2537: a close the pause ignores unwinds with Circle, Cross, Start until the menu reads closed.
  // Simulated menu: `pages` modal pages that only Cross advances, then a submenu Circle leaves, then
  // the plain menu that Start closes.
  auto unwind=[&](int pages,bool submenu,std::uint64_t until,std::vector<std::uint16_t>& presses){
    S u;u.Acquire(ok);S::Sample t=ok;unsigned menu=255;bool sub=submenu;
    auto tick=[&](std::uint64_t now){t.menu=menu;t.now=now;auto o=u.Tick(t);
      const std::uint16_t b=static_cast<std::uint16_t>(o.buttons|(o.start?S::StartButton:0));
      if(b){presses.push_back(b);
        if(menu==255&&b==S::StartButton)menu=10;
        else if(pages>0){if(b==S::CrossButton)--pages;}
        else if(sub){if(b==S::CircleButton)sub=false;}
        else if(b==S::StartButton)menu=255;}
      return o;};
    tick(1001);tick(1002);tick(1003);u.Wake();
    S::Output last{};for(std::uint64_t now=1004;now<until&&!last.failed;now+=50)last=tick(now);
    return std::make_pair(u.Current(),last);};
  {std::vector<std::uint16_t> p;auto r=unwind(2,false,20000,p);
   check(r.first==S::Phase::OwnerWait&&!r.second.stuck,"two modal pages: Cross advances them and Start closes the menu");
   bool order=p.size()>=5&&p[0]==S::StartButton&&p[1]==S::StartButton&&p[2]==S::CircleButton&&p[3]==S::StartButton&&
              p[4]==S::CrossButton;
   check(order,"the first close is Start; the unwind tries Circle and Start before any Cross");}
  {std::vector<std::uint16_t> p;auto r=unwind(0,true,20000,p);
   bool cross=false;for(auto b:p)cross=cross||b==S::CrossButton;
   check(r.first==S::Phase::OwnerWait&&!cross,"a submenu backs out on Circle and the menu closes on Start, with no Cross");}
  {std::vector<std::uint16_t> p;auto r=unwind(1000,false,60000,p);
   check(r.first==S::Phase::Failed&&r.second.stuck&&r.second.failed&&!r.second.neutral&&p.size()==2+S::UnwindPresses,
         "a pause that never closes fails after UnwindPresses, reports it and returns the input");}
  S d;check(d.Acquire(ok),"menu-change acquire");at=ok;at.menu=255;at.now=1001;d.Tick(at);at.now=1002;d.Tick(at);
  at.menu=8;at.now=1003;d.Tick(at);at.menu=10;at.now=1004;check(d.Tick(at).failed,"a different menu replacing the owned pause still fails");
  // VUH-2359: a transition into a qualified scene closes the pause, then gives input back at once
  // (no owner release): the friend plays the host's scene and advances its own dialog.
  S f;check(f.Acquire(ok),"scene acquire");at=ok;
  auto fstep=[&](unsigned menuNow,std::uint64_t now){at.menu=menuNow;at.now=now;return f.Tick(at);};
  fstep(255,1001);fstep(255,1002);fstep(10,1003);check(f.Current()==S::Phase::Paused,"scene hold paused");
  f.FollowScene();check(fstep(10,1004).start,"following a scene presses Start to close the pause");
  fstep(10,1005);fstep(255,1006);check(f.Current()==S::Phase::OwnerWait,"pause closed");
  auto back=fstep(255,1007);check(back.released&&!back.timedOut&&!f.Holding(),"scene follow returns input without the owner's release");
  const std::array<std::uint32_t,9> armor{1,0x702,0xFFFFFFFF,0xFFFF,1,0xFFFFFFFF,3,0,0};auto other=armor;other[6]=4;
  check(kh2coop::eventscenes::QualifiedStart(2,7,95,0x343031,9,armor)&&!kh2coop::eventscenes::QualifiedStart(2,7,95,0x343031,9,other)&&
        !kh2coop::eventscenes::QualifiedStart(2,7,97,0x343031,9,armor)&&!kh2coop::eventscenes::QualifiedStart(2,7,95,0x353031,9,armor)&&
        !kh2coop::eventscenes::QualifiedStart(2,7,95,0x343031,2,armor)&&
        kh2coop::eventscenes::QualifiedStart(2,7,97,0x363031,9,armor)&&!kh2coop::eventscenes::QualifiedStart(2,7,99,0x363031,9,armor)&&
        kh2coop::eventscenes::QualifiedStart(2,7,99,0x383031,9,{2,0x402,0x510051,0x51,1,0xFFFFFFFF,3,0,0})&&
        !kh2coop::eventscenes::QualifiedStart(2,7,99,0x383031,9,armor),"only the observed in-field starts, exactly, qualify");
  // VUH-2569: the installed Thorn victory descriptor has one native padding
  // byte. Every semantic bit remains exact, and qualification changes no input.
  const auto& thorn=kh2coop::eventscenes::TwilightThornVictory;
  const auto thornQualifies=[&](const auto& params) {
    return kh2coop::eventscenes::QualifiedStart(thorn.world,thorn.room,thorn.eventProgram,thorn.id,thorn.flags,params)!=nullptr;
  };
  bool padding=true,semantic=true;
  for(unsigned byte=0;byte<256;++byte) {
    auto raw=thorn.params;raw[1]|=static_cast<std::uint32_t>(byte)<<24;const auto saved=raw;
    padding=padding && thornQualifies(raw) && raw==saved;
  }
  for(unsigned arg=0;arg<thorn.params.size();++arg)for(unsigned bit=0;bit<32;++bit) {
    auto changed=thorn.params;changed[arg]^=std::uint32_t{1}<<bit;
    if(arg!=1 || bit<24)semantic=semantic && !thornQualifies(changed);
  }
  check(padding,"Thorn qualification accepts every destination padding byte without changing the descriptor");
  check(semantic,"every changed semantic bit of the Thorn descriptor refuses qualification");
  check(!kh2coop::eventscenes::QualifiedStart(3,34,157,thorn.id,thorn.flags,thorn.params) &&
        !kh2coop::eventscenes::QualifiedStart(2,33,157,thorn.id,thorn.flags,thorn.params) &&
        !kh2coop::eventscenes::QualifiedStart(2,34,156,thorn.id,thorn.flags,thorn.params) &&
        !kh2coop::eventscenes::QualifiedStart(2,34,157,thorn.id+1,thorn.flags,thorn.params) &&
        !kh2coop::eventscenes::QualifiedStart(2,34,157,thorn.id,thorn.flags^1,thorn.params),
        "Thorn qualification requires its exact source, event ID and flags");
  auto armorPadding=armor;armorPadding[1]|=0xA5000000;
  check(!kh2coop::eventscenes::QualifiedStart(2,7,95,0x343031,9,armorPadding),
        "Thorn padding exception does not relax another in-field event");
  check(!kh2coop::eventscenes::QualifiedStageEnd(2,34,157,thorn.id,thorn.flags,thorn.params),
        "Thorn victory remains a host in-field start, not a friend's stage-end request");
  check(kh2coop::eventscenes::Qualified(2,19,0,3,0,1)&&!kh2coop::eventscenes::Qualified(2,19,0,3,0,2)&&
        !kh2coop::eventscenes::Qualified(2,19,1,3,0,1),"the basement entry scene is checked exactly");
  check(kh2coop::eventscenes::Qualified(2,7,0,0,0,3)&&kh2coop::eventscenes::Qualified(2,4,0,81,81,81)&&
        kh2coop::eventscenes::Qualified(18,3,0,83,83,83)&&kh2coop::eventscenes::Qualified(2,19,0,136,136,136)&&
        !kh2coop::eventscenes::Qualified(2,19,50,1,0,2)&&!kh2coop::eventscenes::Qualified(2,13,0,135,135,135)&&
        !kh2coop::eventscenes::Qualified(2,7,0,97,97,97)&&!kh2coop::eventscenes::Qualified(2,7,1,0,0,3),
        "qualified scenes are exact prologue event-only locations");}
 if(mode=="off"){fresh();check(!n::Enabled()&&!n::ApplyInput(data)&&buttons()==0x4d4d,"default off leaves fresh physical untouched");}
 else{
 e::Channel runtime;check(runtime.Open(GetCurrentProcessId()),"independent runtime view opened");
 e::Scope scope{};std::memcpy(scope.session,"0123456789abcdef0123456789abcdef",32);scope.generation=2;scope.slot=1;scope.hostConnection=1;scope.selfConnection=2;scope.hostDelivery=1;scope.targetDelivery=1;
 check(runtime.Bind(scope,GetTickCount64()),"runtime binding");
 // VUH-2359: a New Game binds during the title/opening, before any actor update registers the game
 // thread. The input hook takes no control then, and must not abort the channel for the session.
 gameThreadKnown=false;
 check(!n::EnterInput()&&runtime.Reason()==e::Abort::None,"no registered game thread yet: no control, channel kept");
 gameThreadKnown=true;
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
 // VUH-2359: an acquire carrying the host's in-field start plays it natively instead of the pause:
 // no Start press, no hold, the exact arguments replayed. A refused replay keeps the ordinary hold.
 acquire.hostSourceSerial=20;acquire.epoch=3;acquire.eventProgram=95;acquire.startWorld=2;acquire.startRoom=7;
 acquire.startId=0x343031;acquire.startFlags=9;acquire.startParams={1,0x702,0xFFFFFFFF,0xFFFF,1,0xFFFFFFFF,3,0,0};
 check(runtime.Publish(acquire,GetTickCount64()),"in-field event acquire published");tick();
 check(replays==1&&replayed.id==0x343031&&replayed.flags==9&&replayed.worldId==2&&replayed.roomId==7&&replayedProgram==95&&
       replayed.params==acquire.startParams,"the host's start is replayed with its exact arguments");
 check(!n::HoldingInput()&&buttons()==0x4d4d&&runtime.Reason()==e::Abort::None,"a replayed in-field event takes no hold and no Start");
 release.hostSourceSerial=21;release.epoch=3;
 check(runtime.Publish(release,GetTickCount64()),"the host's release after a replayed event published");tick();
 check(!n::HoldingInput()&&buttons()==0x4d4d,"a release after a replayed event has nothing to give back");
 acquire.hostSourceSerial=22;acquire.eventProgram=157;acquire.startWorld=2;acquire.startRoom=34;
 acquire.startId=0x353133;acquire.startFlags=0x12;
 acquire.startParams={2,0xA5000002,0x00330033,0x33,1,0xFFFFFFFF,3,0,0};
 check(runtime.Publish(acquire,GetTickCount64()),"Thorn victory acquire with raw padding published");tick();
 check(replays==2 && replayed.id==acquire.startId && replayed.flags==acquire.startFlags &&
       replayed.worldId==2 && replayed.roomId==34 && replayedProgram==157 && replayed.params==acquire.startParams,
       "Thorn native replay receives the complete raw host descriptor, including padding");
 check(!n::HoldingInput() && buttons()==0x4d4d,"Thorn in-field replay keeps native event control");
 release.hostSourceSerial=23;
 check(runtime.Publish(release,GetTickCount64()),"Thorn victory release published");tick();
 replayStarts=false;acquire.hostSourceSerial=24;
 check(runtime.Publish(acquire,GetTickCount64()),"second in-field acquire published");tick();
 check(replays==3&&buttons()==8&&n::HoldingInput(),"a refused replay falls back to the owned pause");
 }
 }
 if(mode=="normal") {
   // VUH-2640 F2, the adapter: the hold's own pause (menu 10) is open when the lifetime expires. The raw
   // callback closes it (Start), holds a neutral frame, and gives the input back once the menu reads closed;
   // the abort stays HeartbeatExpired throughout.
   menu=10;tick();tick();check(n::HoldingInput()&&buttons()==0,"the hold's own pause is open when the lifetime expires");
   runtime.Invalidate(e::Abort::HeartbeatExpired);tick();     // the hold fails in this callback's raw apply
   check(buttons()==0x4d4d,"the failing callback emits nothing of the orphan (no decision in the raw apply)");
   tick();                                                     // the owner service arms and prepares Start
   check(n::HoldingInput()&&buttons()==8&&runtime.Reason()==e::Abort::HeartbeatExpired,
       "an expired lifetime's own open pause is closed with Start; the abort is untouched");
   tick();check(n::HoldingInput()&&buttons()==0,"a neutral frame after the close press");
   // Quill r1 P1-5: a failed native sample (the collector instance bookend no longer matches): the owner
   // stops the orphan and the raw apply emits nothing; the menu stays the player's.
   const auto saved=*reinterpret_cast<uintptr_t*>(base+offsets::INPUT_STRUCT_PTR);
   *reinterpret_cast<uintptr_t*>(base+offsets::INPUT_STRUCT_PTR)=0;tick();
   *reinterpret_cast<uintptr_t*>(base+offsets::INPUT_STRUCT_PTR)=saved;
   check(!n::HoldingInput()&&buttons()==0x4d4d,"a failed native sample: the orphan stops, nothing is emitted");
   tick();check(!n::HoldingInput()&&buttons()==0x4d4d,"and it is not re-armed for the same failure");
   menu=255;tick();
   check(!n::HoldingInput()&&buttons()==0x4d4d,"the player closes it; input stays the player's");
   runtime.Invalidate(e::Abort::HeartbeatExpired);tick();
   check(!n::HoldingInput()&&runtime.Reason()==e::Abort::HeartbeatExpired,"expired lifetime remains terminal");
   auto resumed=scope;++resumed.generation;++resumed.selfConnection;
   menu=10;
   check(!n::BeginRecoveredLifetime(resumed,3)&&runtime.Reason()==e::Abort::HeartbeatExpired,
       "an old owned or foreign pause never grants recovery authority");
   menu=255;
   check(n::BeginRecoveredLifetime(resumed,3),"settled owner opens a separate recovered mapping");
   check(runtime.Reason()==e::Abort::HeartbeatExpired,"old independent producer still sees immutable expiry");
   e::Channel recovered;
   check(recovered.OpenScopedExisting(GetCurrentProcessId(),resumed)&&recovered.Bind(resumed,GetTickCount64()),
       "fresh runtime opens existing full-scope lifetime and publishes reset1");
   check(!recovered.Arm(GetTickCount64()),"fresh mapping cannot arm without native reset ACK");tick();
   n::OwnerAck(resumed,3,true,true);
   check(recovered.Arm(GetTickCount64()),"fresh owner ACK qualifies the reset baseline");
   e::Command next{};next.scope=resumed;next.kind=e::Kind::Acquire;next.hostSourceSerial=91;next.epoch=3;
   check(recovered.Publish(next,GetTickCount64()),"fresh event after recovery publishes");tick();
   check(n::HoldingInput()&&buttons()==8,"failed old input state does not poison fresh lifetime");
   tick();menu=10;tick();
   next.kind=e::Kind::Release;next.hostSourceSerial=92;
   check(recovered.Publish(next,GetTickCount64()),"same-room release after recovery publishes");tick();tick();menu=255;tick();
   n::Observe(resumed,e::Kind::Acquire,91,3,nullptr,0);
   n::Observe(resumed,e::Kind::Release,92,3,nullptr,0);
   n::OwnerAck(resumed,3,true,true);tick();tick();
   check(!n::HoldingInput()&&recovered.Reason()==e::Abort::None,"same-room recovered hold releases at fresh native ACK");
   check(!n::BeginRecoveredLifetime(resumed,3),"same lifetime cannot be recycled by another result");
   next.kind=e::Kind::Acquire;next.hostSourceSerial=93;
   check(recovered.Publish(next,GetTickCount64()),"next native side effect waits in the fresh FIFO");
   check(n::EnterInput(),"registered owner enters a fresh callback");
   gameThreadCurrent=false;
   check(!n::EnterInput()&&recovered.Reason()==e::Abort::None,"foreign rejection never touches a replaceable mapping");
   gameThreadCurrent=true;
   fresh();n::ApplyInput(data);n::LeaveInput();
   check(recovered.Reason()==e::Abort::Unsupported&&buttons()==0x4d4d,
       "rejection arriving after EnterInput is latched inside that callback before a native side effect");
 }
 }
 n::Disable();VirtualFree(memory,0,MEM_RELEASE);std::printf("mode=%.*s checks=%u failures=%u\n",static_cast<int>(mode.size()),mode.data(),checks,failures);return failures?1:0;
}
