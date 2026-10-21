#include "ScriptReaction.hpp"
#include <cstdio>
namespace R=kh2coop::inject::scriptmirror::rules;
namespace {
unsigned failures=0;
void check(bool ok,const char* label) { std::printf("%s: %s\n",ok?"PASS":"FAIL",label);if(!ok)++failures; }
R::NativeReactionIdentity identity() {
    R::NativeReactionIdentity id{};id.target={0x101,0,0x202,UINT32_MAX};
    id.roots={0x100000,0x200000,0x300000,0x400000,0x500000,0x600000,0x700000,0x800000};
    id.occurrence=10;id.incarnation=20;id.connection=30;id.resource=20;id.epoch=40;
    id.load=50;id.transition=60;id.actorHandle=70;id.objectId=307;
    id.source=80;id.command=432;id.slot=1;id.family=1;return id;
}
void publish(R::NativeReactionCache& cache,const R::NativeReactionIdentity& id,std::uint32_t frame) {
    check(cache.Begin(),"refresh begins");cache.Winner(&id,frame);cache.End(&id,id.command,id.target,frame);
}
}
int main() {
    const std::array<R::ReactionActorCandidate,3> twoSeats{{{0x1000,0},{0x2000,1},{0,0}}};
    check(R::ReactionActorForSeat(0,0,0x1000,twoSeats)==0x1000,"unused third slot0 cannot erase canonical host completion");
    check(R::ReactionActorForSeat(1,0,0x1000,twoSeats)==0x2000,"host resolves a living friend candidate");
    check(!R::ReactionActorForSeat(2,0,0x1000,twoSeats),"missing third player grants no callback actor");
    check(R::ReactionActorForSeat(1,1,0x3000)==0x3000,"client1 local proof uses canonical actor with no host candidate table");
    check(R::ReactionActorForSeat(2,2,0x4000)==0x4000,"client2 local proof uses its own authenticated seat");
    check(!R::ReactionActorForSeat(0,1,0x3000,twoSeats) && !R::ReactionActorForSeat(2,1,0x3000,twoSeats),
          "client cannot resolve other seats through a host candidate snapshot");
    check(!R::ReactionActorForSeat(1,1,0,twoSeats),"missing client canonical cannot borrow a matching host candidate");
    auto stale=twoSeats;stale[2]={0x9000,0};
    check(R::ReactionActorForSeat(0,0,0x1000,stale)==0x1000 && !R::ReactionActorForSeat(0,0,0,stale),
          "remote default slot0 cannot replace even a missing canonical actor");
    auto third=twoSeats;third[2]={0x4000,2};
    check(R::ReactionActorForSeat(2,0,0x1000,third)==0x4000,"three-seat host resolves the exact requested remote seat");
    check(!R::ReactionActorForSeat(3,0,0x1000,third) && !R::ReactionActorForSeat(0,3,0x1000,third),
          "invalid requested or local seat refuses");
    auto id=identity();R::NativeReactionCache cache;
    check(R::NativeReactionIdentityValid(id),"native receipt identity includes current roots and occurrence");
    auto malformed=id;malformed.connection=0;check(!R::NativeReactionIdentityValid(malformed),"missing owner identity refuses");
    malformed=id;malformed.target[3]=3;check(!R::NativeReactionIdentityValid(malformed),"non-native target shape refuses");
    R::NativeReactionAcceptance invalid;
    check(!invalid.Reserve({{},1,1,true},{}),"fabricated valid bit cannot reserve an empty identity");
    check(!cache.Current(id,100),"current registry binding alone is not a native selection");publish(cache,id,100);
    check(cache.Current(id,100) && cache.Current(id,101),"receipt works now and before the next native tick");
    check(!cache.Current(id,99) && !cache.Current(id,102),"read cannot renew freshness or accept a future receipt");
    const auto original=cache.Published();
    check(cache.Current(id,100) && cache.Current(id,100) && cache.Published().refresh==original.refresh,"queries are idempotent");
    auto replacement=id;++replacement.resource;++replacement.incarnation;++replacement.occurrence;
    check(replacement.target==id.target && replacement.roots==id.roots && !cache.Current(replacement,100),
          "same native handle/address/target cannot relabel an old cache to a reused allocation");
    auto changed=id;++changed.occurrence;check(!cache.Current(changed,100),"new occurrence requires a fresh selection");
    changed=id;++changed.incarnation;check(!cache.Current(changed,100),"same-role command incarnation is immutable");
    changed=id;++changed.connection;check(!cache.Current(changed,100),"roster replacement revokes receipt");
    changed=id;++changed.actorHandle;check(!cache.Current(changed,100),"canonical player replacement revokes receipt");
    changed=id;++changed.roots[5];check(!cache.Current(changed,100),"source status root replacement revokes receipt");
    changed=id;++changed.roots[4];check(!cache.Current(changed,100),"player objentry replacement revokes receipt");
    changed=id;++changed.epoch;check(!cache.Current(changed,100),"room epoch replacement revokes receipt");
    changed=id;++changed.load;check(!cache.Current(changed,100),"same-room load replacement revokes receipt");
    changed=id;++changed.transition;check(!cache.Current(changed,100),"same-room transition replacement revokes receipt");
    changed=id;++changed.slot;check(!cache.Current(changed,100),"wrong selected seat refuses");
    changed=id;changed.target[3]=3;check(!cache.Current(changed,100),"TARGET mode3 cannot replace native no-part sentinel");
    check(cache.Begin(),"early-return refresh begins");cache.End(&id,id.command,id.target,101);
    check(!cache.Current(id,101),"no native winner invalidates cache even if native bytes stay unchanged");
    check(cache.Begin(),"overwritten winner refresh begins");cache.Winner(&id,102);cache.Winner(nullptr,102);
    cache.End(&id,id.command,id.target,102);check(!cache.Current(id,102),"unrelated last winner clears an earlier owned candidate");
    check(cache.Begin(),"two winners refresh begins");cache.Winner(&id,103);cache.Winner(&replacement,103);
    cache.End(&id,id.command,id.target,103);check(!cache.Current(id,103),"cache publication must match the last winner");
    check(cache.Begin(),"reentry begins");cache.Winner(&id,104);check(!cache.Begin(),"nested refresh refuses scope");
    cache.End(&id,id.command,id.target,104);check(!cache.Current(id,104),"reentry poisons outer proof despite identical bytes");
    check(cache.Begin(),"post-helper mutation begins");cache.Winner(&id,105);cache.End(&replacement,id.command,id.target,105);
    check(!cache.Current(replacement,105),"binding replacement between winner and publication cannot relabel proof");
    check(cache.Begin(),"target check begins");cache.Winner(&id,106);auto other=id.target;++other[1];
    cache.End(&id,id.command,other,106);check(!cache.Current(id,106),"all four logical TARGET words must match");
    publish(cache,replacement,107);check(cache.Current(replacement,107) && !cache.Current(id,107),"fresh selection admits new allocation only");
    const auto serial=cache.Published().refresh;cache.Reset();publish(cache,replacement,108);
    check(cache.Published().refresh>serial,"reset does not reuse a refresh serial");
    check(R::NativeReactionCommand(432,false,false)==432 && R::NativeReactionCommand(432,false,true)==432,"unrelated and unknown native command preserved");
    check(R::NativeReactionCommand(432,true,true)==432 && R::NativeReactionCommand(432,true,false)==0,"only known unactionable mirror command yields Triangle");
    // VUH-2620: the friend's own command on a mirrored Dusk (objectId 318) is actionable natively, with no
    // host occurrence; Twilight Thorn's script commands still wait for one.
    constexpr std::uint32_t dusk=318,thorn=307;
    check(R::NativeReactionCommand(69,R::KnownReactionSource(false,true,dusk),false)==69,
          "friend Reversal (69) on a mirrored Dusk stays actionable");
    check(R::NativeReactionCommand(32,R::KnownReactionSource(false,true,dusk),false)==32,
          "friend command 32 on a mirrored Dusk stays actionable");
    check(R::NativeReactionCommand(297,R::KnownReactionSource(false,true,thorn),false)==0 &&
              R::NativeReactionCommand(297,R::KnownReactionSource(false,true,thorn),true)==297,
          "a Thorn script command still needs the host occurrence");
    check(R::NativeReactionCommand(69,R::KnownReactionSource(true,true,dusk),false)==0,
          "an actor the host routes reactions for still waits, whatever its class");
    check(!R::KnownReactionSource(false,false,thorn) && !R::KnownReactionSource(false,false,dusk),
          "an unmirrored, unrouted actor is never a known source");
    // r2: the receiver callback (0x3B5FA0) on the client. reversal-02 refused 50 friend Reversals here.
    check(!R::ClientOwnedCallbackReceiver(true,true,true,dusk),
          "a mirrored Dusk's callback passes through on the client (Reversal starts natively)");
    check(R::ClientOwnedCallbackReceiver(true,true,true,thorn),
          "a mirrored Thorn's unpaired callback stays refused (warmup and held included)");
    check(R::ClientOwnedCallbackReceiver(true,true,false,0),
          "a mirrored receiver whose class cannot be read stays refused");
    // r3: "mirrored" also covers host-owned F_ field/puzzle actors (SuppressClientDecision); only the
    // qualified Dusk is exempt, so any other class, field actors included, keeps its unpaired refusal.
    constexpr std::uint32_t fieldActor=0x1F6,otherEnemy=302;
    check(R::ClientOwnedCallbackReceiver(true,true,true,fieldActor),
          "a mirrored F_ field actor's unpaired callback stays refused");
    check(R::ClientOwnedCallbackReceiver(true,true,true,otherEnemy),
          "a mirrored enemy of another, unqualified class stays refused");
    check(!R::ClientOwnedCallbackReceiver(false,true,true,thorn) && !R::ClientOwnedCallbackReceiver(true,false,true,thorn),
          "the host, and an unmirrored receiver, never refuse here");
    check(R::NativeReactionActive(replacement,replacement.roots[0]+0x10,replacement.command,replacement.target),
          "native active command and inline TARGET prove accepted handoff");
    check(!R::NativeReactionActive(replacement,replacement.roots[0]+0x20,replacement.command,replacement.target),
          "same TARGET bytes at another pointer cannot prove native active request");
    check(!R::NativeReactionActive(replacement,replacement.roots[0]+0x10,345,replacement.target),
          "current active command must match accepted command");
    check(!R::NativeReactionActive(replacement,replacement.roots[0]+0x10,replacement.command,other),
          "current active TARGET must match every accepted logical word");
    R::NativeReactionAcceptance acceptance;
    check(!acceptance.Reserve(original,replacement),"old receipt cannot reserve a new incarnation");
    check(acceptance.Reserve(cache.Published(),replacement),"exact selected identity reserves native acceptance");
    check(acceptance.Current(replacement) && !acceptance.Reserve(cache.Published(),replacement),"pending acceptance cannot restart before callback");
    check(!acceptance.Reserve(original,id) && acceptance.Pending(),
          "a stale pending action fences even another fresh-looking incarnation until callback or scope retirement");
    acceptance.Finish(false);check(!acceptance.Current(replacement),"native refusal rolls back local latch");
    check(acceptance.Reserve(cache.Published(),replacement),"native refusal permits a later valid attempt");acceptance.Finish(true);
    check(!acceptance.Take(id) && acceptance.Current(replacement),"other incarnation callback cannot consume the latch");
    check(acceptance.Take(replacement) && !acceptance.Take(replacement),"exact delayed callback takes once");
    auto capture=replacement;capture.family=2;capture.mode=2;capture.target[2]=0;publish(cache,capture,109);
    check(R::OwnerCommandCaptureLive(capture.mode,true) && !R::OwnerCommandCaptureLive(capture.mode,false),"captured commands require native current capture");
    changed=capture;changed.mode=1;check(!cache.Current(changed,109),"same role cannot inherit another producer mode");
    check(acceptance.Reserve(cache.Published(),capture),"captured command uses same local receipt boundary");acceptance.Reset();
    check(!acceptance.Take(capture),"retirement revokes a pending callback latch");
    std::array<R::ReactionSeat,3> seats{};seats[1]={capture.connection,{0,0,0},true,true,true};
    R::ReactionWindow window{capture.occurrence,capture.connection,capture.epoch,109,3,capture.source,capture.command,1,false};
    const R::ReactionCompletion claim{capture.occurrence,capture.connection,capture.epoch,capture.source,capture.command,1};
    check(R::ReactionLive(window,capture.epoch,111) && !R::ReactionLive(window,capture.epoch,112),"fresh UI cannot extend occurrence expiry");
    check(R::ConsumeReaction(window,claim,capture.epoch,110,seats) && !R::ConsumeReaction(window,claim,capture.epoch,110,seats),"completion consumes original window once");
    window.consumed=false;seats[1].living=false;
    check(!R::ConsumeReaction(window,claim,capture.epoch,110,seats) && window.consumed,"fatal hit after selection revokes completion");
    return failures?1:0;
}
