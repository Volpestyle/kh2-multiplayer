#define main OriginalNativeHitMain
#include "NativeHitClaimTest.cpp"
#undef main
#include <fstream>
std::string field(const std::string& row,const std::string& key){const auto p=" "+key+"=";auto at=row.find(p);if(at==std::string::npos)return {};at+=p.size();return row.substr(at,row.find(' ',at)-at);}
int main(){
    image=reinterpret_cast<uintptr_t>(VirtualAlloc(nullptr,0x3000000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));if(!image)return 2;
    std::ofstream raw("native-raw.log");std::vector<std::string> rows;
    const CausalSink sink=[&](const auto& row){rows.push_back(row);raw<<row<<'\n';raw.flush();return raw.good();};
    const auto start=[&]{Reset(true);g_hashDiagnosticSink={};g_hashDiagnosticStream={};rows.clear();};
    const auto publish=[&]{g_lastHashMs=0;PublishAppliedHash(777,CaptureNativeCensus());};
    for(const char* setting:{"","0","01","true","2"," 1"}){
        start();SetEnvironmentVariableA("KH2COOP_CAUSAL_DIAGNOSTICS",setting);SetHashDiagnosticSink(sink);publish();
        Check(rows.empty()&&!g_hashDiagnosticStream.highWater&&HasHash(hashAppliedEnemies({{1,309,1000}})),"native opt-in exact; disabled still publishes unchanged real hash");
    }
    SetEnvironmentVariableA("KH2COOP_CAUSAL_DIAGNOSTICS","1");start();SetHashDiagnosticSink(sink);publish();
    Check(rows.size()==2&&field(rows[0],"censusCount")=="1"&&field(rows[0],"living")=="1"&&field(rows[0],"selectedAvailable")=="0"&&field(rows[0],"selectedPresent")=="unavailable","nonempty native census never invents selected-controller membership");
    Check(field(rows[0],"enemiesHash")==std::to_string(hashAppliedEnemies({{1,309,1000}}))&&field(rows[0],"connection")=="101"&&field(rows[0],"contextCurrent")=="1"&&field(rows[0],"generation")=="1"&&field(rows[0],"delivery")=="1","actual publisher receipt matches decoded H and current bridge context");
    Check(field(rows[0],"world")=="5"&&field(rows[0],"room")=="2"&&field(rows[0],"door")=="3"&&field(rows[0],"map")=="4"&&field(rows[0],"battle")=="6"&&field(rows[0],"event")=="8"&&field(rows[0],"load")=="11"&&field(rows[0],"transition")=="7","full native tuple and lifecycle captured from actual census");
    Check(field(rows[0],"hostSource")=="0"&&g_bridge.outgoingContexts.back().hostSourceSerial==0,"client actual enqueue source serial is zero, not invented publication identity");
    start();Reset(false);SetHashDiagnosticSink(sink);publish();
    Check(rows.size()==2&&field(rows[0],"hostSource")==std::to_string(g_bridge.outgoingContexts.back().hostSourceSerial)&&g_bridge.outgoingContexts.back().hostSourceSerial!=0&&g_worldSourceSerial==1,"host receipt retains the one actual enqueue source serial without second context allocation");
    start();SetHashDiagnosticSink(sink);Put(image+offsets::active_entity_list::TAIL,player);Put(player+offsets::actor::LINKED_NEXT_HANDLE,std::uint32_t{0});publish();
    Check(rows.size()==2&&field(rows[0],"complete")=="1"&&field(rows[0],"censusCount")=="0"&&field(rows[0],"living")=="0"&&field(rows[0],"selectedAvailable")=="1"&&field(rows[0],"selectedPresent")=="0"&&field(rows[0],"selectedProof")=="complete-whole-combat-census-empty"&&HasHash(hashAppliedEnemies({})),"complete whole native combat census empty explicitly proves selected zero");
    Check(field(rows[1],"highWater")=="1"&&field(rows[1],"flushedHighWater")=="1"&&field(rows[1],"dropped")=="0","native seal is backed by actual flushed row");
    start();SetHashDiagnosticSink(sink);Put(status,std::int32_t{0});publish();
    Check(rows.size()==2&&field(rows[0],"censusCount")=="1"&&field(rows[0],"living")=="0"&&field(rows[0],"selectedAvailable")=="0"&&HasHash(hashAppliedEnemies({})),"empty living hash alone cannot prove selected zero when native dead actor remains");
    start();SetHashDiagnosticSink(sink);Put(image+offsets::active_entity_list::TAIL,uintptr_t{0});publish();Check(rows.empty()&&g_bridge.outgoing.empty(),"incomplete native census emits neither hash nor proof");
    start();SetHashDiagnosticSink(sink);g_bridge.sendAllowed=false;publish();Check(rows.empty()&&!g_hashDiagnosticStream.highWater,"failed bridge enqueue emits no published proof");
    start();SetHashDiagnosticSink([](const auto&){return false;});publish();Check(g_hashDiagnosticStream.highWater==1&&g_hashDiagnosticStream.flushed==0&&g_hashDiagnosticStream.dropped==2&&g_hashDiagnosticStream.unavailable&&HasHash(hashAppliedEnemies({{1,309,1000}})),"failed native receipt and seal flush are gaps without changing publication");
    start();SetHashDiagnosticSink(sink);g_hashDiagnosticStream.highWater=CausalStream::Limit;publish();Check(g_hashDiagnosticStream.highWater==CausalStream::Limit&&g_hashDiagnosticStream.dropped==1&&rows.size()==1&&field(rows[0],"unavailable")=="1","native exhaustion emits only unavailable seal and never wraps");
    g_hashDiagnosticSink={};VirtualFree(reinterpret_cast<void*>(image),0,MEM_RELEASE);std::cout<<"failures="<<errors<<'\n';return errors?1:0;
}
