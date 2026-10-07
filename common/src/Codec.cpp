#include "kh2coop/Codec.hpp"
#include "kh2coop/ProgressAllowList.hpp"
#include "kh2coop/NativeRecordContent.hpp"
#include <map>
#include <set>

#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <algorithm>
#include <bit>
#include <limits>
#include <utility>

namespace kh2coop {

namespace {
void rsRequire(bool value) { if (!value) throw std::runtime_error("resync: invalid bounded record"); }
void rsEnd(ByteReader& r) { rsRequire(r.atEnd()); }
void rsSession(const std::string& s) {
    rsRequire(s.size()==32 && std::all_of(s.begin(),s.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');}));
}
void rsKey(ByteWriter& w,const ResyncKey& k) { rsSession(k.sessionId);rsRequire(k.hostConnectionId&&k.requestId);w.writeString(k.sessionId);w.writeU64(k.hostConnectionId);w.writeU64(k.requestId); }
void rsKey(ByteReader& r,ResyncKey& k) { k.sessionId=r.readString();k.hostConnectionId=r.readU64();k.requestId=r.readU64();ByteWriter w;rsKey(w,k); }
void rsDigest(ByteWriter& w,const ResyncDigest& d) { for(auto b:d)w.writeU8(b); }
void rsDigest(ByteReader& r,ResyncDigest& d) { for(auto& b:d)b=r.readU8(); }
void rsTarget(ByteWriter& w,const ResyncTarget& t) { rsRequire(t.slot>=1&&t.slot<=2&&t.connectionId&&t.deliverySerial);w.writeU8(t.slot);w.writeU64(t.connectionId);w.writeU64(t.deliverySerial); }
void rsTarget(ByteReader& r,ResyncTarget& t) { t.slot=r.readU8();t.connectionId=r.readU64();t.deliverySerial=r.readU64();ByteWriter w;rsTarget(w,t); }
void rsTargets(ByteWriter& w,const std::array<ResyncTarget,2>& ts,std::uint8_t n) { rsRequire(n>=1&&n<=2);w.writeU8(n);for(std::size_t i=0;i<n;++i){rsTarget(w,ts[i]);if(i)rsRequire(ts[i].slot>ts[i-1].slot&&ts[i].connectionId!=ts[i-1].connectionId);} }
void rsTargets(ByteReader& r,std::array<ResyncTarget,2>& ts,std::uint8_t& n) { n=r.readU8();rsRequire(n>=1&&n<=2);for(std::size_t i=0;i<n;++i)rsTarget(r,ts[i]);ByteWriter w;rsTargets(w,ts,n); }
void rsPhase(ByteWriter& w,ResyncPhase p) { rsRequire(p==ResyncPhase::Bootstrap||p==ResyncPhase::Checkpoint);w.writeU8(static_cast<std::uint8_t>(p)); }
void rsPhase(ByteReader& r,ResyncPhase& p) { p=static_cast<ResyncPhase>(r.readU8());ByteWriter w;rsPhase(w,p); }
void rsRequest(ByteWriter& w,const ResyncRequest& m) {
    rsKey(w,m.key);rsRequire(m.room.epoch&&m.targetMask&&!(m.targetMask&~6u)&&m.connections[0]==m.key.hostConnectionId);
    for(std::size_t i=1;i<3;++i)if(m.targetMask&(1u<<i))rsRequire(m.connections[i]&&m.connections[i]!=m.connections[0]);
    rsRequire(!m.connections[1]||m.connections[1]!=m.connections[2]);
    write(w,m.room);w.writeU8(m.targetMask);for(auto c:m.connections)w.writeU64(c);
}
void rsRequest(ByteReader& r,ResyncRequest& m) { rsKey(r,m.key);read(r,m.room);m.targetMask=r.readU8();for(auto& c:m.connections)c=r.readU64();ByteWriter w;rsRequest(w,m); }
void rsBegin(ByteWriter& w,const ResyncBegin& m) {
    rsKey(w,m.key);rsPhase(w,m.phase);rsRequire(m.room.epoch&&m.snapshotCut&&m.totalBytes&&m.totalBytes<=RESYNC_MAX_SNAPSHOT_BYTES&&m.partCount==(m.totalBytes+RESYNC_MAX_PART_BYTES-1)/RESYNC_MAX_PART_BYTES);
    write(w,m.room);rsTargets(w,m.targets,m.targetCount);w.writeU64(m.snapshotCut);w.writeU32(m.totalBytes);w.writeU16(m.partCount);rsDigest(w,m.sha256);
}
void rsBegin(ByteReader& r,ResyncBegin& m) {rsKey(r,m.key);rsPhase(r,m.phase);read(r,m.room);rsTargets(r,m.targets,m.targetCount);m.snapshotCut=r.readU64();m.totalBytes=r.readU32();m.partCount=r.readU16();rsDigest(r,m.sha256);ByteWriter w;rsBegin(w,m);}
std::map<std::uint32_t,std::uint8_t> rsProgress(const ProgressUpdate& p) {
    rsRequire(p.full);std::map<std::uint32_t,std::uint8_t> out;
    for(const auto& s:p.spans){rsRequire(!s.bytes.empty()&&s.offset<=UINT32_MAX-s.bytes.size());for(std::size_t i=0;i<s.bytes.size();++i){auto o=s.offset+static_cast<std::uint32_t>(i);auto mask=verifiedProgressByteMask(o);rsRequire(mask&&(s.bytes[i]&~mask)==0&&out.emplace(o,s.bytes[i]).second);}}
    std::size_t count=0;for(const auto& r:verifiedProgressAllowList())count+=r.length;
    rsRequire(out.size()==count);return out;
}
void rsEnemy(ByteWriter& w,const ResyncEnemyState& e) {
    const auto& i=e.identity;rsRequire(i.netId&&i.objectId&&(e.objectType==3||e.objectType==4)&&std::isfinite(i.spawnPosition.x)&&std::isfinite(i.spawnPosition.y)&&std::isfinite(i.spawnPosition.z)&&e.maxHp>0);
    rsRequire((e.life==ResyncLife::Alive&&e.hp>0&&e.hp<=e.maxHp)||(e.life==ResyncLife::ObservedDeadHistory&&e.hp<=0));
    w.writeU16(i.netId);w.writeU16(i.battleProgram);w.writeU16(i.spawnIndex);w.writeU32(i.objectId);write(w,i.spawnPosition);w.writeU32(e.objectType);w.writeI32(e.hp);w.writeI32(e.maxHp);w.writeU8(static_cast<std::uint8_t>(e.life));w.writeU16(e.record.definitionIndex);w.writeU16(e.record.recordIndex);
}
void rsEnemy(ByteReader& r,ResyncEnemyState& e) {auto& i=e.identity;i.netId=r.readU16();i.battleProgram=r.readU16();i.spawnIndex=r.readU16();i.objectId=r.readU32();read(r,i.spawnPosition);e.objectType=r.readU32();e.hp=r.readI32();e.maxHp=r.readI32();e.life=static_cast<ResyncLife>(r.readU8());e.record.definitionIndex=r.readU16();e.record.recordIndex=r.readU16();ByteWriter w;rsEnemy(w,e);}
void rsRecordDefinition(ByteWriter& w, const NativeRecordContentDefinition& d, bool projected) {
    rsDigest(w, d.layoutSha256);
    w.writeU16(d.location.world); w.writeU16(d.location.room); w.writeU16(d.location.door);
    w.writeU16(d.location.mapProgram); w.writeU16(d.location.battleProgram); w.writeU16(d.location.eventProgram);
    w.writeU32(d.groupKey);
    for (std::size_t i = 0; i < d.header.size(); ++i)
        if (!projected || i != 0xE) w.writeU8(d.header[i]);
    for (const auto& record : d.records) for (const auto byte : record) w.writeU8(byte);
}
void rsRecordDefinition(ByteReader& r, NativeRecordContentDefinition& d, std::size_t& total) {
    rsRequire(r.remaining() >= 92); // fixed fields, before allocating records
    rsDigest(r, d.layoutSha256);
    d.location = {r.readU16(), r.readU16(), r.readU16(), r.readU16(), r.readU16(), r.readU16()};
    d.groupKey = r.readU32();
    for (auto& byte : d.header) byte = r.readU8();
    const auto count = native_record_detail::u16(d.header.data() + 4);
    rsRequire(count <= NativeRecordContentMaxRecords && count <= NativeRecordContentMaxTotalRecords - total &&
        static_cast<std::size_t>(count) * 64 <= r.remaining());
    total += count;
    d.records.resize(count);
    for (auto& record : d.records) for (auto& byte : record) byte = r.readU8();
}
void rsSnapshotBytes(std::size_t& total, std::size_t bytes) {
    rsRequire(total <= RESYNC_MAX_SNAPSHOT_BYTES && bytes <= RESYNC_MAX_SNAPSHOT_BYTES - total);
    total += bytes;
}
constexpr std::uint32_t rsActivationTag = 0x50524148; // "HARP", little endian
constexpr std::uint16_t rsActivationVersion = 1;
constexpr std::uint16_t rsActivationPayloadBytes = 1120;
constexpr std::size_t rsActivationTrailerBytes = 8 + rsActivationPayloadBytes;
void rsActivationState(ByteWriter& w, const ResyncActivationState& s) {
    w.writeU32(s.flags); w.writeU32(s.currentCount); w.writeU32(s.initialCount);
    w.writeF32(s.cooldown); w.writeU8(s.stage); w.writeU8(s.activation); w.writeU8(s.nativeType);
    w.writeU16(s.headerId); w.writeU16(s.recordCount);
    w.writeI32(s.cacheRoom); w.writeI32(s.cacheAge);
    for (const auto id : s.cacheIds) w.writeU16(id);
}
void rsActivationState(ByteReader& r, ResyncActivationState& s) {
    s.flags = r.readU32(); s.currentCount = r.readU32(); s.initialCount = r.readU32();
    s.cooldown = r.readF32(); s.stage = r.readU8(); s.activation = r.readU8(); s.nativeType = r.readU8();
    s.headerId = r.readU16(); s.recordCount = r.readU16();
    s.cacheRoom = r.readI32(); s.cacheAge = r.readI32();
    for (auto& id : s.cacheIds) id = r.readU16();
}
void rsActivationReplay(ByteWriter& w, const ResyncActivationReplay& a) {
    w.writeU32(rsActivationTag); w.writeU16(rsActivationVersion); w.writeU16(rsActivationPayloadBytes);
    w.writeU16(a.definitionIndex);
    for (const auto value : a.point) w.writeF32(value);
    rsActivationState(w, a.before); rsActivationState(w, a.current);
    w.writeU64(a.firstUpdateSequence); w.writeU32(a.hostTransition); w.writeU32(a.hostLoad);
}
void rsActivationReplay(ByteReader& r, ResyncActivationReplay& a) {
    // A single fixed-size trailer only: unknown versions, duplicates, partial
    // extensions and trailing garbage cannot be silently skipped.
    rsRequire(r.remaining() == rsActivationTrailerBytes);
    rsRequire(r.readU32() == rsActivationTag);
    rsRequire(r.readU16() == rsActivationVersion);
    rsRequire(r.readU16() == rsActivationPayloadBytes);
    a.definitionIndex = r.readU16();
    for (auto& value : a.point) value = r.readF32();
    rsActivationState(r, a.before); rsActivationState(r, a.current);
    a.firstUpdateSequence = r.readU64(); a.hostTransition = r.readU32(); a.hostLoad = r.readU32();
}
void rsActivationValid(const ResyncSnapshot& m) {
    if (!m.activationReplay) return;
    const auto& a = *m.activationReplay;
    rsRequire(m.coverageMask == ResyncNativeComplete && m.enemies.size() == 5 &&
        m.livingCount == 5 && !m.deadCount && !m.hold.active &&
        a.definitionIndex < m.recordDefinitions.size() && a.firstUpdateSequence &&
        a.hostTransition && a.hostTransition == m.transitionSerial && a.hostLoad && a.hostLoad == m.loadSerial);
    for (const auto value : a.point) rsRequire(std::isfinite(value));
    const auto& d = m.recordDefinitions[a.definitionIndex];
    using native_record_detail::u16;
    constexpr std::array<std::uint16_t, 5> ids{11, 12, 13, 14, 18};
    // Same deliberately narrow content subset as surviving-pack preparation.
    rsRequire(d.groupKey == 808476514 && d.header[0] == 2 && u16(d.header.data() + 2) == 30 &&
        d.records.size() == ids.size() && u16(d.header.data() + 4) == ids.size());
    for (std::size_t i = 0; i < ids.size(); ++i) {
        const auto& r = d.records[i];
        rsRequire(r[0] == 0x2e && r[1] == 1 && !r[2] && !r[3] && r[0x1c] == 2 && !r[0x1d] &&
            u16(r.data() + 0x1e) == ids[i] && u16(r.data() + 0x2a) == 8 && !r[0x30]);
    }
    std::array<bool, 5> seen{};
    for (const auto& e : m.enemies) {
        rsRequire(e.record.definitionIndex == a.definitionIndex && e.record.recordIndex < seen.size() &&
            !seen[e.record.recordIndex] && e.identity.objectId == 302 && e.objectType == 4 &&
            e.life == ResyncLife::Alive);
        seen[e.record.recordIndex] = true;
    }
    for (const auto* s : {&a.before, &a.current}) {
        rsRequire(std::isfinite(s->cooldown) && s->nativeType == 2 && s->headerId == 30 &&
            s->recordCount == 5 && s->initialCount == 5 && s->currentCount <= 5 &&
            s->activation <= 1 && s->cacheRoom == m.room.roomId);
        std::array<bool, 5> cached{};
        for (const auto id : s->cacheIds) {
            if (!id) continue;
            const auto found = std::find(ids.begin(), ids.end(), id);
            rsRequire(found != ids.end());
            const auto index = static_cast<std::size_t>(found - ids.begin());
            rsRequire(!cached[index]); cached[index] = true;
        }
        if (s == &a.current)
            rsRequire(std::all_of(cached.begin(), cached.end(), [](bool present) { return present; }));
    }
    // Counts are remaining population, not emitted count. Preserve the actual
    // reviewed pre-emission state; +E is the retained native activation marker.
    rsRequire(a.before.flags == 2 && a.before.currentCount == 5 && a.before.initialCount == 5 &&
        a.before.cooldown == 0 && a.before.stage == 0 && a.before.activation == 0 &&
        std::all_of(a.before.cacheIds.begin(), a.before.cacheIds.end(), [](auto id) { return id == 0; }) &&
        a.current.activation == d.header[0xE]);
}
void rsSnapshotValid(const ResyncSnapshot& m) {
    rsRequire(m.room.epoch&&m.hold.epoch==m.room.epoch&&m.hold.eventProgram==m.room.eventProgram&&m.progress.version&&(m.coverageMask==ResyncComplete||m.coverageMask==ResyncNativeComplete)&&m.hpSequence&&m.generation&&m.loadSerial&&m.captureFrameBefore&&m.captureFrameAfter>=m.captureFrameBefore&&m.enemies.size()<=RESYNC_MAX_ENEMIES);
    rsProgress(m.progress);std::uint32_t live=0,dead=0;std::uint16_t last=0;
    for(const auto& e:m.enemies){ByteWriter w;rsEnemy(w,e);rsRequire(e.identity.netId>last&&e.identity.battleProgram==m.room.battleProgram);last=e.identity.netId;if(e.life==ResyncLife::Alive)++live;else ++dead;}
    rsRequire(live==m.livingCount&&dead==m.deadCount);
    rsActivationValid(m);
    // Exact v9 encoded size: fixed snapshot fields, progress spans, 39-byte
    // enemy rows, and 92-byte definition prefixes plus every 64-byte record.
    std::size_t bytes = 114;
    if (m.activationReplay) rsSnapshotBytes(bytes, rsActivationTrailerBytes);
    for (const auto& span : m.progress.spans) rsSnapshotBytes(bytes, 6 + span.bytes.size());
    rsSnapshotBytes(bytes, m.enemies.size() * 39);
    rsRequire(m.recordDefinitions.size() <= NativeRecordContentMaxDefinitions);
    if (m.coverageMask == ResyncComplete) {
        rsRequire(m.recordDefinitions.empty());
        for (const auto& e : m.enemies) rsRequire(e.record == ResyncRecordReference{});
        return;
    }
    const NativeRecordLocation location {m.room.worldId, m.room.roomId, m.room.door,
        m.room.mapProgram, m.room.battleProgram, m.room.eventProgram};
    std::size_t records = 0;
    std::vector<std::uint16_t> ids;
    for (std::size_t i = 0; i < m.recordDefinitions.size(); ++i) {
        const auto& d = m.recordDefinitions[i];
        rsRequire(native_record_detail::validate(d) == NativeRecordContentStatus::Complete &&
            (d.header[0] == 1 || d.header[0] == 2) && d.location == location &&
            d.layoutSha256 == m.recordDefinitions.front().layoutSha256 &&
            d.records.size() <= NativeRecordContentMaxTotalRecords - records);
        records += d.records.size();
        rsSnapshotBytes(bytes, 92 + d.records.size() * 64);
        for (std::size_t j = 0; j < i; ++j)
            rsRequire(!native_record_detail::descriptorEqual(d, m.recordDefinitions[j]));
        for (const auto& record : d.records) ids.push_back(native_record_detail::u16(record.data() + 0x1E));
    }
    std::sort(ids.begin(), ids.end());
    rsRequire(std::adjacent_find(ids.begin(), ids.end()) == ids.end());
    std::set<std::pair<std::uint16_t, std::uint16_t>> references;
    for (const auto& e : m.enemies) {
        rsRequire(e.record.definitionIndex < m.recordDefinitions.size());
        const auto& d = m.recordDefinitions[e.record.definitionIndex];
        rsRequire(e.record.recordIndex < d.records.size() &&
            references.emplace(e.record.definitionIndex, e.record.recordIndex).second);
        ByteReader record(d.records[e.record.recordIndex].data(), 4);
        rsRequire(record.readU32() == e.identity.objectId); // native object ID is full u32
    }
}
}
bool sameResyncRoom(const RoomTransition& a,const RoomTransition& b) {return a.epoch==b.epoch&&a.worldId==b.worldId&&a.roomId==b.roomId&&a.door==b.door&&a.mapProgram==b.mapProgram&&a.battleProgram==b.battleProgram&&a.eventProgram==b.eventProgram;}
bool isScopedWorldPacket(PacketType t) {return (isWorldPacket(t)&&t!=PacketType::ResyncRequest)||t==PacketType::ActorSnapshot||t==PacketType::EnemySnapshot||t==PacketType::EventMessage;}
bool isMaterialWorldPacket(PacketType t) {return t==PacketType::RoomTransition||t==PacketType::EventHold||t==PacketType::EnemyManifest||t==PacketType::EnemyHp||t==PacketType::EnemyDeath||t==PacketType::ProgressUpdate;}
PacketType validateScopedWorldPacket(const std::vector<std::uint8_t>& bytes) {
    const std::uint8_t* p;std::size_t n;const auto type=decodePacketHeader(bytes.data(),bytes.size(),p,n);
    rsRequire(bytes.size()==n+3&&isScopedWorldPacket(type));ByteReader r(p,n);
    switch(type) {
#define RS_INNER(T) case PacketType::T: {T value;read(r,value);break;}
        RS_INNER(RoomTransition) RS_INNER(EventHold) RS_INNER(EnemyManifest)
        RS_INNER(EnemyHp) RS_INNER(EnemyDeath) RS_INNER(EnemyMotion) RS_INNER(ProgressUpdate)
        RS_INNER(PartyLayout) RS_INNER(PartyReapply) RS_INNER(PartyIntent) RS_INNER(ReviveRequest) RS_INNER(HitClaim) RS_INNER(TransitionAck) RS_INNER(StateHash)
        RS_INNER(DesyncNotice) RS_INNER(ActivationRequest) RS_INNER(HostActivationPoint)
        RS_INNER(ActorSnapshot) RS_INNER(EnemySnapshot) RS_INNER(EventMessage)
#undef RS_INNER
        default:rsRequire(false);
    }
    rsEnd(r);if(isEphemeralWorldPacket(type))validateActivationPacket(bytes);return type;
}
void write(ByteWriter& w,const ResyncRequest& m){rsRequest(w,m);}
void read(ByteReader& r,ResyncRequest& out){ResyncRequest m;rsRequest(r,m);rsEnd(r);out=std::move(m);}
void write(ByteWriter& w,const WorldBinding& m){rsSession(m.sessionId);rsRequire(m.hostConnectionId&&m.selfConnectionId&&m.selfSlot<3&&m.deliverySerial);w.writeString(m.sessionId);w.writeU64(m.hostConnectionId);w.writeU64(m.selfConnectionId);w.writeU8(m.selfSlot);w.writeU64(m.deliverySerial);}
void read(ByteReader& r,WorldBinding& out){WorldBinding m;m.sessionId=r.readString();m.hostConnectionId=r.readU64();m.selfConnectionId=r.readU64();m.selfSlot=r.readU8();m.deliverySerial=r.readU64();rsEnd(r);ByteWriter w;write(w,m);out=std::move(m);}
void write(ByteWriter& w,const WorldEnvelope& m){const auto& s=m.scope;rsSession(s.sessionId);rsRequire(s.sourceConnectionId&&s.sourceDeliverySerial&&m.packet.size()<=62000);const std::uint8_t* p;std::size_t n;auto t=decodePacketHeader(m.packet.data(),m.packet.size(),p,n);rsRequire(isScopedWorldPacket(t)&&m.packet.size()==n+3);validateScopedWorldPacket(m.packet);const bool legacy=t==PacketType::ActorSnapshot||t==PacketType::EnemySnapshot||t==PacketType::EventMessage;rsRequire((s.kind==WorldSourceKind::Native&&!legacy&&t!=PacketType::DesyncNotice)||(s.kind==WorldSourceKind::Simulation&&legacy)||(s.kind==WorldSourceKind::Relay&&(t==PacketType::DesyncNotice||t==PacketType::PartyReapply)));w.writeString(s.sessionId);w.writeU64(s.sourceConnectionId);w.writeU64(s.sourceDeliverySerial);w.writeU64(s.hostSourceSerial);w.writeU64(s.targetConnectionId);w.writeU64(s.targetDeliverySerial);w.writeU8(static_cast<std::uint8_t>(s.kind));w.writeU16(static_cast<std::uint16_t>(m.packet.size()));for(auto b:m.packet)w.writeU8(b);}
void read(ByteReader& r,WorldEnvelope& out){WorldEnvelope m;auto& s=m.scope;s.sessionId=r.readString();s.sourceConnectionId=r.readU64();s.sourceDeliverySerial=r.readU64();s.hostSourceSerial=r.readU64();s.targetConnectionId=r.readU64();s.targetDeliverySerial=r.readU64();s.kind=static_cast<WorldSourceKind>(r.readU8());auto n=r.readU16();rsRequire(n<=62000&&n==r.remaining());m.packet.reserve(n);while(!r.atEnd())m.packet.push_back(r.readU8());ByteWriter w;write(w,m);out=std::move(m);}
void write(ByteWriter& w,const ResyncPlan& m){rsRequest(w,m.request);rsTargets(w,m.targets,m.targetCount);rsRequire(m.remainingMs&&m.remainingMs<=RESYNC_TIMEOUT_MS&&static_cast<unsigned>(m.stage)<=1);w.writeU32(m.remainingMs);rsPhase(w,m.phase);w.writeU8(static_cast<std::uint8_t>(m.stage));}
void read(ByteReader& r,ResyncPlan& out){ResyncPlan m;rsRequest(r,m.request);rsTargets(r,m.targets,m.targetCount);m.remainingMs=r.readU32();rsPhase(r,m.phase);m.stage=static_cast<ResyncPlanStage>(r.readU8());rsEnd(r);ByteWriter w;write(w,m);out=std::move(m);}
void write(ByteWriter& w,const ResyncBegin& m){rsBegin(w,m);}
void read(ByteReader& r,ResyncBegin& out){ResyncBegin m;rsBegin(r,m);rsEnd(r);out=std::move(m);}
void write(ByteWriter& w,const ResyncPart& m){rsKey(w,m.key);rsPhase(w,m.phase);rsRequire(m.snapshotCut&&!m.bytes.empty()&&m.bytes.size()<=RESYNC_MAX_PART_BYTES&&m.offset<=RESYNC_MAX_SNAPSHOT_BYTES-m.bytes.size());w.writeU64(m.snapshotCut);w.writeU32(m.offset);w.writeU16(static_cast<std::uint16_t>(m.bytes.size()));for(auto b:m.bytes)w.writeU8(b);}
void read(ByteReader& r,ResyncPart& out){ResyncPart m;rsKey(r,m.key);rsPhase(r,m.phase);m.snapshotCut=r.readU64();m.offset=r.readU32();auto n=r.readU16();rsRequire(n==r.remaining()&&n<=RESYNC_MAX_PART_BYTES);while(!r.atEnd())m.bytes.push_back(r.readU8());ByteWriter w;write(w,m);out=std::move(m);}
void write(ByteWriter& w,const ResyncEnd& m){rsKey(w,m.key);rsPhase(w,m.phase);rsRequire(m.snapshotCut&&m.totalBytes&&m.totalBytes<=RESYNC_MAX_SNAPSHOT_BYTES);w.writeU64(m.snapshotCut);w.writeU32(m.totalBytes);rsDigest(w,m.sha256);}
void read(ByteReader& r,ResyncEnd& out){ResyncEnd m;rsKey(r,m.key);rsPhase(r,m.phase);m.snapshotCut=r.readU64();m.totalBytes=r.readU32();rsDigest(r,m.sha256);rsEnd(r);ByteWriter w;write(w,m);out=std::move(m);}
ResyncDigest resyncNativeFingerprint(const ResyncSnapshot& m) {
    rsSnapshotValid(m);
    ByteWriter w;
    constexpr char domain[] = "KH2ResyncNativeFingerprint/v9";
    for (const char byte : domain) w.writeU8(static_cast<std::uint8_t>(byte)); // includes NUL
    const bool witness = m.coverageMask == ResyncNativeComplete;
    w.writeU8(witness ? 1 : 0); // explicit even for the synthetic 127 path
    write(w, m.room); write(w, m.hold);
    const auto progress = rsProgress(m.progress);
    for (const auto& [offset, byte] : progress) { w.writeU32(offset); w.writeU8(byte); }
    w.writeU32(m.livingCount); w.writeU32(m.deadCount);
    // Canonical content bytes, never native table order or hash-only equality.
    // Retain every ordered record and every header byte except the reviewed +E.
    std::vector<std::vector<std::uint8_t>> definitions;
    std::vector<std::size_t> order;
    for (const auto& d : m.recordDefinitions) {
        ByteWriter encoded; rsRecordDefinition(encoded, d, true);
        definitions.push_back(encoded.take()); order.push_back(order.size());
    }
    std::sort(order.begin(), order.end(), [&](auto a, auto b) { return definitions[a] < definitions[b]; });
    std::array<std::uint16_t, NativeRecordContentMaxDefinitions> canonical {};
    w.writeU16(static_cast<std::uint16_t>(order.size()));
    for (std::size_t i = 0; i < order.size(); ++i) {
        canonical[order[i]] = static_cast<std::uint16_t>(i);
        const auto& bytes = definitions[order[i]];
        w.writeU32(static_cast<std::uint32_t>(bytes.size()));
        for (const auto byte : bytes) w.writeU8(byte);
    }
    w.writeU16(static_cast<std::uint16_t>(m.enemies.size()));
    for (const auto& e : m.enemies) {
        w.writeU16(e.identity.netId); w.writeU32(e.identity.objectId); w.writeU32(e.objectType);
        w.writeU8(static_cast<std::uint8_t>(e.life)); w.writeI32(e.hp); w.writeI32(e.maxHp);
        if (witness) { w.writeU16(canonical[e.record.definitionIndex]); w.writeU16(e.record.recordIndex); }
    }
    return desyncSha256(w.data());
}
void write(ByteWriter& w, const ResyncSnapshot& m) {
    rsSnapshotValid(m); rsRequire(m.nativeFingerprint == resyncNativeFingerprint(m));
    const auto start = w.size();
    write(w,m.room);write(w,m.hold);write(w,m.progress);w.writeU64(m.hpSequence);
    w.writeU16(static_cast<std::uint16_t>(m.enemies.size()));for(const auto& e:m.enemies)rsEnemy(w,e);
    w.writeU32(m.livingCount);w.writeU32(m.deadCount);w.writeU32(m.coverageMask);w.writeU32(m.generation);
    w.writeU32(m.transitionSerial);w.writeU32(m.loadSerial);w.writeU64(m.captureFrameBefore);w.writeU64(m.captureFrameAfter);
    rsDigest(w,m.nativeFingerprint);
    w.writeU16(static_cast<std::uint16_t>(m.recordDefinitions.size()));
    for (const auto& d : m.recordDefinitions) rsRecordDefinition(w, d, false);
    if (m.activationReplay) rsActivationReplay(w, *m.activationReplay);
    rsRequire(w.size() - start <= RESYNC_MAX_SNAPSHOT_BYTES);
}
void read(ByteReader& r, ResyncSnapshot& out) {
    rsRequire(r.remaining() <= RESYNC_MAX_SNAPSHOT_BYTES);
    ResyncSnapshot m;read(r,m.room);m.hold.epoch=r.readU32();auto active=r.readU8();rsRequire(active<=1);
    m.hold.active=active!=0;m.hold.eventProgram=r.readU16();m.progress.version=r.readU32();auto full=r.readU8();
    rsRequire(full==1);m.progress.full=true;auto spans=r.readU16();rsRequire(spans<=8108);
    for(std::size_t i=0;i<spans;++i){ProgressSpan p;p.offset=r.readU32();auto n=r.readU16();rsRequire(n<=8108&&n<=r.remaining());for(std::size_t j=0;j<n;++j)p.bytes.push_back(r.readU8());m.progress.spans.push_back(std::move(p));}
    m.hpSequence=r.readU64();auto n=r.readU16();rsRequire(n<=RESYNC_MAX_ENEMIES&&static_cast<std::size_t>(n)*39<=r.remaining());
    for(std::size_t i=0;i<n;++i){ResyncEnemyState e;rsEnemy(r,e);m.enemies.push_back(e);}
    m.livingCount=r.readU32();m.deadCount=r.readU32();m.coverageMask=r.readU32();m.generation=r.readU32();
    m.transitionSerial=r.readU32();m.loadSerial=r.readU32();m.captureFrameBefore=r.readU64();m.captureFrameAfter=r.readU64();
    rsDigest(r,m.nativeFingerprint);
    const auto count = r.readU16();
    rsRequire(count <= NativeRecordContentMaxDefinitions && static_cast<std::size_t>(count) * 92 <= r.remaining());
    m.recordDefinitions.reserve(count);
    std::size_t records = 0;
    for (std::size_t i = 0; i < count; ++i) {
        NativeRecordContentDefinition d; rsRecordDefinition(r, d, records);
        m.recordDefinitions.push_back(std::move(d));
    }
    if (!r.atEnd()) { m.activationReplay.emplace(); rsActivationReplay(r, *m.activationReplay); }
    rsEnd(r);ByteWriter w;write(w,m);out=std::move(m);
}
void write(ByteWriter& w,const ResyncAck& m){rsKey(w,m.key);rsTarget(w,m.target);rsPhase(w,m.phase);rsRequire(static_cast<unsigned>(m.status)<=4&&m.error.size()<=256);w.writeU64(m.snapshotCut);rsDigest(w,m.snapshotSha256);rsDigest(w,m.observedFingerprint);w.writeU8(static_cast<std::uint8_t>(m.status));write(w,m.observedRoom);w.writeU32(m.enemyCount);w.writeU32(m.deadCount);w.writeU32(m.loadBefore);w.writeU32(m.loadAfter);w.writeU64(m.observationFrame1);w.writeU64(m.observationFrame2);w.writeU32(m.checksMask);w.writeString(m.error);}
void read(ByteReader& r,ResyncAck& out){ResyncAck m;rsKey(r,m.key);rsTarget(r,m.target);rsPhase(r,m.phase);m.snapshotCut=r.readU64();rsDigest(r,m.snapshotSha256);rsDigest(r,m.observedFingerprint);m.status=static_cast<ResyncAckStatus>(r.readU8());read(r,m.observedRoom);m.enemyCount=r.readU32();m.deadCount=r.readU32();m.loadBefore=r.readU32();m.loadAfter=r.readU32();m.observationFrame1=r.readU64();m.observationFrame2=r.readU64();m.checksMask=r.readU32();m.error=r.readString();rsEnd(r);ByteWriter w;write(w,m);out=std::move(m);}
void write(ByteWriter& w,const ResyncResult& m){rsKey(w,m.key);rsRequire(static_cast<unsigned>(m.reason)<=12&&m.targetCount>=1&&m.targetCount<=2);w.writeU8(static_cast<std::uint8_t>(m.reason));w.writeU8(m.targetCount);for(std::size_t i=0;i<m.targetCount;++i){const auto& t=m.targets[i];rsTarget(w,t.target);rsRequire(static_cast<unsigned>(t.status)<=4&&t.error.size()<=256);w.writeU8(static_cast<std::uint8_t>(t.status));w.writeU64(t.appliedCut);rsDigest(w,t.fingerprint);w.writeString(t.error);}}
void read(ByteReader& r,ResyncResult& out){ResyncResult m;rsKey(r,m.key);m.reason=static_cast<ResyncResultReason>(r.readU8());m.targetCount=r.readU8();rsRequire(m.targetCount>=1&&m.targetCount<=2);for(std::size_t i=0;i<m.targetCount;++i){auto& t=m.targets[i];rsTarget(r,t.target);t.status=static_cast<ResyncAckStatus>(r.readU8());t.appliedCut=r.readU64();rsDigest(r,t.fingerprint);t.error=r.readString();}rsEnd(r);ByteWriter w;write(w,m);out=std::move(m);}
#define RS_ENCODE(T) std::vector<std::uint8_t> encode(const T& m){ByteWriter w;write(w,m);return encodePacket(PacketType::T,w.data());}
RS_ENCODE(WorldBinding) RS_ENCODE(WorldEnvelope) RS_ENCODE(ResyncPlan) RS_ENCODE(ResyncBegin) RS_ENCODE(ResyncPart) RS_ENCODE(ResyncEnd) RS_ENCODE(ResyncAck) RS_ENCODE(ResyncResult)
#undef RS_ENCODE
std::vector<std::uint8_t> encodeResyncSnapshot(const ResyncSnapshot& m){ByteWriter w;write(w,m);rsRequire(w.size()<=RESYNC_MAX_SNAPSHOT_BYTES);return w.take();}
ResyncSnapshot decodeResyncSnapshot(const std::vector<std::uint8_t>& bytes){rsRequire(bytes.size()<=RESYNC_MAX_SNAPSHOT_BYTES);ByteReader r(bytes);ResyncSnapshot m;read(r,m);return m;}
std::vector<std::uint8_t> encodeNativeResyncSnapshot(const ResyncBegin& b,const ResyncSnapshot& s){auto bytes=encodeResyncSnapshot(s);rsRequire(bytes.size()==b.totalBytes&&desyncSha256(bytes)==b.sha256&&sameResyncRoom(s.room,b.room));ByteWriter w;rsBegin(w,b);for(auto v:bytes)w.writeU8(v);return encodePacket(PacketType::NativeResyncSnapshot,w.data());}
void decodeNativeResyncSnapshot(const std::vector<std::uint8_t>& bytes,ResyncBegin& b,ResyncSnapshot& s){const std::uint8_t* p;std::size_t n;rsRequire(decodePacketHeader(bytes.data(),bytes.size(),p,n)==PacketType::NativeResyncSnapshot);ByteReader r(p,n);ResyncBegin begin;rsBegin(r,begin);rsRequire(r.remaining()==begin.totalBytes);std::vector<std::uint8_t> v;while(!r.atEnd())v.push_back(r.readU8());rsRequire(desyncSha256(v)==begin.sha256);auto snapshot=decodeResyncSnapshot(v);rsRequire(sameResyncRoom(begin.room,snapshot.room));b=std::move(begin);s=std::move(snapshot);}
std::vector<std::uint8_t> encodeLocalResyncCommand(std::uint8_t mask){rsRequire(mask&&!(mask&~6u));return encodePacket(PacketType::LocalResyncCommand,{mask});}
void ResyncAssembler::Reset(){begin_.reset();bytes_.clear();parts_=0;}
bool ResyncAssembler::Begin(const ResyncBegin& b){try{ByteWriter w;write(w,b);if(begin_)return encode(*begin_)==encode(b);begin_=b;bytes_.clear();bytes_.reserve(b.totalBytes);parts_=0;return true;}catch(const std::exception&){Reset();return false;}}
bool ResyncAssembler::Part(const ResyncPart& p){try{ByteWriter w;write(w,p);rsRequire(begin_&&p.key==begin_->key&&p.phase==begin_->phase&&p.snapshotCut==begin_->snapshotCut&&p.offset+p.bytes.size()<=begin_->totalBytes);rsRequire(p.offset%RESYNC_MAX_PART_BYTES==0&&p.bytes.size()==std::min<std::size_t>(RESYNC_MAX_PART_BYTES,begin_->totalBytes-p.offset));if(p.offset<bytes_.size()){rsRequire(p.offset+p.bytes.size()<=bytes_.size()&&std::equal(p.bytes.begin(),p.bytes.end(),bytes_.begin()+p.offset));return true;}rsRequire(p.offset==bytes_.size()&&parts_<begin_->partCount);bytes_.insert(bytes_.end(),p.bytes.begin(),p.bytes.end());++parts_;return true;}catch(const std::exception&){Reset();return false;}}
std::optional<ResyncSnapshot> ResyncAssembler::End(const ResyncEnd& e){try{rsRequire(begin_&&e.key==begin_->key&&e.phase==begin_->phase&&e.snapshotCut==begin_->snapshotCut&&e.totalBytes==begin_->totalBytes&&e.sha256==begin_->sha256&&parts_==begin_->partCount&&bytes_.size()==e.totalBytes&&desyncSha256(bytes_)==e.sha256);auto s=decodeResyncSnapshot(bytes_);rsRequire(sameResyncRoom(s.room,begin_->room));Reset();return s;}catch(const std::exception&){Reset();return {};}}

std::array<std::uint8_t, 32> desyncSha256(std::span<const std::uint8_t> input) {
    constexpr std::uint32_t k[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    std::uint32_t h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    const auto blocks = input.size()/64 + ((input.size()%64 < 56) ? 1u : 2u);
    const auto bits = static_cast<std::uint64_t>(input.size()) * 8;
    for (std::size_t block=0; block<blocks; ++block) {
        std::uint32_t w[64]{};
        for (std::size_t j=0;j<64;++j) {
            const auto p=block*64+j;
            std::uint8_t b=0;
            if (p<input.size()) b=input[p];
            else if (p==input.size()) b=0x80;
            else if (p>=blocks*64-8) b=static_cast<std::uint8_t>(bits >> ((blocks*64-1-p)*8));
            w[j/4] |= static_cast<std::uint32_t>(b) << ((3-j%4)*8);
        }
        for (std::size_t j=16;j<64;++j) {
            const auto a=w[j-15],b=w[j-2];
            w[j]=w[j-16]+(std::rotr(a,7)^std::rotr(a,18)^(a>>3))+w[j-7]+(std::rotr(b,17)^std::rotr(b,19)^(b>>10));
        }
        auto a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],v=h[7];
        for (std::size_t j=0;j<64;++j) {
            const auto t=v+(std::rotr(e,6)^std::rotr(e,11)^std::rotr(e,25))+((e&f)^(~e&g))+k[j]+w[j];
            const auto u=(std::rotr(a,2)^std::rotr(a,13)^std::rotr(a,22))+((a&b)^(a&c)^(b&c));
            v=g;g=f;f=e;e=d+t;d=c;c=b;b=a;a=t+u;
        }
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=v;
    }
    std::array<std::uint8_t,32> out{};
    for(std::size_t i=0;i<32;++i) out[i]=static_cast<std::uint8_t>(h[i/4]>>((3-i%4)*8));
    return out;
}
std::string desyncDigestHex(const std::array<std::uint8_t,32>& digest) {
    constexpr char digits[]="0123456789abcdef";
    std::string out;out.reserve(64);
    for(auto b:digest){out.push_back(digits[b>>4]);out.push_back(digits[b&15]);}
    return out;
}
bool desyncPngStructure(std::span<const std::uint8_t> bytes) noexcept {
    constexpr std::uint8_t signature[]={137,80,78,71,13,10,26,10};
    if(bytes.size()<45 || bytes.size()>DESYNC_PNG_BYTES ||
       !std::equal(std::begin(signature),std::end(signature),bytes.begin()))return false;
    static const auto table=[] {
        std::array<std::uint32_t,256> values{};
        for(std::uint32_t i=0;i<256;++i){auto c=i;for(unsigned b=0;b<8;++b)c=(c>>1)^((c&1)?0xedb88320u:0u);values[i]=c;}
        return values;
    }();
    const auto be=[&](std::size_t p){return (static_cast<std::uint32_t>(bytes[p])<<24)|
        (static_cast<std::uint32_t>(bytes[p+1])<<16)|(static_cast<std::uint32_t>(bytes[p+2])<<8)|bytes[p+3];};
    bool header=false,idat=false,idatEnded=false,palette=false;std::uint8_t color=0;
    std::size_t p=8;
    while(p<bytes.size()) {
        if(bytes.size()-p<12)return false;
        const auto length=be(p),type=be(p+4);
        if(length>bytes.size()-p-12)return false;
        for(std::size_t i=p+4;i<p+8;++i)if(!((bytes[i]>='A'&&bytes[i]<='Z')||(bytes[i]>='a'&&bytes[i]<='z')))return false;
        if(bytes[p+6]<'A'||bytes[p+6]>'Z')return false;
        std::uint32_t crc=0xffffffffu;
        for(std::size_t i=p+4;i<p+8+length;++i)crc=(crc>>8)^table[(crc^bytes[i])&255u];
        if((crc^0xffffffffu)!=be(p+8+length))return false;
        if(!header && type!=0x49484452u)return false;
        if(type==0x49484452u) {
            if(header || length!=13)return false;
            const auto width=be(p+8),height=be(p+12);const auto depth=bytes[p+16];color=bytes[p+17];
            const bool legal=(color==0 && (depth==1||depth==2||depth==4||depth==8||depth==16)) ||
                ((color==2||color==4||color==6) && (depth==8||depth==16)) ||
                (color==3 && (depth==1||depth==2||depth==4||depth==8));
            if(!width||!height||width>16384||height>16384||!legal||bytes[p+18]||bytes[p+19]||bytes[p+20]>1)return false;
            header=true;
        } else if(type==0x504c5445u) {
            if(palette||idat||!length||length>768||length%3||color==0||color==4)return false;
            palette=true;
        } else if(type==0x49444154u) {
            if(idatEnded || (color==3&&!palette))return false;
            idat=idat||length!=0;
        } else if(type==0x49454e44u) {
            return length==0 && idat && p+12==bytes.size();
        } else {
            if(bytes[p+4]>='A'&&bytes[p+4]<='Z')return false; // unknown critical chunk
            if(idat)idatEnded=true;
        }
        p+=12+length;
    }
    return false;
}

namespace {
void diagnosticRequire(bool condition) { if (!condition) throw std::runtime_error("invalid desync diagnostic payload"); }
void validateKey(const DesyncKey& k) {
    diagnosticRequire(k.reportId && k.sessionId.size()==32 &&
        std::all_of(k.sessionId.begin(),k.sessionId.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');}));
}
void writeKey(ByteWriter& w,const DesyncKey& k) { validateKey(k);w.writeString(k.sessionId);w.writeU64(k.reportId); }
void readKey(ByteReader& r,DesyncKey& k) { k.sessionId=r.readString();k.reportId=r.readU64();validateKey(k); }
void validateRequest(const DesyncCaptureRequest& m) {
    validateKey(m.key);
    constexpr auto allowedFields = DesyncRoom | DesyncEnemies | DesyncProgress | DesyncMissingEnemies;
    diagnosticRequire(m.connections[0] && m.divergedSlot>0 && m.divergedSlot<3 && m.connections[m.divergedSlot] &&
        m.fields && !(m.fields&~allowedFields) && m.hostHash.epoch==m.epoch && m.clientHash.epoch==m.epoch &&
        m.triggerMs<=UINT64_MAX-DESYNC_DEADLINE_MS && m.deadlineMs==m.triggerMs+DESYNC_DEADLINE_MS &&
        m.remainingMs>0 && m.remainingMs<=DESYNC_DEADLINE_MS);
    for(std::size_t i=0;i<3;++i) for(std::size_t j=0;j<i;++j)
        diagnosticRequire(!m.connections[i] || m.connections[i]!=m.connections[j]);
}
void validateDescriptor(const DesyncArtifactDescriptor& a,std::size_t index) {
    diagnosticRequire(static_cast<std::size_t>(a.kind)==index && static_cast<unsigned>(a.status)<=5 &&
        a.bytes<=(index==3?DESYNC_PNG_BYTES:DESYNC_LOG_BYTES) && a.sourceLabel.size()<=128 && a.error.size()<=256 &&
        a.rangeBegin<=a.rangeEnd && a.rangeEnd<=a.sourceBytes && a.rangeEnd-a.rangeBegin==a.bytes &&
        a.startedMs<=a.finishedMs);
}
void validateDone(const DesyncCaptureDone& m) {
    validateKey(m.key);diagnosticRequire(m.connectionId!=0);
    std::uint64_t logs=0;
    for(std::size_t i=0;i<4;++i) {validateDescriptor(m.artifacts[i],i);if(i<3)logs+=m.artifacts[i].bytes;}
    diagnosticRequire(logs<=DESYNC_LOG_BYTES);
}
}
void write(ByteWriter& w,const DesyncCaptureRequest& m) {
    validateRequest(m);writeKey(w,m.key);for(auto c:m.connections)w.writeU64(c);
    w.writeU32(m.epoch);w.writeU8(m.divergedSlot);w.writeU8(m.fields);write(w,m.hostHash);write(w,m.clientHash);
    for(auto n:{m.hostReceiptSeq,m.clientReceiptSeq,m.hostReceiptMs,m.clientReceiptMs,m.comparisonSeq,m.triggerMs,m.deadlineMs})w.writeU64(n);
    w.writeU32(m.remainingMs);
}
void read(ByteReader& r,DesyncCaptureRequest& m) {
    readKey(r,m.key);for(auto& c:m.connections)c=r.readU64();m.epoch=r.readU32();m.divergedSlot=r.readU8();m.fields=r.readU8();
    read(r,m.hostHash);read(r,m.clientHash);m.hostReceiptSeq=r.readU64();m.clientReceiptSeq=r.readU64();
    m.hostReceiptMs=r.readU64();m.clientReceiptMs=r.readU64();m.comparisonSeq=r.readU64();m.triggerMs=r.readU64();m.deadlineMs=r.readU64();
    m.remainingMs=r.readU32();
    diagnosticRequire(r.atEnd());validateRequest(m);
}
void write(ByteWriter& w,const DesyncArtifactChunk& m) {
    writeKey(w,m.key);diagnosticRequire(m.connectionId && static_cast<unsigned>(m.kind)<4 && !m.bytes.empty() && m.bytes.size()<=DESYNC_CHUNK_BYTES);
    const auto cap=m.kind==DesyncArtifactKind::ScreenshotPng?DESYNC_PNG_BYTES:DESYNC_LOG_BYTES;
    diagnosticRequire(m.offset<=cap && m.bytes.size()<=cap-m.offset);
    w.writeU64(m.connectionId);w.writeU8(static_cast<std::uint8_t>(m.kind));w.writeU32(m.offset);w.writeU16(static_cast<std::uint16_t>(m.bytes.size()));
    for(auto b:m.bytes)w.writeU8(b);
}
void read(ByteReader& r,DesyncArtifactChunk& m) {
    readKey(r,m.key);m.connectionId=r.readU64();m.kind=static_cast<DesyncArtifactKind>(r.readU8());m.offset=r.readU32();const auto n=r.readU16();
    diagnosticRequire(n && n<=DESYNC_CHUNK_BYTES && r.remaining()==n);m.bytes.resize(n);for(auto& b:m.bytes)b=r.readU8();
    ByteWriter checked;write(checked,m);
}
void write(ByteWriter& w,const DesyncCaptureDone& m) {
    validateDone(m);writeKey(w,m.key);w.writeU64(m.connectionId);
    for(const auto& a:m.artifacts){w.writeU8(static_cast<std::uint8_t>(a.kind));w.writeU8(static_cast<std::uint8_t>(a.status));w.writeU32(a.bytes);
        for(auto b:a.sha256)w.writeU8(b);
        for(auto n:{a.sourceBytes,a.rangeBegin,a.rangeEnd,a.startedMs,a.finishedMs})w.writeU64(n);
        w.writeBool(a.truncated);w.writeU32(a.errorCode);w.writeString(a.sourceLabel);w.writeString(a.error);}
}
void read(ByteReader& r,DesyncCaptureDone& m) {
    readKey(r,m.key);m.connectionId=r.readU64();
    for(auto& a:m.artifacts){a.kind=static_cast<DesyncArtifactKind>(r.readU8());a.status=static_cast<DesyncArtifactStatus>(r.readU8());a.bytes=r.readU32();
        for(auto& b:a.sha256)b=r.readU8();
        a.sourceBytes=r.readU64();a.rangeBegin=r.readU64();a.rangeEnd=r.readU64();a.startedMs=r.readU64();a.finishedMs=r.readU64();
        const auto flag=r.readU8();diagnosticRequire(flag<=1);a.truncated=flag!=0;a.errorCode=r.readU32();a.sourceLabel=r.readString();a.error=r.readString();}
    diagnosticRequire(r.atEnd());validateDone(m);
}
std::vector<std::uint8_t> encode(const DesyncCaptureRequest& m){ByteWriter w;write(w,m);return encodePacket(PacketType::DesyncCaptureRequest,w.data());}
std::vector<std::uint8_t> encode(const DesyncArtifactChunk& m){ByteWriter w;write(w,m);return encodePacket(PacketType::DesyncArtifactChunk,w.data());}
std::vector<std::uint8_t> encode(const DesyncCaptureDone& m){ByteWriter w;write(w,m);return encodePacket(PacketType::DesyncCaptureDone,w.data());}

namespace {
constexpr std::size_t kActivationRequestBytes = 41;
constexpr std::size_t kHostActivationPointBytes = 65;

void validateActivationRequest(const ActivationRequest& m) {
    if (m.location.epoch == 0 || m.requestSeq == 0 ||
        (m.incarnation[0] == 0 && m.incarnation[1] == 0))
        throw std::runtime_error("ActivationRequest: unset identity");
}

void readActivationRequestFields(ByteReader& r, ActivationRequest& m) {
    read(r, m.location);
    m.incarnation[0] = r.readU64();
    m.incarnation[1] = r.readU64();
    m.requestSeq = r.readU64();
    m.requesterSlot = r.readU8();
    validateActivationRequest(m);
}

void validateHostActivationPoint(const HostActivationPoint& m) {
    validateActivationRequest(m.request);
    if (m.sourceSeq == 0 || (m.request.requesterSlot != 1 && m.request.requesterSlot != 2))
        throw std::runtime_error("HostActivationPoint: unset source or invalid requester");
    for (float value : m.position) {
        if (!std::isfinite(value)) throw std::runtime_error("HostActivationPoint: nonfinite position");
    }
}
} // namespace

// ===== Binary write helpers =================================================

void write(ByteWriter& w, const Vec3& v) {
    w.writeF32(v.x);
    w.writeF32(v.y);
    w.writeF32(v.z);
}

void write(ByteWriter& w, const InputButtons& b) {
    std::uint16_t bits = 0;
    if (b.attack) bits |= (1 << 0);
    if (b.jump) bits |= (1 << 1);
    if (b.guard) bits |= (1 << 2);
    if (b.dodge) bits |= (1 << 3);
    if (b.lockOn) bits |= (1 << 4);
    if (b.magic1) bits |= (1 << 5);
    if (b.magic2) bits |= (1 << 6);
    if (b.special1) bits |= (1 << 7);
    if (b.special2) bits |= (1 << 8);
    w.writeU16(bits);
}

void write(ByteWriter& w, const InputFrame& f) {
    w.writeU32(f.seq);
    w.writeU64(f.clientTimeMs);
    w.writeU32(f.ownedActorId);
    w.writeF32(f.leftStickX);
    w.writeF32(f.leftStickY);
    w.writeF32(f.rightStickX);
    w.writeF32(f.rightStickY);
    w.writeU32(f.requestedTargetId);
    write(w, f.buttons);
}

void write(ByteWriter& w, const ActorState& a) {
    w.writeU32(a.actorId);
    w.writeU8(static_cast<std::uint8_t>(a.slot));
    write(w, a.position);
    w.writeF32(a.rotationY);
    write(w, a.velocity);
    w.writeU32(a.motionId);
    w.writeU16(static_cast<std::uint16_t>(a.action));
    w.writeU32(a.comboStep);
    w.writeI32(a.hp);
    w.writeI32(a.mp);
    w.writeI32(a.drive);
    w.writeU32(a.targetId);
    // Pack 4 bools into one byte.
    std::uint8_t flags = 0;
    if (a.airborne) flags |= (1 << 0);
    if (a.invuln) flags |= (1 << 1);
    if (a.staggered) flags |= (1 << 2);
    if (a.downed) flags |= (1 << 3);
    w.writeU8(flags);
}

void write(ByteWriter& w, const EnemyState& e) {
    w.writeU32(e.netId);
    w.writeU32(e.objectId);
    w.writeU32(e.spawnGroupId);
    write(w, e.position);
    w.writeF32(e.rotationY);
    w.writeU32(e.motionId);
    w.writeI32(e.hp);
    w.writeU32(e.targetActorId);
    w.writeBool(e.alive);
}

void write(ByteWriter& w, const RoomState& r) {
    w.writeU32(r.worldId);
    w.writeU32(r.roomId);
    w.writeU32(r.mapProgram);
    w.writeU32(r.battleProgram);
    w.writeU32(r.eventProgram);
    w.writeBool(r.inTransition);
    w.writeBool(r.inCutscene);
}

void write(ByteWriter& w, const SessionActor& sa) {
    w.writeU32(sa.actorId);
    w.writeU8(static_cast<std::uint8_t>(sa.slot));
    w.writeString(sa.ownerPeerId);
    w.writeString(sa.archetype);
    w.writeU64(sa.connectionId);
}

void write(ByteWriter& w, const SessionState& ss) {
    w.writeString(ss.sessionId);
    w.writeString(ss.gameBuild);
    w.writeString(ss.modHash);
    write(w, ss.room);
    auto count = static_cast<std::uint16_t>(ss.actors.size());
    w.writeU16(count);
    for (const auto& a : ss.actors) {
        write(w, a);
    }
}

void write(ByteWriter& w, const ActorSnapshot& as) {
    w.writeU32(as.snapshotId);
    write(w, as.actor);
}

void write(ByteWriter& w, const EnemySnapshot& es) {
    w.writeU32(es.snapshotId);
    write(w, es.enemy);
}

void write(ByteWriter& w, const EventMessage& em) {
    w.writeU32(em.snapshotId);
    w.writeU16(static_cast<std::uint16_t>(em.type));
    w.writeString(em.payloadJson);
}

void write(ByteWriter& w, const HelloReject& hr) {
    w.writeU8(hr.code);
    w.writeString(hr.reason);
}

void write(ByteWriter& w, const ClientHello& ch) {
    w.writeU16(ch.protocolVersion);
    w.writeString(ch.gameBuild);
    w.writeString(ch.contentHash);
    w.writeString(ch.modHash);
    w.writeString(ch.peerId);
    w.writeString(ch.peerName);
    w.writeU8(static_cast<std::uint8_t>(ch.requestedMode));
    w.writeU8(ch.requestedSlot);
}

void write(ByteWriter& w, const AvatarState& a) {
    w.writeU32(a.seq);
    w.writeU64(a.serverTimeMs);
    w.writeU8(static_cast<std::uint8_t>(a.ownerSlot));
    w.writeU8(a.character);
    w.writeU8(a.colorVariant);
    w.writeU16(a.worldId);
    w.writeU16(a.roomId);
    write(w, a.position);
    w.writeF32(a.rotationY);
    write(w, a.velocity);
    w.writeU32(a.motionId);
    w.writeF32(a.motionTime);
    w.writeF32(a.motionSpeed);
    w.writeU8(a.flags);
    w.writeI32(a.hp);
    w.writeI32(a.maxHp);
    w.writeI32(a.mp);
    w.writeI32(a.maxMp);
    w.writeU32(a.downedEpoch);
    w.writeU64(a.downedEpisode);
    w.writeU64(a.downedDelivery);
}

void write(ByteWriter& w, const ClockPing& p) { w.writeU64(p.clientSendMs); }

void write(ByteWriter& w, const ClockPong& p) {
    w.writeU64(p.clientSendMs);
    w.writeU64(p.serverMs);
}

void write(ByteWriter& w, const RoomTransition& m) {
    w.writeU32(m.epoch);
    w.writeU16(m.worldId);
    w.writeU16(m.roomId);
    w.writeU16(m.door);
    w.writeU16(m.mapProgram);
    w.writeU16(m.battleProgram);
    w.writeU16(m.eventProgram);
}

void write(ByteWriter& w, const TransitionAck& m) {
    w.writeU32(m.epoch);
    w.writeU16(m.worldId);
    w.writeU16(m.roomId);
    w.writeBool(m.arrived);
}

void write(ByteWriter& w, const ActivationRequest& m) {
    validateActivationRequest(m);
    write(w, m.location);
    w.writeU64(m.incarnation[0]);
    w.writeU64(m.incarnation[1]);
    w.writeU64(m.requestSeq);
    w.writeU8(m.requesterSlot);
}

void write(ByteWriter& w, const HostActivationPoint& m) {
    validateHostActivationPoint(m);
    write(w, m.request);
    w.writeU64(m.sourceSeq);
    for (float value : m.position) w.writeF32(value);
}

void write(ByteWriter& w, const EventHold& m) {
    w.writeU32(m.epoch);
    w.writeBool(m.active);
    w.writeU16(m.eventProgram);
}

void write(ByteWriter& w, const EnemyManifest& m) {
    if (m.entries.size() > 0xFFFF) throw std::runtime_error("EnemyManifest too large");
    w.writeU32(m.epoch);
    w.writeBool(m.replace);
    w.writeU16(static_cast<std::uint16_t>(m.entries.size()));
    for (const auto& e : m.entries) {
        w.writeU16(e.netId);
        w.writeU16(e.battleProgram);
        w.writeU16(e.spawnIndex);
        w.writeU32(e.objectId);
        write(w, e.spawnPosition);
    }
}

void write(ByteWriter& w, const EnemyHp& m) {
    if (m.entries.size() > 0xFFFF) throw std::runtime_error("EnemyHp too large");
    if (!m.sequence) throw std::runtime_error("EnemyHp: zero sequence");
    w.writeU32(m.epoch);
    w.writeU64(m.sequence);
    w.writeU16(static_cast<std::uint16_t>(m.entries.size()));
    for (const auto& e : m.entries) {
        w.writeU16(e.netId);
        w.writeI32(e.hp);
        w.writeI32(e.maxHp);
    }
}

namespace {
void checkEnemyMotion(const EnemyMotion& m) {
    if (!m.epoch) throw std::runtime_error("EnemyMotion: zero epoch");
    if (!m.sequence) throw std::runtime_error("EnemyMotion: zero sequence");
    if (m.entries.size() > ENEMY_MOTION_MAX_ENTRIES) throw std::runtime_error("EnemyMotion too large");
    for (std::size_t i = 0; i < m.entries.size(); ++i) {
        const auto& e = m.entries[i];
        if (!e.netId || !e.objectId) throw std::runtime_error("EnemyMotion: zero netId/objectId");
        if (e.flags != ENEMY_MOTION_ALIVE) throw std::runtime_error("EnemyMotion: bad flags");
        if (!std::isfinite(e.motionTime) || !std::isfinite(e.rotationY) || !std::isfinite(e.position.x) ||
            !std::isfinite(e.position.y) || !std::isfinite(e.position.z))
            throw std::runtime_error("EnemyMotion: non-finite value");
        if (e.motionId >= ENEMY_MOTION_MAX_MOTION_ID) throw std::runtime_error("EnemyMotion: motion id out of range");
        if (e.motionTime < 0.0f || e.motionTime > ENEMY_MOTION_MAX_TIME)
            throw std::runtime_error("EnemyMotion: motion time out of range");
        if (std::fabs(e.rotationY) > ENEMY_MOTION_MAX_ROTATION) throw std::runtime_error("EnemyMotion: rotation out of range");
        if (std::fabs(e.position.x) > ENEMY_MOTION_MAX_COORD || std::fabs(e.position.y) > ENEMY_MOTION_MAX_COORD ||
            std::fabs(e.position.z) > ENEMY_MOTION_MAX_COORD)
            throw std::runtime_error("EnemyMotion: position out of range");
        for (std::size_t j = 0; j < i; ++j)
            if (m.entries[j].netId == e.netId) throw std::runtime_error("EnemyMotion: duplicate netId");
    }
}
} // namespace

void write(ByteWriter& w, const EnemyMotion& m) {
    checkEnemyMotion(m);
    w.writeU32(m.epoch);
    w.writeU64(m.sequence);
    w.writeU32(m.hostFrame);
    w.writeU16(static_cast<std::uint16_t>(m.entries.size()));
    for (const auto& e : m.entries) {
        w.writeU16(e.netId);
        w.writeU32(e.objectId);
        w.writeU32(e.motionId);
        w.writeF32(e.motionTime);
        write(w, e.position);
        w.writeF32(e.rotationY);
        w.writeU8(e.flags);
    }
}

void write(ByteWriter& w, const EnemyDeath& m) {
    w.writeU32(m.epoch);
    w.writeU16(m.netId);
}

void write(ByteWriter& w, const PartyLayout& m) {
    if(!validPartyLayout(m,m.connections))throw std::runtime_error("PartyLayout: invalid policy/identity");
    write(w,m.location);w.writeU64(m.version);
    for(auto c:m.connections)w.writeU64(c);
    w.writeU8(static_cast<std::uint8_t>(m.rule));w.writeU8(static_cast<std::uint8_t>(m.reason));
    for(const auto& s:m.seats){w.writeU8(static_cast<std::uint8_t>(s.kind));w.writeU8(s.playerSlot);w.writeU32(s.objectId);}
}
void read(ByteReader& r, PartyLayout& out) {
    if(r.remaining()!=68)throw std::runtime_error("PartyLayout: wrong payload size");
    PartyLayout m;read(r,m.location);m.version=r.readU64();
    for(auto& c:m.connections)c=r.readU64();
    m.rule=static_cast<PartyRule>(r.readU8());m.reason=static_cast<PartyApplyReason>(r.readU8());
    for(auto& s:m.seats){s.kind=static_cast<PartyMemberKind>(r.readU8());s.playerSlot=r.readU8();s.objectId=r.readU32();}
    if(!validPartyLayout(m,m.connections))throw std::runtime_error("PartyLayout: invalid policy/identity");
    out=m;
}
void write(ByteWriter& w, const PartyReapply& m) {
    if(!m.location.epoch || static_cast<unsigned>(m.reason)>3)throw std::runtime_error("PartyReapply: invalid context");
    write(w,m.location);w.writeU64(m.afterVersion);w.writeU8(static_cast<std::uint8_t>(m.reason));
}
void read(ByteReader& r, PartyReapply& out) {
    if(r.remaining()!=25)throw std::runtime_error("PartyReapply: wrong payload size");
    PartyReapply m;read(r,m.location);m.afterVersion=r.readU64();m.reason=static_cast<PartyApplyReason>(r.readU8());
    ByteWriter check;write(check,m);out=m;
}
std::vector<std::uint8_t> encode(const PartyLayout& m){ByteWriter w;write(w,m);return encodePacket(PacketType::PartyLayout,w.data());}
std::vector<std::uint8_t> encode(const PartyReapply& m){ByteWriter w;write(w,m);return encodePacket(PacketType::PartyReapply,w.data());}
void write(ByteWriter& w, const PartyIntent& m) {
    if(!validPartyIntent(m,m.connections))throw std::runtime_error("PartyIntent: invalid policy/identity");
    w.writeU64(m.version);
    for(auto c:m.connections)w.writeU64(c);
    w.writeU16(m.target.worldId);w.writeU16(m.target.roomId);w.writeU16(m.target.eventProgram);
    w.writeU8(static_cast<std::uint8_t>(m.rule));
    for(const auto& s:m.seats){w.writeU8(static_cast<std::uint8_t>(s.kind));w.writeU8(s.playerSlot);w.writeU32(s.objectId);}
    for(auto k:m.kits)w.writeU16(k);
}
void read(ByteReader& r, PartyIntent& out) {
    if(r.remaining()!=PARTY_INTENT_PAYLOAD)throw std::runtime_error("PartyIntent: wrong payload size");
    PartyIntent m;m.version=r.readU64();
    for(auto& c:m.connections)c=r.readU64();
    m.target.worldId=r.readU16();m.target.roomId=r.readU16();m.target.eventProgram=r.readU16();
    m.rule=static_cast<PartyRule>(r.readU8());
    for(auto& s:m.seats){s.kind=static_cast<PartyMemberKind>(r.readU8());s.playerSlot=r.readU8();s.objectId=r.readU32();}
    for(auto& k:m.kits)k=r.readU16();
    if(!validPartyIntent(m,m.connections))throw std::runtime_error("PartyIntent: invalid policy/identity");
    out=m;
}
std::vector<std::uint8_t> encode(const PartyIntent& m){ByteWriter w;write(w,m);return encodePacket(PacketType::PartyIntent,w.data());}

void write(ByteWriter& w, const ReviveRequest& m) {
    write(w, m.location);
    w.writeU64(m.seq); w.writeU64(m.requesterConnectionId);
    w.writeU64(m.targetConnectionId); w.writeU64(m.targetEpisode);
    w.writeU8(m.requesterSlot); w.writeU8(m.targetSlot);
}
void read(ByteReader& r, ReviveRequest& m) {
    if (r.remaining() != 50) throw std::runtime_error("ReviveRequest: wrong payload length");
    read(r, m.location);
    m.seq = r.readU64(); m.requesterConnectionId = r.readU64();
    m.targetConnectionId = r.readU64(); m.targetEpisode = r.readU64();
    m.requesterSlot = r.readU8(); m.targetSlot = r.readU8();
}
std::vector<std::uint8_t> encode(const ReviveRequest& m) {
    ByteWriter w; write(w, m); return encodePacket(PacketType::ReviveRequest, w.data());
}

void write(ByteWriter& w, const HitClaim& m) {
    w.writeU32(m.epoch);
    w.writeU32(m.seq);
    w.writeU16(m.netId);
    w.writeU32(m.objectId);
    w.writeU64(m.requesterConnectionId);
    w.writeU32(m.attackId);
    w.writeI32(m.damage);
    write(w, m.attackerPosition);
    w.writeU8(static_cast<std::uint8_t>(m.attackerSlot));
}

void write(ByteWriter& w, const ProgressUpdate& m) {
    if (m.spans.size() > 0xFFFF) throw std::runtime_error("ProgressUpdate: too many spans");
    w.writeU32(m.version);
    w.writeBool(m.full);
    w.writeU16(static_cast<std::uint16_t>(m.spans.size()));
    for (const auto& span : m.spans) {
        if (span.bytes.size() > 0xFFFF) throw std::runtime_error("ProgressUpdate: span too long");
        w.writeU32(span.offset);
        w.writeU16(static_cast<std::uint16_t>(span.bytes.size()));
        for (auto b : span.bytes) w.writeU8(b);
    }
}

void write(ByteWriter& w, const StateHash& m) {
    w.writeU32(m.epoch);
    w.writeU16(m.worldId);
    w.writeU16(m.roomId);
    w.writeU32(m.enemiesHash);
    w.writeU32(m.progressHash);
    w.writeBool(m.nativeCensusComplete);
    w.writeU32(m.nativeLivingCount);
    w.writeU32(m.nativeCombatCount);
}

void write(ByteWriter& w, const DesyncNotice& m) {
    w.writeU8(static_cast<std::uint8_t>(m.slot));
    w.writeU32(m.epoch);
    w.writeU8(m.fields);
}



// ===== Binary read helpers ==================================================

void read(ByteReader& r, Vec3& v) {
    v.x = r.readF32();
    v.y = r.readF32();
    v.z = r.readF32();
}

void read(ByteReader& r, InputButtons& b) {
    auto bits = r.readU16();
    b.attack = (bits & (1 << 0)) != 0;
    b.jump = (bits & (1 << 1)) != 0;
    b.guard = (bits & (1 << 2)) != 0;
    b.dodge = (bits & (1 << 3)) != 0;
    b.lockOn = (bits & (1 << 4)) != 0;
    b.magic1 = (bits & (1 << 5)) != 0;
    b.magic2 = (bits & (1 << 6)) != 0;
    b.special1 = (bits & (1 << 7)) != 0;
    b.special2 = (bits & (1 << 8)) != 0;
}

void read(ByteReader& r, InputFrame& f) {
    f.seq = r.readU32();
    f.clientTimeMs = r.readU64();
    f.ownedActorId = r.readU32();
    f.leftStickX = r.readF32();
    f.leftStickY = r.readF32();
    f.rightStickX = r.readF32();
    f.rightStickY = r.readF32();
    f.requestedTargetId = r.readU32();
    read(r, f.buttons);
}

void read(ByteReader& r, ActorState& a) {
    a.actorId = r.readU32();
    a.slot = static_cast<SlotType>(r.readU8());
    read(r, a.position);
    a.rotationY = r.readF32();
    read(r, a.velocity);
    a.motionId = r.readU32();
    a.action = static_cast<ActionState>(r.readU16());
    a.comboStep = r.readU32();
    a.hp = r.readI32();
    a.mp = r.readI32();
    a.drive = r.readI32();
    a.targetId = r.readU32();
    auto flags = r.readU8();
    a.airborne = (flags & (1 << 0)) != 0;
    a.invuln = (flags & (1 << 1)) != 0;
    a.staggered = (flags & (1 << 2)) != 0;
    a.downed = (flags & (1 << 3)) != 0;
}

void read(ByteReader& r, EnemyState& e) {
    e.netId = r.readU32();
    e.objectId = r.readU32();
    e.spawnGroupId = r.readU32();
    read(r, e.position);
    e.rotationY = r.readF32();
    e.motionId = r.readU32();
    e.hp = r.readI32();
    e.targetActorId = r.readU32();
    e.alive = r.readBool();
}

void read(ByteReader& r, RoomState& rs) {
    rs.worldId = r.readU32();
    rs.roomId = r.readU32();
    rs.mapProgram = r.readU32();
    rs.battleProgram = r.readU32();
    rs.eventProgram = r.readU32();
    rs.inTransition = r.readBool();
    rs.inCutscene = r.readBool();
}

void read(ByteReader& r, SessionActor& sa) {
    sa.actorId = r.readU32();
    sa.slot = static_cast<SlotType>(r.readU8());
    sa.ownerPeerId = r.readString();
    sa.archetype = r.readString();
    sa.connectionId = r.readU64();
}

void read(ByteReader& r, SessionState& ss) {
    ss.sessionId = r.readString();
    ss.gameBuild = r.readString();
    ss.modHash = r.readString();
    read(r, ss.room);
    auto count = r.readU16();
    ss.actors.resize(count);
    for (auto& a : ss.actors) {
        read(r, a);
    }
}

void read(ByteReader& r, ActorSnapshot& as) {
    as.snapshotId = r.readU32();
    read(r, as.actor);
}

void read(ByteReader& r, EnemySnapshot& es) {
    es.snapshotId = r.readU32();
    read(r, es.enemy);
}

void read(ByteReader& r, EventMessage& em) {
    em.snapshotId = r.readU32();
    em.type = static_cast<EventType>(r.readU16());
    em.payloadJson = r.readString();
}

void read(ByteReader& r, HelloReject& hr) {
    hr.code = r.readU8();
    hr.reason = r.readString();
}

void read(ByteReader& r, ClientHello& ch) {
    ch.protocolVersion = r.readU16();
    ch.gameBuild = r.readString();
    ch.contentHash = r.readString();
    ch.modHash = r.readString();
    ch.peerId = r.readString();
    ch.peerName = r.readString();
    ch.requestedMode = static_cast<RuntimeMode>(r.readU8());
    ch.requestedSlot = r.readU8();
}

void read(ByteReader& r, AvatarState& a) {
    a.seq = r.readU32();
    a.serverTimeMs = r.readU64();
    a.ownerSlot = static_cast<SlotType>(r.readU8());
    a.character = r.readU8();
    a.colorVariant = r.readU8();
    a.worldId = r.readU16();
    a.roomId = r.readU16();
    read(r, a.position);
    a.rotationY = r.readF32();
    read(r, a.velocity);
    a.motionId = r.readU32();
    a.motionTime = r.readF32();
    a.motionSpeed = r.readF32();
    a.flags = r.readU8();
    a.hp = r.readI32();
    a.maxHp = r.readI32();
    a.mp = r.readI32();
    a.maxMp = r.readI32();
    a.downedEpoch = r.readU32();
    a.downedEpisode = r.readU64();
    a.downedDelivery = r.readU64();
}

void read(ByteReader& r, ClockPing& p) { p.clientSendMs = r.readU64(); }

void read(ByteReader& r, ClockPong& p) {
    p.clientSendMs = r.readU64();
    p.serverMs = r.readU64();
}

void read(ByteReader& r, RoomTransition& m) {
    m.epoch = r.readU32();
    m.worldId = r.readU16();
    m.roomId = r.readU16();
    m.door = r.readU16();
    m.mapProgram = r.readU16();
    m.battleProgram = r.readU16();
    m.eventProgram = r.readU16();
}

void read(ByteReader& r, TransitionAck& m) {
    m.epoch = r.readU32();
    m.worldId = r.readU16();
    m.roomId = r.readU16();
    m.arrived = r.readBool();
}

void read(ByteReader& r, ActivationRequest& m) {
    if (r.remaining() != kActivationRequestBytes)
        throw std::runtime_error("ActivationRequest: wrong payload length");
    readActivationRequestFields(r, m);
}

void read(ByteReader& r, HostActivationPoint& m) {
    if (r.remaining() != kHostActivationPointBytes)
        throw std::runtime_error("HostActivationPoint: wrong payload length");
    readActivationRequestFields(r, m.request);
    m.sourceSeq = r.readU64();
    for (float& value : m.position) value = r.readF32();
    validateHostActivationPoint(m);
}

void read(ByteReader& r, EventHold& m) {
    m.epoch = r.readU32();
    m.active = r.readBool();
    m.eventProgram = r.readU16();
}

void read(ByteReader& r, EnemyManifest& m) {
    m.epoch = r.readU32();
    m.replace = r.readBool();
    const auto n = r.readU16();
    m.entries.resize(n);
    for (auto& e : m.entries) {
        e.netId = r.readU16();
        e.battleProgram = r.readU16();
        e.spawnIndex = r.readU16();
        e.objectId = r.readU32();
        read(r, e.spawnPosition);
    }
}

void read(ByteReader& r, EnemyMotion& m) {
    EnemyMotion decoded;
    decoded.epoch = r.readU32();
    decoded.sequence = r.readU64();
    decoded.hostFrame = r.readU32();
    const auto n = r.readU16();
    if (n > ENEMY_MOTION_MAX_ENTRIES || r.remaining() != static_cast<std::size_t>(n) * ENEMY_MOTION_ENTRY_BYTES)
        throw std::runtime_error("EnemyMotion: wrong payload length");
    decoded.entries.resize(n);
    for (auto& e : decoded.entries) {
        e.netId = r.readU16();
        e.objectId = r.readU32();
        e.motionId = r.readU32();
        e.motionTime = r.readF32();
        read(r, e.position);
        e.rotationY = r.readF32();
        e.flags = r.readU8();
    }
    checkEnemyMotion(decoded);
    m = std::move(decoded);
}

void read(ByteReader& r, EnemyHp& m) {
    EnemyHp decoded;
    decoded.epoch = r.readU32();
    decoded.sequence = r.readU64();
    if (!decoded.sequence) throw std::runtime_error("EnemyHp: zero sequence");
    const auto n = r.readU16();
    if (r.remaining() != static_cast<std::size_t>(n) * 10)
        throw std::runtime_error("EnemyHp: wrong payload length");
    decoded.entries.resize(n);
    for (auto& e : decoded.entries) {
        e.netId = r.readU16();
        e.hp = r.readI32();
        e.maxHp = r.readI32();
    }
    m = std::move(decoded);
}

void read(ByteReader& r, EnemyDeath& m) {
    m.epoch = r.readU32();
    m.netId = r.readU16();
}

void read(ByteReader& r, HitClaim& m) {
    if (r.remaining() != 43) throw std::runtime_error("HitClaim: wrong payload length");
    m.epoch = r.readU32();
    m.seq = r.readU32();
    m.netId = r.readU16();
    m.objectId = r.readU32();
    m.requesterConnectionId = r.readU64();
    m.attackId = r.readU32();
    m.damage = r.readI32();
    read(r, m.attackerPosition);
    m.attackerSlot = static_cast<SlotType>(r.readU8());
}

void read(ByteReader& r, ProgressUpdate& m) {
    m.version = r.readU32();
    m.full = r.readBool();
    m.spans.resize(r.readU16());
    for (auto& span : m.spans) {
        span.offset = r.readU32();
        span.bytes.resize(r.readU16());
        for (auto& b : span.bytes) b = r.readU8();
    }
}

void read(ByteReader& r, StateHash& m) {
    StateHash decoded;
    decoded.epoch = r.readU32();
    decoded.worldId = r.readU16();
    decoded.roomId = r.readU16();
    decoded.enemiesHash = r.readU32();
    decoded.progressHash = r.readU32();
    const auto complete = r.readU8();
    if (complete > 1) throw std::runtime_error("invalid native census completeness");
    decoded.nativeCensusComplete = complete != 0;
    decoded.nativeLivingCount = r.readU32();
    decoded.nativeCombatCount = r.readU32();
    m = decoded;
}

void read(ByteReader& r, DesyncNotice& m) {
    m.slot = static_cast<SlotType>(r.readU8());
    m.epoch = r.readU32();
    m.fields = r.readU8();
}



// ===== Framed packet helpers ================================================

static constexpr std::size_t kHeaderSize = 3; // 1 type + 2 length

std::vector<std::uint8_t> encodePacket(PacketType type,
                                       const std::vector<std::uint8_t>& payload) {
    if (payload.size() > 0xFFFF) {
        throw std::runtime_error("encodePacket: payload exceeds 64 KiB limit");
    }
    std::vector<std::uint8_t> out;
    out.reserve(kHeaderSize + payload.size());
    out.push_back(static_cast<std::uint8_t>(type));
    auto len = static_cast<std::uint16_t>(payload.size());
    out.push_back(static_cast<std::uint8_t>(len & 0xFF));
    out.push_back(static_cast<std::uint8_t>((len >> 8) & 0xFF));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

std::vector<std::uint8_t> encode(const InputFrame& f) {
    ByteWriter w;
    write(w, f);
    return encodePacket(PacketType::InputFrame, w.data());
}

std::vector<std::uint8_t> encode(const SessionState& ss) {
    ByteWriter w;
    write(w, ss);
    return encodePacket(PacketType::SessionState, w.data());
}

std::vector<std::uint8_t> encode(const ActorSnapshot& as) {
    ByteWriter w;
    write(w, as);
    return encodePacket(PacketType::ActorSnapshot, w.data());
}

std::vector<std::uint8_t> encode(const EnemySnapshot& es) {
    ByteWriter w;
    write(w, es);
    return encodePacket(PacketType::EnemySnapshot, w.data());
}

std::vector<std::uint8_t> encode(const EventMessage& em) {
    ByteWriter w;
    write(w, em);
    return encodePacket(PacketType::EventMessage, w.data());
}

std::vector<std::uint8_t> encode(const HelloReject& hr) {
    ByteWriter w;
    write(w, hr);
    return encodePacket(PacketType::HelloReject, w.data());
}

std::vector<std::uint8_t> encode(const ClientHello& ch) {
    ByteWriter w;
    write(w, ch);
    return encodePacket(PacketType::ClientHello, w.data());
}

std::vector<std::uint8_t> encode(const ClockPing& p) {
    ByteWriter w;
    write(w, p);
    return encodePacket(PacketType::ClockPing, w.data());
}

std::vector<std::uint8_t> encode(const ClockPong& p) {
    ByteWriter w;
    write(w, p);
    return encodePacket(PacketType::ClockPong, w.data());
}

void write(ByteWriter& w, const AvatarRelay& a) {
    w.writeU64(a.ownerConnectionId);
    write(w, a.avatar);
}

void read(ByteReader& r, AvatarRelay& a) {
    if (r.remaining() != AVATAR_RELAY_PAYLOAD_SIZE)
        throw std::runtime_error("AvatarRelay: wrong payload length");
    a.ownerConnectionId = r.readU64();
    read(r, a.avatar);
}

std::vector<std::uint8_t> encode(const AvatarRelay& a) {
    ByteWriter w;
    write(w, a);
    return encodePacket(PacketType::AvatarRelay, w.data());
}

std::vector<std::uint8_t> encode(const AvatarState& a, PacketType type) {
    if (type != PacketType::AvatarState) {
        throw std::invalid_argument("encode(AvatarState): bad packet type");
    }
    ByteWriter w;
    write(w, a);
    return encodePacket(type, w.data());
}

std::vector<std::uint8_t> encode(const RoomTransition& m) {
    ByteWriter w;
    write(w, m);
    return encodePacket(PacketType::RoomTransition, w.data());
}

std::vector<std::uint8_t> encode(const TransitionAck& m) {
    ByteWriter w;
    write(w, m);
    return encodePacket(PacketType::TransitionAck, w.data());
}

std::vector<std::uint8_t> encode(const ActivationRequest& m) {
    ByteWriter w;
    write(w, m);
    return encodePacket(PacketType::ActivationRequest, w.data());
}

std::vector<std::uint8_t> encode(const HostActivationPoint& m) {
    ByteWriter w;
    write(w, m);
    return encodePacket(PacketType::HostActivationPoint, w.data());
}

std::vector<std::uint8_t> encodeWorldSessionReset(std::uint32_t generation, std::uint64_t deliverySerial) {
    ByteWriter w;
    w.writeU32(generation);
    w.writeU64(deliverySerial);
    return encodePacket(PacketType::SessionState, w.data());
}

std::vector<std::uint8_t> encode(const EventHold& m) {
    ByteWriter w;
    write(w, m);
    return encodePacket(PacketType::EventHold, w.data());
}

std::vector<std::uint8_t> encode(const EnemyManifest& m) {
    ByteWriter w;
    write(w, m);
    return encodePacket(PacketType::EnemyManifest, w.data());
}

std::vector<std::uint8_t> encode(const EnemyHp& m) {
    ByteWriter w;
    write(w, m);
    return encodePacket(PacketType::EnemyHp, w.data());
}

std::vector<std::uint8_t> encode(const EnemyMotion& m) {
    ByteWriter w;
    write(w, m);
    return encodePacket(PacketType::EnemyMotion, w.data());
}

std::vector<std::uint8_t> encode(const EnemyDeath& m) {
    ByteWriter w;
    write(w, m);
    return encodePacket(PacketType::EnemyDeath, w.data());
}

std::vector<std::uint8_t> encode(const HitClaim& m) {
    ByteWriter w;
    write(w, m);
    return encodePacket(PacketType::HitClaim, w.data());
}

std::vector<std::uint8_t> encode(const ProgressUpdate& m) {
    ByteWriter w;
    write(w, m);
    return encodePacket(PacketType::ProgressUpdate, w.data());
}

std::vector<std::uint8_t> encode(const StateHash& m) {
    ByteWriter w;
    write(w, m);
    return encodePacket(PacketType::StateHash, w.data());
}

std::vector<std::uint8_t> encode(const DesyncNotice& m) {
    ByteWriter w;
    write(w, m);
    return encodePacket(PacketType::DesyncNotice, w.data());
}

std::vector<std::uint8_t> encode(const ResyncRequest& m) {
    ByteWriter w;
    write(w, m);
    return encodePacket(PacketType::ResyncRequest, w.data());
}

PacketType decodePacketHeader(const std::uint8_t* data, std::size_t size,
                              const std::uint8_t*& payloadOut,
                              std::size_t& payloadSizeOut) {
    if (size < kHeaderSize) {
        throw std::runtime_error("decodePacketHeader: buffer too small for header");
    }
    auto type = static_cast<PacketType>(data[0]);
    std::uint16_t len = static_cast<std::uint16_t>(data[1])
                      | (static_cast<std::uint16_t>(data[2]) << 8);
    if (size < kHeaderSize + len) {
        throw std::runtime_error("decodePacketHeader: buffer too small for payload");
    }
    if ((type == PacketType::PartyLayout || type == PacketType::PartyReapply || type == PacketType::PartyIntent) && size != kHeaderSize + len)
        throw std::runtime_error("Party: wrong frame length");
    if (type == PacketType::ReviveRequest && (len != 50 || size != kHeaderSize + len))
        throw std::runtime_error("ReviveRequest: wrong frame length");
    if (type == PacketType::HitClaim && (len != 43 || size != kHeaderSize + len))
        throw std::runtime_error("HitClaim: wrong frame length");
    if (type == PacketType::EnemyHp && (len < 14 || size != kHeaderSize + len))
        throw std::runtime_error("EnemyHp: wrong frame length");
    if (type == PacketType::EnemyMotion &&
        (len < ENEMY_MOTION_HEADER_BYTES || (len - ENEMY_MOTION_HEADER_BYTES) % ENEMY_MOTION_ENTRY_BYTES != 0 ||
         size != kHeaderSize + len))
        throw std::runtime_error("EnemyMotion: wrong frame length");
    if (type == PacketType::AvatarRelay &&
        (len != AVATAR_RELAY_PAYLOAD_SIZE || size != kHeaderSize + len))
        throw std::runtime_error("AvatarRelay: wrong frame length");
    if ((type == PacketType::DesyncCaptureRequest || type == PacketType::DesyncArtifactChunk ||
         type == PacketType::DesyncCaptureDone) && size != kHeaderSize + len)
        throw std::runtime_error("desync diagnostic: wrong frame length");
    if ((type == PacketType::ResyncRequest || (data[0] >= 33 && data[0] <= 40) || data[0] >= 0xF0) && size != kHeaderSize + len)
        throw std::runtime_error("resync: wrong frame length");
    payloadOut = data + kHeaderSize;
    payloadSizeOut = len;
    return type;
}

void validateActivationPacket(const std::vector<std::uint8_t>& packet) {
    const std::uint8_t* payload = nullptr;
    std::size_t size = 0;
    const auto type = decodePacketHeader(packet.data(), packet.size(), payload, size);
    if (packet.size() != size + kHeaderSize)
        throw std::runtime_error("Activation packet: trailing frame bytes");
    ByteReader r(payload, size);
    if (type == PacketType::ActivationRequest) {
        ActivationRequest m;
        read(r, m);
    } else if (type == PacketType::HostActivationPoint) {
        HostActivationPoint m;
        read(r, m);
    } else {
        throw std::runtime_error("Activation packet: wrong type");
    }
}

// ===== Debug strings ========================================================

// Small helper to avoid snprintf boilerplate.
static std::string fmt(const char* format, ...) {
    char buf[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    return std::string(buf);
}

static const char* slotName(SlotType s) {
    switch (s) {
        case SlotType::Player: return "PLAYER";
        case SlotType::Friend1: return "FRIEND_1";
        case SlotType::Friend2: return "FRIEND_2";
    }
    return "?";
}

static const char* actionName(ActionState a) {
    switch (a) {
        case ActionState::Unknown: return "Unknown";
        case ActionState::Idle: return "Idle";
        case ActionState::Move: return "Move";
        case ActionState::Jump: return "Jump";
        case ActionState::Attack: return "Attack";
        case ActionState::Guard: return "Guard";
        case ActionState::Dodge: return "Dodge";
        case ActionState::Stagger: return "Stagger";
        case ActionState::Downed: return "Downed";
    }
    return "?";
}

static const char* eventName(EventType t) {
    switch (t) {
        case EventType::Unknown: return "Unknown";
        case EventType::SpawnGroup: return "SpawnGroup";
        case EventType::KillEnemy: return "KillEnemy";
        case EventType::RewardGranted: return "RewardGranted";
        case EventType::RoomTransitionBegin: return "RoomTransitionBegin";
        case EventType::RoomTransitionComplete: return "RoomTransitionComplete";
        case EventType::CutsceneBegin: return "CutsceneBegin";
        case EventType::CutsceneEnd: return "CutsceneEnd";
        case EventType::PlayerKo: return "PlayerKo";
        case EventType::PlayerRevive: return "PlayerRevive";
        case EventType::ForceTeleport: return "ForceTeleport";
        case EventType::SessionResyncRequired: return "SessionResyncRequired";
    }
    return "?";
}

std::string toDebugString(const Vec3& v) {
    return fmt("(%.2f, %.2f, %.2f)", v.x, v.y, v.z);
}

std::string toDebugString(const ActorState& a) {
    return fmt("Actor{id=%u slot=%s pos=%s rotY=%.2f action=%s hp=%d mp=%d}",
               a.actorId, slotName(a.slot), toDebugString(a.position).c_str(),
               a.rotationY, actionName(a.action), a.hp, a.mp);
}

std::string toDebugString(const EnemyState& e) {
    return fmt("Enemy{net=%u obj=%u pos=%s hp=%d alive=%s}",
               e.netId, e.objectId, toDebugString(e.position).c_str(),
               e.hp, e.alive ? "yes" : "no");
}

std::string toDebugString(const RoomState& r) {
    return fmt("Room{world=%u room=%u map=%u battle=%u event=%u transition=%s cutscene=%s}",
               r.worldId, r.roomId, r.mapProgram, r.battleProgram, r.eventProgram,
               r.inTransition ? "yes" : "no", r.inCutscene ? "yes" : "no");
}

std::string toDebugString(const InputFrame& f) {
    return fmt("Input{seq=%u actor=%u stick=(%.2f,%.2f) atk=%d jmp=%d grd=%d}",
               f.seq, f.ownedActorId, f.leftStickX, f.leftStickY,
               f.buttons.attack, f.buttons.jump, f.buttons.guard);
}

std::string toDebugString(const SessionState& ss) {
    return fmt("Session{id=%s build=%s mod=%s room=%s actors=%zu}",
               ss.sessionId.c_str(), ss.gameBuild.c_str(), ss.modHash.c_str(),
               toDebugString(ss.room).c_str(), ss.actors.size());
}

std::string toDebugString(const ActorSnapshot& as) {
    return fmt("ActorSnap{snap=%u %s}", as.snapshotId,
               toDebugString(as.actor).c_str());
}

std::string toDebugString(const EnemySnapshot& es) {
    return fmt("EnemySnap{snap=%u %s}", es.snapshotId,
               toDebugString(es.enemy).c_str());
}

std::string toDebugString(const EventMessage& em) {
    return fmt("Event{snap=%u type=%s payload=%s}", em.snapshotId,
               eventName(em.type), em.payloadJson.c_str());
}

static const char* runtimeModeName(RuntimeMode m) {
    switch (m) {
        case RuntimeMode::CampaignCoop: return "CampaignCoop";
        case RuntimeMode::PublicRealm: return "PublicRealm";
    }
    return "?";
}

std::string toDebugString(const ClientHello& ch) {
    return fmt("ClientHello{proto=%u build=%s peer=%s name=%s mode=%s slot=%u}",
               ch.protocolVersion, ch.gameBuild.c_str(), ch.peerId.c_str(),
               ch.peerName.c_str(), runtimeModeName(ch.requestedMode),
               ch.requestedSlot);
}

std::string toDebugString(const AvatarState& a) {
    return fmt("Avatar{seq=%u t=%llu slot=%u room=%u/%u pos=%s rot=%.2f motion=%u@%.2f hp=%d/%d}",
               a.seq, static_cast<unsigned long long>(a.serverTimeMs),
               static_cast<unsigned>(a.ownerSlot), a.worldId, a.roomId,
               toDebugString(a.position).c_str(), a.rotationY, a.motionId,
               a.motionTime, a.hp, a.maxHp);
}

} // namespace kh2coop
