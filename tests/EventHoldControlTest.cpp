#include "EventHoldRuntimeProjection.hpp"
#include "kh2coop/EventHoldRecovery.hpp"
#include "kh2coop/SessionHost.hpp"
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace kh2coop;
using namespace kh2coop::eventhold;
static unsigned passed = 0;
static void require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
static Scope scope() {
    Scope s {}; std::memcpy(s.session, "a7a75d66cd0a2635012e91284706ae39", 32);
    s.generation=11; s.slot=1; s.hostConnection=100; s.selfConnection=200; s.hostDelivery=1; s.targetDelivery=1;
    return s;
}
static WorldEnvelope envelope(const Scope& s, std::uint64_t serial, const std::vector<std::uint8_t>& bytes) {
    return {{std::string(s.session,32),s.hostConnection,s.hostDelivery,serial,s.selfConnection,s.targetDelivery,WorldSourceKind::Native},bytes};
}
static Scope recoveredScope(const Scope& prior) {
    auto next=prior; ++next.generation; ++next.selfConnection;
    return next; // A fresh connection can legitimately begin at delivery1 again.
}
static ResyncResult converged(const Scope& s,std::uint64_t request=10) {
    ResyncResult result {};
    result.key={std::string(s.session,32),s.hostConnection,request};
    result.reason=ResyncResultReason::Converged;result.targetCount=1;
    result.targets[0].target={static_cast<std::uint8_t>(s.slot),s.selfConnection,s.targetDelivery};
    result.targets[0].status=ResyncAckStatus::Converged;result.targets[0].appliedCut=90;
    result.targets[0].fingerprint.fill(1);
    return result;
}
struct Fixture {
    Channel runtime, input, owner;
    RuntimeProjection projection {runtime};
    Scope s=scope(); std::uint64_t now=GetTickCount64();
    Fixture() {
        require(runtime.Open(GetCurrentProcessId()), "create private self-PID mapping");
        require(input.OpenExisting(GetCurrentProcessId()) && owner.OpenExisting(GetCurrentProcessId()), "existing independent views");
        require(projection.Tick(s,true,now), "bind");
    }
    Admission room(std::uint32_t epoch,std::uint64_t serial) {
        return projection.Admit(envelope(s,serial,encode(RoomTransition{epoch,8,12,50,0,0,22})),s,true,now);
    }
    Admission hold(bool active,std::uint32_t epoch,std::uint64_t serial) {
        return projection.Admit(envelope(s,serial,encode(EventHold{epoch,active,1})),s,true,now);
    }
    Command pop() { Command c {}; require(input.Read(now,c)==ReadResult::Record,"FIFO record"); return c; }
    void arm() {
        require(room(1,5)==Admission::Excluded,"initial room stays outside controls");
        require(pop().kind==Kind::Reset,"ordered initial reset");
        require(owner.PublishAck({s,1,0,1,SafeConverged|LiveEligible}),"native baseline ACK");
        require(projection.Tick(s,true,now) && runtime.Armed(),"armed after owner baseline only");
    }
};
struct RecoveryFixture : Fixture {
    Scope next=recoveredScope(s);
    Channel nextInput,nextOwner;
    void prepare() {
        arm();
        runtime.Invalidate(Abort::HeartbeatExpired);
        require(!projection.Tick(s,true,now),"old lifetime is blocked");
        runtime.Close();
        require(runtime.OpenScoped(GetCurrentProcessId(),next),"open distinct recovered mapping");
        require(nextInput.OpenScopedExisting(GetCurrentProcessId(),next) &&
                nextOwner.OpenScopedExisting(GetCurrentProcessId(),next),"independent recovered views");
        require(runtime.Bind(next,now),"fresh producer bound only after caller convergence proof");
    }
    void ack(std::uint32_t epoch=7) {
        Command reset {};
        require(nextInput.Read(now,reset)==ReadResult::Record && reset.ordinal==1 &&
                reset.kind==Kind::Reset && reset.scope==next,"fresh ordered reset1");
        require(nextOwner.PublishAck({next,1,0,epoch,SafeConverged|LiveEligible}),"fresh native reset ACK");
    }
};
template<class F> static void test(const char* name,F body) {
    body(); ++passed; std::cout << "PASS " << name << '\n';
}
int main() try {
    test("scoped names use every full identity field and never legacy fallback", [] {
        Fixture f;
        const auto next=recoveredScope(f.s);
        Channel creator,consumer;
        require(!consumer.OpenScopedExisting(GetCurrentProcessId(),next),"legacy mapping cannot satisfy scoped existing-only");
        require(creator.OpenScoped(GetCurrentProcessId(),next) &&
                consumer.OpenScopedExisting(GetCurrentProcessId(),next),"exact recovered namespace");
        require(creator.IsScopedFor(next) && !creator.OpenExisting(GetCurrentProcessId()),"open view cannot alias legacy");
        for (unsigned field=0;field<7;++field) {
            auto wrong=next;
            switch(field) {
            case 0:wrong.session[0]='f';break;
            case 1:++wrong.generation;break;
            case 2:wrong.slot=2;break;
            case 3:++wrong.hostConnection;break;
            case 4:++wrong.selfConnection;break;
            case 5:++wrong.hostDelivery;break;
            default:++wrong.targetDelivery;break;
            }
            Channel absent;
            require(!absent.OpenScopedExisting(GetCurrentProcessId(),wrong),"full scope names never alias");
        }
        auto wrong=next;++wrong.targetDelivery;
        require(!creator.Bind(wrong,f.now) && creator.Bind(next,f.now),"wrong namespace bind cannot claim producer");
        require(!consumer.Bind(next,f.now),"scoped existing producer cannot be taken over");
    });
    test("quiescent swap preserves old abort and transfers all local lifetime state", [] {
        Fixture f;f.arm();require(f.hold(true,1,8)==Admission::Published,"old active acquire");
        f.runtime.Invalidate(Abort::HeartbeatExpired);
        const auto next=recoveredScope(f.s);
        Channel candidate,owner,input;
        require(candidate.OpenScoped(GetCurrentProcessId(),next) && candidate.Bind(next,f.now),"candidate producer reset1");
        require(owner.OpenScopedExisting(GetCurrentProcessId(),next) && input.OpenScopedExisting(GetCurrentProcessId(),next),"candidate consumers");
        f.runtime.Swap(candidate);
        require(f.runtime.IsScopedFor(next) && f.runtime.PublishedOrdinal()==1 && !f.runtime.Armed(),"new local counters and epoch");
        require(candidate.PublishedOrdinal()==2 && candidate.Armed() && candidate.Reason()==Abort::HeartbeatExpired,"old view and counters transferred");
        require(f.input.Reason()==Abort::HeartbeatExpired,"old independent view remains aborted");
        Command reset {};require(input.Read(f.now,reset)==ReadResult::Record && reset.ordinal==1,"no inherited consumer cursor");
        require(owner.PublishAck({next,1,0,7,SafeConverged|LiveEligible}) && f.runtime.Arm(f.now),"new ACK arms only new view");
        Command acquire {};acquire.scope=next;acquire.epoch=7;acquire.kind=Kind::Acquire;acquire.hostSourceSerial=8;
        require(f.runtime.Publish(acquire,f.now),"old active/source ledger not inherited");
        candidate.Close();require(f.input.Reason()==Abort::HeartbeatExpired,"closing old producer cannot clear first abort");
    });
    test("post-Converged proof keeps old abort and needs new ordinal1 ACK", [] {
        RecoveryFixture f;f.prepare();
        require(f.input.Reason()==Abort::HeartbeatExpired,"old mapping abort immutable");
        require(!f.projection.Tick(f.next,true,f.now),"binding alone cannot rearm blocked projection");
        require(f.projection.AdoptConvergedBaseline(f.next,7,converged(f.next),f.now),"explicit caller-proven new baseline");
        require(!f.runtime.Armed() && f.projection.Tick(f.next,true,f.now) && !f.runtime.Armed(),"no arm before ACK");
        require(!f.projection.AdoptConvergedBaseline(f.next,7,converged(f.next),f.now),"repeat baseline not adopted");
        f.ack();require(f.projection.Tick(f.next,true,f.now) && f.runtime.Armed(),"new ACK arms on next Tick");
        require(f.projection.Admit(envelope(f.next,8,encode(EventHold{7,true,1})),f.next,true,f.now)==Admission::Published,
                "only a fresh future acquire enters new channel");
        require(f.input.Reason()==Abort::HeartbeatExpired && f.runtime.PublishedOrdinal()==2,"old and new lifetimes independent");
    });
    test("early new ACK cannot arm until explicit convergence proof", [] {
        RecoveryFixture f;f.prepare();f.ack();
        require(!f.projection.Tick(f.next,true,f.now) && !f.runtime.Armed(),"ACK alone cannot unblock");
        require(f.projection.AdoptConvergedBaseline(f.next,7,converged(f.next),f.now) && !f.runtime.Armed(),"adoption never auto-arms");
        require(f.projection.Tick(f.next,true,f.now) && f.runtime.Armed(),"next Tick verifies proven baseline ACK");
    });
    test("wrong current identity result freshness and target never clear blocked state", [] {
        for (unsigned field=0;field<20;++field) {
            RecoveryFixture f;f.prepare();auto current=f.next;auto result=converged(f.next);std::uint32_t epoch=7;
            switch(field) {
            case 0:current=f.s;break;
            case 1:current.generation=f.s.generation;break;
            case 2:current.selfConnection=f.s.selfConnection;break;
            case 3:current.session[0]='f';break;
            case 4:++current.hostConnection;break;
            case 5:current.slot=2;break;
            case 6:epoch=0;break;
            case 7:result.reason=ResyncResultReason::Rejected;break;
            case 8:result.key.requestId=0;break;
            case 9:++result.key.hostConnectionId;break;
            case 10:result.key.sessionId[0]='f';break;
            case 11:++result.targets[0].target.deliverySerial;break;
            case 12:result.targets[0].status=ResyncAckStatus::Arrived;break;
            case 13:result.targets[0].appliedCut=0;break;
            case 14:++result.targets[0].target.connectionId;break;
            case 15:result.targetCount=0;break;
            case 16:result.targetCount=3;break;
            case 17:result.targetCount=2;result.targets[1]=result.targets[0];break;
            case 18:result.targets[0].target.slot=2;break;
            default:result.targets[0].target.deliverySerial=0;break;
            }
            require(!f.projection.AdoptConvergedBaseline(current,epoch,result,f.now),"invalid proof refused");
            require(!f.projection.Tick(f.next,true,f.now) && f.input.Reason()==Abort::HeartbeatExpired,"bad proof changes neither lifetime");
        }
    });
    test("same-scope scoped mapping is not a fresh recovery lifetime", [] {
        Fixture f;f.arm();f.projection.Retire(Abort::HeartbeatExpired);f.runtime.Close();
        require(f.runtime.OpenScoped(GetCurrentProcessId(),f.s) && f.runtime.Bind(f.s,f.now),"distinct namespace alone available");
        require(!f.projection.AdoptConvergedBaseline(f.s,7,converged(f.s),f.now),"same scope cannot authorize recovery");
        require(f.input.Reason()==Abort::HeartbeatExpired,"legacy abort unchanged");
    });
    test("cached room cannot replace proven recovery epoch", [] {
        RecoveryFixture f;f.prepare();
        require(f.projection.AdoptConvergedBaseline(f.next,7,converged(f.next),f.now),"proven epoch7");
        require(f.projection.Admit(envelope(f.next,100,encode(RoomTransition{8,8,12,50,0,0,22})),f.next,true,f.now)==Admission::Aborted,
                "cached successor cannot mint or replace recovery baseline");
        require(f.runtime.Reason()==Abort::InvalidOrder && !f.runtime.Armed(),"wrong cached baseline stays unarmed");
    });
    test("recovered native reset ACK must match the proven epoch", [] {
        RecoveryFixture f;f.prepare();
        require(f.projection.AdoptConvergedBaseline(f.next,7,converged(f.next),f.now),"proven epoch7");
        f.ack(8);
        require(f.projection.Tick(f.next,true,f.now) && !f.runtime.Armed(),"different reset ACK cannot arm");
    });
    test("recovery transaction high-water survives a second expired lifetime", [] {
        RecoveryFixture f;f.prepare();
        require(f.projection.AdoptConvergedBaseline(f.next,7,converged(f.next,10),f.now),"first request10");
        f.ack();require(f.projection.Tick(f.next,true,f.now),"first recovered lifetime armed");
        f.projection.Retire(Abort::HeartbeatExpired);f.runtime.Close();
        f.next=recoveredScope(f.next);
        require(f.runtime.OpenScoped(GetCurrentProcessId(),f.next) && f.runtime.Bind(f.next,f.now),"second fresh mapping");
        require(!f.projection.AdoptConvergedBaseline(f.next,8,converged(f.next,10),f.now) &&
                !f.projection.AdoptConvergedBaseline(f.next,8,converged(f.next,9),f.now),"repeat and older request refused");
        require(f.projection.AdoptConvergedBaseline(f.next,8,converged(f.next,11),f.now),"new successful request accepted");
    });
    test("exact session and mapping ownership", [] {
        Channel absent; require(!absent.OpenExisting(GetCurrentProcessId()),"no existing fallback");
        Fixture f; Channel second; require(second.OpenExisting(GetCurrentProcessId()),"second view");
        require(!second.Bind(f.s,f.now),"second producer cannot take over");
        auto s=f.s; s.session[31]='g'; require(!ValidScope(s),"nonhex rejected");
        s=f.s; s.slot=0; require(!ValidScope(s),"host mapping not client control");
    });
    test("acquire transition release preserves wake latch and source order", [] {
        Fixture f; f.arm();
        require(f.hold(true,1,8)==Admission::Published,"Acquire epoch1/source8");
        require(f.room(2,7)==Admission::Published,"preallocated lower-source successor transition");
        require(f.hold(false,2,12)==Admission::Published,"Release epoch2/source12 after transition");
        auto a=f.pop(),t=f.pop(),r=f.pop();
        require(a.kind==Kind::Acquire && t.kind==Kind::Transition && r.kind==Kind::Release &&
            a.ordinal==2 && t.ordinal==3 && r.ordinal==4 && t.hostSourceSerial==7 && r.epoch==2,"exact FIFO identity");
        require(f.owner.PublishAck({f.s,4,12,2,0}),"consumption without convergence");
        Ack ack {}; require(f.input.ReadAck(ack) && !(ack.flags&SafeConverged),"release transport cannot mint convergence");
        require(f.owner.PublishAck({f.s,4,12,2,SafeConverged}),"later actual convergence ACK");
    });
    test("an acquire carries the host's in-field event start exactly", [] {
        // VUH-2359: wire round trip, projection into the input command, and the codec's refusals.
        EventStart start {2, 7, 0x343031, 9, {1, 0x702, 0xFFFFFFFF, 0xFFFF, 1, 0xFFFFFFFF, 3, 0, 0}};
        EventHold held {1, true, 95, start};
        const auto bytes = encode(held);
        const std::uint8_t* payload = nullptr; std::size_t size = 0;
        require(decodePacketHeader(bytes.data(), bytes.size(), payload, size) == PacketType::EventHold, "header");
        ByteReader reader(payload, size); EventHold back {}; read(reader, back);
        require(reader.atEnd() && back.start && *back.start == start && back.eventProgram == 95, "start round trip");
        bool refused = false;
        try { (void)encode(EventHold {1, false, 95, start}); } catch (const std::exception&) { refused = true; }
        require(refused, "a release cannot carry a start");
        Fixture f; f.arm();
        require(f.projection.Admit(envelope(f.s, 8, bytes), f.s, true, f.now) == Admission::Published, "published");
        const auto c = f.pop();
        require(c.kind == Kind::Acquire && c.eventProgram == 95 && c.startWorld == 2 && c.startRoom == 7 &&
            c.startId == 0x343031 && c.startFlags == 9 && c.startParams == start.params, "command carries the start");
        require(f.hold(false, 1, 9) == Admission::Published && f.pop().startId == 0, "a release carries none");
    });
    test("acquire and release cannot overwrite", [] {
        Fixture f; f.arm(); require(f.hold(true,1,8)==Admission::Published && f.hold(false,1,9)==Admission::Published,"back-to-back publication");
        require(f.pop().kind==Kind::Acquire && f.pop().kind==Kind::Release,"both remain FIFO");
    });
    test("Thorn victory preserves raw destination padding through wire and projection", [] {
        EventStart start{2,34,0x353133,0x12,{2,0xA5000002,0x00330033,0x33,1,0xFFFFFFFF,3,0,0}};
        const auto bytes=encode(EventHold{1,true,157,start});
        const std::uint8_t* payload=nullptr;std::size_t size=0;
        require(decodePacketHeader(bytes.data(),bytes.size(),payload,size)==PacketType::EventHold,"header");
        ByteReader reader(payload,size);EventHold back{};read(reader,back);
        require(reader.atEnd() && back.start && *back.start==start && back.eventProgram==157,
                "wire carries all nine host parameters without canonicalizing padding");
        Fixture f;f.arm();
        require(f.projection.Admit(envelope(f.s,8,bytes),f.s,true,f.now)==Admission::Published,"published");
        const auto c=f.pop();
        require(c.kind==Kind::Acquire && c.eventProgram==157 && c.startWorld==2 && c.startRoom==34 &&
                c.startId==start.id && c.startFlags==start.flags && c.startParams==start.params,
                "native input command keeps the full raw host descriptor");
    });
    test("wrong envelope scope and deliveries fail closed", [] {
        for (unsigned field=0;field<7;++field) {
            Fixture f; f.arm(); auto e=envelope(f.s,8,encode(EventHold{1,true,1}));
            switch(field) {
            case 0:e.scope.sessionId[0]='f';break;
            case 1:++e.scope.sourceConnectionId;break;
            case 2:++e.scope.sourceDeliverySerial;break;
            case 3:++e.scope.targetConnectionId;break;
            case 4:++e.scope.targetDeliverySerial;break;
            case 5:e.scope.kind=WorldSourceKind::Simulation;break;
            default:e.scope.hostSourceSerial=0;break;
            }
            require(f.projection.Admit(e,f.s,true,f.now)==Admission::Aborted && f.runtime.Reason()==Abort::WrongScope,"wrong original scope");
            require(f.runtime.PublishedOrdinal()==1,"no forged control");
        }
    });
    test("native generation and binding replacement invalidate", [] {
        Fixture f; f.arm(); auto other=f.s; ++other.generation;
        require(!f.projection.Tick(other,true,f.now) && f.runtime.Reason()==Abort::BindingReset,"native generation change");
        Command terminal {}; require(f.runtime.ReadPublished(2,terminal) && terminal.kind==Kind::Reset,"ordered terminal reset retained");
        Command c {}; require(f.input.Read(f.now,c)==ReadResult::Aborted,"input cannot use old scope");
    });
    test("cached or active bootstrap never becomes hold", [] {
        Fixture f; require(f.room(1,5)==Admission::Excluded,"cached baseline");
        require(f.hold(false,1,5)==Admission::Excluded,"cached inactive not command");
        require(f.hold(true,1,5)==Admission::Aborted,"active latejoin unsupported");
        require(f.runtime.PublishedOrdinal()==1,"no cached acquire");
    });
    test("startup source and conflicting replay reject after arm", [] {
        { Fixture f; f.arm(); require(f.hold(true,1,5)==Admission::Aborted && f.runtime.Reason()==Abort::Replay,"cached source reuse"); }
        { Fixture f; f.arm(); require(f.hold(true,1,8)==Admission::Published,"first acquire");
          require(f.hold(false,1,8)==Admission::Aborted && f.runtime.Reason()==Abort::Replay,"conflicting duplicate"); }
    });
    test("old epoch and unmatched release abort", [] {
        { Fixture f; f.arm(); require(f.hold(true,2,8)==Admission::Aborted,"future hold epoch"); }
        { Fixture f; f.arm(); require(f.hold(false,1,8)==Admission::Aborted,"unmatched release"); }
    });
    test("pending resync quarantine excluded", [] {
        Fixture f; f.arm(); require(!f.projection.Tick(f.s,false,f.now),"eligibility lost");
        require(f.runtime.Reason()==Abort::Unsupported && !f.runtime.Heartbeat(f.now),"no rearm heartbeat");
    });
    test("overflow cannot replace an unread acquire", [] {
        Fixture f; f.arm();
        for (std::uint64_t i=0;i<Capacity;++i)
            require(f.hold(i%2==0,1,10+i)==Admission::Published,"fill bounded FIFO");
        require(f.hold(true,1,100)==Admission::Aborted && f.runtime.Reason()==Abort::Overflow,"overflow explicit abort");
        Command a {}; require(f.runtime.ReadPublished(2,a) && a.kind==Kind::Acquire,"first acquire immutable");
    });
    test("source identity ledger never evicts replay history", [] {
        Fixture f; f.arm();
        for (std::uint64_t i=0;i<SourceCapacity-1;++i) {
            require(f.hold(i%2==0,1,100+i)==Admission::Published,"bounded source record"); f.pop();
        }
        require(f.hold(false,1,1000)==Admission::Aborted && f.runtime.Reason()==Abort::SourceLimit,"source bound explicit abort");
    });
    test("real short heartbeat expires and cannot be renewed", [] {
        Channel runtime,input; require(runtime.Open(GetCurrentProcessId()) && input.OpenExisting(GetCurrentProcessId()),"heartbeat views");
        const auto now=GetTickCount64(); require(runtime.Bind(scope(),now,50),"50ms test lease");
        Sleep(80); Command c {};
        require(input.Read(GetTickCount64(),c)==ReadResult::Aborted && input.Reason()==Abort::HeartbeatExpired,"wall expiry without avatar");
        require(!runtime.Heartbeat(GetTickCount64()),"expired lease not renewed");
    });
    test("default lease rides out a scheduler stall and expires past 4000ms", [] {
        static_assert(HeartbeatTimeoutMs == 4000);
        // A nominal margin only: relay age counts from the last packet, not from a stall's start.
        require(HeartbeatTimeoutMs < SessionConfig{}.heartbeatTimeoutMs,"lease below the relay's default peer timeout");
        // VUH-2640 bots05: one 1625ms PC-wide stall crossed the old 1500ms lease. Each gap
        // is one stalled pump; the native view checks first, as it keeps running meanwhile.
        for (const std::uint64_t gap : {1625ull,2594ull,3900ull,4000ull}) {
            Fixture f;
            require(f.input.Healthy(f.now+gap) && f.runtime.Heartbeat(f.now+gap),"stall keeps the link");
            require(f.input.Healthy(f.now+gap+gap) && f.runtime.Reason()==Abort::None,"renewed lifetime stays bound");
        }
        for (const std::uint64_t gap : {4001ull,4500ull}) {
            Fixture f;
            require(!f.input.Healthy(f.now+gap) && f.input.Reason()==Abort::HeartbeatExpired,"native view expires the lifetime");
            require(!f.runtime.Heartbeat(f.now+gap+1) && f.runtime.Reason()==Abort::HeartbeatExpired,"runtime cannot renew it");
        }
        Fixture runtimeFirst;
        require(!runtimeFirst.runtime.Heartbeat(runtimeFirst.now+4001) &&
            runtimeFirst.input.Reason()==Abort::HeartbeatExpired,"runtime's own pump expires it too");
    });
    test("concurrent newer heartbeat is fresh for an older caller timestamp", [] {
        Fixture f; require(f.runtime.Heartbeat(f.now+1),"runtime new heartbeat");
        require(f.input.Healthy(f.now),"no false backwards-time abort");
    });
    test("uninitialized existing mapping never formats or falls back", [] {
        char name[96] {}; std::snprintf(name,sizeof(name),"Local\\kh2coop_event_hold_v1_%lu",static_cast<unsigned long>(GetCurrentProcessId()));
        HANDLE raw=CreateFileMappingA(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,65536,name);
        require(raw!=nullptr,"owned zero mapping");
        Channel existing; const bool rejected=!existing.OpenExisting(GetCurrentProcessId());
        Channel creator; const bool initialized=creator.Open(GetCurrentProcessId());
        CloseHandle(raw);
        require(rejected && initialized && existing.OpenExisting(GetCurrentProcessId()),"create CAS publishes complete initialized mapping");
    });
    test("concurrent openers initialize once and only one producer binds", [] {
        std::array<Channel,8> channels;
        std::array<std::thread,8> threads;
        std::atomic<unsigned> opened=0, bound=0;
        for (std::size_t i=0;i<threads.size();++i) threads[i]=std::thread([&,i] {
            // A busy Open is a documented setup-thread retry, never a callback spin.
            for (unsigned attempt=0;attempt<10;++attempt) {
                if (channels[i].Open(GetCurrentProcessId())) { ++opened; break; }
                Sleep(1);
            }
            if (channels[i].IsOpen() && channels[i].Bind(scope(),GetTickCount64())) ++bound;
        });
        for (auto& thread:threads) thread.join();
        require(opened==8 && bound==1,"one initializer publication and exclusive runtime claim");
    });
    test("malformed admitted control cannot publish", [] {
        Fixture f; f.arm(); auto e=envelope(f.s,8,encode(EventHold{1,true,1})); e.packet.push_back(0);
        require(f.projection.Admit(e,f.s,true,f.now)==Admission::Aborted &&
            f.runtime.Reason()==Abort::InvalidOrder && f.runtime.PublishedOrdinal()==1,"exact payload consumption");
    });
    test("ACK exact receipt wrong scope replay and convergence", [] {
        Fixture f; f.arm(); require(f.hold(true,1,8)==Admission::Published,"acquire");
        auto wrong=f.s; ++wrong.targetDelivery;
        require(!f.owner.PublishAck({wrong,2,8,1,0}),"wrong ACK scope");
        require(!f.owner.PublishAck({f.s,2,9,1,0}),"wrong processed source");
        require(!f.owner.PublishAck({f.s,3,8,1,0}),"unpublished order");
        require(f.owner.PublishAck({f.s,2,8,1,0}),"actual receipt ACK");
        require(!f.owner.PublishAck({f.s,1,0,1,SafeConverged}),"regressive ACK");
    });
    test("ACK concurrent snapshots bounded and coherent", [] {
        Fixture f; f.arm(); std::atomic<bool> done=false; std::atomic<bool> bad=false;
        std::thread owner([&] {
            for (unsigned i=0;i<20000;++i)
                if (!f.owner.PublishAck({f.s,1,0,1,i%2 ? SafeConverged : LiveEligible})) bad=true;
            done=true;
        });
        while (!done.load()) { Ack a {}; if (f.input.ReadAck(a) &&
            (a.scope!=f.s || a.epoch!=1 || a.processedOrdinal!=1 || a.processedHostSourceSerial!=0 || a.flags>3)) bad=true; }
        owner.join(); require(!bad,"no torn ACK payload");
    });
    test("shutdown retains ordered abort without takeover", [] {
        Fixture f; f.arm(); f.projection.Retire(Abort::Shutdown);
        require(f.input.Reason()==Abort::Shutdown,"independent input sees shutdown");
        require(!f.runtime.Heartbeat(f.now),"shutdown not revived");
    });
    test("recovery result belongs to actual fresh snapshot and incarnation", [] {
        const auto s=scope(); auto result=converged(s);
        ResyncBegin begin {}; begin.key=result.key; begin.snapshotCut=90;
        begin.targetCount=1; begin.targets[0]=result.targets[0].target;
        begin.room.epoch=7; begin.room.worldId=2; begin.room.roomId=1;
        ResyncSnapshot snapshot {}; snapshot.room=begin.room;
        snapshot.progress.version=1; snapshot.coverageMask=ResyncNativeComplete;
        snapshot.nativeFingerprint.fill(1);
        require(RecoveryResultMatches(s,begin,snapshot,result),"matching empty-field native snapshot");
        for(unsigned bad=0;bad<13;++bad) {
            auto r=result; auto b=begin; auto n=snapshot; auto current=s;
            switch(bad) {
            case 0: ++r.key.requestId; break;
            case 1: ++r.targets[0].target.connectionId; break;
            case 2: ++r.targets[0].target.deliverySerial; break;
            case 3: ++r.targets[0].appliedCut; break;
            case 4: r.targets[0].fingerprint[0]^=1; break;
            case 5: r.targets[0].status=ResyncAckStatus::Received; break;
            case 6: r.reason=ResyncResultReason::Deadline; break;
            case 7: ++n.room.roomId; break;
            case 8: b.targetCount=2; b.targets[1]=b.targets[0]; break;
            case 9: r.targetCount=2; r.targets[1]=r.targets[0]; break;
            case 10: ++current.hostConnection; break;
            case 11: current.session[0]='b'; break;
            case 12: b.phase=static_cast<ResyncPhase>(255); break;
            }
            require(!RecoveryResultMatches(current,b,n,r),"mismatched native recovery proof refused");
        }
        ResyncEnemyState ordinary {}; ordinary.objectType=3; ordinary.identity.objectId=302;
        ordinary.hp=ordinary.maxHp=20; snapshot.enemies={ordinary}; snapshot.livingCount=1;
        require(RecoverySnapshotSupported(snapshot),"ordinary all-living population is eligible for native census proof");
        for(unsigned bad=0;bad<8;++bad) {
            auto n=snapshot;
            switch(bad) {
            case 0: n.enemies[0].identity.objectId=307; n.enemies[0].objectType=12; break;
            case 1: n.hold.active=true; break;
            case 2: n.deadCount=1; break;
            case 3: n.enemies[0].hp=0; break;
            case 4: n.enemies[0].life=ResyncLife::ObservedDeadHistory; break;
            case 5: n.coverageMask=0; break;
            case 6: n.enemies[0].objectType=12; break;
            case 7: n.livingCount=0; break;
            }
            require(!RecoverySnapshotSupported(n),"boss/script/active/dead/partial recovery remains refused");
        }
    });
    std::cout << "RESULT PASS cases=" << passed << " scope=owned-process-mappings-only\n";
    return 0;
} catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
