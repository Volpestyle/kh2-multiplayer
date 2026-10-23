#include "ScriptCamera.hpp"
#include <cstdio>
using namespace kh2coop::inject::scriptmirror::rules;
int main() {
    const EntityCameraIdentity opened{0x1000,0x2080,0x3000,0x6C000B};
    unsigned failed=0;
    const auto check=[&](bool ok,const char* label){std::printf("%s: %s\n",ok?"PASS":"FAIL",label);if(!ok)++failed;};
    check(SameEntityCamera(opened,opened),"exact registered native kind11 source and camera payload remain one occurrence");
    for(unsigned field=0;field<4;++field) {
        auto changed=opened;
        if(field==0)++changed.context;
        if(field==1)++changed.sourceAnchor;
        if(field==2)++changed.asset;
        if(field==3)changed.kindRecord=0x6C000A; // another native task using the same storage
        check(!SameEntityCamera(opened,changed),"context reuse, source, asset or native task replacement ends camera identity");
    }
    auto story=opened;story.kindRecord=0x00000001;
    check(!SameEntityCamera(opened,story),"a story event cannot inherit an entity camera exception");
    check(!SameEntityCamera({},opened) && !SameEntityCamera(opened,{}),"unregistered or unavailable task never grants gameplay");
    auto malformed=opened;malformed.asset=0;
    check(!SameEntityCamera(malformed,malformed),"identical missing payloads are not a registered camera");

    // VUH-2707: the production observation wrappers never supply authority,
    // call a reader or retry a failed predicate. Compare the exact operand order.
    for(bool enabled:{false,true})for(unsigned stop=0;stop<5;++stop) {
        CameraObservation trace{};trace.enabled=enabled;
        unsigned reads=0;std::uint64_t value=0;
        const auto reader=[&](){value=++reads;return reads!=stop;};
        const bool result=!trace.Bad(stop==4,"root") &&
            trace.Read(reader(),"context",CameraObservation::Context,value) &&
            trace.Read(reader(),"asset",CameraObservation::Asset,value) &&
            trace.Read(reader(),"kind",CameraObservation::Kind,value);
        const unsigned expected=stop==4?0:stop==0?3:stop;
        check(result==(stop==0) && reads==expected,"on/off keeps return, short-circuit count and order");
        const unsigned successful=stop==4?0:stop==0?3:stop-1;
        const auto mask=(successful>0?(1u<<CameraObservation::Context):0u) |
                        (successful>1?(1u<<CameraObservation::Asset):0u) |
                        (successful>2?(1u<<CameraObservation::Kind):0u);
        check(trace.observed==(enabled?mask:0u),"only completed reads are observed");
        if(enabled && successful>0)check(trace.values[CameraObservation::Context]==1,"context sampled after its reader");
        if(enabled && successful>1)check(trace.values[CameraObservation::Asset]==2,"asset sampled after its reader");
        if(enabled && successful>2)check(trace.values[CameraObservation::Kind]==3,"kind sampled after its reader");
        if(enabled && stop>0)check(std::strcmp(trace.reason,stop==4?"root":stop==1?"context":stop==2?"asset":"kind")==0,
                                   "first rejected operand is named without reading later operands");
        if(!enabled)check(trace.observed==0 && !std::strcmp(trace.reason,"complete"),"off observes no scalar or refusal");
    }
    for(bool enabled:{false,true}) {
        CameraObservation ordered{};ordered.enabled=enabled;
        unsigned order=0;std::uint64_t value=0;
        const auto reader=[&](unsigned tag){order=order*10+tag;value=tag;return true;};
        check(ordered.Read(reader(1),"one",CameraObservation::Context,value) &&
              ordered.Read(reader(2),"two",CameraObservation::Asset,value) &&
              ordered.Read(reader(3),"three",CameraObservation::Kind,value) && order==123,
              "distinct reader identities preserve the original evaluation order");
    }
    CameraObservation partial{};partial.enabled=true;
    std::uint64_t unavailable=999;unsigned calls=0;
    const auto failingReader=[&](){++calls;unavailable=123;return false;};
    check(!partial.Read(failingReader(),"partial",CameraObservation::Asset,unavailable) && calls==1 && !partial.observed,
          "failed/partial output is unobserved and never retried");
    partial.Bad(true,"later");
    check(!std::strcmp(partial.reason,"partial"),"later refusals cannot overwrite the first one");
    CameraObservation skipped{};skipped.enabled=true;skipped.watch=false;
    check(skipped.Bad(true,"another-role") && !skipped.Read(false,"another-read",CameraObservation::Kind,unavailable) &&
          !skipped.observed && !std::strcmp(skipped.reason,"complete"),"another context keeps its decision without poisoning this role's evidence");
    check(skipped.Good(true,"pass") && !skipped.Good(false,"fail"),"positive predicate forwarding preserves both answers");
    CameraObservationBudget budget;
    CameraObservationKey key{0,0,1,2,3,false,"pre-native"};
    check(!budget.Take(false,key) && !budget.Used(),"off does not consume the one trace budget");
    check(budget.Take(true,key) && !budget.Take(true,key) && budget.Used()==1,"repeated per-frame proof does not exhaust the budget");
    key.nativeCalls=1;key.reason="registration";
    check(budget.Take(true,key),"registration after the native call remains distinct from pre-native refusal");
    key.site=2;key.reason="frozen";
    check(budget.Take(true,key),"registered-then-failed-proof has its own site and first refusal");
    for(unsigned i=budget.Used();i<CameraObservationBudget::Capacity;++i) {key.role=100+i;check(budget.Take(true,key),"distinct trace row fits within the cap");}
    key.role=999;
    check(!budget.Take(true,key) && budget.Used()==64,"one fixed budget caps every site without changing decisions");
    return failed?1:0;
}
