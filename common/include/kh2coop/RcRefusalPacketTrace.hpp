#pragma once
// Only Protocol-aware transport/native-consumer TUs may include this header.
#include "kh2coop/RcRefusalTrace.hpp"
#include "kh2coop/Codec.hpp"
#include "kh2coop/WorldContext.hpp"
#include <string_view>

namespace kh2coop::rcdiag {
inline void ProducerScope(Record& r,const ProducerWorldContext& c) noexcept {
    r.facts|=Producer;r.generation=c.generation;r.sourceDelivery=c.deliverySerial;r.hostSource=c.hostSourceSerial;
}
inline void EnvelopeScope(Record& r,const WorldScope& s) noexcept {
    r.facts|=Envelope;r.kind=static_cast<unsigned>(s.kind);r.source=s.sourceConnectionId;r.sourceDelivery=s.sourceDeliverySerial;
    r.target=s.targetConnectionId;r.targetDelivery=s.targetDeliverySerial;r.hostSource=s.hostSourceSerial;
    std::uint64_t hash=14695981039346656037ull;
    for(unsigned char c:std::string_view(s.sessionId)){hash^=c;hash*=1099511628211ull;}
    r.session=hash; // correlation only; never an authority/session comparison
}
inline Record Message(const ScriptReactionCompletion& m) noexcept {
    auto r=Claim(m.location.epoch,m.resource,m.objectId,m.netId,m.command,m.ownerSlot,
                 m.requesterConnectionId,m.occurrence,m.seq);
    r.type=static_cast<unsigned>(PacketType::ScriptReactionCompletion);return r;
}
inline Record Packet(const std::vector<std::uint8_t>& bytes,unsigned depth=0) noexcept {
    Record r{};r.bytes=static_cast<std::uint32_t>(bytes.size());r.type=bytes.empty()?0:bytes.front();
    // No arbitrary recursive decoding. A normal completion envelope is <4KiB.
    // An unavailable/malformed diagnostic tuple never changes packet handling.
    if(bytes.empty() || bytes.size()>4096 ||
       (r.type!=static_cast<unsigned>(PacketType::ScriptReactionCompletion) &&
        r.type!=static_cast<unsigned>(PacketType::WorldEnvelope)))return r;
    try {
        const std::uint8_t* payload=nullptr;std::size_t size=0;
        const auto type=decodePacketHeader(bytes.data(),bytes.size(),payload,size);
        if(bytes.size()!=size+3)return r;
        ByteReader reader(payload,size);
        if(type==PacketType::ScriptReactionCompletion) {
            ScriptReactionCompletion m;read(reader,m);if(!reader.atEnd())return r;
            auto parsed=Message(m);parsed.type=static_cast<unsigned>(type);parsed.bytes=r.bytes;return parsed;
        }
        if(type==PacketType::WorldEnvelope && depth==0) {
            WorldEnvelope e;read(reader,e);if(!reader.atEnd())return r;
            auto inner=Packet(e.packet,1); // never recurse beyond one envelope
            if(inner.type!=static_cast<unsigned>(PacketType::ScriptReactionCompletion))return r;
            EnvelopeScope(inner,e.scope);inner.bytes=r.bytes;return inner;
        }
    }catch(...) {}
    return r;
}
inline Record Packet(const std::uint8_t* data,std::size_t size) noexcept {
    if(!data || !size)return {};
    Record r{};r.bytes=static_cast<std::uint32_t>(size);r.type=data[0];
    if(size>4096 || (r.type!=static_cast<unsigned>(PacketType::ScriptReactionCompletion) &&
                      r.type!=static_cast<unsigned>(PacketType::WorldEnvelope)))return r;
    try {return Packet(std::vector<std::uint8_t>(data,data+size));}catch(...){return r;}
}
inline bool Completion(const Record& r) noexcept {return r.type==static_cast<unsigned>(PacketType::ScriptReactionCompletion);}
template<class Sink> inline void Note(Stage stage,const char* result,const char* reason,
                                      const Record& r,Sink&& sink) noexcept {
    if(Completion(r))Emit(stage,result,reason,r,static_cast<Sink&&>(sink));
}
inline void Stdout(Stage stage,const char* result,const char* reason,const Record& r) noexcept {
    Note(stage,result,reason,r,[](const char* line){std::printf("%s\n",line);std::fflush(stdout);});
}
} // namespace kh2coop::rcdiag
