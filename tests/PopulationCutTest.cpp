#include "kh2coop/Codec.hpp"
#include "kh2coop/PopulationCutJson.hpp"
#include "kh2coop/OrdinaryLifecycle.hpp"
#include <cstdio>
#include <limits>
#include <string>
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
static PopulationCut membership() {
    PopulationCut c;c.membershipOnly=true;c.epoch=2;c.hostLoad=3;c.hostTransition=2;c.sequence=5;c.location={5,6,0,1,1,0};
    NativeRecordContentDefinition d;d.location=c.location;d.groupKey=1;d.layoutSha256.fill(1);d.header[4]=5;d.records.resize(5);
    for(auto& r:d.records){r[0]=0x2e;r[1]=1;}c.definitions.push_back(d);
    for(unsigned i=0;i<5;++i)c.entries.push_back({static_cast<std::uint16_t>(i+1),0,static_cast<std::uint16_t>(i),302,i+1,i+1,0,false,{float(i),0,0,1}});
    return c;
}
static void lifecycleControls() {
    auto first=membership();ordinarylifecycle::History history;CHECK(history.Admit(first));
    auto retire=first;retire.sequence=6;retire.entries[3].terminal=true;retire.entries[3].removed=true;retire.entries[3].terminalSequence=6;
    CHECK(history.Admit(retire));CHECK(!history.Admit(retire)); // duplicate
    auto birth=retire;birth.sequence=7;auto row=first.entries[3];row.netId=6;row.occurrence=row.birthSequence=7;birth.entries.push_back(row);
    CHECK(history.Admit(birth));
    ByteWriter w;write(w,birth);ByteReader r(w.data());PopulationCut decoded;read(r,decoded);
    CHECK(decoded.membershipOnly && decoded.entries[3].removed && encode(decoded)==encode(birth));
    CHECK(PopulationCutJson(birth,"s").find("\"deathSequence\":0")!=std::string::npos);
    // Retained ca007 B friend serial198: epoch2/HPseq24 includes replacement6.
    // Its original cut admits1..5, so the whole HP packet must be rejected
    // before advancing the floor. The ordered constructed successor admits6.
    const char* retained="17400002000000180000000000000005000100140000001400000002001400000014000000030014000000140000000500140000001400000006001400000014000000";
    std::vector<std::uint8_t> raw;
    for(std::size_t i=0;retained[i];i+=2)raw.push_back(static_cast<std::uint8_t>(std::stoul(std::string(retained+i,2),nullptr,16)));
    const std::uint8_t* payload=nullptr;std::size_t bytes=0;
    CHECK(decodePacketHeader(raw.data(),raw.size(),payload,bytes)==PacketType::EnemyHp);
    ByteReader hpReader(payload,bytes);EnemyHp hp;read(hpReader,hp);
    CHECK(hp.epoch==2 && hp.sequence==24 && hp.entries.size()==5 && hp.entries.back().netId==6);
    bool unknown=false;for(const auto& e:hp.entries)unknown=unknown||!ordinarylifecycle::AdmitsHp(&first,e.netId);
    CHECK(unknown);CHECK(!ordinarylifecycle::AdmitsHp(&retire,4));
    for(const auto& e:hp.entries)CHECK(ordinarylifecycle::AdmitsHp(&birth,e.netId));
    for(unsigned mode=0;mode<9;++mode) {
        ordinarylifecycle::History h;CHECK(h.Admit(first));auto bad=birth;
        switch(mode) {
        case 0:bad.entries[3]=first.entries[3];break; // missing retirement
        case 1:bad.entries.back().netId=4;break; // reused ID
        case 2:bad.entries.back().birthSequence=bad.entries.back().occurrence=6;break; // reorder
        case 3:bad.entries.push_back(bad.entries.back());break;
        case 4:++bad.hostLoad;break;
        case 5:++bad.epoch;break;
        case 6:bad.entries[3].removed=false;break; // kill cannot stand in for alive retirement
        case 7:bad.definitions[0].records[3][4]^=1;break; // changed original content
        case 8:bad.entries.erase(bad.entries.begin());break;
        }
        CHECK(!h.Admit(bad));CHECK(h.Current()->sequence==5);
    }
}
int main() {lifecycleControls();
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
