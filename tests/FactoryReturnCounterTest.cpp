#include "NativePrivateStatus.hpp"
#include <cstdio>
#include <type_traits>
namespace P=kh2coop::inject::privatestatus;
namespace {unsigned failed=0;void check(bool ok,const char* what){std::printf("%s: %s\n",ok?"PASS":"FAIL",what);if(!ok)++failed;}}
int main() {
    static_assert(std::is_trivially_copyable_v<P::FactoryReturnNotice>);
    P::detail::FactoryReturnCounters counts;
    check(!counts.Snapshot().registered && !counts.Snapshot().poisoned,"bootstrap has no ownership coverage");
    auto entered=counts.Begin(false,false);check(entered && counts.Snapshot().inFlight==1,"pre-owner native entry is counted");
    check(!counts.Arm(),"registration cannot overtake an in-flight bootstrap call");
    check(counts.Next()==1,"bootstrap exit consumes a unique serial without granting a receipt");counts.End(entered);
    check(!counts.Snapshot().poisoned && counts.Snapshot().inFlight==0,"normal bootstrap does not poison a later first ownership scope");
    check(counts.Arm() && !counts.Arm(),"coverage starts once and cannot rearm/reset");
    entered=counts.Begin(true,false);check(entered && counts.Snapshot().inFlight==1 && !counts.Snapshot().poisoned,"owner entry is unavailable while native call is active");
    check(counts.Next()==2,"owner return cannot reuse a bootstrap serial");counts.End(entered);
    check(counts.Snapshot().serial==2 && !counts.Snapshot().inFlight,"normal return restores only negative interval availability");
    entered=counts.Begin(false,false);
    check(entered && counts.Snapshot().poisoned && counts.Snapshot().inFlight==1,"foreign ENTRY permanently revokes before original construction");
    check(counts.Next()==3,"foreign return still has a unique diagnostic serial");counts.End(entered);
    check(counts.Snapshot().poisoned && !counts.Arm(),"foreign exit cannot restore coverage or relabel old ownership");
    P::detail::FactoryReturnCounters recursion;check(recursion.Arm(),"recursive case arms");
    entered=recursion.Begin(true,true);check(entered && recursion.Snapshot().poisoned,"callback reentry poisons even on owner thread");recursion.End(entered);
    P::detail::FactoryReturnCounters unwind;unwind.PoisonIfArmed();check(!unwind.Snapshot().poisoned,"pre-ownership unwind is not a fabricated old receipt");
    check(unwind.Arm(),"unwind case arms");entered=unwind.Begin(true,false);unwind.PoisonIfArmed();unwind.End(entered);
    check(unwind.Snapshot().poisoned && !unwind.Snapshot().inFlight,"native unwind retires authority while balancing acquired interval");
    P::detail::FactoryReturnCounters last(UINT64_MAX-1);check(last.Arm(),"last serial case arms");
    check(last.Next()==UINT64_MAX && !last.Snapshot().poisoned,"last unique serial remains representable");
    check(last.Next()==0 && last.Snapshot().serial==UINT64_MAX && last.Snapshot().poisoned,"overflow saturates and poisons, never wraps to an old serial");
    check(last.Next()==0 && !last.Arm(),"exhaustion remains permanent");
    P::detail::FactoryReturnCounters full;check(full.Arm(),"interval capacity case arms");
    bool all=true;for(unsigned i=0;i<full.CountMask;++i)all=full.Begin(true,false)&&all;
    check(all && full.Snapshot().inFlight==full.CountMask,"bounded interval count never truncates active calls");
    const auto extra=full.Begin(true,false);check(!extra && full.Snapshot().poisoned,"interval overflow fails closed before native entry");
    full.End(extra);check(full.Snapshot().inFlight==full.CountMask,"failed entry does not decrement another call");
    for(unsigned i=0;i<full.CountMask;++i)full.End(true);
    check(!full.Snapshot().inFlight && full.Snapshot().poisoned,"balanced exits do not unpoison lost coverage");
    P::detail::FactoryReturnCounters bad;bad.End(true);check(bad.Snapshot().poisoned && !bad.Snapshot().inFlight,"underflow poisons without wrapping active count");
    return failed?1:0;
}
