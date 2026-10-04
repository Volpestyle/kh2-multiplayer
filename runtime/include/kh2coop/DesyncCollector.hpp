#pragma once
#include "kh2coop/DesyncProtocol.hpp"
#include <filesystem>
#include <functional>
#include <memory>

namespace kh2coop {
struct DesyncBinding {
    std::string sessionId;
    std::array<std::uint64_t, 3> connections {};
    std::uint32_t localGeneration {}, attachedPid {};
    std::uint8_t localSlot {0xFF};
    bool admitted {false}, generationValid {false};
    bool operator==(const DesyncBinding&) const = default;
};
enum class DesyncCaptureState { Pending, Complete, Unavailable, Busy, Abandoned, Error };
struct DesyncCapturePoll {
    DesyncCaptureState state {DesyncCaptureState::Unavailable};
    std::uint32_t requestSeq {}, doneSeq {}, framesWritten {}, width {}, height {};
    std::int32_t nativeStatus {-1};
    std::uint32_t errorCode {};
    std::string error;
    // Optional observation fields: older synthetic providers leave availability
    // false. These never change CaptureChannel v1 or determine capture authority.
    bool mailboxAvailable {false}, submissionAvailable {false}, submitted {false}, rendererAvailable {false};
    std::uint32_t renderer {}, backbufferFormat {};
    bool expectedSequenceAvailable {false};
    std::uint32_t expectedSequence {};
};
// All methods run on the SINGLE collector worker. Begin owns the cooperative
// lease through Finish, including timeout. No provider may follow another path.
class DesyncCaptureProvider {
public:
    virtual ~DesyncCaptureProvider() = default;
    virtual DesyncCapturePoll Begin(std::uint32_t pid, const std::filesystem::path& output) = 0;
    virtual DesyncCapturePoll Poll() = 0;
    virtual void Finish() noexcept = 0;
};
std::unique_ptr<DesyncCaptureProvider> MakeDesyncCaptureProvider();
struct DesyncCollectorOptions {
    std::filesystem::path spoolRoot, injectLogPath;
    std::uint32_t captureTimeoutMs {5000}; // clamped to 1..8000 and request remainingMs
    // Test seam; empty uses the Windows CaptureChannel v1 adapter (unsupported elsewhere).
    std::function<std::unique_ptr<DesyncCaptureProvider>()> captureFactory;
};
struct DesyncCollected {
    DesyncCaptureDone done;
    std::array<std::vector<std::uint8_t>, 4> bytes;
    DesyncBinding before, after;
    std::filesystem::path localDirectory;
    bool identityChanged {false};
};
class DesyncCollector {
public:
    explicit DesyncCollector(DesyncCollectorOptions options);
    ~DesyncCollector(); // joins one bounded job; never called by Tick
    DesyncCollector(const DesyncCollector&) = delete;
    DesyncCollector& operator=(const DesyncCollector&) = delete;
    // Owner-thread API. Inputs copied/bounded in memory; no filesystem/channel I/O.
    // Root owns a <=128KiB runtime ring. Empty runtimeTail is explicit Unavailable.
    bool Start(const DesyncCaptureRequest&, const DesyncBinding&,
               std::string metadataBefore, std::string runtimeTail);
    void Tick(const DesyncBinding&, std::string metadataAfter = {});
    void Cancel(); // retires transport eligibility; worker retains local partial evidence
    bool Busy() const;
    std::unique_ptr<DesyncCollected> TakeCompleted(); // only current, uncancelled binding
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace kh2coop
