#include "kh2coop/DesyncCollector.hpp"
#include "kh2coop/CaptureChannel.hpp"
#include "kh2coop/CaptureLease.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace kh2coop {
namespace {
using Clock = std::chrono::steady_clock;
constexpr std::size_t MetadataCap = 160 * 1024, RuntimeCap = 128 * 1024;
constexpr std::size_t InjectCap = DESYNC_LOG_BYTES - MetadataCap - RuntimeCap;
std::uint64_t Now() { return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count(); }
bool Valid(const DesyncBinding& b) {
    return b.admitted &&
        b.localSlot < 3 && b.connections[0] && b.connections[b.localSlot] &&
        b.sessionId.size() == 32 && b.sessionId.find_first_not_of("0123456789abcdef") == std::string::npos;
}
void Fail(DesyncArtifactDescriptor& d, DesyncArtifactStatus status, std::string error, std::uint32_t code = 0) {
    d.status = status; d.error = error.substr(0, 256); d.errorCode = code;
}
void Bounded(std::string& s, std::size_t cap) { if (s.size() > cap) s.resize(cap); }
void Digest(DesyncArtifactDescriptor& d, const std::vector<std::uint8_t>& bytes) {
    d.bytes = static_cast<std::uint32_t>(bytes.size()); d.sha256 = desyncSha256(bytes);
    // Ranges describe copied bytes, including explicit empty failure ranges;
    // sourceBytes separately retains the observed oversized/source file length.
    d.rangeEnd = d.rangeBegin + bytes.size();
}
void Write(const std::filesystem::path& p, const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.exceptions(std::ios::badbit | std::ios::failbit);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.close();
}
struct FileStamp {
    std::uint64_t size {};
    std::filesystem::file_time_type time {};
    std::uint64_t id {}, volume {};
    bool idAvailable {false};
    bool operator==(const FileStamp&) const = default;
};
FileStamp Stamp(const std::filesystem::path& path) {
    FileStamp s; s.size = std::filesystem::file_size(path); s.time = std::filesystem::last_write_time(path);
#ifdef _WIN32
    const auto h = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        BY_HANDLE_FILE_INFORMATION info {};
        if (GetFileInformationByHandle(h, &info)) {
            s.idAvailable = true; s.volume = info.dwVolumeSerialNumber;
            s.id = (static_cast<std::uint64_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
        }
        CloseHandle(h);
    }
#endif
    return s;
}
struct TailNotes { bool cutLine {}, rotationDetected {}, changedDuringRead {}, fileIdentityAvailable {}; };
struct CaptureAttempt {
    bool providerAttempted {}, providerAvailable {}, mailboxAvailable {}, submissionAvailable {}, submitted {};
    bool expectedSequenceAvailable {}, pollAvailable {}, completionAvailable {}, rendererAvailable {}, pathAvailable {};
    std::uint32_t expectedSequence {}, requestSeq {}, doneSeq {}, framesWritten {}, width {}, height {};
    std::uint32_t renderer {}, backbufferFormat {}, errorCode {};
    std::int32_t nativeStatus {-1};
    std::uint64_t startedMs {}, submittedObservedMs {}, lastPollMs {}, finishedMs {};
    char error[257] {};
};
void AttemptError(CaptureAttempt& a, const std::string& error, std::uint32_t code) {
    const auto size = std::min<std::size_t>(error.size(), sizeof(a.error) - 1);
    std::copy_n(error.data(), size, a.error); a.error[size] = '\0'; a.errorCode = code;
}
void ObserveAttempt(CaptureAttempt& a, const DesyncCapturePoll& p, bool begin) {
    a.pollAvailable = true; a.lastPollMs = Now(); a.mailboxAvailable = p.mailboxAvailable;
    if (begin) {
        a.expectedSequenceAvailable = p.expectedSequenceAvailable || p.state == DesyncCaptureState::Pending || p.state == DesyncCaptureState::Complete;
        a.expectedSequence = p.expectedSequenceAvailable ? p.expectedSequence : a.expectedSequenceAvailable ? p.requestSeq : 0;
    }
    a.submissionAvailable = p.submissionAvailable;
    a.submitted = a.submitted || p.submitted;
    if (p.submitted && !a.submittedObservedMs) a.submittedObservedMs = a.lastPollMs;
    a.requestSeq = p.requestSeq; a.doneSeq = p.doneSeq; a.nativeStatus = p.nativeStatus;
    a.framesWritten = p.framesWritten; a.width = p.width; a.height = p.height;
    a.completionAvailable = a.expectedSequenceAvailable && p.nativeStatus >= 0 &&
        p.requestSeq == a.expectedSequence && p.doneSeq == a.expectedSequence;
    a.rendererAvailable = p.rendererAvailable; a.renderer = p.renderer; a.backbufferFormat = p.backbufferFormat;
    AttemptError(a, p.error, p.errorCode);
}
void AttemptText(std::ostream& out, const CaptureAttempt& a) {
    out << "captureAttempt schema=1 providerAttempted=" << a.providerAttempted << " providerAvailable=" << a.providerAvailable
        << " mailboxAvailable=" << a.mailboxAvailable << " submissionAvailable=" << a.submissionAvailable << " submitted=" << a.submitted
        << " expectedSequenceAvailable=" << a.expectedSequenceAvailable << " expectedSequence=" << a.expectedSequence
        << " pollAvailable=" << a.pollAvailable << " requestSeq=" << a.requestSeq << " doneSeq=" << a.doneSeq
        << " completionAvailable=" << a.completionAvailable << " nativeStatus=" << a.nativeStatus << " framesWritten=" << a.framesWritten
        << " width=" << a.width << " height=" << a.height << " rendererAvailable=" << a.rendererAvailable
        << " renderer=" << a.renderer << " backbufferFormat=" << a.backbufferFormat << " pathAvailable=" << a.pathAvailable
        << " relativePath=\"screenshot.png\" startedMs=" << a.startedMs << " submittedObservedMs=" << a.submittedObservedMs
        << " lastPollMs=" << a.lastPollMs << " finishedMs=" << a.finishedMs << " errorCode=" << a.errorCode
        << " error=" << std::quoted(a.error) << '\n';
}
void Tail(const std::filesystem::path& path, std::size_t cap, DesyncArtifactDescriptor& d,
          std::vector<std::uint8_t>& bytes, TailNotes& notes) {
    d.startedMs = Now();
    if (path.empty()) { Fail(d, DesyncArtifactStatus::Unavailable, "explicit inject log path not supplied"); d.finishedMs = Now(); return; }
    try {
        const auto before = Stamp(path); notes.fileIdentityAvailable = before.idAvailable;
        d.sourceBytes = before.size; d.rangeEnd = before.size;
        d.rangeBegin = before.size > cap ? before.size - cap : 0;
        d.truncated = d.rangeBegin != 0;
        std::ifstream file(path, std::ios::binary);
        file.exceptions(std::ios::badbit | std::ios::failbit);
        file.seekg(static_cast<std::streamoff>(d.rangeBegin));
        bytes.resize(static_cast<std::size_t>(d.rangeEnd - d.rangeBegin));
        if (!bytes.empty()) file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        const auto after = Stamp(path);
        notes.rotationDetected = before.idAvailable && after.idAvailable && (before.id != after.id || before.volume != after.volume);
        notes.changedDuringRead = before != after;
        if (notes.changedDuringRead) Fail(d, DesyncArtifactStatus::Interrupted, "log changed during capture; rotation/truncation/append cannot certify a stable tail");
        else d.status = DesyncArtifactStatus::Complete;
        if (d.rangeBegin && !bytes.empty()) {
            const auto newline = std::find(bytes.begin(), bytes.end(), '\n');
            notes.cutLine = true;
            if (newline != bytes.end()) {
                const auto cut = static_cast<std::size_t>(newline - bytes.begin()) + 1;
                d.rangeBegin += cut; bytes.erase(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(cut));
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        bytes.clear();
        Fail(d, DesyncArtifactStatus::ReadError, "inject log filesystem error", static_cast<std::uint32_t>(e.code().value()));
    } catch (...) { bytes.clear(); Fail(d, DesyncArtifactStatus::ReadError, "inject log read failed or changed during read"); }
    d.finishedMs = Now();
}
std::uint32_t Big32(const std::vector<std::uint8_t>& b, std::size_t i) {
    return (std::uint32_t(b[i]) << 24) | (std::uint32_t(b[i+1]) << 16) | (std::uint32_t(b[i+2]) << 8) | b[i+3];
}
bool PngHeader(const std::vector<std::uint8_t>& b, const DesyncCapturePoll& p) {
    constexpr std::uint8_t sig[] {137,80,78,71,13,10,26,10};
    return desyncPngStructure(b) && b.size() >= 33 && std::equal(std::begin(sig), std::end(sig), b.begin()) &&
        Big32(b, 8) == 13 && b[12] == 'I' && b[13] == 'H' && b[14] == 'D' && b[15] == 'R' &&
        p.width && p.height && p.width <= 32768 && p.height <= 32768 &&
        Big32(b, 16) == p.width && Big32(b, 20) == p.height;
}
class ChannelCapture final : public DesyncCaptureProvider {
public:
    ~ChannelCapture() override { Finish(); }
    DesyncCapturePoll Begin(std::uint32_t pid, const std::filesystem::path& output) override {
        Finish();
        DesyncCapturePoll result;
        result.submissionAvailable = true;
#ifdef _WIN32
        const auto acquired = lease_.Acquire(pid);
        if (acquired != CaptureLeaseStatus::Acquired) {
            result.state = acquired == CaptureLeaseStatus::Busy ? DesyncCaptureState::Busy :
                acquired == CaptureLeaseStatus::Abandoned ? DesyncCaptureState::Abandoned : DesyncCaptureState::Error;
            result.error = "capture caller lease unavailable"; result.errorCode = lease_.Error(); return result;
        }
        const auto name = CAPTURE_NAME_PREFIX + std::to_wstring(pid);
        mapping_ = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
        if (!mapping_) { result.errorCode = GetLastError(); result.error = "owned PID capture mapping unavailable"; return result; }
        channel_ = static_cast<CaptureChannel*>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(CaptureChannel)));
        if (!channel_ || channel_->magic != CAPTURE_MAGIC || channel_->version != CAPTURE_VERSION) {
            result.error = "capture mapping/version unavailable"; return result;
        }
        result.mailboxAvailable = true;
        result.rendererAvailable = true; result.renderer = channel_->renderer; result.backbufferFormat = channel_->backbufferFormat;
        result.requestSeq = static_cast<std::uint32_t>(InterlockedCompareExchange(&channel_->requestSeq, 0, 0));
        result.doneSeq = static_cast<std::uint32_t>(InterlockedCompareExchange(&channel_->doneSeq, 0, 0));
        path_ = output.wstring();
        if (path_.size() >= std::size(channel_->output)) { result.error = "capture path too long"; return result; }
        if (InterlockedCompareExchange(&channel_->requestSeq, 0, 0) != InterlockedCompareExchange(&channel_->doneSeq, 0, 0)) {
            result.state = DesyncCaptureState::Busy; result.error = "unfinished mailbox request (including timed-out caller)"; return result;
        }
        std::copy(path_.begin(), path_.end(), channel_->output); channel_->output[path_.size()] = L'\0';
        channel_->frameCount = 1; channel_->frameInterval = 1;
        sequence_ = static_cast<std::uint32_t>(InterlockedIncrement(&channel_->requestSeq));
        submitted_ = true;
        return Poll();
#else
        (void)pid; (void)output; result.error = "CaptureChannel adapter requires Windows"; return result;
#endif
    }
    DesyncCapturePoll Poll() override {
        DesyncCapturePoll result;
#ifdef _WIN32
        if (!channel_ || !submitted_) return result;
        result.mailboxAvailable = true; result.submissionAvailable = true; result.submitted = true;
        result.expectedSequenceAvailable = true; result.expectedSequence = sequence_;
        result.rendererAvailable = true; result.renderer = channel_->renderer; result.backbufferFormat = channel_->backbufferFormat;
        result.requestSeq = static_cast<std::uint32_t>(InterlockedCompareExchange(&channel_->requestSeq, 0, 0));
        result.doneSeq = static_cast<std::uint32_t>(InterlockedCompareExchange(&channel_->doneSeq, 0, 0));
        const auto end = std::find(std::begin(channel_->output), std::end(channel_->output), L'\0');
        if (result.requestSeq != sequence_ || end == std::end(channel_->output) ||
            std::wstring(channel_->output, end) != path_ || channel_->frameCount != 1 || channel_->frameInterval != 1) {
            result.state = DesyncCaptureState::Error; result.error = "mailbox ownership changed"; return result;
        }
        result.state = DesyncCaptureState::Pending;
        if (result.doneSeq != sequence_) return result;
        result.nativeStatus = channel_->status; result.framesWritten = channel_->framesWritten;
        result.width = channel_->width; result.height = channel_->height;
        if (static_cast<std::uint32_t>(InterlockedCompareExchange(&channel_->requestSeq, 0, 0)) != sequence_ ||
            static_cast<std::uint32_t>(InterlockedCompareExchange(&channel_->doneSeq, 0, 0)) != sequence_) {
            result.state = DesyncCaptureState::Error; result.error = "capture completion changed while sampled"; return result;
        }
        result.state = result.nativeStatus == 0 ? DesyncCaptureState::Complete : DesyncCaptureState::Error;
        if (result.nativeStatus != 0) result.error = "native capture status " + std::to_string(result.nativeStatus);
#endif
        return result;
    }
    void Finish() noexcept override {
#ifdef _WIN32
        if (channel_) UnmapViewOfFile(channel_); channel_ = nullptr;
        if (mapping_) CloseHandle(mapping_); mapping_ = nullptr;
        submitted_ = false; lease_.Release();
#endif
    }
private:
#ifdef _WIN32
    CaptureLease lease_;
    HANDLE mapping_ {};
    CaptureChannel* channel_ {};
    std::wstring path_;
    std::uint32_t sequence_ {};
    bool submitted_ {};
#endif
};
} // namespace

std::unique_ptr<DesyncCaptureProvider> MakeDesyncCaptureProvider() { return std::make_unique<ChannelCapture>(); }

struct DesyncCollector::Impl {
    struct Job {
        DesyncCaptureRequest request; DesyncBinding binding; std::string metadata, runtime;
        std::uint64_t started {}, runtimeSourceBytes {}, metadataSourceBytes {};
    };
    DesyncCollectorOptions options;
    mutable std::mutex mutex;
    std::condition_variable cv;
    std::thread worker;
    std::optional<Job> queued;
    std::unique_ptr<DesyncCollected> completed;
    DesyncBinding current;
    std::string metadataAfter;
    DesyncKey last;
    DesyncCaptureRequest activeRequest;
    DesyncBinding activeBinding;
    std::uint64_t activeDeadline {}, tickSerial {}, metadataAfterSourceBytes {};
    CaptureAttempt activeAttempt;
    bool active {}, cancelled {}, stopping {}, timedOut {};
    explicit Impl(DesyncCollectorOptions o) : options(std::move(o)) { worker = std::thread([this] { Run(); }); }
    ~Impl() {
        { std::lock_guard lock(mutex); stopping = true; cancelled = true; }
        cv.notify_one(); worker.join();
    }
    bool Interrupted(const DesyncBinding& b) { std::lock_guard lock(mutex); return cancelled || stopping || timedOut || current != b; }
    void PublishAttempt(const CaptureAttempt& attempt) { std::lock_guard lock(mutex); activeAttempt = attempt; }
    void Screenshot(const Job& job, DesyncCollected& result, std::uint64_t deadline, CaptureAttempt& attempt) {
        auto& d = result.done.artifacts[3]; auto& bytes = result.bytes[3]; d.startedMs = Now();
        attempt.startedMs = d.startedMs;
        struct RetainAttempt {
            Impl& owner; CaptureAttempt& attempt; DesyncArtifactDescriptor& descriptor;
            ~RetainAttempt() {
                attempt.finishedMs = Now();
                if (!descriptor.error.empty()) AttemptError(attempt, descriptor.error, descriptor.errorCode);
                owner.PublishAttempt(attempt);
            }
        } retain {*this, attempt, d};
        if (!job.binding.attachedPid || !job.binding.generationValid || !job.binding.localGeneration) {
            Fail(d, DesyncArtifactStatus::Unavailable, "no current attached PID/native generation; logs collected independently");
            d.finishedMs = Now(); return;
        }
        auto provider = options.captureFactory ? options.captureFactory() : MakeDesyncCaptureProvider();
        if (!provider) { Fail(d, DesyncArtifactStatus::Unavailable, "capture provider unavailable"); d.finishedMs = Now(); return; }
        attempt.providerAvailable = true;
        struct Finish { DesyncCaptureProvider* p; ~Finish() { p->Finish(); } } finish {provider.get()};
        const auto path = result.localDirectory / "screenshot.png";
        if (Interrupted(job.binding)) { Fail(d, DesyncArtifactStatus::Interrupted, "binding retired before capture"); return; }
        attempt.providerAttempted = true; PublishAttempt(attempt);
        auto p = provider->Begin(job.binding.attachedPid, path);
        ObserveAttempt(attempt, p, true); PublishAttempt(attempt);
        const auto ownSequence = p.requestSeq;
        while (p.state == DesyncCaptureState::Pending) {
            if (Interrupted(job.binding)) { Fail(d, DesyncArtifactStatus::Interrupted, "binding retired during capture; late file retained locally only"); d.finishedMs = Now(); return; }
            if (Now() >= deadline) { Fail(d, DesyncArtifactStatus::Timeout, "capture timed out; late file retained locally only"); d.finishedMs = Now(); return; }
            std::this_thread::sleep_for(std::chrono::milliseconds(10)); p = provider->Poll();
            ObserveAttempt(attempt, p, false); PublishAttempt(attempt);
        }
        if (Now() >= deadline) { Fail(d, DesyncArtifactStatus::Timeout, "completion exceeded local deadline"); d.finishedMs = Now(); return; }
        if (p.state != DesyncCaptureState::Complete) {
            const auto status = p.state == DesyncCaptureState::Error ? DesyncArtifactStatus::ReadError : DesyncArtifactStatus::Unavailable;
            const auto reason = p.state == DesyncCaptureState::Busy ? "Busy: " : p.state == DesyncCaptureState::Abandoned ? "Abandoned: " : "";
            Fail(d, status, std::string(reason) + p.error, p.errorCode); d.finishedMs = Now(); return;
        }
        if (p.requestSeq != ownSequence || p.doneSeq != ownSequence || p.nativeStatus != 0 || p.framesWritten != 1 || !p.width || !p.height) {
            Fail(d, DesyncArtifactStatus::ReadError, "completion sequence/status/frame/dimensions mismatch"); d.finishedMs = Now(); return;
        }
        try {
            const auto before = Stamp(path); d.sourceBytes = before.size; d.rangeEnd = before.size;
            if (before.size > DESYNC_PNG_BYTES) { d.truncated = true; Fail(d, DesyncArtifactStatus::Oversize, "raw PNG retained locally; exceeds 16MiB"); d.finishedMs = Now(); return; }
            std::ifstream file(path, std::ios::binary); file.exceptions(std::ios::badbit | std::ios::failbit);
            bytes.resize(static_cast<std::size_t>(before.size));
            if (!bytes.empty()) file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (before != Stamp(path)) Fail(d, DesyncArtifactStatus::Interrupted, "PNG changed while read");
            else if (!PngHeader(bytes, p)) Fail(d, DesyncArtifactStatus::ReadError, "PNG header/dimensions invalid");
            else d.status = DesyncArtifactStatus::Complete;
        } catch (const std::filesystem::filesystem_error& e) { bytes.clear(); Fail(d, DesyncArtifactStatus::ReadError, "capture PNG filesystem error", static_cast<std::uint32_t>(e.code().value())); }
        catch (...) { bytes.clear(); Fail(d, DesyncArtifactStatus::ReadError, "capture PNG read failed"); }
        if (Interrupted(job.binding)) Fail(d, DesyncArtifactStatus::Interrupted, "binding retired during PNG read");
        if (Now() >= deadline) Fail(d, DesyncArtifactStatus::Timeout, "PNG read exceeded local deadline; retained locally only");
        d.finishedMs = Now();
    }
    std::unique_ptr<DesyncCollected> Collect(const Job& job) {
        auto result = std::make_unique<DesyncCollected>();
        result->before = job.binding; result->done.key = job.request.key;
        result->done.connectionId = job.binding.connections[job.binding.localSlot];
        const auto started = job.started;
        const auto deadline = started + std::min<std::uint32_t>(job.request.remainingMs, std::clamp(options.captureTimeoutMs, 1u, 8000u));
        constexpr const char* labels[] {"runtime-metadata", "runtime-tail-provided-snapshot", "explicit-inject-log", "owned-PID-Present-PNG"};
        for (unsigned i = 0; i < 4; ++i) { auto& d = result->done.artifacts[i]; d.kind = static_cast<DesyncArtifactKind>(i); d.sourceLabel = labels[i]; d.startedMs = started; }
        TailNotes notes;
        CaptureAttempt attempt;
        try {
            if (options.spoolRoot.empty()) throw std::runtime_error("spool root not supplied");
            std::filesystem::create_directories(options.spoolRoot);
            static std::atomic<std::uint64_t> serial {};
            bool created = false;
            for (unsigned attempt = 0; attempt < 32 && !created; ++attempt) {
                const auto name = job.binding.sessionId + "_" + std::to_string(job.request.key.reportId) + "_" +
                    std::to_string(result->done.connectionId) + "_" + std::to_string(started) + "_" + std::to_string(++serial);
                result->localDirectory = std::filesystem::absolute(options.spoolRoot / name);
                created = std::filesystem::create_directory(result->localDirectory);
            }
            if (!created) throw std::runtime_error("unique spool directory unavailable");
            attempt.pathAvailable = true; PublishAttempt(attempt);
            auto& runtime = result->done.artifacts[1];
            runtime.sourceBytes = job.runtimeSourceBytes; runtime.rangeEnd = job.runtimeSourceBytes;
            runtime.rangeBegin = job.runtimeSourceBytes - job.runtime.size();
            runtime.truncated = runtime.rangeBegin != 0;
            result->bytes[1].assign(job.runtime.begin(), job.runtime.end());
            if (job.runtime.empty()) Fail(runtime, DesyncArtifactStatus::Unavailable, "runtime ring snapshot not supplied or empty");
            else runtime.status = DesyncArtifactStatus::Complete;
            runtime.finishedMs = Now();
            Tail(options.injectLogPath, InjectCap, result->done.artifacts[2], result->bytes[2], notes);
            if (Now() < deadline) Screenshot(job, *result, deadline, attempt);
            else Fail(result->done.artifacts[3], DesyncArtifactStatus::Timeout, "collection deadline expired before capture");
        } catch (const std::exception& e) {
            for (auto& d : result->done.artifacts) if (d.status == DesyncArtifactStatus::Unavailable) Fail(d, DesyncArtifactStatus::ReadError, e.what());
        } catch (...) { for (auto& d : result->done.artifacts) Fail(d, DesyncArtifactStatus::ReadError, "collector failure"); }
        if (!result->done.artifacts[3].error.empty()) AttemptError(attempt, result->done.artifacts[3].error, result->done.artifacts[3].errorCode);
        PublishAttempt(attempt);
        std::string after;
        bool afterFresh = false, expired = false;
        std::uint64_t afterSourceBytes = 0;
        {
            std::unique_lock lock(mutex);
            const auto previousTick = tickSerial;
            // Ask for a genuinely later owner snapshot without waiting on the owner thread.
            cv.wait_for(lock, std::chrono::milliseconds(100), [&] { return tickSerial != previousTick || cancelled || stopping || timedOut; });
            afterFresh = tickSerial != previousTick;
            result->after = current; if (afterFresh) after = metadataAfter;
            afterSourceBytes = metadataAfterSourceBytes;
            result->identityChanged = cancelled || stopping || current != job.binding;
            expired = timedOut || Now() >= deadline;
        }
        std::ostringstream metadata;
        metadata << "desync-local schema=1 session=" << job.request.key.sessionId << " report=" << job.request.key.reportId
            << " connection=" << result->done.connectionId << " pid=" << job.binding.attachedPid
            << " generationBefore=" << job.binding.localGeneration << " generationAfter=" << result->after.localGeneration
            << " identityChanged=" << result->identityChanged << " startedMs=" << started << " finishedMs=" << Now()
            << " afterFresh=" << afterFresh << " metadataBeforeSourceBytes=" << job.metadataSourceBytes
            << " metadataAfterSourceBytes=" << afterSourceBytes << " metadataBeforeTruncated=" << (job.metadataSourceBytes > job.metadata.size())
            << " metadataAfterTruncated=" << (afterSourceBytes > after.size())
            << "\nlog cutLine=" << notes.cutLine << " rotationDetected=" << notes.rotationDetected
            << " changedDuringRead=" << notes.changedDuringRead << " fileIdentityAvailable=" << notes.fileIdentityAvailable
            << "\nruntimeRingCutLineUnknown=1 runtimeRingPriorHistoryUnavailable=1\n";
        AttemptText(metadata, attempt);
        metadata << "explicitInjectLog=" << std::quoted(options.injectLogPath.string()) << '\n';
        for (const auto* b : {&result->before, &result->after}) {
            metadata << "binding session=" << b->sessionId << " slot=" << unsigned(b->localSlot)
                << " generation=" << b->localGeneration << " pid=" << b->attachedPid << " admitted=" << b->admitted
                << " generationValid=" << b->generationValid << " roster=" << b->connections[0] << ',' << b->connections[1] << ',' << b->connections[2] << '\n';
        }
        metadata << "BEFORE\n" << job.metadata << "\nAFTER (owner Tick after collection)\n" << after << '\n';
        auto text = metadata.str();
        const auto composedSourceBytes = text.size();
        Bounded(text, MetadataCap);
        result->bytes[0].assign(text.begin(), text.end());
        auto& md = result->done.artifacts[0]; md.sourceBytes = composedSourceBytes; md.rangeEnd = text.size();
        md.truncated = composedSourceBytes > text.size();
        if (job.metadata.empty() || after.empty()) Fail(md, DesyncArtifactStatus::Unavailable, "before/after metadata snapshot missing");
        else md.status = DesyncArtifactStatus::Complete;
        if (result->identityChanged) for (auto& d : result->done.artifacts) Fail(d, DesyncArtifactStatus::Interrupted, "binding changed/cancelled; local evidence only");
        else if (expired) for (auto& d : result->done.artifacts) Fail(d, DesyncArtifactStatus::Timeout, "local collection deadline expired; late evidence retained locally only");
        for (unsigned i = 0; i < 4; ++i) {
            auto& d = result->done.artifacts[i]; if (!d.finishedMs) d.finishedMs = Now(); Digest(d, result->bytes[i]);
            if (!result->localDirectory.empty() && i != 3) {
                try { Write(result->localDirectory / (std::to_string(i) + ".raw"), result->bytes[i]); }
                catch (...) { Fail(d, DesyncArtifactStatus::ReadError, "local spool write failed"); }
            }
        }
        if (!result->localDirectory.empty()) {
            try {
                std::ostringstream manifest;
                for (const auto& d : result->done.artifacts) manifest << "kind=" << unsigned(d.kind) << " status=" << unsigned(d.status)
                    << " bytes=" << d.bytes << " sha256=" << desyncDigestHex(d.sha256) << " sourceBytes=" << d.sourceBytes
                    << " rangeBegin=" << d.rangeBegin << " rangeEnd=" << d.rangeEnd << " truncated=" << d.truncated
                    << " startedMs=" << d.startedMs << " finishedMs=" << d.finishedMs << " errorCode=" << d.errorCode
                    << " error=" << std::quoted(d.error) << '\n';
                const auto s = manifest.str(); Write(result->localDirectory / "artifacts.txt", {s.begin(), s.end()});
            } catch (...) { Fail(md, DesyncArtifactStatus::ReadError, "local descriptor spool write failed"); }
        }
        return result;
    }
    void Run() {
        for (;;) {
            Job job;
            { std::unique_lock lock(mutex); cv.wait(lock, [&] { return stopping || queued.has_value(); });
              if (stopping && !queued) return;
              job = std::move(*queued); queued.reset(); }
            std::unique_ptr<DesyncCollected> result;
            try { result = Collect(job); } catch (...) { /* allocation failure: no fabricated successful result */ }
            {
                std::lock_guard lock(mutex);
                if (!cancelled && !stopping && !timedOut && current == job.binding) {
                    if (result && Now() >= activeDeadline) for (auto& d : result->done.artifacts)
                        Fail(d, DesyncArtifactStatus::Timeout, "local spool finalization exceeded deadline; bytes retained as partial evidence");
                    completed = std::move(result);
                }
                active = false;
            }
        }
    }
};
DesyncCollector::DesyncCollector(DesyncCollectorOptions options) : impl_(std::make_unique<Impl>(std::move(options))) {}
DesyncCollector::~DesyncCollector() = default;
bool DesyncCollector::Start(const DesyncCaptureRequest& request, const DesyncBinding& binding, std::string metadata, std::string runtime) {
    if (!Valid(binding) || request.key.sessionId != binding.sessionId || !request.key.reportId || request.connections != binding.connections ||
        !request.remainingMs || request.remainingMs > DESYNC_DEADLINE_MS) return false;
    const auto metadataSourceBytes = metadata.size(), runtimeSourceBytes = runtime.size();
    Bounded(metadata, 64 * 1024);
    if (runtime.size() > RuntimeCap) runtime.erase(0, runtime.size() - RuntimeCap);
    std::lock_guard lock(impl_->mutex);
    if (impl_->active || impl_->completed || impl_->stopping || (impl_->last.sessionId == request.key.sessionId && request.key.reportId <= impl_->last.reportId)) return false;
    const auto started = Now();
    impl_->queued = Impl::Job {request, binding, std::move(metadata), std::move(runtime), started, runtimeSourceBytes, metadataSourceBytes};
    impl_->activeRequest = request; impl_->activeBinding = binding;
    impl_->activeDeadline = started + std::min<std::uint32_t>(request.remainingMs, std::clamp(impl_->options.captureTimeoutMs, 1u, 8000u));
    impl_->current = binding; impl_->metadataAfter.clear(); impl_->metadataAfterSourceBytes = 0;
    impl_->activeAttempt = {};
    impl_->cancelled = false; impl_->timedOut = false; impl_->active = true; impl_->last = request.key;
    impl_->cv.notify_one(); return true;
}
void DesyncCollector::Tick(const DesyncBinding& binding, std::string metadata) {
    const auto sourceBytes = metadata.size();
    Bounded(metadata, 64 * 1024);
    std::lock_guard lock(impl_->mutex);
    if (impl_->current != binding) { impl_->cancelled = true; impl_->completed.reset(); }
    impl_->current = binding; impl_->metadataAfter = std::move(metadata); impl_->metadataAfterSourceBytes = sourceBytes;
    ++impl_->tickSerial; impl_->cv.notify_one();
    // Disk I/O can stall an OS thread. Publish a bounded partial result here,
    // never spawn a replacement worker or wait for that I/O on the owner thread.
    if (impl_->active && !impl_->cancelled && !impl_->timedOut && Now() >= impl_->activeDeadline) {
        auto result = std::make_unique<DesyncCollected>();
        result->done.key = impl_->activeRequest.key; result->done.connectionId = binding.connections[binding.localSlot];
        result->before = impl_->activeBinding; result->after = binding;
        std::ostringstream witness;
        witness << "desync-local schema=1 deadlineSnapshot=1 session=" << result->done.key.sessionId
            << " report=" << result->done.key.reportId << " connection=" << result->done.connectionId
            << " pid=" << binding.attachedPid << " generation=" << binding.localGeneration
            << " snapshotMs=" << Now() << "\nworkerFinished=0; latest copied attempt, no new capture reads\n";
        AttemptText(witness, impl_->activeAttempt);
        const auto witnessText = witness.str(); result->bytes[0].assign(witnessText.begin(), witnessText.end());
        for (unsigned i = 0; i < 4; ++i) {
            auto& d = result->done.artifacts[i]; d.kind = static_cast<DesyncArtifactKind>(i);
            d.startedMs = impl_->activeDeadline - std::min<std::uint32_t>(impl_->activeRequest.remainingMs, std::clamp(impl_->options.captureTimeoutMs, 1u, 8000u));
            d.finishedMs = Now(); d.sourceLabel = "collector-deadline";
            d.sourceBytes = result->bytes[i].size();
            Fail(d, DesyncArtifactStatus::Timeout, "worker deadline; any late raw evidence remains local, final status will not be rewritten"); Digest(d, result->bytes[i]);
        }
        impl_->timedOut = true; impl_->completed = std::move(result); impl_->cv.notify_one();
    }
}
void DesyncCollector::Cancel() { std::lock_guard lock(impl_->mutex); impl_->cancelled = true; impl_->completed.reset(); }
bool DesyncCollector::Busy() const { std::lock_guard lock(impl_->mutex); return impl_->active || bool(impl_->completed); }
std::unique_ptr<DesyncCollected> DesyncCollector::TakeCompleted() { std::lock_guard lock(impl_->mutex); return std::move(impl_->completed); }
} // namespace kh2coop
