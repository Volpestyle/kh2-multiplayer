// VUH-2914: verbatim production admission and lifecycle bodies, fake native boundaries.
// This is not a phase/hazard simulation and never accesses a game or remote machine.
#include "ScriptMirror.hpp"
#include "kh2coop/Codec.hpp"
#include "kh2coop/KH2Offsets.hpp"
#include "kh2coop/ResyncProtocol.hpp"
#include <atomic>
#include <cstdarg>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#ifdef _MSC_VER
#include <excpt.h>
#else
#define __try try
#define __except(filter) catch (...)
#define __fastcall
#endif
using namespace kh2coop;
namespace R = kh2coop::inject::scriptmirror::rules;
namespace D {
static bool Check(bool value, const char* label, const char*& why) { if (!value) why=label; return value; }
}
namespace enemysync {
#include "lifecycle_record.inc"
static bool ReceiveScriptActorBirth(std::uint16_t,std::uint32_t,std::uint16_t,std::uint32_t,const std::uint64_t*) { return false; }
}
static int failures=0;
static void check(bool ok,const char* label) {
    std::cout<<(ok?"PASS ":"FAIL ")<<label<<std::endl;
    if(!ok)++failures;
}
static void Log(const char*,...) {}
static uintptr_t g_exeBase=0,g_soraActor=0;
static std::uint32_t g_frameCounter=10;
static std::map<std::uint32_t,uintptr_t> handles;
static std::map<uintptr_t,std::vector<unsigned char>> memory;
static uintptr_t unread=0;
template<class T> static void Put(uintptr_t at,T value) {
    auto& bytes=memory[at];bytes.resize(sizeof(value));std::memcpy(bytes.data(),&value,sizeof(value));
}
template<class T> static bool ReadHitTrace(uintptr_t at,T& value) {
    const auto it=memory.find(at);
    if(at==unread || it==memory.end() || it->second.size()!=sizeof(T))return false;
    std::memcpy(&value,it->second.data(),sizeof(value));return true;
}
static uintptr_t SmDecode(std::uint32_t handle) { const auto it=handles.find(handle);return it==handles.end()?0:it->second; }
static std::uint32_t SmOwnHandle(uintptr_t actor) {
    for(const auto& [handle,at]:handles)if(at==actor)return handle;
    return 0;
}
static bool SmGameThread() noexcept { return true; }
static bool SmClientReady() { return true; }
namespace enemymirror {
static bool OwnerThread() { return true; }
static struct { uintptr_t actor=0x1000;std::uint16_t netId=3;bool running=true;std::uint32_t frame=10;
    struct { bool field=false; } pose;
} g_driven[1];
}
namespace enemysync { static bool FieldActorCurrent(uintptr_t) { return true; } }
#include "lifecycle_driven.inc"
struct Reaction { uintptr_t attack{};std::uint32_t handle{},resource{}; };
struct State {
    bool requested=true;
    R::ClientEffects clientEffects,clientAttacks;
    R::HostEffects hostEffects,hostAttacks;
    R::Hold hold;
    R::Sender sender;
    R::Retirements retirements;
    R::ReactionOccurrences occurrences;
    R::Second second;
    std::array<Reaction,4> reactions{};
    std::uint64_t faults=0;
    unsigned unmappedLogs=0,resourceMissLogs=0,storedHandleLogs=0,invalidLogs=0,birthLogs=0;
    struct Births { void Reset() {} } births;
    struct Slot { uintptr_t native=0; } slots[R::kTrapCount];
};
static State g_sm;
static bool g_smLifetimeReady=true;
static std::atomic<bool> g_smLifetimeReliable{true};
static void SmOwnerReset(bool) {}
static void SmAxelClientReset() {}
static int SmOwnerApply(const R::Call&,uintptr_t) { return 0; }
static int SmApplyReactionOffer(const R::Call&,uintptr_t) { return 0; }
static int SmApplyReactionOwner(const R::Call&,uintptr_t) { return 0; }
static uintptr_t SmOwnerAttackActor(std::uint32_t,std::uint16_t,std::uint32_t,std::uint8_t&) { return 0; }
static bool SmCallXyzRot(uintptr_t,const std::uint64_t*) { return true; }
static bool SmAttackVector(uintptr_t,const std::uint64_t*) { return true; }
static unsigned writes=0;
static bool SmWriteField(uintptr_t,const std::uint64_t*) { ++writes;return true; }
using TrapFn=void(__fastcall*)(std::uint64_t*);
#include "lifecycle_identity.inc"
#include "lifecycle_forget.inc"
static unsigned births=0,ends=0;
static bool reenter=false,forgetPending=false;
static std::uint32_t nextHandle=100;
// Aligned backing storage for the production effect-state reader.
struct alignas(uintptr_t) Effect { std::array<unsigned char,64> bytes{}; };
static std::map<std::uint32_t,Effect> effects;
static std::map<std::uint32_t,bool> live;
static void NativeFree(std::uint64_t* args) {
    const auto handle=R::BitsOf(args[0]);++ends;live[handle]=false;
    if(forgetPending) {
        // A destructor can also invalidate a not-yet-visited allocation.
        uintptr_t other=0;
        g_sm.clientAttacks.Each([&](std::uint32_t,const R::ClientEffect& pair) {
            if(pair.handle!=handle && !other)other=pair.effect;
        });
        if(other)SmForgetAllocation(other);
        forgetPending=false;
    }
    if(reenter)SmForgetAllocation(SmDecode(handle));
}
static bool SmCallTrap(std::size_t trap,std::uint32_t handle,const std::uint64_t*,std::uint64_t& result) noexcept {
    const auto& t=R::kTraps[trap];
    if(t.kind==R::Kind::EffectStart || t.kind==R::Kind::AttackStart) {
        ++births;const auto made=++nextHandle;live[made]=true;
        uintptr_t at=0;
        if(t.kind==R::Kind::EffectStart) {
            auto& e=effects[made];at=reinterpret_cast<uintptr_t>(e.bytes.data());
            const uintptr_t part=at+48;std::memcpy(e.bytes.data()+R::kEffectPart,&part,sizeof(part));
        } else {
            at=0x10000+made*0x200;
            Put(at,g_exeBase+0x5D7090);Put(at+8,std::uint32_t{0});Put(at+0x10,std::uint32_t{1});
        }
        handles[made]=at;result=R::Tagged(R::kTagAddress,made);return true;
    }
    if(t.ends) { std::uint64_t args[]{R::Tagged(R::kTagAddress,handle)};NativeFree(args); }
    else ++writes;
    return true;
}
#include "lifecycle_reset.inc"
#include "lifecycle_replay.inc"

// Only the session/bridge context is fake. Both admission bodies below are extracted verbatim.
static RoomTransition room{7,2,0x14,0,0,137,0};
namespace admission {
enum class Role { Host,Client };
static Role role=Role::Client;
static Role CurrentRole() { return role; }
static bool active=true;
static bool ActivationContext(Role,RoomTransition& out) { out=room;return active; }
struct Bridge {
    std::uint8_t slot=1;
    std::uint64_t host=10,delivery=20;
    std::uint8_t LocalSlot() { return slot; }
    std::uint64_t ConnectionId(unsigned) { return host; }
    std::uint64_t PeerDeliverySerial(unsigned) { return delivery; }
} g_bridge;
static struct { bool arrived=true;std::uint32_t epoch=7; } g_host;
static std::uint64_t g_resyncAppliedCut=0,g_scriptCallFloorHost=0,g_scriptCallFloor=0;
static int (*g_scriptCallApply)(const enemysync::ScriptCallRecord&)=SmApplyRecord;
using enemysync::ScriptCallRecord;
static struct { unsigned seen=0,admissionRefused=0,records=0,applied=0,refused=0; } g_scriptCallStats;
static unsigned g_scriptCallLogs=0;
static void (*g_log)(const char*,...)=Log;
#include "lifecycle_admission.inc"
}
namespace network {
static std::array<std::uint64_t,3> avatarConnections_{10,11,0};
static std::optional<RoomTransition> hostRoom_=room;
static std::uint64_t receivedScriptCallHost_=0,receivedScriptCallSeq_=0;
static void log(const std::string&) {}
static unsigned forwarded=0;
static void Deliver(const ScriptCall& calls,const WorldScope& scope) {
    const auto encoded=encode(calls);const std::uint8_t* payload=nullptr;std::size_t payloadSize=0;
    const auto type=decodePacketHeader(encoded.data(),encoded.size(),payload,payloadSize);
    ByteReader reader(payload,payloadSize);
    const auto* admittedScope=&scope;
#include "lifecycle_network.inc"
    ++forwarded;
    admission::AdmitScriptCall(scope,payload,payloadSize);
}
}
static WorldScope scope() {
    WorldScope s;s.sessionId="fixture-session";s.sourceConnectionId=10;s.sourceDeliverySerial=20;s.hostSourceSerial=30;
    return s;
}
static ScriptCallEntry entry(bool attack,std::uint32_t id=1,bool end=false,std::uint16_t netId=3) {
    ScriptCallEntry e;e.netId=netId;e.objectId=81;e.effect=id;
    e.table=attack?2:(end?0:1);e.index=attack?(end?13:10):(end?51:21);
    if(!end)e.args={R::Tagged(R::kTagInt,11),R::Tagged(R::kTagInt,0),R::Tagged(R::kTagInt,0),0};
    return e;
}
static std::uint64_t seq=0;
static void send(std::initializer_list<ScriptCallEntry> entries) {
    ScriptCall c;c.location=room;c.hostConnectionId=10;c.seq=++seq;c.entries=entries;network::Deliver(c,scope());
}
static void reset() {
    g_sm=State{};g_smLifetimeReliable=true;handles.clear();memory.clear();effects.clear();live.clear();
    handles[1]=0x1000;births=ends=writes=0;reenter=forgetPending=false;enemymirror::g_driven[0].running=true;unread=0;
    Put(uintptr_t{0x1000}+offsets::actor::OBJENTRY_PTR,uintptr_t{0x2000});
    Put(uintptr_t{0x2000}+offsets::objentry::OBJECT_ID,std::uint32_t{81});
    g_exeBase=reinterpret_cast<uintptr_t>(&NativeFree)-0x4323B0;
}
static void duplicate() {
    reset();send({entry(false)});const auto first=*g_sm.clientEffects.Find(1);
    send({entry(false)});
    const auto* now=g_sm.clientEffects.Find(1);
    check(now && now->handle==first.handle && births==1,"admitted duplicate effect start preserves pairing with no second birth");
    send({entry(false,1,true)});
    check(!g_sm.clientEffects.Find(1) && ends==1 && !live[first.handle],"one effect end retires original birth without orphan");
    reset();enemymirror::g_driven[0].running=false;send({entry(false),entry(false)});enemymirror::g_driven[0].running=true;SmDrainHold();
    check(births==1 && g_sm.clientEffects.Size()==1 && !g_sm.hold.Size(),"held duplicate uses actual drain/replay and creates once");
}
static void controls() {
    reset();send({entry(false),entry(true)});
    check(births==2 && g_sm.clientEffects.Size()==1 && g_sm.clientAttacks.Size()==1,"effect/attack equal IDs have separate namespaces");
    auto field=entry(false);field.table=0;field.index=38;
    field.args.fill(R::Tagged(R::kTagFloat,0));send({field});
    send({entry(false,1,true),entry(true,1,true)});
    check(writes==1 && ends==2 && !g_sm.clientEffects.Size() && !g_sm.clientAttacks.Size(),"valid effect update/end and attack start/end survive");
    reset();enemymirror::g_driven[0].running=false;send({entry(true)});send({entry(true,1,true)});enemymirror::g_driven[0].running=true;SmDrainHold();
    check(!births && !g_sm.hold.Size(),"attack end cancels its held start before source drive");
    reset();enemymirror::g_driven[0].running=false;send({entry(false)});send({entry(false,1,true)});enemymirror::g_driven[0].running=true;SmDrainHold();
    check(births==1 && ends==1 && !g_sm.clientEffects.Size(),"held effect start/end drains in order without orphan");
    reset();send({entry(true)});const auto old=*g_sm.clientAttacks.Find(1);
    SmForgetAllocation(old.effect); // real native destruction notification before allocation reuse
    nextHandle=old.handle-1;send({entry(true,2)});send({entry(true,1,true)});
    check(g_sm.clientAttacks.Find(2) && !ends,"old attack end cannot retire replacement at same handle/address");
    unread=old.effect+0x10;send({entry(true,2,true)});
    check(!ends && !g_sm.clientAttacks.Size(),"unread native owner identity refuses free and drops stale binding");
    reset();send({entry(true)});enemymirror::g_driven[0].running=false;send({entry(true,1,true)});
    check(ends==1 && !g_sm.clientAttacks.Size(),"owner/source loss still permits final attack free");
    reset();unread=0x2000+offsets::objentry::OBJECT_ID;send({entry(false)});SmDrainHold();
    check(!births && g_sm.hold.Size()==1,"unknown source identity cannot create native resources");
    SmResetRoom(false);unread=0;SmDrainHold();
    check(!births && !g_sm.hold.Size(),"room teardown discards held starts without old native writes");
    reset();send({entry(false)});SmResetRoom(false);send({entry(false)});
    check(births==2,"new room registry allows a fresh resource namespace");
}
static void admissionControls() {
    reset();send({entry(false)});const auto before=g_sm.second.received;
    ScriptCall stale;stale.location=room;stale.hostConnectionId=10;stale.seq=seq;stale.entries={entry(false)};
    network::Deliver(stale,scope());
    check(g_sm.second.received==before,"NetworkClient rejects repeated/old packet sequence before adapter");
    auto bytes=encode(stale);const std::uint8_t* p;std::size_t n;decodePacketHeader(bytes.data(),bytes.size(),p,n);
    admission::AdmitScriptCall(scope(),p,n);
    check(g_sm.second.received==before,"EnemySync independently rejects old packet sequence before adapter");
    stale.seq=++seq;++stale.location.epoch;network::Deliver(stale,scope());
    check(g_sm.second.received==before,"old epoch cannot enter adapter");
    stale.location=room;stale.seq=++seq;auto foreign=scope();foreign.sourceConnectionId=99;network::Deliver(stale,foreign);
    check(g_sm.second.received==before,"foreign host cannot enter adapter");
    foreign=scope();foreign.sourceDeliverySerial=99;stale.seq=++seq;network::Deliver(stale,foreign);
    check(g_sm.second.received==before,"old delivery/session incarnation cannot enter adapter");
    reset();send({entry(false)});
    stale.location=room;stale.hostConnectionId=10;stale.seq=seq;stale.entries={entry(false)};
    send({entry(false,1,true)});send({entry(false,2)});const auto afterEnd=g_sm.second.received;
    network::Deliver(stale,scope());
    check(g_sm.second.received==afterEnd && births==2 && !g_sm.clientEffects.Find(1) && g_sm.clientEffects.Find(2),
          "start/end/late old packet cannot revive retired resource or replace newer occurrence");
    reset();stale.seq=++seq;stale.entries={entry(true)};
    send({entry(true,1,true)});const auto beforeLate=g_sm.second.received;network::Deliver(stale,scope());
    check(g_sm.second.received==beforeLate && !births && !g_sm.hold.Size(),
          "end received before older start packet cannot leave a late held start");
}
static void retirement() {
    reset();send({entry(true,1),entry(true,2),entry(true,3)});
    check(births==3 && g_sm.clientAttacks.Size()==3,"three admitted attack births before reentrant retirement");
    reenter=true;SmResetRoom();
    check(ends==3 && !g_sm.clientAttacks.Size(),"native free reenters actual allocation-forget during room retirement");
    reset();send({entry(true,1),entry(true,2),entry(true,3)});
    reenter=forgetPending=true;SmResetRoom();
    check(ends==2 && !g_sm.clientAttacks.Size(),"retirement revalidates and skips another allocation forgotten by native callback");
}
int main(int argc,char** argv) {
    const std::string mode=argc>1?argv[1]:"all";
    if(mode=="all" || mode=="duplicate")duplicate();
    if(mode=="all" || mode=="controls") { controls();admissionControls(); }
    if(mode=="all" || mode=="retirement")retirement();
    return failures?1:0;
}
