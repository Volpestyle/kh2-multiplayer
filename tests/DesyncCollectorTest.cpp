// Production collector/lease controls with owned files and synthetic mailboxes.
// No KH2, renderer, production EntityHook or native gameplay proof.
#include "kh2coop/DesyncCollector.hpp"
#include "kh2coop/Codec.hpp"
#include "kh2coop/CaptureChannel.hpp"
#include "kh2coop/CaptureLease.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <thread>
using namespace kh2coop;
namespace {
int checks=0, failures=0;
void check(bool good,const char* label) { ++checks; if(!good) ++failures; std::cout<<(good?"PASS: ":"FAIL: ")<<label<<'\n'; }
using Clock=std::chrono::steady_clock;
bool until(const std::function<bool()>& done) {
    const auto end=Clock::now()+std::chrono::seconds(5);
    while(!done() && Clock::now()<end) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return done();
}
std::vector<std::uint8_t> png() {
    const std::string hex="89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c4890000000d49444154789c6360e03af11f0002b201d2d0a3f5f60000000049454e44ae426082";
    std::vector<std::uint8_t> bytes;
    for(std::size_t i=0;i<hex.size();i+=2) bytes.push_back(static_cast<std::uint8_t>(std::stoul(hex.substr(i,2),nullptr,16)));
    return bytes;
}
void writeFile(const std::filesystem::path& path,const std::vector<std::uint8_t>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path,std::ios::binary); file.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
}
struct ProviderState { DesyncCapturePoll poll; bool write=true; std::size_t extra=0; std::atomic<unsigned> begins{0},finishes{0}; };
class Provider final: public DesyncCaptureProvider {
    std::shared_ptr<ProviderState> state_;
public:
    explicit Provider(std::shared_ptr<ProviderState> s):state_(std::move(s)){}
    DesyncCapturePoll Begin(std::uint32_t,const std::filesystem::path& output) override {
        ++state_->begins;
        if(state_->write) { auto bytes=png(); bytes.resize(bytes.size()+state_->extra); writeFile(output,bytes); }
        auto initial=state_->poll; initial.state=DesyncCaptureState::Pending; initial.requestSeq=11; initial.doneSeq=0; return initial;
    }
    DesyncCapturePoll Poll() override { return state_->poll; }
    void Finish() noexcept override { ++state_->finishes; }
};
DesyncCaptureRequest request() {
    DesyncCaptureRequest r; r.key={std::string(32,'a'),1}; r.connections={0x100000001ULL,0x200000002ULL,0x300000003ULL};
    r.epoch=9; r.divergedSlot=1; r.fields=2; r.hostHash={9,4,26,12,13}; r.clientHash={9,4,26,14,13};
    r.hostReceiptSeq=2;r.clientReceiptSeq=3;r.comparisonSeq=4;r.triggerMs=100;r.deadlineMs=30100;return r;
}
DesyncBinding binding() { auto r=request(); DesyncBinding b; b.sessionId=r.key.sessionId;b.connections=r.connections;b.localSlot=1;b.localGeneration=7;b.attachedPid=12345;b.admitted=true;b.generationValid=true;return b; }
std::shared_ptr<ProviderState> success() { auto s=std::make_shared<ProviderState>();s->poll={DesyncCaptureState::Complete,11,11,1,1,1,0,0,{}};s->poll.mailboxAvailable=true;s->poll.submissionAvailable=true;s->poll.submitted=true;s->poll.rendererAvailable=true;s->poll.renderer=12;s->poll.backbufferFormat=28;return s; }
std::unique_ptr<DesyncCollected> collect(const std::filesystem::path& root,std::shared_ptr<ProviderState> state,
                                       const std::filesystem::path& log, const char* runtime="runtime tail\n") {
    DesyncCollectorOptions options; options.spoolRoot=root;options.injectLogPath=log;options.captureTimeoutMs=500;
    options.captureFactory=[state] {return std::make_unique<Provider>(state);};
    DesyncCollector collector(options);const auto b=binding();
    check(collector.Start(request(),b,"before metadata",runtime),"collector accepts exact admitted frozen binding");
    std::unique_ptr<DesyncCollected> result;
    check(until([&] {collector.Tick(b,"after metadata");if(!result)result=collector.TakeCompleted();return result!=nullptr;}),"bounded production worker yields a result while owner keeps ticking");
    bool encodable=false;try {if(result){auto packet=encode(result->done);encodable=!packet.empty();}}catch(const std::exception&){}
    check(encodable,"every successful or failed local result remains wire-encodable");
    return result;
}
std::string attemptLine(const DesyncCollected& result){const std::string metadata(result.bytes[0].begin(),result.bytes[0].end());const auto pos=metadata.find("captureAttempt schema=1 ");return pos==std::string::npos?std::string{}:metadata.substr(pos,metadata.find('\n',pos)-pos);}
std::optional<std::uint64_t> field(const std::string& line,const std::string& key){const auto pos=line.find(" "+key+"=");if(pos==std::string::npos)return {};try{return std::stoull(line.substr(pos+key.size()+2));}catch(const std::exception&){return {};}}
void testCollector(const std::filesystem::path& root) {
    const auto log=root/"source.log";std::vector<std::uint8_t> source(400000,'L');source.back()='X';writeFile(log,source);
    auto state=success();auto good=collect(root/"positive",state,log);
    check(good && good->done.connectionId==binding().connections[1] && good->before==good->after,"full-width connection/binding retained without relabel");
    if(good) {
        bool valid=true;
        for(std::size_t i=0;i<4;++i) {const auto& d=good->done.artifacts[i];valid=valid&&d.status==DesyncArtifactStatus::Complete&&d.bytes==good->bytes[i].size()&&d.sha256==desyncSha256(good->bytes[i]);}
        check(valid,"all four copied artifacts have independently recomputed byte counts/digests");
        const auto& tail=good->done.artifacts[2];
        check(tail.sourceBytes==source.size() && tail.rangeEnd==source.size() && tail.rangeEnd-tail.rangeBegin==good->bytes[2].size() &&
              std::equal(good->bytes[2].begin(),good->bytes[2].end(),source.begin()+static_cast<std::ptrdiff_t>(tail.rangeBegin)),"actual inject-log byte range matches source including unterminated tail");
        check(good->bytes[3]==png(),"screenshot is actual provider-written PNG bytes, not local path");
        const auto attempt=attemptLine(*good);
        bool flags=true;for(const char* name:{"providerAttempted","providerAvailable","mailboxAvailable","submissionAvailable","submitted","expectedSequenceAvailable","pollAvailable","completionAvailable","rendererAvailable","pathAvailable"})flags=flags&&field(attempt,name)==1;
        check(flags&&field(attempt,"expectedSequence")==11&&field(attempt,"requestSeq")==11&&field(attempt,"doneSeq")==11&&field(attempt,"nativeStatus")==0&&field(attempt,"framesWritten")==1&&field(attempt,"width")==1&&field(attempt,"height")==1&&field(attempt,"renderer")==12&&field(attempt,"backbufferFormat")==28&&attempt.find("relativePath=\"screenshot.png\"")!=std::string::npos,"metadata preserves exact provider/mailbox/submission/completion/renderer witness fields");
        const auto started=field(attempt,"startedMs"),submitted=field(attempt,"submittedObservedMs"),polled=field(attempt,"lastPollMs"),finished=field(attempt,"finishedMs");
        check(started&&submitted&&polled&&finished&&*started>0&&*started<=*submitted&&*submitted<=*polled&&*polled<=*finished,"attempt timestamps describe ordered observation interval, not trigger-frame identity");
        const auto& metadata=good->done.artifacts[0];check(metadata.sourceBytes==good->bytes[0].size()&&metadata.rangeBegin==0&&metadata.rangeEnd==metadata.sourceBytes&&!metadata.truncated,"ordinary composed metadata reports its actual untruncated byte extent");

    }
    check(state->begins==1&&state->finishes==1,"provider begins/finishes exactly once");
    {
        auto unknown=success();unknown->poll.mailboxAvailable=false;unknown->poll.submissionAvailable=false;unknown->poll.submitted=false;unknown->poll.rendererAvailable=false;
        auto r=collect(root/"unknown-optionals",unknown,log);if(r){const auto a=attemptLine(*r);check(field(a,"mailboxAvailable")==0&&field(a,"submissionAvailable")==0&&field(a,"rendererAvailable")==0,"legacy provider optional fields stay unavailable instead of inferred from returned PNG");}
    }
    auto missing=collect(root/"missing-log",success(),root/"does-not-exist");
    check(missing&&missing->done.artifacts[2].status!=DesyncArtifactStatus::Complete&&missing->bytes[2].empty(),"absent inject log explicit unavailable/error, never empty complete");
    auto empty=collect(root/"missing-runtime",success(),log,"");
    check(empty&&empty->done.artifacts[1].status==DesyncArtifactStatus::Unavailable,"absent runtime ring is explicit unavailable");
    const auto negative=[&](const char* label,const std::function<void(ProviderState&)>& change) {
        auto s=success();change(*s);auto result=collect(root/label,s,log);
        check(result&&result->done.artifacts[3].status!=DesyncArtifactStatus::Complete,label);
        if(result){const auto a=attemptLine(*result);check(field(a,"providerAttempted")==1&&field(a,"pollAvailable")==1,"failed screenshot retains attempted provider/poll evidence instead of disappearing");}

    };
    negative("no-present-timeout",[](auto& s){s.poll.state=DesyncCaptureState::Pending;});
    negative("mismatched-done-sequence",[](auto& s){s.poll.doneSeq=10;});
    negative("changed-request-sequence",[](auto& s){s.poll.requestSeq=12;s.poll.doneSeq=12;});
    negative("native-write-error",[](auto& s){s.poll.nativeStatus=4;});
    negative("zero-dimensions",[](auto& s){s.poll.width=0;});
    negative("multiple-frames",[](auto& s){s.poll.framesWritten=2;});
    negative("missing-PNG",[](auto& s){s.write=false;});
    negative("oversize-PNG",[](auto& s){s.extra=DESYNC_PNG_BYTES;});
    negative("provider-busy",[](auto& s){s.poll.state=DesyncCaptureState::Busy;});
    negative("provider-abandoned",[](auto& s){s.poll.state=DesyncCaptureState::Abandoned;});
    {
        auto s=success();DesyncCollectorOptions o;o.spoolRoot=root/"unattached";o.injectLogPath=log;o.captureTimeoutMs=500;o.captureFactory=[s]{return std::make_unique<Provider>(s);};
        DesyncCollector c(o);auto b=binding();b.attachedPid=0;b.localGeneration=0;b.generationValid=false;
        check(c.Start(request(),b,"before","runtime"),"admitted unattached peer still starts log collection");std::unique_ptr<DesyncCollected> r;
        check(until([&]{c.Tick(b,"after");if(!r)r=c.TakeCompleted();return bool(r);}),"unattached peer yields explicit partial result");
        check(r&&r->done.artifacts[1].status==DesyncArtifactStatus::Complete&&r->done.artifacts[2].status==DesyncArtifactStatus::Complete&&r->done.artifacts[3].status==DesyncArtifactStatus::Unavailable&&s->begins==0,"unattached logs retained without making any capture request");
        if(r){const auto a=attemptLine(*r);check(field(a,"providerAttempted")==0&&field(a,"submitted")==0&&field(a,"completionAvailable")==0,"unattached metadata explicitly records no capture submission or completion");}
        bool encoded=false;try{if(r)encoded=!encode(r->done).empty();}catch(const std::exception&){}check(encoded,"unattached partial descriptors can cross real protocol");
    }
    {
        auto state=success();DesyncCollectorOptions options;options.spoolRoot=root/"large-metadata";
        options.injectLogPath=root/std::string(90000,'"');options.captureTimeoutMs=1000;options.captureFactory=[state]{return std::make_unique<Provider>(state);};
        DesyncCollector c(options);const auto b=binding();const std::string before(100000,'B'),after(120000,'A');
        check(c.Start(request(),b,before,"runtime"),"large metadata fixture starts with explicitly invalid owned log path");std::unique_ptr<DesyncCollected> result;
        check(until([&]{c.Tick(b,after);if(!result)result=c.TakeCompleted();return bool(result);}),"large metadata/error fixture yields bounded result");
        if(result){const auto& d=result->done.artifacts[0];const std::string text(result->bytes[0].begin(),result->bytes[0].end());
            check(d.sourceBytes>160u*1024u&&d.truncated&&d.bytes==160u*1024u&&d.rangeBegin==0&&d.rangeEnd==d.bytes&&d.sha256==desyncSha256(result->bytes[0]),"composed metadata sourceBytes precedes160KiB cap while ranges/digest cover only copied bytes");
            check(text.find("metadataBeforeSourceBytes=100000")!=std::string::npos&&text.find("metadataAfterSourceBytes=120000")!=std::string::npos&&text.find("metadataBeforeTruncated=1")!=std::string::npos&&text.find("metadataAfterTruncated=1")!=std::string::npos,"input metadata clamps remain independently explicit from composed output truncation");
            bool encoded=false;try{encoded=!encode(result->done).empty();}catch(const std::exception&){}check(encoded,"truncated composed metadata remains wire encodable");}
    }
    {
        const auto blocked=root/"spool-is-file";writeFile(blocked,{1,2,3});auto r=collect(blocked,success(),log);
        check(r&&std::any_of(r->done.artifacts.begin(),r->done.artifacts.end(),[](const auto& d){return d.status==DesyncArtifactStatus::ReadError;}),"real spool filesystem error remains explicit partial evidence");
    }
#ifdef _WIN32
    {
        HANDLE locked=CreateFileW(log.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        check(locked!=INVALID_HANDLE_VALUE,"owned log exclusively opened for actual read-denial control");
        auto r=collect(root/"sharing-denied",success(),log);
        check(r&&r->done.artifacts[2].status==DesyncArtifactStatus::ReadError,"actual sharing-denied log read returns explicit error, no fabricated empty success");
        if(locked!=INVALID_HANDLE_VALUE)CloseHandle(locked);
    }
#endif
    for(unsigned change=0;change<4;++change) {
        auto s=success();s->poll.state=DesyncCaptureState::Pending;
        DesyncCollectorOptions o;o.spoolRoot=root/("retire"+std::to_string(change));o.injectLogPath=log;o.captureTimeoutMs=10;o.captureFactory=[s]{return std::make_unique<Provider>(s);};
        DesyncCollector c(o);auto b=binding();check(c.Start(request(),b,"before","runtime"),"retirement fixture starts");
        check(until([&]{return s->begins.load()!=0;}),"worker starts before retirement");
        if(change==0)++b.localGeneration;else if(change==1)++b.attachedPid;else if(change==2)++b.connections[1];else c.Cancel();
        check(until([&]{c.Tick(b,"changed");return !c.Busy();}),"retired collector completes bounded local work");
        check(!c.TakeCompleted(),"changed PID/generation/incarnation/cancel cannot publish old bytes as current contribution");
    }
}
#ifdef _WIN32
void testLeaseAndMailbox(const std::filesystem::path& root) {
    const auto pid=GetCurrentProcessId()+0x40000000u;CaptureLease first;
    check(first.Acquire(pid)==CaptureLeaseStatus::Acquired,"first production named lease acquired");
    CaptureLeaseStatus secondStatus=CaptureLeaseStatus::Error;
    std::thread competing([&]{CaptureLease second;secondStatus=second.Acquire(pid);});competing.join();
    check(secondStatus==CaptureLeaseStatus::Busy,"different thread competing named caller is busy without waiting");
    first.Release();CaptureLease next;check(next.Acquire(pid)==CaptureLeaseStatus::Acquired,"release permits next caller");next.Release();
    const auto name=L"Local\\kh2coop_capture_caller_"+std::to_wstring(pid);
    HANDLE keeper=CreateMutexW(nullptr,FALSE,name.c_str());
    std::thread abandon([&]{WaitForSingleObject(keeper,INFINITE);});abandon.join();
    CaptureLease abandoned;check(abandoned.Acquire(pid)==CaptureLeaseStatus::Abandoned,"abandoned OS mutex ownership is explicit and not usable");
    check(abandoned.Acquire(pid)==CaptureLeaseStatus::Acquired,"abandoned result releases ownership for later clean acquisition");abandoned.Release();CloseHandle(keeper);
    const auto mappingName=std::wstring(CAPTURE_NAME_PREFIX)+std::to_wstring(pid);
    HANDLE mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,static_cast<DWORD>(sizeof(CaptureChannel)),mappingName.c_str());
    auto* channel=static_cast<CaptureChannel*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(CaptureChannel)));
    check(channel!=nullptr,"owned synthetic versioned capture mailbox mapped");if(!channel){if(mapping)CloseHandle(mapping);return;}
    *channel={};channel->magic=CAPTURE_MAGIC;channel->version=CAPTURE_VERSION;
    auto provider=MakeDesyncCaptureProvider();const auto output=root/"mailbox.png";
    auto start=provider->Begin(pid,output);const auto sequence=channel->requestSeq;
    check(start.state==DesyncCaptureState::Pending&&sequence!=0&&channel->frameCount==1&&channel->frameInterval==1,"real adapter submits one frame to owned mailbox");
    provider->Finish();auto retry=MakeDesyncCaptureProvider();auto busy=retry->Begin(pid,root/"must-not-overwrite.png");
    check(busy.state==DesyncCaptureState::Busy&&channel->requestSeq==sequence&&std::filesystem::path(channel->output)==output,"timeout/finish does not overwrite outstanding mailbox request");retry->Finish();
    channel->status=0;channel->framesWritten=1;channel->width=1;channel->height=1;InterlockedExchange(&channel->doneSeq,sequence);
    auto fresh=MakeDesyncCaptureProvider();auto freshStart=fresh->Begin(pid,root/"next.png");
    check(freshStart.state==DesyncCaptureState::Pending&&channel->requestSeq!=sequence,"completed old request permits a new caller request");
    InterlockedIncrement(&channel->requestSeq);
    check(fresh->Poll().state!=DesyncCaptureState::Complete,"changed sequence is never a complete owned capture");fresh->Finish();
    channel->version=999;auto invalid=MakeDesyncCaptureProvider();check(invalid->Begin(pid,root/"invalid.png").state!=DesyncCaptureState::Pending,"wrong capture schema cannot submit");invalid->Finish();
    UnmapViewOfFile(channel);CloseHandle(mapping);
}
#endif
}
int main() {
    const auto stamp=Clock::now().time_since_epoch().count();const auto root=std::filesystem::temp_directory_path()/("kh2-desync-collector-test-"+std::to_string(stamp));
    std::filesystem::create_directories(root);testCollector(root);
#ifdef _WIN32
    testLeaseAndMailbox(root);
#endif
    std::cout<<checks-failures<<" PASS, "<<failures<<" FAIL; artifacts "<<root.string()<<'\n';return failures?1:0;
}
