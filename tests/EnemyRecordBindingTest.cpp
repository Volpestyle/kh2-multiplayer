#include "EnemyRecordBinding.hpp"
#ifndef RECORD_PURE_ONLY
#include "kh2coop/Codec.hpp"
#include "kh2coop/NativeRecordContent.hpp"
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
int main(int argc,char** argv) {
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
    check("v14 peer is explicitly incompatible",PROTOCOL_VERSION==15);
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
