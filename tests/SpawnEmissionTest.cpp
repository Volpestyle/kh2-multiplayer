#include "kh2coop/Codec.hpp"
#include "kh2coop/SpawnEmissionLedger.hpp"
#include "WorldWireFixture.hpp"
#include "kh2coop/SessionHost.hpp"
#include <enet/enet.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <limits>
#include <memory>
#include <thread>
using namespace kh2coop;
namespace {
int errors = 0;
void check(bool ok, const char* label) { std::cout << (ok ? "PASS: " : "FAIL: ") << label << '\n'; if (!ok) ++errors; }
SpawnEmission sample() { return {{7,4,0x15,0,0,26,0},1,1234,750,0,{-7,0,-405,1}}; }
template<class F> bool throws(F f) { try { f(); return false; } catch (const std::exception&) { return true; } }
SpawnEmission decode(const std::vector<std::uint8_t>& bytes) {
    const std::uint8_t* p; std::size_t n;
    if (decodePacketHeader(bytes.data(),bytes.size(),p,n)!=PacketType::SpawnEmission) throw std::runtime_error("wrong type");
    ByteReader r(p,n); SpawnEmission out; read(r,out); return out;
}
void codec() {
    auto e = sample(); auto bytes = encode(e);
    const std::uint8_t* p; std::size_t n;
    check(PROTOCOL_VERSION==38 && decodePacketHeader(bytes.data(),bytes.size(),p,n)==PacketType::SpawnEmission && n==SPAWN_EMISSION_BYTES && n==71,
          "protocol38 emission packet50 has exact 71-byte payload");
    check(decode(bytes).netId==0, "existing aggregate initializer remains a helper with no combat identity");
    for (std::uint16_t id : {std::uint16_t{1},std::uint16_t{0x8000},std::uint16_t{0xFFFF}}) {
        e.netId=id; bytes=encode(e); auto out=decode(bytes);
        check(out.netId==id && encode(out)==bytes && bytes[51]==static_cast<std::uint8_t>(id) && bytes[52]==static_cast<std::uint8_t>(id>>8),
              "full uint16 combat identity round trips after the historical point");
    }
    check(isScopedWorldPacket(PacketType::SpawnEmission) && isMaterialWorldPacket(PacketType::SpawnEmission) && validateScopedWorldPacket(bytes)==PacketType::SpawnEmission,
          "emissions retain authenticated material/cache transport policy");
    for (int delta : {-1,1}) {
        auto bad=bytes; if(delta<0)bad.pop_back();else bad.push_back(0);
        check(throws([&]{decode(bad);}), "truncated and trailing frames refused");
    }
    auto legacy=bytes;legacy.resize(51);legacy[1]=48;
    check(throws([&]{decode(legacy);}), "legacy v20/v21 payload without exact combat identity refused");
    e.sequence=0; check(throws([&]{encode(e);}), "zero sequence refused");
    e=sample();e.recordIndex=256; check(throws([&]{encode(e);}), "out of range record refused");
    e=sample();e.point[0]=std::numeric_limits<float>::quiet_NaN(); check(throws([&]{encode(e);}), "nonfinite trigger refused");
}
void ledger() {
    SpawnEmissionLedger l; auto a=sample();a.netId=7;
    auto b=a;b.sequence=2;b.recordIndex=1;b.point[2]=-493;b.netId=0xFFFF;
    check(!l.Permit(a.controllerKey,a.headerId,0,false) && !l.Pending(a.controllerKey,a.headerId,0), "no birth identity without announcement");
    check(l.Add(a)&&l.Add(b)&&!l.Add(a), "reliable replay does not duplicate permission");
    auto pendingDuplicate=b;pendingDuplicate.sequence=3;
    check(!l.Add(pendingDuplicate), "new sequence cannot duplicate a pending combat occurrence");
    const auto* first=l.Pending(a.controllerKey,750,0);
    check(first && first->point[2]==-405 && first->netId==7, "exact FIFO receipt exposes historical point and host occurrence");
    check(!l.Pending(a.controllerKey,750,1) && !l.Pending(a.controllerKey+1,750,0) && !l.Pending(a.controllerKey,751,0),
          "exact lookup cannot skip FIFO or cross controller/header identity");
    check(l.Permit(a.controllerKey,750,0,false)&&l.Permit(a.controllerKey,750,0,false) && l.Pending(a.controllerKey,750,0)->netId==7,
          "null allocation retries retain the exact same unconsumed combat ticket");
    check(l.Permit(a.controllerKey,750,0,true)&&!l.Permit(a.controllerKey,750,0,false)&&!l.Pending(a.controllerKey,750,0),
          "successful birth consumes its receipt once and exposes no consumed ticket");
    auto duplicate=a;duplicate.sequence=3;duplicate.controllerKey++;
    check(!l.Add(duplicate), "new sequence cannot regrant consumed combat occurrence through another controller");
    check(l.Pending(a.controllerKey,750,1)->netId==0xFFFF && l.Permit(a.controllerKey,750,1,true), "full uint16 next occurrence has its own one-use identity");
    a.sequence=3;a.netId=8;check(l.Add(a)&&l.Pending(a.controllerKey,750,0)->netId==8&&l.Permit(a.controllerKey,750,0,true),
                               "same-record refill requires and preserves a distinct host combat netId");
    auto helper=sample();helper.sequence=4;
    check(l.Add(helper)&&l.Pending(helper.controllerKey,helper.headerId,0)->netId==0&&l.Permit(helper.controllerKey,helper.headerId,0,true),
          "helper receipt grants no combat netId");
    helper.sequence=5;check(l.Add(helper)&&l.Permit(helper.controllerKey,helper.headerId,0,true), "zero helper identities remain repeatable across distinct receipts");
    l.Clear();check(!l.Pending(a.controllerKey,750)&&l.Add(a), "room reset retires receipt history including combat identity deduplication");
    l.Clear();helper=sample();for(std::uint64_t i=1;i<=SPAWN_EMISSION_CAP;++i){helper.sequence=i;l.Add(helper);}
    helper.sequence=SPAWN_EMISSION_CAP+1;check(!l.Add(helper), "bounded receipt storage refuses overflow");
}
void reservations() {
    SpawnEmissionLedger l;auto a=sample();a.netId=7;auto b=a;b.sequence=2;b.recordIndex=1;b.netId=8;
    l.Add(a);l.Add(b);
    check(!l.Commit(a.sequence)&&!l.Cancel(a.sequence), "unreserved receipts cannot be committed or cancelled");
    check(!l.Reserve(a.controllerKey,a.headerId,1)&&!l.Reserve(a.controllerKey+1,a.headerId,0), "reservation checks exact FIFO record and controller");
    const auto* pending=l.Reserve(a.controllerKey,a.headerId,a.recordIndex);
    const auto token=pending?*pending:SpawnEmission{};
    check(pending&&token.sequence==a.sequence&&token.netId==7, "reservation captures immutable exact birth token before native call");
    auto other=a;other.sequence=3;other.netId=9;other.controllerKey++;
    check(l.Add(other), "another announcement can arrive while a birth token is in flight");
    const auto* nested=l.Reserve(other.controllerKey,other.headerId,other.recordIndex);
    const auto nestedSequence=nested?nested->sequence:0;
    check(nested&&nested->netId==9, "nested different controller reserves its own receipt without changing outer token");
    check(!l.Reserve(a.controllerKey,a.headerId,0)&&!l.Reserve(a.controllerKey,a.headerId,1), "nested reserve cannot share reserved head or skip to next record");
    check(!l.Permit(a.controllerKey,a.headerId,0,false)&&!l.Permit(a.controllerKey,a.headerId,0,true), "ordinary Permit cannot borrow or consume in-flight reservation");
    check(!l.Commit(b.sequence)&&!l.Cancel(b.sequence)&&l.Pending(a.controllerKey,a.headerId)->sequence==token.sequence,
          "wrong return token cannot consume or release another birth");
    check(l.Cancel(token.sequence)&&!l.Cancel(token.sequence), "normal null releases exact reservation once");
    pending=l.Reserve(a.controllerKey,a.headerId,0);
    check(pending&&pending->sequence==token.sequence&&pending->netId==token.netId, "retry after null allocation reserves original exact birth identity");
    check(l.Commit(token.sequence)&&!l.Commit(token.sequence)&&!l.Cancel(token.sequence), "nonnull native return commits captured token exactly once");
    pending=l.Reserve(b.controllerKey,b.headerId,b.recordIndex);
    check(pending&&pending->netId==8&&l.Commit(b.sequence), "next birth becomes reservable only after prior commit");
    check(l.Commit(nestedSequence)&&!l.Commit(nestedSequence), "nested return commits only its own captured sequence");
    auto helper=sample();helper.sequence=4;l.Add(helper);pending=l.Reserve(helper.controllerKey,helper.headerId,0);
    check(pending&&pending->netId==0, "reserved helper token retains zero combat authority");
    l.Clear();check(!l.Commit(helper.sequence)&&!l.Cancel(helper.sequence)&&!l.Pending(helper.controllerKey,helper.headerId),
                    "room retirement discards in-flight reservations");
}

// The ScriptActor branch shares the existing receipt vector. These controls
// exercise pure wire/receipt facts; no native constructor or ownership is implied.
ScriptCallEntry scriptBirth(std::uint32_t child=7,std::uint64_t occurrence=0x80000001FFFFFFFFull,
                           std::uint16_t parent=1,bool end=false) {
    ScriptCallEntry entry{};entry.netId=parent;entry.objectId=SCRIPT_ACTOR_BIRTH_PARENT_OBJECT;
    entry.table=255;entry.index=end?SCRIPT_ACTOR_BIRTH_END:SCRIPT_ACTOR_BIRTH_BEGIN;entry.effect=child;
    const auto integer=[](std::uint32_t value){return (static_cast<std::uint64_t>(SCRIPT_TAG_INT)<<32)|value;};
    entry.args={integer(static_cast<std::uint32_t>(occurrence)),integer(static_cast<std::uint32_t>(occurrence>>32)),
                integer(child),integer(SCRIPT_ACTOR_BIRTH_CHILD_OBJECT)};return entry;
}
bool sameScriptBirth(const ScriptCallEntry& a,const ScriptCallEntry& b) {
    return a.netId==b.netId && a.objectId==b.objectId && a.table==b.table && a.index==b.index && a.effect==b.effect && a.args==b.args;
}
void scriptLedgerReservations() {
    using A=SpawnEmissionLedger::ScriptActorAdd;using S=SpawnEmissionLedger::ScriptActorReceiptState;
    using T=SpawnEmissionLedger::ScriptActorReservation;using C=SpawnEmissionLedger::ScriptActorCancel;
    SpawnEmissionLedger l;const auto a=scriptBirth();auto b=scriptBirth(8,42);ScriptCallEntry copied{};T outer{},nested{},retry{};
    copied=a;check(!l.PendingScriptActor(copied) && !copied.effect && l.ScriptActorState(a)==S::Missing,
                  "missing typed receipt returns no pointer or stale copied descriptor");
    check(l.AddScriptActor(a)==A::Added && l.AddScriptActor(a)==A::Duplicate && l.ScriptActorState(a)==S::Pending,
          "unsigned high-bit occurrence and exact duplicate create one pending script receipt");
    check(l.PendingScriptActor(copied) && sameScriptBirth(copied,a),"pending ScriptActor returns complete copied canonical BEGIN");
    check(!l.Pending(0,0) && !l.Reserve(0,0,0) && !l.Permit(0,0,0,false) && !l.Permit(0,0,0,true),
          "indexed zero-key lookups cannot expose a ScriptActor's default emission fields");
    check(l.ReserveScriptActor(a,outer) && outer.attempt && sameScriptBirth(outer.birth,a),"reserve copies exact descriptor plus local attempt");
    auto refused=outer;
    check(!l.ReserveScriptActor(a,refused) && !refused.attempt && !refused.birth.effect,"nested same receipt cannot borrow reservation or stale output");
    check(!l.Commit(0) && !l.Cancel(0) && l.ScriptActorState(a)==S::Reserved,"indexed sequence0 cannot mutate a reserved typed row");
    check(l.AddScriptActor(a)==A::Duplicate && l.ScriptActorState(a)==S::Reserved,"duplicate begin does not release a reservation");
    auto wrong=outer;++wrong.attempt;
    check(!l.CommitScriptActor(wrong) && !l.CancelScriptActor(wrong,C::QualifiedNormalNull),"wrong attempt cannot spend or release current receipt");
    wrong=outer;wrong.birth.args[0]^=1;
    check(!l.CommitScriptActor(wrong),"matching attempt with wrong immutable descriptor refuses");
    check(!l.CancelScriptActor(outer,static_cast<C>(1)) && l.ScriptActorState(a)==S::Reserved,"unknown cancel reason is not normal-null proof");
    check(l.AddScriptActor(b)==A::Added && l.ReserveScriptActor(b,nested),"different exact receipt reserves independently after vector growth");
    check(l.CommitScriptActor(outer) && !l.CommitScriptActor(outer) && !l.CancelScriptActor(outer,C::QualifiedNormalNull),
          "nonnull or ambiguous native return spends only its copied receipt once");
    check(l.AddScriptActor(a)==A::Duplicate && l.ScriptActorState(a)==S::Consumed,"consumed duplicate never regrants a birth");
    check(l.CancelScriptActor(nested,C::QualifiedNormalNull) && !l.CancelScriptActor(nested,C::QualifiedNormalNull),
          "qualified side-effect-free normal-null releases exactly once");
    check(l.ReserveScriptActor(b,retry) && retry.attempt>nested.attempt,"normal-null retry gets a new monotonic attempt");
    check(!l.CommitScriptActor(nested) && !l.CancelScriptActor(nested,C::QualifiedNormalNull) && l.ScriptActorState(b)==S::Reserved,
          "stale pre-null callback cannot consume or cancel a later same-descriptor attempt");
    check(l.CommitScriptActor(retry) && l.ScriptActorState(b)==S::Consumed,"only current retry token commits");
    l.Clear();check(l.AddScriptActor(a)==A::Added && l.ReserveScriptActor(a,retry) && retry.attempt>nested.attempt,
                    "room clear does not rewind the local attempt sequence");
    check(!l.CommitScriptActor(outer) && !l.CancelScriptActor(outer,C::QualifiedNormalNull),"pre-Clear token cannot hit a new-room identical descriptor");
    auto end=a;end.index=SCRIPT_ACTOR_BIRTH_END;
    check(l.AddScriptActor(end)==A::Ended && l.ScriptActorState(a)==S::Ended && l.CommitScriptActor(retry),
          "end during native entry stops new birth but still permits exact in-flight spend bookkeeping");
    check(!l.ReserveScriptActor(a,refused) && l.AddScriptActor(a)==A::Ended && l.AddScriptActor(end)==A::Ended,
          "end does not reset consumed history or permit resurrection");
    l.Clear();check(l.AddScriptActor(end)==A::Ended && l.AddScriptActor(a)==A::Ended && !l.PendingScriptActor(copied),
                    "end-before-start stores a permanent tombstone");
    check(!l.ReserveScriptActor(end,refused),"END operation itself cannot reserve a native birth");
    l.Clear();l.AddScriptActor(a);l.ReserveScriptActor(a,outer);l.AddScriptActor(end);
    check(l.CancelScriptActor(outer,C::QualifiedNormalNull) && l.ScriptActorState(a)==S::Ended && !l.ReserveScriptActor(a,retry),
          "normal-null after end closes reservation without reopening creation");
}
void scriptLedgerIsolationAndConflict() {
    using A=SpawnEmissionLedger::ScriptActorAdd;using S=SpawnEmissionLedger::ScriptActorReceiptState;
    using T=SpawnEmissionLedger::ScriptActorReservation;using C=SpawnEmissionLedger::ScriptActorCancel;
    SpawnEmissionLedger l;auto a=scriptBirth();T token{},otherToken{};
    check(l.AddScriptActor(a)==A::Added,"typed receipt starts independent namespace");
    for(int mode=0;mode<11;++mode) {
        auto bad=a;
        switch(mode) {
        case 0:bad.table=1;break;case 1:bad.index=1;break;case 2:bad.netId=0;break;
        case 3:bad.objectId=318;break;case 4:bad.effect=0;break;case 5:++bad.effect;break;
        case 6:bad.args[0]=static_cast<std::uint32_t>(bad.args[0]);break;
        case 7:bad.args[0]=static_cast<std::uint64_t>(SCRIPT_TAG_INT)<<32;bad.args[1]=bad.args[0];break;
        case 8:bad.args[3]=(static_cast<std::uint64_t>(SCRIPT_TAG_INT)<<32)|318;break;
        case 9:bad.netId=7;break;
        case 10:bad.effect=65536;bad.args[2]=(static_cast<std::uint64_t>(SCRIPT_TAG_INT)<<32)|65536;break;
        }
        check(l.AddScriptActor(bad)==A::Refused && l.ScriptActorState(a)==S::Pending,
              "shared helper rejects malformed source/tag/identity/policy without poisoning valid receipt");
    }
    auto indexed=sample();indexed.netId=7;
    check(l.Add(indexed) && l.Reserve(indexed.controllerKey,indexed.headerId,indexed.recordIndex) && l.ReserveScriptActor(a,token),
          "numeric indexed netId7 and script child7 coexist in the same vector");
    check(l.Commit(indexed.sequence) && l.ScriptActorState(a)==S::Reserved && l.CommitScriptActor(token),
          "indexed and typed commits touch only their respective kind");
    indexed.sequence=2;indexed.netId=8;
    check(l.Add(indexed),"script high occurrence bits never advance the indexed sequence floor");
    auto b=scriptBirth(8,42);check(l.AddScriptActor(b)==A::Added,"indexed netId8 does not deduplicate script child8");
    l.Clear();a=scriptBirth(7,10);b=scriptBirth(8,20);auto bridge=scriptBirth(7,20);
    l.AddScriptActor(a);l.AddScriptActor(b);l.ReserveScriptActor(a,token);
    check(l.AddScriptActor(bridge)==A::Conflict && l.ScriptActorState(a)==S::Poisoned &&
          l.ScriptActorState(b)==S::Poisoned && l.ScriptActorState(bridge)==S::Poisoned,
          "same child OR occurrence conflict poisons every retained match and retains incoming alias");
    check(l.CommitScriptActor(token) && !l.ReserveScriptActor(a,otherToken) && l.ScriptActorState(a)==S::Poisoned,
          "in-flight poisoned return is spent and never grants adoption authority");
    check(l.AddScriptActor(a)==A::Conflict && l.AddScriptActor(b)==A::Conflict,"poison survives exact duplicates and consumed history");
    auto linked=scriptBirth(9,10);
    check(l.AddScriptActor(linked)==A::Conflict && l.ScriptActorState(linked)==S::Poisoned,"retained conflict alias poisons later transitive occurrence reuse");
    auto changedParent=a;changedParent.netId=2;
    check(l.AddScriptActor(changedParent)==A::Conflict,"different historical parent cannot reuse child and occurrence");
    auto conflictingEnd=scriptBirth(11,20,1,true);
    check(l.AddScriptActor(conflictingEnd)==A::Conflict && l.ScriptActorState(conflictingEnd)==S::Poisoned,
          "new conflicting END reports conflict rather than mistaking its appended row for a preexisting exact end");
    auto exactEnd=a;exactEnd.index=SCRIPT_ACTOR_BIRTH_END;
    check(l.AddScriptActor(exactEnd)==A::Ended && l.AddScriptActor(exactEnd)==A::Ended && l.ScriptActorState(a)==S::Poisoned,
          "retained exact ends remain idempotent bookkeeping while poison remains sticky");
    l.Clear();l.AddScriptActor(a);l.ReserveScriptActor(a,token);l.AddScriptActor(changedParent);
    check(l.CancelScriptActor(token,C::QualifiedNormalNull) && l.ScriptActorState(a)==S::Poisoned && !l.ReserveScriptActor(a,otherToken),
          "qualified normal-null after conflict does not reopen poisoned receipt");
    l.Clear();l.AddScriptActor(a);l.ReserveScriptActor(a,token);l.CommitScriptActor(token);
    check(l.AddScriptActor(scriptBirth(7,99))==A::Conflict,"a new occurrence cannot reuse a consumed child identity");
    l.Clear();exactEnd=a;exactEnd.index=SCRIPT_ACTOR_BIRTH_END;l.AddScriptActor(exactEnd);
    check(l.AddScriptActor(scriptBirth(8,10))==A::Conflict,"an ended occurrence cannot be relabelled to another child");
}
void scriptLedgerCapacity() {
    using A=SpawnEmissionLedger::ScriptActorAdd;using S=SpawnEmissionLedger::ScriptActorReceiptState;
    using T=SpawnEmissionLedger::ScriptActorReservation;
    SpawnEmissionLedger l;auto a=scriptBirth(7,10);l.AddScriptActor(a);auto indexed=sample();
    bool filled=true;
    for(std::uint64_t i=1;i<SPAWN_EMISSION_CAP;++i){indexed.sequence=i;filled=filled && l.Add(indexed);}
    check(filled && l.AddScriptActor(scriptBirth(8,20))==A::Refused,"indexed and script receipts share one bounded capacity without eviction");
    check(l.AddScriptActor(a)==A::Duplicate,"full capacity retains exact duplicate idempotence");
    auto end=a;end.index=SCRIPT_ACTOR_BIRTH_END;
    check(l.AddScriptActor(end)==A::Ended && l.ScriptActorState(a)==S::Ended,"full capacity retains exact cleanup/end bookkeeping");
    const auto alias=scriptBirth(8,10);
    check(l.AddScriptActor(alias)==A::Conflict && l.ScriptActorState(a)==S::Poisoned && l.ScriptActorState(alias)==S::Missing,
          "full-capacity conflict poisons retained matches without evicting to store an unknown alias");
    T token{};
    check(!l.ReserveScriptActor(alias,token) && l.AddScriptActor(scriptBirth(8,30))==A::Refused && l.AddScriptActor(end)==A::Ended,
          "unknown conflicting alias cannot later gain rights while permanently full; known ends still work");
    check(l.Permit(indexed.controllerKey,indexed.headerId,0,true),"script capacity conflict does not poison an indexed receipt");
    l.Clear();filled=true;
    for(std::uint32_t i=1;i<=SPAWN_EMISSION_CAP;++i)
        filled=filled && l.AddScriptActor(scriptBirth(i,100+i,65000))==A::Added;
    indexed.sequence=1;
    check(filled && !l.Add(indexed),"script receipts also exhaust the same capacity for indexed adds");
    const auto first=scriptBirth(1,101,65000);auto lastEnd=scriptBirth(static_cast<std::uint32_t>(SPAWN_EMISSION_CAP),100+SPAWN_EMISSION_CAP,65000,true);
    check(l.ReserveScriptActor(first,token) && l.CommitScriptActor(token) && l.AddScriptActor(first)==A::Duplicate &&
          l.AddScriptActor(lastEnd)==A::Ended,"spend/consumed duplicate/exact end remain available in an all-script full ledger");
    check(!l.Pending(0,0) && !l.Commit(0) && !l.Cancel(0),"all-script history exposes no accidental indexed receipt");
    l.Clear();filled=true;
    for(std::uint32_t i=1;i<=SPAWN_EMISSION_CAP;++i)
        filled=filled && l.AddScriptActor(scriptBirth(i,100+i,65000,true))==A::Ended;
    ScriptCallEntry pending{};
    check(filled && !l.PendingScriptActor(pending) && l.AddScriptActor(first)==A::Ended &&
          l.AddScriptActor(scriptBirth(5000,9999,65000))==A::Refused,"end-before-start tombstones consume capacity and cannot be evicted or resurrected");
}

// Stable wire/ledger boundary: every arrival order and every processing split
// preserves the receipt's identity even when the exact host occurrence is dead.
void splitDelivery() {
    auto ticket=sample();ticket.netId=7;
    EnemyManifest manifest;manifest.epoch=ticket.location.epoch;manifest.entries.push_back({7,26,0,2403,{-7,0,-405},{}});
    EnemyDeath death;death.epoch=manifest.epoch;death.netId=7;
    const std::array packets{encode(manifest),encode(death),encode(ticket)};
    std::array<unsigned,3> order{0,1,2};
    do {
        for(unsigned split=0;split<4;++split) {
            SpawnEmissionLedger receipts;bool known=false,dead=false,hasReceipt=false;
            for(unsigned i=0;i<3;++i) {
                const auto& bytes=packets[order[i]];const std::uint8_t* payload;std::size_t size;
                const auto type=decodePacketHeader(bytes.data(),bytes.size(),payload,size);ByteReader reader(payload,size);
                if(type==PacketType::EnemyManifest){EnemyManifest m;read(reader,m);known=m.entries[0].netId==7;}
                else if(type==PacketType::EnemyDeath){EnemyDeath d;read(reader,d);dead=d.netId==7;}
                else {SpawnEmission e;read(reader,e);hasReceipt=receipts.Add(e);}
                if(i==2 || (split&(1u<<i))) {
                    const auto* pending=receipts.Pending(ticket.controllerKey,ticket.headerId,ticket.recordIndex);
                    check((pending!=nullptr)==hasReceipt && (!pending || pending->netId==7),
                          "split manifest/death/receipt delivery never relabels the exact birth ticket");
                    if(pending)check(receipts.Permit(ticket.controllerKey,ticket.headerId,ticket.recordIndex,false),
                                     "processing between split arrivals retains receipt for null allocation retry");
                }
            }
            check(known&&dead&&hasReceipt&&receipts.Pending(ticket.controllerKey,ticket.headerId,0)->netId==7&&
                  receipts.Permit(ticket.controllerKey,ticket.headerId,0,true)&&!receipts.Permit(ticket.controllerKey,ticket.headerId,0,true),
                  "already-dead exact occurrence still has exactly one birth receipt after every delivery split");
        }
    } while(std::next_permutation(order.begin(),order.end()));
}
void relay() {
    SessionConfig config;config.bindAddress="127.0.0.1";config.port=29882;config.gameBuild="emission-test";config.modHash="none";
    SessionHost host(config,{});if(!host.start()){check(false,"relay bind");return;}
    std::array<std::unique_ptr<NetworkClient>,3> clients;
    std::array<std::vector<SpawnEmission>,3> received;
    auto make=[&](unsigned i){ClientCallbacks cb;cb.onWorldEnvelope=[&,i](const WorldEnvelope& e){
        if(e.packet.front()==static_cast<std::uint8_t>(PacketType::SpawnEmission))received[i].push_back(decode(e.packet));};
        clients[i]=std::make_unique<NetworkClient>("127.0.0.1",config.port,"emission-test","none","e"+std::to_string(i),static_cast<SlotType>(i),cb);clients[i]->connect();};
    auto pump=[&]{for(int k=0;k<70;++k){host.tick();for(auto& c:clients)if(c)c->tick();std::this_thread::sleep_for(std::chrono::milliseconds(1));}};
    make(0);make(1);pump();pump();
    if(host.verifiedPeerCount()!=2){check(false,"host and friend admitted");for(auto& c:clients)if(c)c->disconnect();host.stop();return;}
    auto e=sample();e.netId=1;clients[0]->sendRoomTransition(e.location);pump();
    std::uint64_t source=10;
    auto send=[&](unsigned i,const auto& m){clients[i]->sendNativeWorld(encode(m),ProducerWorldContext{1,clients[i]->deliverySerial(),i==0?++source:0},true);pump();};
    auto refuse=[&](unsigned i,const SpawnEmission& m,const char* label){const auto before=host.rejectedWorldMessages();const auto count=received[1].size()+received[2].size();send(i,m);
        check(host.rejectedWorldMessages()==before+1&&received[1].size()+received[2].size()==count,label);};
    refuse(0,e,"combat receipt cannot arrive before current-room manifest identity");
    EnemyManifest manifest;manifest.epoch=e.location.epoch;
    for(std::uint16_t id:{std::uint16_t{1},std::uint16_t{2},std::uint16_t{0xFFFF}})manifest.entries.push_back({id,26,0,2403,{-7,0,-405},{}});
    send(0,manifest);EnemyDeath death;death.epoch=manifest.epoch;death.netId=1;send(0,death);
    send(0,e);check(received[1].size()==1&&received[0].empty()&&received[1][0].netId==1,"already-dead host occurrence receipt still forwards with exact identity");
    refuse(0,e,"duplicate emission sequence refused");
    auto next=e;next.sequence=2;next.netId=2;
    refuse(1,next,"friend cannot announce combat receipt");
    auto stale=next;--stale.location.epoch;refuse(0,stale,"stale room cannot authorize combat receipt");
    stale=next;++stale.location.mapProgram;refuse(0,stale,"same epoch with wrong full location cannot authorize receipt");
    auto unknown=next;unknown.netId=3;refuse(0,unknown,"unknown current-room combat identity refused");
    send(0,next);check(received[1].size()==2&&received[1].back().netId==2,"same record refill carries distinct netId and refusals do not advance sequence floor");
    auto duplicate=e;duplicate.sequence=3;refuse(0,duplicate,"new sequence cannot grant a second receipt for same combat occurrence");
    next.sequence=3;next.netId=0;send(0,next);next.sequence=4;send(0,next);
    check(received[1].size()==4&&received[1].back().netId==0,"zero helper births remain valid repeatable receipts");
    next.sequence=5;next.netId=0xFFFF;send(0,next);
    check(received[1].size()==5&&received[1].back().netId==0xFFFF,"relay supports highest uint16 combat identity");
    make(2);pump();pump();
    bool cache=received[2].size()==received[1].size();
    for(std::size_t i=0;cache&&i<received[2].size();++i)cache=encode(received[2][i])==encode(received[1][i]);
    check(cache,"late join cache retains exact birth identities and historical trigger points");
    ++e.location.epoch;clients[0]->sendRoomTransition(e.location);pump();
    refuse(0,e,"prior-room manifest cannot authorize a new-room combat ticket");
    auto helper=e;helper.netId=0;send(0,helper);
    check(received[1].size()==6&&received[2].size()==6, "helper receipt is valid even without a current-room enemy manifest");
    manifest.epoch=e.location.epoch;send(0,manifest);e.sequence=2;send(0,e);
    check(received[1].size()==7&&received[2].size()==7,"room retirement clears ticket sequence and combat identity uniqueness history");
    const auto binding=*clients[1]->worldBinding();WorldScope scope;
    scope.sessionId=binding.sessionId;scope.sourceConnectionId=host.peerBySlot(SlotType::Player)->connectionId;
    scope.sourceDeliverySerial=clients[0]->deliverySerial();scope.hostSourceSerial=++source;
    scope.targetConnectionId=binding.selfConnectionId;scope.targetDeliverySerial=binding.deliverySerial;
    auto clientRefuse=[&](const WorldScope& from,const SpawnEmission& emission,const char* label){
        const auto before=received[1].size();auto bytes=worldfixture::uncheckedEnvelope(from,encode(emission));
        clients[1]->inbound_.enqueue(0,std::move(bytes),true);clients[1]->tick(0);check(received[1].size()==before,label);};
    auto forged=scope;forged.sourceConnectionId=binding.selfConnectionId;forged.sourceDeliverySerial=binding.deliverySerial;forged.hostSourceSerial=0;
    clientRefuse(forged,e,"client rejects friend-authored receipt before raw native delivery");
    forged=scope;forged.kind=WorldSourceKind::Relay;clientRefuse(forged,e,"client rejects nonnative receipt source");
    stale=e;stale.location.door++;clientRefuse(scope,stale,"client rejects stale full room receipt before raw delivery");
    for(auto& c:clients)if(c)c->disconnect();host.stop();
}
}
int main(){if(enet_initialize()!=0)return 2;codec();ledger();reservations();scriptLedgerReservations();scriptLedgerIsolationAndConflict();scriptLedgerCapacity();splitDelivery();relay();enet_deinitialize();std::cout<<"failures="<<errors<<'\n';return errors?1:0;}
