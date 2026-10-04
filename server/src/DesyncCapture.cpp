#include "kh2coop/DesyncCapture.hpp"
#include "kh2coop/Codec.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <utility>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace kh2coop {
namespace {
std::string quoted(const std::string& s) {
    std::string out="\"";constexpr char hex[]="0123456789abcdef";
    for(unsigned char c:s) {
        if(c=='"'||c=='\\'){out+='\\';out+=static_cast<char>(c);}
        else if(c<32 || c>=127){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}
        else out+=static_cast<char>(c);
    }
    return out+'"';
}
const char* statusName(DesyncCollectionStatus s) {
    switch(s){case DesyncCollectionStatus::Complete:return "complete";case DesyncCollectionStatus::Partial:return "partial";
        case DesyncCollectionStatus::Interrupted:return "interrupted";case DesyncCollectionStatus::StorageError:return "storage-error";}
    return "partial";
}
const char* artifactStatus(DesyncArtifactStatus s) {
    switch(s){case DesyncArtifactStatus::Complete:return "complete";case DesyncArtifactStatus::Unavailable:return "unavailable";
        case DesyncArtifactStatus::ReadError:return "read-error";case DesyncArtifactStatus::Timeout:return "timeout";
        case DesyncArtifactStatus::Interrupted:return "interrupted";case DesyncArtifactStatus::Oversize:return "oversize";}
    return "unavailable";
}
void hashJson(std::ostream& out,const StateHash& h) {
    out<<"{\"epoch\":"<<h.epoch<<",\"worldId\":"<<h.worldId<<",\"roomId\":"<<h.roomId
       <<",\"enemiesHash\":"<<h.enemiesHash<<",\"progressHash\":"<<h.progressHash<<'}';
}
void triggerJson(std::ostream& out,const DesyncCaptureRequest& r) {
    out<<"{\"connections\":["<<r.connections[0]<<','<<r.connections[1]<<','<<r.connections[2]<<"],\"epoch\":"<<r.epoch
       <<",\"divergedSlot\":"<<static_cast<unsigned>(r.divergedSlot)<<",\"fields\":"<<static_cast<unsigned>(r.fields)<<",\"hostHash\":";
    hashJson(out,r.hostHash);out<<",\"clientHash\":";hashJson(out,r.clientHash);
    out<<",\"hostReceiptSeq\":"<<r.hostReceiptSeq<<",\"clientReceiptSeq\":"<<r.clientReceiptSeq
       <<",\"hostReceiptMs\":"<<r.hostReceiptMs<<",\"clientReceiptMs\":"<<r.clientReceiptMs
       <<",\"comparisonSeq\":"<<r.comparisonSeq<<",\"triggerMs\":"<<r.triggerMs
       <<",\"deadlineMs\":"<<r.deadlineMs<<",\"remainingMs\":"<<r.remainingMs<<'}';
}
struct Artifact {
    std::vector<std::uint8_t> bytes;
    std::map<std::uint32_t,std::uint32_t> chunks;
    bool written=false;
};
struct Participant {
    bool done=false,failed=false;
    std::string error;
    std::optional<DesyncCaptureDone> completion;
    std::array<Artifact,4> artifacts;
};
struct Report {
    DesyncCaptureRequest request;
    std::vector<DesyncCaptureRequest> extras;
    std::array<Participant,3> peers;
    std::string directory,relayLog,error;
    std::uint64_t relayLogSourceBytes=0;
    bool relayLogWritten=false;
    DesyncCollectionStatus status=DesyncCollectionStatus::Partial;
    DesyncCaptureStats stats;
};
std::string fileName(std::size_t slot,std::uint64_t connection,std::size_t kind) {
    return "peer_"+std::to_string(slot)+"_"+std::to_string(connection)+"_"+std::to_string(kind)+(kind==3?".png":".bin");
}
void writeBytes(const std::filesystem::path& path,std::span<const std::uint8_t> bytes) {
    std::ofstream file(path,std::ios::binary|std::ios::trunc);
    if(!file)throw std::runtime_error("cannot create report file");
    if(!bytes.empty())file.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    file.flush();if(!file)throw std::runtime_error("cannot write report file");
}
void writeText(const std::filesystem::path& path,const std::string& text) {
    writeBytes(path,{reinterpret_cast<const std::uint8_t*>(text.data()),text.size()});
}
std::string manifest(Report& report,bool final) {
    std::ostringstream out;
    bool allComplete=report.status!=DesyncCollectionStatus::Interrupted && report.status!=DesyncCollectionStatus::StorageError;
    std::ostringstream participants;
    bool comma=false;
    for(std::size_t slot=0;slot<3;++slot) {
        const auto connection=report.request.connections[slot];if(!connection)continue;
        auto& p=report.peers[slot];if(comma)participants<<',';comma=true;
        allComplete=allComplete && p.done && !p.failed && p.completion.has_value();
        participants<<"{\"slot\":"<<slot<<",\"connectionId\":"<<connection<<",\"done\":"<<(p.done?"true":"false")
                    <<",\"error\":"<<quoted(p.error)<<",\"artifacts\":[";
        for(std::size_t kind=0;kind<4;++kind) {
            if(kind)participants<<',';
            const auto& a=p.artifacts[kind];
            DesyncArtifactDescriptor d;d.kind=static_cast<DesyncArtifactKind>(kind);
            if(p.completion)d=p.completion->artifacts[kind];
            const auto digest=desyncSha256(a.bytes);
            bool integrity=p.completion && d.bytes==a.bytes.size() && d.sha256==digest;
            const bool pngStructure=kind==3 && desyncPngStructure(a.bytes);
            if(kind==3 && d.status==DesyncArtifactStatus::Complete)integrity=integrity && pngStructure;
            allComplete=allComplete && integrity && d.status==DesyncArtifactStatus::Complete;
            participants<<"{\"kind\":"<<kind<<",\"file\":"<<quoted(fileName(slot,connection,kind))
                <<",\"status\":"<<quoted(artifactStatus(d.status))<<",\"bytes\":"<<d.bytes<<",\"receivedBytes\":"<<a.bytes.size()
                <<",\"receivedChunks\":"<<a.chunks.size()<<",\"fileWritten\":"<<(a.written?"true":"false")
                <<",\"structurallyValidated\":"<<(pngStructure?"true":"false")
                <<",\"sha256\":"<<quoted(desyncDigestHex(d.sha256))<<",\"receivedSha256\":"<<quoted(desyncDigestHex(digest))
                <<",\"integrity\":"<<(integrity?"true":"false")<<",\"sourceBytes\":"<<d.sourceBytes
                <<",\"rangeBegin\":"<<d.rangeBegin<<",\"rangeEnd\":"<<d.rangeEnd<<",\"truncated\":"<<(d.truncated?"true":"false")
                <<",\"startedMs\":"<<d.startedMs<<",\"finishedMs\":"<<d.finishedMs<<",\"errorCode\":"<<d.errorCode
                <<",\"sourceLabel\":"<<quoted(d.sourceLabel)<<",\"error\":"<<quoted(d.error)<<'}';
        }
        participants<<"]}";
    }
    if(final && report.status!=DesyncCollectionStatus::Interrupted && report.status!=DesyncCollectionStatus::StorageError)
        report.status=allComplete?DesyncCollectionStatus::Complete:DesyncCollectionStatus::Partial;
    out<<"{\"schemaVersion\":1,\"key\":{\"sessionId\":"<<quoted(report.request.key.sessionId)<<",\"reportId\":"<<report.request.key.reportId
       <<"},\"collectionStatus\":"<<quoted(final?statusName(report.status):"collecting")<<",\"error\":"<<quoted(report.error)<<",\"trigger\":";
    triggerJson(out,report.request);out<<",\"extraTriggers\":[";
    for(std::size_t i=0;i<report.extras.size();++i){if(i)out<<',';triggerJson(out,report.extras[i]);}
    const auto& s=report.stats;
    out<<"],\"counters\":{\"started\":"<<s.started<<",\"finalized\":"<<s.finalized<<",\"suppressedCadence\":"<<s.suppressedCadence
       <<",\"suppressedQuota\":"<<s.suppressedQuota<<",\"extraTriggers\":"<<s.extraTriggers<<",\"extraTriggerOverflow\":"<<s.extraTriggerOverflow
       <<",\"rejected\":"<<s.rejected<<",\"duplicateChunks\":"<<s.duplicateChunks<<",\"storageErrors\":"<<s.storageErrors<<"},\"peers\":["<<participants.str()<<']';
    const auto bytes=std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(report.relayLog.data()),report.relayLog.size());
    out<<",\"relay\":{\"file\":\"relay.log\",\"bytes\":"<<bytes.size()<<",\"sha256\":"<<quoted(desyncDigestHex(desyncSha256(bytes)))
       <<",\"fileWritten\":"<<(report.relayLogWritten?"true":"false")
       <<",\"sourceBytes\":"<<report.relayLogSourceBytes<<",\"rangeBegin\":"<<(report.relayLogSourceBytes-bytes.size())
       <<",\"rangeEnd\":"<<report.relayLogSourceBytes<<",\"truncated\":"<<(report.relayLogSourceBytes>bytes.size()?"true":"false")
       <<"},\"limits\":[\"bounded tails, not whole history\",\"relay receipts are not native frames\",\"peer clocks and screenshots are not atomic\",\"PNG structure and CRC checked, not decompressed or native capture proven\",\"transport integrity is not native provenance\"]}\n";
    return out.str();
}
struct WorkResult {DesyncCaptureResult result;};
WorkResult persist(std::shared_ptr<Report> report,const std::string& root,bool final) {
    WorkResult work;auto& result=work.result;result.key=report->request.key;result.status=report->status;
    try {
        if(!final) {
            const auto parent=std::filesystem::path(root)/report->request.key.sessionId;
            std::filesystem::create_directories(parent);
            const auto directory=parent/std::to_string(report->request.key.reportId);
            if(!std::filesystem::create_directory(directory))throw std::runtime_error("report directory already exists");
            report->directory=directory.string();
        }
        const auto directory=std::filesystem::path(report->directory);
        result.directory=report->directory;result.manifestPath=(directory/"manifest.json").string();
        if(!final){writeText(directory/"relay.log",report->relayLog);report->relayLogWritten=true;}
        if(final)for(std::size_t slot=0;slot<3;++slot)if(report->request.connections[slot])
            for(std::size_t kind=0;kind<4;++kind) {
                writeBytes(directory/fileName(slot,report->request.connections[slot],kind),report->peers[slot].artifacts[kind].bytes);
                report->peers[slot].artifacts[kind].written=true;
            }
        const auto text=manifest(*report,final);
        if(final) {
            // Preserve the initial trigger even if final publication is interrupted.
            writeText(directory/"final-manifest.json",text);
#ifdef _WIN32
            if(!MoveFileExW((directory/"final-manifest.json").c_str(),(directory/"manifest.json").c_str(),
                            MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("cannot publish final manifest");
#else
            std::filesystem::rename(directory/"final-manifest.json",directory/"manifest.json");
#endif
        } else {
            writeText(directory/"trigger.json",text);
            writeText(directory/"manifest.json",text);
        }
        result.manifestWritten=true;result.status=report->status;
    } catch(const std::exception& ex) {
        result.status=DesyncCollectionStatus::StorageError;result.error=ex.what();
        report->status=DesyncCollectionStatus::StorageError;report->error=result.error;
        if(!report->directory.empty())try{
            result.directory=report->directory;result.manifestPath=(std::filesystem::path(report->directory)/"manifest.json").string();
            writeText(result.manifestPath,manifest(*report,true));result.manifestWritten=true;
        }catch(const std::exception&){result.manifestWritten=false;}
    }
    return work;
}
struct SuppressionWitness {
    DesyncCaptureRequest request;
    std::string reason;
    std::uint64_t receivedMs=0;
};
struct SuppressionSummary {
    std::string sessionId;
    DesyncKey lastKey,lastLostKey;
    std::uint64_t revision=0,lastMs=0,lastLostMs=0;
    std::uint64_t cadence=0,quota=0,active=0,finishing=0,witnessOverflow=0;
    std::uint64_t lostTriggers=0,storageErrors=0;
    std::vector<SuppressionWitness> witnesses;
};
void keyJson(std::ostream& out,const DesyncKey& key) {
    out<<"{\"sessionId\":"<<quoted(key.sessionId)<<",\"reportId\":"<<key.reportId<<'}';
}
DesyncSuppressionResult persistSuppression(const SuppressionSummary& s,const std::string& root) {
    DesyncSuppressionResult result;result.sessionId=s.sessionId;result.revision=s.revision;
    try {
        const auto directory=std::filesystem::path(root)/s.sessionId;
        std::filesystem::create_directories(directory);
        const auto target=directory/"suppression-summary.json";
        const auto temporary=directory/"suppression-summary.pending.json";
        result.path=target.string();
        std::ostringstream out;
        out<<"{\"schemaVersion\":1,\"artifactType\":\"desync-suppression-summary\",\"collectionStatus\":\"skipped\",\"sessionId\":"
           <<quoted(s.sessionId)<<",\"revision\":"<<s.revision<<",\"lastKey\":";
        keyJson(out,s.lastKey);out<<",\"lastMs\":"<<s.lastMs<<",\"lastLostKey\":";keyJson(out,s.lastLostKey);
        out<<",\"lastLostMs\":"<<s.lastLostMs<<",\"counters\":{\"cadence\":"<<s.cadence<<",\"quota\":"<<s.quota
           <<",\"active\":"<<s.active<<",\"finishing\":"<<s.finishing<<",\"witnessOverflow\":"<<s.witnessOverflow
           <<",\"lostTriggers\":"<<s.lostTriggers<<",\"storageErrors\":"<<s.storageErrors<<"},\"witnesses\":[";
        for(std::size_t i=0;i<s.witnesses.size();++i) {
            if(i)out<<',';
            const auto& w=s.witnesses[i];out<<"{\"reason\":"<<quoted(w.reason)<<",\"key\":";keyJson(out,w.request.key);
            out<<",\"receivedMs\":"<<w.receivedMs<<",\"trigger\":";triggerJson(out,w.request);out<<'}';
        }
        out<<"],\"limits\":[\"suppressed triggers are not collected reports\",\"first 16 witnesses retained; overflow counted\","
              "\"lostTriggers and storageErrors are relay-lifetime counters\",\"snapshot may lag pending updates\"]}\n";
        writeText(temporary,out.str());
#ifdef _WIN32
        if(!MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("cannot publish suppression summary");
#else
        std::filesystem::rename(temporary,target);
#endif
        result.written=true;
    }catch(const std::exception& ex){result.error=std::string(ex.what()).substr(0,256);}
     catch(...){result.error="suppression summary persistence failed";}
    return result;
}
} // namespace

struct DesyncCapture::Impl {
    explicit Impl(std::string root):outputRoot(std::move(root)){}
    std::string outputRoot,quotaSession;
    std::uint64_t lastStarted=0;
    std::uint32_t quota=0;
    DesyncCaptureStats stats;
    std::shared_ptr<Report> active;
    std::future<WorkResult> work;
    bool initial=true,requestAvailable=false,requestIssued=false,finishing=false;
    std::uint64_t now=0;
    std::optional<DesyncCaptureResult> finalized;
    // One current coalesced snapshot, one worker-owned immutable snapshot.
    // Never retain an unbounded queue when sessions change during slow storage.
    std::optional<SuppressionSummary> summary;
    std::future<DesyncSuppressionResult> summaryWork;
    std::optional<DesyncSuppressionResult> summaryResult;
    DesyncKey lastLostKey;
    std::uint64_t lastLostMs=0,summaryWorkRevision=0;
    std::string summaryWorkSession;
    bool summaryDirty=false;
    void suppression(const DesyncCaptureRequest& request,const char* reason,std::uint64_t receivedMs) {
        if(summary && summary->sessionId!=request.key.sessionId && summaryDirty) {
            // Preserve the old pending session rather than silently replacing it.
            ++stats.summaryLostTriggers;lastLostKey=request.key;lastLostMs=receivedMs;
            ++summary->revision;summary->lostTriggers=stats.summaryLostTriggers;
            summary->lastLostKey=lastLostKey;summary->lastLostMs=lastLostMs;
            return;
        }
        if(!summary || summary->sessionId!=request.key.sessionId) {
            summary.emplace();summary->sessionId=request.key.sessionId;
        }
        auto& value=*summary;++value.revision;value.lastKey=request.key;value.lastMs=receivedMs;
        value.lostTriggers=stats.summaryLostTriggers;value.storageErrors=stats.summaryStorageErrors;
        value.lastLostKey=lastLostKey;value.lastLostMs=lastLostMs;
        const std::string kind(reason);
        if(kind=="cadence")++value.cadence;
        else if(kind=="quota")++value.quota;
        else if(kind=="finishing")++value.finishing;
        else ++value.active;
        if(value.witnesses.size()<DESYNC_EXTRA_TRIGGERS)value.witnesses.push_back({request,kind,receivedMs});
        else ++value.witnessOverflow;
        summaryDirty=true;
    }
    void summaryFailure(const std::string& error) {
        ++stats.summaryStorageErrors;++stats.storageErrors;
        summaryResult=DesyncSuppressionResult{summaryWorkSession,{},error.substr(0,256),summaryWorkRevision,false};
        if(summary)summary->storageErrors=stats.summaryStorageErrors;
        // No automatic retry loop on failed storage. A later trigger may retry.
    }
    void pumpSummary() {
        if(summaryWork.valid()) {
            if(summaryWork.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready)return;
            try {
                auto result=summaryWork.get();
                if(result.written)++stats.summaryWrites;
                else {++stats.summaryStorageErrors;++stats.storageErrors;}
                summaryResult=std::move(result);
                if(summary)summary->storageErrors=stats.summaryStorageErrors;
            }catch(const std::exception& ex){summaryFailure(ex.what());}
             catch(...){summaryFailure("suppression worker result retrieval failed");}
        }
        if(!summaryDirty || !summary)return;
        summaryDirty=false;summaryWorkSession=summary->sessionId;summaryWorkRevision=summary->revision;
        try {
            const auto snapshot=*summary;
            summaryWork=std::async(std::launch::async,[snapshot,root=outputRoot]{return persistSuppression(snapshot,root);});
        }catch(const std::exception& ex){summaryFailure(ex.what());}
         catch(...){summaryFailure("suppression worker launch failed");}
    }
    bool allDone()const {
        for(std::size_t i=0;i<3;++i)if(active->request.connections[i]&&!active->peers[i].done)return false;
        return true;
    }
    Participant* participant(std::uint64_t connection) {
        if(!active||finishing)return nullptr;
        for(std::size_t i=0;i<3;++i)if(connection && active->request.connections[i]==connection)return &active->peers[i];
        return nullptr;
    }
    bool fail(Participant* p,const char* reason){++stats.rejected;if(p){p->done=true;p->failed=true;p->error=reason;}return false;}
    void workerFailure(const std::string& error) {
        DesyncCaptureResult result;
        if(active){result.key=active->request.key;result.directory=active->directory;}
        result.status=DesyncCollectionStatus::StorageError;result.error=error.substr(0,256);
        // Existing initial files remain untouched. No successful final file is
        // invented when thread launch or future retrieval could not complete.
        result.manifestWritten=false;
        ++stats.storageErrors;++stats.finalized;finalized=std::move(result);
        active.reset();requestAvailable=false;requestIssued=false;finishing=false;
    }
};
DesyncCapture::DesyncCapture(std::string root):impl_(std::make_unique<Impl>(std::move(root))){}
DesyncCapture::~DesyncCapture() {
    Shutdown();
}
bool DesyncCapture::Trigger(const DesyncCaptureRequest& request,std::string relayLog,std::uint64_t now,std::uint64_t sourceBytes) {
    auto& s=*impl_;s.now=now;
    try{(void)encode(request);}catch(const std::exception&){++s.stats.rejected;return false;}
    if(s.outputRoot.empty() || now>=request.deadlineMs || request.triggerMs!=now){++s.stats.rejected;return false;}
    if(s.active) {
        ++s.stats.extraTriggers;
        if(!s.finishing && s.active->extras.size()<DESYNC_EXTRA_TRIGGERS)s.active->extras.push_back(request);
        else ++s.stats.extraTriggerOverflow;
        s.suppression(request,s.finishing?"finishing":"active",now);
        return false;
    }
    if(s.quotaSession!=request.key.sessionId){s.quotaSession=request.key.sessionId;s.quota=0;s.lastStarted=0;}
    if(s.quota>=DESYNC_SESSION_QUOTA){++s.stats.suppressedQuota;s.suppression(request,"quota",now);return false;}
    if(s.quota && now-s.lastStarted<DESYNC_CADENCE_MS){++s.stats.suppressedCadence;s.suppression(request,"cadence",now);return false;}
    ++s.quota;s.lastStarted=now;++s.stats.started;
    s.active=std::make_shared<Report>();s.active->request=request;s.active->relayLogSourceBytes=std::max<std::uint64_t>(sourceBytes,relayLog.size());
    if(relayLog.size()>DESYNC_LOG_BYTES)relayLog.erase(0,relayLog.size()-DESYNC_LOG_BYTES);
    s.active->relayLog=std::move(relayLog);s.active->stats=s.stats;
    s.initial=true;s.finishing=false;s.requestAvailable=false;s.requestIssued=false;
    // Worker owns a separate initial snapshot: network callbacks can safely
    // mark the active report interrupted while the initial disk write runs.
    try {
        auto snapshot=std::make_shared<Report>(*s.active);
        s.work=std::async(std::launch::async,[snapshot,root=s.outputRoot]{return persist(snapshot,root,false);});
    } catch(const std::exception& ex) { s.workerFailure(ex.what());return false; }
      catch(...) { s.workerFailure("initial worker launch failed");return false; }
    return true;
}
void DesyncCapture::Pump(std::uint64_t now) {
    auto& s=*impl_;s.now=now;
    s.pumpSummary();
    if(!s.active)return;
    if(s.work.valid()) {
        if(s.work.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready)return;
        DesyncCaptureResult result;
        try { result=s.work.get().result; }
        catch(const std::exception& ex){s.workerFailure(ex.what());return;}
        catch(...){s.workerFailure("worker result retrieval failed");return;}
        if(!s.initial || result.status==DesyncCollectionStatus::StorageError) {
            ++s.stats.finalized;if(result.status==DesyncCollectionStatus::StorageError)++s.stats.storageErrors;
            s.finalized=std::move(result);s.active.reset();s.finishing=false;s.requestAvailable=false;return;
        }
        s.active->directory=result.directory;s.active->relayLogWritten=true;s.initial=false;
        s.requestAvailable=s.active->status!=DesyncCollectionStatus::Interrupted && now<s.active->request.deadlineMs;
    }
    if(now>=s.active->request.deadlineMs) {
        s.requestAvailable=false;
        for(std::size_t i=0;i<3;++i)if(s.active->request.connections[i]&&!s.active->peers[i].done){
            s.active->peers[i].done=true;s.active->peers[i].failed=true;s.active->peers[i].error="deadline";
        }
    }
    if(!s.initial && !s.finishing && s.allDone()) {
        s.finishing=true;s.requestAvailable=false;s.active->stats=s.stats;++s.active->stats.finalized;
        try {
            auto snapshot=s.active;s.work=std::async(std::launch::async,[snapshot,root=s.outputRoot]{return persist(snapshot,root,true);});
        } catch(const std::exception& ex) { s.workerFailure(ex.what()); }
          catch(...) { s.workerFailure("final worker launch failed"); }
    }
}
std::optional<DesyncCaptureRequest> DesyncCapture::TakeRequest() {
    auto& s=*impl_;
    if(!s.active||!s.requestAvailable||s.now>=s.active->request.deadlineMs)return {};
    s.requestAvailable=false;s.requestIssued=true;auto request=s.active->request;
    request.remainingMs=static_cast<std::uint32_t>(request.deadlineMs-s.now);
    s.active->request.remainingMs=request.remainingMs;
    return request;
}
bool DesyncCapture::Chunk(std::uint64_t auth,const DesyncArtifactChunk& chunk) {
    auto& s=*impl_;auto* p=s.participant(auth);if(!p)return s.fail(nullptr,"unexpected connection");
    if(!s.requestIssued || s.now>=s.active->request.deadlineMs)return s.fail(p,"outside request window");
    try{(void)encode(chunk);}catch(const std::exception&){return s.fail(p,"malformed chunk");}
    if(chunk.key!=s.active->request.key || chunk.connectionId!=auth)return s.fail(p,"wrong key/connection");
    const auto kind=static_cast<std::size_t>(chunk.kind);auto& a=p->artifacts[kind];
    if(auto it=a.chunks.find(chunk.offset);it!=a.chunks.end()) {
        if(it->second==chunk.bytes.size() && std::equal(chunk.bytes.begin(),chunk.bytes.end(),a.bytes.begin()+chunk.offset)){
            ++s.stats.duplicateChunks;return true;
        }
        return s.fail(p,"conflicting chunk");
    }
    if(p->done)return s.fail(p,"data after completion");
    if(chunk.offset!=a.bytes.size())return s.fail(p,"chunk gap/overlap");
    std::size_t chunks=0;for(const auto& artifact:p->artifacts)chunks+=artifact.chunks.size();
    if(chunks>=DESYNC_MAX_CHUNKS_PER_PEER)return s.fail(p,"chunk bookkeeping cap");
    std::size_t logs=0;for(std::size_t i=0;i<3;++i)logs+=p->artifacts[i].bytes.size();
    if(kind<3 && logs+chunk.bytes.size()>DESYNC_LOG_BYTES)return s.fail(p,"aggregate log cap");
    a.chunks.emplace(chunk.offset,static_cast<std::uint32_t>(chunk.bytes.size()));
    a.bytes.insert(a.bytes.end(),chunk.bytes.begin(),chunk.bytes.end());return true;
}
bool DesyncCapture::Done(std::uint64_t auth,const DesyncCaptureDone& done) {
    auto& s=*impl_;auto* p=s.participant(auth);if(!p)return s.fail(nullptr,"unexpected connection");
    if(!s.requestIssued || s.now>=s.active->request.deadlineMs)return s.fail(p,"outside request window");
    try{(void)encode(done);}catch(const std::exception&){return s.fail(p,"malformed completion");}
    if(done.key!=s.active->request.key || done.connectionId!=auth)return s.fail(p,"wrong key/connection");
    if(p->completion) {
        if(encode(*p->completion)==encode(done))return true;
        return s.fail(p,"conflicting completion");
    }
    if(p->done)return false;
    p->completion=done;p->done=true;
    for(std::size_t i=0;i<4;++i)if(done.artifacts[i].bytes!=p->artifacts[i].bytes.size())return s.fail(p,"missing bytes at completion");
    return true; // SHA validation runs on the persistence worker, never the ENet callback.
}
void DesyncCapture::PeerLeft(std::uint64_t connection) {
    auto& s=*impl_;auto* p=s.participant(connection);if(p){p->done=true;p->failed=true;p->error="peer disconnected";Interrupt("expected peer disconnected");}
}
void DesyncCapture::Interrupt(const std::string& reason) {
    auto& s=*impl_;if(!s.active||s.finishing)return;
    s.active->status=DesyncCollectionStatus::Interrupted;s.active->error=reason.substr(0,256);s.requestAvailable=false;
    for(std::size_t i=0;i<3;++i)if(s.active->request.connections[i]){auto& p=s.active->peers[i];if(!p.done){p.done=true;p.failed=true;p.error="interrupted";}}
}
void DesyncCapture::Reject(std::uint64_t connection,const std::string& reason) {
    auto& s=*impl_;s.fail(s.participant(connection),reason.substr(0,256).c_str());
}
void DesyncCapture::Shutdown() {
    Interrupt("collector shutdown");
    while(impl_->active) {
        if(impl_->work.valid())impl_->work.wait();
        Pump(impl_->now);
    }
    while(impl_->summaryDirty || impl_->summaryWork.valid()) {
        if(impl_->summaryWork.valid())impl_->summaryWork.wait();
        impl_->pumpSummary();
    }
}
std::optional<DesyncCaptureResult> DesyncCapture::TakeFinalized(){return std::exchange(impl_->finalized,std::nullopt);}
std::optional<DesyncSuppressionResult> DesyncCapture::TakeSuppressionResult(){return std::exchange(impl_->summaryResult,std::nullopt);}
const DesyncCaptureStats& DesyncCapture::Stats()const noexcept{return impl_->stats;}
bool DesyncCapture::Active()const noexcept{return impl_->active!=nullptr;}
} // namespace kh2coop
