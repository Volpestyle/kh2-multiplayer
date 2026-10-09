#include "kh2coop/Codec.hpp"
#include "kh2coop/PopulationCutJson.hpp"
#include <cstdio>
#include <limits>
using namespace kh2coop;
static int failures=0;
#define CHECK(x) do {if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);++failures;}}while(0)
static PopulationCut sample() {
    PopulationCut cut;cut.epoch=2;cut.hostLoad=2;cut.hostTransition=1;cut.sequence=100;
    cut.location={5,6,0,1,1,0};
    NativeRecordContentDefinition definition;definition.location=cut.location;
    definition.groupKey=808476514;definition.layoutSha256.fill(1);definition.header[4]=2;
    definition.records.resize(2);definition.records[0][0]=0x2e;definition.records[0][1]=1;
    definition.records[1][0]=4;cut.definitions.push_back(definition);
    cut.entries.push_back({1,0,0,302,80,80,90,true,{10,0,20,1}});
    cut.entries.push_back({8,0,1,4,95,95,0,false,{20,0,30,1}});return cut;
}
template<class F> static bool refuses(F operation) {
    try {operation();return false;}catch(const std::exception&) {return true;}
}
int main() {
    auto cut=sample();ByteWriter writer;write(writer,cut);PopulationCut out;ByteReader reader(writer.data());read(reader,out);
    CHECK(out.entries.size()==2 && out.entries[0].terminalSequence==90 && out.sequence==100);
    CHECK(encode(out)==encode(cut));CHECK(isScopedWorldPacket(PacketType::PopulationCut));
    for(std::size_t n=0;n<writer.size();++n) {
        PopulationCut sentinel;sentinel.sequence=999;
        CHECK(refuses([&]{ByteReader shortReader(writer.data().data(),n);read(shortReader,sentinel);}));
        CHECK(sentinel.sequence==999); // decode cannot partially install a poisoned cut
    }
    auto bad=writer.data();bad.push_back(0);
    CHECK(refuses([&]{ByteReader r(bad);PopulationCut value;read(r,value);}));
    bad=writer.data();bad[33]=0xff;bad[34]=0xff;
    CHECK(refuses([&]{ByteReader r(bad);PopulationCut value;read(r,value);}));
    for(int mode=0;mode<11;++mode) {
        auto invalid=sample();
        switch(mode) {
        case 0:invalid.entries[0].terminalSequence=101;break;
        case 1:invalid.entries[0].birthSequence=91;break;
        case 2:invalid.entries[1].netId=1;break;
        case 3:invalid.entries[1].occurrence=80;break;
        case 4:invalid.entries[0].objectId=4;break;
        case 5:invalid.entries[0].record=2;break;
        case 6:invalid.entries[0].activationPoint[0]=std::numeric_limits<float>::quiet_NaN();break;
        case 7:invalid.definitions[0].location.eventProgram=1;break;
        case 8:invalid.definitions.push_back(invalid.definitions[0]);break;
        case 9:invalid.definitions[0].header[4]=6;break;
        case 10:invalid.entries[0].terminal=false;invalid.entries[0].terminalSequence=0;
                invalid.entries[1].record=0;invalid.entries[1].objectId=302;break;
        }
        CHECK(refuses([&]{ByteWriter w;write(w,invalid);}));
    }
    CHECK(PopulationCutJson(cut,"session").find("\"deathSequence\":90")!=std::string::npos);
    CHECK(PopulationJsonString("a\"\\\n")=="\"a\\\"\\\\\\u000a\"");
    std::printf("PopulationCutTest: %s\n",failures?"FAIL":"PASS");return failures?1:0;
}
