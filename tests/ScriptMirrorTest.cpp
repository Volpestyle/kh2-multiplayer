// VUH-2252: ScriptCall wire format, relay forwarding and the DLL's send-on-change rules.
#include "kh2coop/Codec.hpp"
#include "kh2coop/RcRefusalTrace.hpp"
#include "kh2coop/NetworkClient.hpp"
#include "kh2coop/SessionHost.hpp"
#include "ScriptMirror.hpp"
#include "ScriptReaction.hpp"
#include <enet/enet.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <thread>
#include <vector>
using namespace kh2coop;
namespace R = kh2coop::inject::scriptmirror::rules;
namespace {
int errors = 0;
void check(bool ok, const char* text) { std::cout << (ok ? "PASS " : "FAIL ") << text << '\n'; if (!ok) ++errors; }

std::uint64_t flt(float f) { std::uint32_t b; std::memcpy(&b, &f, 4); return R::Tagged(R::kTagFloat, b); }
std::uint64_t num(std::uint32_t v) { return R::Tagged(R::kTagInt, v); }

ScriptCall sample() {
    ScriptCall m;
    m.location = {7, 2, 0x19, 0, 0, 149, 0};
    m.seq = 42; m.hostConnectionId = 10; m.hostFrame = 900;
    ScriptCallEntry tilt; tilt.netId = 3; tilt.objectId = 0x1FF; tilt.table = 8; tilt.index = 2;
    tilt.args = {flt(0.25f), flt(1.5f), flt(-0.125f), flt(1.0f)};
    ScriptCallEntry tex; tex.netId = 3; tex.objectId = 0x1FF; tex.table = 1; tex.index = 85; tex.args = {num(4), 0, 0, 0};
    m.entries = {tilt, tex};
    return m;
}
bool refusedRead(const std::vector<std::uint8_t>& bytes) {
    try { const std::uint8_t* p; std::size_t n; decodePacketHeader(bytes.data(), bytes.size(), p, n); ByteReader r(p, n); ScriptCall out; read(r, out); }
    catch (const std::exception&) { return true; }
    return false;
}
bool refusedWrite(const ScriptCall& m) { try { encode(m); } catch (const std::exception&) { return true; } return false; }

ScriptCallEntry actorBirthEntry(std::uint16_t index=SCRIPT_ACTOR_BIRTH_BEGIN,std::uint16_t child=17,
                               std::uint16_t parent=3,std::uint64_t occurrence=0xFEDCBA9876543210ULL) {
    ScriptCallEntry e;e.netId=parent;e.objectId=307;e.table=255;e.index=index;e.effect=child;
    e.args={num(static_cast<std::uint32_t>(occurrence)),num(static_cast<std::uint32_t>(occurrence>>32)),num(child),num(317)};
    return e;
}
R::Call actorBirthCall(const ScriptCallEntry& e) {
    R::Call c;c.netId=e.netId;c.objectId=e.objectId;c.effect=e.effect;
    c.trap=static_cast<std::uint8_t>(R::FindWire(e.table,e.index));
    for(unsigned i=0;i<4;++i)c.values[i]=e.args[i];
    return c;
}

void actorBirthCodecChecks() {
    for(const auto index:{SCRIPT_ACTOR_BIRTH_BEGIN,SCRIPT_ACTOR_BIRTH_END}) {
        auto packet=sample();packet.entries={actorBirthEntry(index)};
        const auto bytes=encode(packet);const std::uint8_t* p=nullptr;std::size_t n=0;
        decodePacketHeader(bytes.data(),bytes.size(),p,n);ByteReader reader(p,n);ScriptCall out;read(reader,out);
        const auto& e=out.entries.front();
        const auto occurrence=static_cast<std::uint64_t>(static_cast<std::uint32_t>(e.args[0])) |
            (static_cast<std::uint64_t>(static_cast<std::uint32_t>(e.args[1]))<<32);
        check(reader.atEnd() && encode(out)==bytes && occurrence==0xFEDCBA9876543210ULL &&
              n==SCRIPT_CALL_HEADER_BYTES+SCRIPT_CALL_ENTRY_BYTES && e.netId==3 && e.objectId==307 &&
              e.effect==17 && e.index==index && validScriptActorBirthIdentity(e.netId,e.objectId,e.effect,e.args),
              "actor birth/end preserves unsigned occurrence and exact parent/child in existing37+45 framing");
        check(R::ValidCall(actorBirthCall(e)),"full native-side call validator accepts the exact birth/end descriptor");
        const auto reject=[&](const ScriptCallEntry& bad,const char* label) {
            auto changed=packet;changed.entries={bad};check(refusedWrite(changed),label);
            // Bypass the writer's validator to exercise the actual decoder too.
            ByteWriter raw;raw.writeU16(bad.netId);raw.writeU32(bad.objectId);raw.writeU8(bad.table);
            raw.writeU16(bad.index);raw.writeU32(bad.effect);for(const auto value:bad.args)raw.writeU64(value);
            auto patched=bytes;std::copy(raw.data().begin(),raw.data().end(),patched.begin()+3+SCRIPT_CALL_HEADER_BYTES);
            check(refusedRead(patched),label);
            check(!R::ValidCall(actorBirthCall(bad)),"full native-side validator shares the wire identity refusal");
        };
        for(unsigned cell=0;cell<4;++cell) {
            for(const auto tag:{R::kTagAddress,R::kTagWork,R::kTagFloat,0xDEADBEEFu}) {
                auto bad=packet.entries.front();bad.args[cell]=R::Tagged(tag,static_cast<std::uint32_t>(bad.args[cell]));
                reject(bad,"birth/end refuses non-INT native handle/work/float/unknown tags on write and read");
            }
        }
        for(int mode=0;mode<11;++mode) {
            auto bad=packet.entries.front();
            switch(mode) {
            case 0:bad.netId=0;break;
            case 1:bad.objectId=308;break;
            case 2:bad.args[0]=bad.args[1]=num(0);break;
            case 3:bad.effect=0;bad.args[2]=num(0);break;
            case 4:bad.effect=65536;bad.args[2]=num(65536);break;
            case 5:bad.effect=bad.netId;bad.args[2]=num(bad.netId);break;
            case 6:++bad.effect;break;
            case 7:bad.args[3]=num(318);break;
            case 8:bad.effect=0x80000011u;break;
            case 9:bad.table=254;break;
            case 10:bad.index=17;break;
            }
            reject(bad,"birth/end refuses zero/wrong source, zero occurrence, bad child/effect or unknown operation");
        }
        for(const auto occurrenceValue:{std::uint64_t{1},std::uint64_t{1}<<32,(std::numeric_limits<std::uint64_t>::max)()}) {
            auto edge=packet;edge.entries={actorBirthEntry(index,UINT16_MAX,3,occurrenceValue)};
            check(!refusedWrite(edge) && validScriptCallEntry(edge.entries.front()),
                  "birth/end accepts unsigned low-only, high-only and all-bits occurrence with maximum child ID");
        }
    }
    auto e=actorBirthEntry();e.objectId=308;const auto c=actorBirthCall(e);
    check(R::ValidValues(c.trap,c.values) && !R::ValidCall(c),
          "argument-only checks cannot authenticate the parent header; full call admission is required");
    check(scriptActorBirthPolicy(307,317) && !scriptActorBirthPolicy(317,317) &&
          !scriptActorBirthPolicy(307,318),"only the approved source307 to child317 policy is exposed");
}

void actorBirthOrderedChecks() {
    auto begin=actorBirthCall(actorBirthEntry());auto end=actorBirthCall(actorBirthEntry(SCRIPT_ACTOR_BIRTH_END));
    const auto& startTrap=R::kTraps[begin.trap];const auto& endTrap=R::kTraps[end.trap];
    check(startTrap.kind==R::Kind::ActorBirth && endTrap.kind==R::Kind::ActorBirth &&
          startTrap.replay==R::Replay::ActorBirth && endTrap.replay==R::Replay::ActorBirth &&
          !startTrap.fnRva && !endTrap.fnRva && !startTrap.body && !endTrap.body &&
          !startTrap.ends && endTrap.ends && R::IsEvent(begin) && R::IsEvent(end) &&
          R::EffectRecord(begin.trap) && !R::IsAttackKind(startTrap.kind) && !R::IsEffectKind(startTrap.kind),
          "birth/end are ordered normalized metadata with a distinct replay/resource kind, not native traps");
    R::Sender sender;bool queued=true;
    for(const auto& c:std::array{end,begin,begin,end})queued=queued && sender.Observe(c)==R::Sender::Outcome::Queued;
    R::Call sent[4]{};const auto count=sender.Peek(sent,4);
    check(queued && count==4 && sent[0].trap==end.trap && sent[1].trap==begin.trap &&
          sent[2].trap==begin.trap && sent[3].trap==end.trap && sent[1].values[0]==sent[2].values[0],
          "end-before-start and duplicate descriptors preserve reliable event order; owner ledger decides lifecycle");
    sender.Commit(count);
    check(sender.Observe(begin)==R::Sender::Outcome::Queued,
          "birth heartbeat is an ordered event and is not hidden by a send-on-change baseline");
    R::Call visual;visual.netId=begin.netId;visual.objectId=307;visual.effect=begin.effect;
    visual.trap=static_cast<std::uint8_t>(R::FindTrap(0,51));
    auto attack=visual;attack.trap=static_cast<std::uint8_t>(R::FindTrap(2,13));
    check(R::KeyOf(begin)!=R::KeyOf(visual) && R::KeyOf(begin)!=R::KeyOf(attack),
          "child netId resource key cannot alias attack or visual effect resource namespaces");
    R::Hold hold;hold.Put(begin,1);hold.Put(visual,1);hold.Put(attack,1);
    hold.CancelResource(begin.netId,begin.effect,true);hold.CancelResource(begin.netId,begin.effect,false);
    hold.CancelOwner(begin.netId,begin.effect);std::size_t expired=0;unsigned births=0;
    const auto drained=hold.Drain(2,[&](const R::Call& c){births+=R::IsActorBirthKind(R::kTraps[c.trap].kind)?1:0;return true;},expired);
    check(drained==1 && births==1 && !expired,
          "generic attack/effect/owner cleanup cannot erase a same-numbered actor birth descriptor");
    R::Sender full;
    for(std::size_t i=0;i<R::Sender::kPendingCap;++i)full.Observe(begin);
    check(full.Observe(end)==R::Sender::Outcome::Full && full.Pending()==R::Sender::kPendingCap,
          "full ordered queue refuses without evicting earlier births; owner outbox retains the pending end");
    const std::uint64_t raw[]{R::Tagged(R::kTagWork,1),R::Tagged(R::kTagWork,2),R::Tagged(R::kTagWork,3),R::Tagged(R::kTagWork,4)};
    std::uint64_t values[4]{};
    check(!R::NormalizeNativeScalars(begin.trap,raw,values),
          "birth schema does not extend native workspace scalar normalization");
}

void reactionChecks() {
    std::array<R::ReactionSeat, 3> seats{{{100, {10,0,0}, true,true,true},
                                        {101, {-10,0,0}, true,true,true},
                                        {102, {0,20,0}, true,true,true}}};
    check(R::ReactionWinner({}, 50, seats) == 0, "RC distance tie chooses stable seat zero");
    seats[0].living = false;
    check(R::ReactionWinner({}, 50, seats) == 1, "downed host cannot win a reaction");
    seats[1].fresh = false;
    check(R::ReactionWinner({}, 50, seats) == 2, "stale seat cannot win a reaction");
    seats[2].capable = false;
    check(R::ReactionWinner({}, 50, seats) == -1, "unsupported profile cannot claim a reaction");
    seats[1].fresh = seats[2].capable = true;
    check(R::ReactionWinner({}, 9, seats) == -1, "outside range is not eligible");
    check(R::ReactionWinner({0, std::numeric_limits<float>::quiet_NaN(), 0}, 50, seats) == -1,
          "nonfinite trigger refuses arbitration");
    std::array<R::ActorRoleFacts,3> roles{{{500,10,true,true},{500,20,true,true},{600,30,true,true}}};
    check(R::SelectActorRole(500,20,10,roles)==1,"executing remote coroutine wins over canonical local controller");
    check(R::SelectActorRole(500,99,10,roles)==0,"native parent resolves nested role callback");
    check(R::SelectActorRole(500,99,98,roles)==-1,"ambiguous root callback refuses both captured roles");
    roles[0].current=false;
    check(R::SelectActorRole(500,10,98,roles)==1,"retired actor roots cannot borrow a role");
    roles[1].captured=false;
    check(R::SelectActorRole(500,99,98,roles)==-1,"uncaptured fallback is refused");
    R::ReactionOccurrences occurrences;
    auto* w = occurrences.Open(0xABC, 1, 1, 432, 7, 100, 60, {}, 50, seats);
    check(w && w->slot == 1, "attack occurrence offers Reversal to closest living friend");
    if (!w) return;
    const auto first = w->occurrence;
    check(occurrences.Open(0xABC, 1, 1, 432, 7, 110, 60, {}, 50, seats) == w &&
          w->openedFrame == 100 && w->occurrence == first, "repeated offers cannot extend or remint a window");
    R::ReactionCompletion claim{first, 101, 7, 1, 432, 1};
    auto forged = claim; forged.slot = 0; forged.connection = 100;
    check(!R::ConsumeReaction(*w, forged, 7, 120, seats), "nonwinner completion is refused");
    forged = claim; ++forged.source;
    check(!R::ConsumeReaction(*w, forged, 7, 120, seats), "a projectile cannot borrow another trigger");
    seats[1].connection = 201;
    check(!R::ConsumeReaction(*w, claim, 7, 120, seats), "seat replacement retires old owner's rights");
    seats[1].connection = 101;
    check(!R::ConsumeReaction(*w, claim, 7, 120, seats), "returning connection cannot resurrect a revoked window");
    auto downed=*w;downed.consumed=false;seats[1].living=false;
    check(!R::RefreshReaction(downed,7,120,seats) && downed.consumed,"frame observation permanently revokes downed owner");
    seats[1].living=true;
    check(!R::ConsumeReaction(downed,claim,7,121,seats),"revive cannot resurrect the old occurrence");
    auto valid=*w;valid.consumed=false;
    check(!R::ConsumeReaction(valid, claim, 8, 120, seats) && !R::ConsumeReaction(valid, claim, 7, 160, seats),
          "old room and expired window cannot complete");
    check(R::ConsumeReaction(valid, claim, 7, 159, seats) && !R::ConsumeReaction(valid, claim, 7, 159, seats),
          "winner's callback is reserved once before native execution");
    check(occurrences.Open(0xABC, 1, 1, 432, 7, 160, 60, {}, 50, seats)->consumed,
          "repeated script offer cannot resurrect a consumed trigger");
    occurrences.Retire(0xABC, 1);
    check(!occurrences.Find(first), "freed attack forgets its reaction window");
    w = occurrences.Open(0xABC, 2, 1, 432, 7, 170, 60, {}, 50, seats);
    check(w && w->occurrence > first && !R::ConsumeReaction(*w, claim, 7, 171, seats),
          "reused attack handle has a new occurrence and rejects old completion");
    const auto second = w ? w->occurrence : 0;
    occurrences.Reset();
    w = occurrences.Open(0xABC, 1, 1, 432, 7, 180, 60, {}, 50, seats);
    check(w && w->occurrence > second, "session reset never rewinds host occurrence IDs");
    occurrences.Reset();
    bool filled=true;
    for (unsigned i=0; i<R::ReactionOccurrences::Capacity; ++i)
        filled=filled && occurrences.Open(i+1, 1, 1, 432, 7, 200, 60, {}, 50, seats) != nullptr;
    check(filled,"bounded RC registry retains each distinct live attack");
    check(!occurrences.Open(999, 1, 1, 432, 7, 200, 60, {}, 50, seats), "full registry refuses without evicting a live claim");
}

void completionCodecChecks() {
    ScriptReactionCompletion m;m.location={9,2,34,0,157,157,157};m.seq=12;
    m.requesterConnectionId=101;m.occurrence=999;m.resource=7;m.objectId=307;m.netId=1;m.command=297;
    const auto bytes=encode(m);const std::uint8_t* p=nullptr;std::size_t n=0;
    check(decodePacketHeader(bytes.data(),bytes.size(),p,n)==PacketType::ScriptReactionCompletion &&
          n==SCRIPT_REACTION_COMPLETION_BYTES,"reaction completion has exact bounded framing");
    ByteReader r(p,n);ScriptReactionCompletion decoded;read(r,decoded);
    check(r.atEnd() && encode(decoded)==bytes,"reaction completion preserves room, occurrence and owner identity");
    auto bad=m;bad.occurrence=0;
    bool refused=false;try{encode(bad);}catch(const std::exception&){refused=true;}
    check(refused,"unminted reaction occurrence refuses serialization");
    auto trailing=bytes;trailing.push_back(0);
    refused=false;try{decodePacketHeader(trailing.data(),trailing.size(),p,n);}catch(const std::exception&){refused=true;}
    check(refused,"trailing completion bytes refuse framing");
    auto offers=sample();offers.entries.resize(1);
    auto& offer=offers.entries[0];offer.table=255;offer.index=1;offer.effect=77;
    offer.args={num(0xFFFFFFFE),num(1),num(345),num(1)};
    const auto offerBytes=encode(offers);
    decodePacketHeader(offerBytes.data(),offerBytes.size(),p,n);
    ByteReader offerReader(p,n);ScriptCall decodedOffer;read(offerReader,decodedOffer);
    check(offerReader.atEnd() && encode(decodedOffer)==offerBytes,
          "normalized RC offer roundtrip preserves 64-bit occurrence, command and seat");
    auto invalid=offers;invalid.entries[0].args[2]=num(0);
    check(refusedWrite(invalid),"zero offer command refuses");
    invalid=offers;invalid.entries[0].args[2]=num(65536);
    check(refusedWrite(invalid),"overflow offer command refuses");
    invalid=offers;invalid.entries[0].args[3]=num(3);
    check(refusedWrite(invalid),"unadmitted offer seat refuses");
    invalid=offers;invalid.entries[0].args[0]=flt(1);
    check(refusedWrite(invalid),"floating occurrence refuses");
}

void ownerCodecChecks() {
    auto packet=sample();packet.entries.clear();
    const auto add=[&](std::uint16_t operation,std::initializer_list<std::uint64_t> values) {
        ScriptCallEntry record;record.netId=3;record.objectId=307;record.table=255;record.index=operation;record.effect=0x80000001u;
        std::copy(values.begin(),values.end(),record.args.begin());packet.entries.push_back(record);
    };
    add(2,{num(1),num(101),num(2),num(60)});
    add(3,{num(253),flt(0)});
    add(4,{flt(1),flt(2),flt(3),flt(0.5f)});
    add(5,{num(0x30306372),num(1)});
    add(12,{num(1),num(101),num(2)});
    add(1,{num(9),num(1),num(297),num(1)});
    add(7,{num(1),num(297),num(1),num(100)});
    add(8,{num(253),flt(25.5f)});
    add(9,{num(0x80000001u),num(1305),num(UINT32_MAX),num(0)});
    add(11,{num(100)});add(6,{});add(13,{num(16386),num(5)});add(14,{});
    const auto bytes=encode(packet);const std::uint8_t* p=nullptr;std::size_t n=0;
    decodePacketHeader(bytes.data(),bytes.size(),p,n);ByteReader reader(p,n);ScriptCall decoded;read(reader,decoded);
    check(reader.atEnd() && encode(decoded)==bytes && decoded.entries.size()==13,
          "owner role, capture, camera, command and attack descriptors roundtrip without native handles");
    auto bad=packet;bad.entries[0].args[0]=num(3);check(refusedWrite(bad),"role begin refuses unknown seat");
    bad=packet;bad.entries[0].args[3]=num(61);check(refusedWrite(bad),"role begin refuses unbounded lease policy");
    bad=packet;bad.entries[1].args[1]=num(0);check(refusedWrite(bad),"capture rate must retain its native float tag");
    bad=packet;bad.entries[3].args[1]=num(2);check(refusedWrite(bad),"camera cannot name an arbitrary target actor");
    bad=packet;bad.entries[6].args[0]=num(0);check(refusedWrite(bad),"owner command cannot replace the ordinary attack category");
    bad=packet;bad.entries[6].args[3]=num(256);check(refusedWrite(bad),"command priority refuses truncation");
    bad=packet;bad.entries[7].args[1]=flt(-1);check(refusedWrite(bad),"role motion refuses negative time");
    bad=packet;bad.entries[8].args[0]=num(3);check(refusedWrite(bad),"script attack requires an immutable actor-role identity");
    bad=packet;bad.entries[11].args[0]=num(16387);check(refusedWrite(bad),"attach refuses unqualified native flags");
    bad=packet;bad.entries[11].args[1]=num(6);check(refusedWrite(bad),"attach refuses another bone");
    bad=packet;bad.entries[11].args[2]=num(15845);check(refusedWrite(bad),"attach cannot carry a BDX callback PC");
    bad=packet;bad.entries[12].args[0]=num(1);check(refusedWrite(bad),"detach cannot carry native actor handles");
}

void codecChecks() {
    const auto m = sample();
    const auto bytes = encode(m);
    const std::uint8_t* p = nullptr; std::size_t n = 0;
    check(PacketType::ScriptCall == static_cast<PacketType>(49) && decodePacketHeader(bytes.data(), bytes.size(), p, n) == PacketType::ScriptCall &&
          n == SCRIPT_CALL_HEADER_BYTES + 2u * SCRIPT_CALL_ENTRY_BYTES, "script call is packet type 49, 37 + 45 * n payload bytes");
    ByteReader reader(p, n); ScriptCall copy; read(reader, copy);
    check(encode(copy) == bytes && reader.atEnd() && copy.seq == 42 && copy.hostConnectionId == 10 && copy.hostFrame == 900 &&
          copy.entries.size() == 2 && copy.entries[0].args == m.entries[0].args && copy.entries[1].index == 85 &&
          sameResyncRoom(copy.location, m.location), "script call exact codec roundtrip in field order");
    for (int delta : {-1, 1}) {
        auto bad = bytes; if (delta < 0) bad.pop_back(); else bad.push_back(0);
        check(refusedRead(bad), "truncated or trailing script call frame refused");
        bad[1] = static_cast<std::uint8_t>(bad.size() - 3); bad[2] = static_cast<std::uint8_t>((bad.size() - 3) >> 8);
        check(refusedRead(bad), "script call with a self-consistent length off the entry grid refused");
    }
    check(isWorldPacket(PacketType::ScriptCall) && isScopedWorldPacket(PacketType::ScriptCall) &&
          !isMaterialWorldPacket(PacketType::ScriptCall) && !isEphemeralWorldPacket(PacketType::ScriptCall) &&
          validateScopedWorldPacket(bytes) == PacketType::ScriptCall, "script call is a scoped, non-material world packet");
    auto bad = m; bad.entries.clear(); check(refusedWrite(bad), "empty script call refused");
    bad = m; bad.entries.assign(SCRIPT_CALL_MAX_ENTRIES + 1, m.entries[1]); check(refusedWrite(bad), "33 entries refused");
    bad = m; bad.seq = 0; check(refusedWrite(bad), "zero sequence refused");
    bad = m; bad.entries[1].index = 121; check(refusedWrite(bad), "a trap outside the list (target_search) refused");
    bad = m; bad.entries[1].netId = 0; check(refusedWrite(bad), "netId 0 refused");
    bad = m; bad.entries[1].args[1] = num(1); check(refusedWrite(bad), "a nonzero unused value refused");
    bad = m; bad.entries[1].args[0] = R::Tagged(R::kTagAddress, 5); check(refusedWrite(bad), "a handle value refused");
    bad = m; bad.entries[1].args[0] = R::Tagged(R::kTagWork, 5); check(refusedWrite(bad), "native workspace tag refused by wire writer");
    bad = m; bad.entries[0].args[2] = num(1); check(refusedWrite(bad), "an int inside a vector refused");
    bad = m; bad.entries[0].args[0] = flt(std::numeric_limits<float>::quiet_NaN()); check(refusedWrite(bad), "NaN refused");
    bad = m; bad.entries[0].args[0] = flt(2.0e5f); check(refusedWrite(bad), "out-of-bound float refused");
    // Same refusals on read: patch one byte of a valid frame (entry 1 index low byte at 37 + 45 + 7).
    auto withEffect = m; withEffect.entries[1].effect = 9; check(refusedWrite(withEffect), "an effect key on an actor call refused");
    auto patched = bytes; patched[3 + SCRIPT_CALL_HEADER_BYTES + SCRIPT_CALL_ENTRY_BYTES + 7] = 121;
    check(refusedRead(patched), "reader refuses a trap outside the list");
    patched = bytes; patched[3 + SCRIPT_CALL_HEADER_BYTES - 1] = 0;
    check(refusedRead(patched), "reader refuses a zero entry count");
    patched=bytes;
    const auto tagOffset=3+SCRIPT_CALL_HEADER_BYTES+SCRIPT_CALL_ENTRY_BYTES+13+4;
    for(unsigned k=0;k<4;++k)patched[tagOffset+k]=static_cast<std::uint8_t>(R::kTagWork>>(8*k));
    check(refusedRead(patched),"wire reader refuses a patched native workspace tag");
}

void rulesAgreeWithCodec() {
    bool shapes = true, values = true, converted = true;
    for (std::size_t i = 0; i < R::kTrapCount; ++i) {
        const auto& t = R::kTraps[i];
        const auto shape = scriptCallShape(t.table, t.index);
        const unsigned scriptArgs = t.kind == R::Kind::EffectStartPos || t.kind==R::Kind::AttackRc ? 5u :
            t.kind==R::Kind::AttackStrike?4u:1u + (t.vector ? 1u : t.values);
        const bool pinned = R::FindTrap(t.table,t.index)==static_cast<int>(i) &&
            (t.fnRva ? t.body && t.bodySize>0 && (t.flags&0xFFFF)==scriptArgs :
             t.table==255 && ((t.index==1 && t.kind==R::Kind::ReactionOffer) ||
                             (t.index==12 && t.kind==R::Kind::ReactionOwner) ||
                             (t.kind==R::Kind::Owner && ((t.index>=2 && t.index<=8) || t.index==11 || t.index==13 || t.index==14)) ||
                             (t.kind==R::Kind::OwnerAttackStart && t.index==9) ||
                             (t.kind==R::Kind::ActorBirth && scriptActorBirthOperation(t.table,t.index))) && !t.body && !t.bodySize);
        if (t.replay == R::Replay::None) {  // never on the wire itself: it travels as another trap
            const int wire = R::FindWire(t.wireTable, t.wireIndex);
            converted = converted && pinned && shape.kind == ScriptCallKind::None && wire >= 0 &&
                        R::kTraps[wire].kind != R::Kind::EffectStartPos && R::kTraps[wire].replay != R::Replay::None;
            continue;
        }
        shapes = shapes && pinned && shape.argc == t.values && (shape.kind == ScriptCallKind::Vector4) == t.vector &&
                 shape.kind != ScriptCallKind::None && shape.effect == R::EffectRecord(i) &&
                 t.wireTable == t.table && t.wireIndex == t.index;
        const std::uint64_t cases[][4] = {{0, 0, 0, 0}, {num(1), 0, 0, 0}, {flt(0.5f), 0, 0, 0}, {flt(1), flt(2), flt(3), flt(4)},
                                          {num(1), num(2), 0, 0}, {flt(1), num(2), flt(3), flt(4)}, {R::Tagged(R::kTagAddress, 1), 0, 0, 0},
                                          {num(1), num(2), num(3), 0}, {flt(0.5f), num(1), 0, 0}};
        for (const auto& v : cases) {
            ScriptCallEntry e; e.netId = 1; e.table = t.table; e.index = t.index; e.effect = R::EffectRecord(i) ? 7u : 0u;
            for (int k = 0; k < 4; ++k) e.args[static_cast<std::size_t>(k)] = v[k];
            const bool nativeValid=R::IsActorBirthKind(t.kind)?R::ValidCall(actorBirthCall(e)):R::ValidValues(i,v);
            values = values && nativeValid == validScriptCallEntry(e);
            auto wrongEffect = e; wrongEffect.effect = e.effect ? 0u : 7u;
            values = values && !validScriptCallEntry(wrongEffect);
        }
    }
    check(shapes, "every DLL trap sent as itself is on the wire list with the same shape, effect id rule, argument count and a pinned body");
    check(converted, "start_pos and set_dir never go on the wire; each travels as a trap that does");
    const auto hide=R::FindTrap(1,133),show=R::FindTrap(1,134);
    check(hide>=0 && show>=0 && R::kTraps[hide].group==R::Group::Visibility &&
          R::kTraps[show].group==R::Group::Visibility,"boss body visibility is separate from shadow visibility");
    check(values, "DLL value rules and wire validation agree on every trap; an effect id is required exactly for effect calls");
    check(R::FindTrap(1, 121) < 0 && scriptCallShape(1, 121).kind == ScriptCallKind::None, "target_search is not mirrored");
}

R::Call call(std::uint16_t netId, std::uint8_t trap, std::uint64_t v0 = 0) {
    R::Call c; c.netId = netId; c.objectId = 0x1FF; c.trap = trap;
    if (R::kTraps[trap].vector) { c.values[0] = v0 ? v0 : flt(0); c.values[1] = flt(0); c.values[2] = flt(0); c.values[3] = flt(1); }
    else if (R::kTraps[trap].values) c.values[0] = v0;
    return c;
}
void senderChecks() {
    using O = R::Sender::Outcome;
    const auto rot = static_cast<std::uint8_t>(R::FindTrap(1, 1)), xyz = static_cast<std::uint8_t>(R::FindTrap(8, 2));
    const auto texOn = static_cast<std::uint8_t>(R::FindTrap(1, 85)), texOff = static_cast<std::uint8_t>(R::FindTrap(1, 86));
    const auto hide = static_cast<std::uint8_t>(R::FindTrap(6, 45)), show = static_cast<std::uint8_t>(R::FindTrap(6, 64));
    R::Sender s;
    check(s.Observe(call(3, xyz, flt(0.2f))) == O::Queued && s.Observe(call(3, xyz, flt(0.2f))) == O::Repeat,
          "an identical queued call is a repeat");
    check(s.Observe(call(3, xyz, flt(0.3f))) == O::Replaced && s.Pending() == 1, "a changed value replaces the queued one");
    check(s.Observe(call(3, rot, flt(1.0f))) == O::Replaced && s.Pending() == 1, "set_rot and set_xyzrot share the rotation key");
    check(s.Observe(call(4, rot, flt(1.0f))) == O::Queued && s.Pending() == 2, "another enemy has its own key");
    R::Call out[32];
    check(s.Peek(out, 32) == 2 && out[0].netId == 3 && out[0].trap == rot && out[1].netId == 4, "queued calls flush in call order");
    s.Commit(2);
    check(s.Pending() == 0 && s.Observe(call(3, rot, flt(1.0f))) == O::Repeat && s.Observe(call(4, rot, flt(1.0f))) == O::Repeat,
          "every frame's identical call after a send is not re-sent");
    check(s.Observe(call(3, rot, flt(1.5f))) == O::Queued, "a change after the send queues again");
    check(s.Observe(call(3, texOn, num(4))) == O::Queued && s.Observe(call(3, texOn, num(5))) == O::Queued &&
          s.Observe(call(3, texOff, num(4))) == O::Replaced, "texture animations are keyed by id; stop replaces its start");
    check(s.Observe(call(3, hide)) == O::Queued && s.Observe(call(3, show)) == O::Replaced, "shadow hide and show share a key");
    std::size_t n = s.Peek(out, 32);
    check(n == 4, "four keys pending");
    s.Commit(1);  // only the first went out: the rest stay queued for the next packet
    check(s.Pending() == 3 && s.Observe(call(3, rot, flt(1.5f))) == O::Repeat, "a partial commit keeps the rest queued");
    s.Reset();
    check(s.Pending() == 0 && s.Observe(call(3, rot, flt(1.0f))) == O::Queued, "reset (room change) forgets every baseline");
    R::Sender full;
    bool ok = true;
    for (std::uint16_t id = 1; id <= R::Sender::kPendingCap; ++id) ok = ok && full.Observe(call(id, rot, flt(1))) == O::Queued;
    check(ok && full.Observe(call(999, rot, flt(1))) == O::Full, "pending is bounded");
    // Per-second counts: 120 frames of the same tilt from 3 enemies cost 3 records.
    R::Sender boss; unsigned queued = 0, repeats = 0;
    for (int frame = 0; frame < 120; ++frame) {
        for (std::uint16_t id = 1; id <= 3; ++id) { const auto o = boss.Observe(call(id, xyz, flt(0.4f))); queued += o == O::Queued; repeats += o == O::Repeat; }
        boss.Commit(boss.Pending());
    }
    check(queued == 3 && repeats == 357, "a boss repeating the same call every frame sends it once");
}

void holdChecks() {
    const auto rot = static_cast<std::uint8_t>(R::FindTrap(1, 1));
    R::Hold h;
    check(h.Put(call(3, rot, flt(1)), 10) && h.Put(call(3, rot, flt(2)), 11) && h.Size() == 1, "hold keeps the latest value per key");
    check(h.Put(call(4, rot, flt(1)), 12) && h.Size() == 2, "hold keeps one item per key");
    std::size_t expired = 0; std::vector<std::uint16_t> applied;
    auto ready3 = [&](const R::Call& c) { if (c.netId != 3) return false; applied.push_back(c.netId); return true; };
    check(h.Drain(20, ready3, expired) == 1 && applied.size() == 1 && h.Size() == 1 && expired == 0, "drain applies ready copies only");
    check(h.Drain(12 + R::Hold::kHoldFrames + 1, ready3, expired) == 0 && expired == 1 && h.Size() == 0, "an unbound copy's call expires");
    h.Put(call(3, rot, flt(1)), 30); h.Erase(R::KeyOf(call(3, rot, flt(9))));
    check(h.Size() == 0, "erase drops the held value for a key a newer call already set");
}

R::Call effectCall(std::uint32_t effect, std::uint8_t table, std::uint16_t index, float v0 = 0) {
    R::Call c; c.netId = 5; c.objectId = 0x161; c.effect = effect;
    c.trap = static_cast<std::uint8_t>(R::FindWire(table, index));
    const auto& t = R::kTraps[c.trap];
    if (t.vector) { c.values[0] = flt(v0); c.values[1] = flt(0); c.values[2] = flt(0); c.values[3] = flt(1); }
    else for (unsigned k = 0; k < t.values; ++k) c.values[k] = num(k + 1);
    return c;
}
void effectChecks() {
    using O = R::Sender::Outcome;
    R::Sender s;
    check(s.Observe(effectCall(1, 1, 21)) == O::Queued && s.Observe(effectCall(1, 1, 21)) == O::Queued,
          "an effect start is an event: two identical starts both queue");
    check(s.Observe(effectCall(1, 0, 38, 10)) == O::Queued && s.Observe(effectCall(1, 0, 38, 10)) == O::Repeat &&
          s.Observe(effectCall(1, 0, 38, 11)) == O::Replaced, "an effect's position is sent on change");
    check(s.Observe(effectCall(2, 0, 38, 11)) == O::Queued && s.Observe(effectCall(1, 0, 40, 11)) == O::Queued,
          "each effect and each field has its own key");
    check(s.Observe(effectCall(1, 0, 51)) == O::Queued && s.Observe(effectCall(1, 0, 51)) == O::Queued,
          "kills are events too");
    R::Call out[32];
    const std::size_t n = s.Peek(out, 32);
    check(n == 7 && out[0].trap == R::FindWire(1, 21) && out[2].values[0] == flt(11) && out[6].trap == R::FindWire(0, 51),
          "starts, fields and kills flush in call order");
    s.Commit(n);
    check(s.Observe(effectCall(1, 0, 38, 11)) == O::Repeat && s.Observe(effectCall(1, 1, 21)) == O::Queued,
          "a sent field is a baseline; a sent event is not");
    // A moving effect: 120 frames of set_pos that changes every 4 frames costs 30 records.
    R::Sender moving; unsigned queued = 0;
    for (int frame = 0; frame < 120; ++frame) {
        queued += moving.Observe(effectCall(9, 0, 38, static_cast<float>(frame / 4))) == O::Queued;
        moving.Commit(moving.Pending());
    }
    check(queued == 30, "a travelling effect sends one record per new position");

    R::HostEffects host;
    const auto a = host.Add(0x100, 0x5000, 0x6000, 5, 0x161), b = host.Add(0x200, 0x5100, 0x6100, 6, 0x162);
    check(a && b > a && host.Find(0x100)->id == a && host.Find(0x200)->netId == 6 && !host.Find(0x300), "host effects get fresh ids by handle");
    const auto again = host.Add(0x100, 0x5200, 0x6200, 5, 0x161);
    check(again > b && host.Find(0x100)->id == again && host.Find(0x100)->part == 0x6200, "a reused handle gets a new id");
    host.EraseOwner(5);
    check(!host.Find(0x100) && host.Find(0x200), "kill_all drops only that enemy's effects");
    R::HostEffects full;
    for (std::uint32_t h = 1; h <= R::HostEffects::kCap; ++h) full.Add(h, h, h, 1, 1);
    full.Add(9999, 1, 1, 1, 1);
    check(full.Size() == R::HostEffects::kCap && !full.Find(1) && full.Find(9999), "host effects are bounded, oldest out first");

    R::ClientEffects client;
    client.Put(a, {0x11, 0x5000, 0x6000, 5}); client.Put(b, {0x12, 0x5100, 0x6100, 6});
    check(client.Find(a)->handle == 0x11 && client.Find(b)->netId == 6 && !client.Find(999), "client pairs a host id with its own effect");
    client.EraseOwner(6); client.Erase(a);
    check(!client.Find(a) && !client.Find(b), "client pairs end with the effect or its enemy");

    R::Hold h;
    h.Put(effectCall(4, 1, 21), 10);
    h.Put(effectCall(4, 0, 38, 1), 10);
    h.Put(effectCall(4, 0, 38, 2), 11);
    check(h.Size() == 2 && h.HoldsStart(4) && !h.HoldsStart(5) && h.HoldsNetId(5) && !h.HoldsNetId(6),
          "a held start keeps its later field (latest value) behind it");
    std::vector<std::uint8_t> order; std::size_t expired = 0; bool ready = false;
    auto apply = [&](const R::Call& c) { if (!ready) return false; order.push_back(c.trap); return true; };
    check(h.Drain(12, apply, expired) == 0 && h.Size() == 2, "nothing drains before the copy is driven");
    ready = true;
    check(h.Drain(13, apply, expired) == 2 && order.size() == 2 && order[0] == R::FindWire(1, 21) && order[1] == R::FindWire(0, 38),
          "once driven, the start lands before its field");
    R::Hold blocked; std::size_t exp2 = 0;
    blocked.Put(call(5, static_cast<std::uint8_t>(R::FindTrap(1, 1)), flt(1)), 1);  // netId 5, not ready
    blocked.Put(effectCall(4, 1, 21), 1);                                          // netId 5 too: waits behind it
    auto onlyEffects = [&](const R::Call& c) { return c.effect != 0; };
    check(blocked.Drain(2, onlyEffects, exp2) == 0 && blocked.Size() == 2, "a later record for a netId waits behind an earlier one");
}

void attackLifetimeChecks() {
    R::HostEffects host;
    R::ClientEffects client;
    const auto old=host.Add(0x100,0x5000,0x6000,5,307,false);
    check(client.Put(old,{0x100,0x5000,0x6000,5},false),"attack resource pairs without eviction");
    // The native constructor/destructor adapters invoke this before memory is
    // freed or reused, even when address, handle and owner are identical.
    host.ForgetAllocation(0x5000);client.ForgetAllocation(0x5000);
    const auto next=host.Add(0x100,0x5000,0x6000,5,307,false);
    client.Put(next,{0x100,0x5000,0x6000,5},false);
    check(next>old && !client.Find(old) && client.Find(next),"recycled allocation never revives an old attack resource");
    R::HostEffects full;
    R::ClientEffects copies;
    for(std::uint32_t i=1;i<=R::HostEffects::kCap;++i) {
        full.Add(i,i,i,5,307,false);copies.Put(i,{i,i,i,5},false);
    }
    check(!full.Add(9999,9999,9999,5,307,false) && full.Find(1) &&
          !copies.Put(9999,{9999,9999,9999,5},false) && copies.Find(1),
          "collision registry exhaustion preserves every existing pairing");
    auto start=effectCall(77,2,10);
    auto end=effectCall(77,2,13);
    R::Hold held;held.Put(start,1);
    check(held.HoldsStart(77,true) && !held.HoldsStart(77,false),"attack and visual starts have distinct namespaces");
    held.Put(effectCall(77,2,12),1);
    held.CancelResource(start.netId,77,true);
    std::size_t expired=0;
    check(!held.HoldsStart(77,true) && held.Drain(2,[](const R::Call&){return true;},expired)==0,
          "free before driven cancels held attack start and fields before any native allocation");
    auto visual=effectCall(77,0,38);
    auto position=effectCall(77,2,12);
    // Namespace is explicit even if future fields share a coalescing group.
    check(R::KeyOf(visual)!=R::KeyOf(position),"attack fields cannot overwrite visual resource fields");
    R::Sender sender;
    for(std::size_t i=0;i<R::Sender::kPendingCap;++i) sender.Observe(start);
    check(sender.Observe(end)==R::Sender::Outcome::Full,"saturated normal queue refuses immediate attack end");
    R::Retirements pending;
    check(pending.Put(end),"attack end has a reserved deferred path");
    pending.Drain(sender);
    check(pending.Size()==1,"deferred end survives a still-full queue");
    sender.Commit(1);pending.Drain(sender);
    R::Call batch[R::Sender::kPendingCap]{};
    const auto n=sender.Peek(batch,R::Sender::kPendingCap);
    check(!pending.Size() && n==R::Sender::kPendingCap && batch[n-1].effect==77 &&
          batch[n-1].trap==R::FindTrap(2,13),"attack end queues after earlier starts when capacity returns");
}

void storedResourceHandleChecks() {
    // Reproduce the BDX lifetime, not just the tag parser: attack_new returns
    // @ADR, the script stores its low word, then fields/RC/free push @???.
    R::HostEffects attacks;
    const auto resource=attacks.Add(0x100,0x5000,0x6000,5,307,false);
    struct NativeAllocation { std::uintptr_t allocation,owner;bool live; } native{0x5000,0x6000,true};
    unsigned nativeReads=0;
    const auto current=[&](std::uint32_t handle,const R::HostEffect& pair) {
        ++nativeReads;
        return handle==0x100 && native.live && pair.effect==native.allocation && pair.part==native.owner;
    };
    const auto returned=attacks.CaptureNative(R::Tagged(R::kTagAddress,0x100),current);
    check(returned.status==R::NativeResourceStatus::Paired && returned.value.id==resource,
          "native returned address pairs with the announced attack resource");
    R::Sender sender;
    const std::uint16_t operations[]{12,51,13};bool accepted=true;
    for(const auto operation:operations) {
        const auto stored=attacks.CaptureNative(R::Tagged(R::kTagWork,0x100),current);
        accepted=accepted && stored.status==R::NativeResourceStatus::Paired && stored.value.id==resource;
        if(stored.status!=R::NativeResourceStatus::Paired)continue;
        R::Call call;call.netId=stored.value.netId;call.objectId=stored.value.objectId;
        call.effect=stored.value.id;call.trap=static_cast<std::uint8_t>(R::FindTrap(2,operation));
        if(operation==12)for(unsigned k=0;k<4;++k)call.values[k]=flt(static_cast<float>(k));
        if(operation==51){call.values[0]=num(345);call.values[1]=flt(800);call.values[2]=flt(500);}
        if(operation==13)native.live=false; // native free retires its flag after the pre-call capture
        accepted=accepted && attacks.StillPaired(stored) && R::ValidValues(call.trap,call.values) &&
            sender.Observe(call)==R::Sender::Outcome::Queued;
    }
    R::Call batch[3];const auto count=sender.Peek(batch,3);
    check(accepted && count==3 && batch[0].effect==resource && batch[1].effect==resource &&
          batch[2].effect==resource && R::kTraps[batch[0].trap].index==12 &&
          R::kTraps[batch[1].trap].index==51 && R::kTraps[batch[2].trap].index==13,
          "stored work attack handle preserves ordered position, reaction command and free on one resource");
    const auto retired=attacks.CaptureNative(R::Tagged(R::kTagWork,0x100),current);
    check(retired.status==R::NativeResourceStatus::NotCurrent,
          "integer handle cannot revive an attack whose native lifetime ended");
    native.live=true;native.owner=0xDEAD;
    check(attacks.CaptureNative(R::Tagged(R::kTagWork,0x100),current).status==R::NativeResourceStatus::NotCurrent,
          "stored work handle still requires its registered native owner");
    native.owner=0x6000;native.allocation=0xDEAD;
    check(attacks.CaptureNative(R::Tagged(R::kTagWork,0x100),current).status==R::NativeResourceStatus::NotCurrent,
          "stored work handle still requires its registered native allocation");
    const auto reads=nativeReads;
    check(attacks.CaptureNative(R::Tagged(R::kTagFloat,0x100),current).status==R::NativeResourceStatus::WrongTag &&
          attacks.CaptureNative(R::Tagged(0xDEADBEEF,0x100),current).status==R::NativeResourceStatus::WrongTag &&
          attacks.CaptureNative(R::Tagged(R::kTagWork,0x999),current).status==R::NativeResourceStatus::Unpaired &&
          attacks.CaptureNative(R::Tagged(R::kTagAddress,0x999),current).status==R::NativeResourceStatus::Unpaired &&
          attacks.CaptureNative(R::Tagged(R::kTagWork,0),current).status==R::NativeResourceStatus::Unpaired && nativeReads==reads,
          "unsupported tags and unregistered work handles never reach native allocation reads");
    // Native callbacks can destroy and reuse a handle while the observed trap
    // runs. The pre-call snapshot must neither dangle nor name the replacement.
    attacks.ForgetAllocation(0x5000);native.allocation=0x5000;
    const auto replacement=attacks.Add(0x100,0x5000,0x6000,5,307,false);
    check(!attacks.StillPaired(returned) && returned.value.id==resource && replacement>resource &&
          attacks.CaptureNative(num(0x100),current).value.id==replacement,
          "native reentry cannot retarget a captured integer handle to a replacement resource");
    R::HostEffects effects;
    const auto effect=effects.Add(0x200,0x7000,0x8000,5,307);
    const auto liveEffect=[](std::uint32_t handle,const R::HostEffect& pair) {
        return handle==0x200 && pair.effect==0x7000 && pair.part==0x8000;
    };
    check(effects.CaptureNative(R::Tagged(R::kTagWork,0x200),liveEffect).status==R::NativeResourceStatus::Paired &&
          effects.CaptureNative(num(0x200),liveEffect).value.id==effect &&
          !attacks.CaptureNative(num(0x200),current).value.id,
          "stored effect handles use their own validated pairing, never the attack registry");
}

void nativeScalarChecks() {
    const auto work=[](std::uint32_t bits){return R::Tagged(R::kTagWork,bits);};
    const auto normalize=[&](std::uint8_t table,std::uint16_t index,const std::array<std::uint64_t,5>& args,
                             const std::array<std::uint64_t,4>& expected) {
        const auto trap=R::FindTrap(table,index);std::uint64_t values[4]{};
        if(!R::NormalizeNativeScalars(trap,args.data(),values))return false;
        for(unsigned k=0;k<4;++k)if(values[k]!=expected[k])return false;
        return R::ValidValues(trap,values);
    };
    const auto fbits=[](float f){std::uint32_t bits;std::memcpy(&bits,&f,4);return bits;};
    check(normalize(2,10,{work(0xDEAD),work(7),work(0x80000000u),work(0xFFFFFFFFu),0},
                    {num(7),num(0x80000000u),num(0xFFFFFFFFu),0}),
          "native attack start preserves low32 integer bits without consuming actor argument");
    check(normalize(2,11,{work(0xDEAD),work(fbits(800)),work(fbits(500)),0,0},
                    {flt(800),flt(500),0,0}) &&
          normalize(2,51,{work(0xDEAD),work(0xBEEF),work(345),work(fbits(800)),work(fbits(500))},
                    {num(345),flt(800),flt(500),0}) &&
          normalize(2,17,{work(0xDEAD),work(0xBEEF),work(1),work(0xFFFFFFFFu),0},
                    {num(1),num(0xFFFFFFFFu),0,0}),
          "native radius, RC and strike use only wrapper-proven scalar positions");
    check(normalize(0,52,{work(0xDEAD),work(fbits(30)),work(1),0,0},{flt(30),num(1),0,0}) &&
          normalize(0,66,{work(0xDEAD),work(1),0,0,0},{num(1),0,0,0}) &&
          normalize(1,22,{work(0xDEAD),work(9),work(0xBEEF),work(2),work(3)},
                    {num(9),num(2),num(3),0}),
          "native fade, pause and positioned effect start normalize scalar slots without reading vector handle");
    std::uint64_t values[4]{};
    for(const auto bits:{0x7FC00000u,0x7F800000u,fbits(100001),fbits(-1)}) {
        const std::uint64_t args[]{work(0xDEAD),work(bits),work(fbits(1)),0,0};
        check(!R::NormalizeNativeScalars(R::FindTrap(2,11),args,values),
              "invalid native radius refuses before any prompt or resource mutation");
    }
    for(const auto command:{0u,65536u}) {
        const std::uint64_t args[]{work(0xDEAD),work(0xBEEF),work(command),work(fbits(1)),work(fbits(1))};
        check(!R::NormalizeNativeScalars(R::FindTrap(2,51),args,values),"invalid native RC command refuses");
    }
    const std::uint64_t raw[]{work(1),work(2),work(3),work(4),work(5)};
    check(!R::NormalizeNativeScalars(R::FindTrap(0,38),raw,values) &&
          !R::NormalizeNativeScalars(R::FindTrap(1,1),raw,values) &&
          !R::NormalizeNativeScalars(R::FindTrap(255,7),raw,values) &&
          !R::ValidValues(R::FindTrap(2,11),raw),
          "native scalar normalization cannot admit arbitrary actor, vector, owner or wire values");
}

void enetChecks() {
    SessionConfig config; config.bindAddress = "127.0.0.1"; config.port = 29881;
    config.gameBuild = "script-call-test"; config.modHash = "none";
    std::string lastReason;
    SessionCallbacks relayCallbacks; relayCallbacks.onLog = [&](const std::string& m) {
        const std::string key = "ScriptCall refused reason="; const auto at = m.find(key);
        if (at != std::string::npos) { const auto start = at + key.size(); lastReason = m.substr(start, m.find(' ', start) - start); }
    };
    SessionHost server(config, relayCallbacks); if (!server.start()) { check(false, "script call ENet bind"); return; }
    std::array<std::vector<ScriptCall>, 3> received;
    std::array<std::vector<ScriptReactionCompletion>, 3> completions;
    std::array<std::unique_ptr<NetworkClient>, 3> clients;
    auto make = [&](unsigned i) {
        ClientCallbacks cb;
        cb.onWorldEnvelope = [&, i](const WorldEnvelope& e) {
            if(e.packet.front()==static_cast<std::uint8_t>(PacketType::ScriptReactionCompletion)) {
                const std::uint8_t* p;std::size_t n;decodePacketHeader(e.packet.data(),e.packet.size(),p,n);
                ByteReader r(p,n);ScriptReactionCompletion c;read(r,c);completions[i].push_back(c);return;
            }
            if (e.packet.front() != static_cast<std::uint8_t>(PacketType::ScriptCall)) return;
            const std::uint8_t* p; std::size_t n; decodePacketHeader(e.packet.data(), e.packet.size(), p, n);
            ByteReader r(p, n); ScriptCall c; read(r, c); received[i].push_back(c);
        };
        clients[i] = std::make_unique<NetworkClient>("127.0.0.1", config.port, "script-call-test", "none", "sc" + std::to_string(i),
                                                     static_cast<SlotType>(i), cb);
        clients[i]->connect();
    };
    auto pump = [&] { for (unsigned k = 0; k < 30; ++k) { server.tick(); for (auto& c : clients) if (c) c->tick(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); } };
    make(0); make(1); pump(); pump();
    check(server.verifiedPeerCount() == 2, "script call: host and first friend admitted");
    if (server.verifiedPeerCount() != 2) { for (auto& c : clients) if (c) c->disconnect(); server.stop(); return; }
    const RoomTransition room{20, 2, 0x19, 0, 0, 149, 0};
    clients[0]->sendRoomTransition(room); pump();
    EnemyManifest manifest; manifest.epoch = room.epoch;
    EnemyManifestEntry enemy; enemy.netId = 3; enemy.objectId = 0x1FF; manifest.entries.push_back(enemy);
    clients[0]->sendEnemyManifest(manifest); pump();
    const auto hostId = server.peerBySlot(SlotType::Player)->connectionId;
    auto calls = sample(); calls.location = room; calls.seq = 1; calls.hostConnectionId = hostId;
    clients[0]->sendScriptCall(calls); pump();
    check(received[1].size() == 1 && received[0].empty() && received[1][0].entries.size() == 2 && received[1][0].entries[0].table == 8,
          "script call forwarded to the friend, not back to the host");
    make(2); pump(); pump();
    check(server.verifiedPeerCount() == 3 && received[2].empty(), "late joiner gets no script call replay");
    ++calls.seq; clients[0]->sendScriptCall(calls); pump();
    check(received[1].size() == 2 && received[2].size() == 1 && received[0].empty(), "script call forwarded to both clients");
    auto total = [&] { return received[0].size() + received[1].size() + received[2].size(); };
    auto refusedBy = [&](const char* reason, const char* label, const auto& send) {
        const auto before = total(); const auto rejected = server.rejectedWorldMessages(); lastReason.clear();
        send(); pump();
        check(total() == before && server.rejectedWorldMessages() == rejected + 1 && lastReason == reason, label);
        if (lastReason != reason) std::cout << "  relay reason=" << lastReason << " expected=" << reason << '\n';
    };
    {   auto forged = calls; forged.seq = 1000;
        refusedBy("sender-not-verified-host", "non-host script call refused", [&] {
            clients[1]->sendNativeWorld(encode(forged), ProducerWorldContext{1, clients[1]->deliverySerial(), 0}, true); });
    }
    refusedBy("sequence", "duplicate script call sequence refused", [&] { clients[0]->sendScriptCall(calls); });
    auto stale = calls; ++calls.seq; stale.seq = calls.seq; stale.location.epoch = 19;
    refusedBy("room-mismatch", "old room epoch script call refused", [&] { clients[0]->sendScriptCall(stale); });
    auto forgedHost = calls; ++calls.seq; forgedHost.seq = calls.seq; forgedHost.hostConnectionId = hostId + 1;
    refusedBy("host-connection", "forged host connection script call refused", [&] { clients[0]->sendScriptCall(forgedHost); });
    ++calls.seq; clients[0]->sendScriptCall(calls); pump();
    check(received[1].size() == 3 && received[2].size() == 2, "a valid script call after refusals still forwards");
    ScriptReactionCompletion completion;completion.location=room;completion.seq=1;
    completion.requesterConnectionId=server.peerBySlot(SlotType::Friend1)->connectionId;
    completion.occurrence=1;completion.resource=1;completion.objectId=enemy.objectId;completion.netId=enemy.netId;completion.command=345;
    const auto sendCompletion=[&](unsigned slot,const ScriptReactionCompletion& m){
        return clients[slot]->sendNativeWorld(encode(m),ProducerWorldContext{1,clients[slot]->deliverySerial(),0},true);
    };
    check(sendCompletion(1,completion),"owner submits reaction completion through native world envelope");pump();
    check(completions[0].size()==1 && completions[0][0].ownerSlot==1 && completions[1].empty() && completions[2].empty(),
          "relay stamps reaction owner and delivers only to verified host");
    const auto refusedCompletion=[&](const ScriptReactionCompletion& m,const char* label){
        const auto before=server.rejectedWorldMessages();sendCompletion(1,m);pump();
        check(server.rejectedWorldMessages()==before+1 && completions[0].size()==1,label);
    };
    refusedCompletion(completion,"duplicate completion sequence cannot arrive twice");
    auto forgedCompletion=completion;forgedCompletion.seq=2;++forgedCompletion.requesterConnectionId;
    refusedCompletion(forgedCompletion,"forged reaction owner connection is refused");
    forgedCompletion=completion;forgedCompletion.seq=3;++forgedCompletion.location.epoch;
    refusedCompletion(forgedCompletion,"old reaction room cannot grant a callback");
    forgedCompletion=completion;forgedCompletion.seq=4;++forgedCompletion.objectId;
    refusedCompletion(forgedCompletion,"unannounced reaction source is refused");
    // Native-side diagnostics have no control over relay sequence/dispatch.
    // Exhaust transport and native-lifecycle telemetry independently, then
    // exercise the real transport. Neither logger can consume gameplay work.
    for(const auto stage:{rcdiag::Stage::RelayCompletion,rcdiag::Stage::LifecycleQuery,
                         rcdiag::Stage::LifecycleCancel,rcdiag::Stage::LifecycleCapture})
        for(unsigned i=0;i<rcdiag::Limit(stage)+100;++i)
            rcdiag::Emit(stage,"test","budget",{},[](const char*){});
    auto corrected=completion;corrected.seq=4;
    refusedCompletion(corrected,"manifest rejection permanently consumed wire sequence before diagnostic saturation");
    corrected.seq=5;check(sendCompletion(1,corrected),"fresh type55 submission still returns true after trace saturation");pump();
    check(completions[0].size()==2 && completions[0].back().seq==5 && completions[0].back().ownerSlot==1,
          "saturated diagnostics preserve exactly one owner-stamped native completion callback");
    const auto beforeContextReject=server.rejectedWorldMessages();
    check(!clients[1]->sendNativeWorld(encode(corrected),ProducerWorldContext{1,clients[1]->deliverySerial()+1,0},true),
          "retired producer delivery still refuses before transport despite diagnostics");pump();
    check(server.rejectedWorldMessages()==beforeContextReject && completions[0].size()==2,
          "producer refusal neither submits a relay packet nor invokes owner callback");

    // A terminal source's deferred end cannot poison an unrelated living
    // source's end in the same reliable batch.
    auto living=enemy;living.netId=4;manifest.entries.push_back(living);
    clients[0]->sendEnemyManifest(manifest);pump();
    clients[0]->sendEnemyDeath({room.epoch,enemy.netId});pump();
    ScriptCall ends=calls;ends.seq=++calls.seq;ends.entries.clear();
    ScriptCallEntry end;end.netId=enemy.netId;end.objectId=enemy.objectId;end.table=2;end.index=13;end.effect=11;
    ends.entries.push_back(end);end.netId=living.netId;end.effect=12;ends.entries.push_back(end);
    const auto prior=received[1].size();clients[0]->sendScriptCall(ends);pump();
    check(received[1].size()==prior+1 && received[1].back().entries.size()==2,
          "terminal and living owners' paired attack ends forward in one batch");
    auto deadStart=ends;deadStart.seq=++calls.seq;deadStart.entries.resize(1);
    deadStart.entries[0].index=10;
    for(unsigned k=0;k<3;++k)deadStart.entries[0].args[k]=num(0);
    refusedBy("actor-identity","terminal source cannot create another attack",[&]{clients[0]->sendScriptCall(deadStart);});
    auto wrongEnd=ends;wrongEnd.seq=++calls.seq;++wrongEnd.entries[0].objectId;
    refusedBy("actor-identity","terminal teardown still requires announced source identity",[&]{clients[0]->sendScriptCall(wrongEnd);});
    // Existing host-only reliable ScriptCall admission also carries the typed
    // metadata. Owner-side tombstones and parent-terminal teardown are separate integration.
    auto parent=enemy;parent.netId=5;parent.objectId=307;manifest.entries.push_back(parent);
    auto child=enemy;child.netId=6;child.objectId=317;manifest.entries.push_back(child);
    clients[0]->sendEnemyManifest(manifest);pump();
    ScriptCall birth=calls;birth.seq=++calls.seq;birth.entries={actorBirthEntry(SCRIPT_ACTOR_BIRTH_BEGIN,6,5)};
    auto beforeBirth=received[1].size();auto beforeBirth2=received[2].size();
    clients[0]->sendScriptCall(birth);pump();
    check(received[1].size()==beforeBirth+1 && received[2].size()==beforeBirth2+1 && received[0].empty() &&
          received[1].back().entries.front().index==SCRIPT_ACTOR_BIRTH_BEGIN &&
          received[1].back().entries.front().effect==6 && received[1].back().entries.front().objectId==307,
          "host-only reliable path forwards exact source/child birth metadata to both friends");
    auto birthEnd=birth;birthEnd.seq=++calls.seq;birthEnd.entries.front().index=SCRIPT_ACTOR_BIRTH_END;
    clients[0]->sendScriptCall(birthEnd);pump();
    check(received[1].size()==beforeBirth+2 && received[2].size()==beforeBirth2+2 &&
          received[1].back().entries.front().index==SCRIPT_ACTOR_BIRTH_END &&
          received[1].back().entries.front().args==birth.entries.front().args,
          "birth end preserves the immutable tuple on the same reliable path while parent is current");
    auto forgedBirth=birth;forgedBirth.seq=++calls.seq;
    refusedBy("sender-not-verified-host","friend cannot forge an actor birth descriptor",[&]{
        clients[1]->sendNativeWorld(encode(forgedBirth),ProducerWorldContext{1,clients[1]->deliverySerial(),0},true);});
    auto unknownParent=birth;unknownParent.seq=++calls.seq;unknownParent.entries.front().netId=living.netId;
    refusedBy("actor-identity","birth source must match its announced parent class",[&]{clients[0]->sendScriptCall(unknownParent);});
    auto staleBirth=birth;staleBirth.seq=++calls.seq;++staleBirth.location.epoch;
    refusedBy("room-mismatch","birth cannot cross the admitted room scope",[&]{clients[0]->sendScriptCall(staleBirth);});
    for (auto& c : clients) if (c) c->disconnect();
    server.stop();
}
} // namespace

int main() {
    if (enet_initialize() != 0) return 2;
    reactionChecks();
    completionCodecChecks();
    ownerCodecChecks();
    codecChecks();
    actorBirthCodecChecks();
    actorBirthOrderedChecks();
    rulesAgreeWithCodec();
    senderChecks();
    holdChecks();
    effectChecks();
    attackLifetimeChecks();
    storedResourceHandleChecks();
    nativeScalarChecks();
    enetChecks();
    enet_deinitialize();
    std::cout << "failures=" << errors << '\n';
    if (!errors) std::cout << "ALL CHECKS PASSED\n";
    return errors ? 1 : 0;
}
