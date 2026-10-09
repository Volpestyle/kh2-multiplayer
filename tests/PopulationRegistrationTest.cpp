#include "PopulationRegistration.hpp"
#include <cstdlib>
#include <iostream>

namespace pr=kh2coop::inject::populationregistration;
static void Require(bool value,const char* what) {
    if (!value) { std::cerr<<what<<'\n';std::abort(); }
}
static pr::Enrollment Initial() {
    pr::Enrollment out;
    out.scope={3,2,0x180000,0x190000,1};
    out.initial=out.current=5;
    out.initializationReturned=out.complete=out.stable=out.cacheComplete=true;
    out.cacheEmpty=out.owner=out.coverageComplete=true;
    for (std::size_t i=0;i<5;++i)
        out.records[i]={out.scope.header+44+i*64,static_cast<std::uint16_t>(11+i),302,2,0,0};
    return out;
}
static pr::Roots Body(const pr::Enrollment& initial) {
    return {0x200000,0x210000,0x220000,initial.scope.controller,initial.records[0].address};
}
static pr::Request Request(const pr::Enrollment& initial) {
    pr::Request request;
    request.scope=initial.scope;request.record=initial.records[0];request.netId=11;
    request.birth=16;request.cut=16;request.generation=5;request.delivery=1;request.epoch=1;
    request.session[0]=1;request.hostConnection=1;request.selfConnection=4;
    request.location={5,6,0,1,1,0};return request;
}
static pr::Check Check(const pr::Request& request) {
    pr::Check out;out.request=request;out.initial=out.current=5;
    out.source=out.census=out.admission=out.ownerActivation=out.fiberContinuity=true;
    out.creatorExclusive=out.globalPendingExcluded=out.noNativeCopy=true;
    out.noPendingDisposal=out.loadedFamily=out.recordBookends=true;return out;
}
static pr::PostCheck Post(const pr::Request& request,const pr::Roots& body) {
    pr::PostCheck out;out.state=Check(request);out.state.noNativeCopy=false;
    out.roots=body;out.rootsStable=out.subtypeMatches=out.physicalCensusMember=true;
    return out;
}
int main() {
    const auto initial=Initial();const auto request=Request(initial);const auto body=Body(initial);
    {
        pr::Ledger ledger;Require(ledger.Initialize(initial),"native init accepted");
        Require(ledger.Birth(initial.scope,0,body),"native birth occupies credit");
        Require(ledger.Death(initial.scope,0,body,5,4,true),"exact native death debits credit");
        Require(ledger.Available(initial.scope,0,initial.records[0],5,4)==pr::Reason::AlreadyDebited,"ID1 terminal credit cannot be used for ID11");
        Require(!ledger.Initialize(initial),"same init cannot rearm spent credit");
    }
    {
        pr::Ledger ledger;Require(ledger.Initialize(initial),"init");
        Require(ledger.Birth(initial.scope,0,body),"birth");
        Require(ledger.AliveRemoval(initial.scope,0,body,5,5,true,true),"alive removal releases outstanding credit");
        Require(ledger.Available(initial.scope,0,initial.records[0],5,5)==pr::Reason::Ready,"unspent native credit remains valid");
    }
    for (unsigned mutation=0;mutation<14;++mutation) {
        pr::Ledger ledger;Require(ledger.Initialize(initial),"init");pr::Consumer consumer;
        unsigned reads=0,calls=0,recaptures=0;
        const auto probe=[&] {
            auto out=Check(request);++reads;
            // Every refusal is injected on the FINAL capture, after a good first capture.
            if (reads==2) switch(mutation) {
            case 0:out.source=false;break;
            case 1:out.census=false;break;
            case 2:out.admission=false;break;
            case 3:out.ownerActivation=false;break;
            case 4:out.fiberContinuity=false;break;
            case 5:out.creatorExclusive=false;break;
            case 6:out.globalPendingExcluded=false;break;
            case 7:out.noNativeCopy=false;break;
            case 8:out.noPendingDisposal=false;break;
            case 9:out.loadedFamily=false;break;
            case 10:out.recordBookends=false;break;
            case 11:out.request.birth=17;break;
            case 12:out.current=4;break;
            case 13:out.request.scope.transition=3;break;
            }
            return out;
        };
        const auto outcome=consumer.Dispatch(ledger,request,probe,[&](const auto&){++calls;return body[0];},
            [&](const auto&,auto){++recaptures;return Post(request,body);});
        Require(outcome!=pr::Reason::Ready && reads==2 && calls==0 && recaptures==0,"final adversary must cause zero native calls");
    }
    {
        pr::Ledger ledger;pr::Consumer consumer;unsigned calls=0;
        const auto outcome=consumer.Dispatch(ledger,request,[&]{return Check(request);},
            [&](const auto&){++calls;return body[0];},[&](const auto&,auto){return Post(request,body);});
        Require(outcome==pr::Reason::UnknownEnrollment && calls==0,"positive aggregate count is not enrollment authority");
    }
    for (bool fault:{false,true}) {
        pr::Ledger ledger;Require(ledger.Initialize(initial),"init");pr::Consumer consumer;
        unsigned calls=0,recaptures=0;
        auto invoke=[&](const auto&){++calls;return fault?std::uintptr_t{}:body[0];};
        auto recapture=[&](const auto&,auto){++recaptures;return Post(request,body);};
        const auto outcome=consumer.Dispatch(ledger,request,[&]{return Check(request);},invoke,recapture);
        Require(outcome==(fault?pr::Reason::NativeFault:pr::Reason::Ready) && calls==1 && recaptures==1,"one native call and mandatory recapture");
        auto reauthorized=request;++reauthorized.cut;++reauthorized.generation;
        Require(consumer.Dispatch(ledger,reauthorized,[&]{return Check(reauthorized);},invoke,recapture)==pr::Reason::AlreadyAttempted && calls==1,"reauthorization cannot replay native call");
    }
    for (unsigned mutation=0;mutation<10;++mutation) {
        pr::Ledger ledger;Require(ledger.Initialize(initial),"init");pr::Consumer consumer;
        unsigned calls=0;
        const auto outcome=consumer.Dispatch(ledger,request,[&]{return Check(request);},
            [&](const auto&){++calls;return body[0];},[&](const auto&,auto){
                auto post=Post(request,body);
                switch(mutation) {
                case 0:post.roots[0]+=0x1000;break;
                case 1:post.roots[3]+=0x1000;break;
                case 2:post.roots[4]+=64;break;
                case 3:post.state.source=false;break;
                case 4:++post.state.request.delivery;break;
                case 5:post.state.census=false;break;
                case 6:post.rootsStable=false;break;
                case 7:post.subtypeMatches=false;break;
                case 8:post.physicalCensusMember=false;break;
                case 9:post.state.current=4;break;
                }
                return post;
            });
        Require(outcome==pr::Reason::PostCallRefused && calls==1,"post-call replacement cannot qualify registration");
        Require(ledger.status()==pr::Reason::PostCallRefused,"ambiguous native mutation poisons ledger");
        Require(consumer.Dispatch(ledger,request,[&]{return Check(request);},
            [&](const auto&){++calls;return body[0];},[&](const auto&,auto){return Post(request,body);})==pr::Reason::AlreadyAttempted && calls==1,
            "post-call refusal cannot replay constructor");
    }
    std::cout<<"Population registration ledger and actual consumer controls PASS\n";
}
