#ifndef NOMINMAX
#define NOMINMAX
#endif
// Production codec/staging/ENet controls. All snapshot and ACK native facts in
// this test are explicitly synthetic; this does not execute the native adapter.
#include "kh2coop/Codec.hpp"
#include "WorldWireFixture.hpp"
#include "kh2coop/ProgressAllowList.hpp"
#include "kh2coop/SessionHost.hpp"
#include <enet/enet.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>
using namespace kh2coop;
namespace {
int checks=0,failures=0;
void check(bool ok,const char* label){++checks;if(!ok)++failures;std::cout<<(ok?"PASS: ":"FAIL: ")<<label<<'\n';}
template<class F> bool throws(F&& fn){try{fn();return false;}catch(const std::exception&){return true;}}
ResyncSnapshot snapshot(bool nonempty=true,std::int32_t hp=73){
    ResyncSnapshot s;s.room={7,4,26,0,0,3,0};s.hold={7,false,0};s.progress.version=1;s.progress.full=true;
    for(const auto& range:verifiedProgressAllowList())s.progress.spans.push_back({range.offset,std::vector<std::uint8_t>(range.length,0)});
    s.hpSequence=10;s.coverageMask=ResyncComplete;s.generation=1;s.transitionSerial=2;s.loadSerial=3;s.captureFrameBefore=100;s.captureFrameAfter=101;
    if(nonempty){ResyncEnemyState e;e.identity.netId=1;e.identity.objectId=309;e.identity.battleProgram=3;e.objectType=4;e.hp=hp;e.maxHp=100;s.enemies.push_back(e);s.livingCount=1;}
    s.nativeFingerprint=resyncNativeFingerprint(s);return s;
}
ResyncBegin header(const ResyncSnapshot& s,const ResyncPlan& plan,std::uint64_t cut){
    const auto bytes=encodeResyncSnapshot(s);ResyncBegin b;b.key=plan.request.key;b.phase=plan.phase;b.room=s.room;b.targets=plan.targets;b.targetCount=plan.targetCount;b.snapshotCut=cut;b.totalBytes=static_cast<std::uint32_t>(bytes.size());b.partCount=static_cast<std::uint16_t>((bytes.size()+RESYNC_MAX_PART_BYTES-1)/RESYNC_MAX_PART_BYTES);b.sha256=desyncSha256(bytes);return b;
}
std::vector<ResyncPart> parts(const ResyncBegin& b,const ResyncSnapshot& s){
    const auto bytes=encodeResyncSnapshot(s);std::vector<ResyncPart> out;
    for(std::size_t off=0;off<bytes.size();off+=RESYNC_MAX_PART_BYTES){const auto end=(std::min)(bytes.size(),off+RESYNC_MAX_PART_BYTES);out.push_back({b.key,b.phase,b.snapshotCut,static_cast<std::uint32_t>(off),{bytes.begin()+off,bytes.begin()+end}});}return out;
}
ResyncEnd ending(const ResyncBegin& b){return {b.key,b.phase,b.snapshotCut,b.totalBytes,b.sha256};}
ResyncPlan staticPlan(){ResyncPlan p;p.request.key={std::string(32,'a'),0x100000001ULL,0x200000002ULL};p.request.room=snapshot().room;p.request.targetMask=6;p.request.connections={0x100000001ULL,0x300000003ULL,0x400000004ULL};p.targets={ResyncTarget{1,p.request.connections[1],0x500000005ULL},ResyncTarget{2,p.request.connections[2],0x600000006ULL}};p.targetCount=2;p.remainingMs=RESYNC_TIMEOUT_MS;return p;}
template<class T> void exactCodec(const T& m){ByteWriter w;write(w,m);T copy;ByteReader r(w.data());read(r,copy);ByteWriter again;write(again,copy);check(r.atEnd()&&w.data()==again.data(),"actual record codec exact roundtrip");auto shortBytes=w.data();shortBytes.pop_back();check(throws([&]{ByteReader bad(shortBytes);T value;read(bad,value);}),"record truncated payload rejected");auto extra=w.data();extra.push_back(0);check(throws([&]{ByteReader bad(extra);T value;read(bad,value);}),"record suffix rejected");}
void codecAndStaging(){
    const auto plan=staticPlan();auto s=snapshot();const auto b=header(s,plan,0x700000007ULL);auto chunks=parts(b,s);
    exactCodec(plan.request);exactCodec(plan);exactCodec(b);exactCodec(chunks[0]);exactCodec(ending(b));exactCodec(s);
    exactCodec(WorldBinding{plan.request.key.sessionId,plan.request.connections[0],plan.request.connections[1],1,0x500000005ULL});
    WorldEnvelope e{{plan.request.key.sessionId,plan.request.connections[0],1,b.snapshotCut,plan.targets[0].connectionId,plan.targets[0].deliverySerial},encode(EnemyHp{7,{{1,73,100}},10})};exactCodec(e);
    auto nested=e;nested.packet=encode(e);check(throws([&]{(void)encode(nested);}),"nested world envelope forbidden");nested.packet=encodeLocalResyncCommand(6);check(throws([&]{(void)encode(nested);}),"local operator opcode never admissible as wire world payload");
    for(unsigned mode=0;mode<12;++mode){auto invalid=s;
        if(mode==0)invalid.coverageMask&=~ResyncCompleteCensus;
        if(mode==1)invalid.progress.spans[0].bytes.pop_back();
        if(mode==2)invalid.progress.spans.push_back(invalid.progress.spans[0]);
        if(mode==3)invalid.progress.full=false;
        if(mode==4)invalid.progress.spans.back().bytes.back()=0x80;
        if(mode==5)invalid.livingCount=0;
        if(mode==6)invalid.enemies.push_back(invalid.enemies[0]);
        if(mode==7)invalid.enemies[0].identity.objectId=0;
        if(mode==8)invalid.enemies[0].identity.battleProgram=4;
        if(mode==9)invalid.hpSequence=0;
        if(mode==10)invalid.hold.epoch=8;
        if(mode==11)invalid.nativeFingerprint[0]^=1;
        check(throws([&]{(void)encodeResyncSnapshot(invalid);}),"incomplete or inconsistent snapshot cannot serialize as complete");
    }
    const auto empty=snapshot(false);check(decodeResyncSnapshot(encodeResyncSnapshot(empty)).enemies.empty(),"explicit complete empty census is valid, unlike missing coverage");
    // Force multiple genuine canonical parts with unique full typed rows.
    for(std::uint16_t id=2;id<=400;++id){auto row=s.enemies[0];row.identity.netId=id;row.identity.spawnIndex=id;s.enemies.push_back(row);}s.livingCount=400;s.nativeFingerprint=resyncNativeFingerprint(s);
    const auto multi=header(s,plan,9);chunks=parts(multi,s);check(chunks.size()>1,"multipart fixture crosses actual 16KiB boundary");
    ResyncAssembler a;check(a.Begin(multi)&&a.Begin(multi),"exact duplicate Begin idempotent");
    for(const auto& p:chunks)check(a.Part(p)&&a.Part(p),"exact repeated chunk idempotent at real offset");
    auto decoded=a.End(ending(multi));check(decoded&&decoded->livingCount==400&&!a.Header(),"only complete SHA-checked snapshot publishes once");check(!a.End(ending(multi)),"second End cannot publish a second snapshot");
    for(unsigned mode=0;mode<7;++mode){a.Reset();a.Begin(multi);auto bad=chunks[0];
        if (mode == 0) { bad.offset = 1; }
        if (mode == 1) { ++bad.key.requestId; }
        if (mode == 2) { ++bad.snapshotCut; }
        if (mode == 3) { bad.phase = ResyncPhase::Checkpoint; }
        if (mode == 4) { bad.bytes.pop_back(); }
        if (mode == 5) { a.Part(bad); bad.bytes[0] ^= 1; }
        if (mode == 6) { bad.bytes.resize(RESYNC_MAX_PART_BYTES + 1); }
        check(!a.Part(bad)&&!a.End(ending(multi)),"gap/conflict/identity/size failure never publishes partial state");
    }
    a.Begin(multi);a.Part(chunks[0]);check(!a.End(ending(multi)),"missing tail cannot certify complete snapshot");
    a.Begin(multi);for(const auto& p:chunks)a.Part(p);auto badEnd=ending(multi);badEnd.sha256[0]^=1;check(!a.End(badEnd),"wrong terminal digest invalidates fully received bytes");
    const auto native=encodeNativeResyncSnapshot(b,snapshot());ResyncBegin nativeB;ResyncSnapshot nativeS;decodeNativeResyncSnapshot(native,nativeB,nativeS);check(nativeB.snapshotCut==b.snapshotCut&&nativeS.nativeFingerprint==snapshot().nativeFingerprint,"local immutable snapshot keeps exact cut and full checked state");
    auto broken=native;broken.push_back(0);check(throws([&]{decodeNativeResyncSnapshot(broken,nativeB,nativeS);}),"local snapshot suffix rejected before any native consumer");
}
NativeRecordContentDefinition recordDefinition(const RoomTransition& room, std::uint16_t id,
                                               std::uint16_t count, std::uint16_t firstRecordId) {
    NativeRecordContentDefinition d; d.layoutSha256[0] = 0x90;
    d.location = {room.worldId,room.roomId,room.door,room.mapProgram,room.battleProgram,room.eventProgram};
    d.groupKey = 808476514; d.header[0] = 2;
    d.header[2] = static_cast<std::uint8_t>(id); d.header[3] = static_cast<std::uint8_t>(id >> 8);
    d.header[4] = static_cast<std::uint8_t>(count); d.header[5] = static_cast<std::uint8_t>(count >> 8);
    d.records.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        d.records[i][0] = 0x35; d.records[i][1] = 1; // object309 as full raw u32
        const auto rawId = static_cast<std::uint16_t>(firstRecordId + i);
        d.records[i][30] = static_cast<std::uint8_t>(rawId);
        d.records[i][31] = static_cast<std::uint8_t>(rawId >> 8);
    }
    return d;
}
ResyncSnapshot witnessedSnapshot() {
    auto s = snapshot(); s.coverageMask = ResyncNativeComplete;
    s.recordDefinitions = {recordDefinition(s.room,30,2,11),recordDefinition(s.room,31,0,0)};
    s.enemies[0].record = {0,1}; // native index is deliberately not manifest spawnIndex
    s.nativeFingerprint = resyncNativeFingerprint(s); return s;
}
void recordContentCodec() {
    const auto s = witnessedSnapshot(); const auto bytes = encodeResyncSnapshot(s);
    exactCodec(s);
    const auto copy = decodeResyncSnapshot(bytes);
    check(copy.coverageMask == ResyncNativeComplete && copy.recordDefinitions.size() == 2 &&
        copy.enemies[0].record == ResyncRecordReference{0,1} && copy.enemies[0].identity.spawnIndex == 0 &&
        copy.recordDefinitions[0].header == s.recordDefinitions[0].header &&
        copy.recordDefinitions[0].records == s.recordDefinitions[0].records,
        "v9 full record witness roundtrips exact raw headers arrays and independent native record reference");
    auto projected = s; projected.recordDefinitions[0].header[14] ^= 1;
    projected.nativeFingerprint = resyncNativeFingerprint(projected);
    check(projected.nativeFingerprint == s.nativeFingerprint && encodeResyncSnapshot(projected) != bytes &&
        desyncSha256(encodeResyncSnapshot(projected)) != desyncSha256(bytes),
        "header+E remains exact wire evidence but is excluded from canonical native fingerprint");
    projected = s; projected.recordDefinitions[0].header[15] ^= 1;
    check(resyncNativeFingerprint(projected) != s.nativeFingerprint,
        "nonexcluded header sidecar byte changes native fingerprint");
    auto permuted = s; std::swap(permuted.recordDefinitions[0],permuted.recordDefinitions[1]);
    permuted.enemies[0].record.definitionIndex = 1;
    permuted.nativeFingerprint = resyncNativeFingerprint(permuted);
    check(permuted.nativeFingerprint == s.nativeFingerprint && encodeResyncSnapshot(permuted) != bytes,
        "definition permutation with remapped references changes wire bytes but not content fingerprint");
    auto different = s; different.enemies[0].record.recordIndex = 0;
    check(resyncNativeFingerprint(different) != s.nativeFingerprint,
        "same-object rows referring to different native record indices have different fingerprints");
    different = s; different.recordDefinitions[0].records[0][63] ^= 1;
    check(resyncNativeFingerprint(different) != s.nativeFingerprint,
        "unreferenced record bytes still participate in complete catalog fingerprint");
    different = s; std::swap(different.recordDefinitions[0].records[0],different.recordDefinitions[0].records[1]);
    different.enemies[0].record.recordIndex = 0;
    check(resyncNativeFingerprint(different) != s.nativeFingerprint,
        "native record array order is semantic even when a row is remapped to the same bytes");
    different = s; different.generation += 1; different.loadSerial += 1; different.transitionSerial += 1;
    different.captureFrameBefore += 5; different.captureFrameAfter += 5; different.hpSequence += 1; different.progress.version += 1;
    check(resyncNativeFingerprint(different) == s.nativeFingerprint,
        "local capture transport and progress-version stamps remain outside content fingerprint");
    auto empty = snapshot(false), emptyWitness = empty; emptyWitness.coverageMask = ResyncNativeComplete;
    emptyWitness.nativeFingerprint = resyncNativeFingerprint(emptyWitness);
    check(empty.nativeFingerprint != emptyWitness.nativeFingerprint &&
        decodeResyncSnapshot(encodeResyncSnapshot(emptyWitness)).recordDefinitions.empty(),
        "explicit witness presence distinguishes complete native-empty from synthetic-empty fingerprints");
    auto dead = s; dead.enemies[0].life = ResyncLife::ObservedDeadHistory; dead.enemies[0].hp = 0;
    dead.livingCount = 0; dead.deadCount = 1; dead.nativeFingerprint = resyncNativeFingerprint(dead);
    check(decodeResyncSnapshot(encodeResyncSnapshot(dead)).enemies[0].record == s.enemies[0].record,
        "shared codec retains valid historical-dead row content references without granting native support");
    auto wide = s; wide.enemies[0].identity.objectId = 0x12340135; wide.recordDefinitions[0].records[1][2] = 0x34;
    wide.recordDefinitions[0].records[1][3] = 0x12; wide.nativeFingerprint = resyncNativeFingerprint(wide);
    check(decodeResyncSnapshot(encodeResyncSnapshot(wide)).enemies[0].identity.objectId == 0x12340135,
        "record object identity uses full raw u32 and never narrows to raw u16 record ID");
    wide.enemies[0].identity.objectId = 309;
    check(throws([&]{(void)resyncNativeFingerprint(wide);}), "equal low object-ID bits cannot hide differing high16 bits");

    for (unsigned mode = 0; mode < 23; ++mode) {
        auto bad = s;
        if (mode == 0) bad.coverageMask = ResyncComplete;
        if (mode == 1) bad.coverageMask = ResyncNativeComplete | 256;
        if (mode == 2) bad.coverageMask = ResyncRecordContent;
        if (mode == 3) bad.coverageMask = ResyncNativeComplete & ~ResyncDeaths;
        if (mode == 4) bad.recordDefinitions.clear();
        if (mode == 5) bad.enemies[0].record = {};
        if (mode == 6) bad.enemies[0].record.definitionIndex = 2;
        if (mode == 7) bad.enemies[0].record.recordIndex = 2;
        if (mode == 8) bad.enemies[0].record = {1,0};
        if (mode == 9) bad.recordDefinitions[0].header[4] = 1;
        if (mode == 10) bad.recordDefinitions[0].header[0] = 3;
        if (mode == 11) bad.recordDefinitions[0].layoutSha256.fill(0);
        if (mode == 12) bad.recordDefinitions[1].layoutSha256[1] = 1;
        if (mode == 13) ++bad.recordDefinitions[0].location.door;
        if (mode == 14) bad.recordDefinitions[0].records[1][0] ^= 1;
        if (mode == 15) { auto row=bad.enemies[0]; row.identity.netId=2; bad.enemies.push_back(row); ++bad.livingCount; }
        if (mode == 16) bad.recordDefinitions[0].records[1][30] = bad.recordDefinitions[0].records[0][30];
        if (mode == 17) bad.recordDefinitions[1] = recordDefinition(bad.room,31,1,11);
        if (mode == 18) bad.recordDefinitions.push_back(bad.recordDefinitions[1]);
        if (mode == 19) { bad.recordDefinitions.push_back(bad.recordDefinitions[1]); bad.recordDefinitions.back().header[15] ^= 1; }
        if (mode == 20) bad.recordDefinitions[0] = recordDefinition(bad.room,30,257,11);
        if (mode == 21) { bad.recordDefinitions.clear(); for(std::uint16_t i=0;i<65;++i)bad.recordDefinitions.push_back(recordDefinition(bad.room,i,0,0)); }
        if (mode == 22) { bad.recordDefinitions.clear(); for(std::uint16_t i=0;i<5;++i)bad.recordDefinitions.push_back(recordDefinition(bad.room,i,256,static_cast<std::uint16_t>(i*256))); }
        check(throws([&]{bad.nativeFingerprint=resyncNativeFingerprint(bad);(void)encodeResyncSnapshot(bad);}),
            "inconsistent coverage unsupported/ambiguous catalog or invalid full record reference is rejected");
    }
    auto legacyRef = snapshot(); legacyRef.enemies[0].record = {0,0};
    check(throws([&]{(void)resyncNativeFingerprint(legacyRef);}), "synthetic127 path rejects orphan non-sentinel references");
    auto maxDefs = emptyWitness;
    for(std::uint16_t i=0;i<64;++i) maxDefs.recordDefinitions.push_back(recordDefinition(maxDefs.room,i,0,0));
    maxDefs.nativeFingerprint = resyncNativeFingerprint(maxDefs);
    check(decodeResyncSnapshot(encodeResyncSnapshot(maxDefs)).recordDefinitions.size() == 64,
        "all64 distinct empty native definitions fit without truncating catalog");
    auto maxPer = s; maxPer.recordDefinitions[0] = recordDefinition(maxPer.room,30,256,0);
    maxPer.recordDefinitions[0].records[255][30] = 0; maxPer.recordDefinitions[0].records[255][31] = 0x80;
    maxPer.nativeFingerprint = resyncNativeFingerprint(maxPer);
    check(decodeResyncSnapshot(encodeResyncSnapshot(maxPer)).recordDefinitions[0].records.size() == 256,
        "256 records including zero and high-bit raw IDs remain exact representable content");

    // Corrupt genuine production bytes, including count fields checked before allocation.
    const auto definitionsAt = bytes.size() - (2 + 92 * 2 + 64 * 2);
    const auto firstDefinition = definitionsAt + 2;
    for (unsigned mode = 0; mode < 6; ++mode) {
        auto broken = bytes;
        if(mode==0){broken[definitionsAt]=65;broken[definitionsAt+1]=0;}
        if(mode==1){broken[firstDefinition+52]=1;broken[firstDefinition+53]=1;}
        if(mode==2)broken.resize(firstDefinition+92+64);
        if(mode==3)broken[firstDefinition+48]=4;
        if(mode==4)broken[firstDefinition+92+64]^=1;
        if(mode==5)broken.pop_back();
        auto unchanged=s; unchanged.generation=12345;
        check(throws([&]{ByteReader r(broken);read(r,unchanged);}) && unchanged.generation==12345,
            "malformed real witness bytes are rejected without committing partial output");
    }
    // Meaningful exact byte limit: split existing allowed progress spans without
    // changing progress content to fill the final v9 snapshot to exactly60000.
    auto boundary = s; auto row=boundary.enemies[0]; row.identity.netId=2; row.record={0,0};
    boundary.enemies.push_back(row); ++boundary.livingCount;
    std::size_t extraSpans=0; bool found=false;
    for (std::uint16_t total=700;total<=900&&!found;++total) {
        boundary.recordDefinitions.clear();
        std::uint16_t used=0;
        while(used<total){const auto n=static_cast<std::uint16_t>((std::min)(256u,static_cast<unsigned>(total-used)));
            boundary.recordDefinitions.push_back(recordDefinition(boundary.room,used,n,used)); used=static_cast<std::uint16_t>(used+n);}
        std::size_t size=114+39*boundary.enemies.size()+92*boundary.recordDefinitions.size()+64*total,capacity=0;
        for(const auto& p:boundary.progress.spans){size+=6+p.bytes.size();capacity+=p.bytes.size()-1;}
        if(size<=60000&&(60000-size)%6==0&&(60000-size)/6<=capacity){extraSpans=(60000-size)/6;found=true;}
    }
    check(found,"exact60000 fixture derives from bounded real wire widths and allowed progress content");
    if(found){std::vector<ProgressSpan> split;
        for(const auto& p:boundary.progress.spans){const auto n=(std::min)(extraSpans,p.bytes.size()-1);
            for(std::size_t i=0;i<n;++i)split.push_back({p.offset+static_cast<std::uint32_t>(i),{p.bytes[i]}});
            split.push_back({p.offset+static_cast<std::uint32_t>(n),{p.bytes.begin()+n,p.bytes.end()}});extraSpans-=n;}
        boundary.progress.spans=std::move(split);boundary.nativeFingerprint=resyncNativeFingerprint(boundary);
        const auto full=encodeResyncSnapshot(boundary);
        check(full.size()==RESYNC_MAX_SNAPSHOT_BYTES&&decodeResyncSnapshot(full).nativeFingerprint==boundary.nativeFingerprint,
            "exact60000-byte native witness snapshot roundtrips without truncation");
        const auto plan=staticPlan();const auto begin=header(boundary,plan,77);ResyncAssembler assembler;bool staged=assembler.Begin(begin);
        const auto chunks=parts(begin,boundary);for(const auto& part:chunks)staged=assembler.Part(part)&&staged;
        const auto rebuilt=assembler.End(ending(begin));
        check(staged&&chunks.size()==4&&rebuilt&&rebuilt->nativeFingerprint==boundary.nativeFingerprint,
            "full witness crosses all four actual16KiB staging parts with exact content");
        auto oversized=full;oversized.push_back(0);
        check(throws([&]{(void)decodeResyncSnapshot(oversized);}),"60001-byte snapshot rejected by bounded decoder");
        row=boundary.enemies[0];row.identity.netId=3;row.record={0,2};boundary.enemies.push_back(row);++boundary.livingCount;
        check(throws([&]{(void)resyncNativeFingerprint(boundary);}),
            "count-valid catalog plus row that exceeds60000 bytes fails before fingerprint or serialization");
    }
}
struct Seen {std::vector<ResyncPlan> plans;std::vector<ResyncBegin> begins;std::vector<ResyncSnapshot> snapshots;std::vector<ResyncResult> results;std::vector<EnemyHp> hp;std::vector<EventHold> holds;std::vector<WorldEnvelope> envelopes;std::vector<std::string> order;std::uint32_t progress=0;};
ClientCallbacks callbacks(Seen& s){ClientCallbacks c;c.onResyncPlan=[&](const auto& p){s.plans.push_back(p);};c.onResyncSnapshot=[&](const auto& b,const auto& v){s.begins.push_back(b);s.snapshots.push_back(v);};c.onResyncResult=[&](const auto& r){s.results.push_back(r);};c.onWorldEnvelope=[&](const auto& e){s.envelopes.push_back(e);s.order.push_back("envelope");};c.onWorldPacket=[&](const auto&){s.order.push_back("raw");};c.onEnemyHp=[&](const auto& h){s.hp.push_back(h);};c.onEventHold=[&](const auto& h){s.holds.push_back(h);};c.onProgressUpdate=[&](const auto& p){s.progress=p.version;};return c;}
struct Rig {
    SessionConfig cfg;std::unique_ptr<SessionHost> relay;std::array<Seen,3> seen;std::array<std::unique_ptr<NetworkClient>,3> clients;std::uint32_t marker=100;
    explicit Rig(std::uint16_t port){cfg.bindAddress="127.0.0.1";cfg.port=port;cfg.gameBuild="forced-resync-v8";cfg.modHash="m";cfg.contentHash="c";relay=std::make_unique<SessionHost>(cfg);check(relay->start(),"resync relay starts on loopback");for(std::uint8_t slot=0;slot<3;++slot){clients[slot]=std::make_unique<NetworkClient>("127.0.0.1",port,cfg.gameBuild,cfg.modHash,"peer"+std::to_string(slot),static_cast<SlotType>(slot),callbacks(seen[slot]),RuntimeMode::CampaignCoop,cfg.contentHash);clients[slot]->connect();}}
    ~Rig(){for(auto& c:clients)c->disconnect();relay->stop();}
    void pump(){relay->tick(0);for(auto& c:clients){c->tick(0);c->sendHeartbeat();}}
    bool wait(const std::function<bool()>& predicate){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(4);while(!predicate()&&std::chrono::steady_clock::now()<end){pump();std::this_thread::sleep_for(std::chrono::milliseconds(1));}return predicate();}
    bool ready(){return wait([&]{return clients[0]->worldReady()&&clients[1]->worldReady()&&clients[2]->worldReady();});}
    bool barrier(){const auto n=++marker;clients[0]->sendProgressUpdate({n,false,{}});return wait([&]{return seen[1].progress==n&&seen[2].progress==n;});}
    bool prime(){if(!ready())return false;auto s=snapshot();clients[0]->sendRoomTransition(s.room);clients[0]->sendEventHold(s.hold);EnemyManifest m;m.epoch=7;m.entries={s.enemies[0].identity};clients[0]->sendEnemyManifest(m);clients[0]->sendEnemyHp({7,{{1,22,100}},1});return wait([&]{return !seen[1].hp.empty()&&!seen[2].hp.empty();})&&barrier();}
    bool begin(std::uint8_t mask=6){return clients[0]->requestWorldResync(mask)&&wait([&]{if(!clients[0]->pendingResync())return false;for(std::size_t i=1;i<3;++i)if((mask&(1u<<i))&&!clients[i]->pendingResync())return false;return true;});}
    bool capture(const ResyncSnapshot& s){const auto context=clients[0]->makeTestingWorldContext();const auto plan=*clients[0]->pendingResync();const auto b=header(s,plan,context.hostSourceSerial);const auto n1=seen[1].snapshots.size(),n2=seen[2].snapshots.size();return clients[0]->sendResyncCapture(b,s,context)&&wait([&]{return (!(plan.request.targetMask&2)||seen[1].snapshots.size()==n1+1)&&(!(plan.request.targetMask&4)||seen[2].snapshots.size()==n2+1);});}
    ResyncAck ack(std::size_t slot,ResyncAckStatus status=ResyncAckStatus::Converged){const auto& b=seen[slot].begins.back();const auto& s=seen[slot].snapshots.back();ResyncAck a;a.key=b.key;a.phase=b.phase;a.snapshotCut=b.snapshotCut;a.snapshotSha256=b.sha256;a.observedFingerprint=s.nativeFingerprint;a.status=status;for(std::size_t i=0;i<b.targetCount;++i)if(b.targets[i].slot==slot)a.target=b.targets[i];a.observedRoom=s.room;a.enemyCount=s.livingCount;a.deadCount=s.deadCount;a.loadBefore=3;a.loadAfter=4;a.observationFrame1=200;a.observationFrame2=201;a.checksMask=ResyncChecksComplete;return a;}
    bool sendAck(std::size_t slot,ResyncAckStatus status=ResyncAckStatus::Converged){return clients[slot]->sendResyncAck(ack(slot,status),clients[slot]->makeTestingWorldContext());}
    bool result(ResyncResultReason why){return wait([&]{return !seen[0].results.empty()&&seen[0].results.back().reason==why;});}
};
void loopbackSuccess(bool empty,std::uint16_t port){
    Rig r(port);const bool primed=r.prime();check(primed,"real three-peer scoped world setup positively received");if(!primed)return;
    const auto staleContext=r.clients[0]->makeTestingWorldContext();const auto stalePacket=encode(EventHold{7,true,0});const auto oldSelf=r.clients[1]->makeTestingWorldContext();const auto oldSerial=r.clients[1]->deliverySerial();
    check(!r.clients[1]->requestWorldResync(6),"friend cannot request host capture");const bool started=r.begin();check(started,"host request freezes both admitted friends");if(!started)return;
    const auto plan=*r.clients[0]->pendingResync();const auto deadline=r.relay->resyncDeadlineMs();
    check(plan.targetCount==2&&plan.targets[0].connectionId==r.clients[1]->worldBinding()->selfConnectionId&&plan.targets[1].connectionId==r.clients[2]->worldBinding()->selfConnectionId&&r.clients[1]->deliverySerial()>oldSerial&&!r.clients[1]->worldReady(),"fixed full denominator and new delivery serial fence before capture");
    check(!r.clients[1]->sendNativeWorld(encode(StateHash{7,4,26,1,1}),oldSelf,true),"retained old reverse packet cannot be relabeled with current serial");
    check(r.clients[0]->sendResyncRequest(plan.request),"identical host request may be retransmitted");
    auto s=snapshot(!empty);check(r.capture(s),"actual codec multipart transport publishes fresh complete snapshot to every target");if(r.seen[1].snapshots.empty()||r.seen[2].snapshots.empty())return;
    check(r.relay->resyncDeadlineMs()==deadline&&r.relay->activeResyncPlan()&&!r.relay->lastResyncResult(),"duplicate request and complete receipt do not renew deadline or imply native success");
    check(r.seen[1].snapshots.back().enemies.size()==s.enemies.size()&&r.seen[2].snapshots.back().nativeFingerprint==s.nativeFingerprint,"nonempty/explicit-empty full snapshot preserved for both targets");
    check(r.sendAck(1,ResyncAckStatus::Received)&&r.sendAck(2,ResyncAckStatus::Arrived)&&r.barrier()&&r.relay->activeResyncPlan()&&!r.relay->lastResyncResult(),"receipt and arrival acknowledgments cannot complete native convergence");
    check(r.sendAck(1)&&r.barrier()&&r.relay->activeResyncPlan(),"one synthetic native convergence cannot shrink two-target denominator");
    check(r.sendAck(2)&&r.result(ResyncResultReason::Converged),"both matching synthetic native witnesses complete transport transaction");
    check(r.seen[0].results.back().targetCount==2&&r.seen[0].results.back().targets[0].target==plan.targets[0]&&r.seen[0].results.back().targets[1].target==plan.targets[1],"terminal result retains exact original target identities and serials");
    const auto before=r.seen[1].holds.size();check(r.clients[0]->sendNativeWorld(stalePacket,staleContext,true)&&r.barrier()&&r.seen[1].holds.size()==before,"delayed pre-cut host packet remains retired after terminal success");
    // Actual rejoin probes rebuilt cache without pretending it is a new capture.
    r.clients[2]->disconnect();check(r.wait([&]{return r.relay->verifiedPeerCount()==2;}),"target leaves after result before independent cache probe");r.seen[2].hp.clear();r.seen[2].holds.clear();r.clients[2]->connect();check(r.wait([&]{return r.clients[2]->worldReady();})&&r.barrier(),"fresh friend reconnect receives current scoped bootstrap behind barrier");
    check(empty?r.seen[2].hp.empty():(!r.seen[2].hp.empty()&&r.seen[2].hp.back().entries[0].hp==73&&r.seen[2].hp.back().sequence==10),"late cache is refreshed from captured snapshot with exact HP source sequence");check(r.seen[2].holds.size()==1&&!r.seen[2].holds[0].active,"late cache never replays retired pre-cut event hold");
    bool paired=true;for(const auto& seen:r.seen){if(seen.order.size()%2)paired=false;for(std::size_t i=0;i+1<seen.order.size();i+=2)paired=paired&&seen.order[i]=="envelope"&&seen.order[i+1]=="raw";}check(paired,"captured envelope callback precedes every admitted raw world callback");
}
void checkpoint(bool changeAgain,std::uint16_t port){
    Rig r(port);if(!r.prime()||!r.begin()) {check(false,"checkpoint setup");return;}const auto original=*r.clients[0]->pendingResync();const auto deadline=r.relay->resyncDeadlineMs();if(!r.capture(snapshot())){check(false,"checkpoint bootstrap capture");return;}
    r.clients[0]->sendEnemyHp({7,{{1,61,100}},11});check(r.wait([&]{return r.seen[1].hp.back().entries[0].hp==61;})&&r.sendAck(1),"real post-cut material update precedes initial synthetic convergence");
    check(r.wait([&]{return r.clients[0]->pendingResync()&&r.clients[0]->pendingResync()->phase==ResyncPhase::Checkpoint;}),"changed host state requests exactly one fresh checkpoint");
    if(!r.clients[0]->pendingResync()||r.clients[0]->pendingResync()->phase!=ResyncPhase::Checkpoint)return;
    check(r.clients[0]->pendingResync()->request.key==original.request.key&&r.clients[0]->pendingResync()->targets==original.targets&&r.relay->resyncDeadlineMs()==deadline,"checkpoint preserves key targets serials and original deadline");
    auto s=snapshot(true,61);s.hpSequence=11;check(r.capture(s),"fresh checkpoint transports updated canonical state without second bootstrap");
    if(changeAgain){r.clients[0]->sendEnemyHp({7,{{1,60,100}},12});check(r.result(ResyncResultReason::NativeFailed),"material change after checkpoint cut fails instead of infinite recapture");}
    else{check(r.sendAck(1)&&r.sendAck(2)&&r.result(ResyncResultReason::Converged),"unchanged checkpoint plus both synthetic witnesses converges at new cut");}
}
void terminalControls(std::uint16_t port,unsigned mode){
    Rig r(port);if(!r.prime()||!r.begin()){check(false,"terminal setup");return;}const auto plan=*r.clients[0]->pendingResync();const auto deadline=r.relay->resyncDeadlineMs();
    if(mode==0){r.relay->pumpResync(deadline-1);check(r.relay->activeResyncPlan().has_value(),"original deadline not early");r.relay->pumpResync(deadline);check(r.result(ResyncResultReason::Deadline),"no capture finishes with explicit original deadline");}
    if(mode==1){r.clients[1]->disconnect();check(r.result(ResyncResultReason::TargetChanged),"selected connection departure cancels, never substitutes fresh slot");}
    if(mode==2){r.clients[0]->sendRoomTransition({8,4,27,0,0,3,0});check(r.result(ResyncResultReason::RoomChanged),"room change cancels original room transaction");}
    if(mode==3){ResyncResult unavailable;unavailable.key=plan.request.key;unavailable.reason=ResyncResultReason::CaptureUnavailable;unavailable.targetCount=plan.targetCount;for(std::size_t i=0;i<plan.targetCount;++i)unavailable.targets[i].target=plan.targets[i];check(r.clients[0]->sendResyncFailure(unavailable,r.clients[0]->makeTestingWorldContext())&&r.result(ResyncResultReason::CaptureUnavailable),"checked capture unavailable remains explicit terminal failure");}
    if(mode==4){if(!r.capture(snapshot())){check(false,"native unavailable setup");return;}auto a=r.ack(1,ResyncAckStatus::Unavailable);a.error="synthetic unsupported native witness";check(r.clients[1]->sendResyncAck(a,r.clients[1]->makeTestingWorldContext())&&r.result(ResyncResultReason::NativeUnavailable),"native unavailable cannot be replaced by received snapshot");}
    if(mode==5){if(!r.capture(snapshot())){check(false,"bad native witness setup");return;}auto a=r.ack(1);a.observationFrame2=a.observationFrame1;check(r.clients[1]->sendResyncAck(a,r.clients[1]->makeTestingWorldContext())&&r.result(ResyncResultReason::NativeFailed),"same frame repeated cannot certify two fresh observations");}
    if(mode==6){const auto c=r.clients[0]->makeTestingWorldContext();const auto s=snapshot();const auto b=header(s,plan,c.hostSourceSerial);r.clients[0]->disconnect();check(!r.clients[0]->sendResyncCapture(b,s,c),"disconnected capture submission reports failure instead of fake completion");check(r.wait([&]{return r.relay->lastResyncResult().has_value();})&&r.relay->lastResyncResult()->reason==ResyncResultReason::HostChanged,"host loss ends transaction and retains failed original targets");}
    if(r.relay->lastResyncResult())check(r.relay->lastResyncResult()->targetCount==plan.targetCount&&r.relay->lastResyncResult()->targets[0].target==plan.targets[0]&&r.relay->lastResyncResult()->targets[1].target==plan.targets[1],"failure result preserves denominator including missing peers");else check(false,"terminal result exists");
}

void conditionedRetirement(){
    Rig r(17844);if(!r.prime()){check(false,"conditioned retirement setup");return;}
    const auto beforeAvatar=r.relay->relayedAvatarCount();
    r.clients[0]->setLinkConditions({800,0,0.0f,88},{});
    const auto heldContext=r.clients[0]->makeTestingWorldContext();
    check(r.clients[0]->sendNativeWorld(encode(EventHold{7,true,0}),heldContext,true),"old hold physically enters production outbound conditioner");
    AvatarState witness;witness.ownerSlot=SlotType::Player;witness.seq=9001;witness.serverTimeMs=1;
    const auto pendingBeforeWitness = r.clients[0]->outbound_.pending();
    r.clients[0]->sendRawPacket(encode(witness, PacketType::AvatarState), true);
    check(pendingBeforeWitness >= 1 && r.clients[0]->outbound_.pending() == pendingBeforeWitness + 1,
          "reliable same-channel avatar witness is enqueued behind the retained old hold");
    r.clients[0]->setLinkConditions({},{});
    check(r.begin()&&r.capture(snapshot()),"fresh transaction overtakes deliberately delayed old producer bytes");
    if(r.seen[1].snapshots.empty())return;
    check(r.sendAck(1)&&r.sendAck(2)&&r.result(ResyncResultReason::Converged),"synthetic native convergence completes before delayed stale flush");
    const auto holdCount=r.seen[1].holds.size();
    const bool flushed = r.wait([&]{return r.relay->relayedAvatarCount()>beforeAvatar;});
    check(flushed,"queued reliable avatar proves old same-channel conditioner batch actually flushed");
    const bool orderedBarrier = r.barrier();
    check(orderedBarrier,"fresh reliable progress barrier reaches both peers after old conditioner flush");
    check(r.seen[1].holds.size()==holdCount,"retired old hold never changes post-cut target callback count");
    std::cout << "conditioned-retirement diagnostics avatarBefore=" << beforeAvatar
              << " avatarAfter=" << r.relay->relayedAvatarCount() << " holdBefore=" << holdCount
              << " holdAfter=" << r.seen[1].holds.size() << " marker=" << r.marker
              << " peer1Progress=" << r.seen[1].progress << " peer2Progress=" << r.seen[2].progress
              << " retainedSource=" << heldContext.hostSourceSerial << '\n';
    const auto before=r.relay->rejectedWorldMessages();
    auto scope=r.clients[1]->worldBinding().value();
    WorldScope forged{scope.sessionId,scope.selfConnectionId,scope.deliverySerial-1,0,0,0};
    check(worldfixture::raw(*r.clients[1],worldfixture::uncheckedEnvelope(forged,encode(StateHash{7,4,26,123,456})))&&r.barrier()&&r.relay->rejectedWorldMessages()==before+1,"actual authenticated old reverse serial is rejected at relay, not merely local send gate");
}
void singleTargetAndOverflow(bool overflow,std::uint16_t port){
    Rig r(port);if(!r.prime()){check(false,"single target setup");return;}const auto otherSerial=r.clients[2]->deliverySerial();
    check(r.begin(2),"single friend target plan admitted");if(!r.clients[0]->pendingResync())return;
    check(r.clients[0]->pendingResync()->targetCount==1&&r.clients[2]->deliverySerial()==otherSerial&&r.clients[2]->worldReady()&&!r.clients[2]->pendingResync(),"unselected friend retains serial and active authority");
    if(overflow){for(unsigned i=0;i<=RESYNC_MAX_CONTINUATION_RECORDS;++i)r.clients[0]->sendProgressUpdate({1000+i,false,{{0x10,{static_cast<std::uint8_t>(i)}}}});check(r.result(ResyncResultReason::Overflow)&&r.seen[0].results.back().targetCount==1,"bounded post-cut continuation overflow cannot silently become complete");return;}
    check(r.capture(snapshot())&&r.sendAck(1)&&r.result(ResyncResultReason::Converged),"exact single target can complete without inventing a second acknowledgment");
    check(r.seen[2].snapshots.empty()&&r.clients[2]->deliverySerial()==otherSerial,"unselected peer never receives reset snapshot");
}


void wireStaging(unsigned mode,std::uint16_t port){
    Rig r(port);if(!r.prime()||!r.begin()){check(false,"wire staging setup");return;}
    const auto context=r.clients[0]->makeTestingWorldContext();const auto plan=*r.clients[0]->pendingResync();const auto s=snapshot();const auto b=header(s,plan,context.hostSourceSerial);const auto ps=parts(b,s);
    const auto hostBarrier=[&]{const auto before=r.relay->peerBySlot(SlotType::Player)->hashReceiptSeq;r.clients[0]->sendNativeWorld(encode(StateHash{7,4,26,73,0}),r.clients[0]->makeTestingWorldContext(),true);return r.wait([&]{return r.relay->peerBySlot(SlotType::Player)->hashReceiptSeq>before;});};
    check(worldfixture::raw(*r.clients[0],encode(b)),"genuine ENet begins bounded staging");
    if(mode==0){check(hostBarrier()&&r.seen[1].snapshots.empty()&&r.seen[2].snapshots.empty(),"positive post-Begin barrier proves no prefix snapshot callback");worldfixture::raw(*r.clients[0],encode(ending(b)));check(r.result(ResyncResultReason::InvalidSnapshot),"actual End with missing parts terminates incomplete");}
    if(mode==1){for(const auto& p:ps)worldfixture::raw(*r.clients[0],encode(p));auto end=ending(b);end.sha256[0]^=1;worldfixture::raw(*r.clients[0],encode(end));check(r.result(ResyncResultReason::InvalidSnapshot),"actual fully received wrong digest never forwards snapshot");}
    if(mode==2){worldfixture::raw(*r.clients[0],encode(ps[0]));auto bad=ps[0];bad.bytes[0]^=1;worldfixture::raw(*r.clients[0],encode(bad));check(r.result(ResyncResultReason::InvalidSnapshot),"actual conflicting duplicate chunk terminates exact transaction");}
    if(mode==3){
        const auto fixedDeadline=r.relay->resyncDeadlineMs();
        for(const auto& p:ps){worldfixture::raw(*r.clients[0],encode(p));worldfixture::raw(*r.clients[0],encode(p));}
        check(hostBarrier()&&r.relay->resyncDeadlineMs()==fixedDeadline,"duplicate complete parts retain original fixed deadline before malformed End");
        auto end=encode(ending(b));end.pop_back();worldfixture::raw(*r.clients[0],end);
        check(r.result(ResyncResultReason::InvalidSnapshot),"malformed terminal is rejected immediately by exact frame gate");
        check(r.relay->lastResyncResult()&&r.relay->lastResyncResult()->targetCount==plan.targetCount&&
              r.relay->lastResyncResult()->targets[0].target==plan.targets[0]&&r.relay->lastResyncResult()->targets[1].target==plan.targets[1]&&
              r.seen[1].snapshots.empty()&&r.seen[2].snapshots.empty(),"malformed terminal preserves original denominator and publishes no prefix");
    }
    check(r.seen[1].snapshots.empty()&&r.seen[2].snapshots.empty(),"invalid transaction publishes no partial native snapshot to either target");
}


void simulationUnavailable(){
    Rig r(17851);if(!r.prime()){check(false,"simulation compatibility setup");return;}
    const auto serial1=r.clients[1]->deliverySerial(),serial2=r.clients[2]->deliverySerial();
    ActorSnapshot actor;actor.snapshotId=55;actor.actor.actorId=7;actor.actor.hp=43;r.relay->broadcastActorSnapshots({actor});
    check(r.wait([&]{return std::any_of(r.seen[1].envelopes.begin(),r.seen[1].envelopes.end(),[](const auto& e){return e.scope.kind==WorldSourceKind::Simulation;});}),"actual relay simulation envelope has explicit distinct source kind");
    check(r.clients[0]->requestWorldResync(6)&&r.result(ResyncResultReason::CaptureUnavailable),"active legacy simulation refuses fresh native capture explicitly");
    check(!r.relay->activeResyncPlan()&&r.clients[1]->deliverySerial()==serial1&&r.clients[2]->deliverySerial()==serial2&&r.clients[1]->worldReady()&&r.clients[2]->worldReady()&&r.seen[1].plans.empty()&&r.seen[2].plans.empty(),"simulation refusal occurs before fencing or mutating target authority");
    check(r.seen[0].results.back().targetCount==2&&r.seen[0].results.back().targets[0].target.deliverySerial==serial1&&r.seen[0].results.back().targets[1].target.deliverySerial==serial2,"unavailable result names both unchanged original target incarnations");
}

// Transport-only deadline controls: the existing injected client clock advances
// while genuine ENet packets pass through the actual production conditioners.
bool serverSend(Rig& r, std::size_t slot, const std::vector<std::uint8_t>& bytes) {
    const auto* peer = r.relay->peerBySlot(static_cast<SlotType>(slot));
    if (!peer) return false;
    auto* packet = enet_packet_create(bytes.data(), bytes.size(), ENET_PACKET_FLAG_RELIABLE);
    if (!packet) return false;
    if (enet_peer_send(peer->enetPeer, 0, packet) < 0) {
        enet_packet_destroy(packet); return false;
    }
    enet_host_flush(peer->enetPeer->host); return true;
}
bool clientMarker(Rig& r, std::size_t slot, std::uint32_t seq) {
    AvatarRelay marker;
    const auto owner = slot == 0 ? SlotType::Friend1 : SlotType::Player;
    marker.ownerConnectionId = r.relay->peerBySlot(owner)->connectionId;
    marker.avatar.ownerSlot = owner; marker.avatar.seq = seq;
    marker.avatar.serverTimeMs = seq;
    return serverSend(r,slot,encode(marker));
}
void completedReplayAndMixedCut(bool mixed, std::uint16_t port) {
    Rig r(port); if (!r.prime() || !r.begin() || !r.capture(snapshot())) {
        check(false,"immutable phase replay setup"); return;
    }
    const auto original = r.seen[1].begins.back();
    if (mixed) {
        const auto targetReceipt = r.relay->peerBySlot(SlotType::Friend1)->hashReceiptSeq;
        const bool ackSent = r.sendAck(1);
        const bool targetBarrierSent = r.clients[1]->sendNativeWorld(
            encode(StateHash{7,4,26,73,0}), r.clients[1]->makeTestingWorldContext(), true);
        check(ackSent && targetBarrierSent && r.wait([&] {
                  return r.relay->peerBySlot(SlotType::Friend1)->hashReceiptSeq > targetReceipt;
              }) && r.relay->activeResyncPlan(),
              "same-target reliable StateHash receipt proves first ACK admission before conflicting host Begin");
        const auto context = r.clients[0]->makeTestingWorldContext();
        auto different = header(snapshot(true,61),*r.clients[0]->pendingResync(),context.hostSourceSerial);
        check(worldfixture::raw(*r.clients[0],encode(different)) &&
              r.result(ResyncResultReason::InvalidSnapshot),
              "new same-phase Begin after target1 ACK cannot combine different cuts or fingerprints");
        check(r.seen[0].results.back().reason != ResyncResultReason::Converged &&
              r.seen[1].snapshots.size()==1 && r.seen[2].snapshots.size()==1,
              "mixed-cut attempt keeps original denominator and publishes no replacement snapshot");
    } else {
        std::uint32_t marker=0;
        r.clients[1]->callbacks_.onAvatarState=[&](const AvatarRelay& a){marker=a.avatar.seq;};
        check(serverSend(r,1,encode(original)),"actual relay-side exact completed Begin replay sent");
        for(const auto& part:parts(original,snapshot()))check(serverSend(r,1,encode(part)),"actual completed part replay sent");
        check(serverSend(r,1,encode(ending(original))) && clientMarker(r,1,9191) &&
              r.wait([&]{return marker==9191;}) && r.seen[1].snapshots.size()==1 && r.clients[1]->pendingResync(),
              "same-channel positive marker proves exact completed replay never invokes second snapshot callback");
        auto changed=original; ++changed.snapshotCut;
        check(serverSend(r,1,encode(changed)) && r.wait([&]{return !r.seen[1].results.empty();}) &&
              r.seen[1].results.back().reason==ResyncResultReason::InvalidSnapshot && r.seen[1].snapshots.size()==1,
              "client independently rejects changed Begin for already completed phase");
    }
}
void expiredInboundEnd() {
    Rig r(17854); if(!r.prime()||!r.begin()){check(false,"expired inbound End setup");return;}
    auto& client=*r.clients[1]; std::uint32_t marker=0;
    client.callbacks_.onAvatarState=[&](const AvatarRelay& a){marker=a.avatar.seq;};
    const auto context=r.clients[0]->makeTestingWorldContext();
    const auto b=header(snapshot(),*r.clients[0]->pendingResync(),context.hostSourceSerial);
    check(serverSend(r,1,encode(b)),"valid Begin reaches selected client");
    for(const auto& part:parts(b,snapshot()))serverSend(r,1,encode(part));
    check(clientMarker(r,1,9192)&&r.wait([&]{return marker==9192;})&&client.resyncAssembler_.Header(),
          "positive pre-End marker proves complete staging before deliberate End delay");
    client.setLinkConditions({}, {60000,0,0.0f,77});
    check(serverSend(r,1,encode(ending(b)))&&clientMarker(r,1,9193)&&
          r.wait([&]{return client.inbound_.pending()>=2;}),"actual End and later marker enter inbound conditioner");
    client.setClockSkewMs(70000);
    check(r.wait([&]{return marker==9193;})&&r.seen[1].snapshots.empty()&&
          !r.seen[1].results.empty()&&r.seen[1].results.front().reason==ResyncResultReason::Deadline,
          "expired conditioned End is processed behind deadline retirement with no native snapshot callback");
}
void expiredOutbound(unsigned mode,std::uint16_t port) {
    Rig r(port);if(!r.prime()){check(false,"expired outbound setup");return;}
    if(mode!=0 && (!r.begin() || (mode==2&&!r.capture(snapshot())))){check(false,"expired outbound plan setup");return;}
    const std::size_t sender=mode==2?1:0;auto& client=*r.clients[sender];
    client.setLinkConditions({60000,0,0.0f,91},{});
    bool queued=false;
    if(mode==0)queued=client.requestWorldResync(6);
    if(mode==1){const auto context=client.makeTestingWorldContext();const auto b=header(snapshot(),*client.pendingResync(),context.hostSourceSerial);queued=client.sendResyncCapture(b,snapshot(),context);}
    if(mode==2)queued=r.sendAck(1);
    // This reliable ordinary packet is the positive flush witness. It is not
    // a native/world success witness and cannot bypass resync gates.
    AvatarState marker;marker.ownerSlot=static_cast<SlotType>(sender);marker.seq=9292;marker.serverTimeMs=9292;
    client.sendRawPacket(encode(marker, PacketType::AvatarState),true);
    check(queued&&client.outbound_.pending()>=2,"resync control and reliable marker physically queued before original deadline");
    auto* serverHost=r.relay->peerBySlot(static_cast<SlotType>(sender))->enetPeer->host;
    client.setClockSkewMs(70000);
    client.tick(0);
    unsigned forbidden=0;bool sawMarker=false;
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(4);
    // Read the actual relay socket while the relay dispatcher is paused. This
    // isolates send-side expiry from the relay's independent terminal gate.
    while(!sawMarker&&std::chrono::steady_clock::now()<until){
        client.tick(0); ENetEvent event;
        while(enet_host_service(serverHost,&event,0)>0){
            if(event.type!=ENET_EVENT_TYPE_RECEIVE)continue;
            const auto type=event.packet->dataLength?static_cast<PacketType>(event.packet->data[0]):PacketType::Heartbeat;
            if(type==PacketType::ResyncRequest || (type>=PacketType::ResyncBegin&&type<=PacketType::ResyncAck))++forbidden;
            if(type==PacketType::AvatarState){const std::uint8_t* bytes=nullptr;std::size_t size=0;decodePacketHeader(event.packet->data,event.packet->dataLength,bytes,size);ByteReader reader(bytes,size);AvatarState a;read(reader,a);sawMarker=sawMarker||a.seq==9292;}
            enet_packet_destroy(event.packet);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(sawMarker&&forbidden==0,
          "actual reliable marker crosses socket while expired request/capture/normal ACK bytes never submit");
    check(!r.seen[sender].results.empty()&&r.seen[sender].results.front().reason==ResyncResultReason::Deadline,
          "outbound delayed transaction retains explicit original-deadline terminal result");
}
void initialExpiryLatePlan() {
    Rig r(17858);if(!r.prime()){check(false,"initial expiry setup");return;}
    auto& host=*r.clients[0];host.setLinkConditions({60000,0,0.0f,41},{});
    check(host.requestWorldResync(6)&&host.requestedResync_,"host request awaits first plan with original local deadline");
    if(!host.requestedResync_)return;
    ResyncPlan late;late.request=*host.requestedResync_;late.targetCount=2;late.remainingMs=RESYNC_TIMEOUT_MS;
    late.targets={ResyncTarget{1,late.request.connections[1],2},ResyncTarget{2,late.request.connections[2],2}};
    const auto id=late.request.key.requestId;
    host.setClockSkewMs(70000);host.tick(0);
    check(!host.pendingResync()&&!host.requestedResync_&&host.lastResyncPlanId_>=id,
          "initial request expiry consumes high-water before any plan exists");
    host.setLinkConditions({},{});std::uint32_t marker=0;
    host.callbacks_.onAvatarState=[&](const AvatarRelay& a){marker=a.avatar.seq;};
    const auto oldDeadline=host.resyncDeadline_;
    check(serverSend(r,0,encode(late))&&clientMarker(r,0,9393)&&r.wait([&]{return marker==9393;})&&
          !host.pendingResync()&&r.seen[0].plans.empty()&&host.resyncDeadline_==oldDeadline,
          "late first Plan behind reliable marker cannot reopen consumed request or renew deadline");
}

void legacy(){SessionConfig cfg;cfg.bindAddress="127.0.0.1";cfg.port=17843;cfg.gameBuild="legacy";cfg.modHash="m";cfg.contentHash="c";SessionHost relay(cfg);check(relay.start(),"version10 relay gate starts");std::string reason;bool closed=false;ClientCallbacks cb;cb.onRejected=[&](const HelloReject& r){reason=r.reason;};cb.onClosed=[&](const auto&){closed=true;};NetworkClient old("127.0.0.1",cfg.port,cfg.gameBuild,cfg.modHash,"old8",SlotType::Friend1,cb,RuntimeMode::CampaignCoop,cfg.contentHash,8);old.connect();const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(4);while(!closed&&std::chrono::steady_clock::now()<until){relay.tick(0);old.tick(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}check(PROTOCOL_VERSION==10&&closed&&reason=="Protocol mismatch: client=8 server=10"&&relay.verifiedPeerCount()==0,"v8 cannot negotiate snapshots without native record-content witness semantics");old.disconnect();relay.stop();}
}
int main(){if(enet_initialize()!=0)return 2;codecAndStaging();recordContentCodec();loopbackSuccess(false,17831);loopbackSuccess(true,17832);checkpoint(false,17833);checkpoint(true,17834);for(unsigned i=0;i<7;++i)terminalControls(static_cast<std::uint16_t>(17835+i),i);conditionedRetirement();singleTargetAndOverflow(false,17845);singleTargetAndOverflow(true,17846);for(unsigned i=0;i<4;++i)wireStaging(i,static_cast<std::uint16_t>(17847+i));simulationUnavailable();completedReplayAndMixedCut(false,17852);completedReplayAndMixedCut(true,17853);expiredInboundEnd();for(unsigned i=0;i<3;++i)expiredOutbound(i,static_cast<std::uint16_t>(17855+i));initialExpiryLatePlan();legacy();enet_deinitialize();std::cout<<checks-failures<<" PASS, "<<failures<<" FAIL\n";return failures?1:0;}
