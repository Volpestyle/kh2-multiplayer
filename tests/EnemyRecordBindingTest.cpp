#include "EnemyRecordBinding.hpp"
#include "OrdinaryEnemyBinding.hpp"
#ifndef RECORD_PURE_ONLY
#include "kh2coop/Codec.hpp"
#include "kh2coop/NativeRecordContent.hpp"
#include "kh2coop/SpawnEmissionLedger.hpp"
#include <fstream>
#endif
#include <iostream>
#include <string>
using namespace kh2coop;
namespace rb=kh2coop::inject::recordbinding;
int checks=0,failed=0;
void check(const std::string& name,bool ok) { ++checks; if (!ok) { ++failed; std::cerr<<"FAIL "<<name<<'\n'; } }
struct Fixture {
    rb::Scope scope{2,3,4,3,100,77,{18,6,0,0,2,32767}};
    std::vector<rb::Local> local;
    std::vector<rb::Host> host;
    rb::Lease lease;
    explicit Fixture(std::uint32_t oid=317) {
        if (oid==76) scope.location={7,0,0,0,2,32767};
        unsigned n=0;
        for (const auto& a:rb::Allowed) if (a.objectId==oid) {
            const auto id=static_cast<int>(++n);
            local.push_back({{100+n,200+n,300+n,400,500+n,oid},a.key,id,true});
            host.push_back({a.key,id+10,oid,false});
        }
    }
    rb::Result run(bool complete=true) { return lease.Resolve(scope,local,host,complete); }
};

// VUH-2584: a death announced while the census was held (the mission-complete event closed the
// gameplay gate) still applies to the same body in the same load; nothing else is loosened.
void OrdinaryHeldDeath() {
    namespace ob=kh2coop::inject::ordinarybinding;
    const ob::Identity roots{0x7FF62746DCA0ULL,0x7FF62793A518ULL,0x7FF6279330A8ULL,0x7FF6279F8130ULL,0x7FF6278FFC54ULL,318};
    const std::array<float,3> point{1,2,3};
    auto resolve=[&](ob::Local l,ob::Host h,ob::Scope scope) {
        const std::array locals{l}; const std::array hosts{h};
        return ob::Resolve(locals,hosts,scope).front();
    };
    ob::Local local{roots,point,{},true,true};
    ob::Host host{1,318,point,false};
    const auto live=resolve(local,host,{2,2,3900,3});
    check("held: living body bound before the hold",live.id==1 && live.next.bound==1);
    auto heldPrior=live.next; heldPrior.held=heldPrior.bound; heldPrior.bound=0;  // what a held census leaves
    host.dead=true;
    auto l=local; l.prior=heldPrior;
    const auto death=resolve(l,host,{2,2,4156,3});
    check("held: the host death applies to the same body when the gate reopens",death.id==1 && death.next.held==0);
    for (int mode=0;mode<8;++mode) {
        auto m=l; auto h=host; ob::Scope now{2,2,4156,3};
        switch(mode) {
        case 0:now.load=4;break;             // a new load in between
        case 1:now.load=0;m.prior.scope.load=0;break; // unknown load
        case 2:++m.identity.actor;break;
        case 3:++m.identity.record;break;
        case 4:m.prior.living=false;break;   // was not living when held
        case 5:m.prior.held=2;break;         // held a different ID
        case 6:++now.epoch;break;
        case 7:now.frame=3900;break;         // no time passed
        }
        const auto d=resolve(m,h,now);
        check("held: refused variant "+std::to_string(mode),d.id==0);
    }
}

// VUH-2550: the Back Alley ambush's late 309 spawns at a different point on each seat. The only fresh
// unmatched body of its kind binds to the only unmatched living host entry of its kind; nothing else.
void OrdinaryUniqueKind() {
    namespace ob=kh2coop::inject::ordinarybinding;
    const ob::Identity a{0x1000,0x2000,0x3000,0x4000,0x5000,309}, b{0x1100,0x2100,0x3100,0x4000,0x5040,309},
        dusk{0x1200,0x2200,0x3200,0x4000,0x5080,318};
    const ob::Local fresh{a,{104,-26,346},{},true,true,false,0,0,true};
    const ob::Host host{3,309,{-29,0,-134},false};
    const ob::Scope now{2,2,500,6};
    auto one=[&](std::vector<ob::Local> ls,std::vector<ob::Host> hs){return ob::Resolve(ls,hs,now);};
    auto d=one({fresh},{host});
    check("unique: the late 309 binds by kind",d[0].id==3 && d[0].reason==ob::Reason::UniqueKind);
    {   // The next frames keep it; its death still applies through the continuous binding.
        auto next=fresh; next.prior=d[0].next; next.unharmed=false;  // it has since been hit
        const auto d2=ob::Resolve(std::array{next},std::array{host},ob::Scope{2,2,501,6});
        check("unique: the kind binding continues on the next frame",d2[0].id==3 && d2[0].reason==ob::Reason::Live);
        auto later=next; later.prior=d2[0].next;
        auto deadHost=host; deadHost.dead=true;
        const auto d3=ob::Resolve(std::array{later},std::array{deadHost},ob::Scope{2,2,502,6});
        check("unique: the host's death applies to the kind-bound body",d3[0].id==3 && d3[0].reason==ob::Reason::BoundDeath);
        auto gap=later; gap.prior=d2[0].next;
        const auto d4=ob::Resolve(std::array{gap},std::array{host},ob::Scope{2,2,505,6});
        check("unique: a missed frame (no hold) does not keep it",d4[0].id==0);
    }
    // A point-matched 318 alongside changes nothing.
    const ob::Local duskLocal{dusk,{10,0,10},{},true,true,false,0,0,true};
    d=one({fresh,duskLocal},{host,ob::Host{1,318,{10,0,10},false}});
    check("unique: other kinds keep point binding",d[0].id==3 && d[1].id==1 && d[1].reason==ob::Reason::Live);
    auto second=fresh; second.identity=b; second.point={500,0,500};
    d=one({fresh,second},{host});
    check("unique: two unmatched local 309s stay unbound",d[0].id==0 && d[1].id==0);
    d=one({fresh},{host,ob::Host{4,309,{900,0,900},false}});
    check("unique: two unmatched host 309s stay unbound",d[0].id==0);
    auto hurt=fresh; hurt.unharmed=false;
    check("unique: a damaged body is not fresh",one({hurt},{host})[0].id==0);
    auto seen=fresh; seen.prior.highWater=2;
    check("unique: a body that carried another ID is not fresh",one({seen},{host})[0].id==0);
    auto claimed=host; claimed.claimed=true;
    check("unique: a host ID already carried here is not offered",one({fresh},{claimed})[0].id==0);
    auto dead=host; dead.dead=true;
    check("unique: a dead host entry is not offered",one({fresh},{dead})[0].id==0);
    auto ticket=fresh; ticket.birthId=7;
    check("unique: a birth-ticket body keeps its own rule",one({ticket},{host})[0].reason!=ob::Reason::UniqueKind);
    // A point match elsewhere for this host entry wins over the kind rule.
    auto near=second; near.point=host.point;
    d=one({fresh,near},{host});
    check("unique: a point match takes the host entry; the far body stays unbound",d[1].id==3 && d[0].id==0);
}

// VUH-2466: the Day 5 Struggle Dusks (btl189), decoded from the friend's log of run 20261021-015242
// (before VUH-2550 landed). The host announced three 318s, the last at (0,-90,-200). The friend's first
// two point-matched (newest-live-point); its third came up at (-20,-90,-212), 23 units off, and stayed
// unbound (no-point-match) with unscaled HP: 37 on the friend against the host's 60. The host's first
// two points are the friend's, which the log's point matches put within 8 units.
void OrdinaryStruggle189() {
    namespace ob=kh2coop::inject::ordinarybinding;
    const ob::Identity d0{0x7FF627443DA0,0x7FF62793A518,0x7FF6279330A8,0x7FF6279F8130,0x7FF6278CCDB4,318},
        d1{0x7FF62743D1A0,0x7FF62793A518,0x7FF627932E30,0x7FF6279F8130,0x7FF6278CCD74,318},
        d2{0x7FF6274365A0,0x7FF62793A518,0x7FF627932BB8,0x7FF6279F8130,0x7FF6278CCD34,318};
    const std::vector<ob::Local> locals{{d0,{-200,-90,200},{},true,true,false,0,0,true},
                                        {d1,{200,-90,200},{},true,true,false,0,0,true},
                                        {d2,{-20,-90,-212},{},true,true,false,0,0,true}};
    const std::vector<ob::Host> hosts{{1,318,{-200,-90,200}},{2,318,{200,-90,200}},{3,318,{0,-90,-200}}};
    const auto d=ob::Resolve(locals,hosts,ob::Scope{2,2,584,2});
    check("btl189: the first two Dusks bind by point as logged",
          d[0].id==1 && d[0].reason==ob::Reason::Live && d[1].id==2 && d[1].reason==ob::Reason::Live);
    check("btl189: the third Dusk, 23 units off, binds to the host's third by kind",
          d[2].id==3 && d[2].reason==ob::Reason::UniqueKind);
    // A fourth announced 318 makes the host's unmatched entry ambiguous: nothing is guessed.
    auto more=hosts; more.push_back({4,318,{300,-90,-300}});
    check("btl189: two unmatched host Dusks leave it unbound",ob::Resolve(locals,more,ob::Scope{2,2,584,2})[2].id==0);
}

// Copied-fact production resolver controls; no native memory, writes or hooks.
void OrdinaryTerminalControls() {
    namespace ob=kh2coop::inject::ordinarybinding;
    const ob::Identity roots{0x7FF656D27550ULL,0x7FF6571C9F18ULL,
        0x7FF6571C2E30ULL,0x7FF657288340ULL,0x7FF657186394ULL,302};
    const std::array<float,3> point{1,2,3};
    auto resolve=[&](ob::Local l,ob::Host h,ob::Scope scope) {
        const std::array locals{l}; const std::array hosts{h};
        return ob::Resolve(locals,hosts,scope).front();
    };
    ob::Local local{roots,point,{},true,true};
    ob::Host host{1,302,point,false};
    auto live=resolve(local,host,{2,2,983});
    check("ordinary prior living binding",live.id==1);
    local.prior=live.next; host.dead=true;
    auto death=resolve(local,host,{2,2,984});
    check("856 retained frame984 established death while native living",death.id==1);
    // Native DeathApply at 984 reads HP 2->0. Frame985 is not logged: this
    // constructs the required continuous intermediate, NOT a recovered sample.
    local.prior=death.next; local.living=false;
    auto dead=resolve(local,host,{2,2,985});
    check("ordinary living to native dead authenticated",dead.id==1);
    local.prior=dead.next;
    auto dead986=resolve(local,host,{2,2,986});
    check("856 retained frame986 roots survive continuous dead observation",dead986.id==1);
    local.prior=dead986.next;
    auto dead987=resolve(local,host,{2,2,987});
    check("ordinary repeated dead remains authenticated",dead987.id==1);
    auto terminal=local; terminal.prior=dead987.next;
    for (int mode=0;mode<12;++mode) {
        auto l=terminal; auto h=host; ob::Scope now{2,2,988};
        switch(mode) {
        case 0:l.prior={};break; // no living authentication
        case 1:++now.frame;break; // actual missing-frame variant still refuses
        case 2:++l.identity.actor;break;
        case 3:++l.identity.objentry;break;
        case 4:++l.identity.status;break;
        case 5:++l.identity.controller;break;
        case 6:++l.identity.record;break;
        case 7:++h.id;break; // netId reuse/replacement
        case 8:++now.epoch;break;
        case 9:++now.generation;break;
        case 10:l.living=true;break; // native resurrection despite dead host
        case 11:l.living=true;h.dead=false;break; // both advertise resurrection
        }
        auto refused=resolve(l,h,now);
        check("ordinary terminal ambiguity refuses "+std::to_string(mode),refused.id==0);
        l.prior=refused.next; ++now.frame;
        check("ordinary refused terminal cannot recover "+std::to_string(mode),
              resolve(l,h,now).id==0);
    }
    auto l=terminal; l.living=true; l.prior={};
    check("ordinary fresh living remains allowed",resolve(l,{2,302,point,false},{2,2,988}).id==2);
    l=terminal;
    check("ordinary dead cannot turn into host live binding",
          resolve(l,{1,302,point,false},{2,2,988}).id==0);
}

void OrdinaryBirthControls() {
    namespace ob=kh2coop::inject::ordinarybinding;
    ob::Local fresh;fresh.identity={11,12,13,14,15,2405};fresh.available=fresh.living=true;fresh.birthId=4;
    const ob::Host dead{4,2405,{900,800,700},true};
    auto resolve=[&](const ob::Local& l,std::vector<ob::Host> hs,ob::Scope scope=ob::Scope{2,3,101}) {
        return ob::Resolve(std::array{l},hs,scope)[0];
    };
    check("exact receipt birth can adopt terminal occurrence without a positional death exemption",resolve(fresh,{dead}).id==4);
    auto noTicket=fresh;noTicket.birthId=0;noTicket.point=dead.point;
    check("same position without birth proof still refuses unestablished death",resolve(noTicket,{dead}).id==0);
    check("receipt before manifest holds exact identity",resolve(fresh,{}).id==0);
    check("later terminal manifest admits the retained exact ticket",resolve(fresh,{dead}).id==4);
    auto mismatch=dead;mismatch.objectId=2407;
    check("ticket cannot name another object class",resolve(fresh,{mismatch}).id==0);
    auto removed=dead;removed.removed=true;
    check("retired host identity never grants ticket death",resolve(fresh,{removed}).id==0);
    auto certified=fresh;certified.certificateRequired=true;certified.certifiedId=4;
    check("birth ticket cannot bypass certified lifecycle authority",resolve(certified,{dead}).id==0);
    auto missing=fresh;missing.identity.controller=0;
    check("missing native controller cannot use birth ticket",resolve(missing,{dead}).id==0);
    missing=fresh;missing.identity.record=0;
    check("missing native record cannot use birth ticket",resolve(missing,{dead}).id==0);
    auto retired=fresh;retired.prior.removed=true;
    check("removed local body cannot reuse birth ticket",resolve(retired,{dead}).id==0);
    auto used=fresh;used.prior.highWater=4;
    check("an earlier adoption prevents fresh terminal ticket exception",resolve(used,{dead}).id==0);
    auto live=dead;live.dead=false;
    auto adopted=resolve(fresh,{live});used=fresh;used.prior=adopted.next;used.birthId=0;
    used.point=dead.point; // after adoption all ordinary point/continuity checks still apply
    check("live-first birth adoption keeps ordinary contiguous death",resolve(used,{dead},{2,3,102}).id==4);
    check("spent live birth ticket cannot repair a later continuity gap",resolve(used,{dead},{2,3,103}).id==0);
    auto refill=live;refill.id=7;refill.point=dead.point;
    check("exact old terminal ticket never selects newer same-point refill",resolve(fresh,{dead,refill}).id==4);
    auto next=fresh;next.birthId=7;
    check("new refill ticket selects its own distinct living occurrence",resolve(next,{dead,refill}).id==7);
    next.birthId=8;
    check("unknown ticket cannot fall back to a living same-point occurrence",resolve(next,{dead,refill}).id==0);
    auto other=fresh;other.identity.actor=31;other.identity.status=33;other.birthId=0;other.point=dead.point;
    const auto conflict=ob::Resolve(std::array{fresh,other},std::array{dead},ob::Scope{2,3,101});
    check("whole census duplicate claimant blocks ticket and positional claimant",!conflict[0].id && !conflict[1].id && conflict[0].conflict && conflict[1].conflict);
}

// VUH-2605: per-frame authority for an adopted script child (adds-owner24 ScriptActorBoundCurrent).
void ScriptActorBoundCurrentControls() {
    namespace ob=kh2coop::inject::ordinarybinding;
    const ob::ScriptActorScope full{2,3,4,5,6,7,8,{2,34,0,157,157,157}};
    const ob::Scope now{2,3,101,4,full};
    ob::Local current;current.identity={101,102,103,0,0,317};current.living=true;
    current.scriptActor={41,51,17,9,current.identity,full};current.scriptActorCurrent=true;
    ob::State state;state.bound=17;state.highWater=17;state.identity=current.identity;state.scope=now;state.living=true;
    state.scriptActor=current.scriptActor;
    check("adopted current child holds live authority",ob::ScriptActorBoundCurrent(state,current,now,false,false));
    auto changed=[&](auto edit){auto s=state;auto c=current;auto n=now;edit(s,c,n);return ob::ScriptActorBoundCurrent(s,c,n,false,false);};
    check("authority is for this owner frame only",!changed([](auto&,auto&,auto& n){++n.frame;}));
    check("a changed full scope refuses",!changed([](auto&,auto&,auto& n){n.scriptActor.writerDelivery=9;}));
    check("changed native roots refuse",!changed([](auto&,auto& c,auto&){c.identity.status=999;}));
    check("a stale witness is not current",!changed([](auto&,auto& c,auto&){c.scriptActorCurrent=false;}));
    check("a different witness refuses",!changed([](auto&,auto& c,auto&){c.scriptActor.nativeReturnSerial=52;}));
    check("a removed binding refuses",!changed([](auto& s,auto&,auto&){s.removed=true;}));
    check("binding must be its own highest ID",!changed([](auto& s,auto&,auto&){s.highWater=18;}));
    check("the bound ID must be the witnessed child",!changed([](auto& s,auto&,auto&){s.bound=16;s.highWater=16;}));
    check("live authority needs a living body",!changed([](auto&,auto& c,auto&){c.living=false;}));
    check("live authority refuses a dead host",!ob::ScriptActorBoundCurrent(state,current,now,false,true));
    auto dead=current;dead.living=false;
    check("terminal authority follows a dead host and a dead body",ob::ScriptActorBoundCurrent(state,dead,now,true,true));
    check("terminal authority refuses a living host",!ob::ScriptActorBoundCurrent(state,dead,now,true,false));
}

void OrdinaryScriptActorControls() {
    namespace ob=kh2coop::inject::ordinarybinding;
    const ob::ScriptActorScope full{2,3,4,5,6,7,8,{2,34,0,157,157,157}};
    const ob::Scope firstScope{2,3,101,0,full};
    ob::Local fresh;fresh.identity={101,102,103,0,0,317};fresh.available=fresh.living=true;fresh.birthId=17;
    fresh.scriptActor={41,51,17,9,fresh.identity,full};fresh.scriptActorCurrent=true;
    const ob::Host live{17,317,{900,800,700},false};
    auto resolve=[&](const ob::Local& l,std::vector<ob::Host> hs,ob::Scope now) {
        return ob::Resolve(std::array{l},hs,now)[0];
    };
    auto admitted=resolve(fresh,{live},firstScope);
    check("typed native zero/zero script return admits exact child",admitted.id==17);
    check("adopted script witness retained",admitted.next.scriptActor==fresh.scriptActor && admitted.next.highWater==17);
    auto samePoint=live;samePoint.point=fresh.point;
    for(int mode=0;mode<10;++mode) {
        auto l=fresh;
        switch(mode) {
        case 0:l.scriptActor={};break; // bare birthId/object317 is not provenance
        case 1:l.scriptActor={};l.birthId=0;break; // not even a point match
        case 2:l.scriptActorCurrent=false;break;
        case 3:l.identity.controller=104;l.scriptActor.child=l.identity;break;
        case 4:l.identity.record=105;l.scriptActor.child=l.identity;break;
        case 5:l.identity.controller=104;l.identity.record=105;l.scriptActor.child=l.identity;break;
        case 6:l.certificateRequired=true;l.certifiedId=17;break;
        case 7:l.birthId=0;break; // first birth requires its exact one-use selection
        case 8:l.living=false;break; // ConstructorPending/unknown dead first bodies never bind
        case 9:l.identity.status=0;l.scriptActor.child=l.identity;break;
        }
        check("untyped/mixed/uncurrent script roots refuse "+std::to_string(mode),resolve(l,{samePoint},firstScope).id==0);
    }
    for(int mode=0;mode<10;++mode) {
        auto l=fresh;
        switch(mode) {
        case 0:l.scriptActor.occurrence=0;break;
        case 1:l.scriptActor.nativeReturnSerial=0;break;
        case 2:l.scriptActor.childId=18;break;
        case 3:l.scriptActor.parentId=17;break;
        case 4:l.scriptActor.parentId=0;break;
        case 5:l.scriptActor.parentId=65536;break;
        case 6:++l.scriptActor.child.status;break;
        case 7:l.scriptActor.scope.hostConnection=0;break;
        case 8:l.scriptActor.scope.hostDelivery=0;break;
        case 9:l.scriptActor.scope.writerDelivery=0;break;
        }
        check("malformed exact script witness refuses "+std::to_string(mode),resolve(l,{samePoint},firstScope).id==0);
    }
    auto partial=fresh;partial.identity.controller=104;partial.identity.record=105;
    partial.scriptActor={};partial.scriptActor.occurrence=1;
    check("partial typed stamp never falls back to indexed birth",resolve(partial,{live},firstScope).id==0);
    partial.birthId=0;
    check("partial typed stamp never falls back to points",resolve(partial,{samePoint},firstScope).id==0);
    auto missingScope=firstScope;missingScope.scriptActor={};
    check("legacy three-field scope cannot authorize script roots",resolve(fresh,{live},missingScope).id==0);
    auto wrongClass=live;wrongClass.objectId=318;
    check("script exact child cannot name another object class",resolve(fresh,{wrongClass},firstScope).id==0);
    auto removed=live;removed.removed=true;
    check("removed child manifest cannot authorize script birth",resolve(fresh,{removed},firstScope).id==0);
    auto dead=live;dead.dead=true;
    check("exact already-reserved live script return admits its dead host ID",resolve(fresh,{dead},firstScope).id==17);
    auto claimed=live;claimed.claimed=true;
    check("fresh script witness cannot reuse another body's historical live ID",resolve(fresh,{claimed},firstScope).id==0);
    claimed.dead=true;
    check("fresh script witness cannot recreate another body's terminal ID",resolve(fresh,{claimed},firstScope).id==0);
    check("missing child manifest waits",resolve(fresh,{},firstScope).id==0);
    auto newer=samePoint;newer.id=18;
    check("script child selection never chooses newer same-point ID",resolve(fresh,{live,newer},firstScope).id==17);
    auto generic=fresh;generic.identity.objectId=2405;generic.scriptActor.child=generic.identity;
    auto genericHost=live;genericHost.objectId=2405;
    check("typed script resolver contains no Thorn family rule",resolve(generic,{genericHost},firstScope).id==17);

    auto bound=fresh;bound.prior=admitted.next;bound.birthId=0;bound.point={-900,-800,-700};
    auto nextScope=firstScope;++nextScope.frame;
    auto following=resolve(bound,{live,newer},nextScope);
    check("spent birth retains exact child without point matching",following.id==17 && following.next.scriptActor==fresh.scriptActor);
    claimed.dead=false;
    check("continuing adopted script keeps its naturally claimed ID",resolve(bound,{claimed},nextScope).id==17);
    auto handed=resolve(bound,{dead},nextScope);
    check("zero-root script identity has continuous death handoff",handed.id==17 && handed.reason==ob::Reason::BoundDeath);
    auto terminal=bound;terminal.prior=handed.next;terminal.living=false;
    auto terminalScope=nextScope;++terminalScope.frame;
    auto settled=resolve(terminal,{dead},terminalScope);
    check("native script death retains terminal witness",settled.id==17 && settled.next.terminal && settled.next.scriptActor==fresh.scriptActor);
    terminal.prior=settled.next;++terminalScope.frame;
    check("terminal script identity continues while native dead",resolve(terminal,{dead},terminalScope).id==17);
    auto resurrected=terminal;resurrected.living=true;
    check("terminal script body cannot resurrect against host death",resolve(resurrected,{dead},terminalScope).id==0);
    check("terminal script body cannot resurrect against live host",resolve(resurrected,{live},terminalScope).id==0);

    for(int mode=0;mode<19;++mode) {
        auto l=bound;auto now=nextScope;
        switch(mode) {
        case 0:now.frame=firstScope.frame;break; // repeated same owner frame is not continuity
        case 1:++now.frame;break;
        case 2:++l.scriptActor.nativeReturnSerial;break; // same sampled roots, different actual native return
        case 3:++l.scriptActor.occurrence;break;
        case 4:++l.scriptActor.parentId;break; // historical parent identity is immutable
        case 5:++l.identity.status;l.scriptActor.child=l.identity;break;
        case 6:++l.identity.actor;l.scriptActor.child=l.identity;break;
        case 7:++l.identity.objentry;l.scriptActor.child=l.identity;break;
        case 8:++now.scriptActor.hostConnection;l.scriptActor.scope=now.scriptActor;break;
        case 9:++now.scriptActor.hostDelivery;l.scriptActor.scope=now.scriptActor;break;
        case 10:++now.scriptActor.writerDelivery;l.scriptActor.scope=now.scriptActor;break;
        case 11:++now.scriptActor.load;l.scriptActor.scope=now.scriptActor;break;
        case 12:++now.scriptActor.transition;l.scriptActor.scope=now.scriptActor;break;
        case 13:++now.epoch;++now.scriptActor.epoch;l.scriptActor.scope=now.scriptActor;break;
        case 14:++now.generation;++now.scriptActor.generation;l.scriptActor.scope=now.scriptActor;break;
        case 15:++now.scriptActor.location[0];l.scriptActor.scope=now.scriptActor;break;
        case 16:++now.scriptActor.location[3];l.scriptActor.scope=now.scriptActor;break;
        case 17:l.scriptActorCurrent=false;break;
        case 18:l.scriptActor={};break;
        }
        auto held=resolve(l,{live},now);
        check("adopted script discontinuity holds "+std::to_string(mode),held.id==0 && held.next.highWater==17 && held.next.scriptActor==fresh.scriptActor);
        if(mode<17)check("script discontinuity has exact reason "+std::to_string(mode),held.reason==ob::Reason::ScriptDiscontinuity);
        l.prior=held.next;++now.frame;l.scriptActorCurrent=true;
        check("held adopted script cannot reestablish from a fresh current bool "+std::to_string(mode),resolve(l,{live},now).id==0);
    }
    auto relabel=bound;relabel.scriptActor.childId=18;relabel.birthId=18;
    auto held=resolve(relabel,{newer},nextScope);
    check("script highWater refuses a new child ID on the same body",held.id==0 && held.reason==ob::Reason::Relabel && held.next.highWater==17);
    auto retired=bound;retired.prior.removed=true;
    check("removed local script body cannot reuse its witness",resolve(retired,{live},nextScope).id==0);
    auto stripped=bound;stripped.scriptActor={};stripped.identity.controller=104;stripped.identity.record=105;
    check("retained script stamp cannot be stripped to recover indexed authority",resolve(stripped,{samePoint},nextScope).id==0);
    for(std::size_t field=0;field<full.location.size();++field) {
        auto l=bound;auto now=nextScope;++now.scriptActor.location[field];l.scriptActor.scope=now.scriptActor;
        check("every NOW scope field remains immutable after script adoption "+std::to_string(field),resolve(l,{live},now).id==0);
    }

    auto duplicate=fresh;duplicate.identity.actor=201;duplicate.identity.status=203;
    duplicate.scriptActor.child=duplicate.identity;duplicate.scriptActor.nativeReturnSerial=52;
    for(bool available:{true,false}) {
        duplicate.available=available;
        const auto result=ob::Resolve(std::array{bound,duplicate},std::array{live},nextScope);
        check("duplicate living script bodies conflict even when one unavailable "+std::to_string(available),
              !result[0].id && !result[1].id && result[0].conflict && result[1].conflict && result[0].next.scriptActor==fresh.scriptActor);
        const auto reversed=ob::Resolve(std::array{duplicate,bound},std::array{live},nextScope);
        check("typed script conflict is whole-census order independent "+std::to_string(available),
              !reversed[0].id && !reversed[1].id && reversed[0].conflict && reversed[1].conflict);
    }
    ob::Local indexed;indexed.identity={301,302,303,304,305,2405};indexed.available=indexed.living=true;indexed.birthId=17;
    const ob::Host indexedDead{17,2405,{9,8,7},true};
    check("indexed birth still accepts nonzero native roots",resolve(indexed,{indexedDead},{2,3,101}).id==17);
    indexed.identity.record=0;
    check("indexed receipt still refuses a missing native record",resolve(indexed,{indexedDead},{2,3,101}).id==0);
    indexed.identity.record=305;indexed.identity.controller=0;
    check("indexed receipt still refuses a missing native controller",resolve(indexed,{indexedDead},{2,3,101}).id==0);
}

void OrdinaryLifecycleControls() {
    namespace ob=kh2coop::inject::ordinarybinding;
    ob::Local l{{100,200,300,400,500,302},{1,2,3},{},true,true};
    auto go=[&](ob::Local local,std::vector<ob::Host> host){return ob::Resolve(std::array{local},host,{2,2,737})[0];};
    auto original=go(l,{{4,302,{1,2,3},false}});check("lifecycle original bound",original.id==4);l.prior=original.next;
    auto relabel=go(l,{{4,302,{1,2,3},false},{6,302,{1,2,3},false}});
    check("ca007 line796 live 4->6 refuses",relabel.id==0 && relabel.reason==ob::Reason::Relabel);
    l.prior.removed=true;l.certificateRequired=true;l.certifiedId=6;
    check("retired live body cannot become replacement",go(l,{{6,302,{1,2,3},false}}).id==0);
    l.prior={};l.identity.actor=101;
    check("fresh body exact certificate allows replacement",go(l,{{6,302,{9,9,9},false}}).id==6);
    l.certifiedId=0;check("missing birth certificate refuses",go(l,{{6,302,{1,2,3},false}}).id==0);
}
// VUH-2497 (run 20261019-160928): the host killed Basement Hall's first Dusks while the friend's census
// was held in the entry event; its first sample of each body already named a dead ID.
void OrdinaryUnseenDeathControls() {
    namespace ob=kh2coop::inject::ordinarybinding;
    const std::array<float,3> point{-400,0,-550};
    ob::Local fresh{{0x7FF627459AA0ULL,0x7FF62793A518ULL,0x7FF627933320ULL,0x7FF6279F8188ULL,0x7FF6278FC420ULL,318},
                    point,{},true,true};
    fresh.unharmed=true;
    const ob::Host dead{1,318,point,true};
    auto go=[](const ob::Local& l,std::vector<ob::Host> hs,ob::Scope now=ob::Scope{2,3,1554}) {
        return ob::Resolve(std::array{l},hs,now)[0];
    };
    const auto first=go(fresh,{dead});
    check("unseen fresh body takes its dead ID",first.id==1 && first.reason==ob::Reason::UnseenDeath);
    auto next=fresh;next.prior=first.next;
    const auto held=go(next,{dead},{2,3,1555});
    check("the next frame continues as an established death",held.id==1 && held.reason==ob::Reason::BoundDeath);
    check("a gap after first sighting still refuses",go(next,{dead},{2,3,1556}).id==0);
    for(int mode=0;mode<11;++mode) {
        auto l=fresh;auto h=dead;
        switch(mode) {
        case 0:l.unharmed=false;break;              // hurt here: something already saw it
        case 1:l.living=false;break;
        case 2:l.prior.scope={2,3,1500};break;      // sampled before, even unbound
        case 3:l.prior.highWater=1;break;
        case 4:h.claimed=true;break;                // a local refill after a bound death
        case 5:h.left=true;break;                   // the host's body left alive
        case 6:l.identity.controller=0;break;
        case 7:l.identity.record=0;break;
        case 8:l.birthId=2;break;                   // a ticket decides, not this rule
        case 9:l.certificateRequired=true;l.certifiedId=1;break;
        case 10:l.prior.terminal=true;break;
        }
        check("unseen death refused "+std::to_string(mode),go(l,{h}).id==0);
    }
    auto live=dead;live.id=5;live.dead=false;
    check("a newer live ID at the point wins",go(fresh,{dead,live}).id==5);
    auto other=fresh;other.identity.actor+=0x100;other.identity.status+=0x100;
    const auto pair=ob::Resolve(std::array{fresh,other},std::array{dead},ob::Scope{2,3,1554});
    check("two unseen claimants of one dead ID both hold",!pair[0].id && !pair[1].id && pair[0].conflict);
}

void OrdinaryRefillControls() {
    namespace ob=kh2coop::inject::ordinarybinding;
    ob::Local local{{100,200,300,400,500,2403},{1,2,3},{},true,true};
    std::vector<ob::Host> hosts{{2,2403,{1,2,3},false}};
    local.prior=ob::Resolve(std::array{local},hosts,{2,2,100})[0].next;
    hosts[0].dead=true;hosts[0].left=true;
    hosts.push_back({7,2403,{1,2,3},false});
    const auto left=ob::Resolve(std::array{local},hosts,{2,2,101})[0];
    check("batched leave/refill keeps established old ID",left.id==2 && left.reason==ob::Reason::BoundDeath);
    auto settling=local;settling.prior=left.next;
    check("native leave keeps binding until body leaves",ob::Resolve(std::array{settling},hosts,{2,2,102})[0].id==2);
    auto fresh=local;fresh.prior={};fresh.identity.actor=101;
    const auto pair=ob::Resolve(std::array{local,fresh},hosts,{2,2,101});
    check("fresh native refill binds new ID without relabelling old body",pair[0].id==2 && pair[1].id==7);
    for(int mode=0;mode<13;++mode) {
        auto l=local;auto h=hosts;ob::Scope now{2,2,101};
        switch(mode) {
        case 0:l.prior.bound=0;break;
        case 1:++now.frame;break;
        case 2:++now.generation;break;
        case 3:++now.epoch;break;
        case 4:++l.identity.actor;break;
        case 5:++l.identity.objentry;break;
        case 6:++l.identity.status;break;
        case 7:++l.identity.controller;break;
        case 8:++l.identity.record;break;
        case 9:h[0].left=false;break;
        case 10:h[0].dead=false;break;
        case 11:h[0].removed=true;break;
        case 12:l.certificateRequired=true;l.certifiedId=7;break;
        }
        check("refill ambiguity never grants old or new ID "+std::to_string(mode),
              ob::Resolve(std::array{l},h,now)[0].id==0);
    }
}

#ifndef RECORD_PURE_ONLY
// Real codec and receipt ledger, with every split of the reliable sender order.
// Native emission is represented by a fresh census row only after Permit succeeds.
void SplitRefillDeliveryControls() {
    namespace ob=kh2coop::inject::ordinarybinding;
    const std::array<float,3> point{1,2,3};
    EnemyDeath leave;leave.epoch=2;leave.netId=2;leave.left=1;
    EnemyManifest manifest;manifest.epoch=2;manifest.replace=false;
    manifest.entries.push_back({7,26,6,2403,{1,2,3},{}});
    SpawnEmission emission{{2,4,21,0,1,26,32767},1,99,750,0,{1,2,3,1}};
    const std::array ordered{encode(leave),encode(manifest),encode(emission)};
    for(unsigned absent=0;absent<2;++absent) for(unsigned split=0;split<4;++split) {
        std::vector<ob::Host> hosts{{2,2403,point,false}};
        ob::Local old{{100,200,300,400,500,2403},point,{},true,true};
        old.prior=ob::Resolve(std::array{old},hosts,{2,2,100})[0].next;
        std::vector<ob::Local> locals;if(!absent)locals.push_back(old);
        SpawnEmissionLedger receipts;
        std::uint32_t frame=101;
        for(unsigned packet=0;packet<ordered.size();++packet) {
            const std::uint8_t* payload=nullptr;std::size_t size=0;
            const auto type=decodePacketHeader(ordered[packet].data(),ordered[packet].size(),payload,size);
            ByteReader reader(payload,size);
            if(type==PacketType::EnemyDeath) {
                EnemyDeath d;read(reader,d);hosts[0].dead=true;hosts[0].left=d.left!=0;
            } else if(type==PacketType::EnemyManifest) {
                EnemyManifest m;read(reader,m);const auto& e=m.entries[0];
                hosts.push_back({e.netId,e.objectId,{e.spawnPosition.x,e.spawnPosition.y,e.spawnPosition.z},false});
            } else {
                SpawnEmission e;read(reader,e);
                check("split refill receipt admitted once",receipts.Add(e)&&receipts.Permit(e.controllerKey,e.headerId,e.recordIndex,true));
                locals.push_back({{101,201,301,400,500,2403},point,{},true,true});
            }
            if(packet==2 || (split&(1u<<packet))) {
                const auto decisions=ob::Resolve(locals,hosts,{2,2,frame++});
                for(std::size_t i=0;i<locals.size();++i) {
                    const int expected=locals[i].identity.actor==100?2:7;
                    check("every split keeps old leave and new incarnation distinct",decisions[i].id==expected);
                    locals[i].prior=decisions[i].next;
                }
            }
        }
    }
    // Sensitivity control: receipt-before-identity lets a new native body steal
    // still-live old2. Its subsequent leave would then retire the wrong body.
    ob::Local fresh{{101,201,301,400,500,2403},point,{},true,true};
    std::vector<ob::Host> stale{{2,2403,point,false}};
    const auto wrong=ob::Resolve(std::array{fresh},stale,{2,2,100})[0];
    check("receipt-first split reproduces stale-old-ID capture",wrong.id==2);
    fresh.prior=wrong.next;stale[0].dead=true;stale[0].left=true;
    stale.push_back({7,2403,point,false});
    check("receipt-first split wrongly gives replacement old leave authority",
          ob::Resolve(std::array{fresh},stale,{2,2,101})[0].reason==ob::Reason::BoundDeath);
}
#endif

int main(int argc,char** argv) {
    OrdinaryUnseenDeathControls();
    OrdinaryRefillControls();
    OrdinaryBirthControls();
    OrdinaryScriptActorControls();
    ScriptActorBoundCurrentControls();
#ifndef RECORD_PURE_ONLY
    SplitRefillDeliveryControls();
#endif
    OrdinaryLifecycleControls();
    OrdinaryTerminalControls();
    OrdinaryHeldDeath();
    OrdinaryUniqueKind();
    OrdinaryStruggle189();
    (void)argc; (void)argv;
    for (auto oid:{317u,76u}) {
        Fixture f(oid);check("first exact complete population",f.run().admitted);
        ++f.scope.frame;check("repeated content and continuous roots",f.run().admitted);
        std::reverse(f.local.begin(),f.local.end());++f.scope.frame;
        check("census ordering is not identity",f.run().admitted);
        check("idempotent same-frame recheck",f.run().admitted);
        f.lease.Interrupt();++f.scope.frame;check("census interruption holds consumed records",!f.run().admitted);
        ++f.scope.load;++f.scope.transition;++f.scope.epoch;++f.scope.frame;
        check("new local load plus new shared epoch admits fresh population",f.run().admitted);
    }
    for (int mode=0;mode<18;++mode) {
        Fixture f;check("seed lifecycle control",f.run().admitted);++f.scope.frame;
        switch(mode) {
        case 0:f.host[0].id=99;break; // host respawn with friend's old body
        case 1:f.local[0].roots.actor=999;break;
        case 2:f.local[0].roots.controller=999;break;
        case 3:f.local[0].roots.status=999;break;
        case 4:f.local[0].id=99;break; // same address reused in new tracked row
        case 5:f.local.push_back(f.local[0]);break;
        case 6:f.host.push_back(f.host[0]);break;
        case 7:f.local[0].key.record[0]^=1;break;
        case 8:f.host[0].key.definition[0]^=1;break;
        case 9:f.local[0].key.header[0]^=1;break;
        case 10:f.local[0].key.controllerKey^=1;break;
        case 11:f.host[0].dead=true;break; // delayed/old death never authorizes lethal
        case 12:--f.scope.epoch;break;
        case 13:++f.scope.epoch;break; // no new client load
        case 14:++f.scope.load;++f.scope.transition;break; // no new host epoch
        case 15:++f.scope.frame;break;
        case 16:f.local.erase(f.local.begin());break;
        case 17:f.scope.hostConnection=999;break;
        }
        check("lifecycle ambiguity holds "+std::to_string(mode),!f.run().admitted);
        Fixture clean;f.local=clean.local;f.host=clean.host;f.scope=clean.scope;++f.scope.frame;
        check("ambiguity cannot recover by replaying old rows "+std::to_string(mode),!f.run().admitted);
    }
    for (int mode=0;mode<9;++mode) {
        Fixture f;
        switch(mode) {
        case 0:f.scope.location.room=7;break;
        case 1:f.scope.location.battleProgram=1;break;
        case 2:f.scope.location.eventProgram=0;break;
        case 3:f.local[0].key.nativeId++;break;
        case 4:f.local[0].key.ordinal++;break;
        case 5:f.host[1]=f.host[0];break;
        case 6:f.local[1]=f.local[0];break;
        case 7:f.local[0].roots.record=0;break;
        case 8:f.local[0].living=false;break;
        }
        check("initial wrong/missing/duplicate key refused "+std::to_string(mode),!f.run().admitted);
    }
    Fixture missing;check("incomplete census cannot establish",!missing.run(false).admitted);
    check("later first complete population permitted",missing.run().admitted);
    ++missing.scope.frame;check("incomplete census revokes existing lease",!missing.run(false).admitted);
    ++missing.scope.frame;check("continuous roots cannot undo missing lifetime evidence",!missing.run().admitted);
    unsigned writes=0;
    for (auto id:{317u,76u}) if (rb::AuthorityAllowed(true,id)) ++writes;
    check("no HP/death/claim authority under opt-in",writes==0);
    check("default and other families retain authority",rb::AuthorityAllowed(false,317) && rb::AuthorityAllowed(true,301));
    Fixture wrongScope;check("seed out-of-scope control",wrongScope.run().admitted);
    ++wrongScope.scope.load;++wrongScope.scope.transition;++wrongScope.scope.epoch;wrongScope.scope.location.room=99;
    check("out-of-scope load cannot erase consumed records",!wrongScope.run().admitted);
    Fixture original;wrongScope.scope=original.scope;++wrongScope.scope.frame;
    check("old scope replay after out-of-scope load stays held",!wrongScope.run().admitted);
    Fixture rollback;check("seed monotonic scope control",rollback.run().admitted);
    --rollback.scope.load;--rollback.scope.transition;--rollback.scope.epoch;
    check("old load and old epoch cannot reset lease",!rollback.run().admitted);
    {
        Fixture f;check("current scoped authority",rb::AuthorityScopeCurrent(f.scope,f.scope,true,true));
        for (int k=0;k<10;++k) {
            auto now=f.scope;
            switch(k) {case 0:++now.frame;break;case 1:++now.generation;break;case 2:++now.epoch;break;
            case 3:++now.load;break;case 4:++now.transition;break;case 5:++now.hostConnection;break;
            case 6:++now.location.room;break;case 7:++now.location.battleProgram;break;
            case 8:++now.location.eventProgram;break;case 9:++now.location.door;break;}
            check("stale scope native write refused",!rb::AuthorityScopeCurrent(f.scope,now,true,true));
        }
        check("retired world context holds native write",!rb::AuthorityScopeCurrent(f.scope,f.scope,false,true));
        check("unsafe gameplay holds native write",!rb::AuthorityScopeCurrent(f.scope,f.scope,true,false));
    }
    for (auto oid:{317u,76u}) {
        Fixture f(oid);check("authority population seeded",f.run().admitted);
        ++f.scope.frame;f.host[0].dead=true;
        check("one terminal death from continuous living population",f.lease.Resolve(f.scope,f.local,f.host,true,true).admitted);
        check("same-frame terminal observation idempotent",f.lease.Resolve(f.scope,f.local,f.host,true,true).admitted);
        ++f.scope.frame;check("terminal lease retired on next frame",!f.lease.Resolve(f.scope,f.local,f.host,true,true).admitted);
        f.host[0].dead=false;check("death rollback cannot recover authority",!f.lease.Resolve(f.scope,f.local,f.host,true,true).admitted);
        ++f.scope.load;++f.scope.transition;++f.scope.epoch;++f.scope.frame;
        check("fresh load resets terminal retirement",f.lease.Resolve(f.scope,f.local,f.host,true,true).admitted);
        Fixture initial(oid);initial.host[0].dead=true;
        check("terminal first admission refused",!initial.lease.Resolve(initial.scope,initial.local,initial.host,true,true).admitted);
        Fixture two(oid);check("two death seed",two.run().admitted);++two.scope.frame;two.host[0].dead=true;two.host[1].dead=true;
        check("ambiguous multi-death refused",!two.lease.Resolve(two.scope,two.local,two.host,true,true).admitted);
        for (unsigned bits=0;bits<8;++bits) {
            const bool b=bits&1,a=bits&2,c=bits&4;
            check("scope opt-in/current proof all required",rb::ScopedAuthorityAllowed(b,a,oid,c)==(b&&a&&c));
        }
        check("other families independent of record opt-in",rb::ScopedAuthorityAllowed(false,false,301,false));
    }
    for (unsigned sides=0;sides<4;++sides) {
        Fixture f(76);
        unsigned n=0;
        for (const auto& a:rb::Excluded) {
            const auto k=++n;
            if (sides&1) f.local.push_back({{1000+k,2000+k,3000+k,4000,5000+k,76},a.key,100+static_cast<int>(k),true});
            if (sides&2) f.host.push_back({a.key,200+static_cast<int>(k),76,false});
        }
        const auto selected=rb::SelectPopulation(f.scope,f.local,f.host);
        check("positive host-only friend-only or both exclusion",selected.complete && selected.local.size()==5 && selected.host.size()==5);
        check("negative records have no binder rights",f.lease.Resolve(f.scope,selected.local,selected.host,selected.complete).admitted);
        check("exact excluded counts",selected.localExcluded==((sides&1)?6:0) && selected.hostExcluded==((sides&2)?6:0));
    }
    for (unsigned mode=0;mode<17;++mode) {
        Fixture f(76);check("ambiguity seed",f.run().admitted);++f.scope.frame;
        auto l=rb::Local{{1001,2001,3001,4000,5001,76},rb::Excluded[0].key,100,true};
        auto h=rb::Host{rb::Excluded[0].key,200,76,false};
        f.local.push_back(l);f.host.push_back(h);
        switch(mode) {
        case 0:f.local.back().key.record[0]^=1;break;
        case 1:f.host.back().key.header[0]^=1;break;
        case 2:f.local.back().roots.actor=f.local[0].roots.actor;break;
        case 3:f.local.back().roots.status=f.local[0].roots.status;break;
        case 4:f.local.back().roots.controller=f.local[0].roots.controller;break;
        case 5:f.local.back().roots.record=f.local[0].roots.record;break;
        case 6:f.local.back().roots.record=0;break;
        case 7:f.local.back().id=f.local[0].id;break;
        case 8:f.host.back().id=f.host[0].id;break;
        case 9:f.local.push_back(l);break;
        case 10:f.host.push_back(h);break;
        case 11:f.local.erase(f.local.begin());break;
        case 12:f.host.erase(f.host.begin());break;
        case 13:f.host.back().key.schema=0;break;
        case 14:f.local.back().key.location.room++;break;
        case 15:f.local.back().key.controllerKey++;break;
        case 16:f.host.back().objectId=317;break;
        }
        const auto selection=rb::SelectPopulation(f.scope,f.local,f.host);
        check("ambiguous exclusion refused",!selection.complete);
        check("selection ambiguity poisons admitted lease",!f.lease.Resolve(f.scope,selection.local,selection.host,selection.complete).admitted);
        Fixture fresh(76);f.local=fresh.local;f.host=fresh.host;
        check("removing ambiguity cannot restore this load",!f.run().admitted);
        ++f.scope.epoch;++f.scope.frame;check("new epoch alone cannot restore this load",!f.run().admitted);
        ++f.scope.load;++f.scope.transition;++f.scope.epoch;++f.scope.frame;
        check("genuinely fresh load restores only original population",f.run().admitted);
    }
#ifndef RECORD_PURE_ONLY
    EnemyManifest m; m.epoch=3; m.entries.push_back({1,2,0,317,{},rb::Allowed[0].key});
    const auto bytes=encode(m);
    const std::uint8_t* payload=nullptr;std::size_t size=0;
    check("record manifest packet type",decodePacketHeader(bytes.data(),bytes.size(),payload,size)==PacketType::EnemyManifest);
    ByteReader reader(payload,size);EnemyManifest decoded;read(reader,decoded);
    check("exact scoped key codec roundtrip",reader.atEnd() && decoded.entries[0].recordKey==m.entries[0].recordKey);
    for (std::size_t cut=0;cut<size;++cut) {
        bool rejected=false;try { ByteReader shortReader(payload,cut);EnemyManifest bad;read(shortReader,bad); } catch (const std::exception&) { rejected=true; }
        check("truncated identity envelope "+std::to_string(cut),rejected);
    }
    auto unsupported=m;unsupported.entries[0].recordKey->schema=2;bool refused=false;
    try { (void)encode(unsupported); } catch (const std::exception&) { refused=true; }
    check("unsupported schema refused",refused);
    m.entries[0].recordKey.reset();const auto ordinary=encode(m);decodePacketHeader(ordinary.data(),ordinary.size(),payload,size);
    ByteReader normal(payload,size);read(normal,decoded);check("default manifest has no experimental key",normal.atEnd()&&!decoded.entries[0].recordKey);
    check("v14/v15 peers require the newer population protocol",PROTOCOL_VERSION == 39);
    if (argc>=2) {
        std::ifstream input(argv[1],std::ios::binary);
        for (const auto oid:{317u,76u}) {
            NativeRecordContentDefinition d;
            const std::string layout="9002b2de6a1f91a790bd0673de125d1cf833f7942bfec827cdcf6ba64d5849ed";
            for (std::size_t i=0;i<32;++i) d.layoutSha256[i]=static_cast<std::uint8_t>(std::stoul(layout.substr(i*2,2),nullptr,16));
            d.location=oid==317?NativeRecordLocation{18,6,0,0,2,32767}:NativeRecordLocation{7,0,0,0,2,32767};
            d.groupKey=825253730;
            input.read(reinterpret_cast<char*>(d.header.data()),44);
            const auto n=native_record_detail::u16(d.header.data()+4);
            if (n>256) return 2;
            d.records.resize(n);
            for (auto& record:d.records) input.read(reinterpret_cast<char*>(record.data()),64);
            check("complete retained ARD input",static_cast<bool>(input));
            const auto actual=BuildNativeRecordContent(d);
            check("native content builder completes retained definition",actual.status==NativeRecordContentStatus::Complete);
            for (const auto& expected:rb::Allowed) if (expected.objectId==oid) {
                check("independent ARD definition hash",actual.comparisonSha256==expected.key.definition);
                check("independent ARD header projection hash",desyncSha256(actual.headerProjection)==expected.key.header);
                check("independent ARD native record identity",actual.records.at(expected.key.ordinal).rawId==expected.key.nativeId && actual.records.at(expected.key.ordinal).recordSha256==expected.key.record);
            }
        }
        char extra{};input.read(&extra,1);check("exact retained content framing",input.eof());
    }    if (argc>=3) {
        std::ifstream in(argv[2],std::ios::binary);NativeRecordContentDefinition d;
        const std::string layout="9002b2de6a1f91a790bd0673de125d1cf833f7942bfec827cdcf6ba64d5849ed";
        for (std::size_t i=0;i<32;++i) d.layoutSha256[i]=static_cast<std::uint8_t>(std::stoul(layout.substr(i*2,2),nullptr,16));
        d.location={7,0,0,0,2,32767};d.groupKey=825253730;
        in.read(reinterpret_cast<char*>(d.header.data()),44);d.records.resize(6);
        for (auto& r:d.records) in.read(reinterpret_cast<char*>(r.data()),64);
        check("complete negative ARD population",static_cast<bool>(in));
        auto actual=BuildNativeRecordContent(d);check("negative content independently complete",actual.status==NativeRecordContentStatus::Complete);
        for (const auto& a:rb::Excluded) {
            check("negative definition pin",actual.comparisonSha256==a.key.definition);
            check("negative header pin",desyncSha256(actual.headerProjection)==a.key.header);
            check("negative record pin",actual.records[a.key.ordinal].recordSha256==a.key.record && actual.records[a.key.ordinal].rawId==a.key.nativeId);
        }
        char extra{};in.read(&extra,1);check("negative ARD framing",in.eof());
    }

#endif
    std::cout<<checks-failed<<'/'<<checks<<" checks PASS\n";return failed?1:0;
}
