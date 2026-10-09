#pragma once
#include "kh2coop/ByteBuffer.hpp"
#include "kh2coop/NativeRecordContentTypes.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
namespace kh2coop {
inline constexpr std::size_t POPULATION_MAX_DEFINITIONS=64, POPULATION_MAX_RECORDS=512,
                             POPULATION_MAX_ENTRIES=64, POPULATION_MAX_PAYLOAD=60000;
struct PopulationOccurrence {
    std::uint16_t netId{},definition{},record{};
    std::uint32_t objectId{};
    std::uint64_t occurrence{},birthSequence{},terminalSequence{};
    bool terminal{};
    std::array<float,4> activationPoint{};
};
struct PopulationCut {
    std::uint32_t epoch{},hostLoad{},hostTransition{};
    std::uint64_t sequence{};
    NativeRecordLocation location{};
    std::vector<NativeRecordContentDefinition> definitions;
    std::vector<PopulationOccurrence> entries;
};
namespace population_cut_detail {
inline void require(bool b){if(!b)throw std::runtime_error("PopulationCut: invalid bounded record");}
inline void location(ByteWriter& w,const NativeRecordLocation& l){
    w.writeU16(l.world);w.writeU16(l.room);w.writeU16(l.door);w.writeU16(l.mapProgram);
    w.writeU16(l.battleProgram);w.writeU16(l.eventProgram);
}
inline NativeRecordLocation location(ByteReader& r){return {r.readU16(),r.readU16(),r.readU16(),r.readU16(),r.readU16(),r.readU16()};}
inline bool validDefinition(const NativeRecordContentDefinition& d){
    return d.groupKey && !d.records.empty() && d.records.size()<=NativeRecordContentMaxRecords && d.records.size()==
        (std::size_t(d.header[4])|(std::size_t(d.header[5])<<8)) &&
        std::any_of(d.layoutSha256.begin(),d.layoutSha256.end(),[](auto b){return b!=0;});
}
inline bool sameDefinition(const NativeRecordContentDefinition& a,const NativeRecordContentDefinition& b){
    if(a.layoutSha256!=b.layoutSha256||a.location!=b.location||a.groupKey!=b.groupKey||a.records!=b.records)return false;
    for(std::size_t i=0;i<a.header.size();++i)if(i!=14 && a.header[i]!=b.header[i])return false;
    return true;
}
inline std::uint32_t object(const std::array<std::uint8_t,64>& bytes){
    return std::uint32_t(bytes[0])|(std::uint32_t(bytes[1])<<8)|(std::uint32_t(bytes[2])<<16)|(std::uint32_t(bytes[3])<<24);
}
}
inline void ValidatePopulationCut(const PopulationCut& cut){
    using namespace population_cut_detail;
    require(cut.epoch && cut.sequence && cut.hostLoad && cut.hostTransition &&
        cut.definitions.size()<=POPULATION_MAX_DEFINITIONS && cut.entries.size()<=POPULATION_MAX_ENTRIES);
    std::size_t total=0;
    for(const auto& d:cut.definitions){
        require(validDefinition(d) && d.location==cut.location);
        require(d.records.size()<=POPULATION_MAX_RECORDS-total);total+=d.records.size();
    }
    // Same content associated to multiple definitions is ambiguous, including aliases.
    for(std::size_t i=0;i<cut.definitions.size();++i)
        for(std::size_t j=0;j<i;++j)require(!sameDefinition(cut.definitions[i],cut.definitions[j]));
    std::set<std::uint16_t> ids;
    std::set<std::uint64_t> occurrences;
    for(const auto& e:cut.entries){
        require(e.netId && e.occurrence && e.birthSequence && e.birthSequence<=cut.sequence &&
            ids.insert(e.netId).second && occurrences.insert(e.occurrence).second &&
            e.definition<cut.definitions.size());
        const auto& d=cut.definitions[e.definition];
        require(e.record<d.records.size() && object(d.records[e.record])==e.objectId && e.objectId &&
            std::all_of(e.activationPoint.begin(),e.activationPoint.end(),[](float v){return std::isfinite(v);}) &&
            e.activationPoint[3]==1.0f);
        require(e.terminal ? (e.terminalSequence>=e.birthSequence && e.terminalSequence<=cut.sequence) : e.terminalSequence==0);
        for(const auto& other:cut.entries)
            if(&other!=&e && !e.terminal && !other.terminal)
                require(e.definition!=other.definition || e.record!=other.record);
    }
}
inline void write(ByteWriter& writer,const PopulationCut& cut){
    using namespace population_cut_detail;
    ValidatePopulationCut(cut);ByteWriter w;
    w.writeU8(1);w.writeU32(cut.epoch);w.writeU64(cut.sequence);w.writeU32(cut.hostLoad);w.writeU32(cut.hostTransition);
    location(w,cut.location);w.writeU16(static_cast<std::uint16_t>(cut.definitions.size()));
    for(const auto& d:cut.definitions){
        for(auto b:d.layoutSha256) { w.writeU8(b); }
        location(w,d.location);w.writeU32(d.groupKey);
        for(auto b:d.header) { w.writeU8(b); }
        w.writeU16(static_cast<std::uint16_t>(d.records.size()));
        for(const auto& record:d.records)for(auto b:record)w.writeU8(b);
    }
    w.writeU16(static_cast<std::uint16_t>(cut.entries.size()));
    for(const auto& e:cut.entries){w.writeU16(e.netId);w.writeU16(e.definition);w.writeU16(e.record);w.writeU32(e.objectId);
        w.writeU64(e.occurrence);w.writeU64(e.birthSequence);w.writeU64(e.terminalSequence);w.writeBool(e.terminal);
        for(auto v:e.activationPoint)w.writeF32(v);}
    require(w.size()<=POPULATION_MAX_PAYLOAD);for(auto b:w.data())writer.writeU8(b);
}
inline void read(ByteReader& r,PopulationCut& destination){
    using namespace population_cut_detail;
    require(r.remaining()<=POPULATION_MAX_PAYLOAD && r.readU8()==1);PopulationCut cut;
    cut.epoch=r.readU32();cut.sequence=r.readU64();cut.hostLoad=r.readU32();cut.hostTransition=r.readU32();
    cut.location=location(r);auto n=r.readU16();require(n<=POPULATION_MAX_DEFINITIONS);std::size_t total=0;
    for(std::size_t i=0;i<n;++i){NativeRecordContentDefinition d;
        for(auto& b:d.layoutSha256) { b=r.readU8(); }
        d.location=location(r);d.groupKey=r.readU32();
        for(auto& b:d.header) { b=r.readU8(); }
        const auto count=r.readU16();require(count<=NativeRecordContentMaxRecords && count<=POPULATION_MAX_RECORDS-total);
        require(static_cast<std::size_t>(count)*64<=r.remaining());total+=count;d.records.resize(count);
        for(auto& record:d.records) { for(auto& b:record) { b=r.readU8(); } }
        cut.definitions.push_back(std::move(d));}
    n=r.readU16();require(n<=POPULATION_MAX_ENTRIES);cut.entries.reserve(n);
    for(std::size_t i=0;i<n;++i){PopulationOccurrence e;e.netId=r.readU16();e.definition=r.readU16();e.record=r.readU16();
        e.objectId=r.readU32();e.occurrence=r.readU64();e.birthSequence=r.readU64();e.terminalSequence=r.readU64();
        const auto terminal=r.readU8();require(terminal<=1);e.terminal=terminal!=0;
        for(auto& v:e.activationPoint) { v=r.readF32(); }
        cut.entries.push_back(e);}
    require(r.atEnd());ValidatePopulationCut(cut);destination=std::move(cut); // no partial install or poisoned floors
}
} // namespace kh2coop
