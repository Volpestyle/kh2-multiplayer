// Actual ENet + production report assembly/collector/upload controls.
// Synthetic files/PNG/time are fixtures; no native renderer or multi-PC proof.
#include "kh2coop/DesyncCapture.hpp"
#include "kh2coop/DesyncCollector.hpp"
#include "kh2coop/DesyncUpload.hpp"
#include "kh2coop/NetworkClient.hpp"
#include "kh2coop/SessionHost.hpp"
#include <enet/enet.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <thread>
using namespace kh2coop;
namespace {
int checks=0,failures=0;
void check(bool good,const char* label){++checks;if(!good)++failures;std::cout<<(good?"PASS: ":"FAIL: ")<<label<<'\n';}
using Clock=std::chrono::steady_clock;
bool until(const std::function<bool()>& condition,const std::function<void()>& pump={}) {
    const auto deadline=Clock::now()+std::chrono::seconds(5);
    while(!condition()&&Clock::now()<deadline){if(pump)pump();std::this_thread::sleep_for(std::chrono::milliseconds(1));}return condition();
}
std::vector<std::uint8_t> readFile(const std::filesystem::path& path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
std::string textFile(const std::filesystem::path& path){auto b=readFile(path);return {b.begin(),b.end()};}
void writeFile(const std::filesystem::path& path,const std::vector<std::uint8_t>& bytes){std::filesystem::create_directories(path.parent_path());std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));}
DesyncCaptureRequest request(std::uint64_t id=1){
    DesyncCaptureRequest r;r.key={std::string(32,'a'),id};r.connections={0x100000001ULL,0x200000002ULL,0x300000003ULL};
    r.epoch=7;r.divergedSlot=2;r.fields=DesyncEnemies;r.hostHash={7,4,26,123,456};r.clientHash={7,4,26,124,456};
    r.hostReceiptSeq=4;r.clientReceiptSeq=2;r.hostReceiptMs=900;r.clientReceiptMs=800;r.comparisonSeq=8;r.triggerMs=1000;r.deadlineMs=31000;return r;
}
std::vector<std::uint8_t> png(unsigned tag);
using Blobs=std::array<std::vector<std::uint8_t>,4>;
Blobs bytesFor(unsigned slot){Blobs b;for(std::size_t i=0;i<4;++i)b[i]=std::vector<std::uint8_t>(i==2?DESYNC_CHUNK_BYTES+19:31,static_cast<std::uint8_t>(10+slot*4+i));b[3]=png(slot);return b;}
DesyncCaptureDone doneFor(const DesyncCaptureRequest& req,unsigned slot,const Blobs& blobs){
    DesyncCaptureDone d;d.key=req.key;d.connectionId=req.connections[slot];
    for(std::size_t i=0;i<4;++i){auto& a=d.artifacts[i];a.kind=static_cast<DesyncArtifactKind>(i);a.status=DesyncArtifactStatus::Complete;a.bytes=static_cast<std::uint32_t>(blobs[i].size());a.sha256=desyncSha256(blobs[i]);a.sourceBytes=a.bytes+17;a.rangeBegin=17;a.rangeEnd=a.sourceBytes;a.startedMs=11;a.finishedMs=12;a.truncated=true;a.sourceLabel="synthetic-owned-artifact";}return d;
}
void chunks(DesyncCapture& capture,const DesyncCaptureRequest& req,unsigned slot,const Blobs& blobs){
    for(std::size_t kind=0;kind<4;++kind)for(std::size_t off=0;off<blobs[kind].size();off+=DESYNC_CHUNK_BYTES){
        const auto end=std::min(off+DESYNC_CHUNK_BYTES,blobs[kind].size());DesyncArtifactChunk c{req.key,req.connections[slot],static_cast<DesyncArtifactKind>(kind),static_cast<std::uint32_t>(off),{blobs[kind].begin()+static_cast<std::ptrdiff_t>(off),blobs[kind].begin()+static_cast<std::ptrdiff_t>(end)}};
        check(capture.Chunk(req.connections[slot],c),"production assembler accepts bounded contiguous authenticated bytes");
    }
}
std::optional<DesyncCaptureRequest> trigger(DesyncCapture& c,const DesyncCaptureRequest& r){
    check(c.Trigger(r,"relay diagnostic witness\n",r.triggerMs),"capture trigger admitted");std::optional<DesyncCaptureRequest> out;
    check(until([&]{c.Pump(r.triggerMs);if(!out)out=c.TakeRequest();return out.has_value();}),"initial manifest persisted before request exposed");return out;
}
std::optional<DesyncCaptureResult> finalized(DesyncCapture& c,std::uint64_t now){std::optional<DesyncCaptureResult> r;check(until([&]{c.Pump(now);if(!r)r=c.TakeFinalized();return r.has_value();}),"bounded assembler finalization persists result");return r;}
void verifyFiles(const DesyncCaptureResult& result,const DesyncCaptureRequest& req,unsigned slot,const Blobs& b){
    const auto manifest=textFile(result.manifestPath);
    for(std::size_t kind=0;kind<4;++kind){const auto name="peer_"+std::to_string(slot)+"_"+std::to_string(req.connections[slot])+"_"+std::to_string(kind)+(kind==3?".png":".bin");auto saved=readFile(std::filesystem::path(result.directory)/name);
        check(saved==b[kind]&&desyncSha256(saved)==desyncSha256(b[kind])&&manifest.find(desyncDigestHex(desyncSha256(saved)))!=std::string::npos,"received artifact bytes/digest equal independent sender fixture");}
}
template<class T> void codecShape(const T& message){
    ByteWriter writer;write(writer,message);auto payload=writer.take();T decoded{};ByteReader good(payload);read(good,decoded);ByteWriter again;write(again,decoded);
    check(payload==again.data(),"diagnostic codec roundtrip preserves every serialized field");
    for(unsigned variant=0;variant<2;++variant){auto bad=payload;if(variant)bad.push_back(0xEE);else bad.pop_back();bool rejected=false;try{ByteReader r(bad);T out{};read(r,out);}catch(const std::exception&){rejected=true;}check(rejected,"diagnostic payload rejects truncated or trailing data");}
    for(unsigned variant=0;variant<2;++variant){auto frame=encode(message);if(variant)frame.push_back(0xEE);else frame.pop_back();bool rejected=false;try{const std::uint8_t* data=nullptr;std::size_t size=0;(void)decodePacketHeader(frame.data(),frame.size(),data,size);}catch(const std::exception&){rejected=true;}check(rejected,"diagnostic frame rejects short or extra bytes");}
}
void testAssembly(const std::filesystem::path& root){
    auto missing=request();missing.fields=DesyncEnemies|DesyncMissingEnemies;
    missing.hostHash.nativeCensusComplete=true;missing.hostHash.nativeLivingCount=5;missing.hostHash.nativeCombatCount=5;
    missing.clientHash.nativeCensusComplete=true;
    codecShape(missing);
    for(const auto fields:{0,16,26}) {
        auto invalid=missing;invalid.fields=static_cast<std::uint8_t>(fields);
        bool rejected=false;try{(void)encode(invalid);}catch(const std::exception&){rejected=true;}
        check(rejected,"capture request rejects zero or unknown diagnostic bits");
    }
    const std::vector<std::uint8_t> abc{'a','b','c'};
    check(desyncDigestHex(desyncSha256(abc))=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","production SHA256 matches independent standard abc vector");
    check(desyncDigestHex(desyncSha256({}))=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","SHA256 empty input matches independent standard vector");
    const std::string twoBlock="abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    const std::vector<std::uint8_t> twoBlockBytes(twoBlock.begin(),twoBlock.end());
    check(desyncDigestHex(desyncSha256(twoBlockBytes))=="248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1","SHA256 two-block padding matches independent standard vector");
    auto req=request();codecShape(req);codecShape(DesyncArtifactChunk{req.key,req.connections[0],DesyncArtifactKind::Metadata,0,{1,2,3}});codecShape(doneFor(req,0,bytesFor(0)));DesyncCapture complete((root/"complete").string());auto exposed=trigger(complete,req);
    check(exposed&&exposed->connections==req.connections&&exposed->clientReceiptSeq==2&&exposed->hostReceiptSeq==4,"frozen full-width roster and unequal receipt freshness retained");
    const auto initial=root/"complete"/req.key.sessionId/"1"/"manifest.json";
    check(textFile(initial).find("collecting")!=std::string::npos,"initial on-disk state explicitly collecting");
    for(unsigned slot=0;slot<3;++slot){auto b=bytesFor(slot);chunks(complete,req,slot,b);auto d=doneFor(req,slot,b);check(complete.Done(req.connections[slot],d),"complete per-peer descriptor accepted");}
    auto result=finalized(complete,1100);check(result&&result->status==DesyncCollectionStatus::Complete&&result->manifestWritten,"three complete contributors finalize one complete manifest");
    if(result){for(unsigned slot=0;slot<3;++slot)verifyFiles(*result,req,slot,bytesFor(slot));check(textFile(std::filesystem::path(result->directory)/"relay.log")=="relay diagnostic witness\n","relay diagnostic contribution persisted beside peer bytes");}
    auto replay=DesyncArtifactChunk{req.key,req.connections[0],DesyncArtifactKind::Metadata,0,{1}};
    check(!complete.Chunk(req.connections[0],replay),"late replay after completion rejected");
    auto soon=request(2);soon.triggerMs=1100;soon.deadlineMs=31100;check(!complete.Trigger(soon,"",1100)&&complete.Stats().suppressedCadence>0,"capture cadence does not restart on immediate new trigger");

    const DesyncArtifactChunk prefix{req.key,req.connections[1],DesyncArtifactKind::InjectLog,0,{1,2,3}};
    for(unsigned mode=0;mode<9;++mode){
        DesyncCapture n((root/("chunk-negative"+std::to_string(mode))).string());trigger(n,req);
        check(n.Chunk(req.connections[1],prefix),"positive sender bytes admitted before isolated invalid chunk");
        auto bad=prefix;std::uint64_t auth=req.connections[1];
        if(mode==0)auth=req.connections[0];else if(mode==1)bad.key.sessionId=std::string(32,'b');
        else if(mode==2)++bad.key.reportId;else if(mode==3){++bad.connectionId;auth=bad.connectionId;}
        else if(mode==4)bad.kind=static_cast<DesyncArtifactKind>(4);else if(mode==5)bad.bytes.resize(DESYNC_CHUNK_BYTES+1);
        else if(mode==6)bad.offset=DESYNC_PNG_BYTES;else if(mode==7)bad.bytes[0]=99;else bad.offset=4;
        check(!n.Chunk(auth,bad),"isolated wrong sender/world/report/replacement/kind/cap/offset/conflict/gap rejected");
        n.Interrupt("negative control complete");auto final=finalized(n,1200);
        if(final)check(readFile(std::filesystem::path(final->directory)/("peer_1_"+std::to_string(req.connections[1])+"_2.bin"))==prefix.bytes,"rejected data never overwrites positive received prefix");
    }
    DesyncCapture negative((root/"deadline").string());trigger(negative,req);
    check(negative.Chunk(req.connections[1],prefix)&&negative.Chunk(req.connections[1],prefix)&&negative.Stats().duplicateChunks==1,"exact chunk retry is idempotent and counted");
    auto lateTrigger=request(9);lateTrigger.triggerMs=30000;lateTrigger.deadlineMs=60000;check(!negative.Trigger(lateTrigger,"late mismatch",30000),"later active trigger coalesces without renewing original deadline");
    negative.Pump(30999);check(!negative.TakeFinalized(),"withheld peer remains pending before exact deadline");
    auto partial=finalized(negative,31000);check(partial&&partial->status==DesyncCollectionStatus::Partial,"withheld contributions finalize partial at exact deadline");
    if(partial){const auto saved=readFile(std::filesystem::path(partial->directory)/("peer_1_"+std::to_string(req.connections[1])+"_2.bin"));check(saved==prefix.bytes,"raw interrupted prefix remains unchanged in final artifact");}

    for(unsigned mode=0;mode<4;++mode){DesyncCapture bad((root/("bad-done"+std::to_string(mode))).string());trigger(bad,req);auto b=bytesFor(0);chunks(bad,req,0,b);auto d=doneFor(req,0,b);
        if(mode==0){auto expected=b;expected[2].push_back(77);d=doneFor(req,0,expected);}else if(mode==1)d.artifacts[2].sha256[0]^=1;else if(mode==2)d.artifacts[3].status=DesyncArtifactStatus::Unavailable;else d.artifacts[2].rangeEnd+=1;
        (void)bad.Done(req.connections[0],d);for(unsigned slot=1;slot<3;++slot){auto rest=bytesFor(slot);chunks(bad,req,slot,rest);(void)bad.Done(req.connections[slot],doneFor(req,slot,rest));}auto final=finalized(bad,31000);check(final&&final->status==DesyncCollectionStatus::Partial,"wrong total/hash/range or unavailable screenshot alone prevents otherwise complete report");}
    for(unsigned mode=0;mode<6;++mode){DesyncCapture brokenPng((root/("png-negative"+std::to_string(mode))).string());trigger(brokenPng,req);
        for(unsigned slot=0;slot<3;++slot){auto b=bytesFor(slot);if(slot==0){auto& image=b[3];
            if(mode==0)image.resize(8);else if(mode==1)image.resize(image.size()/2);else if(mode==2){
                const std::string hex="89504e470d0a1a0a0000000d4948445200000000000000010806000000f0d7afb70000000d49444154789c6360e03af11f0002b201d2d0a3f5f60000000049454e44ae426082";image.clear();for(std::size_t i=0;i<hex.size();i+=2)image.push_back(static_cast<std::uint8_t>(std::stoul(hex.substr(i,2),nullptr,16)));
            }else if(mode==3)image[29]^=1;else if(mode==4)image.erase(image.begin()+33,image.end()-12);else image.resize(image.size()-12);
        }chunks(brokenPng,req,slot,b);check(brokenPng.Done(req.connections[slot],doneFor(req,slot,b)),"PNG negative has exact transferred SHA and otherwise valid completion");}
        auto resultPng=finalized(brokenPng,1200);check(resultPng&&resultPng->status==DesyncCollectionStatus::Partial,"signature/truncation/zero-IHDR/bad-CRC/no-IDAT/no-IEND cannot certify complete screenshot");
    }
    for(const char* reason:{"host session ended","relay stopping"}){DesyncCapture stopped((root/reason).string());trigger(stopped,req);stopped.Interrupt(reason);auto final=finalized(stopped,1200);check(final&&final->status==DesyncCollectionStatus::Interrupted&&final->manifestWritten,"host-loss/relay-stop preserves interrupted manifest");}
    {
        DesyncCapture departed((root/"departed").string());trigger(departed,req);check(departed.Chunk(req.connections[1],prefix),"departure fixture has real received prefix");
        departed.PeerLeft(req.connections[1]);auto replacement=prefix;++replacement.connectionId;
        check(!departed.Chunk(replacement.connectionId,replacement),"replacement slot incarnation cannot complete original participant");auto gone=finalized(departed,1001);
        check(gone&&gone->status==DesyncCollectionStatus::Interrupted,"expected peer departure finalizes interrupted immediately, not at deadline");
    }
    {
        DesyncCapture limits((root/"limits").string());trigger(limits,req);
        for(unsigned i=0;i<DESYNC_EXTRA_TRIGGERS+2;++i){auto extra=req;extra.key.reportId=2+i;check(!limits.Trigger(extra,"",1000),"active trigger coalesced without second capture");}
        check(limits.Stats().extraTriggers==DESYNC_EXTRA_TRIGGERS+2&&limits.Stats().extraTriggerOverflow==2,"extra trigger list bounded with exact overflow count");limits.Interrupt("finish");(void)finalized(limits,1001);
        for(unsigned i=1;i<DESYNC_SESSION_QUOTA;++i){auto r=request(i+1);r.triggerMs=1000+DESYNC_CADENCE_MS*i;r.deadlineMs=r.triggerMs+DESYNC_DEADLINE_MS;trigger(limits,r);limits.Interrupt("quota fixture");(void)finalized(limits,r.triggerMs+1);}
        auto over=request(99);over.triggerMs=1000+DESYNC_CADENCE_MS*DESYNC_SESSION_QUOTA;over.deadlineMs=over.triggerMs+DESYNC_DEADLINE_MS;
        check(!limits.Trigger(over,"",over.triggerMs)&&limits.Stats().suppressedQuota==1,"ninth report in same world rejected by exact session quota");
    }
    {
        DesyncCapture cap((root/"aggregate-cap").string());trigger(cap,req);DesyncArtifactChunk c{req.key,req.connections[0],DesyncArtifactKind::InjectLog,0,std::vector<std::uint8_t>(DESYNC_CHUNK_BYTES,1)};
        for(unsigned offset=0;offset<DESYNC_LOG_BYTES;offset+=DESYNC_CHUNK_BYTES){c.offset=offset;check(cap.Chunk(c.connectionId,c),"aggregate log budget admits each bounded prefix chunk");}
        c.kind=DesyncArtifactKind::RuntimeLog;c.offset=0;c.bytes={1};check(!cap.Chunk(c.connectionId,c),"one additional byte across kinds exceeds aggregate log bound");cap.Interrupt("cap tested");(void)finalized(cap,1001);
    }
    const auto blocked=root/"not-a-directory";writeFile(blocked,{1,2,3});DesyncCapture broken(blocked.string());check(broken.Trigger(req,"relay",1000),"storage-error trigger remains observable");auto failed=finalized(broken,31000);check(failed&&failed->status==DesyncCollectionStatus::StorageError&&!failed->error.empty(),"output I/O failure surfaced, never fictional complete path");
}
void testSuppressionDurability(const std::filesystem::path& root){
    const auto req=request();DesyncCapture c(root.string());trigger(c,req);
    for(unsigned i=0;i<18;++i){auto extra=request(10+i);check(!c.Trigger(extra,"active",1000),"active report records bounded suppressed trigger witness");}
    for(unsigned slot=0;slot<3;++slot){auto b=bytesFor(slot);chunks(c,req,slot,b);check(c.Done(req.connections[slot],doneFor(req,slot,b)),"finishing fixture has every exact artifact");}
    c.Pump(1100); // Starts final persistence; finishing stays true until the NEXT Pump.
    for(unsigned i=0;i<2;++i){auto extra=request(40+i);extra.triggerMs=1100;extra.deadlineMs=31100;check(!c.Trigger(extra,"finishing",1100),"finishing-time suppressed trigger recorded without changing immutable snapshot");}
    auto first=finalized(c,1100);const auto firstBytes=first?readFile(first->manifestPath):std::vector<std::uint8_t>{};
    auto cadence=request(50);cadence.triggerMs=1101;cadence.deadlineMs=31101;check(!c.Trigger(cadence,"cadence",1101),"post-report cadence suppression occurs before any new capture");
    for(unsigned i=1;i<DESYNC_SESSION_QUOTA;++i){auto next=request(100+i);next.triggerMs=1000+DESYNC_CADENCE_MS*i;next.deadlineMs=next.triggerMs+DESYNC_DEADLINE_MS;trigger(c,next);c.Interrupt("quota setup");(void)finalized(c,next.triggerMs);}
    auto quota=request(999);quota.triggerMs=1000+DESYNC_CADENCE_MS*DESYNC_SESSION_QUOTA;quota.deadlineMs=quota.triggerMs+DESYNC_DEADLINE_MS;check(!c.Trigger(quota,"quota",quota.triggerMs),"quota overflow creates suppression evidence instead of a report");
    c.Shutdown();const auto summary=c.TakeSuppressionResult();const auto path=root/req.key.sessionId/"suppression-summary.json";const auto text=textFile(path);
    check(summary&&summary->written&&std::filesystem::path(summary->path)==path&&summary->revision==22,"shutdown flushes newest suppression revision through production writer");
    check(text.find("\"artifactType\":\"desync-suppression-summary\"")!=std::string::npos&&text.find("\"collectionStatus\":\"skipped\"")!=std::string::npos&&text.find("\"active\":18")!=std::string::npos&&text.find("\"finishing\":2")!=std::string::npos&&text.find("\"cadence\":1")!=std::string::npos&&text.find("\"quota\":1")!=std::string::npos&&text.find("\"witnessOverflow\":6")!=std::string::npos,"durable summary retains exact active/finishing/cadence/quota and lost-witness counts");
    std::size_t witnesses=0,pos=0;while((pos=text.find("\"reason\":",pos))!=std::string::npos){++witnesses;++pos;}
    check(witnesses==16&&text.find("\"lastKey\":{\"sessionId\":\""+req.key.sessionId+"\",\"reportId\":999}")!=std::string::npos,"only first sixteen suppression witnesses retained with exact latest identity");
    check(first&&readFile(first->manifestPath)==firstBytes,"late suppression does not rewrite finalized report or its completeness");
    {
        const auto brokenRoot=root/"storage-failure";DesyncCapture broken(brokenRoot.string());trigger(broken,req);broken.Interrupt("initial");(void)finalized(broken,1000);
        const auto blocked=brokenRoot/req.key.sessionId/"suppression-summary.json";writeFile(blocked/"keep.bin",{5,6});
        check(!broken.Trigger(cadence,"blocked summary",1101),"summary storage failure fixture suppresses a real trigger");broken.Shutdown();const auto failed=broken.TakeSuppressionResult();
        check(failed&&!failed->written&&!failed->error.empty()&&broken.Stats().summaryStorageErrors==1,"actual summary publish failure produces explicit unwritten result and exact loss counter");
        check(readFile(blocked/"keep.bin")==std::vector<std::uint8_t>({5,6}),"failed summary publication preserves blocking owned evidence");
        const auto errors=broken.Stats().summaryStorageErrors;broken.Pump(99999);broken.Shutdown();check(broken.Stats().summaryStorageErrors==errors,"storage failure does not create an unbounded retry loop");
    }
    {
        DesyncCapture backlog((root/"session-backlog").string());trigger(backlog,req);auto old=request(2);check(!backlog.Trigger(old,"old pending",1000),"first suppression snapshot pending before a session switch");
        auto other=request(3);other.key.sessionId=std::string(32,'b');check(!backlog.Trigger(other,"different session while dirty",1000),"different-world trigger cannot relabel pending summary");backlog.Shutdown();
        const auto saved=textFile(root/"session-backlog"/req.key.sessionId/"suppression-summary.json");check(backlog.Stats().summaryLostTriggers==1&&saved.find("\"lostTriggers\":1")!=std::string::npos&&saved.find("\"lastLostKey\":{\"sessionId\":\""+other.key.sessionId+"\",\"reportId\":3}")!=std::string::npos,"bounded summary backlog explicitly preserves lost foreign-world trigger identity");
    }
}
std::uint64_t nowMs(){return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());}
std::vector<std::uint8_t> png(unsigned tag){
    const std::array<std::string,3> hexes={"89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c4890000000d49444154789c6360e03af11f0002b201d2d0a3f5f60000000049454e44ae426082","89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c4890000000d49444154789c6370e33af11f0003ca02187c426e3b0000000049454e44ae426082","89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c4890000000d49444154789c63e8e13af11f0004e2025eb4fe45e40000000049454e44ae426082"};
    const auto& hex=hexes[tag%3];std::vector<std::uint8_t> out;
    for(std::size_t i=0;i<hex.size();i+=2){out.push_back(static_cast<std::uint8_t>(std::stoul(hex.substr(i,2),nullptr,16)));}return out;
}

class PngProvider final:public DesyncCaptureProvider {
    unsigned tag_;
public:
    explicit PngProvider(unsigned tag):tag_(tag){}
    DesyncCapturePoll Begin(std::uint32_t,const std::filesystem::path& path)override{writeFile(path,png(tag_));return {DesyncCaptureState::Complete,1,1,1,1,1,0,0,{}};}
    DesyncCapturePoll Poll()override{return {DesyncCaptureState::Complete,1,1,1,1,1,0,0,{}};}
    void Finish()noexcept override{}
};
void testUpload(){
    auto req=request();auto bytes=bytesFor(0);bytes[2].resize(200000,42);auto done=doneFor(req,0,bytes);
    DesyncUpload u;check(u.Begin(done,bytes,10000),"production uploader accepts copied bounded artifacts");unsigned sent=0,completions=0;std::size_t total=0;
    const auto chunk=[&](const DesyncArtifactChunk& c){++sent;total+=c.bytes.size();return c.bytes.size()<=DESYNC_CHUNK_BYTES;};
    const auto finished=[&](const DesyncCaptureDone&){++completions;return true;};
    u.Tick(100,req.key.sessionId,done.connectionId,true,chunk,finished);check(sent==4,"uploader emits at most four bounded chunks per tick");
    u.Tick(115,req.key.sessionId,done.connectionId,true,chunk,finished);check(sent==4,"no upload before exact sixteen-ms cadence");
    u.Tick(5000,req.key.sessionId,done.connectionId,true,chunk,finished);check(sent==8,"delayed tick does not accumulate burst credit");
    for(std::uint64_t now=5016;u.State()==DesyncUploadState::Sending&&now<6000;now+=16)u.Tick(now,req.key.sessionId,done.connectionId,true,chunk,finished);
    std::size_t expected=0;for(const auto& b:bytes)expected+=b.size();check(u.State()==DesyncUploadState::Complete&&completions==1&&total==expected,"all bytes sent exactly once before one completion");
    for(unsigned mode=0;mode<5;++mode){DesyncUpload invalid;check(invalid.Begin(done,bytes,200),"retirement uploader starts");unsigned calls=0;
        const auto send=[&](const auto&){++calls;return mode!=4;};auto session=req.key.sessionId;auto id=done.connectionId;bool admitted=true;std::uint64_t now=100;
        if(mode==0)session=std::string(32,'b');else if(mode==1)++id;else if(mode==2)admitted=false;else if(mode==3)now=200;
        auto state=invalid.Tick(now,session,id,admitted,send,send);check(state==(mode<3?DesyncUploadState::IdentityChanged:mode==3?DesyncUploadState::Deadline:DesyncUploadState::SendFailed)&&calls==(mode==4?1u:0u),"identity/admission/deadline/failed-send retire without later chunks");
    }
    DesyncUpload cancelled;cancelled.Begin(done,bytes,200);cancelled.Cancel();check(cancelled.State()==DesyncUploadState::Cancelled,"explicit shutdown cancels upload");
    auto excess=bytes;excess[2].resize(DESYNC_LOG_BYTES+1);auto oversized=doneFor(req,0,excess);DesyncUpload cap;check(!cap.Begin(oversized,excess,200)&&cap.State()==DesyncUploadState::InvalidArtifact,"aggregate log cap enforced before any upload");
}
void runNetwork(const std::filesystem::path& root){
    SessionConfig cfg;cfg.bindAddress="127.0.0.1";cfg.port=17810;cfg.gameBuild="desync-test";cfg.contentHash="desync-content";cfg.modHash="desync-mod";cfg.desyncOutputRoot=(root/"aggregate").string();
    SessionHost relay(cfg);check(relay.start(),"actual diagnostic relay starts loopback");if(!relay.isRunning())return;
    struct Peer{std::unique_ptr<NetworkClient> net;std::unique_ptr<DesyncCollector> collector;DesyncUpload upload;DesyncBinding binding;std::optional<DesyncCaptureRequest> request;Blobs expected;DesyncCaptureDone done;std::uint64_t deadline=0;unsigned requests=0;bool began=false;};
    std::array<Peer,3> peers;unsigned ticks=0;
    for(unsigned slot=0;slot<3;++slot){auto& p=peers[slot];const auto peerRoot=root/("local-"+std::to_string(slot));std::vector<std::uint8_t> log(40000,static_cast<std::uint8_t>('A'+slot));log.back()='\n';writeFile(peerRoot/"inject.log",log);
        DesyncCollectorOptions options;options.spoolRoot=peerRoot/"spool";options.injectLogPath=peerRoot/"inject.log";options.captureTimeoutMs=500;options.captureFactory=[slot]{return std::make_unique<PngProvider>(slot);};p.collector=std::make_unique<DesyncCollector>(options);
        ClientCallbacks cb;cb.onDesyncCaptureRequest=[&,slot](const DesyncCaptureRequest& r){auto& x=peers[slot];++x.requests;x.request=r;x.deadline=nowMs()+r.remainingMs;x.binding.sessionId=r.key.sessionId;x.binding.connections=r.connections;x.binding.localSlot=static_cast<std::uint8_t>(slot);x.binding.localGeneration=1;x.binding.attachedPid=50000+slot;x.binding.admitted=x.net->ready();x.binding.generationValid=true;
            check(x.collector->Start(r,x.binding,"owned metadata before "+std::to_string(slot),"runtime log peer "+std::to_string(slot)),"automatic request starts each peer's production collector");};
        p.net=std::make_unique<NetworkClient>("127.0.0.1",cfg.port,cfg.gameBuild,cfg.modHash,"capture-peer-"+std::to_string(slot),static_cast<SlotType>(slot),std::move(cb),RuntimeMode::CampaignCoop,cfg.contentHash);p.net->connect();
    }
    const auto pump=[&]{++ticks;relay.tick(0);for(unsigned slot=0;slot<3;++slot){auto& p=peers[slot];p.net->tick(0);p.net->sendHeartbeat();
        if(!p.request){continue;}p.collector->Tick(p.binding,"owned metadata after "+std::to_string(slot));
        if(!p.began){if(auto result=p.collector->TakeCompleted()){p.expected=result->bytes;p.done=result->done;p.began=true;check(p.upload.Begin(result->done,std::move(result->bytes),p.deadline),"immutable collector buffers enter actual production uploader");}}
        if(p.began)p.upload.Tick(nowMs(),p.binding.sessionId,p.binding.connections[slot],p.net->ready(),[&](const DesyncArtifactChunk& c){return p.net->sendDesyncArtifactChunk(c);},[&](const DesyncCaptureDone& d){return p.net->sendDesyncCaptureDone(d);});
    }};
    check(until([&]{return std::all_of(peers.begin(),peers.end(),[](const Peer& p){return p.net->worldReady();});},pump),"all three actual clients receive valid roster admission");
    const StateHash good{7,4,26,123,456},bad{7,4,26,124,456};peers[0].net->sendStateHash(good);peers[1].net->sendStateHash(good);
    check(until([&]{auto* a=relay.peerBySlot(SlotType::Player);auto* b=relay.peerBySlot(SlotType::Friend1);return a&&b&&a->hasHash&&b->hasHash;},pump),"host and agreeing peer hashes positively reach relay");
    peers[2].net->sendStateHash(bad);check(until([&]{auto* p=relay.peerBySlot(SlotType::Friend2);return p&&p->hashReceiptSeq!=0;},pump)&&relay.desyncNoticeCount()==0&&relay.desyncCaptureStats().started==0,"one positively received mismatch creates no notice or report");
    peers[2].net->sendStateHash(bad);check(until([&]{return std::all_of(peers.begin(),peers.end(),[](const Peer& p){return p.requests==1;});},pump),"persistent mismatch automatically requests all three including agreeing friend");
    peers[2].net->sendStateHash(bad);peers[2].net->sendStateHash(bad);
    check(until([&]{return relay.lastDesyncCapture().has_value();},pump),"actual remote contribution bytes assemble automatically without direct local path access");
    const auto result=relay.lastDesyncCapture();
    check(result&&result->status==DesyncCollectionStatus::Complete,"all authenticated contributions produce exact complete status");
    check(relay.desyncNoticeCount()==1&&relay.desyncCaptureStats().started==1&&std::all_of(peers.begin(),peers.end(),[](const Peer& p){return p.requests==1;}),"suppressed same desync does not duplicate capture requests");
    check(ticks>4&&relay.verifiedPeerCount()==3,"network/heartbeats continue throughout collection and upload pacing");
    if(result){
        for(unsigned slot=0;slot<3;++slot){const auto& p=peers[slot];if(p.request){verifyFiles(*result,*p.request,slot,p.expected);check(p.expected[3]==png(slot),"distinct peer PNG bytes cross authenticated transport");
            const auto& d=p.done.artifacts[2];const auto source=readFile(root/("local-"+std::to_string(slot))/"inject.log");check(d.rangeEnd<=source.size()&&d.rangeBegin<=d.rangeEnd&&std::vector<std::uint8_t>(source.begin()+static_cast<std::ptrdiff_t>(d.rangeBegin),source.begin()+static_cast<std::ptrdiff_t>(d.rangeEnd))==p.expected[2],"actual peer log range independently matches transferred source bytes");}}
        const auto manifest=textFile(result->manifestPath);check(manifest.find("\"hostReceiptSeq\":1")!=std::string::npos&&manifest.find("\"clientReceiptSeq\":2")!=std::string::npos&&manifest.find("\"enemiesHash\":123")!=std::string::npos&&manifest.find("\"enemiesHash\":124")!=std::string::npos,"trigger preserves exact unequal relay receipt counts and both compared hashes");
        check(!readFile(std::filesystem::path(result->directory)/"relay.log").empty(),"actual relay log included in same artifact");
    }
    for(auto& p:peers){p.net->disconnect();}relay.stop();
}
void testRawSender(const std::filesystem::path& root){
    SessionConfig cfg;cfg.bindAddress="127.0.0.1";cfg.port=17811;cfg.gameBuild="raw-desync";cfg.contentHash="none";cfg.modHash="";cfg.desyncOutputRoot=root.string();SessionHost relay(cfg);
    check(relay.start(),"raw authenticated sender relay starts");if(!relay.isRunning())return;
    std::optional<DesyncCaptureRequest> hostRequest,otherRequest,rawRequest;
    ClientCallbacks hc,oc;hc.onDesyncCaptureRequest=[&](const auto& r){hostRequest=r;};oc.onDesyncCaptureRequest=[&](const auto& r){otherRequest=r;};
    NetworkClient host("127.0.0.1",cfg.port,cfg.gameBuild,cfg.modHash,"raw-host",SlotType::Player,hc,RuntimeMode::CampaignCoop,cfg.contentHash);
    NetworkClient other("127.0.0.1",cfg.port,cfg.gameBuild,cfg.modHash,"raw-other",SlotType::Friend2,oc,RuntimeMode::CampaignCoop,cfg.contentHash);host.connect();other.connect();
    auto* endpoint=enet_host_create(nullptr,1,3,0,0);check(endpoint!=nullptr,"raw ENet endpoint created");if(!endpoint)return;ENetAddress address{};enet_address_set_host(&address,"127.0.0.1");address.port=cfg.port;
    auto* peer=enet_host_connect(endpoint,&address,3,0);check(peer!=nullptr,"raw endpoint connects three channels");if(!peer){enet_host_destroy(endpoint);return;}
    const auto send=[&](const std::vector<std::uint8_t>& packet,std::uint8_t channel){auto* p=enet_packet_create(packet.data(),packet.size(),ENET_PACKET_FLAG_RELIABLE);if(!p)return false;if(enet_peer_send(peer,channel,p)!=0){enet_packet_destroy(p);return false;}enet_host_flush(endpoint);return true;};
    const auto pump=[&]{relay.tick(0);host.tick(0);other.tick(0);host.sendHeartbeat();other.sendHeartbeat();ENetEvent e{};while(enet_host_service(endpoint,&e,0)>0){if(e.type==ENET_EVENT_TYPE_CONNECT){ClientHello hello;hello.protocolVersion=PROTOCOL_VERSION;hello.gameBuild=cfg.gameBuild;hello.contentHash=cfg.contentHash;hello.modHash=cfg.modHash;hello.peerId="raw-friend";hello.peerName="raw-friend";hello.requestedMode=RuntimeMode::CampaignCoop;hello.requestedSlot=1;send(encode(hello),0);}else if(e.type==ENET_EVENT_TYPE_RECEIVE){try{const std::uint8_t* payload=nullptr;std::size_t size=0;auto type=decodePacketHeader(e.packet->data,e.packet->dataLength,payload,size);if(type==PacketType::DesyncCaptureRequest){ByteReader reader(payload,size);DesyncCaptureRequest r;read(reader,r);rawRequest=r;}}catch(const std::exception&){check(false,"raw endpoint decodes valid relay packet");}enet_packet_destroy(e.packet);}}};
    check(until([&]{return host.worldReady()&&other.worldReady()&&relay.verifiedPeerCount()==3;},pump),"raw peer authenticated into real three-member roster");
    host.sendStateHash(StateHash{7,4,26,100,200});check(until([&]{auto* p=relay.peerBySlot(SlotType::Player);return p&&p->hasHash;},pump),"raw sender fixture host hash received");
    other.sendStateHash(StateHash{7,4,26,101,200});check(until([&]{auto* p=relay.peerBySlot(SlotType::Friend2);return p&&p->hashReceiptSeq==1;},pump),"raw sender fixture first mismatch received");other.sendStateHash(StateHash{7,4,26,101,200});
    const bool requested=until([&]{return hostRequest&&otherRequest&&rawRequest;},pump);check(requested,"raw endpoint receives frozen diagnostic request");
    if(requested){const auto& r=*rawRequest;DesyncArtifactChunk prefix{r.key,r.connections[1],DesyncArtifactKind::Metadata,0,{41,42,43}};
        check(send(encode(prefix),2)&&send(encode(prefix),2),"actual endpoint dispatches authenticated prefix and exact duplicate");
        check(until([&]{return relay.desyncCaptureStats().duplicateChunks==1;},pump),"server duplicate acknowledgement counter proves actual channel-two receipt");
        auto forged=prefix;forged.connectionId=r.connections[0];const auto before=relay.desyncCaptureStats().rejected;check(send(encode(forged),2),"forged claimed host identity physically dispatched by friend endpoint");
        check(until([&]{return relay.desyncCaptureStats().rejected==before+1;},pump),"relay authenticates ENet sender and rejects another connection identity");
        for(unsigned slot:{0u,2u}){DesyncCaptureDone done;done.key=r.key;done.connectionId=r.connections[slot];for(unsigned k=0;k<4;++k){done.artifacts[k].kind=static_cast<DesyncArtifactKind>(k);done.artifacts[k].sha256=desyncSha256({});}check((slot==0?host:other).sendDesyncCaptureDone(done),"other actual endpoint publishes explicit unavailable completion");}
        check(until([&]{return relay.lastDesyncCapture().has_value();},pump),"raw sender partial report finalizes after exact participant results");if(auto result=relay.lastDesyncCapture()){check(result->status==DesyncCollectionStatus::Partial,"forged contribution never certifies complete");check(readFile(std::filesystem::path(result->directory)/("peer_1_"+std::to_string(r.connections[1])+"_0.bin"))==prefix.bytes,"actual authenticated prefix retained without forged overwrite");}
    }
    enet_peer_disconnect_now(peer,0);enet_host_destroy(endpoint);host.disconnect();other.disconnect();relay.stop();
}
void testRequestBeforeRoster(){
    ENetAddress address{};enet_address_set_host(&address,"127.0.0.1");address.port=17812;
    auto* server=enet_host_create(&address,1,3,0,0);check(server!=nullptr,"synthetic three-channel server starts for request/roster race");if(!server)return;
    for(unsigned variant=0;variant<3;++variant){ENetPeer* peer=nullptr;std::uint64_t lastPing=0;unsigned requests=0;std::uint32_t remaining=0;
        ClientCallbacks callbacks;callbacks.onDesyncCaptureRequest=[&](const DesyncCaptureRequest& r){++requests;remaining=r.remainingMs;};
        NetworkClient client("127.0.0.1",address.port,"race-build","race-mod","race-self",SlotType::Friend1,callbacks,RuntimeMode::CampaignCoop,"none");client.connect();
        const auto pump=[&]{ENetEvent e{};while(enet_host_service(server,&e,0)>0){if(e.type==ENET_EVENT_TYPE_CONNECT)peer=e.peer;else if(e.type==ENET_EVENT_TYPE_RECEIVE){try{const std::uint8_t* payload=nullptr;std::size_t n=0;if(decodePacketHeader(e.packet->data,e.packet->dataLength,payload,n)==PacketType::ClockPing){ByteReader reader(payload,n);ClockPing ping;read(reader,ping);lastPing=ping.clientSendMs;}}catch(const std::exception&){}enet_packet_destroy(e.packet);}}client.tick(0);};
        const auto send=[&](const std::vector<std::uint8_t>& bytes,std::uint8_t channel){if(!peer)return false;auto* packet=enet_packet_create(bytes.data(),bytes.size(),ENET_PACKET_FLAG_RELIABLE);if(!packet)return false;if(enet_peer_send(peer,channel,packet)!=0){enet_packet_destroy(packet);return false;}enet_host_flush(server);return true;};
        check(until([&]{return peer&&client.isConnected()&&lastPing;},pump),"pre-roster diagnostic client is connected with actual clock ping");
        auto r=request();r.connections[2]=0;r.divergedSlot=1;r.remainingMs=variant?200:2000;
        check(send(encode(r),2)&&send(encode(ClockPong{lastPing,nowMs()}),2),"request then positive clock barrier sent on same diagnostic channel before roster");
        check(until([&]{return client.hasClockSync();},pump)&&requests==0&&!client.ready(),"request reached client boundary but did not callback before identity admission");
        const auto barrierTime=nowMs();const auto holdUntil=barrierTime+(variant==1?150:25);while(nowMs()<holdUntil){pump();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        if(variant==1){check(send(encode(r),2),"duplicate pending request physically queued before old expiry");while(nowMs()<barrierTime+250){pump();std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
        if(variant==1){check(send(encode(r),2),"same request replayed AFTER original expiry before ordered roster barrier");}
        SessionState roster;roster.sessionId=r.key.sessionId;roster.gameBuild="race-build";roster.modHash="race-mod";
        SessionActor host;host.slot=SlotType::Player;host.ownerPeerId="race-host";host.connectionId=r.connections[0];
        SessionActor self;self.slot=SlotType::Friend1;self.ownerPeerId="race-self";self.connectionId=r.connections[1];
        if(variant==2){++self.connectionId;}roster.actors={host,self};
        check(send(encode(roster),variant==1?2:0)&&until([&]{return client.ready();},pump),"actual later roster admitted independently from diagnostic request");
        if(!variant){check(until([&]{return requests==1;},pump)&&remaining>0&&remaining<2000,"matching roster releases exactly one request with reduced original remaining lifetime");}
        else{check(requests==0,"expired duplicate or mismatched current roster cannot authorize pending request");if(variant==2){while(nowMs()<barrierTime+250){pump();std::this_thread::sleep_for(std::chrono::milliseconds(1));}check(requests==0,"wrong replacement remains unmatched through original expiry");}auto fresh=r;fresh.key.reportId=2;fresh.connections[1]=self.connectionId;fresh.remainingMs=2000;check(send(encode(fresh),2)&&until([&]{return requests==1;},pump),"fresh matching request after expiry remains usable positive control");}
        client.disconnect();const auto settle=nowMs()+15;while(nowMs()<settle){pump();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    }
    enet_host_destroy(server);
}
void testProtocol5Refusal(){
    SessionConfig cfg;cfg.bindAddress="127.0.0.1";cfg.port=17813;cfg.gameBuild="desync-gate";cfg.modHash="m";cfg.contentHash="c";SessionHost relay(cfg);
    check(relay.start(),"protocol-ten diagnostic gate relay starts with free capacity");if(!relay.isRunning())return;
    std::vector<ClientCloseInfo> closed;std::string reason;unsigned requests=0;ClientCallbacks callbacks;
    callbacks.onClosed=[&](const ClientCloseInfo& info){closed.push_back(info);};callbacks.onRejected=[&](const HelloReject& rejection){reason=rejection.reason;};callbacks.onDesyncCaptureRequest=[&](const auto&){++requests;};
    NetworkClient old("127.0.0.1",cfg.port,cfg.gameBuild,cfg.modHash,"old-v5",SlotType::Friend1,callbacks,RuntimeMode::CampaignCoop,cfg.contentHash,5);
    old.connect();check(until([&]{return !closed.empty();},[&]{relay.tick(0);old.tick(0);}),"actual version-five endpoint receives protocol refusal");
    check(PROTOCOL_VERSION>5&&closed.size()==1&&closed[0].reason==DisconnectReason::Incompatible&&reason=="Protocol mismatch: client=5 server="+std::to_string(PROTOCOL_VERSION)&&!old.ready()&&relay.verifiedPeerCount()==0&&requests==0,"v5 cannot advertise missing three-channel artifact semantics despite free capacity");
    old.disconnect();relay.stop();
}
void testNetwork(const std::filesystem::path& root){testUpload();testProtocol5Refusal();testRequestBeforeRoster();runNetwork(root/"complete");testRawSender(root/"forged");}
}
int main(){
    const auto root=std::filesystem::temp_directory_path()/("kh2-desync-capture-test-"+std::to_string(Clock::now().time_since_epoch().count()));std::filesystem::create_directories(root);
    testAssembly(root);testSuppressionDurability(root/"suppression");if(enet_initialize()!=0){check(false,"ENet initialization");}else{testNetwork(root/"network");enet_deinitialize();}
    std::cout<<checks-failures<<" PASS, "<<failures<<" FAIL; artifacts "<<root.string()<<'\n';return failures?1:0;
}
