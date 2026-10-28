// VUH-2913: Axel II's collision-offered reaction commands. Pure rules only (AxelReaction.hpp).
#include "AxelReaction.hpp"
#include <cstdio>

namespace R = kh2coop::inject::scriptmirror::rules;
namespace {
unsigned errors=0;
void check(bool ok,const char* label) {
    std::printf("%s: %s\n",ok?"PASS":"FAIL",label);
    if(!ok)++errors;
}
constexpr std::uint64_t kHost=0x1111,kFriend=0x2222;
std::array<R::ReactionSeat,3> Seats(R::ReactionPoint host,R::ReactionPoint friendSeat) {
    return {{{kHost,host,true,true,true},{kFriend,friendSeat,true,true,true},{}}};
}
R::ReactionCompletion Claim(const R::ReactionWindow& w,std::uint8_t slot,std::uint64_t connection) {
    return {w.occurrence,connection,w.epoch,w.source,w.command,slot};
}
constexpr std::uint16_t kSource=7;
constexpr std::uint32_t kEpoch=5;
}
int main() {
    // 3BA550's order: script layer, motion layer, then the model default.
    R::AxelGroupMasks masks{};masks.collision=true;masks.defaults=0x00200003u;
    const auto bit=[](unsigned g){return 1u<<g;};
    check(!R::AxelGroupActive(masks,20) && R::AxelGroupActive(masks,21),
          "Overtaker's group 20 is off by default and Clear Light's group 21 is on by default");
    masks.motionOn=bit(20);
    check(R::AxelGroupActive(masks,20),"a motion's trigger 4 opens group 20");
    masks.scriptOff=bit(20);
    check(!R::AxelGroupActive(masks,20),"Axel I's init disable_collision(20) beats the motion enable");
    masks.scriptOn=bit(20);masks.scriptOff=0;masks.motionOn=0;masks.motionOff=bit(20);
    check(R::AxelGroupActive(masks,20),"a script enable beats a motion disable");
    masks.collision=false;
    check(!R::AxelGroupActive(masks,21),"an actor without collision data offers nothing");

    // Owner choice inside the family's spheres.
    const R::ReactionPoint axel{100,0,100};
    auto seats=Seats({100,0,1100},{100,0,4100});
    check(R::AxelFamilyWinner(R::kOvertakerSpheres,axel,0,seats)==0,"only the seat inside Overtaker's 2800 sphere wins it");
    seats=Seats({100,0,4100},{100,0,600});
    check(R::AxelFamilyWinner(R::kOvertakerSpheres,axel,0,seats)==1,"the friend wins an offer the host is outside of");
    seats=Seats({600,0,100},{100,0,600});
    check(R::AxelFamilyWinner(R::kOvertakerSpheres,axel,0,seats)==0,"equal distance goes to the lower seat");
    seats=Seats({100,0,600},{100,0,1100});seats[0].living=false;
    check(R::AxelFamilyWinner(R::kOvertakerSpheres,axel,0,seats)==1,"a downed seat never wins");
    seats=Seats({100,0,1500},{100,0,-1300});
    check(R::AxelFamilyWinner(R::kBurstFrontierSpheres,axel,0,seats)==0,
          "Burst Frontier's line runs along Axel's forward (local -z), not behind him");
    seats=Seats({100,0,-1300},{1500,0,100});
    check(R::AxelFamilyWinner(R::kBurstFrontierSpheres,axel,1.5707964f,seats)==1,"the line turns with Axel's yaw");
    seats=Seats({3000,0,3000},{-3000,0,-3000});
    check(R::AxelFamilyWinner(R::kBurstFrontierSpheres,axel,0,seats)==-1,"no seat inside: no owner");

    // Host windows: one occurrence per activation, a fixed owner, consumed at most once.
    R::AxelWindows windows;
    seats=Seats({100,0,600},{100,0,1100});
    const auto* opened=windows.Tick(0,true,1,kSource,kEpoch,10,seats);
    check(opened && opened->slot==1 && opened->command==R::kOvertaker && opened->connection==kFriend,
          "an active Overtaker opens one window for the seat that won it");
    const auto first=opened?opened->occurrence:0;
    check(!windows.Tick(0,true,0,kSource,kEpoch,11,seats) && windows.Find(R::kOvertaker)->slot==1,
          "the owner does not change while the offer stays up");
    auto* window=windows.Find(R::kOvertaker);
    check(R::ConsumeReaction(*window,Claim(*window,1,kFriend),kEpoch,12,seats),"the owner's claim consumes it");
    check(!windows.Tick(0,true,0,kSource,kEpoch,13,seats) && windows.Find(R::kOvertaker)->consumed,
          "a consumed offer is never reopened while it is still up");
    check(!windows.Tick(0,false,0,kSource,kEpoch,14,seats) && !windows.Find(R::kOvertaker),
          "the offer going down ends its window");
    opened=windows.Tick(0,true,0,kSource,kEpoch,15,seats);
    check(opened && opened->occurrence>first && opened->slot==0,"the next activation is a new occurrence");
    seats[0].fresh=false;
    (void)windows.Tick(0,true,0,kSource,kEpoch,16,seats);
    check(windows.Find(R::kOvertaker)->consumed,"an owner that goes stale loses its window");
    check(!windows.Tick(2,true,-1,kSource,kEpoch,17,seats) && !windows.Find(R::kBurstFrontier),"no owner, no window");

    // Clear Light follows the Overtaker its owner is playing (407C2B).
    R::AxelExecutorFacts chain{true,R::kOvertaker,1,490,1u<<21};
    check(R::AxelChainOwner(chain)==1,"Clear Light's owner is the seat inside its own Overtaker window");
    chain.flags=0;
    check(R::AxelChainOwner(chain)==-1,"outside AP12's trigger-26 window there is no Clear Light");
    chain.flags=1u<<21;chain.command=R::kBurstFrontier;
    check(R::AxelChainOwner(chain)==-1,"only an Overtaker executor chains Clear Light");
    chain.command=R::kOvertaker;chain.current=false;
    check(R::AxelChainOwner(chain)==-1,"an ended executor chains nothing");

    // Presses: one owner per occurrence on both seats, the host's own press included.
    {
        R::AxelWindows w;const auto s=Seats({100,0,600},{100,0,1100});
        (void)w.Tick(0,true,1,kSource,kEpoch,20,s);auto* live=w.Find(R::kOvertaker);
        check(R::AxelPressDecision(live,R::kOvertaker,0,kHost,kEpoch,21,false)==R::AxelPress::Refused,
              "a host press while the friend owns the window is refused");
        check(R::AxelPressDecision(live,R::kOvertaker,1,kFriend,kEpoch,21,false)==R::AxelPress::Claim,
              "the friend owner claims it");
        check(!R::ConsumeReaction(*live,Claim(*live,0,kHost),kEpoch,21,s),"the host cannot consume the friend's window");
        check(R::ConsumeReaction(*live,Claim(*live,1,kFriend),kEpoch,21,s),"the friend's claim still consumes once");
        check(!R::ConsumeReaction(*live,Claim(*live,1,kFriend),kEpoch,22,s),"a repeated claim never consumes twice");
        check(R::AxelPressDecision(live,R::kOvertaker,1,kFriend,kEpoch,22,false)==R::AxelPress::Refused,
              "a consumed window refuses its owner's next press");
    }
    {
        R::AxelWindows w;const auto s=Seats({100,0,600},{100,0,1100});
        (void)w.Tick(0,true,0,kSource,kEpoch,30,s);auto* live=w.Find(R::kOvertaker);
        // Both seats press in the same frame on a host-owned window.
        check(R::AxelPressDecision(live,R::kOvertaker,0,kHost,kEpoch,31,false)==R::AxelPress::Execute,
              "the host owner executes its own press");
        check(R::AxelPressDecision(live,R::kOvertaker,1,kFriend,kEpoch,31,false)==R::AxelPress::Refused,
              "the friend's same-frame press on the host's window is refused locally");
        check(R::ConsumeReaction(*live,Claim(*live,0,kHost),kEpoch,31,s) &&
              !R::ConsumeReaction(*live,Claim(*live,1,kFriend),kEpoch,31,s),
              "the host consumes once and a friend claim for the same occurrence is refused");
    }
    {
        R::AxelWindows w;const auto s=Seats({100,0,600},{100,0,1100});
        (void)w.Tick(0,true,1,kSource,kEpoch,40,s);auto* live=w.Find(R::kOvertaker);
        // Both seats press in the same frame on a friend-owned window.
        check(R::AxelPressDecision(live,R::kOvertaker,0,kHost,kEpoch,41,false)==R::AxelPress::Refused &&
              R::AxelPressDecision(live,R::kOvertaker,1,kFriend,kEpoch,41,false)==R::AxelPress::Claim,
              "same frame, friend's window: the host is refused and the friend claims");
        check(R::ConsumeReaction(*live,Claim(*live,1,kFriend),kEpoch,41,s),"and that claim consumes once");
        check(R::AxelPressDecision(live,R::kBurstFrontier,1,kFriend,kEpoch,41,false)==R::AxelPress::Refused,
              "a window never answers another command");
        auto fresh=*live;fresh.consumed=false;
        check(R::AxelPressDecision(&fresh,R::kOvertaker,1,kFriend,kEpoch,41,true)==R::AxelPress::Refused &&
              R::AxelPressDecision(&fresh,R::kOvertaker,1,0x9999,kEpoch,41,false)==R::AxelPress::Refused &&
              R::AxelPressDecision(&fresh,R::kOvertaker,1,kFriend,kEpoch+1,41,false)==R::AxelPress::Refused &&
              R::AxelPressDecision(nullptr,R::kOvertaker,0,kHost,kEpoch,41,false)==R::AxelPress::Refused,
              "a downed seat, a replaced connection, another epoch or no window refuses");
    }

    // Client: the host's offers to this seat.
    {
        R::AxelOffers offers;
        check(offers.Apply(5,R::kOvertaker,1,1,kFriend,kSource,kEpoch,50) && offers.Find(R::kOvertaker) &&
              offers.Find(R::kOvertaker)->slot==1,"the friend keeps the host's offer to its own seat");
        check(offers.Apply(5,R::kOvertaker,1,1,kFriend,kSource,kEpoch,51) && !offers.Apply(4,R::kOvertaker,1,1,kFriend,kSource,kEpoch,51),
              "a repeated offer is idempotent and an older occurrence is refused");
        check(offers.Apply(6,R::kOvertaker,0,1,kHost,kSource,kEpoch,52) && !offers.Find(R::kOvertaker),
              "an offer to the host's seat ends the friend's window");
        check(!offers.Apply(7,999,1,1,kFriend,kSource,kEpoch,53),"only Axel's commands are kept");
        (void)offers.Apply(8,R::kBurstFrontier,1,1,kFriend,kSource,kEpoch,54);
        auto* w=offers.Find(R::kBurstFrontier);
        check(w && R::AxelPressDecision(w,R::kBurstFrontier,1,kFriend,kEpoch,54+R::kAxelClientWindowFrames,false)==R::AxelPress::Refused,
              "a client window is bounded");
    }

    // sysobj_player (1/3) inside Axel's RC: the executor, not the nearest seat.
    R::AxelExecutor executor{R::kOvertaker,kSource,1,kFriend,kEpoch,100,false};
    const bool live=R::AxelExecutorLive(executor,kEpoch,kFriend,110);
    check(live && R::AxelPlayerSlot(live,executor.slot,0)==1,"1/3 resolves to the seat that ran the RC");
    check(R::AxelPlayerSlot(false,executor.slot,0)==0,"outside an RC, 1/3 keeps its own choice");
    check(!R::AxelExecutorLive(executor,kEpoch,0x9999,110) && !R::AxelExecutorLive(executor,kEpoch+1,kFriend,110) &&
          !R::AxelExecutorLive(executor,kEpoch,kFriend,100+R::kAxelExecutorLeaseFrames),
          "a replaced seat, another epoch or the lease ends the executor");
    check(R::AxelExecutorContinues(executor,false,false) && !executor.engaged,"the executor waits for its first contact");
    check(R::AxelExecutorContinues(executor,true,false) && executor.engaged &&
          R::AxelExecutorContinues(executor,false,true),"it holds while Axel is attached to or capturing that actor");
    check(!R::AxelExecutorContinues(executor,false,false),"and ends once both are gone");

    // Routing identity.
    std::uint16_t command=0;
    check(R::AxelResourceFor(R::AxelResource(R::kClearLight),81,command) && command==R::kClearLight,
          "an Axel window resource names its command");
    check(!R::AxelResourceFor(R::AxelResource(R::kClearLight),2229,command) &&
          !R::AxelResourceFor(0x80000000u|R::kClearLight,81,command) && !R::AxelResourceFor(0xFFFF0000u|297,81,command),
          "Axel I, owner roles and other commands never take the Axel path");
    check((0x80000000u|R::kOwnerRoleSequenceCap)<R::kAxelResourceBand,"owner roles stop below the Axel band");
    check(R::HostRoutedReactionClass(81) && !R::HostRoutedReactionClass(2229),
          "Axel II's commands are host-routed; Axel I has none");
    check(R::NativeReactionCommand(490,R::KnownReactionSource(false,true,81),false)==0 &&
          R::NativeReactionCommand(490,R::KnownReactionSource(false,true,81),true)==490,
          "an Axel II prompt shows only with this seat's admitted window");
    R::NativeReactionIdentity id{};
    id.target={1,0,2,UINT32_MAX};id.roots={1,2,3,4,5,6,7,8};id.occurrence=1;id.incarnation=1;id.connection=kHost;
    id.resource=R::AxelResource(R::kOvertaker);id.epoch=kEpoch;id.actorHandle=1;id.objectId=81;id.source=kSource;
    id.command=R::kOvertaker;id.slot=0;id.family=3;
    check(R::NativeReactionIdentityValid(id),"an Axel window proof is a valid native identity");
    id.target[2]=0;
    check(!R::NativeReactionIdentityValid(id),"it needs the collision object the native picker named");

    std::printf("%u failure(s)\n",errors);
    return errors?1:0;
}
