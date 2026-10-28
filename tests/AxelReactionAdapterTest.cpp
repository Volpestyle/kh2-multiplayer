// VUH-2913: the Axel II adapter itself (inject/src/AxelReactions.inl), compiled against simulated native memory and
// stand-ins for ScriptMirror/ScriptActorOwner (after Ione's review probes). It drives the real host frame, completion,
// 1/3 decision and the safe abort at 1/18 and 1/29 that the hooks call. The hook bodies themselves (SmOwnerPlayer,
// SmOwnerAttach, SmOwnerCapture) only call these, and the ScriptActorOwner role is a recorder here.
#include "ScriptMirror.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

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
static std::map<uintptr_t,std::uint32_t> handles;
template<class T> static void Put(uintptr_t at,const T& value) {
    auto& bytes=memory[at];bytes.resize(sizeof(T));std::memcpy(bytes.data(),&value,sizeof(T));
}
static bool ReadHitTraceMemory(uintptr_t at,void* out,std::size_t size) {
    const auto it=memory.find(at);
    if(it==memory.end() || it->second.size()<size)return false;
    std::memcpy(out,it->second.data(),size);return true;
}
template<class T> static bool ReadHitTrace(uintptr_t at,T& out) { return ReadHitTraceMemory(at,&out,sizeof(T)); }
static uintptr_t SmDecode(std::uint32_t handle) { for(const auto& [actor,h]:handles)if(h==handle)return actor;return 0; }
static std::uint32_t SmOwnHandle(uintptr_t actor) noexcept {
    std::uint32_t handle=0;
    if(!ReadHitTrace(actor+4,handle) || !handle || SmDecode(handle)!=actor)return 0;
    return handle;
}
// As ScriptMirror's: the argument names an object, whose +4 names the actor.
static uintptr_t SmActorOf(std::uint64_t arg,std::uint32_t& objectId) noexcept {
    objectId=0;std::uint32_t handle=0;
    const auto object=R::TagOf(arg)==R::kTagAddress?SmDecode(R::BitsOf(arg)):0;
    return object && ReadHitTrace(object+4,handle)?SmDecode(handle):0;
}
static bool scopeOk=true;
static bool SmOwnerScope(enemysync::DownedScope& out) { out=enemysync::scope;return scopeOk; }
static std::array<R::ReactionSeat,3> g_seats;
static std::array<R::ReactionSeat,3> SmOwnerLivingSeats(const enemysync::DownedScope&) { return g_seats; }
namespace enemytarget {
static struct { std::array<uintptr_t,3> candidateActor{};std::array<std::uint8_t,3> ownerSlot{0,1,2}; } g;
}
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
static uintptr_t SmDrivenActor(std::uint16_t netId,std::uint32_t objectId) {
    return netId==7 && objectId==81?uintptr_t{0x100000}:0;
}
static bool SmEmit(const R::Call& c) { return R::ValidCall(c); }
static void Log(const char*,...) {}
// ScriptActorOwner's side: the owner role and its attachment record, recorded.
struct RoleBegin { std::uint8_t slot{};uintptr_t actor{},source{};std::uint16_t netId{}; };
static std::vector<RoleBegin> roleBegins;
static bool roleBeginOk=true;
static bool SmOwnerBeginAxelRole(std::uint8_t slot,uintptr_t actor,uintptr_t source,std::uint16_t netId,
                                 const enemysync::DownedScope&) {
    roleBegins.push_back({slot,actor,source,netId});return roleBeginOk;
}
struct Hold { std::uint8_t slot{};uintptr_t actor{};bool held{}; };
static std::vector<Hold> holds;
static bool SmOwnerEmitAxelHold(std::uint8_t slot,uintptr_t actor,bool held) { holds.push_back({slot,actor,held});return true; }
struct Camera { std::uint8_t slot{};uintptr_t actor{};std::uint32_t fourcc{},target{}; };
static std::vector<Camera> cameras;
static bool cameraEmitOk=true;
static bool SmOwnerEmitAxelCamera(std::uint8_t slot,uintptr_t actor,std::uint32_t fourcc,std::uint32_t target) {
    cameras.push_back({slot,actor,fourcc,target});return cameraEmitOk;
}
static unsigned callbacks=0;
static uintptr_t callbackPlayer=0;
static std::size_t rolesAtCallback=0;
static void Callback(uintptr_t,uintptr_t player,std::uint32_t,const std::uint32_t*) {
    ++callbacks;callbackPlayer=player;rolesAtCallback=roleBegins.size();
}
static void (*g_smReactionCallback)(uintptr_t,uintptr_t,std::uint32_t,const std::uint32_t*)=&Callback;
static uintptr_t g_soraActor=0x200000;
static std::uint32_t g_frameCounter=10;
static struct { bool hostActive=true; } g_sm;
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
    g_axel={};memory.clear();handles={{kAxel,kAxelHandle},{kHostActor,kHostHandle},{kFriendActor,kFriendHandle}};
    enemytarget::g.candidateActor={kHostActor,kFriendActor,0};
    enemysync::scope={};enemysync::session=0xA1;enemysync::sentClaims=0;g_sm.hostActive=true;g_frameCounter=10;
    scopeOk=true;roleBeginOk=true;roleBegins.clear();holds.clear();cameras.clear();cameraEmitOk=true;callbacks=0;callbackPlayer=0;downedspike::down=false;
    g_soraActor=kHostActor;
    Put(kAxel+0x100,uintptr_t{0x400000});Put(uintptr_t{0x400000},std::uint32_t{81});
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
    Put(kAxel+0x6A0,parent);Put(kAxel+0x6A4,std::uint16_t{16386});Put(kAxel+0x6A6,std::uint16_t{1});
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
        Setup(1u<<R::kOvertakerGroup);Frame();
        check(!g_axel.executor.command && Act("attach",kAxelHandle,kHostHandle)==R::AxelAct::Refused &&
              Act("capture",kAxelHandle,kFriendHandle)==R::AxelAct::Refused,"with no RC bound, Axel acts on no player");
        (void)FriendOvertaker();scopeOk=false;
        check(Act("attach",kAxelHandle,kFriendHandle)==R::AxelAct::Refused,"an unreadable scope refuses too");
        g_sm.hostActive=false;
        check(Act("attach",kAxelHandle,kFriendHandle)==R::AxelAct::Native,"a client's own traps are not the host's abort");
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
        const bool first=holds.size()==1 && !holds[0].held && holds[0].slot==1 && holds[0].actor==kFriendActor;
        HoldAxel(kFriendHandle);++g_frameCounter;Frame();++g_frameCounter;Frame();
        const bool held=holds.size()==2 && holds[1].held;
        check(first && held,"the friend's role is told Axel is free, then held, once each");
        memory.erase(kAxel+0x6A0);++g_frameCounter;Frame();
        check(holds.size()==2,"an unread parent sends nothing");
        HoldAxel(kHostHandle);Put(kAxel+0x6A0,kHostHandle);++g_frameCounter;Frame();
        check(holds.size()==3 && !holds[2].held,"Axel held by another actor is not the friend's hold");
        Setup(1u<<R::kOvertakerGroup);g_seats[1].living=false;Frame();
        const bool ran=SmAxelComplete(Claim(),nullptr);
        HoldAxel(kHostHandle);++g_frameCounter;Frame();
        check(ran && g_axel.executor.slot==0 && holds.empty(),"the host's own RC sends no hold");
    }

    // Cameras: Axel's camera_start_global (1/164) during a friend's RC goes to its owner, and the host skips it.
    {
        constexpr uintptr_t kName=0x700000;constexpr std::uint32_t kNameHandle=0x70;
        const auto start=[&](std::uint32_t object) {
            const std::uint64_t args[2]{R::Tagged(R::kTagAddress,object),R::Tagged(R::kTagAddress,kNameHandle)};
            return SmAxelCamera(args);
        };
        (void)FriendOvertaker();handles[kName]=kNameHandle;Put(kName,std::uint32_t{0x3030}); // "00"
        check(start(kAxelHandle) && cameras.size()==1 && cameras[0].slot==1 && cameras[0].actor==kFriendActor &&
              cameras[0].fourcc==0x3030 && cameras[0].target==R::kAxelCameraNone,
              "a friend's Overtaker camera on Axel goes to the friend with no target, and the host skips it");
        const auto sent=cameras.size();
        check(!start(kHostHandle) && cameras.size()==sent,"another object's camera stays native");
        Put(kName,std::uint32_t{0x0130});
        check(!start(kAxelHandle) && cameras.size()==sent,"a name that is not a printable fourcc stays native");
        Put(kName,std::uint32_t{0x3130});cameraEmitOk=false;
        check(!start(kAxelHandle),"if the record cannot be sent, the host plays it natively");
        cameraEmitOk=true;Put(kFriendActor+0xD48,uintptr_t{0x777});
        check(!start(kAxelHandle),"without the friend's held role (Burst's hook role, or none) it stays native");
        g_sm.hostActive=false;
        check(!start(kAxelHandle),"a client's own starts stay native");
        Setup(1u<<R::kOvertakerGroup);g_seats[1].living=false;Frame();handles[kName]=kNameHandle;Put(kName,std::uint32_t{0x3030});
        const bool ran=SmAxelComplete(Claim(),nullptr);
        check(ran && !start(kAxelHandle) && cameras.empty(),"the host's own RC camera plays on the host");
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

    std::printf("%u failure(s)\n",errors);
    return errors?1:0;
}
