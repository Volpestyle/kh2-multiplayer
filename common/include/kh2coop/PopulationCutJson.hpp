#pragma once
#include "kh2coop/PopulationCut.hpp"
#include <iomanip>
#include <sstream>
#include <string>
namespace kh2coop {
inline std::string PopulationJsonString(const std::string& text) {
    std::ostringstream out;out<<'"';
    for(unsigned char c:text) {
        if(c=='"'||c=='\\')out<<'\\'<<char(c);
        else if(c<32)out<<"\\u"<<std::hex<<std::setfill('0')<<std::setw(4)<<unsigned(c)<<std::dec;
        else out<<char(c);
    }
    out<<'"';return out.str();
}
inline std::string PopulationLocationJson(const NativeRecordLocation& l) {
    std::ostringstream s;s<<'['<<l.world<<','<<l.room<<','<<l.door<<','<<l.mapProgram<<','<<l.battleProgram<<','<<l.eventProgram<<']';return s.str();
}
template<class Range> inline std::string PopulationHex(const Range& bytes) {
    std::ostringstream s;s<<std::hex<<std::setfill('0');for(auto b:bytes)s<<std::setw(2)<<unsigned(b);return s.str();
}
inline std::string PopulationMembershipJson(const NativeRecordContentDefinition& d,std::uint16_t record) {
    std::array<std::uint8_t,43> projection{};
    for(std::size_t i=0,n=0;i<d.header.size();++i)if(i!=14)projection[n++]=d.header[i];
    std::ostringstream s;s<<"{\"layoutSha256\":\""<<PopulationHex(d.layoutSha256)<<"\",\"groupKey\":"<<d.groupKey
        <<",\"headerComparisonHex\":\""<<PopulationHex(projection)<<"\",\"location\":"<<PopulationLocationJson(d.location)<<",\"recordsHex\":[";
    bool first=true;for(const auto& r:d.records){if(!first)s<<',';first=false;s<<'"'<<PopulationHex(r)<<'"';}
    s<<"],\"ordinal\":"<<record<<'}';return s.str();
}
inline std::string PopulationCutJson(const PopulationCut& cut,const std::string& session) {
    ValidatePopulationCut(cut);std::ostringstream s;
    s<<"{\"schema\":1,\"session\":"<<PopulationJsonString(session)<<",\"epoch\":"<<cut.epoch
        <<",\"location\":"<<PopulationLocationJson(cut.location)<<",\"revision\":"<<cut.sequence
        <<",\"sequence\":"<<cut.sequence<<",\"complete\":true,\"rows\":[";
    bool first=true;for(const auto& e:cut.entries){if(!first)s<<',';first=false;
        s<<"{\"netId\":"<<e.netId<<",\"objectId\":"<<e.objectId<<",\"terminal\":"<<(e.terminal?"true":"false")
            <<",\"birthSequence\":"<<e.birthSequence<<",\"deathSequence\":"<<e.terminalSequence
            <<",\"membership\":"<<PopulationMembershipJson(cut.definitions[e.definition],e.record)<<'}';}
    s<<"]}";return s.str();
}
} // namespace kh2coop
