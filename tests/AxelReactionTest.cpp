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

    // Owner choice inside the family's spheres, in the groups active now.
    const auto open=[](std::uint32_t groups){R::AxelGroupMasks m{};m.collision=true;m.scriptOn=groups;return m;};
    const auto overtakerOn=open(bit(R::kOvertakerGroup));
    const auto burstOn=open(bit(R::kBurstFrontierGroupA)|bit(R::kBurstFrontierGroupB));
    const R::ReactionPoint axel{100,0,100};
    auto seats=Seats({100,0,1100},{100,0,4100});
    check(R::AxelFamilyWinner(R::kOvertakerSpheres,overtakerOn,axel,0,seats)==0,"only the seat inside Overtaker's 2800 sphere wins it");
    check(R::AxelFamilyWinner(R::kOvertakerSpheres,open(0),axel,0,seats)==-1,"a sphere whose group is off owns nothing");
    seats=Seats({100,0,4100},{100,0,600});
    check(R::AxelFamilyWinner(R::kOvertakerSpheres,overtakerOn,axel,0,seats)==1,"the friend wins an offer the host is outside of");
    seats=Seats({600,0,100},{100,0,600});
    check(R::AxelFamilyWinner(R::kOvertakerSpheres,overtakerOn,axel,0,seats)==0,"equal distance goes to the lower seat");
    seats=Seats({100,0,600},{100,0,1100});seats[0].living=false;
    check(R::AxelFamilyWinner(R::kOvertakerSpheres,overtakerOn,axel,0,seats)==1,"a downed seat never wins");
    seats=Seats({100,0,1500},{100,0,-1300});
    check(R::AxelFamilyWinner(R::kBurstFrontierSpheres,burstOn,axel,0,seats)==0,
          "Burst Frontier's line runs along Axel's forward (local -z), not behind him");
    seats=Seats({100,0,-1300},{1500,0,100});
    check(R::AxelFamilyWinner(R::kBurstFrontierSpheres,burstOn,axel,1.5707964f,seats)==1,"the line turns with Axel's yaw");
    seats=Seats({3000,0,3000},{-3000,0,-3000});
    check(R::AxelFamilyWinner(R::kBurstFrontierSpheres,burstOn,axel,0,seats)==-1,"no seat inside: no owner");
    {
        // Ione's probe (5c1a6f87): Axel at the origin, yaw 0; the host in a group-24 sphere, the friend at the end of
        // group 23's line. Only the group that is open may choose.
        const R::ReactionPoint origin{0,0,0};
        const auto s=Seats({0,550,267},{0,0,1400});
        check(R::AxelFamilyWinner(R::kBurstFrontierSpheres,open(bit(R::kBurstFrontierGroupA)),origin,0,s)==1,
              "with only group 23 open, the seat in group 24's sphere cannot take the friend's offer");
        check(R::AxelFamilyWinner(R::kBurstFrontierSpheres,open(bit(R::kBurstFrontierGroupB)),origin,0,s)==0,
              "with only group 24 open, only its spheres choose");
        check(R::AxelFamilyWinner(R::kBurstFrontierSpheres,
                  open(bit(R::kBurstFrontierGroupA)|bit(R::kBurstFrontierGroupB)),origin,0,s)==0,
              "with both open, the nearer seat wins");
        check(R::AxelFamilyWinner(R::kBurstFrontierSpheres,overtakerOn,origin,0,s)==-1,"Overtaker's group opens no Burst sphere");
        unsigned a=0,b=0;
        for(const auto& sphere:R::kBurstFrontierSpheres)(sphere.group==R::kBurstFrontierGroupA?a:b)+=1;
        check(a==7 && b==3,"Burst Frontier is seven group-23 rows and three group-24 rows");
    }

    // The host's mint runs only for the host seat in Axel II's encounter.
    check(R::AxelHostFrameAdmitted(true,0,2,0x14,137),"the host seat mints in 02/14 btl137");
    check(!R::AxelHostFrameAdmitted(true,0,2,0x14,134) && !R::AxelHostFrameAdmitted(true,0,2,0x05,87) &&
          !R::AxelHostFrameAdmitted(true,0,3,0x14,137) && !R::AxelHostFrameAdmitted(true,0,2,0x15,137),
          "another battle, Axel I, another world or another room mints nothing");
    check(!R::AxelHostFrameAdmitted(false,0,2,0x14,137) && !R::AxelHostFrameAdmitted(true,1,2,0x14,137),
          "a client or a friend seat never mints");

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

    // An observation that fails revokes claim authority, and recovery never mints the same activation again.
    {
        R::AxelWindows w;const auto s=Seats({100,0,600},{100,0,1100});
        R::AxelFrameFacts up{};up.observed=true;up.active={true,false,false};up.winners={1,-1,-1};
        check(w.Frame(up,kSource,kEpoch,60,s)==1u && w.ObservationCurrent(60) && w.ObservationCurrent(61),
              "an observed activation opens its window, and the observation is current this frame and the next");
        const auto* opening=w.Find(R::kOvertaker);
        const auto occurrence=opening?opening->occurrence:0;
        R::AxelFrameFacts lost{};
        const bool withheldNone=w.Frame(lost,kSource,kEpoch,61,s)==0u;
        auto* held=w.Find(R::kOvertaker);
        check(withheldNone && !w.ObservationCurrent(61) && held && held->consumed,
              "a failed read withholds the open window and the observation is no longer current");
        check(held && !R::ConsumeReaction(*held,Claim(*held,1,kFriend),kEpoch,62,s) &&
              R::AxelPressDecision(held,R::kOvertaker,1,kFriend,kEpoch,62,false)==R::AxelPress::Refused,
              "the owner's claim on a withheld window is refused");
        const bool reminted=w.Frame(up,kSource,kEpoch,63,s)!=0u;
        const auto* recovered=w.Find(R::kOvertaker);
        check(!reminted && recovered && recovered->occurrence==occurrence && recovered->consumed,
              "reads recovering while the same activation is up mint nothing new");
        R::AxelFrameFacts down=up;down.active={false,false,false};
        (void)w.Frame(down,kSource,kEpoch,64,s);
        check(!w.Find(R::kOvertaker),"only an observed end of the offer clears its tombstone");
        const bool next=w.Frame(up,kSource,kEpoch,65,s)==1u;
        const auto* again=w.Find(R::kOvertaker);
        check(next && again && again->occurrence>occurrence,"the next observed activation is a new occurrence");
        check(!w.ObservationCurrent(67),"an observation two frames old is not current");
        w.Reset();
        check(!w.ObservationCurrent(65),"a reset forgets the observation");
    }

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
    constexpr std::uint64_t kSession=0xA1,kRestarted=0xB2;
    {
        R::AxelOffers offers;
        check(offers.Apply(5,R::kOvertaker,1,1,kFriend,kSource,kEpoch,50,kSession) && offers.Find(R::kOvertaker) &&
              offers.Find(R::kOvertaker)->slot==1,"the friend keeps the host's offer to its own seat");
        check(offers.Apply(5,R::kOvertaker,1,1,kFriend,kSource,kEpoch,51,kSession) &&
              !offers.Apply(4,R::kOvertaker,1,1,kFriend,kSource,kEpoch,51,kSession),
              "a repeated offer is idempotent and an older occurrence is refused");
        check(offers.Apply(6,R::kOvertaker,0,1,kHost,kSource,kEpoch,52,kSession) && !offers.Find(R::kOvertaker),
              "an offer to the host's seat ends the friend's window");
        check(!offers.Apply(5,R::kOvertaker,1,1,kFriend,kSource,kEpoch,52,kSession) && !offers.Find(R::kOvertaker),
              "and an older occurrence still cannot reopen it");
        check(!offers.Apply(7,999,1,1,kFriend,kSource,kEpoch,53,kSession),"only Axel's commands are kept");
        check(!offers.Apply(7,R::kOvertaker,1,1,kFriend,kSource,kEpoch,53,0),"an offer without a session is refused");
        (void)offers.Apply(8,R::kBurstFrontier,1,1,kFriend,kSource,kEpoch,54,kSession);
        auto* w=offers.Find(R::kBurstFrontier);
        check(w && R::AxelPressDecision(w,R::kBurstFrontier,1,kFriend,kEpoch,54+R::kAxelClientWindowFrames,false)==R::AxelPress::Refused,
              "a client window is bounded");
    }
    {
        // Ione's probe (5c1a6f87): occurrence 10 accepted, then a restarted server reusing the epoch offers 1.
        R::AxelOffers offers;
        (void)offers.Apply(10,R::kOvertaker,1,1,kFriend,kSource,kEpoch,70,kSession);
        check(!offers.Apply(1,R::kOvertaker,1,1,kFriend,kSource,kEpoch,71,kSession),
              "within one session, the old high-water mark holds");
        check(offers.Apply(1,R::kOvertaker,1,1,kFriend,kSource,kEpoch,72,kRestarted) &&
              offers.Find(R::kOvertaker)->occurrence==1,
              "a restarted server's fresh sequence in the same epoch is a new history and is accepted");
        check(offers.Apply(1,R::kBurstFrontier,1,1,kFriend,kSource+1,kEpoch,73,kRestarted),
              "another Axel netId is another authority");
        (void)offers.Apply(20,R::kClearLight,1,1,kFriend,kSource,kEpoch,74,kSession);
        offers.Reset();
        check(!offers.Find(R::kClearLight) && offers.Apply(1,R::kClearLight,1,1,kFriend,kSource,kEpoch,75,kSession),
              "a reset forgets the windows and their high-water marks");
    }

    // The executor (1/3 inside Axel's RC): bound to the actor that ran it, released only on a confirmed release.
    {
        R::AxelExecutor executor{R::kOvertaker,kSource,1,kFriend,kEpoch,100,false};
        check(R::AxelExecutorLive(executor,kEpoch,110),"a new executor is live");
        check(!R::AxelExecutorLive(executor,kEpoch+1,110) &&
              !R::AxelExecutorLive(executor,kEpoch,100+R::kAxelExecutorLeaseFrames),
              "another epoch, or the lease before any contact, ends it");
        const R::AxelContact none{true,false,true,false},unread{false,false,false,false};
        const R::AxelContact attached{true,true,true,false},captured{true,false,true,true};
        check(R::AxelExecutorContinues(executor,none) && !executor.engaged,"the executor waits for its first contact");
        check(R::AxelExecutorContinues(executor,attached) && executor.engaged &&
              R::AxelExecutorContinues(executor,captured),"it holds while Axel is attached to or capturing that actor");
        check(R::AxelExecutorLive(executor,kEpoch,100+5*R::kAxelExecutorLeaseFrames),
              "an engaged RC is not ended by the lease");
        check(R::AxelExecutorContinues(executor,unread),"an unreadable attachment and capture are not a release");
        check(R::AxelExecutorContinues(executor,{false,false,true,false}) &&
              R::AxelExecutorContinues(executor,{true,false,false,false}),
              "nor is one read gone while the other is unread");
        check(!R::AxelExecutorContinues(executor,none),"it ends once both are read and gone");
    }
    check(R::AxelPlayerRouteFor(false,false)==R::AxelPlayerRoute::Choice,"outside an RC, 1/3 keeps its own choice");
    check(R::AxelPlayerRouteFor(true,true)==R::AxelPlayerRoute::Executor,
          "inside one, 1/3 names the executor's own actor, downed or not (no eligibility test)");
    check(R::AxelPlayerRouteFor(true,false)==R::AxelPlayerRoute::Unresolved,
          "if that actor is gone, no other seat is chosen in its place");

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
