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

// REACTION rows on the root (bone 16384), actor-local. Local -z is Axel's forward (his forward attack rows,
// group 8 at z=-228); ROT_Y=atan2(vx,vz), so world forward is (sin, cos).
struct AxelSphere { float x{},y{},z{},r{}; };
inline constexpr std::array<AxelSphere,1> kOvertakerSpheres{{{0,0,0,2800}}};
inline constexpr std::array<AxelSphere,10> kBurstFrontierSpheres{{
    {0,550,-267,400},{0,150,0,400},{0,350,-126,400},
    {0,0,-150,200},{0,0,-350,200},{0,0,-550,200},{0,0,-750,200},{0,0,-929,200},{0,0,-1178,200},{0,0,-1444,200}}};
inline constexpr float kAxelSeatTolerance=100.0f; // the player's own body, not modeled per row
inline ReactionPoint AxelSphereWorld(const AxelSphere& s,ReactionPoint origin,float yaw) noexcept {
    const float forward=-s.z,sinYaw=std::sin(yaw),cosYaw=std::cos(yaw);
    return {origin.x+forward*sinYaw+s.x*cosYaw,origin.y+s.y,origin.z+forward*cosYaw-s.x*sinYaw};
}
// The seat inside any of the family's spheres that is nearest Axel; equal distance goes to the lower seat.
// Downed, stale or missing seats never win.
template<std::size_t N>
inline int AxelFamilyWinner(const std::array<AxelSphere,N>& spheres,ReactionPoint origin,float yaw,
                            const std::array<ReactionSeat,3>& seats) noexcept {
    if(!Finite(origin) || !std::isfinite(yaw))return -1;
    int winner=-1;double nearest=0;
    for(unsigned i=0;i<seats.size();++i) {
        const auto& seat=seats[i];
        if(!seat.connection || !seat.living || !seat.fresh || !seat.capable || !Finite(seat.position))continue;
        bool inside=false;
        for(const auto& sphere:spheres) {
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

// Host: one window per command. A window opens once per activation for the seat that wins it then, keeps that
// owner while the offer stays active, and is consumed at most once. Only the next activation opens a new
// occurrence, so a consumed offer is never offered again while it is still up.
inline constexpr std::uint32_t kAxelHostWindowFrames=0x7FFFFFFFu; // bounded by the activation instead
inline constexpr std::uint32_t kAxelClientWindowFrames=180;
class AxelWindows {
public:
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
    void Reset() noexcept { windows_={}; } // the sequence never rewinds
private:
    std::array<ReactionWindow,3> windows_{};
    std::uint64_t next_{};
};

// Client: the host's offers to this seat. The newest occurrence per command; an older one is refused.
class AxelOffers {
public:
    bool Apply(std::uint64_t occurrence,std::uint16_t command,std::uint8_t slot,std::uint8_t localSlot,
               std::uint64_t connection,std::uint16_t source,std::uint32_t epoch,std::uint32_t frame) noexcept {
        const int index=AxelCommandIndex(command);
        if(index<0 || !occurrence || slot>=3 || !connection || !source || !epoch)return false;
        auto& w=windows_[static_cast<unsigned>(index)];
        if(w.occurrence && w.epoch==epoch && occurrence<w.occurrence)return false;
        if(w.occurrence==occurrence && w.epoch==epoch)return w.slot==slot && w.connection==connection;
        if(slot!=localSlot){w={};return true;} // another seat's offer ends ours: one owner per offer
        w={occurrence,connection,epoch,frame,kAxelClientWindowFrames,source,command,slot,false};
        return true;
    }
    ReactionWindow* Find(std::uint16_t command) noexcept {
        const int index=AxelCommandIndex(command);
        return index>=0 && windows_[static_cast<unsigned>(index)].occurrence?&windows_[static_cast<unsigned>(index)]:nullptr;
    }
    void Reset() noexcept { windows_={}; }
private:
    std::array<ReactionWindow,3> windows_{};
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

// The seat whose actor ran the RC. sysobj_player (1/3) inside Axel's script resolves to it while it is
// current, instead of the nearest seat. It ends once Axel's attachment and capture of that actor, having been
// seen, are both gone, or at its lease.
inline constexpr std::uint32_t kAxelExecutorLeaseFrames=1200;
struct AxelExecutor {
    std::uint16_t command{},source{};
    std::uint8_t slot{0xFF};
    std::uint64_t connection{};
    std::uint32_t epoch{},admittedFrame{};
    bool engaged{};
};
inline bool AxelExecutorLive(const AxelExecutor& e,std::uint32_t epoch,std::uint64_t connection,
                             std::uint32_t frame) noexcept {
    return e.command && e.slot<3 && e.connection && e.epoch==epoch && e.connection==connection &&
        frame-e.admittedFrame<kAxelExecutorLeaseFrames;
}
// Returns false once the executor has ended; marks it engaged on first contact.
inline bool AxelExecutorContinues(AxelExecutor& e,bool attachedToActor,bool capturedByAxel) noexcept {
    const bool engagedNow=attachedToActor || capturedByAxel;
    if(engagedNow){e.engaged=true;return true;}
    return !e.engaged;
}
inline int AxelPlayerSlot(bool executorLive,std::uint8_t executorSlot,int nearest) noexcept {
    return executorLive && executorSlot<3?static_cast<int>(executorSlot):nearest;
}

} // namespace kh2coop::inject::scriptmirror::rules
