#pragma once
// Passive, fixed-capacity owner-thread diagnostic. No native memory or I/O here.
#include "NativeHitTrace.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>

namespace kh2coop::inject::combatcausal {
constexpr unsigned TargetCount = 5, QueueCount = 128, CauseCount = 16, PayloadMax = 4096;
// Keep this copied-facts header usable beside HitChannel.hpp: Codec.hpp also
// declares an unrelated HitClaim. Production-codec static_asserts live in controls.
enum class WireType : std::uint8_t { HitClaim=8, EnemyHp=23, EnemyDeath=24 };
enum class Kind : unsigned { Admission, Hit, Claim, HostApply, HpPublish, HpReceive,
    HpApply, DeathPublish, DeathReceive, DeathApply, Packet, Retired, ConsumerRejected, ConsumerAccepted };
enum class Reason : unsigned { Ok, Unavailable, Scope, Mapping, Source, Native,
    UnknownDelta, Payload, Capacity, Duplicate, Despawn, Loss, Pending, RejectFraming, RejectEpoch, RejectZeroSequence,
    RejectStaleSequence, RejectUnknownTarget, RejectDecode, RejectScope, RejectRole };
struct Scope {
    nativehittrace::Context native{};
    std::uint64_t delivery{}, sessionSalt{}, roster[3]{}, peerDelivery[3]{};
};
struct Target {
    std::uintptr_t actor{}, objentry{}, status{}, controller{}, record{};
    std::uint32_t netId{}, objectId{}, type{};
    std::int32_t hp{}, maxHp{};
};
struct Key {
    std::uint64_t connection{};
    std::uint32_t sequence{}, epoch{}, netId{}, objectId{}, attackId{};
    std::int32_t damage{};
};
struct Receipt {
    Kind kind{}; Reason reason{Reason::Unavailable};
    std::uint64_t serial{}, qpc{}, loss{}, event{}, hpSequence{}, payloadDigest{};
    Scope scope{}, admittedScope{}; Target target{}; Key key{};
    std::int32_t beforeHp{}, afterHp{}, requestedHp{};
    bool attempted{}, returned{}, readback{}, enqueued{}, locallyQualified{};
    unsigned causeCount{}, payloadBytes{}, payloadOriginalBytes{}, consumerOutcome{}, wireType{};
    std::uint64_t consumerSequenceFloor{}; std::uint32_t consumerEpoch{}; bool payloadTruncated{};
    std::uint64_t causes[CauseCount]{};
    std::uint8_t payload[PayloadMax]{};
};
inline bool SameScope(const Scope& a, const Scope& b) noexcept {
    const auto& x=a.native; const auto& y=b.native;
    return x.available && y.available && x.readMask==nativehittrace::ContextComplete &&
        y.readMask==nativehittrace::ContextComplete && x.generation==y.generation &&
        x.epoch==y.epoch && x.transitionSerial==y.transitionSerial && x.loadSerial==y.loadSerial &&
        x.connectionId==y.connectionId && x.hostConnectionId==y.hostConnectionId &&
        x.role==y.role && x.slot==y.slot && std::memcmp(x.location,y.location,sizeof(x.location))==0 &&
        a.delivery && a.delivery==b.delivery && a.sessionSalt && a.sessionSalt==b.sessionSalt &&
        std::memcmp(a.roster,b.roster,sizeof(a.roster))==0 &&
        std::memcmp(a.peerDelivery,b.peerDelivery,sizeof(a.peerDelivery))==0;
}
inline bool SameTarget(const Target& a,const Target& b) noexcept {
    return a.actor && a.objentry && a.status && a.controller && a.record &&
        a.actor==b.actor && a.objentry==b.objentry && a.status==b.status &&
        a.controller==b.controller && a.record==b.record && a.netId==b.netId &&
        a.objectId==b.objectId && a.type==b.type && a.maxHp==b.maxHp;
}
inline bool ScopeEligible(const Scope& s) noexcept {
    constexpr std::uint16_t location[6]={5,6,0,1,1,0};
    const auto& n=s.native;
    return n.available && n.readMask==nativehittrace::ContextComplete && n.generation && n.epoch &&
        n.loadSerial && n.transitionSerial && n.connectionId && n.hostConnectionId && s.delivery && s.sessionSalt &&
        s.roster[0] && s.roster[1] && !s.roster[2] && s.peerDelivery[0] && s.peerDelivery[1] &&
        ((n.role==1 && n.slot==0) || (n.role==2 && n.slot==1)) &&
        n.connectionId==s.roster[n.slot] && n.hostConnectionId==s.roster[0] &&
        std::memcmp(n.location,location,sizeof(location))==0;
}
inline bool SameActor(const nativehittrace::ActorSnapshot& a,
                      const nativehittrace::ActorSnapshot& b) noexcept {
    return a.readMask==nativehittrace::ActorComplete && b.readMask==nativehittrace::ActorComplete &&
        a.actor && a.objentry && a.status && a.actor==b.actor && a.objentry==b.objentry &&
        a.status==b.status && a.objectId==b.objectId && a.type==b.type &&
        a.team==b.team && a.namePrefix==b.namePrefix && a.maxHp==b.maxHp;
}
// Separate from NativeHitTrace::Witness (which remains unchanged).
inline bool PlayerEnemyHit(const nativehittrace::Event& e, bool client) noexcept {
    namespace ht=nativehittrace;
    const auto& a=e.before; const auto& b=e.after; const auto& h=a.hit; const auto& z=b.hit;
    if (!e.ownerThread || !e.returned || e.unwound || e.nested || e.overflow ||
        !e.coverageStable || e.coverageMask!=ht::AllHooks || !e.contextStable ||
        !e.metadataStable || !e.lossStable || !e.callerAvailable || e.callerRva!=0x3D613C ||
        !SameActor(a.victim,b.victim) || !SameActor(a.source,b.source) ||
        a.victim.objectId!=302 || a.victim.type!=4 || a.victim.hp<=0 ||
        a.source.type!=0 || a.source.namePrefix!=0x5F50 || a.source.hp<=0 ||
        h.readMask!=ht::HitComplete || z.readMask!=ht::HitComplete || !h.hit ||
        !h.attack || !h.owner || !h.attackHandle || !h.ownerHandle ||
        h.owner!=a.source.actor || h.canonicalPlayer!=a.source.actor ||
        h.head!=a.source.actor || h.tracked!=a.source.actor ||
        h.hit!=z.hit || h.attack!=z.attack || h.owner!=z.owner ||
        h.attackHandle!=z.attackHandle || h.ownerHandle!=z.ownerHandle ||
        h.atkpHandle!=z.atkpHandle || h.attackId!=z.attackId || h.stat!=0 || h.stat!=z.stat || h.kind!=z.kind ||
        z.canonicalPlayer!=a.source.actor || z.head!=a.source.actor || z.tracked!=a.source.actor ||
        h.kind==5 || h.kind==6 || h.damage<=0 || h.manualFilterOn || h.manualDrop ||
        z.manualFilterOn || z.manualDrop || (h.flags&2) || !(z.flags&2)) return false;
    if (client) {
        if (!h.syncDrop || !z.syncDrop || !e.policy.recorded || !e.policy.claimAttempted ||
            !e.policy.claimQueued || a.victim.hp!=b.victim.hp || z.damage!=0) return false;
        for(unsigned i=0;i<e.childCount;++i) {
            const auto& c=e.children[i];
            if(!c.returned || c.unwound || !c.matching || c.actor!=a.victim.actor ||
                c.stat!=0 || c.delta!=0 || !SameActor(c.before,a.victim) || !SameActor(c.after,b.victim) ||
                c.before.hp!=a.victim.hp || c.after.hp!=b.victim.hp) return false;
        }
        return true;
    }
    if (h.syncDrop || z.syncDrop || e.takeCalls!=1 || e.statCalls!=1 || e.childCount!=2 ||
        b.victim.hp<0 || b.victim.hp>=a.victim.hp) return false;
    const ht::Child* take=nullptr; const ht::Child* stat=nullptr;
    for (unsigned i=0;i<e.childCount;++i) {
        const auto& c=e.children[i];
        if (!c.returned || c.unwound || !c.matching || c.actor!=a.victim.actor ||
            c.stat!=0 || c.delta>=0 || !SameActor(c.before,a.victim) ||
            !SameActor(c.after,b.victim) || c.before.hp!=a.victim.hp || c.after.hp!=b.victim.hp)
            return false;
        if (c.kind==ht::ChildKind::Take) take=&c; else if(c.kind==ht::ChildKind::Stat) stat=&c;
    }
    if (!take || !stat || !take->callerAvailable || take->callerRva!=0x3D3CD5 || stat->takeSequence!=take->sequence || take->delta!=stat->delta ||
        stat->result!=b.victim.hp) return false;
    const auto sum=static_cast<std::int64_t>(a.victim.hp)+stat->delta;
    return b.victim.hp==(sum<0?0:sum);
}
inline std::uint64_t Digest(const std::uint8_t* p,unsigned n) noexcept {
    std::uint64_t h=0xcbf29ce484222325ULL;
    for(unsigned i=0;i<n;++i) {h^=p[i];h*=0x100000001b3ULL;} return h;
}
class Engine {
    struct Ledger { Target identity{}; std::int32_t expected{}, published{};
        std::uint64_t lastEvent{}, lastClaimConnection{}; std::uint32_t lastClaimSequence{};
        std::int32_t causeBefore{}, causeAfter{}; unsigned count{};
        std::uint64_t causes[CauseCount]{}; bool deathSent{}, deathReceived{}, dead{};
        std::uint64_t appliedHpSequence{}, receivedHpSequence{}; std::int32_t receivedHp{}; };
    Scope original_{}; std::array<Ledger,TargetCount> targets_{};
    std::array<Receipt,QueueCount> queue_{};
    unsigned read_{},size_{}; std::uint64_t serial_{},loss_{},dropped_{},started_{},drained_{};
    bool admitted_{},retired_{},pending_{};
    std::uint32_t lastHostClaimSequence_{}, lastLocalClaimSequence_{};
    Scope pendingScope_{}; std::array<Target,TargetCount> pendingTargets_{};
    Ledger* Find(unsigned id) noexcept {
        for(auto& t:targets_) if(t.identity.netId==id) return &t; return nullptr;
    }
    Receipt Row(Kind kind,const Scope& scope,std::uint64_t now) noexcept {
        Receipt r{};r.kind=kind;r.scope=scope;r.admittedScope=original_;r.qpc=now;r.loss=loss_;return r;
    }
    void Push(Receipt& r) noexcept {
        ++started_;
        if(serial_==UINT64_MAX || size_==QueueCount) {++loss_;++dropped_;retired_=true;return;}
        r.serial=++serial_;r.loss=loss_;queue_[(read_+size_)%QueueCount]=r;++size_;
    }
    bool Current(const Scope& s) noexcept {
        if(!admitted_ || retired_) return false;
        if(!SameScope(original_,s)) {Retire(s,0,Reason::Scope);return false;} return true;
    }
    bool Payload(Receipt& r,const std::uint8_t* p,unsigned n) noexcept {
        if(!p || !n || n>PayloadMax) {r.reason=Reason::Payload;return false;}
        r.payloadBytes=n;std::memcpy(r.payload,p,n);r.payloadDigest=Digest(p,n);return true;
    }
    static std::uint64_t Le(const std::uint8_t* p,unsigned count) noexcept {
        std::uint64_t value=0;for(unsigned i=0;i<count;++i)value|=static_cast<std::uint64_t>(p[i])<<(i*8);return value;
    }
    static bool Framed(const std::uint8_t* p,unsigned n,WireType type) noexcept {
        return p&&n>=3&&p[0]==static_cast<std::uint8_t>(type)&&Le(p+1,2)==n-3;
    }
    bool ClaimWire(const Scope& s,const Key& key,const std::uint8_t* p,unsigned n) noexcept {
        return n==46&&Framed(p,n,WireType::HitClaim)&&Le(p+3,4)==key.epoch&&Le(p+7,4)==key.sequence&&
            Le(p+11,2)==key.netId&&Le(p+13,4)==key.objectId&&Le(p+17,8)==key.connection&&
            Le(p+25,4)==key.attackId&&Le(p+29,4)==static_cast<std::uint32_t>(key.damage)&&p[45]==s.native.slot;
    }
    bool PacketWire(const Scope& s,Kind kind,unsigned id,std::uint64_t sequence,int hp,int maxHp,
                    const std::uint8_t* p,unsigned n) noexcept {
        if(kind==Kind::DeathPublish||kind==Kind::DeathReceive)
            return n==9&&Framed(p,n,WireType::EnemyDeath)&&Le(p+3,4)==s.native.epoch&&Le(p+7,2)==id;
        if(n<17||!Framed(p,n,WireType::EnemyHp)||Le(p+3,4)!=s.native.epoch||Le(p+7,8)!=sequence)return false;
        const auto count=Le(p+15,2);
        if(!count||count>TargetCount||n!=17+count*10)return false;
        bool found=false;
        for(unsigned i=0;i<count;++i) {
            const auto* row=p+17+i*10;const auto net=static_cast<unsigned>(Le(row,2));
            for(unsigned j=0;j<i;++j)if(Le(p+17+j*10,2)==net)return false;
            const auto* mapped=Find(net);const auto value=Le(row+2,4),maximum=Le(row+6,4);
            if(!mapped||!value||value>static_cast<unsigned>(INT32_MAX)||maximum!=static_cast<unsigned>(mapped->identity.maxHp))return false;
            if(kind==Kind::HpPublish&&value!=static_cast<unsigned>(mapped->expected))return false;
            if(net==id){if(value!=static_cast<unsigned>(hp)||maximum!=static_cast<unsigned>(maxHp))return false;found=true;}
        }
        return found;
    }
    bool Cause(Ledger& l,std::uint64_t id,int before,int after) noexcept {
        if(!id || before!=l.expected || after<0 || after>=before || l.count==CauseCount) return false;
        if(id==l.lastEvent) return false;
        l.causes[l.count++]=id;l.lastEvent=id;l.expected=after;
        l.causeBefore=before;l.causeAfter=after;return true;
    }
    void Causes(Receipt& r,const Ledger& l) noexcept {
        r.causeCount=l.count;for(unsigned i=0;i<l.count;++i)r.causes[i]=l.causes[i];
        r.event=l.lastEvent;
    }
public:
    bool Admitted() const noexcept{return admitted_;}
    bool Retired() const noexcept{return retired_;}
    std::uint64_t Loss() const noexcept{return loss_;}
    std::uint64_t Dropped() const noexcept{return dropped_;}
    std::uint64_t Started() const noexcept{return started_;}
    std::uint64_t Drained() const noexcept{return drained_;}
    void Retire(const Scope& s,std::uint64_t now,Reason why) noexcept {
        if(retired_)return;retired_=true;auto r=Row(Kind::Retired,s,now);r.reason=why;Push(r);
    }
    void Population(const Scope& s,const Target* rows,unsigned count,std::uint64_t now) noexcept {
        if(retired_)return;
        if(!admitted_) {
            if(!ScopeEligible(s) || !rows || count!=TargetCount)return;
            for(unsigned i=0;i<count;++i) {
                const auto& t=rows[i];if(!SameTarget(t,t) || t.objectId!=302 || t.type!=4 ||
                    !t.netId || t.hp<=0 || t.maxHp<t.hp)return;
                for(unsigned j=0;j<i;++j)if(rows[j].netId==t.netId || rows[j].actor==t.actor)return;
            }
            bool repeated=pending_ && SameScope(pendingScope_,s) && s.native.frame==pendingScope_.native.frame+1;
            for(unsigned i=0;i<count && repeated;++i) {
                bool match=false;
                for(const auto& prior:pendingTargets_)if(SameTarget(prior,rows[i])&&prior.hp==rows[i].hp)match=true;
                repeated=repeated&&match;
            }
            if(!repeated){pendingScope_=s;for(unsigned i=0;i<count;++i)pendingTargets_[i]=rows[i];pending_=true;return;}
            original_=s;admitted_=true;
            for(unsigned i=0;i<count;++i) {targets_[i].identity=rows[i];targets_[i].expected=rows[i].hp;
                targets_[i].published=rows[i].hp;auto r=Row(Kind::Admission,s,now);r.target=rows[i];
                r.reason=Reason::Ok;r.locallyQualified=true;Push(r);} return;
        }
        if(!Current(s))return;
        if(count>TargetCount || (!rows&&count)) {Retire(s,now,Reason::Mapping);return;}
        for(unsigned i=0;i<count;++i) {
            for(unsigned j=0;j<i;++j) if(rows[j].netId==rows[i].netId || rows[j].actor==rows[i].actor) {
                Retire(s,now,Reason::Mapping);return;
            }
            auto* l=Find(rows[i].netId);
            if(!l || !SameTarget(l->identity,rows[i]) || rows[i].hp!=l->expected) {
                Retire(s,now,Reason::UnknownDelta);return;
            }
        }
        for(auto& l:targets_) {
            bool found=false;for(unsigned i=0;i<count;++i)if(rows[i].netId==l.identity.netId)found=true;
            if(!found && !(l.dead && l.expected==0)) {Retire(s,now,Reason::Mapping);return;}
        }
    }
    void Hit(const Scope& s,const nativehittrace::Event& e,std::uint64_t now) noexcept {
        if(!admitted_ || retired_)return;
        auto r=Row(Kind::Hit,s,now);r.event=e.sequence;
        Ledger* l=nullptr;for(auto& t:targets_)if(t.identity.actor==e.before.victim.actor)l=&t;
        r.beforeHp=e.before.victim.hp;r.afterHp=e.after.victim.hp;
        r.returned=e.returned;r.readback=e.contextStable&&e.metadataStable;
        if(l)r.target=l->identity;
        const bool client=s.native.role==2;
        r.locallyQualified=Current(s)&&l&&PlayerEnemyHit(e,client)&&
            e.before.context.generation==s.native.generation && e.before.context.epoch==s.native.epoch &&
            e.before.victim.objentry==l->identity.objentry && e.before.victim.status==l->identity.status &&
            (client ? e.before.victim.hp==l->expected : Cause(*l,e.sequence,r.beforeHp,r.afterHp));
        r.reason=r.locallyQualified?Reason::Ok:Reason::Source;Push(r);
        // Any observed damage to an admitted enemy with unsupported ownership is
        // an unexplained change, never a host-local witness.
        if(l&&!r.locallyQualified&&e.before.victim.hp!=e.after.victim.hp)Retire(s,now,Reason::UnknownDelta);
    }
    void Claim(const Scope& s,const Key& key,std::uint64_t event,const std::uint8_t* bytes,
               unsigned size,bool queued,std::uint64_t now) noexcept {
        if(!admitted_||retired_)return;
        auto r=Row(Kind::Claim,s,now);r.key=key;r.event=event;r.enqueued=queued;
        auto* l=Find(key.netId);if(l)r.target=l->identity;
        r.locallyQualified=Current(s)&&l&&event&&key.connection==s.native.connectionId&&
            key.epoch==s.native.epoch&&key.objectId==302&&key.sequence>lastLocalClaimSequence_&&key.damage>0&&
            queued&&Payload(r,bytes,size)&&ClaimWire(s,key,bytes,size);
        if(!r.payloadBytes)Payload(r,bytes,size);
        if(r.locallyQualified)lastLocalClaimSequence_=key.sequence;
        r.reason=r.locallyQualified?Reason::Pending:Reason::Unavailable;Push(r);
    }
    void HostApply(const Scope& s,const Key& key,const Target& target,int before,int after,
                   bool attempted,bool returned,bool readback,std::uint64_t now) noexcept {
        if(!admitted_||retired_)return;
        auto r=Row(Kind::HostApply,s,now);r.key=key;r.target=target;r.beforeHp=before;r.afterHp=after;
        r.attempted=attempted;r.returned=returned;r.readback=readback;
        auto* l=Find(key.netId);
        const auto event=serial_+1;r.event=event | (1ULL<<63); // disjoint from native hit seq
        r.locallyQualified=Current(s)&&l&&SameTarget(l->identity,target)&&attempted&&returned&&readback&&
            key.epoch==s.native.epoch&&key.objectId==302&&key.connection==s.roster[1]&&
            key.sequence>lastHostClaimSequence_&&key.damage>0&&
            after==(static_cast<std::int64_t>(before)-key.damage<0?0:before-key.damage)&&
            Cause(*l,r.event,before,after);
        if(r.locallyQualified){lastHostClaimSequence_=key.sequence;l->lastClaimSequence=key.sequence;l->lastClaimConnection=key.connection;}
        r.reason=r.locallyQualified?Reason::Ok:Reason::Native;Push(r);
        if(!r.locallyQualified)Retire(s,now,Reason::Native);
    }
    void Consumer(const Scope& s,unsigned wireType,Reason reason,unsigned id,std::uint64_t sequence,
                  std::uint32_t consumerEpoch,std::uint64_t floor,const std::uint8_t* bytes,unsigned size,
                  std::uint64_t now,bool accepted=false) noexcept {
        if(!admitted_||retired_)return;
        auto r=Row(accepted?Kind::ConsumerAccepted:Kind::ConsumerRejected,s,now);r.consumerOutcome=accepted?1:2;r.wireType=wireType;
        r.hpSequence=sequence;r.consumerEpoch=consumerEpoch;r.consumerSequenceFloor=floor;
        auto* l=Find(id);if(l)r.target=l->identity;else r.target.netId=id;
        r.payloadOriginalBytes=size;r.payloadTruncated=size>PayloadMax;
        Payload(r,bytes,size>PayloadMax?PayloadMax:size);r.reason=reason;Push(r);
        // A bounded prefix is explicit loss, never complete raw rejection evidence.
        if(r.payloadTruncated||!r.payloadBytes)Retire(s,now,Reason::Loss);
        else if(!SameScope(original_,s))Retire(s,now,Reason::Scope);
    }
    void Packet(const Scope& s,Kind kind,unsigned id,std::uint64_t hpSequence,int hp,int maxHp,
                const std::uint8_t* bytes,unsigned size,bool outcome,bool despawn,std::uint64_t now) noexcept {
        if(!admitted_||retired_)return;
        auto r=Row(kind,s,now);r.hpSequence=hpSequence;r.enqueued=outcome;r.requestedHp=hp;
        if(kind==Kind::HpReceive||kind==Kind::DeathReceive){r.consumerOutcome=1;r.wireType=bytes&&size?bytes[0]:0;r.payloadOriginalBytes=size;}
        auto* l=Find(id);if(l){r.target=l->identity;Causes(r,*l);r.beforeHp=l->published;r.afterHp=hp;}
        bool ok=Current(s)&&l&&Payload(r,bytes,size)&&PacketWire(s,kind,id,hpSequence,hp,maxHp,bytes,size)&&outcome&&!despawn;
        if(l && kind==Kind::HpPublish) {
            ok=ok&&hpSequence&&hp==l->expected&&maxHp==l->identity.maxHp&&
               (hp==l->published||l->count>0);
            if(ok){l->published=hp;l->count=0;}
        } else if(l && kind==Kind::HpReceive) {
            ok=ok&&hpSequence&&hpSequence>l->receivedHpSequence&&hp>0&&maxHp==l->identity.maxHp;
            if(ok){l->receivedHpSequence=hpSequence;l->receivedHp=hp;}
        } else if(l && kind==Kind::DeathReceive) {
            ok=ok&&!l->deathReceived;
            if(ok)l->deathReceived=true;
        } else if(l && kind==Kind::DeathPublish) {
            ok=ok&&!l->deathSent&&l->expected==0&&l->causeBefore>0&&l->causeAfter==0&&l->lastEvent;
            if(ok){l->deathSent=true;l->dead=true;}
        }
        r.locallyQualified=ok;r.reason=despawn?Reason::Despawn:(ok?Reason::Ok:Reason::Unavailable);Push(r);
        if(kind==Kind::DeathPublish&&!ok)Retire(s,now,Reason::UnknownDelta);
    }
    void Applied(const Scope& s,Kind kind,const Target& target,std::uint64_t hpSequence,
                 int before,int requested,int after,bool attempted,bool returned,bool readback,
                 std::uint64_t now) noexcept {
        if(!admitted_||retired_)return;
        auto r=Row(kind,s,now);r.target=target;r.hpSequence=hpSequence;r.beforeHp=before;
        r.requestedHp=requested;r.afterHp=after;r.attempted=attempted;r.returned=returned;r.readback=readback;
        auto* l=Find(target.netId);
        r.locallyQualified=Current(s)&&l&&SameTarget(l->identity,target)&&returned&&readback&&
            before==l->expected&&after==requested&&((kind==Kind::DeathApply)?
            attempted&&before>0&&after==0&&!l->dead&&l->deathReceived:
            hpSequence&&after>0&&hpSequence==l->receivedHpSequence&&requested==l->receivedHp);
        if(r.locallyQualified){l->expected=after;l->published=after;if(kind==Kind::DeathApply)l->dead=true;else l->appliedHpSequence=hpSequence;}
        r.reason=r.locallyQualified?Reason::Ok:Reason::Native;Push(r);
        if(!r.locallyQualified)Retire(s,now,Reason::Native);
    }
    bool NeedsHpReadback(unsigned id,std::uint64_t sequence) noexcept {
        auto* l=Find(id);return admitted_&&!retired_&&l&&sequence&&sequence==l->receivedHpSequence&&sequence!=l->appliedHpSequence;
    }
    bool Pop(Receipt& r) noexcept {
        if(!size_)return false;r=queue_[read_];read_=(read_+1)%QueueCount;--size_;++drained_;return true;
    }
};
using Clock=std::uint64_t(*)() noexcept;
inline std::atomic<bool> enabled{false};
inline Engine engine{};
inline std::atomic<Clock> clockFn{nullptr};
inline Scope currentScope{};
inline bool Requested() noexcept{return enabled.load(std::memory_order_acquire);}
inline void Configure(bool requested,Clock clock) noexcept {
    if(!requested)return;clockFn.store(clock,std::memory_order_relaxed);enabled.store(true,std::memory_order_release);
}
inline std::uint64_t Now() noexcept{const auto clock=clockFn.load(std::memory_order_acquire);return clock?clock():0;}
inline void OnHit(const nativehittrace::Event& event) noexcept {
    if(Requested()){auto scope=currentScope;scope.native=event.before.context;engine.Hit(scope,event,Now());}
}
inline void Drain(nativehittrace::LogFn log) noexcept {
    if(!Requested()||!log)return;
    try {
    Receipt r{};
    for(unsigned n=0;n<16&&engine.Pop(r);++n) {
        char payload[PayloadMax*2+1]{};constexpr char hex[]="0123456789abcdef";
        for(unsigned i=0;i<r.payloadBytes;++i){payload[i*2]=hex[r.payload[i]>>4];payload[i*2+1]=hex[r.payload[i]&15];}
        using U=unsigned long long;
        log("[combat-causal] receipt schema=1 serial=%llu qpc=%llu kind=%u reason=%u qualified=%u event=%llu hpSequence=%llu loss=%llu generation=%llu delivery=%llu sessionSalt=%llu connection=%llu host=%llu peer=%llu hostDelivery=%llu peerDelivery=%llu epoch=%llu load=%llu transition=%llu frame=%llu role=%u slot=%u location=%u,%u,%u,%u,%u,%u netId=%u objectId=%u type=%u actor=%llX objentry=%llX status=%llX controller=%llX record=%llX targetHp=%d maxHp=%d claimConnection=%llu claimSeq=%u attackId=%u damage=%d beforeHp=%d afterHp=%d requestedHp=%d attempted=%u returned=%u readback=%u enqueued=%u causeCount=%u payloadBytes=%u payloadFnv64=%llu payload=%s incarnationAuthority=0",
            U(r.serial),U(r.qpc),unsigned(r.kind),unsigned(r.reason),unsigned(r.locallyQualified),U(r.event),U(r.hpSequence),U(r.loss),
            U(r.scope.native.generation),U(r.scope.delivery),U(r.scope.sessionSalt),U(r.scope.native.connectionId),U(r.scope.native.hostConnectionId),U(r.scope.roster[1]),U(r.scope.peerDelivery[0]),U(r.scope.peerDelivery[1]),
            U(r.scope.native.epoch),U(r.scope.native.loadSerial),U(r.scope.native.transitionSerial),U(r.scope.native.frame),unsigned(r.scope.native.role),unsigned(r.scope.native.slot),
            r.scope.native.location[0],r.scope.native.location[1],r.scope.native.location[2],r.scope.native.location[3],r.scope.native.location[4],r.scope.native.location[5],
            r.target.netId,r.target.objectId,r.target.type,U(r.target.actor),U(r.target.objentry),U(r.target.status),U(r.target.controller),U(r.target.record),r.target.hp,r.target.maxHp,
            U(r.key.connection),r.key.sequence,r.key.attackId,r.key.damage,r.beforeHp,r.afterHp,r.requestedHp,unsigned(r.attempted),unsigned(r.returned),unsigned(r.readback),unsigned(r.enqueued),r.causeCount,r.payloadBytes,U(r.payloadDigest),payload);
        if(r.consumerOutcome) {
            const auto& a=r.admittedScope;
            log("[combat-causal] consumer schema=1 serial=%llu outcome=%u wireType=%u reason=%u expectedEpoch=%u sequenceFloor=%llu originalBytes=%u retainedBytes=%u truncated=%u admittedGeneration=%llu admittedDelivery=%llu admittedSessionSalt=%llu admittedConnection=%llu admittedHost=%llu admittedPeer=%llu admittedHostDelivery=%llu admittedPeerDelivery=%llu admittedEpoch=%llu admittedLoad=%llu admittedTransition=%llu admittedRole=%u admittedSlot=%u admittedLocation=%u,%u,%u,%u,%u,%u",
                U(r.serial),r.consumerOutcome,r.wireType,unsigned(r.reason),r.consumerEpoch,U(r.consumerSequenceFloor),r.payloadOriginalBytes,r.payloadBytes,unsigned(r.payloadTruncated),
                U(a.native.generation),U(a.delivery),U(a.sessionSalt),U(a.native.connectionId),U(a.native.hostConnectionId),U(a.roster[1]),U(a.peerDelivery[0]),U(a.peerDelivery[1]),
                U(a.native.epoch),U(a.native.loadSerial),U(a.native.transitionSerial),unsigned(a.native.role),unsigned(a.native.slot),
                a.native.location[0],a.native.location[1],a.native.location[2],a.native.location[3],a.native.location[4],a.native.location[5]);
        }
        for(unsigned i=0;i<r.causeCount;++i)log("[combat-causal] association schema=1 serial=%llu index=%u event=%llu",U(r.serial),i,U(r.causes[i]));
    }
    static std::uint64_t lastSummary=0;
    if(Now()-lastSummary<1000000000ULL)return;lastSummary=Now();
    log("[combat-causal] summary schema=1 admitted=%u retired=%u started=%llu drained=%llu dropped=%llu loss=%llu nativeCoverageQualified=0 acceptance=0",
        unsigned(engine.Admitted()),unsigned(engine.Retired()),static_cast<unsigned long long>(engine.Started()),static_cast<unsigned long long>(engine.Drained()),
        static_cast<unsigned long long>(engine.Dropped()),static_cast<unsigned long long>(engine.Loss()));
    } catch(...) {
        engine.Retire(currentScope,Now(),Reason::Loss);
    }
}
} // namespace kh2coop::inject::combatcausal
