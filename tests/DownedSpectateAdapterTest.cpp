#include "DownedSpectateAdapter.hpp"
#include <atomic>
#include <cstdio>
#include <thread>
using namespace kh2coop::inject::spectate;
namespace {
unsigned checks{}, failures{};
void check(bool value) { ++checks; if (!value) ++failures; }
CandidateFacts Good() {
    CandidateFacts f;
    f.remote = {true,true,true,true,1,0,20};
    f.actor=200; f.localActor=100; f.clones={300,200};
    f.index=0; f.slot=1; f.localSlot=0; f.planned=2;
    f.kits=true; f.wantKit=90; f.otherKit=84; return f;
}
CandidateMetadata Metadata(std::uint64_t actor) { return {{actor,2,900,1000,42,true},90,0}; }
}
int main() {
    // Mixed-kit resolver: another cached clone is retired/unmapped. It must
    // never be inspected; only the bound current actor can reach guarded reads.
    auto f=Good(); unsigned census{}, reads{};
    auto current=[&](std::uint64_t actor) { ++census; check(actor==200); return true; };
    auto read=[&](std::uint64_t actor) { ++reads; check(census>0 && actor==200); return Metadata(actor); };
    check(ResolveCandidate(f,current,read).actor==200); check(census==2 && reads==1);
    // Stale bound candidate: census fails before ANY actor/objentry read.
    census=reads=0;
    check(!ResolveCandidate(f,[&](std::uint64_t){++census;return false;},read).valid);
    check(census==1 && reads==0);
    for(unsigned bad=0;bad<8;++bad) {
        auto invalid=f;
        if(bad==0) invalid.slot=255;
        if(bad==1) invalid.slot=0;
        if(bad==2) invalid.index=2;
        if(bad==3) invalid.remote.active=false;
        if(bad==4) invalid.remote.age=31;
        if(bad==5) invalid.remote.nativeBound=false;
        if(bad==6) invalid.actor=400;
        if(bad==7) invalid.remote.flags=2;
        census=reads=0; check(!ResolveCandidate(invalid,current,read).valid); check(census==0 && reads==0);
    }
    check(!ResolveCandidate(f,current,[](auto actor){auto m=Metadata(actor);m.objectId=84;return m;}).valid);
    check(!ResolveCandidate(f,current,[](auto actor){auto m=Metadata(actor);m.target.valid=false;return m;}).valid);
    census=0; check(!ResolveCandidate(f,[&](auto){return ++census==1;},read).valid); check(census==2);
    // Entity-list order is unrelated to kit: actor200 at clone[1] still wins.
    f.planned=1; f.present=0; f.clones={200,300};
    check(ResolveCandidate(f,current,read).valid);
    f.present=1; census=reads=0; check(!ResolveCandidate(f,current,read).valid); check(!census && !reads);
    f=Good(); f.planned=0; f.clones={}; f.friends={200,300}; f.kits=false;
    check(ResolveCandidate(f,current,read).valid);
    f.blocksPlayer=true; check(!ResolveCandidate(f,current,read).valid);

    // Deterministic off-owner shutdown while originalFollow is paused with an
    // actual borrowed pointer. Execute the same gate/drain protocol as the DLL.
    // No other feature retains trampolines in this control.
    for (bool replaced : {false,true}) {
        CallbackGate gate; gate.RetainHooks();
        std::atomic<bool> borrowedReady{}, stopFollow{}, allowReturn{}, retired{};
        std::atomic<std::uint64_t> canonical{100}, camera{100};
        std::thread owner([&] {
            const bool entered=gate.Enter(); if(!entered) std::terminate();
            camera=200; borrowedReady=true;
            while(!allowReturn.load()) std::this_thread::yield();
            if(replaced) camera=300; // native owner replacement must survive
            if(RestoreOwned(camera.load(),200,true,canonical.load()==100)) camera=100;
            gate.Leave();
        });
        while(!borrowedReady.load()) std::this_thread::yield();
        std::thread shutdown([&] {
            DrainCallbacks(gate,false,[&]{stopFollow=true;},[]{std::this_thread::yield();},[&]{canonical=0;retired=true;});
        });
        while(!stopFollow.load()) std::this_thread::yield();
        check(!gate.Enter()); check(!gate.Drained()); check(!retired.load());
        check(canonical==100 && camera==200); check(gate.RetainsHooks());
        allowReturn=true; owner.join(); shutdown.join();
        check(retired && canonical==0 && gate.Drained()); check(camera==(replaced?300U:100U));
        check(gate.RetainsHooks()); // no global MH_Uninitialize, even alone
        check(!gate.Enter()); // delayed detour can only call retained original
    }
    CallbackGate nested; check(nested.Enter()); bool retired=false;
    check(!DrainCallbacks(nested,true,[]{},[]{},[&]{retired=true;}));
    check(!retired && !nested.Drained()); nested.Leave();
    check(DrainCallbacks(nested,false,[]{},[]{},[&]{retired=true;}) && retired);
    std::printf("downed spectate adapter: %u/%u controls PASS\n",checks-failures,checks);
    return failures?1:0;
}
