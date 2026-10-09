#include "AvatarPositionDiagnostic.hpp"
#include <iostream>
#include <algorithm>
#include <limits>
#include <vector>
using namespace kh2coop;
using namespace kh2coop::inject::avatarposition;
namespace {
int checks{}, failures{};
void check(bool ok, const char* label) { ++checks; if (!ok) ++failures; std::cout << (ok ? "PASS " : "FAIL ") << label << '\n'; }
Scope scope() { Scope s; s.actor=0x1234; s.handle=7; s.load=9; s.owner=1; s.provenance.producer=PuppetProducer::Network; s.provenance.generation=4; s.provenance.ownerConnectionId=55; return s; }
}
int main() {
    Monitor m; std::vector<Receipt> reports;
    auto emit = [&](Receipt r) { reports.push_back(r); };
    const Vec3 target{10,20,30}, bad{110,20,30};
    Vec3 memory{}; int writes{};
    auto read=[&] { return memory; };
    auto write=[&](Vec3 p) { memory=p; ++writes; };
    m.Bind(scope(),false);
    // Original native movement is permitted; only the observed correction lane reports it.
    for (unsigned f=1; f<=8; ++f) {
        m.BeforeUpdate(f,read,emit); memory=bad; m.Apply(f,f,target,read,write,emit);
    }
    check(writes==8 && memory.x==10, "production seam preserves every existing transform write");
    check(reports.size()==1 && reports[0].phase==Phase::NativeCorrection, "native motion distinguished from between-update desync");
    check(reports[0].frame==3 && reports[0].error==100 && reports[0].scope.actor==0x1234, "debounced receipt binds native actor and exact magnitude");
    reports.clear(); m.Reset(); m.Bind(scope(),false); memory=target;
    m.Apply(1,42,target,read,write,emit);
    for(unsigned f=2;f<=4;++f) { memory=bad; m.BeforeUpdate(f,read,emit); memory=target; m.Apply(f,42,target,read,write,emit); }
    check(reports.size()==1 && reports[0].phase==Phase::BetweenUpdates && reports[0].seq==42, "late native writer detected against prior applied target");
    m.BeforeUpdate(5,read,emit);
    check(reports.size()==2 && reports.back().recovered, "corrected readback emits recovery");
    reports.clear(); m.Reset(); m.Bind(scope(),false);
    for(unsigned f=1;f<=4;++f) m.Apply(f,f,target,read,[&](Vec3) {memory=bad;},emit);
    check(reports.size()==2 && std::any_of(reports.begin(),reports.end(),[](const Receipt& r) { return r.phase==Phase::Writeback; }), "owned-memory failed write control reaches actual production seam");
    reports.clear(); m.Reset(); m.Bind(scope(),false);
    for(unsigned n=0;n<8;++n) m.Sample(Phase::Writeback,1,1,target,bad,emit);
    check(reports.empty(), "repeated same-frame calls never satisfy debounce");
    m.Sample(Phase::Writeback,3,1,target,bad,emit); m.Sample(Phase::Writeback,5,1,target,bad,emit);
    check(reports.empty(), "nonconsecutive observations restart debounce");
    m.Reset(); m.Bind(scope(),false);
    for(unsigned f=1;f<=123;++f) m.Sample(Phase::Writeback,f,f,target,bad,emit);
    check(reports.size()==2 && reports[0].frame==3 && reports[1].frame==123, "repeat cadence bounded at 120 frames");
    reports.clear(); m.Reset(); m.Bind(scope(),true);
    for(unsigned f=1;f<=4;++f) m.Apply(f,f,target,read,write,emit);
    check(reports.empty() && writes==16, "held poses still written but diagnostic continuity suspended");
    bool readHeld=false;
    m.Apply(5,5,target,[&] { readHeld=true; return bad; },write,emit);
    check(!readHeld && writes==17, "inactive diagnostic performs no extra reads and preserves existing write");
    // Every identity component must reset accumulation and previous-target continuity.
    for(unsigned field=0;field<15;++field) {
        reports.clear(); m.Reset(); auto a=scope(); m.Bind(a,false);
        m.Sample(Phase::Writeback,1,1,target,bad,emit); m.Sample(Phase::Writeback,2,1,target,bad,emit);
        auto b=a;
        switch(field) {
        case 0: ++b.actor; break; case 1: ++b.handle; break; case 2: ++b.transition; break;
        case 3: ++b.load; break; case 4: ++b.owner; break; case 5: ++b.character; break;
        case 6: ++b.world; break; case 7: ++b.room; break; case 8: ++b.provenance.generation; break;
        case 9: ++b.provenance.ownerConnectionId; break; case 10: ++b.provenance.localConnectionId; break;
        case 11: ++b.provenance.hostConnectionId; break; case 12: b.provenance.producer=PuppetProducer::Standalone; break;
        case 13: ++b.provenance.localSlot; break;
        case 14: ++b.puppetIndex; break;
        }
        m.Bind(b,false); m.Sample(Phase::Writeback,3,1,target,bad,emit);
        check(reports.empty(), "scope replacement resets fault streak");
    }
    reports.clear(); m.Reset(); m.Bind(scope(),false);
    auto nan=target; nan.x=std::numeric_limits<float>::quiet_NaN();
    for(unsigned f=1;f<=3;++f) m.Sample(Phase::Writeback,f,f,target,nan,emit);
    check(reports.size()==1 && !reports[0].finite, "nonfinite readback explicitly reported");
    reports.clear(); m.Reset(); m.Bind(scope(),false);
    for(unsigned f=1;f<=3;++f) m.Sample(Phase::Writeback,f,f,target,Vec3{15,20,30},emit);
    check(reports.empty(), "five-unit inclusive boundary quiet");
    Monitor budget; reports.clear();
    for(unsigned n=0;n<150;++n) { budget.Reset(); budget.Bind(scope(),false); for(unsigned f=1;f<=3;++f) budget.Sample(Phase::Writeback,f,f,target,bad,emit); }
    check(reports.size()==Monitor::ReceiptLimit && reports.back().budgetExhausted, "lifetime fault cap survives resets and last receipt marks exhaustion");
    Monitor separate; reports.clear();
    for(unsigned n=0;n<20;++n) { separate.Reset(); separate.Bind(scope(),false); for(unsigned f=1;f<=3;++f) separate.Sample(Phase::NativeCorrection,f,f,target,bad,emit); }
    check(reports.size()==Monitor::CorrectionLimit && reports.back().budgetExhausted, "correction pressure has separate small lifetime cap");
    for(unsigned f=4;f<=6;++f) separate.Sample(Phase::Writeback,f,f,target,bad,emit);
    check(reports.size()==Monitor::CorrectionLimit+1 && reports.back().phase==Phase::Writeback, "legitimate native correction never consumes fault budget");
    Monitor throwing; throwing.Bind(scope(),false); int preservedWrites{}; memory=bad;
    for(unsigned f=1;f<=3;++f) throwing.Apply(f,f,target,read,[&](Vec3) { ++preservedWrites; },[](Receipt) { throw 7; });
    check(preservedWrites==3, "throwing diagnostic sink cannot suppress existing writes");
    std::cout << "checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
