// VUH-2913: the Axel II adapter itself (inject/src/AxelReactions.inl), compiled against simulated native memory and
// stand-ins for ScriptMirror/ScriptActorOwner (after Ione's review probes). It drives the real host frame, completion,
// 1/3 decision, the safe abort at 1/18, 1/29 and 1/101 that the hooks call, and the client's hold of the Axel clone
// (op 13/14) against recorded native attach/detach. The hook bodies themselves (SmOwnerPlayer, SmOwnerAttach,
// SmOwnerCapture, SmOwnerCaptureQuery) only call these, the slot installer is SmOwnerAxelAbortReady's stand-in, and
// the host's ScriptActorOwner role is a recorder here.
#include "ScriptMirror.hpp"
#include "kh2coop/Protocol.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>
#ifdef _MSC_VER
#include <excpt.h>
#else
#define __try try
#define __except(filter) catch(...)
#define __fastcall
#endif

namespace R = kh2coop::inject::scriptmirror::rules;

namespace offsets {
namespace actor { constexpr uintptr_t OBJENTRY_PTR=0x100,ENTITY_TRANSFORM=0x1000; }
namespace objentry { constexpr uintptr_t OBJECT_ID=0; }
namespace entity { constexpr uintptr_t POS_X=0,ROT_Y=0x20; }
}
namespace enemysync {
struct DownedScope {
    std::uint16_t worldId=2,roomId=0x14,battleProgram=137;
    std::uint8_t localSlot=0;
    std::uint32_t epoch=5;
    std::array<std::uint64_t,3> connections{0x1111,0x2222,0};
};
struct ScriptReactionCompletionRecord {
    std::uint64_t occurrence,requesterConnectionId;
    std::uint32_t resource,objectId,epoch;
    std::uint16_t netId,command;
    std::uint8_t ownerSlot;
};
static DownedScope scope;
static std::uint64_t session=0xA1;
static bool SessionIdentity(std::uint64_t& out) { out=session;return session!=0; }
static uintptr_t HostLiveEnemyActor(std::uint32_t,std::uint16_t& netId) { netId=7;return 0x100000; }
static unsigned sentClaims=0;
static bool SendScriptReactionCompletion(const ScriptReactionCompletionRecord&) { ++sentClaims;return true; }
}
namespace D {
static bool Check(bool value,const char* label,const char*& why) { if(!value)why=label;return value; }
}
namespace downedspike { static bool down=false;static bool LogicallyDowned() { return down; } }

static std::map<uintptr_t,std::vector<unsigned char>> memory;
static unsigned reads=0,vmLookups=0;
static uintptr_t readFault=0;
static std::map<uintptr_t,std::uint32_t> handles;
template<class T> static void Put(uintptr_t at,const T& value) {
    auto& bytes=memory[at];bytes.resize(sizeof(T));std::memcpy(bytes.data(),&value,sizeof(T));
}
static bool ReadHitTraceMemory(uintptr_t at,void* out,std::size_t size) {
    ++reads;
    if(at==readFault)return false;
    const auto it=memory.find(at);
    if(it==memory.end() || it->second.size()<size)return false;
    std::memcpy(out,it->second.data(),size);return true;
}
template<class T> static bool ReadHitTrace(uintptr_t at,T& out) { return ReadHitTraceMemory(at,&out,sizeof(T)); }
static std::uint32_t decodeFault=0;
static uintptr_t SmDecode(std::uint32_t handle) { if(handle && handle==decodeFault)return 0; for(const auto& [actor,h]:handles)if(h==handle)return actor;return 0; }
// ScriptMirror's: a lookup of decodeFault faults (caught), and answers nothing either way.
static bool SmDecodeKnown(std::uint32_t handle,uintptr_t& out) noexcept {
    out=0;
    if(handle && handle==decodeFault)return false;
    out=SmDecode(handle);return true;
}
static std::uint32_t SmOwnHandle(uintptr_t actor) noexcept {
    std::uint32_t handle=0;
    if(!ReadHitTrace(actor+4,handle) || !handle || SmDecode(handle)!=actor)return 0;
    return handle;
}
// As ScriptMirror's: the argument names an object, whose +4 names the actor, which has an object entry.
static uintptr_t SmActorOf(std::uint64_t arg,std::uint32_t& objectId) noexcept {
    objectId=0;std::uint32_t handle=0;uintptr_t entry=0;
    const auto object=R::TagOf(arg)==R::kTagAddress?SmDecode(R::BitsOf(arg)):0;
    const auto actor=object && ReadHitTrace(object+4,handle)?SmDecode(handle):0;
    return actor && ReadHitTrace(actor+offsets::actor::OBJENTRY_PTR,entry) && entry &&
        ReadHitTrace(entry+offsets::objentry::OBJECT_ID,objectId)?actor:0;
}
static bool scopeOk=true;
static bool SmOwnerScope(enemysync::DownedScope& out) { out=enemysync::scope;return scopeOk; }
static std::array<R::ReactionSeat,3> g_seats;
static std::array<R::ReactionSeat,3> SmOwnerLivingSeats(const enemysync::DownedScope&) { return g_seats; }
namespace enemytarget {
constexpr uintptr_t RVA_NATIVE_PLAYER=0x2A105D0;
static struct { std::array<uintptr_t,3> candidateActor{};std::array<std::uint8_t,3> ownerSlot{0,1,2}; } g;
}
static uintptr_t g_exeBase=0x140000000;
static std::array<uintptr_t,2> puppets{};
static uintptr_t PuppetTarget(int index) { return puppets[static_cast<std::size_t>(index)]; }
// The installer's answer: the core traps and the optional 1/101 slot are in (SmOwnerAxelAbortReady).
static bool abortArmed=true;
static bool SmOwnerAxelAbortReady() noexcept { return abortArmed; }
static uintptr_t SmOwnerSeatActor(std::uint8_t slot) {
    const std::array<R::ReactionActorCandidate,3> candidates{{
        {enemytarget::g.candidateActor[0],0},
        {enemytarget::g.candidateActor[1],enemytarget::g.ownerSlot[1]},
        {enemytarget::g.candidateActor[2],enemytarget::g.ownerSlot[2]}}};
    return R::ReactionActorForSeat(slot,0,enemytarget::g.candidateActor[0],candidates);
}
static int SmOwnerActorSeat(uintptr_t actor) {
    for(unsigned i=0;i<3;++i)if(enemytarget::g.candidateActor[i]==actor && actor)
        return i?enemytarget::g.ownerSlot[i]:0;
    return -1;
}
// The actor whose script is executing (ScriptActorOwner reads the VM's +0x60), set by the test.
static uintptr_t vmSource=0x100000;
static uintptr_t SmOwnerVmSource(std::uint16_t& netId,std::uint32_t& objectId) {
    ++vmLookups;netId=7;objectId=81;return vmSource;
}
static uintptr_t SmDrivenActor(std::uint16_t netId,std::uint32_t objectId) {
    return netId==7 && objectId==81?uintptr_t{0x100000}:0;
}
static void Log(const char*,...) {}
// The role (ScriptActorOwner's): the fields the Axel code reads.
struct ScriptActorRole {
    std::uint32_t objectId{};uintptr_t actor{},source{};bool axelHeld{};
    std::uint32_t resource{},epoch{};std::uint16_t netId{};std::uint8_t slot{0xFF};uintptr_t thread{};
    bool axelWhitedOut{};std::uint64_t connection{};bool axelWatched{};std::uint32_t load{};
};
static std::array<ScriptActorRole,3> g_smRoles{};
static bool roleCurrent=true;
static bool SmOwnerCurrent(const ScriptActorRole& role,const enemysync::DownedScope& scope) {
    return roleCurrent && role.resource && role.epoch==scope.epoch;
}
static R::Call SmOwnerRecord(const ScriptActorRole& role,std::uint16_t operation) {
    R::Call call{};call.netId=role.netId;call.objectId=role.objectId;call.effect=role.resource;
    call.trap=static_cast<std::uint8_t>(R::FindTrap(255,operation));return call;
}
// SmEmit through both of the real wire rules: the client's (R::ValidCall, as SmEmit checks) and the codec's
// (validScriptCallEntry, as SendScriptCalls checks each record SmFlush builds). Records that pass are kept.
static bool emitOk=true;
static unsigned wireRefused=0;
static std::vector<R::Call> emitted;
static bool SmEmit(const R::Call& c) {
    kh2coop::ScriptCallEntry e{};
    if(c.trap<R::kTrapCount) {
        e.netId=c.netId;e.objectId=c.objectId;e.table=R::kTraps[c.trap].table;e.index=R::kTraps[c.trap].index;e.effect=c.effect;
        for(unsigned i=0;i<4;++i)e.args[i]=c.values[i];
    }
    if(!R::ValidCall(c) || !kh2coop::validScriptCallEntry(e)){++wireRefused;return false;}
    if(!emitOk)return false;
    emitted.push_back(c);return true;
}
struct Hold { std::uint8_t slot{};uintptr_t actor{};bool held{}; };
struct Camera { std::uint8_t slot{};uintptr_t actor{};std::uint32_t fourcc{},target{}; };
// The records the Axel role emitted, by kind (op 13/14 holds, op 5 cameras), with the role they went on.
static const ScriptActorRole* RoleOf(const R::Call& c) {
    for(const auto& role:g_smRoles)if(role.resource && role.resource==c.effect)return &role;
    return nullptr;
}
static std::vector<Hold> Holds() {
    std::vector<Hold> out;
    for(const auto& c:emitted) {
        const auto index=R::kTraps[c.trap].index;const auto* role=RoleOf(c);
        if(R::kTraps[c.trap].table==255 && (index==13 || index==14) && role)out.push_back({role->slot,role->actor,index==13});
    }
    return out;
}
struct CameraOp { std::uint8_t slot{};uintptr_t thread{};std::uint32_t kind{},argument{};bool floatArgument{}; };
static std::vector<CameraOp> CameraOps() {
    std::vector<CameraOp> out;
    for(const auto& c:emitted) {
        const auto* role=RoleOf(c);
        if(R::kTraps[c.trap].table==255 && R::kTraps[c.trap].index==17 && role)
            out.push_back({role->slot,role->thread,R::BitsOf(c.values[0]),R::BitsOf(c.values[1]),R::TagOf(c.values[1])==R::kTagFloat});
    }
    return out;
}
static std::vector<Camera> Cameras() {
    std::vector<Camera> out;
    for(const auto& c:emitted) {
        const auto* role=RoleOf(c);
        if(R::kTraps[c.trap].table==255 && R::kTraps[c.trap].index==5 && role)
            out.push_back({role->slot,role->actor,R::BitsOf(c.values[0]),R::BitsOf(c.values[1])});
    }
    return out;
}
// ScriptActorOwner's side: the host's owner role begin, recorded.
struct RoleBegin { std::uint8_t slot{};uintptr_t actor{},source{};std::uint16_t netId{}; };
static std::vector<RoleBegin> roleBegins;
static bool roleBeginOk=true;
static bool SmOwnerBeginAxelRole(std::uint8_t slot,uintptr_t actor,uintptr_t source,std::uint16_t netId,
                                 const enemysync::DownedScope& scope) {
    roleBegins.push_back({slot,actor,source,netId});
    if(!roleBeginOk || slot>=3)return false;
    static std::uint32_t sequence=0;
    auto& role=g_smRoles[slot];
    role={};role.resource=0x80000000u|++sequence;role.objectId=81;role.netId=netId;role.slot=slot;role.actor=actor;
    role.source=source;role.epoch=scope.epoch;role.connection=scope.connections[slot];
    return true;
}
static unsigned callbacks=0;
static uintptr_t callbackPlayer=0;
static std::size_t rolesAtCallback=0;
static std::function<void()> duringCallback; // the callback's own traps, as L531 runs them
static void Callback(uintptr_t,uintptr_t player,std::uint32_t,const std::uint32_t*) {
    ++callbacks;callbackPlayer=player;rolesAtCallback=roleBegins.size();
    if(duringCallback)duringCallback();
}
// The native camera_watch a deferred watch falls back to, recorded.
static unsigned nativeWatches=0;
static void SmOwnerCameraWatchNative(const std::uint64_t*) { ++nativeWatches; }
static void (*g_smReactionCallback)(uintptr_t,uintptr_t,std::uint32_t,const std::uint32_t*)=&Callback;
static uintptr_t g_soraActor=0x200000;
static std::uint32_t g_frameCounter=10;
static struct { bool hostActive=true;unsigned faults=0; } g_sm;
// The client's native attach (1/18) and detach (1/102) originals, recorded.
static bool SmOwnerReady() noexcept { return true; }
static bool gameThread=true;
static bool SmGameThread() noexcept { return gameThread; }
namespace warp {
static bool loading=false;static std::uint32_t serial=0; // serial: CompleteLoad's count
static bool LoadPending() { return loading; } static bool TransitionPending() { return false; }
static std::uint32_t LoadSerial() { return serial; }
}
// The client's native camera calls (SmAxelCameraNatives), recorded.
struct NativeCamera { std::vector<std::string> calls;uintptr_t subject{};std::uint32_t zero{1};bool zeroed{};float fade{};bool fault{}; };
static NativeCamera natives;
static void __fastcall TargetInit(void* t) {
    natives.calls.push_back("init");const auto* b=static_cast<const unsigned char*>(t);natives.zeroed=true;
    for(unsigned i=0;i<32;++i)natives.zeroed=natives.zeroed && !b[i];
}
static void __fastcall TargetSet(void*,uintptr_t actor,std::uint32_t zero) { natives.calls.push_back("set");natives.subject=actor;natives.zero=zero; }
static void __fastcall Watch(void*) { natives.calls.push_back("watch"); }
static void __fastcall Reset(std::uint64_t*) { natives.calls.push_back("reset"); }
static void __fastcall WhiteOut(std::uint64_t* args) {
    natives.calls.push_back("whiteout");natives.fade=R::FloatOf(args[0]);if(natives.fault)throw 1;
}
static unsigned whiteInFaults=0; // the next payments that fault
static void __fastcall WhiteIn(std::uint64_t* args) {
    natives.calls.push_back("whitein");if(whiteInFaults){--whiteInFaults;throw 1;}natives.fade=R::FloatOf(args[0]);
}
// The client's trap pins (SmOwnerPin), refused at one RVA if set.
static std::uint32_t refusedPin=0;
static bool Pin(std::uint32_t rva,const std::uint8_t* body,std::size_t size) { return body && size && rva!=refusedPin; }
static bool SmOwnerActorCurrent(const ScriptActorRole&) { return true; }
using TrapFn=void(*)(std::uint64_t*);
static struct { uintptr_t original{}; } g_smOwnerTraps[6];
static unsigned attachCalls=0,detachCalls=0;
static void NativeAttach(std::uint64_t* args) {
    ++attachCalls;const auto child=SmDecode(R::BitsOf(args[0]));
    Put(child+0x6A0,R::BitsOf(args[1]));Put(child+0x6A4,static_cast<std::uint16_t>(R::BitsOf(args[2])));
    Put(child+0x6A6,static_cast<std::uint16_t>(R::BitsOf(args[3])));Put(child+0x6B0,uintptr_t{});
}
static void NativeDetach(std::uint64_t* args) { ++detachCalls;Put(SmDecode(R::BitsOf(args[0]))+0x6A0,std::uint32_t{}); }
static const char* SmRcConsumeReason(const R::ReactionWindow&,const R::ReactionCompletion&,std::uint32_t,std::uint32_t,
                                     const std::array<R::ReactionSeat,3>&) { return "refused"; }

#include "AxelReactions.inl"

namespace {
unsigned errors=0;
void check(bool ok,const char* label) {
    std::printf("%s: %s\n",ok?"PASS":"FAIL",label);
    if(!ok)++errors;
}
// Axel 0x100000 (handle 1), the host's player 0x200000 (2, seat 0), the friend's actor on the host 0x300000 (3, seat 1).
constexpr uintptr_t kAxel=0x100000,kHostActor=0x200000,kFriendActor=0x300000;
constexpr std::uint32_t kAxelHandle=1,kHostHandle=2,kFriendHandle=3,kStranger=0x99;
void Setup(std::uint32_t groups) {
    g_axel={};readFault=0;gameThread=true;memory.clear();handles={{kAxel,kAxelHandle},{kHostActor,kHostHandle},{kFriendActor,kFriendHandle}};
    enemytarget::g.candidateActor={kHostActor,kFriendActor,0};
    enemysync::scope={};enemysync::session=0xA1;enemysync::sentClaims=0;g_sm.hostActive=true;g_frameCounter=10;
    for(auto& in:g_smAxelCameraHooks.in)in=true;decodeFault=0;warp::serial=0;warp::loading=false;
    scopeOk=true;roleBeginOk=true;roleBegins.clear();g_smRoles={};roleCurrent=true;emitted.clear();emitOk=true;duringCallback=nullptr;nativeWatches=0;callbacks=0;callbackPlayer=0;downedspike::down=false;
    g_soraActor=kHostActor;puppets={kFriendActor,0};abortArmed=true;Put(g_exeBase+enemytarget::RVA_NATIVE_PLAYER,kHostActor);
    Put(kAxel+0x100,uintptr_t{0x400000});Put(uintptr_t{0x400000},std::uint32_t{81});
    Put(kHostActor+0x100,uintptr_t{0x401000});Put(uintptr_t{0x401000},std::uint32_t{566});
    Put(kFriendActor+0x100,uintptr_t{0x402000});Put(uintptr_t{0x402000},std::uint32_t{567});
    Put(kAxel+0x5A0,groups);Put(kAxel+0x5A4,std::uint32_t{});Put(kAxel+0x5A8,std::uint32_t{});Put(kAxel+0x5AC,std::uint32_t{});
    Put(kAxel+0xAB8,uintptr_t{0x500000});Put(uintptr_t{0x500004},std::uint32_t{0x00200003});
    const float origin[3]{};auto& position=memory[kAxel+0x1000];position.resize(sizeof(origin));
    std::memcpy(position.data(),origin,sizeof(origin));Put(kAxel+0x1020,float{});
    Put(kAxel+0x6A0,std::uint32_t{});Put(kAxel+0x6A4,std::uint16_t{});Put(kAxel+0x6A6,std::uint16_t{});
    Put(kAxel+4,kAxelHandle);Put(kHostActor+4,kHostHandle);Put(kFriendActor+4,kFriendHandle);
    Put(kHostActor+0x2B0,uintptr_t{});Put(kFriendActor+0x2B0,uintptr_t{});
    Put(kHostActor+0xD48,uintptr_t{});Put(kFriendActor+0xD48,uintptr_t{});
    // The friend is nearer Axel, so it wins every family it is inside.
    g_seats={{{0x1111,{0,0,1000},true,true,true},{0x2222,{0,0,500},true,true,true},{}}};
}
void Frame() { SmAxelFrame(enemysync::scope); }
enemysync::ScriptReactionCompletionRecord Claim(std::uint16_t command=R::kOvertaker) {
    const auto* w=g_axel.windows.Find(command);
    return {w->occurrence,w->connection,R::AxelResource(w->command),81,w->epoch,w->source,w->command,w->slot};
}
// SmOwnerPlayer as the hook runs it: the native result (the host's canonical player), then the Axel decision. 0 means
// the Axel path left it to the ordinary nearest-seat choice.
std::uint32_t Player() {
    std::uint64_t args[1]{R::Tagged(R::kTagAddress,kHostHandle)};
    return SmAxelPlayer(args,kAxel,enemysync::scope)?R::BitsOf(args[0]):0;
}
R::AxelAct Act(const char* trap,std::uint32_t axel,std::uint32_t player) {
    const std::uint64_t args[2]{R::Tagged(R::kTagAddress,axel),R::Tagged(R::kTagAddress,player)};
    return SmAxelAct(args,trap);
}
void HoldAxel(std::uint32_t parent) {
    Put(kAxel+0x6A0,parent);Put(kAxel+0x6A4,std::uint16_t{16386});Put(kAxel+0x6A6,std::uint16_t{1});Put(kAxel+0x6B0,uintptr_t{});
}
// A friend's Overtaker, completed on the host.
bool FriendOvertaker() {
    Setup(1u<<R::kOvertakerGroup);Frame();
    const auto* w=g_axel.windows.Find(R::kOvertaker);
    return w && w->slot==1 && SmAxelComplete(Claim(),nullptr);
}
}

int main() {
    // A friend owns, the host consumes once, and the callback runs with the friend's actor on the host.
    check(FriendOvertaker() && callbacks==1 && callbackPlayer==kFriendActor && g_axel.executor.slot==1 &&
          g_axel.executorActor==kFriendActor,"a friend's Overtaker claim runs the host callback with the friend's own actor");
    check(roleBegins.size()==1 && roleBegins[0].slot==1 && roleBegins[0].actor==kFriendActor &&
          roleBegins[0].source==kAxel && roleBegins[0].netId==7 && rolesAtCallback==1,
          "its owner role begins before the callback, so the callback's capture reaches the friend");
    check(Player()==kFriendHandle,"1/3 inside the RC names the friend's actor, not the host's or the nearest");
    check(!SmAxelComplete(Claim(),nullptr) && callbacks==1,"a duplicate completion is refused and runs nothing");
    {
        Setup(1u<<R::kOvertakerGroup);Frame();
        auto wrong=Claim();wrong.ownerSlot=0;wrong.requesterConnectionId=0x1111;
        check(!SmAxelComplete(wrong,nullptr) && !g_axel.windows.Find(R::kOvertaker)->consumed && !callbacks,
              "a wrong-owner completion (the host on the friend's window) consumes nothing");
        auto spoof=Claim();spoof.requesterConnectionId=0x3333;
        check(!SmAxelComplete(spoof,nullptr) && !g_axel.windows.Find(R::kOvertaker)->consumed && !callbacks,
              "a completion from another connection for the friend's seat consumes nothing");
        auto stale=Claim();stale.epoch=4;
        check(!SmAxelComplete(stale,nullptr) && !g_axel.windows.Find(R::kOvertaker)->consumed && !callbacks,
              "a completion from an older epoch consumes nothing");
        const auto old=Claim();
        Put(kAxel+0x5A0,std::uint32_t{});++g_frameCounter;Frame();
        Put(kAxel+0x5A0,std::uint32_t{1u<<R::kOvertakerGroup});++g_frameCounter;Frame();
        const auto* replaced=g_axel.windows.Find(R::kOvertaker);
        check(replaced && replaced->occurrence>old.occurrence && !SmAxelComplete(old,nullptr) && !callbacks &&
              !replaced->consumed,"a completion for a replaced occurrence is refused, and the new one stays open");
        check(SmAxelComplete(Claim(),nullptr) && callbacks==1,"the new occurrence's own completion runs once");
    }
    {
        Setup(1u<<R::kOvertakerGroup);Frame();roleBeginOk=false;
        check(!SmAxelComplete(Claim(),nullptr) && !callbacks && !g_axel.executor.command &&
              g_axel.windows.Find(R::kOvertaker)->consumed,
              "if the owner role cannot begin, nothing runs, no executor stays bound, and the occurrence stays consumed");
    }
    {
        Setup(1u<<R::kBurstFrontierGroupA);Frame();
        check(g_axel.windows.Find(R::kBurstFrontier) && SmAxelComplete(Claim(R::kBurstFrontier),nullptr) &&
              callbacks==1 && callbackPlayer==kFriendActor && roleBegins.empty(),
              "a friend's Burst Frontier runs with its actor and leaves the role to its own 1/112 hook");
    }
    {
        // The host's own RC: no role, and the host's actor is the only one Axel may act on.
        Setup(1u<<R::kOvertakerGroup);g_seats[1].living=false;Frame();
        check(g_axel.windows.Find(R::kOvertaker)->slot==0 && SmAxelReactionCallback(kAxel,kHostActor,R::kOvertaker,nullptr) &&
              callbacks==1 && callbackPlayer==kHostActor && roleBegins.empty(),"the host's own press runs with its own actor");
        check(Act("attach",kAxelHandle,kHostHandle)==R::AxelAct::Admitted &&
              Act("capture",kAxelHandle,kHostHandle)==R::AxelAct::Admitted,"its attach and capture onto the host proceed");
        check(Act("attach",kAxelHandle,kFriendHandle)==R::AxelAct::Refused,"onto the friend they are refused");
    }

    // The safe abort, on a friend's RC.
    check(FriendOvertaker() && Act("attach",kAxelHandle,kFriendHandle)==R::AxelAct::Admitted &&
          Act("capture",kAxelHandle,kFriendHandle)==R::AxelAct::Admitted,
          "Axel's attach and capture onto the friend's resolving actor proceed");
    check(Act("attach",kAxelHandle,kHostHandle)==R::AxelAct::Refused &&
          Act("capture",kAxelHandle,kHostHandle)==R::AxelAct::Refused,"onto the host's actor during the friend's RC: refused");
    check(Act("attach",kAxelHandle,kStranger)==R::AxelAct::Refused,"a player argument that does not decode is refused");
    check(Act("attach",kHostHandle,kFriendHandle)==R::AxelAct::Native,"another actor's attach is not Axel's: native");
    {
        (void)FriendOvertaker();
        Put(kFriendActor+4,kStranger); // the friend's actor is gone mid-RC: its handle word names another actor
        const auto native=Player();
        check(native==kHostHandle && g_axel.executor.slot==1,
              "a lost friend actor leaves 1/3 the native host result, and the RC stays the friend's");
        check(Act("attach",kAxelHandle,native)==R::AxelAct::Refused && Act("capture",kAxelHandle,native)==R::AxelAct::Refused,
              "and Axel's attach and capture onto that 1/3 result are refused: the host is never held");
        g_frameCounter+=R::kAxelExecutorLeaseFrames;Frame();
        check(!g_axel.executor.command && Player()==0 && Act("attach",kAxelHandle,kHostHandle)==R::AxelAct::Refused,
              "the never-engaged RC ends at its lease; a late attach of the same flow is still refused");
    }
    {
        (void)FriendOvertaker();HoldAxel(kFriendHandle);++g_frameCounter;Frame();
        memory.erase(kFriendActor+4); // the friend's identity is unread, not gone
        bool kept=true;
        for(int i=0;i<3;++i) {
            g_frameCounter+=R::kAxelExecutorLeaseFrames;Frame();
            kept=kept && g_axel.executor.slot==1 && Player()==kHostHandle &&
                Act("attach",kAxelHandle,kFriendHandle)==R::AxelAct::Refused &&
                Act("attach",kAxelHandle,kHostHandle)==R::AxelAct::Refused;
        }
        check(kept,"an unread friend identity keeps the binding and refuses every act, on the friend or the host");
        Put(kFriendActor+4,kFriendHandle);++g_frameCounter;Frame();
        check(g_axel.executor.slot==1 && Player()==kFriendHandle && Act("attach",kAxelHandle,kFriendHandle)==R::AxelAct::Admitted,
              "on recovery the same RC admits its own actor again");
        // An object that still names the friend's actor while the actor's own identity is unread: the identity decides.
        constexpr uintptr_t kObject=0x310000;constexpr std::uint32_t kObjectHandle=0x13;
        handles[kObject]=kObjectHandle;Put(kObject+4,kFriendHandle);memory.erase(kFriendActor+4);
        check(Act("attach",kAxelHandle,kObjectHandle)==R::AxelAct::Refused &&
              Act("capture",kAxelHandle,kObjectHandle)==R::AxelAct::Refused,
              "an argument naming the friend's actor is refused while that actor's identity is unread");
        Put(kFriendActor+4,kFriendHandle);
        check(Act("attach",kAxelHandle,kObjectHandle)==R::AxelAct::Admitted,"and admitted once it resolves");
    }
    {
        // Ione's 75233daf: a downed or otherwise ineligible host leaves the candidate table yet stays 1/3's native
        // result, so the abort knows a player by identity, not by targeting eligibility.
        const auto query=[](std::uint32_t player) {
            const std::uint64_t args[1]{R::Tagged(R::kTagAddress,player)};return SmAxelCaptureQuery(args);
        };
        const auto refusedOn=[&](std::uint32_t player) {
            return Act("attach",kAxelHandle,player)==R::AxelAct::Refused && Act("capture",kAxelHandle,player)==R::AxelAct::Refused &&
                query(player)==R::AxelAct::Refused;
        };
        (void)FriendOvertaker();downedspike::down=true;enemytarget::g.candidateActor[0]=0; // RefreshCandidates drops it
        check(Act("attach",kAxelHandle,kFriendHandle)==R::AxelAct::Admitted && refusedOn(kHostHandle),
              "control: a downed host is refused while the friend's resolving actor is admitted");
        Put(kFriendActor+4,kStranger);
        const auto lost=Player();
        check(lost==kHostHandle && refusedOn(lost),
              "a lost friend never routes attach, capture or the capture query onto a downed host");
        (void)FriendOvertaker();downedspike::down=true;enemytarget::g.candidateActor[0]=0;memory.erase(kFriendActor+4);
        const auto unread=Player();
        check(SmAxelExecutorActor()==R::AxelActorState::Unknown && g_axel.executor.slot==1 && unread==kHostHandle &&
              refusedOn(unread),"nor does an unread friend, whose binding stays");
        (void)FriendOvertaker();enemytarget::g.candidateActor[0]=0;Put(kFriendActor+4,kStranger);
        check(refusedOn(Player()),"nor onto a host that left the table for any other reason");
        g_soraActor=0;
        check(refusedOn(kHostHandle),"the native player pointer alone names the host");
        g_soraActor=kHostActor;Put(g_exeBase+enemytarget::RVA_NATIVE_PLAYER,uintptr_t{});
        check(refusedOn(kHostHandle),"and the canonical actor alone");
        constexpr uintptr_t kSecond=0x380000,kWeapon=0x390000;constexpr std::uint32_t kSecondHandle=4,kWeaponHandle=5;
        handles[kSecond]=kSecondHandle;Put(kSecond+4,kSecondHandle);Put(kSecond+0x100,uintptr_t{0x402000});
        handles[kWeapon]=kWeaponHandle;Put(kWeapon+4,kWeaponHandle);Put(kWeapon+0x100,uintptr_t{0x403000});
        Put(uintptr_t{0x403000},std::uint32_t{900});puppets[1]=kSecond;
        check(refusedOn(kSecondHandle),"another friend's puppet off the table is a player too");
        check(Act("attach",kAxelHandle,kWeaponHandle)==R::AxelAct::Native,"an actor that is no player's stays native");
    }
    {
        Setup(1u<<R::kOvertakerGroup);Frame();
        check(!g_axel.executor.command && Act("attach",kAxelHandle,kHostHandle)==R::AxelAct::Refused &&
              Act("capture",kAxelHandle,kFriendHandle)==R::AxelAct::Refused,"with no RC bound, Axel acts on no player");
        (void)FriendOvertaker();scopeOk=false;
        check(Act("attach",kAxelHandle,kFriendHandle)==R::AxelAct::Refused,"an unreadable scope refuses too");
        g_sm.hostActive=false;
        check(Act("attach",kAxelHandle,kFriendHandle)==R::AxelAct::Native,"a client's own traps are not the host's abort");
    }

    // 1/101 in idle_after_rc reads 1/3's result: inside Axel's script it reads only the bound executor's resolving actor.
    {
        const auto query=[](std::uint32_t player,uintptr_t vm) {
            const std::uint64_t args[1]{R::Tagged(R::kTagAddress,player)};
            vmSource=vm;const auto act=SmAxelCaptureQuery(args);vmSource=kAxel;return act;
        };
        (void)FriendOvertaker();
        check(query(kFriendHandle,kAxel)==R::AxelAct::Admitted,"Axel's capture query on the friend's actor reads natively");
        check(query(kHostHandle,kAxel)==R::AxelAct::Refused,"on the host's actor during the friend's RC it answers not captured");
        check(query(kHostHandle,kHostActor)==R::AxelAct::Native,"another script's query is not Axel's");
        Put(kFriendActor+4,kStranger);
        check(query(Player(),kAxel)==R::AxelAct::Refused,"a lost friend's 1/3 result (the host) is never read for Axel");
        Setup(1u<<R::kOvertakerGroup);Frame();
        check(query(kHostHandle,kAxel)==R::AxelAct::Refused,"with no RC bound, Axel's query reads no player");
    }
    {
        // Every other encounter: the gates, the camera hook and the query read nothing at all.
        Setup(1u<<R::kOvertakerGroup);enemysync::scope.roomId=0x15;Frame();
        reads=0;vmLookups=0;
        const std::uint64_t two[2]{R::Tagged(R::kTagAddress,kAxelHandle),R::Tagged(R::kTagAddress,kHostHandle)};
        const bool native=SmAxelAct(two,"attach")==R::AxelAct::Native && SmAxelCaptureQuery(two)==R::AxelAct::Native &&
            !SmAxelCamera(two);
        check(native && !reads && !vmLookups,"outside Axel II the hooks stay native and read nothing");
    }

    // Ione's 75233daf: with the 1/101 gate refused the abort is unarmed. A friend is offered nothing and its completion
    // runs and binds nothing, so no native query can follow; the host's own RC still runs.
    {
        Setup(1u<<R::kOvertakerGroup);abortArmed=false;Frame();
        const auto* w=g_axel.windows.Find(R::kOvertaker);
        check(w && w->slot==0,"unarmed, the nearer friend is offered nothing");
        auto forged=Claim();forged.ownerSlot=1;forged.requesterConnectionId=0x2222;
        check(!SmAxelComplete(forged,nullptr) && !g_axel.windows.Find(R::kOvertaker)->consumed && !callbacks &&
              !g_axel.executor.command && roleBegins.empty(),"a friend's claim on it consumes, runs and binds nothing");
        check(SmAxelReactionCallback(kAxel,kHostActor,R::kOvertaker,nullptr) && callbacks==1 && callbackPlayer==kHostActor &&
              g_axel.executor.slot==0,"the host's own press still runs");
        Setup(1u<<R::kOvertakerGroup);Frame();
        const auto offered=Claim();abortArmed=false;
        check(offered.ownerSlot==1 && !SmAxelComplete(offered,nullptr) && !g_axel.windows.Find(R::kOvertaker)->consumed &&
              !callbacks && !g_axel.executor.command && roleBegins.empty(),
              "a friend's completion of a window offered while armed runs nothing once unarmed");
    }
    // The friend's owner role on the host lasts while its executor does.
    {
        (void)FriendOvertaker();
        check(SmAxelRoleHeld(81,1,kFriendActor,0),"the friend's Axel role is held while its executor is bound");
        check(!SmAxelRoleHeld(81,0,kFriendActor,0) && !SmAxelRoleHeld(81,1,kHostActor,0) && !SmAxelRoleHeld(307,1,kFriendActor,0) &&
              !SmAxelRoleHeld(81,1,kFriendActor,0x777),"only that seat's actor, Axel's role, with no thread");
        Put(kFriendActor+0xD48,uintptr_t{0x777});
        check(!SmAxelRoleHeld(81,1,kFriendActor,0),"a coroutine on the actor ends it (a 1/112 hook mints its own)");
        memory.erase(kFriendActor+0xD48);
        check(!SmAxelRoleHeld(81,1,kFriendActor,0),"an unread coroutine word does not hold it");
        Put(kFriendActor+0xD48,uintptr_t{});++enemysync::scope.epoch;
        check(!SmAxelRoleHeld(81,1,kFriendActor,0),"a new epoch ends it");
        enemysync::scope.epoch=5;g_sm.hostActive=false;
        check(!SmAxelRoleHeld(81,1,kFriendActor,0),"a client never holds the host's role");
    }
    {
        // Engaged, released: the executor ends on a confirmed release, and the role with it.
        (void)FriendOvertaker();HoldAxel(kFriendHandle);++g_frameCounter;Frame();
        check(g_axel.executor.engaged && SmAxelRoleHeld(81,1,kFriendActor,0),"Axel held by the friend's actor engages the RC");
        Put(kAxel+0x6A0,std::uint32_t{});++g_frameCounter;Frame();
        check(!g_axel.executor.command && !SmAxelRoleHeld(81,1,kFriendActor,0),"its confirmed release ends the executor and role");
    }
    {
        // A down mid-RC: the executor keeps its actor, so 1/3 still names the friend and nobody else is held.
        (void)FriendOvertaker();HoldAxel(kFriendHandle);++g_frameCounter;Frame();
        g_seats[1].living=false;enemytarget::g.candidateActor[1]=0;++g_frameCounter;Frame();
        check(g_axel.executor.slot==1 && Player()==kFriendHandle && Act("attach",kAxelHandle,kHostHandle)==R::AxelAct::Refused,
              "a downed friend still owns the RC its actor is in, and the host is never held");
        (void)FriendOvertaker();enemysync::scope.roomId=0x15;Frame();
        check(!g_axel.executor.command && !g_axel.actor && !SmAxelRoleHeld(81,1,kFriendActor,0),
              "leaving the encounter (a load) ends the executor and its role");
    }

    // The hold travels to the owner on each change.
    {
        (void)FriendOvertaker();Put(kFriendActor+0x2B0,kAxel);++g_frameCounter;Frame(); // captured: the RC holds
        const bool first=Holds().size()==1 && !Holds()[0].held && Holds()[0].slot==1 && Holds()[0].actor==kFriendActor;
        HoldAxel(kFriendHandle);++g_frameCounter;Frame();++g_frameCounter;Frame();
        const bool held=Holds().size()==2 && Holds()[1].held;
        check(first && held,"the friend's role is told Axel is free, then held, once each");
        memory.erase(kAxel+0x6A0);++g_frameCounter;Frame();
        check(Holds().size()==2,"an unread parent sends nothing");
        HoldAxel(kHostHandle);Put(kAxel+0x6A0,kHostHandle);++g_frameCounter;Frame();
        check(Holds().size()==3 && !Holds()[2].held,"Axel held by another actor is not the friend's hold");
        Setup(1u<<R::kOvertakerGroup);g_seats[1].living=false;Frame();
        const bool ran=SmAxelComplete(Claim(),nullptr);
        HoldAxel(kHostHandle);++g_frameCounter;Frame();
        reads=0;SmAxelSyncHeld();
        check(ran && g_axel.executor.slot==0 && Holds().empty() && !reads,"the host's own RC sends no hold, and reads none");
    }

    // Ione's 75233daf: the friend's hold of the Axel clone (client op 13/14) is this role's only with its receipt.
    {
        Setup(0);g_sm.hostActive=false;attachCalls=0;detachCalls=0;
        g_smOwnerTraps[4].original=reinterpret_cast<uintptr_t>(&NativeAttach);
        g_smOwnerTraps[5].original=reinterpret_cast<uintptr_t>(&NativeDetach);
        const auto parent=[] { std::uint32_t at=0;return ReadHitTrace(kAxel+0x6A0,at)?at:UINT32_MAX; };
        ScriptActorRole role{81,kFriendActor,kAxel,false};
        HoldAxel(kFriendHandle); // another activation's matching pair
        check(!SmAxelApplyHold(role,false) && !detachCalls && parent()==kFriendHandle,
              "op 14 never detaches a matching pair this role did not make");
        check(!SmAxelApplyHold(role,true) && !attachCalls && !role.axelHeld && !SmAxelCloneHeld(role),
              "nor does op 13 take it as its own, or a continuation carry it");
        Put(kAxel+0x6A0,std::uint32_t{});
        check(SmAxelApplyHold(role,true) && attachCalls==1 && role.axelHeld && SmAxelCloneHeld(role) && parent()==kFriendHandle,
              "its own attach records the receipt");
        check(SmAxelApplyHold(role,true) && attachCalls==1,"a repeated op 13 attaches nothing");
        check(SmAxelApplyHold(role,false) && detachCalls==1 && !role.axelHeld && !parent(),"its own hold detaches once");
        check(SmAxelApplyHold(role,false) && detachCalls==1,"a repeated op 14 detaches nothing");
        role.axelHeld=true;HoldAxel(kHostHandle);
        check(!SmAxelApplyHold(role,false) && detachCalls==1 && parent()==kHostHandle,"a replaced parent is preserved");
        HoldAxel(kFriendHandle);Put(kAxel+0x6B0,uintptr_t{0x888});
        check(!SmAxelApplyHold(role,false) && detachCalls==1,"so is a pair with a VM callback");
        Put(kAxel+0x6B0,uintptr_t{});g_sm.hostActive=true;
        check(!SmAxelApplyHold(role,false) && detachCalls==1,"the host never applies a client's hold");
    }

    // Cameras: Axel's camera_start_global (1/164) during a friend's RC goes to its owner, and the host skips it.
    {
        constexpr uintptr_t kName=0x700000;constexpr std::uint32_t kNameHandle=0x70;
        const auto start=[&](std::uint32_t object) {
            const std::uint64_t args[2]{R::Tagged(R::kTagAddress,object),R::Tagged(R::kTagAddress,kNameHandle)};
            return SmAxelCamera(args);
        };
        (void)FriendOvertaker();handles[kName]=kNameHandle;Put(kName,std::uint32_t{0x3030}); // "00"
        check(start(kAxelHandle) && Cameras().size()==1 && Cameras()[0].slot==1 && Cameras()[0].actor==kFriendActor &&
              Cameras()[0].fourcc==0x3030 && Cameras()[0].target==R::kAxelCameraNone,
              "a friend's Overtaker camera on Axel goes to the friend with no target, and the host skips it");
        const auto sent=Cameras().size();
        check(!start(kHostHandle) && Cameras().size()==sent,"another object's camera stays native");
        // Each case below is a fresh RC: the first op decides its custody.
        const auto fresh=[&] { (void)FriendOvertaker();handles[kName]=kNameHandle;Put(kName,std::uint32_t{0x3030}); };
        fresh();Put(kName,std::uint32_t{0x0130});
        check(!start(kAxelHandle) && Cameras().empty(),"a name that is not a printable fourcc stays native");
        fresh();emitOk=false;
        check(!start(kAxelHandle),"if the record cannot be sent, the host plays it natively");
        fresh();Put(kFriendActor+0xD48,uintptr_t{0x777});
        check(!start(kAxelHandle),"without the friend's held role (Burst's hook role, or none) it stays native");
        g_sm.hostActive=false;
        check(!start(kAxelHandle),"a client's own starts stay native");
        Setup(1u<<R::kOvertakerGroup);g_seats[1].living=false;Frame();handles[kName]=kNameHandle;Put(kName,std::uint32_t{0x3030});
        const bool ran=SmAxelComplete(Claim(),nullptr);
        check(ran && !start(kAxelHandle) && Cameras().empty(),"the host's own RC camera plays on the host");
    }

    // Camera custody (R::AxelCameraStepFor): a friend RC's camera sequence goes wholly to its owner or wholly to the host.
    {
        constexpr uintptr_t kName=0x700000;constexpr std::uint32_t kNameHandle=0x70;
        const auto start=[&](std::uint32_t object) {
            const std::uint64_t args[2]{R::Tagged(R::kTagAddress,object),R::Tagged(R::kTagAddress,kNameHandle)};
            return SmAxelCamera(args);
        };
        const auto watch=[](std::uint32_t object) {
            const std::uint64_t args[1]{R::Tagged(R::kTagAddress,object)};return SmAxelCameraWatch(args);
        };
        const auto named=[&] { handles[kName]=kNameHandle;Put(kName,std::uint32_t{0x3030}); };
        (void)FriendOvertaker();named();
        check(start(kAxelHandle) && g_axel.cameraCustody==R::AxelCameraCustody::Owner,
              "a friend's first camera op that is sent makes the sequence its owner's");
        emitOk=false;const auto sent=Cameras().size();
        check(start(kAxelHandle) && Cameras().size()==sent,"a later op of that RC stays off the host even when it cannot be sent");
        emitOk=true;
        check(!start(kHostHandle) && !watch(kHostHandle) && Cameras().size()==sent && CameraOps().empty(),
              "the host's own unrelated camera ops stay native while the owner holds custody");
        (void)FriendOvertaker();named();emitOk=false;
        check(!start(kAxelHandle) && g_axel.cameraCustody==R::AxelCameraCustody::Host,
              "a first op that cannot be sent plays on the host, and the sequence is the host's");
        emitOk=true;
        check(!start(kAxelHandle) && !watch(kAxelHandle) && Cameras().empty() && CameraOps().empty(),
              "so the rest of that RC's sequence plays on the host too: never split");
        g_frameCounter+=R::kAxelExecutorLeaseFrames;Frame();
        check(!g_axel.executor.command && g_axel.cameraCustody==R::AxelCameraCustody::Undecided,"custody ends with its executor");
        Setup(1u<<R::kOvertakerGroup);g_seats[1].living=false;Frame();named();
        const bool ran=SmAxelComplete(Claim(),nullptr);
        check(ran && !start(kAxelHandle) && !watch(kAxelHandle) && Cameras().empty() && CameraOps().empty(),
              "the host's own RC never takes custody");
    }
    {
        // Burst's L531 runs 1/207, then 1/29, then 1/112 (Kit, e4a7861e): its watch waits for the hook role.
        const auto watch=[](std::uint32_t object) {
            const std::uint64_t args[1]{R::Tagged(R::kTagAddress,object)};return SmAxelCameraWatch(args);
        };
        const auto hookRole=[] {
            auto& role=g_smRoles[1];role={};role.resource=0x80000077u;role.objectId=81;role.netId=7;role.slot=1;
            role.actor=kFriendActor;role.source=kAxel;role.thread=0x900;role.epoch=enemysync::scope.epoch;
        };
        bool deferred=false;
        Setup(1u<<R::kBurstFrontierGroupA);Frame();
        duringCallback=[&] { deferred=watch(kAxelHandle) && CameraOps().empty();hookRole(); };
        const bool ran=SmAxelComplete(Claim(R::kBurstFrontier),nullptr);
        const auto ops=CameraOps();
        check(ran && deferred && ops.size()==1 && ops[0].slot==1 && ops[0].thread==0x900 &&
              ops[0].kind==static_cast<std::uint32_t>(R::AxelCameraKind::Watch) && !ops[0].argument && !nativeWatches &&
              g_axel.cameraCustody==R::AxelCameraCustody::Owner,
              "a friend's Burst watch is held through its callback and goes on the hook role, and the host skips it");
        Setup(1u<<R::kBurstFrontierGroupA);Frame();
        duringCallback=[&] { deferred=watch(kAxelHandle); };
        check(SmAxelComplete(Claim(R::kBurstFrontier),nullptr) && deferred && CameraOps().empty() && nativeWatches==1 &&
              g_axel.cameraCustody==R::AxelCameraCustody::Host,
              "with no hook role to carry it, it plays natively once the callback returns");
        duringCallback=nullptr;
        Setup(1u<<R::kBurstFrontierGroupA);Frame();(void)SmAxelComplete(Claim(R::kBurstFrontier),nullptr);hookRole();
        check(watch(kAxelHandle) && CameraOps().size()==1 && !nativeWatches,"outside the callback it goes at once on a current role");
        Setup(1u<<R::kBurstFrontierGroupA);g_seats[1].living=false;Frame();
        duringCallback=[&] { deferred=watch(kAxelHandle); };
        check(SmAxelComplete(Claim(R::kBurstFrontier),nullptr) && !deferred && CameraOps().empty() && !nativeWatches,
              "the host's own Burst watches natively");
        duringCallback=nullptr;
        Setup(1u<<R::kBurstFrontierGroupA);enemysync::scope.roomId=0x15;Frame();reads=0;vmLookups=0;
        check(!watch(kAxelHandle) && !reads && !vmLookups,"outside Axel II the watch hook reads nothing");
    }
    // D303 under the owner's custody (R::AxelFadeAdopted, R::AxelFadeOurs): adopted at its 2/20, known by its thread
    // and Axel as its VM's source.
    {
        constexpr uintptr_t kName=0x700000,kNode=0x710000,kThread=0x720000,kVm=0x730000,kOther=0x740000;
        constexpr std::uint32_t kNameHandle=0x70,kNodeHandle=0x71,kThreadHandle=0x72;
        // Kit's e4a7861e: the node is {u32 handle(thread), u32 handle(0) owner, f32 0, u32 handle(next)}.
        const auto node=[&](std::uint32_t thread=0x72u) { // kThreadHandle (a local's name is no default: MSVC C2587)
            handles[kNode]=kNodeHandle;handles[kThread]=kThreadHandle;
            const std::uint32_t words[4]{thread,0,0,0};auto& bytes=memory[kNode];bytes.resize(sizeof(words));
            std::memcpy(bytes.data(),words,sizeof(words));
        };
        using Kind=R::AxelCameraKind;
        std::uint32_t label=303;
        const auto running=[&](uintptr_t thread,uintptr_t source) {
            Put(g_exeBase+0x2A25048,thread);Put(g_exeBase+0x2A250B0,kVm);Put(kVm+0x60,source);
        };
        const auto queue=[&] { // L140: the start "10", then 2/20 queues D303 and returns its node's handle
            const std::uint64_t start[2]{R::Tagged(R::kTagAddress,kAxelHandle),R::Tagged(R::kTagAddress,kNameHandle)};
            (void)SmAxelCamera(start);
            const std::uint64_t before[2]{R::Tagged(R::kTagInt,label),R::Tagged(R::kTagAddress,kAxelHandle)};
            const std::uint64_t after[1]{R::Tagged(R::kTagAddress,kNodeHandle)};
            if(SmAxelThreadWatched())SmAxelThreadStarted(before,after);
        };
        const auto fade=[](Kind kind,float value) {
            std::uint32_t bits=0;std::memcpy(&bits,&value,sizeof(bits));
            const std::uint64_t args[1]{R::Tagged(R::kTagFloat,bits)};
            return SmAxelFadePending() && SmAxelFade(kind,args);
        };
        const auto friendClearLight=[&](std::uint32_t groups=1u<<R::kOvertakerGroup) {
            Setup(groups);Frame();
            handles[kName]=kNameHandle;Put(kName,std::uint32_t{0x3031});node();
            running(kOther,kAxel);duringCallback=queue;
            const bool ran=SmAxelComplete(Claim(),nullptr);duringCallback=nullptr;
            return ran;
        };
        check(friendClearLight() && g_axel.fade.thread==kThread && g_axel.fade.threadHandle==kThreadHandle && g_axel.fade.slot==1,
              "under the owner's custody the host adopts D303 at its 2/20, decoding the node's thread handle");
        check(g_axel.fade.resource==g_smRoles[1].resource && g_axel.fade.connection==0x2222,
              "with the role its RC's start went to");
        running(kThread,kAxel);
        const bool out=fade(Kind::WhiteOut,70.0f),reset=SmAxelFade(Kind::Reset,nullptr),in=fade(Kind::WhiteIn,30.0f);
        const auto ops=CameraOps();
        check(out && reset && in && ops.size()==3 && ops[0].kind==3 && ops[1].kind==2 && ops[2].kind==4 &&
              ops[0].floatArgument && ops[0].slot==1 && !g_axel.fade.thread,
              "its white-out, reset and white-in go to the owner in order, none on the host, and white-in ends it");
        check(!fade(Kind::WhiteOut,70.0f),"after its white-in, that thread's fades are native again");
        // Ione's 629ab5f1 (1): the handle, not the pointer, names D303.
        Setup(1u<<R::kOvertakerGroup);Frame();
        handles[kName]=kNameHandle;Put(kName,std::uint32_t{0x3031});node(0x99);running(kOther,kAxel);duringCallback=queue;
        (void)SmAxelComplete(Claim(),nullptr);duringCallback=nullptr;
        check(g_axel.cameraCustody==R::AxelCameraCustody::Owner && !g_axel.fade.thread,
              "a node whose thread handle does not resolve adopts nothing");
        (void)friendClearLight();running(kThread,kAxel);handles[kThread]=0x172; // freed, and another thread at its address
        check(!fade(Kind::WhiteOut,70.0f) && g_axel.fade.thread==kThread && CameraOps().empty(),
              "once D303's handle no longer resolves to the running thread, that thread is another's and plays natively");
        // Ione's b318dd1b (1): a lookup that faults is not a replacement.
        (void)friendClearLight();running(kThread,kAxel);decodeFault=kThreadHandle;
        check(fade(Kind::WhiteOut,70.0f) && g_axel.fade.thread==kThread && CameraOps().empty(),
              "a D303 whose handle lookup faults is withheld from both games, and stays adopted");
        running(kOther,kHostActor);
        check(!fade(Kind::WhiteOut,40.0f),"meanwhile the host's own fades on other threads stay native");
        running(kThread,kAxel);decodeFault=0;scopeOk=false;
        check(fade(Kind::WhiteOut,70.0f) && CameraOps().empty() && g_axel.fade.thread==kThread,
              "and so is one in a frame whose owner scope cannot be read");
        scopeOk=true;
        check(fade(Kind::Reset,0) && CameraOps().size()==1 && CameraOps()[0].kind==2,"once it answers again, D303's ops reach the owner");
        // (2): nor is an unread thread at 2/20 a thread that was never queued.
        const auto adoptFaulting=[&](std::uint32_t fault,bool unreadNode=false,std::uint32_t word=0x72u) {
            Setup(1u<<R::kOvertakerGroup);Frame();
            handles[kName]=kNameHandle;Put(kName,std::uint32_t{0x3031});node(word);if(unreadNode)memory.erase(kNode);
            running(kOther,kAxel);decodeFault=fault;duringCallback=queue;
            const bool ran=SmAxelComplete(Claim(),nullptr);duringCallback=nullptr;decodeFault=0;
            return ran && g_axel.cameraCustody==R::AxelCameraCustody::Owner && Cameras().size()==1;
        };
        for(const auto& [fault,unread]:{std::pair{kThreadHandle,false},std::pair{kNodeHandle,false},std::pair{0u,true}}) {
            const bool started=adoptFaulting(fault,unread);
            const bool pendingAdopted=g_axel.fade.pending && !g_axel.fade.thread;
            decodeFault=fault;if(unread)memory.erase(kNode);running(kThread,kAxel);
            const bool withheld=fade(Kind::WhiteOut,70.0f) && CameraOps().empty();
            running(kOther,kHostActor);const bool hostOwn=!fade(Kind::WhiteOut,40.0f);
            decodeFault=0;node();running(kThread,kAxel);
            const bool recovered=fade(Kind::WhiteIn,30.0f) && CameraOps().size()==1 && CameraOps()[0].kind==4 && !g_axel.fade.thread;
            if(!started || !pendingAdopted || !withheld || !hostOwn || !recovered)
                check(false,"an unread node, node handle or thread handle at 2/20 keeps D303 pending, off the host");
        }
        check(true,"an unread node, node handle or thread handle at 2/20 keeps D303 pending: withheld, then the owner's once read");
        (void)adoptFaulting(0,false,0x99);
        check(!g_axel.fade.thread && !g_axel.fade.pending,"a node whose handle answers with no thread adopts nothing (as before)");
        Setup(1u<<R::kOvertakerGroup);Frame();
        handles[kName]=kNameHandle;Put(kName,std::uint32_t{0x3031});node();running(kOther,kAxel);decodeFault=kThreadHandle;
        duringCallback=queue;(void)SmAxelComplete(Claim(),nullptr);duringCallback=nullptr;
        decodeFault=0;handles.erase(kThread);running(kThread,kAxel); // read later: the handle names no thread
        check(!fade(Kind::WhiteOut,70.0f) && !g_axel.fade.pending && !g_axel.fade.thread,
              "a pending adoption whose thread then reads as never queued is released, and that fade is native");
        (void)friendClearLight();
        running(kOther,kAxel);
        check(!fade(Kind::WhiteOut,40.0f) && !SmAxelFade(Kind::Reset,nullptr),
              "while D303 is pending, the host's own fade or reset on any other thread stays native");
        running(kThread,kHostActor);
        check(!fade(Kind::WhiteOut,70.0f) && g_axel.fade.thread,"and so does one on that thread with another source");
        running(kThread,kAxel);roleCurrent=false;const auto sent=CameraOps().size();
        check(fade(Kind::WhiteOut,70.0f) && CameraOps().size()==sent,
              "once the owner's role has ended its ops are dropped, never shown on the host");
        roleCurrent=true;
        // Ione's 629ab5f1 (3): a later RC's role on the same seat and actor is not D303's.
        {
            const auto adopted=g_axel.fade.resource;const auto count=emitted.size();
            (void)SmOwnerBeginAxelRole(1,kFriendActor,kAxel,7,enemysync::scope);
            check(g_smRoles[1].resource!=adopted && fade(Kind::Reset,0) && emitted.size()==count,
                  "a replacement RC's role never carries the retired one's D303, nor does the host show it");
            g_smRoles[1].resource=adopted;g_smRoles[1].connection=0x3333;
            check(fade(Kind::Reset,0) && emitted.size()==count,"nor a role with its resource under another connection");
            g_smRoles[1].connection=0x2222;
            check(fade(Kind::Reset,0) && emitted.size()==count+1 && emitted.back().effect==adopted,
                  "the adopted role itself (as a continuation keeps it) still gets them");
        }
        roleCurrent=true;SmAxelHostReset();
        check(!g_axel.actor && fade(Kind::WhiteIn,30.0f),"D303 stays the owner's after the host's Axel is gone");
        (void)friendClearLight();running(kThread,kAxel);g_frameCounter+=R::kAxelFadeLeaseFrames+1;
        check(!fade(Kind::WhiteOut,70.0f) && !g_axel.fade.thread,"a D303 that never whites in expires at its lease");
        (void)friendClearLight();running(kThread,kAxel);++enemysync::scope.epoch;
        check(fade(Kind::WhiteOut,70.0f) && g_axel.fade.thread && CameraOps().empty(),"another epoch blocks delivery without retiring custody or restoring native");
        Setup(1u<<R::kOvertakerGroup);Frame();
        handles[kName]=kNameHandle;Put(kName,std::uint32_t{0x3031});node();
        running(kOther,kAxel);duringCallback=queue;emitOk=false;
        (void)SmAxelComplete(Claim(),nullptr);duringCallback=nullptr;emitOk=true;
        check(g_axel.cameraCustody==R::AxelCameraCustody::Host && !g_axel.fade.thread,
              "a sequence that stayed the host's adopts nothing, so D303 plays on the host");
        label=304;(void)friendClearLight();label=303;
        check(!g_axel.fade.thread,"no other thread is adopted");
        Setup(1u<<R::kOvertakerGroup);Frame();
        handles[kName]=kNameHandle;Put(kName,std::uint32_t{0x3031});node();
        running(kOther,kHostActor);duringCallback=queue;(void)SmAxelComplete(Claim(),nullptr);duringCallback=nullptr;
        check(g_axel.cameraCustody==R::AxelCameraCustody::Owner && !g_axel.fade.thread,
              "nor a 2/20 whose VM source is not Axel's");
        Setup(1u<<R::kOvertakerGroup);g_seats[1].living=false;Frame();
        node();running(kOther,kAxel);
        bool watched=true;
        duringCallback=[&] { watched=SmAxelThreadWatched();queue(); };
        (void)SmAxelComplete(Claim(),nullptr);duringCallback=nullptr;
        check(!g_axel.fade.thread && watched && g_axel.nativeFades.size()==1 &&
              g_axel.nativeFades.front().thread==kThread && g_axel.nativeFades.front().threadHandle==kThreadHandle,
              "the host's own RC supplies native thread proof without adopting any Owner binding");
        Setup(1u<<R::kOvertakerGroup);Frame();reads=0;
        check(!SmAxelThreadWatched() && !SmAxelFadePending() && !SmAxelFade(Kind::WhiteOut,nullptr) && !reads,
              "with no retained proof and no validated callback running, the 2/20, fade and reset hooks read nothing");
    }
    {
        // Every op-17 kind the custody path can build passes both of the real wire rules.
        ScriptActorRole role{81,kFriendActor,kAxel,false};role.resource=0x80000009u;role.netId=7;role.slot=1;role.thread=0x900;
        const auto before=wireRefused;emitted.clear();emitOk=true;
        const bool all=SmEmit(SmAxelCameraOpRecord(role,R::AxelCameraKind::Watch,0)) &&
            SmEmit(SmAxelCameraOpRecord(role,R::AxelCameraKind::Reset,0)) &&
            SmEmit(SmAxelCameraOpRecord(role,R::AxelCameraKind::WhiteOut,70.0f)) &&
            SmEmit(SmAxelCameraOpRecord(role,R::AxelCameraKind::WhiteIn,30.0f));
        const auto codec=[&](std::uint32_t kind,std::uint64_t argument) {
            auto c=SmAxelCameraOpRecord(role,R::AxelCameraKind::Watch,0);c.values[0]=R::Tagged(R::kTagInt,kind);c.values[1]=argument;
            kh2coop::ScriptCallEntry e{};e.netId=c.netId;e.objectId=c.objectId;e.table=255;e.index=17;e.effect=c.effect;
            for(unsigned i=0;i<4;++i)e.args[i]=c.values[i];
            return kh2coop::validScriptCallEntry(e);
        };
        check(codec(1,R::Tagged(R::kTagInt,0)) && !codec(0,R::Tagged(R::kTagInt,0)) && !codec(5,emitted.empty()?0:R::Tagged(R::kTagFloat,0x428C0000)) &&
              !codec(3,R::Tagged(R::kTagInt,70)),"the codec itself refuses another kind, or a fade without its float");
        check(all && wireRefused==before && emitted.size()==4 && R::TagOf(emitted[2].values[1])==R::kTagFloat &&
              R::FloatOf(emitted[2].values[1])==70.0f && R::TagOf(emitted[1].values[1])==R::kTagInt,
              "watch, reset, white-out and white-in records pass the client's and the codec's rules");
    }

    // The friend's replay of op 17 on its own Axel role, and the white-in its retirement owes.
    {
        for(const std::uint32_t refused:{0x430B80u,0x430B60u,0x41DD70u,0x41DDB0u}) {
            refusedPin=refused;SmAxelCameraNativesInstall(0x140000000,&Pin);
            if(SmAxelCameraNativesReady() || g_smAxelNatives.watch || g_smAxelNatives.whiteIn)
                check(false,"a refused replay pin resolves no native");
        }
        refusedPin=0;SmAxelCameraNativesInstall(0x140000000,nullptr);
        check(!SmAxelCameraNativesReady(),"each of the four replay pins is needed (Ione's 629ab5f1: verified, not just assigned)");
        SmAxelCameraNativesInstall(0x140000000,&Pin);
        const auto at=[](auto fn) { return reinterpret_cast<uintptr_t>(fn)-0x140000000; };
        check(SmAxelCameraNativesReady() && at(g_smAxelNatives.targetInit)==0x3BDA50 && at(g_smAxelNatives.targetSet)==0x3BEE60 &&
              at(g_smAxelNatives.watch)==0x176310 && at(g_smAxelNatives.reset)==0x430B60 &&
              at(g_smAxelNatives.whiteOut)==0x41DD70 && at(g_smAxelNatives.whiteIn)==0x41DDB0,
              "the replay resolves 1/207's body, 1/295, 0/31 and 0/32 at their pinned addresses");
        Setup(0);g_sm.hostActive=false;g_soraActor=kFriendActor;natives={};
        g_smAxelNatives={&TargetInit,&TargetSet,&Watch,&Reset,&WhiteOut,&WhiteIn};
        ScriptActorRole role{81,kFriendActor,kAxel,false};role.resource=0x80000011u;role.netId=7;role.slot=1;
        const auto op=[&](R::AxelCameraKind kind,float value,uintptr_t source=kAxel) {
            const auto call=SmAxelCameraOpRecord(role,kind,value);return SmAxelApplyCameraOp(role,call,source);
        };
        const auto calls=[] { std::string all;for(const auto& c:natives.calls)all+=c+" ";return all; };
        check(op(R::AxelCameraKind::Watch,0) && calls()=="init set watch " && natives.zeroed && natives.subject==kAxel &&
              !natives.zero && role.axelWatched,"a watch replays 1/207's body on a zeroed target, watching the Axel clone");
        natives={};
        check(op(R::AxelCameraKind::Reset,0) && calls()=="reset ","a reset replays 1/295");
        natives={};
        check(op(R::AxelCameraKind::WhiteOut,70.0f) && calls()=="whiteout " && natives.fade==70.0f && role.axelWhitedOut,
              "a white-out replays 0/31 with D303's argument and leaves a receipt");
        natives={};
        check(op(R::AxelCameraKind::WhiteIn,30.0f) && calls()=="whitein " && natives.fade==30.0f && !role.axelWhitedOut,
              "its white-in replays 0/32 and clears it");
        natives={};
        check(!op(R::AxelCameraKind::Watch,0,kHostActor) && natives.calls.empty(),"never with another source");
        auto spectator=role;spectator.actor=0;auto other=role;other.actor=kHostActor;
        check(!SmAxelApplyCameraOp(spectator,SmAxelCameraOpRecord(role,R::AxelCameraKind::WhiteOut,70.0f),kAxel) &&
              !SmAxelApplyCameraOp(other,SmAxelCameraOpRecord(role,R::AxelCameraKind::WhiteOut,70.0f),kAxel) &&
              natives.calls.empty(),"never on a spectator's role or another player's: only the owner's own game");
        auto bad=SmAxelCameraOpRecord(role,R::AxelCameraKind::Watch,0);bad.values[1]=R::Tagged(R::kTagInt,9);
        auto unknown=SmAxelCameraOpRecord(role,R::AxelCameraKind::Watch,0);unknown.values[0]=R::Tagged(R::kTagInt,5);
        check(!SmAxelApplyCameraOp(role,bad,kAxel) && !SmAxelApplyCameraOp(role,unknown,kAxel) && natives.calls.empty(),
              "nor a record the wire rules would refuse");
        g_sm.hostActive=true;
        check(!op(R::AxelCameraKind::Reset,0) && natives.calls.empty(),"the host never replays one");
        g_sm.hostActive=false;const auto ready=g_smAxelNatives;g_smAxelNatives.watch=nullptr;
        check(!op(R::AxelCameraKind::Watch,0) && natives.calls.empty(),"nor does a friend without its natives resolved");
        g_smAxelNatives=ready;
        // Retirement.
        (void)op(R::AxelCameraKind::WhiteOut,70.0f);natives={};
        SmAxelRetireCamera(role);warp::loading=true;SmAxelClientCameraTick();
        check(g_axel.whiteInOwed && natives.calls.empty(),"a role retired whited out owes a white-in, held through a load");
        warp::loading=false;SmAxelClientCameraTick();
        check(!g_axel.whiteInOwed && calls()=="whitein " && natives.fade==30.0f,"and pays it on the next safe frame");
        natives={};SmAxelClientCameraTick();
        check(natives.calls.empty(),"once");
        role.axelWhitedOut=false;SmAxelRetireCamera(role);
        check(!g_axel.whiteInOwed,"a role with no white-out owes nothing");
        natives={};natives.fault=true;role.axelWhitedOut=false;
        check(!op(R::AxelCameraKind::WhiteOut,70.0f) && role.axelWhitedOut,
              "a white-out that faults keeps its receipt, so the screen is still restored");
        natives.fault=false;SmAxelRetireCamera(role);SmAxelClientCameraTick();
        check(calls()=="whiteout whitein ","and its retirement whites in");
        // Ione's 629ab5f1 (4): the debt stays until a payment returns.
        natives={};whiteInFaults=1;role.axelWhitedOut=true;SmAxelRetireCamera(role);SmAxelClientCameraTick();
        check(g_axel.whiteInOwed && calls()=="whitein " && natives.fade==0,"a white-in that faults is still owed");
        SmAxelClientCameraTick();
        check(!g_axel.whiteInOwed && calls()=="whitein whitein " && natives.fade==30.0f,
              "and is paid once on the next safe frame");
        natives={};whiteInFaults=5;SmAxelRetireCamera(role);
        for(unsigned i=0;i<5;++i)SmAxelClientCameraTick();
        check(!g_axel.whiteInOwed && natives.calls.size()==3,"one that keeps faulting is given up after three tries");
        whiteInFaults=0;
        // Kit's e0b6cad9: load complete fades every record in, so a load completed since is the white-in.
        natives={};role.axelWhitedOut=true;SmAxelRetireCamera(role);warp::loading=true;SmAxelClientCameraTick();
        ++warp::serial;warp::loading=false;SmAxelClientCameraTick();
        check(!g_axel.whiteInOwed && natives.calls.empty(),"a debt across a completed load is dropped, never paid over its fade-in");
        role.axelWhitedOut=true;role.load=warp::serial-1;SmAxelRetireCamera(role);
        check(!g_axel.whiteInOwed,"and a role retired after a load completed past its own owes nothing");
        role.load=warp::serial;SmAxelRetireCamera(role);SmAxelClientCameraTick();
        check(!g_axel.whiteInOwed && natives.calls.size()==1,"in the same load it is still paid");
        g_sm.hostActive=true;role.axelWhitedOut=true;SmAxelRetireCamera(role);
        check(!g_axel.whiteInOwed,"the host owes none");
    }

    // Ione's 629ab5f1 (2): custody needs all six of its host hooks, installed in order.
    {
        static std::vector<unsigned> tried;static unsigned refuse=99;
        const auto swap=+[](R::AxelCameraHook hook) { tried.push_back(static_cast<unsigned>(hook));return static_cast<unsigned>(hook)!=refuse; };
        constexpr unsigned kHooks=static_cast<unsigned>(R::AxelCameraHook::Count);
        constexpr uintptr_t kName=0x700000;
        for(refuse=0;refuse<kHooks;++refuse) {
            (void)FriendOvertaker();tried.clear();SmAxelCameraHooksInstall(swap);
            handles[kName]=0x70;Put(kName,std::uint32_t{0x3031});
            const std::uint64_t start[2]{R::Tagged(R::kTagAddress,kAxelHandle),R::Tagged(R::kTagAddress,0x70)};
            const std::uint64_t watch[1]{R::Tagged(R::kTagAddress,kAxelHandle)};
            const bool kept=!SmAxelCamera(start) && !SmAxelCameraWatch(watch);
            if(!kept || g_axel.cameraCustody!=R::AxelCameraCustody::Undecided || !Cameras().empty() || !CameraOps().empty() ||
               tried.size()!=refuse+1 || g_smAxelCameraHooks.Complete())
                check(false,"with one custody hook refused, the friend's start and watch stay on the host");
        }
        check(refuse==kHooks,"with any one of the six refused, a friend's start and watch play on the host and no later hook is tried");
        refuse=99;tried.clear();SmAxelCameraHooksInstall(swap);
        check(g_smAxelCameraHooks.Complete() && tried==std::vector<unsigned>{0,1,2,3,4,5},
              "all six in: start, watch, white-out, white-in, reset, then 2/20");
        SmAxelCameraHooksRevoke();
        check(!g_smAxelCameraHooks.Complete(),"and revoked before the slots are restored");
    }

    // The friend's side: the host's offer, its prompt proof and its press, which claims and runs nothing locally.
    {
        Setup(1u<<R::kOvertakerGroup);Frame();
        check(SmAxelKnownSource(kAxelHandle) && !SmAxelKnownSource(kHostHandle),"the host knows its own Axel as a source");
        Setup(0);g_sm.hostActive=false;enemysync::scope.localSlot=1;g_soraActor=kFriendActor;
        R::Call identity{};identity.netId=7;identity.objectId=81;identity.effect=R::AxelResource(R::kOvertaker);
        identity.trap=static_cast<std::uint8_t>(R::FindTrap(255,12));
        identity.values[0]=R::Tagged(R::kTagInt,1);identity.values[1]=R::Tagged(R::kTagInt,0x2222);
        identity.values[2]=R::Tagged(R::kTagInt,0);
        R::Call offer=identity;offer.trap=static_cast<std::uint8_t>(R::FindTrap(255,1));
        offer.values[0]=R::Tagged(R::kTagInt,10);offer.values[1]=R::Tagged(R::kTagInt,0);
        offer.values[2]=R::Tagged(R::kTagInt,R::kOvertaker);offer.values[3]=R::Tagged(R::kTagInt,1);
        check(SmAxelApplyIdentity(identity,R::kOvertaker,kAxel) && SmAxelApplyOffer(offer,R::kOvertaker,kAxel) &&
              g_axel.offers.Find(R::kOvertaker),"the friend keeps the host's offer to its own seat");
        constexpr uintptr_t kPart=0x600000;handles[kPart]=0x44;Put(kPart+0x118,kAxelHandle);
        uintptr_t source=0,native=0;R::NativeReactionIdentity id{};
        check(SmAxelProof(R::kOvertaker,{kAxelHandle,0,0x44,UINT32_MAX},enemysync::scope,0x2222,source,native,id) &&
              source==kAxel && native==kPart && id.family==3 && id.occurrence==10,"its native prompt proves against that window");
        check(!SmAxelProof(R::kOvertaker,{kAxelHandle,0,0x44,UINT32_MAX},enemysync::scope,0x1111,source,native,id),
              "never under another seat's connection");
        check(SmAxelReactionCallback(kAxel,kFriendActor,R::kOvertaker,nullptr) && enemysync::sentClaims==1 &&
              g_axel.offers.Find(R::kOvertaker)->consumed && !callbacks,"the friend's press sends one claim and runs nothing locally");
        check(SmAxelReactionCallback(kAxel,kFriendActor,R::kOvertaker,nullptr) && enemysync::sentClaims==1,
              "a second press sends nothing");
        SmAxelClientReset();
        check(!g_axel.offers.Find(R::kOvertaker),"a client reset drops its offers");
    }

    // The wire (VUH-2913, protocol 40): what the Axel role builds passes both of the real wire rules. Main's slice D
    // sent no camera or hold, because the rules refused the no-target camera and the bone-1 hold.
    {
        ScriptActorRole role{81,kFriendActor,kAxel,false};role.resource=0x80000009u;role.netId=7;role.slot=1;
        const auto passes=[](const R::Call& c) {
            const auto before=wireRefused;emitted.clear();emitOk=true;return SmEmit(c) && wireRefused==before;
        };
        check(passes(SmAxelHoldRecord(role,true)) && passes(SmAxelHoldRecord(role,false)) &&
              passes(SmAxelCameraRecord(role,0x3030)),
              "the Axel role's hold, release and no-target camera records pass the client's and the codec's rules");
        auto thorn=SmOwnerRecord(role,13);thorn.values[0]=R::Tagged(R::kTagInt,16386);thorn.values[1]=R::Tagged(R::kTagInt,6);
        auto target=SmAxelCameraRecord(role,0x3030);target.values[1]=R::Tagged(R::kTagInt,3);
        const auto before=wireRefused;
        check(!SmEmit(thorn) && !SmEmit(target) && wireRefused==before+2,"another bone or camera target is still refused");
        wireRefused=before;
    }
    check(!wireRefused,"no record any Axel path built in this run was refused by the wire rules");

    std::printf("%u failure(s)\n",errors);
    return errors?1:0;
}
