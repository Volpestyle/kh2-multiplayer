#pragma once
// VUH-2913: Axel II's (02/14 btl137, B_EX110 objectId 81) reaction commands. They are offered by REACTION
// collision rows on Axel through the native picker (408391 -> 407B40 -> 408BD0), which offers each game's own
// canonical player only, so the host mints one owner per offer here and both seats' presses go through it.
// Native facts and their RVAs: docs/AXEL_REACTIONS.md. Pure rules; no native memory.
#include "ScriptReaction.hpp"

#include <array>
#include <cmath>
#include <cstdint>

namespace kh2coop::inject::scriptmirror::rules {

inline constexpr std::uint32_t kAxelIIObjectId=kh2coop::enemyclass::AxelII;
inline constexpr std::uint16_t kAxelIIWorld=2,kAxelIIRoom=0x14,kAxelIIBattle=137;
inline constexpr std::uint16_t kOvertaker=490,kClearLight=443,kBurstFrontier=444;
inline constexpr std::array<std::uint16_t,3> kAxelCommands{kOvertaker,kClearLight,kBurstFrontier};
// Wire resources for Axel windows: a band role resources (0x80000000|sequence) never reach, because
// ScriptActorOwner stops minting roles below it.
inline constexpr std::uint32_t kAxelResourceBand=0xFFFF0000u;
inline constexpr std::uint32_t kOwnerRoleSequenceCap=0x7FFEFFFFu;

inline int AxelCommandIndex(std::uint32_t command) noexcept {
    for(unsigned i=0;i<kAxelCommands.size();++i)if(kAxelCommands[i]==command)return static_cast<int>(i);
    return -1;
}
inline bool AxelEncounter(std::uint16_t world,std::uint16_t room,std::uint16_t battle) noexcept {
    return world==kAxelIIWorld && room==kAxelIIRoom && battle==kAxelIIBattle;
}
// The host's per-frame mint runs only for the host seat itself, in Axel II's encounter; everywhere else it does
// nothing (and reads nothing).
inline bool AxelHostFrameAdmitted(bool hostActive,std::uint8_t localSlot,std::uint16_t world,std::uint16_t room,
                                  std::uint16_t battle) noexcept {
    return hostActive && localSlot==0 && AxelEncounter(world,room,battle);
}
inline std::uint32_t AxelResource(std::uint16_t command) noexcept { return kAxelResourceBand|command; }
inline bool AxelResourceFor(std::uint32_t resource,std::uint32_t objectId,std::uint16_t& command) noexcept {
    command=static_cast<std::uint16_t>(resource&0xFFFFu);
    return (resource&0xFFFF0000u)==kAxelResourceBand && objectId==kAxelIIObjectId && AxelCommandIndex(command)>=0;
}

// 3BA550: script layer (+0x5A8 on / +0x5AC off), then motion layer (+0x5A0 / +0x5A4), then the model's default
// mask (second word of the collision header at +0xAB8). An actor without collision data has no active group.
struct AxelGroupMasks {
    std::uint32_t motionOn{},motionOff{},scriptOn{},scriptOff{},defaults{};
    bool collision{};
};
inline bool AxelGroupActive(const AxelGroupMasks& m,unsigned group) noexcept {
    if(!m.collision || group>=32)return false;
    const auto bit=1u<<group;
    if(m.scriptOn&bit)return true;
    if(m.scriptOff&bit)return false;
    if(m.motionOn&bit)return true;
    if(m.motionOff&bit)return false;
    return (m.defaults&bit)!=0;
}
inline constexpr unsigned kOvertakerGroup=20,kBurstFrontierGroupA=23,kBurstFrontierGroupB=24;
inline constexpr std::uintptr_t kAxelMotionOn=0x5A0,kAxelMotionOff=0x5A4,kAxelScriptOn=0x5A8,kAxelScriptOff=0x5AC,
    kAxelCollision=0xAB8,kAxelCollisionDefaults=4;
// The reads behind AxelGroupMasks, through read(address, value&). False when any of them fails, the collision
// header's default mask included: an unread mask is unknown, never "no active group". Only a collision pointer read
// as zero is an actor without collision data.
template<class Read>
bool AxelReadMasks(const Read& read,std::uintptr_t actor,AxelGroupMasks& m) noexcept {
    m={};std::uintptr_t collision=0;
    if(!read(actor+kAxelMotionOn,m.motionOn) || !read(actor+kAxelMotionOff,m.motionOff) ||
       !read(actor+kAxelScriptOn,m.scriptOn) || !read(actor+kAxelScriptOff,m.scriptOff) ||
       !read(actor+kAxelCollision,collision))return false;
    if(collision && !read(collision+kAxelCollisionDefaults,m.defaults))return false;
    m.collision=collision!=0;
    return true;
}

// REACTION rows on the root (bone 16384), actor-local, each with its collision group. Local -z is Axel's forward
// (his forward attack rows, group 8 at z=-228); ROT_Y=atan2(vx,vz), so world forward is (sin, cos).
struct AxelSphere { float x{},y{},z{},r{}; unsigned group{}; };
inline constexpr std::array<AxelSphere,1> kOvertakerSpheres{{{0,0,0,2800,kOvertakerGroup}}};
// 58A opens group 23 (the line of seven), 59A group 24 (the three above).
inline constexpr std::array<AxelSphere,10> kBurstFrontierSpheres{{
    {0,550,-267,400,kBurstFrontierGroupB},{0,150,0,400,kBurstFrontierGroupB},{0,350,-126,400,kBurstFrontierGroupB},
    {0,0,-150,200,kBurstFrontierGroupA},{0,0,-350,200,kBurstFrontierGroupA},{0,0,-550,200,kBurstFrontierGroupA},
    {0,0,-750,200,kBurstFrontierGroupA},{0,0,-929,200,kBurstFrontierGroupA},{0,0,-1178,200,kBurstFrontierGroupA},
    {0,0,-1444,200,kBurstFrontierGroupA}}};
inline constexpr float kAxelSeatTolerance=100.0f; // the player's own body, not modeled per row
inline ReactionPoint AxelSphereWorld(const AxelSphere& s,ReactionPoint origin,float yaw) noexcept {
    const float forward=-s.z,sinYaw=std::sin(yaw),cosYaw=std::cos(yaw);
    return {origin.x+forward*sinYaw+s.x*cosYaw,origin.y+s.y,origin.z+forward*cosYaw-s.x*sinYaw};
}
// The seat inside any of the family's spheres whose group is active now that is nearest Axel; equal distance goes
// to the lower seat. A sphere in an inactive group offers nothing natively, so it never wins an owner. Downed,
// stale or missing seats never win.
template<std::size_t N>
inline int AxelFamilyWinner(const std::array<AxelSphere,N>& spheres,const AxelGroupMasks& masks,ReactionPoint origin,
                            float yaw,const std::array<ReactionSeat,3>& seats) noexcept {
    if(!Finite(origin) || !std::isfinite(yaw))return -1;
    int winner=-1;double nearest=0;
    for(unsigned i=0;i<seats.size();++i) {
        const auto& seat=seats[i];
        if(!seat.connection || !seat.living || !seat.fresh || !seat.capable || !Finite(seat.position))continue;
        bool inside=false;
        for(const auto& sphere:spheres) {
            if(!AxelGroupActive(masks,sphere.group))continue;
            const auto c=AxelSphereWorld(sphere,origin,yaw);
            const double x=seat.position.x-c.x,y=seat.position.y-c.y,z=seat.position.z-c.z;
            const double reach=static_cast<double>(sphere.r)+kAxelSeatTolerance;
            if(x*x+y*y+z*z<=reach*reach){inside=true;break;}
        }
        if(!inside)continue;
        const double x=seat.position.x-origin.x,y=seat.position.y-origin.y,z=seat.position.z-origin.z;
        const double distance=x*x+y*y+z*z;
        if(winner<0 || distance<nearest){winner=static_cast<int>(i);nearest=distance;}
    }
    return winner;
}

// Clear Light (443, flag bit 6, Require 490) follows the Overtaker its owner is playing: the native refresh
// (407C2B) offers it only while that actor's +0x18C bit 21 is set and +0x638 equals 490 (AP12's trigger 26).
struct AxelExecutorFacts {
    bool current{};
    std::uint16_t command{};
    std::uint8_t slot{0xFF};
    std::uint32_t followup{},flags{}; // the executor actor's +0x638 and +0x18C
};
inline int AxelChainOwner(const AxelExecutorFacts& e) noexcept {
    return e.current && e.command==kOvertaker && e.slot<3 && e.followup==kOvertaker && ((e.flags>>21)&1u) ?
        static_cast<int>(e.slot) : -1;
}
inline constexpr std::uintptr_t kAxelHandleWord=4,kAxelFollowup=0x638,kAxelFlags=0x18C;
// The executor's actor this frame, through read(address, value&) and decode(handle): its handle word read and still
// naming it (Resolves), read and naming another actor or none (Gone), or unread (Unknown). Unknown is never Gone.
enum class AxelActorState : std::uint8_t { Resolves, Gone, Unknown };
template<class Read,class Decode>
AxelActorState AxelReadActor(const Read& read,const Decode& decode,std::uintptr_t actor,std::uint32_t handle) noexcept {
    if(!actor || !handle)return AxelActorState::Gone;
    std::uint32_t now=0;
    if(!read(actor+kAxelHandleWord,now))return AxelActorState::Unknown;
    return now==handle && decode(now)==actor?AxelActorState::Resolves:AxelActorState::Gone;
}
// Fills a bound executor's chain facts (e.current set by the caller) from its actor. False when they could not be
// read (the actor's identity or either follow-up field): Clear Light's offer is then unknown, not ended. A gone
// actor has no chain.
template<class Read,class Decode>
bool AxelReadChain(const Read& read,const Decode& decode,std::uintptr_t actor,std::uint32_t handle,
                   AxelExecutorFacts& e) noexcept {
    if(!e.current)return true;
    const auto state=AxelReadActor(read,decode,actor,handle);
    if(state==AxelActorState::Unknown)return false;
    if(state==AxelActorState::Gone){e.current=false;return true;}
    return read(actor+kAxelFollowup,e.followup) && read(actor+kAxelFlags,e.flags);
}

// Host: one window per command. A window opens once per activation for the seat that wins it then, keeps that
// owner while the offer stays active, and is consumed at most once. Only the next activation opens a new
// occurrence, so a consumed offer is never offered again while it is still up.
inline constexpr std::uint32_t kAxelHostWindowFrames=0x7FFFFFFFu; // bounded by the activation instead
inline constexpr std::uint32_t kAxelClientWindowFrames=180;
// A claim is admitted only on the frame of a successful observation or the one after it (a claim can be handled
// before that frame's mint runs).
inline constexpr std::uint32_t kAxelObservationFrames=1;
// One host frame's facts: whether Axel's masks and transform were read, and per command whether its own facts were
// read (Clear Light's chain), whether its offer is up and which seat won it.
struct AxelFrameFacts {
    bool observed{};
    std::array<bool,3> known{true,true,true};
    std::array<bool,3> active{};
    std::array<int,3> winners{-1,-1,-1};
};
class AxelWindows {
public:
    // The whole host frame. An observation that fails withholds every open window, and a command whose own facts
    // were not read withholds its own: it is no longer claimable, but its occurrence stays as a tombstone, so the
    // same activation never mints a second one when reads recover; only an observed end of the offer clears it.
    // Returns the indices opened this frame as bits.
    unsigned Frame(const AxelFrameFacts& f,std::uint16_t source,std::uint32_t epoch,std::uint32_t frame,
                   const std::array<ReactionSeat,3>& seats) noexcept {
        if(!f.observed) {
            observed_=false;
            for(auto& w:windows_)if(w.occurrence)w.consumed=true;
            return 0;
        }
        observed_=true;observedFrame_=frame;
        unsigned opened=0;
        for(unsigned i=0;i<windows_.size();++i) {
            if(!f.known[i]){if(windows_[i].occurrence)windows_[i].consumed=true;continue;}
            if(Tick(i,f.active[i],f.winners[i],source,epoch,frame,seats))opened|=1u<<i;
        }
        return opened;
    }
    bool ObservationCurrent(std::uint32_t frame) const noexcept {
        return observed_ && frame>=observedFrame_ && frame-observedFrame_<=kAxelObservationFrames;
    }
    // Returns the window opened by this tick, or nullptr.
    const ReactionWindow* Tick(unsigned index,bool active,int winner,std::uint16_t source,std::uint32_t epoch,
                               std::uint32_t frame,const std::array<ReactionSeat,3>& seats) noexcept {
        if(index>=windows_.size())return nullptr;
        auto& w=windows_[index];
        if(!active || (w.occurrence && w.epoch!=epoch)){w={};return nullptr;}
        if(w.occurrence){(void)RefreshReaction(w,epoch,frame,seats);return nullptr;}
        if(winner<0 || winner>=3 || !seats[static_cast<unsigned>(winner)].connection || !source || !epoch ||
           next_==UINT64_MAX)return nullptr;
        w={++next_,seats[static_cast<unsigned>(winner)].connection,epoch,frame,kAxelHostWindowFrames,source,
            kAxelCommands[index],static_cast<std::uint8_t>(winner),false};
        return &w;
    }
    ReactionWindow* Find(std::uint16_t command) noexcept {
        const int index=AxelCommandIndex(command);
        return index>=0 && windows_[static_cast<unsigned>(index)].occurrence?&windows_[static_cast<unsigned>(index)]:nullptr;
    }
    void Reset() noexcept { windows_={};observed_=false;observedFrame_=0; } // the sequence never rewinds
private:
    std::array<ReactionWindow,3> windows_{};
    std::uint64_t next_{};
    bool observed_{};
    std::uint32_t observedFrame_{};
};

// Client: the host's offers to this seat. The newest occurrence per command; an older one is refused. Order and
// idempotency hold only within one authority: the host session, its epoch and Axel's netId. A restarted server may
// reuse an epoch with a fresh sequence, so another session is a new history, never compared with the old one. The
// adapter also resets these with its room, load, transition and session resets.
class AxelOffers {
public:
    bool Apply(std::uint64_t occurrence,std::uint16_t command,std::uint8_t slot,std::uint8_t localSlot,
               std::uint64_t connection,std::uint16_t source,std::uint32_t epoch,std::uint32_t frame,
               std::uint64_t session) noexcept {
        const int index=AxelCommandIndex(command);
        if(index<0 || !occurrence || slot>=3 || !connection || !source || !epoch || !session)return false;
        auto& w=windows_[static_cast<unsigned>(index)];
        auto& from=sessions_[static_cast<unsigned>(index)];
        const bool sameAuthority=from.session==session && from.epoch==epoch && from.source==source;
        if(sameAuthority && occurrence<from.occurrence)return false;
        if(sameAuthority && occurrence==from.occurrence)return from.slot==slot && from.connection==connection;
        from={session,occurrence,connection,epoch,source,slot};
        if(slot!=localSlot){w={};return true;} // another seat's offer ends ours: one owner per offer
        w={occurrence,connection,epoch,frame,kAxelClientWindowFrames,source,command,slot,false};
        return true;
    }
    ReactionWindow* Find(std::uint16_t command) noexcept {
        const int index=AxelCommandIndex(command);
        return index>=0 && windows_[static_cast<unsigned>(index)].occurrence?&windows_[static_cast<unsigned>(index)]:nullptr;
    }
    void Reset() noexcept { windows_={};sessions_={}; }
private:
    // The last offer seen per command, whoever it named (its high-water mark), and the authority it came from.
    struct Seen {
        std::uint64_t session{},occurrence{},connection{};
        std::uint32_t epoch{};
        std::uint16_t source{};
        std::uint8_t slot{0xFF};
    };
    std::array<ReactionWindow,3> windows_{};
    std::array<Seen,3> sessions_{};
};

// A press on either seat: only the seat that owns the live window may act. The host executes its own; a
// friend claims and the host consumes (ConsumeReaction) before running anything.
enum class AxelPress : std::uint8_t { Refused, Execute, Claim };
inline AxelPress AxelPressDecision(const ReactionWindow* w,std::uint16_t command,std::uint8_t localSlot,
    std::uint64_t connection,std::uint32_t epoch,std::uint32_t frame,bool locallyDowned) noexcept {
    if(!w || locallyDowned || localSlot>=3 || !connection || !ReactionLive(*w,epoch,frame) ||
       w->command!=command || w->slot!=localSlot || w->connection!=connection)return AxelPress::Refused;
    return localSlot==0?AxelPress::Execute:AxelPress::Claim;
}

// Any seat may own and execute an Axel RC. The host runs a friend's with that seat's actor on the host, and the safe
// abort below keeps a lost executor's RC from acting on anyone else.
inline bool AxelExecutionAdmitted(std::uint8_t ownerSlot) noexcept { return ownerSlot<3; }
// The host's consumption of an Axel window: ConsumeReaction, for an owner admitted to execute.
inline bool AxelConsume(ReactionWindow& w,const ReactionCompletion& c,std::uint32_t epoch,std::uint32_t frame,
                        const std::array<ReactionSeat,3>& seats) noexcept {
    return AxelExecutionAdmitted(c.slot) && ConsumeReaction(w,c,epoch,frame,seats);
}

// The RC's executor: the seat, and the actor on the host, that ran it. It is bound to that actor, not to the seat's
// eligibility: a downed seat, or one that left the eligible table, still owns the RC its actor is in. It ends only
// on a confirmed release (Axel's attachment to and capture by that actor both seen, then both read and gone), a new
// epoch, or, if it never engaged, its lease. An unreadable attachment, capture or executor identity is unknown, never
// a release.
inline constexpr std::uint32_t kAxelExecutorLeaseFrames=1200;
struct AxelExecutor {
    std::uint16_t command{},source{};
    std::uint8_t slot{0xFF};
    std::uint64_t connection{};
    std::uint32_t epoch{},admittedFrame{};
    bool engaged{};
};
inline bool AxelExecutorLive(const AxelExecutor& e,std::uint32_t epoch,std::uint32_t frame) noexcept {
    return e.command && e.slot<3 && e.connection && e.epoch==epoch &&
        (e.engaged || frame-e.admittedFrame<kAxelExecutorLeaseFrames);
}
struct AxelContact {
    bool attachedRead{},attached{},capturedRead{},captured{};
};
// Returns false once the executor has ended; marks it engaged on first contact.
inline bool AxelExecutorContinues(AxelExecutor& e,const AxelContact& c) noexcept {
    if((c.attachedRead && c.attached) || (c.capturedRead && c.captured)){e.engaged=true;return true;}
    if(!c.attachedRead || !c.capturedRead)return true; // not observed this frame: still the RC's
    return !e.engaged;
}
inline constexpr std::uintptr_t kAxelParent=0x6A0,kAxelCapture=0x2B0;
// The executor's contact this frame, from Axel's parent and the executor actor's capture, given that actor's state
// (AxelReadActor). A gone actor is captured by no one, and Axel is held only while he is still parented. An actor
// whose identity is unread leaves its capture unknown, never a confirmed absence.
template<class Read,class Decode>
AxelContact AxelReadContact(const Read& read,const Decode& decode,std::uintptr_t axel,std::uintptr_t actor,
                            AxelActorState state) noexcept {
    AxelContact c{};
    std::uint32_t parent=0;
    c.attachedRead=read(axel+kAxelParent,parent);
    c.attached=c.attachedRead && parent && (state==AxelActorState::Gone || decode(parent)==actor);
    if(state==AxelActorState::Unknown)return c;
    if(state==AxelActorState::Gone){c.capturedRead=true;return c;}
    std::uintptr_t capture=0;
    c.capturedRead=read(actor+kAxelCapture,capture);
    c.captured=c.capturedRead && capture==axel;
    return c;
}
// sysobj_player (1/3) inside Axel's script. With no RC bound, the caller's own choice. With one bound, the
// executor's own actor, whatever its seat's state. If that actor no longer resolves, there is no owner to name:
// Unresolved, never another seat chosen in its place. 1/3 must still name a live actor (trap 1/18 at 42C576 reads
// the parent it resolves without a null check), so the adapter keeps the native result and says so. That result
// can be another seat's actor (the host's own), so the safe abort refuses every act on it (AxelActAdmitted).
enum class AxelPlayerRoute : std::uint8_t { Choice, Executor, Unresolved };
inline AxelPlayerRoute AxelPlayerRouteFor(bool executorBound,bool executorActorResolves) noexcept {
    if(!executorBound)return AxelPlayerRoute::Choice;
    return executorActorResolves?AxelPlayerRoute::Executor:AxelPlayerRoute::Unresolved;
}

// The safe abort: Axel's own acts on a seat's actor go only to the bound executor's resolving actor.
// - 1/18 attach with Axel as the child (rc_run_up, rc_clear_light): the parent must be that actor.
// - 1/29 capture with Axel as the source (the callback's AP10/AP12/AP13): the captured actor must be that actor.
// Anything else is refused before the native call: the attach is skipped and the capture returns int 0. So when the
// executor's actor is lost mid-RC, or its identity is unread, the RC's flow plays on with Axel unparented and nobody
// else captured or held, instead of acting on 1/3's native result. An unread identity refuses the act without ending
// the binding: unknown is never gone, and the next resolving read admits again.
enum class AxelAct : std::uint8_t { Native, Admitted, Refused };
inline AxelAct AxelActAdmitted(bool axelSource,bool seatTarget,bool executorBound,AxelActorState executor,
                               bool targetIsExecutor) noexcept {
    if(!axelSource || !seatTarget)return AxelAct::Native; // not Axel acting on a player: not ours
    return executorBound && executor==AxelActorState::Resolves && targetIsExecutor?AxelAct::Admitted:AxelAct::Refused;
}
// Axel II's attach pair (docs/AXEL_REACTIONS.md): Axel the child of the executor's actor, flags 16386, bone 1. The
// reverse of Thorn's qualified pair (the player the child of the boss at bone 5).
inline constexpr std::uint32_t kAxelAttachFlags=16386,kAxelAttachBone=1;
inline bool AxelAttachPair(std::uint32_t flags,std::uint32_t bone) noexcept {
    return flags==kAxelAttachFlags && bone==kAxelAttachBone;
}

// The friend executor's owner role on the host: ScriptActorOwner's role stream (begin, capture, position, motion,
// attachment, end) that replays the RC on the friend's own player. Axel's Overtaker and Clear Light hook no coroutine
// into that actor (Burst Frontier's 1/112 hook mints its own role), so the role has no thread. It is current while
// the executor stays bound to the same seat and actor and no coroutine holds the actor; the thread read must succeed.
// Cameras. Axel starts his RC cameras with camera_start_global (1/164 at 42E0D0), which tail-jumps to the
// entity-camera starter 3DB2F0(Axel, name, 0): anchored on Axel, with no target. During a friend's RC the host's
// 1/164 hook skips it and sends it to the owner as the role's camera record (op 5). Its target code is "none"
// (kAxelCameraNone) beside op 5's 0 (the role's actor) and 1 (the source). The trap has no result bit, so a skipped
// call leaves the BDX stack as it was.
inline constexpr std::uint32_t kAxelCameraNone=2;
inline bool AxelCameraForwarded(bool axelSource,bool friendRole) noexcept { return axelSource && friendRole; }
// Which RCs begin that role at completion: Overtaker and Clear Light. Burst Frontier hooks the player into its steering
// coroutine (1/112), and ScriptActorOwner's hook mints the role, adopting the capture issued just before it.
inline bool AxelRoleAtCompletion(std::uint16_t command) noexcept {
    return command==kOvertaker || command==kClearLight;
}
inline bool AxelRoleHeld(bool executorBound,std::uint8_t executorSlot,std::uintptr_t executorActor,std::uint8_t roleSlot,
                         std::uintptr_t roleActor,bool threadRead,std::uintptr_t thread) noexcept {
    return executorBound && executorSlot<3 && executorSlot==roleSlot && executorActor && executorActor==roleActor &&
        threadRead && !thread;
}

} // namespace kh2coop::inject::scriptmirror::rules
