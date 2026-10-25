#include "ThornDefeat.hpp"
#include <cstdio>
#include <initializer_list>

namespace T = kh2coop::inject::thorndefeat;
unsigned failures=0,checks=0;
void check(bool value,const char* name) { ++checks;if(!value){++failures;std::printf("FAIL %s\n",name);} }
T::Key key() {
    T::Key k;k.session.fill(1);k.host=1;k.hostDelivery=2;k.self=3;k.delivery=4;
    k.mission=0x10000;k.generation=5;k.epoch=6;k.load=7;k.transition=8;k.slot=1;return k;
}
int main() {
    const auto k=key();const T::Gates ready{true,true,true,true,true,true};
    {
        T::State state;unsigned seals=0,ends=0,starts=0;
        check(state.Observe(k,true,100),"validated end16 occurrence admitted");
        seals+=state.Seal(k);
        // Real ordering: ReceiveWorldPackets drains ended16 then host=- before
        // the paused owner can ApplyMission. The same native mission survives.
        state.Observe(k,false,101);state.Observe(k,false,102);
        seals+=state.Seal(k);
        check(seals==1 && state.Source()==100,"host=- preserves the sealed original end16 once");
        if(state.Begin(k,ready)){++ends;state.CompletePrelude(true);}
        check(!state.Begin(k,ready)&&ends==1,"one native16 prelude per occurrence");
        // Host315 and local native315 can race in either order. Joining the
        // owned sequence must not call a second start entrypoint.
        if(!state.Owns315(k))++starts; // the real consumer's replay decision
        if(state.Native315(k))++starts;
        if(state.Native315(k))++starts;
        if(!state.Owns315(k))++starts;
        check(starts==1,"exactly one315 entry across replay/native and repeated native callbacks");
    }
    {
        T::State state;state.Observe(k,true,200);state.Seal(k);
        check(state.Native315(k),"a native315 before queued prelude service adopts the occurrence");
        check(!state.Begin(k,ready)&&state.Owns315(k)&&!state.Native315(k),
              "native-first order cannot start16 or315 again");
    }
    for(bool T::Gates::*field : {&T::Gates::scope,&T::Gates::healthy,&T::Gates::converged,
                               &T::Gates::nativeSafe,&T::Gates::mission,&T::Gates::pinned}) {
        T::State state;state.Observe(k,true,300);state.Seal(k);auto gates=ready;gates.*field=false;
        check(!state.Begin(k,gates)&&state.Current()==T::State::Phase::Admitted,
              "every fresh native/scope/health/convergence/mission/pin gate is required");
        check(state.Begin(k,ready),"a refused gate never consumes native start authority");
        state.CompletePrelude(false);
        check(!state.Begin(k,ready)&&!state.Owns315(k),"unknown native outcome cannot retry or claim315");
    }
    for(unsigned field=0;field<11;++field) {
        auto other=k;
        switch(field) {
        case 0:++other.session[0];break;case 1:++other.host;break;case 2:++other.hostDelivery;break;
        case 3:++other.self;break;case 4:++other.delivery;break;case 5:++other.mission;break;
        case 6:++other.generation;break;case 7:++other.epoch;break;case 8:++other.load;break;
        case 9:++other.transition;break;
        case 10:++other.slot;break;
        }
        T::State state;state.Observe(k,true,400);state.Seal(k);
        check(!state.Begin(other,ready)&&!state.Owns315(other),"old occurrence has no changed-scope authority");
        state.Observe(other,false,401);
        check(!state.Owns315(k)&&!state.Owns315(other),"different native scope retires the old receipt");
    }
    {
        T::State state;
        check(state.AdoptNative315(k) && state.Owns315(k),"qualified native315 before host end owns dedup only");
        check(!state.Begin(k,ready) && !state.Native315(k),"native witness cannot start16 or315 again");
        check(state.Observe(k,true,250) && state.Seal(k) && state.Source()==250,
              "later validated end16 seals the already observed native315");
        state.CompleteScene(false);
        check(!state.Owns315(k),"failed context verification aborts observed scene ownership");
    }
    {
        T::State state;check(!state.Observe(k,false,500),"non-end snapshot creates no victory authority");
        check(!state.Begin(k,ready)&&!state.Native315(k),"no unadmitted native path");
        check(!state.Observe(k,true,0),"unscoped source serial refused");
        auto invalid=k;invalid.mission=0;
        check(!state.Observe(invalid,true,500),"unavailable native mission refused");
    }
    std::printf("checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
