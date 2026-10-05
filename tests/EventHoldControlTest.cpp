#include "EventHoldRuntimeProjection.hpp"
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
template<class F> static void test(const char* name,F body) {
    body(); ++passed; std::cout << "PASS " << name << '\n';
}
int main() try {
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
    test("acquire and release cannot overwrite", [] {
        Fixture f; f.arm(); require(f.hold(true,1,8)==Admission::Published && f.hold(false,1,9)==Admission::Published,"back-to-back publication");
        require(f.pop().kind==Kind::Acquire && f.pop().kind==Kind::Release,"both remain FIFO");
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
    std::cout << "RESULT PASS cases=" << passed << " scope=owned-process-mappings-only\n";
    return 0;
} catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
