#include "ScriptReaction.hpp"
#include "kh2coop/Codec.hpp"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace R = kh2coop::inject::scriptmirror::rules;
namespace {
unsigned errors=0;
void check(bool ok,const char* label) {
    std::printf("%s: %s\n",ok?"PASS":"FAIL",label);
    if(!ok)++errors;
}
std::uint64_t integer(std::uint32_t bits) {
    return (static_cast<std::uint64_t>(kh2coop::SCRIPT_TAG_INT)<<32)|bits;
}
}
int main() {
    check(R::OwnerRoleBeginAllowed(0x80000001u,0x80000001u,0x80000001u),"an active same-resource Begin remains an idempotent state refresh");
    check(!R::OwnerRoleBeginAllowed(0x80000001u,0,0x80000001u),"heartbeat Begin cannot resurrect a role after lease/down/root revocation");
    check(!R::OwnerRoleBeginAllowed(0x80000000u,0,0x80000001u) &&
          R::OwnerRoleBeginAllowed(0x80000002u,0,0x80000001u),"only a newer admitted role can follow a retired resource");
    check(!R::OwnerRoleBeginAllowed(0,0,0),"missing role identity never grants refresh authority");
    const R::OwnerCaptureSignature capture{integer(253),0x544C464000000000ull};
    check(!R::CaptureSnapshotApplied({},capture,false,false,false),"a first Begin/capture catch-up installs native source motion");
    check(R::CaptureSnapshotApplied(capture,capture,true,true,false),"heartbeat state refresh does not restart installed source motion");
    check(R::CaptureSnapshotApplied(capture,capture,true,false,true),"consumed RC native playback survives capture-flag clearing and a repeated snapshot");
    auto nextCapture=capture;nextCapture.motion=integer(259);
    check(!R::CaptureSnapshotApplied(capture,nextCapture,true,true,true),"next authored capture input is not swallowed by a consumed command");
    nextCapture=capture;++nextCapture.blend;
    check(!R::CaptureSnapshotApplied(capture,nextCapture,true,true,true),"changed original blend bits remain an authored capture change");
    check(!R::CaptureSnapshotApplied(capture,capture,false,true,true),"a fresh role cannot inherit capture installation from an old occurrence");
    check(!R::CaptureSnapshotApplied(capture,capture,true,false,false),"a failed or replaced unconsumed capture must be installed again");
    using B=R::OwnerMotionBank;
    check(R::OwnerMotion(262,true,262,false)==B::Source && R::OwnerMotion(263,true,263,false)==B::Source,
          "ground captures retain source-bank identity even when native player tick reset their motion");
    check(R::OwnerMotion(0,true,262,false)==B::Own && R::OwnerMotion(0,true,263,false)==B::Own,
          "authored own idle after either ground capture never resolves boss idle0 on Roxas");
    check(R::OwnerMotion(259,true,253,false)==B::Refuse && R::OwnerMotion(263,true,262,false)==B::Refuse,
          "different post-capture ID cannot guess a borrowed or player bank");
    check(R::OwnerMotion(42,false,0,false)==B::Own,
          "a noncaptured role can retain proved ordinary own-bank motion");
    {   // VUH-2610: capture 255 -> 260 while the heartbeat still names 255.
        R::OwnerVisualHeartbeat beat{};
        beat.Capture(true,255,260);
        auto played=beat.Motion(true,255,true,260);
        check(played==260 && !R::OwnerVisualHeartbeat::TimeApplies(true,255,played) &&
              R::OwnerMotion(played,true,260,false)==B::Source,
              "a stale heartbeat of the replaced capture plays the newer capture and writes no time");
        beat.Heartbeat(255);
        check(beat.Motion(true,255,true,260)==260,"repeated stale heartbeats stay mapped until the heartbeat moves");
        beat.Heartbeat(260);
        played=beat.Motion(true,260,true,260);
        check(played==260 && R::OwnerVisualHeartbeat::TimeApplies(true,260,played),"the caught-up heartbeat plays with its time");
        beat.Heartbeat(255);
        played=beat.Motion(true,255,true,260);
        check(played==255 && R::OwnerMotion(played,true,260,false)==B::Refuse,
              "after the heartbeat caught up, a later 255 refuses under the unchanged rule");
        R::OwnerVisualHeartbeat other{};
        other.Capture(true,251,252);
        check(other.Motion(true,4,true,252)==4 && R::OwnerMotion(4,true,252,false)==B::Refuse,
              "a heartbeat leaving the capture for another motion is never remapped");
        other.Heartbeat(4);
        check(other.Motion(true,251,true,252)==251,"any non-replaced heartbeat ends the allowance");
        R::OwnerVisualHeartbeat fresh{};
        fresh.Capture(false,0,262);
        check(fresh.Motion(true,0,true,262)==0 && fresh.Motion(false,0,true,262)==262 &&
              R::OwnerMotion(fresh.Motion(true,262,true,262),true,262,true)==B::Refuse,
              "a first capture replaces nothing, a missing heartbeat plays the capture, consumed still refuses");
    }
    for(const auto motion:std::array<std::uint32_t,3>{0,262,999})
        check(R::OwnerMotion(motion,true,262,true)==B::Refuse,
              "consumed native RC playback cannot be rewound by either bank or an unknown motion");
    check(R::CaptureSnapshotApplied(capture,capture,true,false,false,true),
          "a repeated capture heartbeat preserves already-applied own idle after native capture teardown");
    check(!R::CaptureSnapshotApplied(capture,nextCapture,true,false,false,true),
          "own idle cannot swallow a changed authored capture signature");
    using E=R::OwnerCommandEntry;
    check(R::NativeCapturedCommandCaller(E::Captured,0x40DA4E) &&
          R::NativeCapturedCommandCaller(E::General,0x40DA55) &&
          R::NativeCapturedCommandCaller(E::Captured,0x40E581) &&
          R::NativeCapturedCommandCaller(E::General,0x40E588),
          "both native producers qualify their local and other-actor helper branches");
    check(!R::NativeCapturedCommandCaller(E::General,0x40DA4E) &&
          !R::NativeCapturedCommandCaller(E::Captured,0x40DA55) &&
          !R::NativeCapturedCommandCaller(E::Captured,0x40E588) &&
          !R::NativeCapturedCommandCaller(E::General,0x40E581),
          "a known return address paired with the wrong helper is not native provenance");
    check(!R::NativeCapturedCommandCaller(E::General,0x43007F) &&
          !R::NativeCapturedCommandCaller(E::Captured,0x40DA4D) &&
          !R::NativeCapturedCommandCaller(E::General,0),
          "BDX override, adjacent code and unavailable caller cannot borrow captured semantics");

    std::array<std::uint32_t,5> target{0x87654321,0,0,UINT32_MAX,0xDEADBEEF};
    check(R::OwnerCommandRootTarget(target),"native fifth copied dword is padding, not a wire target field");
    for(unsigned i=0;i<4;++i) {
        auto changed=target;
        changed[i]=i==0?0:i==3?0:1;
        check(!R::OwnerCommandRootTarget(changed),"missing source, part or attack reference cannot become a root target");
    }
    std::array<R::CapturedCommandRoleFacts,3> roles{};
    roles[0]={0x11111111,true,true,true,true,true};
    roles[1]={target[0],true,true,true,true,true};
    // Native controller belongs to seat0; the explicit registered capture source
    // belongs to seat1. Controller order and residual VM ownership cannot vote.
    check(R::SelectCapturedCommandRole(target[0],roles)==1,
          "registered native capture target selects remote owner despite canonical seat0 controller");
    check(R::SelectCapturedCommandRole(0x22222222,roles)<0 && R::SelectCapturedCommandRole(0,roles)<0,
          "unregistered and absent native handles are not owner admission");
    for(unsigned revoked=0;revoked<5;++revoked) {
        auto changed=roles;
        auto& owner=changed[1];
        if(revoked==0)owner.current=false;
        if(revoked==1)owner.captured=false;
        if(revoked==2)owner.sourceLinked=false;
        if(revoked==3)owner.captureFlags=false;
        if(revoked==4)owner.livingFresh=false;
        check(R::SelectCapturedCommandRole(target[0],changed)<0,
              "scope/lease, capture, source link, native flags and living freshness each revoke the target");
    }
    roles[2]=roles[1];
    check(R::SelectCapturedCommandRole(target[0],roles)<0,"two admitted captures sharing a source refuse ambiguous ownership");
    roles[2].current=false;
    check(R::SelectCapturedCommandRole(target[0],roles)==1,"retired duplicate cannot claim the remaining current capture");

    check(R::OwnerCommandContinues(297,2,100,297,2,115),"repeated same-mode command retains its occurrence through frame15");
    check(!R::OwnerCommandContinues(297,1,100,297,2,101) &&
          !R::OwnerCommandContinues(297,2,100,298,2,101) &&
          !R::OwnerCommandContinues(297,2,100,297,2,116) &&
          !R::OwnerCommandContinues(297,2,100,297,2,99),
          "mode, command, expired gap or backwards frame cannot reuse a command occurrence");

    check(R::OwnerCommandCaptureLive(1,false) && R::OwnerCommandCaptureLive(1,true) &&
          R::OwnerCommandCaptureLive(2,true) && !R::OwnerCommandCaptureLive(2,false) &&
          !R::OwnerCommandCaptureLive(0,true) && !R::OwnerCommandCaptureLive(3,true),
          "captured command loses completion authority with native capture; general mode retains its own semantics");

    kh2coop::ScriptCallEntry entry{};
    entry.netId=1;entry.objectId=307;entry.table=255;entry.index=7;entry.effect=0x80000001;
    entry.args={integer(1),integer(297),integer(1),integer(100)};
    check(kh2coop::validScriptCallEntry(entry),"general command mode is accepted by the actual wire validator");
    entry.args[0]=integer(2);
    check(kh2coop::validScriptCallEntry(entry),"captured-motion mode is accepted without changing native kind or shape");
    for(const auto bad:std::array<std::uint32_t,3>{0,3,UINT32_MAX}) {
        entry.args[0]=integer(bad);
        check(!kh2coop::validScriptCallEntry(entry) && !R::ValidOwnerCommandMode(bad),"wire and owner reject unsupported command modes");
    }
    entry.args[0]=(static_cast<std::uint64_t>(kh2coop::SCRIPT_TAG_FLOAT)<<32)|2;
    check(!kh2coop::validScriptCallEntry(entry),"native scalar tolerance cannot relax canonical command wire tags");
    entry.args[0]=integer(2);entry.args[3]=integer(256);
    check(!kh2coop::validScriptCallEntry(entry),"command mode extension preserves priority bounds");
    entry.args[3]=integer(100);entry.args[1]=integer(65536);
    check(!kh2coop::validScriptCallEntry(entry),"command mode extension preserves command width");

    // VUH-2617: the owner-release cleanup's existing decision as a pure rule, and the gate each release names in
    // its read-only `[script-owner] local release` line. Behavior is unchanged: Cleaned is the only native call.
    {
        R::ReleaseCleanupFacts captured{};
        captured.current=true;captured.field=true;captured.local=true;captured.threadRead=true;captured.threadZero=true;
        captured.captureOwned=true;captured.parentRead=true;captured.parentZero=true;
        const auto is=[](R::ReleaseCleanupFacts f,R::ReleaseCleanup want){return R::DecideReleaseCleanup(f)==want;};
        check(is(captured,R::ReleaseCleanup::Cleaned),"an intact capture by the released source: native detach/idle");
        auto f=captured;f.current=false;
        check(is(f,R::ReleaseCleanup::NotCurrent),"a stale actor, room or thread is never touched");
        f=captured;f.field=false;
        check(is(f,R::ReleaseCleanup::NotField),"inside an event or cutscene nothing runs (and nothing retries)");
        f=captured;f.local=false;
        check(is(f,R::ReleaseCleanup::NotLocalOrClone),"neither the canonical player nor the role's listed clone");
        f=captured;f.local=false;f.clone=true;
        check(is(f,R::ReleaseCleanup::Cleaned),"the role's clone is cleaned like the local actor");
        f=captured;f.threadRead=false;
        check(is(f,R::ReleaseCleanup::Guards),"an unreadable coroutine slot proves nothing");
        f=captured;f.threadZero=false;
        check(is(f,R::ReleaseCleanup::Guards),"another coroutine holds the actor: the capture is not ours");
        f=captured;f.threadZero=false;f.threadOwned=true;f.captureOwned=false;
        check(is(f,R::ReleaseCleanup::Cleaned),"the released role's own coroutine, run by its source");
        f=captured;f.captureOwned=false;f.attachment=true;
        check(is(f,R::ReleaseCleanup::Cleaned),"the released role's qualified attachment with no coroutine");
        f=captured;f.parentZero=false;
        check(is(f,R::ReleaseCleanup::ReplacementParent),"a parent that is not the role's attachment is a replacement's");
        f=captured;f.parentZero=false;f.attachment=true;
        check(is(f,R::ReleaseCleanup::Cleaned),"the role's own attachment as parent is ours to detach");
        f=captured;f.parentRead=false;
        check(is(f,R::ReleaseCleanup::ReplacementParent),"an unreadable parent never cleans");
        // bots05 attempt02 (run 20261022-082316), friend seat 1: role 654 captured the friend (motion 263), the
        // mirrored attack 1041 hit it while captured (frame 14381), 654 was released and 655 admitted and released at
        // 14383, and the friend stayed held from 14399. Which gate held is unobserved; these are the two hypotheses
        // the new release line will settle in attempt03, not facts:
        f=captured;f.field=false;
        check(is(f,R::ReleaseCleanup::NotField),"hypothesis 1: released inside the reaction's camera event");
        f=captured;f.captureOwned=false;
        check(is(f,R::ReleaseCleanup::Guards),"hypothesis 2: the hit left no +2B0 source, no coroutine and no attachment");
        check(std::string(R::ReleaseCleanupName(R::ReleaseCleanup::NotField))=="not-field" &&
              std::string(R::ReleaseCleanupName(R::ReleaseCleanup::Guards))=="guards" &&
              std::string(R::ReleaseCleanupName(R::ReleaseCleanup::Cleaned))=="cleaned","release line names");
    }
    // Quill r1 (0b376cf): the release line reads only proven roots. A counting reader records every address.
    {
        const std::uintptr_t actor=0x10000,source=0x50000,action=0x90000;
        std::vector<std::uintptr_t> reads;int decodes=0;
        const auto read=[&](std::uintptr_t at,void* out,std::size_t size){
            reads.push_back(at);std::memset(out,0,size);
            if(at==actor+0xC){const std::uint32_t handle=7;std::memcpy(out,&handle,sizeof(handle));}
            return true;};
        const auto decode=[&](std::uint32_t handle){++decodes;return handle==7?action:std::uintptr_t{0};};
        const auto touched=[&](std::uintptr_t lo,std::uintptr_t hi){
            for(auto at:reads)if(at>=lo && at<hi)return true;return false;};
        auto h=R::ReadLocalHold(false,actor,true,source,read,decode);
        check(reads.empty() && !decodes && !h.actorRead && !h.sourceRead,
              "a refused actor root (stale room, handle or game thread): zero reads, even with a proven source");
        reads.clear();decodes=0;
        h=R::ReadLocalHold(true,actor,false,source,read,decode);
        check(h.actorRead && !h.sourceRead && !touched(source,source+0x1000) && touched(actor,actor+0x1000) && decodes==1,
              "an unproven or freed source is never followed: the actor's fields only");
        reads.clear();decodes=0;
        h=R::ReadLocalHold(true,actor,true,source,read,decode);
        check(h.actorRead && h.sourceRead && touched(source+0x158,source+0x159) && touched(action,action+1),
              "both roots proven: the source's bank and the decoded action are read");
        reads.clear();decodes=0;
        h=R::ReadLocalHold(true,0,true,source,read,decode);
        check(reads.empty() && !decodes,"no actor: nothing read");
        reads.clear();decodes=0;
        const auto failing=[&](std::uintptr_t at,void*,std::size_t){reads.push_back(at);return false;};
        h=R::ReadLocalHold(true,actor,false,source,failing,decode);
        check(!h.actorRead && reads.size()==1 && !decodes,"an unreadable actor stops at the first field: no action decode");
    }
    // Quill r2 (bc443bc): the production boundary, proof then reader. The list lookup compares pointers; the driven
    // entry is the mirror's table; every field read is counted. After a cancel nothing is consulted at all.
    {
        const std::uintptr_t actor=0x10000,source=0x50000;
        const std::uint32_t actorHandle=0x11,sourceHandle=0x55;
        std::vector<std::uintptr_t> reads,listedAsked;int drivenAsked=0;
        std::uint32_t actorListed=actorHandle,sourceListed=sourceHandle;std::uintptr_t drivenNow=source;
        const auto listed=[&](std::uintptr_t at){listedAsked.push_back(at);
            return at==actor?actorListed:at==source?sourceListed:0u;};
        const auto driven=[&](){++drivenAsked;return drivenNow;};
        const auto read=[&](std::uintptr_t at,void* out,std::size_t size){reads.push_back(at);std::memset(out,0,size);return true;};
        const auto decode=[](std::uint32_t){return std::uintptr_t{0};};
        const auto line=[&](bool current,bool cancelled){
            reads.clear();listedAsked.clear();drivenAsked=0;
            const auto roots=R::ProveLocalHoldRoots(current,cancelled,actor,actorHandle,source,sourceHandle,listed,driven);
            return R::ReadLocalHold(roots.actor,actor,roots.source,source,read,decode);};
        const auto sourceTouched=[&](){for(auto at:reads)if(at>=source && at<source+0x1000)return true;return false;};
        auto h=line(true,false);
        check(h.actorRead && h.sourceRead,"release stage, both exact admitted handles listed: actor and source bank read");
        sourceListed=0;                                             // the cancel freed the source, the player remains
        h=line(true,true);
        check(reads.empty() && listedAsked.empty() && !drivenAsked && !h.actorRead,
              "post-cancel: metadata only; no list walk, driven lookup or field read for either root");
        h=line(true,false);
        check(h.actorRead && !h.sourceRead && !sourceTouched() && !drivenAsked,
              "a source no longer listed: no driven lookup, no source read");
        sourceListed=0x77;                                          // the slot reused by another object
        h=line(true,false);
        check(!h.sourceRead && !sourceTouched() && !drivenAsked,"a source whose list handle changed is never read");
        sourceListed=sourceHandle;drivenNow=0x60000;                // listed, but the mirror drives another actor
        h=line(true,false);
        check(!h.sourceRead && !sourceTouched(),"a listed source the mirror no longer drives is never read");
        drivenNow=source;actorListed=0x99;                          // the actor replaced at the same address
        h=line(true,false);
        check(reads.empty() && !h.actorRead && !drivenAsked && listedAsked.size()==1,
              "a replaced actor: one list lookup, no source lookup, zero reads");
        actorListed=actorHandle;
        h=line(false,false);
        check(reads.empty() && listedAsked.empty() && !drivenAsked,"a refused release root: nothing consulted");
    }
    // bots05 attempt03 (run 20261022-104559): every friend release line had actorRead=0. The list walk matches a node
    // through its predecessor's link, and the canonical player is the list head. The head is proven by the head global
    // and the admitted handle resolving to it through the handle table; neither reads the root.
    {
        const std::uintptr_t player=0x10000,thorn=0x50000;
        const std::uint32_t playerHandle=0x11,thornHandle=0x55;
        std::uintptr_t head=player,resolvesTo=player;int walks=0,resolves=0;
        const auto headNow=[&](){return head;};
        const auto resolve=[&](std::uint32_t h){++resolves;return h==playerHandle?resolvesTo:std::uintptr_t{0};};
        const auto walk=[&](std::uintptr_t at){++walks;return at==thorn?thornHandle:0u;};  // the head is never walked to
        check(R::ListedHandle(player,playerHandle,headNow,resolve,walk)==playerHandle && !walks && resolves==1,
              "the canonical player at the head: proven by the head global and its admitted handle, no walk");
        check(R::ListedHandle(thorn,thornHandle,headNow,resolve,walk)==thornHandle && walks==1,
              "a root below the head is still proven by the walk");
        resolvesTo=0x20000;walks=0;
        check(R::ListedHandle(player,playerHandle,headNow,resolve,walk)==0 && !walks,
              "a head whose admitted handle resolves elsewhere (replaced actor, reused slot): unproven, no walk");
        resolvesTo=player;
        check(R::ListedHandle(player,0,headNow,resolve,walk)==0,"no admitted handle: unproven");
        head=0x30000;walks=0;
        check(R::ListedHandle(player,playerHandle,headNow,resolve,walk)==0 && walks==1,
              "a player no longer at the head and not linked: unproven");
        head=player;
        std::vector<std::uintptr_t> reads;
        const auto read=[&](std::uintptr_t at,void* out,std::size_t size){reads.push_back(at);std::memset(out,0,size);return true;};
        const auto listed=[&](std::uintptr_t at){return R::ListedHandle(at,at==player?playerHandle:thornHandle,headNow,resolve,walk);};
        const auto line=[&](bool cancelled){
            reads.clear();
            const auto roots=R::ProveLocalHoldRoots(true,cancelled,player,playerHandle,thorn,thornHandle,listed,
                [&](){return thorn;});
            return R::ReadLocalHold(roots.actor,player,roots.source,thorn,read,[](std::uint32_t){return std::uintptr_t{0};});};
        auto h=line(false);
        check(h.actorRead && h.sourceRead,"attempt03's release stage, player at the head: its fields and the source bank are read");
        h=line(true);
        check(reads.empty() && !h.actorRead,"post-cancel stays metadata only");
    }
    return errors?1:0;
}
