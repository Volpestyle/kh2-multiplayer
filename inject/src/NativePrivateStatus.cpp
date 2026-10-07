#include "kh2coop/PlayerKits.hpp"
#include "NativePrivateStatus.hpp"
#include "NativePrivateStatusPins.hpp"
#include "NativeSpawnController.hpp"
#include "PartyNative.hpp"
#include "PlayerKit.hpp"
#include "Warp.hpp"
#include <Windows.h>
#include <MinHook.h>
#include <intrin.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <type_traits>

namespace kh2coop::inject::privatestatus {
// VUH-1515 (KH2COOP_ENEMY_TARGET_REMOTE): widens Profile, see EnemyTargetProfile below.
static std::atomic<bool> g_enemyTargetScope{};
void EnableEnemyTargetScope(){g_enemyTargetScope=true;}
namespace {
constexpr unsigned DepthCap=8, PoolCount=80, HookCount=8;
constexpr uintptr_t PoolRva=0x2A17290, FreeRva=0x2A23810, CountRva=0x2A23950;
constexpr uintptr_t Stride=0x278;
// 3A7A40 stores every constructed player-class actor here (3A7AF4) before its status allocation.
constexpr uintptr_t PlayerRva=0x2A105D0;
using FactoryFn=uintptr_t(__fastcall*)(std::uint32_t,const float*,float);
using ConstructorFn=uintptr_t(__fastcall*)(uintptr_t,uintptr_t,int,uintptr_t,const float*,float);
using AllocateFn=uintptr_t(__fastcall*)(int,int);
using InitializeFn=uintptr_t(__fastcall*)(uintptr_t);
using CommitFn=void(__fastcall*)(uintptr_t);
FactoryFn g_factory{}; ConstructorFn g_constructor{};
AllocateFn g_allocate{},g_fresh{}; InitializeFn g_initialize{}; CommitFn g_commit{};
uintptr_t g_base{};
std::atomic<bool> g_requested{},g_retained{},g_attempted{},g_ready{};
std::atomic<std::uint64_t> g_faults{},g_fiberThreads{},g_scopeAbandoned{},g_serial{},g_drops{},g_vetoes{},g_foreign{},g_stale{};
std::atomic<unsigned> g_installed{};
// Accessed by checked x64 gateways with byte loads/stores; naturally atomic.
// Interlocked APIs are used in C++ for the same storage, never std::atomic aliasing.
alignas(64) volatile char g_owned[PoolCount]{};
alignas(8) volatile LONG64 g_retired{},g_excluded{};
struct Stamp { std::array<std::uint8_t,10> now{}; std::uint32_t transition{},load{}; };
struct Selection {
    Stamp stamp{};
    uintptr_t actor{},local{},localStatus{},descriptor{},status{};
    std::uint64_t serial{};
    int key{1}; // status key of the selected descriptor (1 Sora; other qualified kits' keys from kh2coop/PlayerKits.hpp)
    int form{}; // the constructor's form argument (= the descriptor's +0x57 byte), passed on to the allocator
    bool selected{},constructor{},fresh{},used{};
    // VUH-1519 party profile: an ordinary factory call in a party stamp (candidate); the
    // constructor promotes a Sora build to selected (a clone) or partyLocal (the last one).
    bool candidate{},partyLocal{};
    std::uint32_t factoryId{};
};
// Never stores caller stack pointers, only numeric stack anchors and owned POD.
#include "NativePrivateStatusScope.inc"
// The raw566 construction is the clone: the later raw567 Sora construction overwrites the
// canonical player pointer and becomes the active-list head, which EntityHook keeps local.
// Binding is published only after that later construction proves it. No pointer is rebound.
struct Pending { Stamp stamp{}; uintptr_t actor{},status{}; std::uint64_t serial{}; bool armed{}; int key{1}; };
Stamp g_spentStamp{};bool g_spent{};
Pending g_pending{}; // Only verified native diagnostic owner accesses this POD.
// VUH-1519 party profile (two clones). Owner thread only, like g_pending. In a party stamp
// every Sora built before the last is a clone: 3A7AF4 stores each player-class actor as
// [2A105D0], so the last Sora built keeps the canonical pointer (VUH-1489 actor fix; on main
// the player seat is built first). Sora (key 1) always; any other qualified kit's key (PlayerKits.hpp) only
// with party kits, where the canonical local may itself be that kit and each clone's key must match the kit
// written for it.
constexpr unsigned PartyClones=2; // the most (three players); a stamp expects AppliedClones() (two players: 1)
struct PartyPending { Stamp stamp{}; std::array<uintptr_t,PartyClones> actor{},status{}; std::array<std::uint64_t,PartyClones> serial{};
    std::array<int,PartyClones> key{}; unsigned claimed{},count{},builds{},canonicalFrames{},expected{}; bool open{},bound{}; };
PartyPending g_party{};
// First binding refusal reason, logging only: 1 null/same actor, 2 not Sora, 3 status owned,
// 4 status shared with clone, 5 clone status changed, 6 clone not owned, 7 player pointer,
// 8 stamp changed, 9 second raw566 while pending, 10 new stamp while pending.
// Party profile: 11 stamp ended with unbound clones, 12 last Sora before both clones,
// 13 extra Sora after binding, 14 clone record changed or not owned at binding,
// 15 the canonical player holds a claimed private record (the third Sora never came),
// 16 party kits: the clones' descriptor keys differ from the clone kits written (members 0/1), or the
//    canonical local's key differs from this machine's expected own kit.
std::atomic<unsigned> g_bindFault{};
struct Event { unsigned kind{},thread{}; std::uint64_t serial{}; uintptr_t actor{},local{},status{},localStatus{}; int index{},hp{},localHp{};
    std::uint32_t factoryId{}; unsigned order{},role{},frame{}; int key{}; }; // kind 4 (party build order) only
std::atomic<unsigned> g_ownerFrames{}; // owner Drain calls (one per game frame); N-c build-line frame
std::array<Event,128> g_queue{};
SRWLOCK g_queueLock=SRWLOCK_INIT;
unsigned g_read{},g_count{};
void Fault() {g_faults.fetch_add(1);g_ready=false;}
void BindFault(unsigned reason){unsigned none=0;g_bindFault.compare_exchange_strong(none,reason);Fault();}
bool Read(uintptr_t p,void* out,std::size_t n) {
    if(p<0x10000 || p>=0x800000000000ULL || n>0x800000000000ULL-p)return false;
    __try {std::memcpy(out,reinterpret_cast<void*>(p),n);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
template<class T> bool Read(uintptr_t p,T& out){return Read(p,&out,sizeof(out));}
int Index(uintptr_t p) {
    const auto start=g_base+PoolRva;
    return p>=start && p<start+PoolCount*Stride && (p-start)%Stride==0 ? static_cast<int>((p-start)/Stride) : -1;
}
bool Owned(uintptr_t p) {
    const int i=Index(p);return i>=0 && _InterlockedCompareExchange8(&g_owned[i],0,0)!=0;
}
bool StampNow(Stamp& s) {
    if(!spawncontroller::IsDiagnosticGameThread())return false;
    s.transition=warp::TransitionSerial();s.load=warp::LoadSerial();
    return Read(g_base+0x717008,s.now);
}
bool Same(const Stamp& a,const Stamp& b){return a.now==b.now && a.transition==b.transition && a.load==b.load;}
// VUH-1515 enemy targeting (KH2COOP_ENEMY_TARGET_REMOTE), review S2: only verified rooms, each with
// its exact leaf row (SAVE+0x3534+4*world, getter 0x3E2E20): Sora in friend slot 1, Goofy in slot 2,
// and the room's own ally byte. BC courtyard 05/06: 00/00/02/12, after the fixture's leaf write of the
// native row 00/01/02/12 (run 20261007-033207).
struct EnemyTargetRoom { std::uint8_t world,room; std::array<std::uint8_t,4> row; };
constexpr EnemyTargetRoom kEnemyTargetRooms[]={{5,6,{0,0,2,0x12}}};
bool EnemyTargetProfile(Stamp& s) {
    std::array<std::uint8_t,4> row{},magic{};
    if(!StampNow(s) || !Read(g_base+0x9A98B0,magic) || magic!=std::array<std::uint8_t,4>{'K','H','2','J'})return false;
    for(const auto& r:kEnemyTargetRooms)
        if(s.now[0]==r.world && s.now[1]==r.room)return Read(g_base+0x9A98B0+0x3534+4*r.world,row) && row==r.row;
    return false;
}
bool Profile(Stamp& s) {
    if(g_enemyTargetScope.load() && EnemyTargetProfile(s))return true; // VUH-1515
    std::array<std::uint8_t,4> row{},magic{};
    return StampNow(s) && s.now[0]==4 && s.now[1]==0x1A &&
        Read(g_base+0x9A98B0,magic) && magic==std::array<std::uint8_t,4>{'K','H','2','J'} &&
        Read(g_base+0x9A98B0+0x3534+4*4,row) && (row==std::array<std::uint8_t,4>{0,0,2,0x12} ||
        // VUH-1513 remote kit member: Friend1 = member 3 (row 00/03/02/12), only in that mode.
        (playerkit::RemoteKitMemberActive() && row==std::array<std::uint8_t,4>{0,3,2,0x12}));
}
// Party profile: the resolver post-hook (PartyNative) replaced members 1/2 with Sora for this
// load, and the native save row is the untouched DEFAULT row (never written by us).
bool PartyProfile(Stamp& s) {
    std::array<std::uint8_t,4> row{},magic{};
    const unsigned clones=partynative::AppliedClones();
    return (clones==1 || clones==PartyClones) && StampNow(s) && s.now[0]==4 && (s.now[1]==0x1A || s.now[1]==0x0A) && // VUH-1786: 04/0A
        Read(g_base+0x9A98B0,magic) && magic==std::array<std::uint8_t,4>{'K','H','2','J'} &&
        Read(g_base+0x9A98B0+0x3534+4*4,row) && row==partynative::DEFAULT_ROW;
}
// Vanilla player-class descriptor with its status key, from the reviewed kit table (PlayerKits.hpp):
// Sora (84, key 1) always; any other qualified kit (Roxas 90/key 14) only while the remote kit member
// or party kits are active. key==0 means "not a selectable clone descriptor".
bool RoxasAllowed() {return playerkit::RemoteKitMemberActive() || partynative::KitsActive();}
int KeyForKit(std::uint16_t kit) {return kh2coop::kitStatusKey(kit);} // the reviewed kit table
// Party kits: the clones' key multiset equals the keys of the kits the observer wrote.
bool PartyKeysMatch(const std::array<int,2>& keys,unsigned count) {
    std::uint16_t m1=0,m2=0;if(!partynative::AppliedMembers(m1,m2))return false;
    const int a=KeyForKit(m1);
    if(count==1)return m2==0 && a && keys[0]==a; // two players: the one clone's key equals its kit's
    const int b=KeyForKit(m2);
    return count==2 && a && b && ((keys[0]==a && keys[1]==b) || (keys[0]==b && keys[1]==a));
}
int CloneDescriptorKey(uintptr_t p) {
    std::uint32_t id=0;std::uint16_t key=0;std::uint8_t type=255;std::int8_t form=-1;std::array<char,8> name{};
    if(!Read(p,id) || !Read(p+4,type) || !Read(p+0x4C,key) || !Read(p+0x57,form) || type!=0 || !Read(p+8,name))return 0; // form: per kit row (the table)
    return kh2coop::kitDescriptorKey(id,type,key,form,name.data(),name.size(),RoxasAllowed()); // the reviewed kit table
}
bool SoraDescriptor(uintptr_t p) {return CloneDescriptorKey(p)==1;}
// The constructor's form argument is the descriptor's +0x57 byte (3DF930 case 0 and its raw566 path
// pass (int)(char)desc[0x57]: 0 for Sora/Roxas, 11 "Default" for Mickey), and 3A7A40 hands it to the
// allocator (3A7B1B). CloneDescriptorKey already ties that byte to the kit row's form (the table), so a
// selectable build's form argument must equal it; a mismatch stays ordinary.
bool ConstructorForm(uintptr_t descriptor,int form) {std::int8_t f=-1;return Read(descriptor+0x57,f) && form==f;}
// expectKey: 1 for the local Sora; the selected clone's key for the clone.
bool PlayerActor(uintptr_t actor,uintptr_t& status,int expectKey) {
    uintptr_t descriptor=0;int key=0,refs=0,hp=0,maxHp=0;
    return Read(actor+0x918,descriptor) && CloneDescriptorKey(descriptor)==expectKey &&
        Read(actor+0x5C0,status) && Index(status)>=0 && Read(status+0x260,key) && key==expectKey &&
        Read(status+0x264,refs) && refs>0 && Read(status,hp) && Read(status+4,maxHp) && hp>0 && hp<=maxHp;
}
bool SoraActor(uintptr_t actor,uintptr_t& status) {return PlayerActor(actor,status,1);}
void Publish(unsigned kind,const Selection& s) {
    Event e{};e.kind=kind;e.thread=GetCurrentThreadId();e.serial=s.serial;e.actor=s.actor;e.local=s.local;
    e.status=s.status;e.localStatus=s.localStatus;e.index=Index(s.status);
    // This is only called before release. Retirement gateway never rereads storage.
    if((kind!=1 && s.status && !Read(s.status,e.hp)) || (s.localStatus && !Read(s.localStatus,e.localHp))){Fault();return;}
    if(!TryAcquireSRWLockExclusive(&g_queueLock)){g_drops.fetch_add(1);return;}
    if(g_count==g_queue.size())g_drops.fetch_add(1);
    else {g_queue[(g_read+g_count)%g_queue.size()]=e;++g_count;}
    ReleaseSRWLockExclusive(&g_queueLock);
}
// Owner thread. Build order of every Sora in a party stamp (S1 evidence: which seat is last).
// key: the promoted build's status key (the clone's private record; the local's own record), from its descriptor.
void PublishBuild(unsigned order,std::uint32_t id,unsigned role,uintptr_t actor,std::uint64_t serial,int key) {
    Event e{};e.kind=4;e.thread=GetCurrentThreadId();e.order=order;e.factoryId=id;e.role=role;e.actor=actor;e.serial=serial;e.frame=g_ownerFrames.load();e.key=key;
    if(!TryAcquireSRWLockExclusive(&g_queueLock)){g_drops.fetch_add(1);return;}
    if(g_count==g_queue.size())g_drops.fetch_add(1);
    else {g_queue[(g_read+g_count)%g_queue.size()]=e;++g_count;}
    ReleaseSRWLockExclusive(&g_queueLock);
}
// True when the canonical player is one of the claimed (private) party actors.
bool CanonicalIsClaimed() {
    uintptr_t player=0;if(!Read(g_base+PlayerRva,player) || !player)return false;
    for(unsigned k=0;k<g_party.count;++k)if(g_party.actor[k]==player)return true;
    return false;
}
Selection* Top(const Scope& token) {
    auto* c=ContextFor();
    if(!c || c!=token.context || c->depth!=token.index+1 || c->frames[token.index].serial!=token.serial){Fault();return nullptr;}
    return &c->frames[token.index].event;
}
Selection* Current(uintptr_t anchor) {
    auto* c=ContextFor();return Parent(c,anchor);
}
bool HasFreeRecord() {
    std::int32_t count=0,index=0;std::array<std::int32_t,PoolCount> indices{};
    if(!Read(g_base+CountRva,count) || count<8 || count>static_cast<int>(PoolCount) ||
       !Read(g_base+FreeRva,indices.data(),static_cast<std::size_t>(count)*4))return false;
    std::array<bool,PoolCount> seen{};
    for(int i=0;i<count;++i){index=indices[static_cast<unsigned>(i)];if(index<0 || index>=static_cast<int>(PoolCount) || seen[static_cast<unsigned>(index)] ||
        _InterlockedCompareExchange8(&g_owned[index],0,0))return false;seen[static_cast<unsigned>(index)]=true;}
    std::int32_t again=0;return Read(g_base+CountRva,again) && count==again;
}

uintptr_t __fastcall Factory(std::uint32_t id,const float* point,float yaw) {
    const DWORD error=GetLastError();const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    const auto anchor=reinterpret_cast<uintptr_t>(_AddressOfReturnAddress());
    Selection s{};Stamp stamp{};
    const bool owner=spawncontroller::IsDiagnosticGameThread();
    const bool ordinary=owner && caller==g_base+0x3FE5F0;
    // An unbound clone may not outlive its stamp or see a second raw566 construction.
    if(ordinary && g_pending.armed && (id==566 || !StampNow(stamp) || !Same(stamp,g_pending.stamp))){g_pending={};BindFault(id==566?9:10);}
    // Party clones may not outlive their stamp unbound. A stamp with no Sora yet just resets.
    if(ordinary && g_party.open && !g_party.bound && (!StampNow(stamp) || !Same(stamp,g_party.stamp))){
        const bool unbound=g_party.claimed!=0;const bool canonical=unbound && CanonicalIsClaimed();
        g_party={};if(unbound)BindFault(canonical?15:11);
    }
    if(ordinary && g_ready.load() && PartyProfile(stamp)) {
        if(!g_party.open || !Same(g_party.stamp,stamp)){g_party={};g_party.stamp=stamp;g_party.open=true;g_party.expected=partynative::AppliedClones();}
        s.candidate=true;s.stamp=stamp;s.factoryId=id; // promoted (or not) by the constructor, by descriptor
    } else if(ordinary && id==566 && g_ready.load() && Profile(stamp)) {
        if((!g_spent || !Same(g_spentStamp,stamp)) && HasFreeRecord()) {
            g_spent=true;g_spentStamp=stamp;
            s.selected=true;s.stamp=stamp;s.serial=g_serial.fetch_add(1)+1;
        } else Fault();
    }
    const auto token=PushScope(&s,anchor);
    if(!token.context){Fault();SetLastError(error);return g_factory(id,point,yaw);}
    uintptr_t result=0;bool returned=false;
    __try {SetLastError(error);result=g_factory(id,point,yaw);returned=true;}
    __finally {
        const DWORD nativeError=GetLastError();
        auto* top=Top(token);
        if(!returned){Fault();}
        else if(top && top->selected) {
            uintptr_t actual=0;Stamp after{};
            if(!top->used || !result || result!=top->actor || !PlayerActor(result,actual,top->key) || actual!=top->status || !Owned(actual) ||
               !StampNow(after) || !Same(after,top->stamp))Fault();
            else if(top->candidate) {
                if(g_party.count>=g_party.expected || !Same(g_party.stamp,top->stamp) || (top->key!=1 && !(top->key!=0 && partynative::KitsActive())))Fault(); // any qualified kit key (the table)
                else {const unsigned k=g_party.count++;g_party.actor[k]=top->actor;g_party.status[k]=top->status;g_party.serial[k]=top->serial;g_party.key[k]=top->key;
                    PublishBuild(++g_party.builds,top->factoryId,1,top->actor,top->serial,top->key);}
            }
            else g_pending={top->stamp,top->actor,top->status,top->serial,true,top->key};
        } else if(top && top->partyLocal) {
            // The last Sora of a party stamp: both clones must already hold owned private
            // records, and this construction must take the canonical pointer with an
            // ordinary, distinct record. Otherwise refuse; nothing is rebound.
            const PartyPending p=g_party;g_party.bound=true;
            PublishBuild(++g_party.builds,top->factoryId,2,result,0,top->key);
            uintptr_t status=0,player=0;Stamp after{};unsigned reason=0;
            if(!p.expected || p.count!=p.expected)reason=12;
            else if(!result || result==p.actor[0] || (p.expected==2 && result==p.actor[1]))reason=1;
            else if(!PlayerActor(result,status,top->key))reason=2; // the canonical local, with its own kit's key
            else if(Owned(status))reason=3;
            else if(status==p.status[0] || (p.expected==2 && status==p.status[1]))reason=4;
            else {
                for(unsigned k=0;k<p.expected && !reason;++k){uintptr_t clone=0;
                    if(!PlayerActor(p.actor[k],clone,p.key[k]) || clone!=p.status[k] || !Owned(clone))reason=14;}
                if(!reason && (!Read(g_base+PlayerRva,player) || player!=result))reason=7;
                if(!reason && (!StampNow(after) || !Same(after,p.stamp)))reason=8;
                if(!reason && !PartyKeysMatch(p.key,p.expected))reason=16;
                if(!reason && top->key!=KeyForKit(partynative::AppliedLocal()))reason=16; // rev2 S3: the canonical local is this machine's own kit
            }
            if(reason)BindFault(reason);
            else for(unsigned k=0;k<p.expected;++k){Selection b{};b.serial=p.serial[k];b.actor=p.actor[k];b.status=p.status[k];b.local=result;b.localStatus=status;Publish(3,b);}
        } else if(ordinary && id==567 && g_pending.armed) {
            // Exactly one later Sora construction must take the canonical player pointer away
            // from the clone and receive an ordinary, distinct status. Otherwise refuse.
            const Pending p=g_pending;g_pending={};
            uintptr_t status=0,clone=0,player=0;Stamp after{};
            const unsigned reason=!result || result==p.actor?1:!SoraActor(result,status)?2:Owned(status)?3:status==p.status?4:
                !PlayerActor(p.actor,clone,p.key) || clone!=p.status?5:!Owned(clone)?6:!Read(g_base+PlayerRva,player) || player!=result?7:
                !StampNow(after) || !Same(after,p.stamp)?8:0;
            if(reason)BindFault(reason);
            else {Selection b{};b.serial=p.serial;b.actor=p.actor;b.status=p.status;b.local=result;b.localStatus=status;Publish(3,b);}
        }
        if(!RestoreScope(token))Fault();SetLastError(nativeError);
    }
    return result;
}

uintptr_t __fastcall Constructor(uintptr_t actor,uintptr_t descriptor,int form,uintptr_t arg4,const float* point,float yaw) {
    const DWORD error=GetLastError();const auto anchor=reinterpret_cast<uintptr_t>(_AddressOfReturnAddress());
    auto* parent=Current(anchor);Selection s{};
    if(parent && parent->candidate && !parent->selected && !parent->partyLocal && !parent->used && g_ready.load() &&
       actor && ConstructorForm(descriptor,form) && (CloneDescriptorKey(descriptor)==1 || (CloneDescriptorKey(descriptor)!=0 && partynative::KitsActive()))) { // qualified kits only (CloneDescriptorKey)
        const int k=CloneDescriptorKey(descriptor); // party kits: each build keeps its own descriptor's key
        Stamp now{};
        if(!StampNow(now) || !Same(now,parent->stamp) || !g_party.open || !Same(now,g_party.stamp))Fault();
        else if(g_party.bound)BindFault(13); // a fourth Sora: stays ordinary (shared), profile fails
        else if(g_party.claimed<g_party.expected) {
            if(HasFreeRecord()){++g_party.claimed;parent->selected=true;parent->key=k;parent->serial=g_serial.fetch_add(1)+1;}
            else Fault();
        } else {parent->partyLocal=true;parent->key=k;}
    }
    if(parent && parent->selected && !parent->constructor && !parent->used && g_ready.load()) {
        Stamp now{};
        const int key=CloneDescriptorKey(descriptor);
        if(actor && key!=0 && ConstructorForm(descriptor,form) && StampNow(now) && Same(now,parent->stamp))
            {s=*parent;s.actor=actor;s.descriptor=descriptor;s.key=key;s.form=form;s.constructor=true;}
        else Fault();
    }
    const auto token=PushScope(&s,anchor);
    if(!token.context){Fault();SetLastError(error);return g_constructor(actor,descriptor,form,arg4,point,yaw);}
    uintptr_t result=0;bool returned=false;Selection done{};
    __try {SetLastError(error);result=g_constructor(actor,descriptor,form,arg4,point,yaw);returned=true;}
    __finally {
        const DWORD nativeError=GetLastError();auto* top=Top(token);if(top)done=*top;
        if(!returned || (s.selected && (!top || !top->used || result!=actor)))Fault();
        if(!RestoreScope(token))Fault();
        // Revalidate the outer FLS frame; never restore a saved stack pointer.
        auto* outer=Current(anchor);
        if(done.selected && outer && outer->selected && outer->serial==done.serial) {
            outer->used=done.used;outer->actor=done.actor;outer->status=done.status;outer->key=done.key;
        }
        SetLastError(nativeError);
    }
    return result;
}

uintptr_t __fastcall Allocate(int key,int form) {
    const DWORD error=GetLastError();const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    const auto anchor=reinterpret_cast<uintptr_t>(_AddressOfReturnAddress());auto* s=Current(anchor);
    if(!s || !s->selected || !s->constructor || s->used || caller!=g_base+0x3A7B1B || !g_ready.load()) {
        SetLastError(error);return g_allocate(key,form);
    }
    Stamp now{};uintptr_t empty=1;
    if(key!=s->key || form!=s->form || !StampNow(now) || !Same(now,s->stamp) || !Read(s->actor+0x5C0,empty) || empty || !HasFreeRecord()) {
        Fault();SetLastError(error);return g_allocate(key,form);
    }
    // Own POD frame stays in FLS pool across actual SwitchToFiber. Nested constructors hide it.
    const auto serial=s->serial;s->fresh=true;s->used=true;bool returned=false;uintptr_t result=0;
    __try {SetLastError(error);result=g_fresh(key,form);returned=true;}
    __finally {
        const DWORD nativeError=GetLastError();auto* current=Current(anchor);
        if(!returned || !current || current->serial!=serial || !current->fresh){Fault();}
        else {
            current->fresh=false;std::int32_t refs=0,actualKey=0;
            if(result!=current->status || !Owned(result) || result==current->localStatus ||
               !Read(result+0x264,refs) || refs!=1 || !Read(result+0x260,actualKey) || actualKey!=current->key)Fault();
            else Publish(2,*current);
        }
        SetLastError(nativeError);
    }
    return result; // Original constructor performs its own actor+5C0 store.
}
uintptr_t __fastcall InitializeRecord(uintptr_t status) {
    const DWORD error=GetLastError();const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    auto* s=Current(reinterpret_cast<uintptr_t>(_AddressOfReturnAddress()));
    const bool selected=s && s->selected && s->constructor && s->fresh && !s->status && caller==g_base+0x3C0432;
    const int index=Index(status);
    // Only verified native pops establish that an old marker is stale. Other callers
    // can reinitialize an in-use record: keep its veto/exclusion and fail the profile.
    const bool knownPop=caller==g_base+0x3C02BB || caller==g_base+0x3C0340 ||
        caller==g_base+0x3C0432 || caller==g_base+0x3C05AE || caller==g_base+0x3C0780;
    if(!selected && index>=0 && _InterlockedCompareExchange8(&g_owned[index],0,0)!=0) {
        if(knownPop)_InterlockedExchange8(&g_owned[index],0);
        g_stale.fetch_add(1);Fault();
    }
    if(selected) {
        const int i=Index(status);
        // Native allocator has already popped this exact slot. Publish exclusion and SAVE veto
        // before native initialization/refresh; no guessed top-of-free-list ownership.
        if(i<0 || status==s->localStatus || _InterlockedCompareExchange8(&g_owned[i],1,0)!=0)Fault();
        else {s->status=status;Publish(1,*s);}
    }
    SetLastError(error);return g_initialize(status);
}
void __fastcall Commit(uintptr_t status) {
    const DWORD error=GetLastError();
    if(Owned(status)) {
        g_vetoes.fetch_add(1);if(!spawncontroller::IsDiagnosticGameThread())g_foreign.fetch_add(1);
        SetLastError(error);return;
    }
    SetLastError(error);g_commit(status);
}

// Install-only code follows. All allocated hook/unwind/FLS resources are permanent.
struct Hook {void* target{};std::uint8_t* trampoline{};std::uint8_t* metadata{};};
std::array<Hook,HookCount> g_hooks{};
constexpr std::array<std::uint32_t,HookCount> Rvas{0x3DF930,0x3A7A40,0x3C0620,0x3C0010,0x3C2120,0x3C05EB,0x3C0830,0x3C07B6};
using Alloc2Fn=PVOID(WINAPI*)(HANDLE,PVOID,SIZE_T,ULONG,ULONG,MEM_EXTENDED_PARAMETER*,ULONG);
std::uint8_t* AllocateMetadata(uintptr_t t) {
    const auto proc=GetProcAddress(GetModuleHandleW(L"KernelBase.dll"),"VirtualAlloc2");Alloc2Fn alloc=nullptr;
    std::memcpy(&alloc,&proc,sizeof(alloc));if(!alloc || t>UINTPTR_MAX-UINT32_MAX)return nullptr;
    SYSTEM_INFO info{};GetSystemInfo(&info);const auto gran=static_cast<uintptr_t>(info.dwAllocationGranularity);
    auto high=t+UINT32_MAX;const auto maximum=reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress);
    if(high>maximum)high=maximum;high=((high+1)/gran)*gran-1;const auto low=((t+gran-1)/gran)*gran;
    if(high<low || high-low<4095)return nullptr;
    MEM_ADDRESS_REQUIREMENTS req{reinterpret_cast<void*>(low),reinterpret_cast<void*>(high),0};
    MEM_EXTENDED_PARAMETER param{};param.Type=MemExtendedParameterAddressRequirements;param.Pointer=&req;
    return static_cast<std::uint8_t*>(alloc(nullptr,nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE,&param,1));
}
bool PinModule(){HMODULE h=nullptr;return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&Initialize),&h)!=FALSE;}
bool Unwind(std::uint8_t* metadata,uintptr_t base,DWORD begin,DWORD end,const std::uint8_t* bytes,std::size_t size) {
    const auto u=reinterpret_cast<uintptr_t>(metadata);if(u<base || u-base>UINT32_MAX || size>64)return false;
    std::memcpy(metadata,bytes,size);auto* table=reinterpret_cast<RUNTIME_FUNCTION*>(metadata+64);
    *table={begin,end,static_cast<DWORD>(u-base)};return RtlAddFunctionTable(table,1,base)!=FALSE;
}
// No-call gateway emitters. Only volatile registers proven dead on the native continuation
// are used. No C++ execution, allocation, logging, stack change, or native-data write.
struct Bytes {std::array<std::uint8_t,256> a{};std::size_t n{};
    void b(std::uint8_t x){a[n++]=x;} void q(uintptr_t x){std::memcpy(a.data()+n,&x,8);n+=8;}
    void list(std::initializer_list<std::uint8_t> v){for(auto x:v)b(x);}
    void jump(uintptr_t p,bool r8=false){if(r8){list({0x49,0xB8});q(p);list({0x41,0xFF,0xE0});}else{list({0x48,0xB8});q(p);list({0xFF,0xE0});}}
};
constexpr std::uint8_t LookupUnwind[]{1,0,6,0,0,0x34,6,0,0,0x64,7,0,0,0x32,0,0x70};
constexpr std::uint8_t HandleLookupUnwind[]{1,0,10,0,0,0x34,8,0,0,0x54,9,0,0,0x64,10,0,0,0x32,0,0xF0,0,0xE0,0,0x70};
constexpr std::uint8_t ReleaseUnwind[]{1,0,2,0,0,0x32,0,0x30};
std::uint8_t* MakeGateway(unsigned hook,uintptr_t continuation,std::uint8_t* p) {
    if(!p)return nullptr;
    const bool release=hook==6;const bool handle=hook==7;
    Bytes c{};
    if(!release) {
        c.list({0x48,0xB8});c.q(reinterpret_cast<uintptr_t>(g_owned)); // mov rax, mask
        c.list({0x80,0x3C,0x10,0,0x74,0x1A}); // cmp byte [rax+rdx],0; je original(+26)
        c.list({0x48,0xB8});c.q(reinterpret_cast<uintptr_t>(&g_excluded));
        c.list({0xF0,0x48,0xFF,0});c.jump(g_base+(handle?0x3C06F0:0x3C0520));
        c.jump(continuation);
    } else {
        c.list({0x49,0xB8});c.q(reinterpret_cast<uintptr_t>(g_owned)); // r8 dead; preserve native RAX
        c.list({0x41,0x80,0x3C,0x10,0,0x74,0x13}); // cmp byte [r8+rdx],0; je continuation(+19)
        c.list({0x41,0xC6,0x04,0x10,0}); // clear before original freeCount publication
        c.list({0x49,0xB8});c.q(reinterpret_cast<uintptr_t>(&g_retired));
        c.list({0xF0,0x49,0xFF,0}); // lock inc qword [r8]
        c.jump(continuation,true);
    }
    std::memcpy(p,c.a.data(),c.n);
    if(!Unwind(p+512,reinterpret_cast<uintptr_t>(p),0,static_cast<DWORD>(c.n),release?ReleaseUnwind:(handle?HandleLookupUnwind:LookupUnwind),release?sizeof(ReleaseUnwind):(handle?sizeof(HandleLookupUnwind):sizeof(LookupUnwind))))return nullptr;
    DWORD old=0;if(!VirtualProtect(p,4096,PAGE_EXECUTE_READ,&old) || !FlushInstructionCache(GetCurrentProcess(),p,c.n))return nullptr;
    return p;
}
bool PrepareHook(unsigned i) {
    auto& h=g_hooks[i];h.target=reinterpret_cast<void*>(g_base+Rvas[i]);void* original=nullptr;
    void* detours[]{reinterpret_cast<void*>(&Factory),reinterpret_cast<void*>(&Constructor),reinterpret_cast<void*>(&Allocate),reinterpret_cast<void*>(&InitializeRecord),reinterpret_cast<void*>(&Commit)};
    // Reserve the gateway before MinHook creates its relay; populate it before enable.
    void* detour=i<5?detours[i]:nullptr;
    std::uint8_t* gateway=nullptr;
    if(i>=5) {
        gateway=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
        if(!gateway)return false;detour=gateway;
    }
    if(MH_CreateHook(h.target,detour,&original)!=MH_OK)return false;
    h.trampoline=static_cast<std::uint8_t*>(original);g_retained=true;
    const auto t=reinterpret_cast<uintptr_t>(original);h.metadata=AllocateMetadata(t);if(!h.metadata)return false;
    if(i<5) {
        const unsigned length=i==4?6:5;std::array<std::uint8_t,40> bytes{};
        if(!Read(t,bytes.data(),length+28) || std::memcmp(bytes.data(),reinterpret_cast<void*>(g_base+Rvas[i]),length))return false;
        const std::uint8_t absjump[]{0xFF,0x25,0,0,0,0};uintptr_t next=0;
        if(std::memcmp(bytes.data()+length,absjump,6))return false;std::memcpy(&next,bytes.data()+length+6,8);
        if(next!=g_base+Rvas[i]+length)return false;
        // R11 is dead at these inspected entries. Register jump avoids an apparent epilogue.
        Bytes tail{};tail.list({0x49,0xBB});tail.q(next);tail.list({0x41,0xFF,0xE3});
        std::memcpy(h.trampoline+length,tail.a.data(),tail.n);
        const std::uint8_t saved[]{1,5,2,0,5,0x34,static_cast<std::uint8_t>(i==0?2:1),0};
        const std::uint8_t push[]{1,6,2,0,6,0x32,2,0x30};
        if(!Unwind(h.metadata,t,0,length+static_cast<DWORD>(tail.n),i==4?push:saved,8) || !FlushInstructionCache(GetCurrentProcess(),original,length+tail.n))return false;
        if(i==0)g_factory=reinterpret_cast<FactoryFn>(original);
        if(i==1)g_constructor=reinterpret_cast<ConstructorFn>(original);
        if(i==2)g_allocate=reinterpret_cast<AllocateFn>(original);
        if(i==3)g_initialize=reinterpret_cast<InitializeFn>(original);
        if(i==4)g_commit=reinterpret_cast<CommitFn>(original);
    } else {
        // Native CMP / RIP-relative INC stay MinHook-relocated instructions, exactly once.
        // Both trampoline and relay run under the existing native frame. Avoid FF25
        // epilogue interpretation using register jumps and complete-frame unwind metadata.
        std::array<std::uint8_t,64> bytes{};if(!Read(t,bytes.data(),bytes.size()))return false;
        constexpr unsigned length=6, relayOffset=20;
        const std::uint8_t absjump[]{0xFF,0x25,0,0,0,0};uintptr_t next=0,relayTarget=0;
        if(std::memcmp(bytes.data()+length,absjump,6) || std::memcmp(bytes.data()+relayOffset,absjump,6))return false;
        std::memcpy(&next,bytes.data()+length+6,8);std::memcpy(&relayTarget,bytes.data()+relayOffset+6,8);
        if(next!=g_base+Rvas[i]+length || relayTarget!=reinterpret_cast<uintptr_t>(detour))return false;
        if(i==5) {
            const std::uint8_t cmp[]{0x39,0xBB,0x60,0x02,0,0};if(std::memcmp(bytes.data(),cmp,6))return false;
        } else if(i==7) {
            const std::uint8_t load[]{0x8B,0x8B,0x70,0x02,0,0};if(std::memcmp(bytes.data(),load,6))return false;
        } else {
            std::int32_t displacement=0;std::memcpy(&displacement,bytes.data()+2,4);
            if(bytes[0]!=0xFF || bytes[1]!=5 || t+6+static_cast<std::intptr_t>(displacement)!=g_base+CountRva)return false;
        }
        Bytes tail{},relay{};tail.jump(next,i==6);relay.jump(reinterpret_cast<uintptr_t>(detour),i==6);
        std::memcpy(h.trampoline+length,tail.a.data(),tail.n);
        std::memcpy(h.trampoline+relayOffset,relay.a.data(),relay.n);
        if(!Unwind(h.metadata,t,0,relayOffset+static_cast<DWORD>(relay.n),i==6?ReleaseUnwind:(i==7?HandleLookupUnwind:LookupUnwind),i==6?sizeof(ReleaseUnwind):(i==7?sizeof(HandleLookupUnwind):sizeof(LookupUnwind))) ||
           !FlushInstructionCache(GetCurrentProcess(),h.trampoline,relayOffset+relay.n) ||
           !MakeGateway(i,t,gateway))return false;
    }
    return true;
}
bool VerifyImage() {
    IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS64 nt{};
    if(!Read(g_base,dos) || dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<=0 || dos.e_lfanew>0x100000 ||
       !Read(g_base+static_cast<uintptr_t>(dos.e_lfanew),nt) || nt.Signature!=IMAGE_NT_SIGNATURE || nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 ||
       nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC || nt.FileHeader.TimeDateStamp!=0x669E384A || nt.OptionalHeader.SizeOfImage!=0x2C2B000)return false;
    std::array<std::uint8_t,2048> bytes{};
    for(const auto& p:pins::Bodies)if(p.size>bytes.size() || !Read(g_base+p.rva,bytes.data(),p.size) || std::memcmp(bytes.data(),p.bytes,p.size))return false;
    for(const auto& p:pins::Unwinds) {
        DWORD64 image=0;const auto* f=RtlLookupFunctionEntry(g_base+p.rva,&image,nullptr);
        if(!f || image!=g_base || f->BeginAddress!=p.begin || f->EndAddress!=p.end || f->UnwindData!=p.unwind ||
           !Read(g_base+p.unwind,bytes.data(),64) || std::memcmp(bytes.data(),p.bytes,64))return false;
    }
    return true;
}
bool Enabled(const char* name){char v[2]{};return GetEnvironmentVariableA(name,v,2)==1 && v[0]=='1';}
} // namespace
bool Initialize(uintptr_t base) {
    const DWORD error=GetLastError();
    if(!Enabled("KH2COOP_NATIVE_SORA_PRIVATE_STATUS")){SetLastError(error);return true;}
    // VUH-1513: with a player kit requested, the Friend1 clone would be the kit (selector 0
    // resolves through member 0). Refuse before any hook or allocation; the caller logs it.
    if(playerkit::BlocksNativeSoraPuppets()){SetLastError(error);return false;}
    g_requested=true;g_base=base;
    bool ok=!g_attempted.exchange(true) && !Enabled("KH2COOP_LIFETIME_TRACE") && !Enabled("KH2COOP_SPAWN_TRACE") &&
        !Enabled("KH2COOP_NATURAL_RESOURCE_TRACE") && !Enabled("KH2COOP_SURVIVING_PACK_PREPARE") && VerifyImage() && PinModule();
    if(ok) {
        g_retained=true;g_fls=FlsAlloc(nullptr);ok=g_fls!=FLS_OUT_OF_INDEXES && ContextFor()!=nullptr;
    }
    g_fresh=reinterpret_cast<AllocateFn>(base+0x3C03F0);
    // Prepare all hooks/dependencies before exposing any selection. Existing status handling
    // remains ordinary until every safety hook is enabled. Failed install never arms allocation.
    for(unsigned i=0;ok && i<HookCount;++i)ok=PrepareHook(i);
    constexpr unsigned order[]{4,5,6,7,3,2,1,0};
    for(auto i:order)if(ok){ok=MH_EnableHook(g_hooks[i].target)==MH_OK;if(ok)g_installed.fetch_or(1u<<i);}
    if(ok)g_ready=true;else Fault();SetLastError(error);return ok;
}
void StopNewAllocations(){g_ready=false;}
bool RetainsMinHookResources(){return g_retained.load();}
bool Ready(){return g_ready.load();}
void Drain(LogFn log) {
    if(!g_requested.load() || !log || !spawncontroller::IsDiagnosticGameThread())return;
    const DWORD error=GetLastError();g_ownerFrames.fetch_add(1);
    for(unsigned i=0;i<8;++i) {
        Event e{};bool have=false;
        if(TryAcquireSRWLockExclusive(&g_queueLock)) {
            if(g_count){e=g_queue[g_read];g_read=(g_read+1)%g_queue.size();--g_count;have=true;}
            ReleaseSRWLockExclusive(&g_queueLock);
        }
        if(!have)break;
        if(e.kind==4){log("[privatestatus] party build order=%u id=%u role=%s actor=%llX serial=%llu tid=%u frame=%u key=%d",e.order,e.factoryId,e.role==2?"local":"clone",e.actor,e.serial,e.thread,e.frame,e.key);continue;}
        log("[privatestatus] kind=%u serial=%llu tid=%u actor=%llX local=%llX status=%llX localStatus=%llX index=%d hp=%d localHp=%d",e.kind,e.serial,e.thread,e.actor,e.local,e.status,e.localStatus,e.index,e.hp,e.localHp);
    }
    // S2: claimed clones whose third Sora never came. Once the canonical pointer has rested on a
    // claimed (private, vetoed) actor for 30 owner frames, or the stamp moved, fail loudly.
    if(g_party.open && !g_party.bound && g_party.claimed!=0 && g_party.count==g_party.claimed) {
        Stamp now{};const bool same=StampNow(now) && Same(now,g_party.stamp);
        if(CanonicalIsClaimed()) {
            if(++g_party.canonicalFrames>=30 || !same) {
                g_party.bound=true;BindFault(15);
                log("[privatestatus] FAULT 15: the canonical player holds a private record (no third Sora in the party stamp); its commits stay vetoed this room, selection disarmed");
            }
        } else g_party.canonicalFrames=0;
    }
    unsigned owned=0;for(unsigned i=0;i<PoolCount;++i)owned+=_InterlockedCompareExchange8(&g_owned[i],0,0)!=0;
    static std::uint64_t last=~std::uint64_t{0};
    const auto value=owned+static_cast<std::uint64_t>(_InterlockedCompareExchange64(&g_excluded,0,0))+g_serial.load()+g_faults.load()+g_guardCount.load()+g_drops.load()+g_stale.load()+g_vetoes.load()+static_cast<std::uint64_t>(_InterlockedCompareExchange64(&g_retired,0,0))+(g_pending.armed?1u:0u)*0x100000000ULL+static_cast<std::uint64_t>(g_bindFault.load())*0x1000000000ULL+(static_cast<std::uint64_t>(g_party.claimed)|(static_cast<std::uint64_t>(g_party.count)<<4)|(g_party.bound?0x100ULL:0)|(g_party.open?0x200ULL:0))*0x100000000000ULL;
    if(value!=last){last=value;log("[privatestatus] stats requested=1 ready=%u installed=%u faults=%llu guards=%llu guardReason=%u dropped=%llu vetoes=%llu excluded=%lld retired=%lld foreign=%llu contexts=%u stale=%llu owned=%u pending=%u bindFault=%u party=%u/%u/%u",g_ready.load(),g_installed.load(),g_faults.load(),g_guardCount.load(),g_guardState.load()==2?static_cast<unsigned>(g_firstGuard.reason):0,g_drops.load(),g_vetoes.load(),_InterlockedCompareExchange64(&g_excluded,0,0),_InterlockedCompareExchange64(&g_retired,0,0),g_foreign.load(),g_contextCount.load(),g_stale.load(),owned,g_pending.armed?1u:0u,g_bindFault.load(),g_party.claimed,g_party.count,g_party.bound?1u:0u);}
    SetLastError(error);
}
} // namespace kh2coop::inject::privatestatus
