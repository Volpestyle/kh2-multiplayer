#pragma once
#include "kh2coop/DesyncProtocol.hpp"
#include <memory>
#include <optional>
#include <string>

namespace kh2coop {
enum class DesyncCollectionStatus : std::uint8_t { Complete, Partial, Interrupted, StorageError };
struct DesyncCaptureResult {
    DesyncKey key;
    DesyncCollectionStatus status {DesyncCollectionStatus::Partial};
    std::string directory, manifestPath, error;
    bool manifestWritten {false};
};
struct DesyncCaptureStats {
    std::uint64_t started {0}, finalized {0}, suppressedCadence {0}, suppressedQuota {0};
    std::uint64_t extraTriggers {0}, extraTriggerOverflow {0}, rejected {0}, duplicateChunks {0};
    std::uint64_t storageErrors {0};
    std::uint64_t summaryWrites {0}, summaryStorageErrors {0}, summaryLostTriggers {0};
};
struct DesyncSuppressionResult {
    std::string sessionId, path, error;
    std::uint64_t revision {0};
    bool written {false};
};
// Network-thread methods only. An owned worker performs ALL filesystem I/O.
// Trigger persistence completes before TakeRequest exposes the peer request.
// One active report, bounded storage/metadata; destructor joins its own worker.
class DesyncCapture {
public:
    explicit DesyncCapture(std::string outputRoot);
    ~DesyncCapture();
    DesyncCapture(const DesyncCapture&) = delete;
    DesyncCapture& operator=(const DesyncCapture&) = delete;
    bool Trigger(const DesyncCaptureRequest&, std::string relayLog, std::uint64_t now,
                 std::uint64_t relayLogSourceBytes = 0);
    void Pump(std::uint64_t now);
    std::optional<DesyncCaptureRequest> TakeRequest();
    bool Chunk(std::uint64_t authenticatedConnection, const DesyncArtifactChunk&);
    bool Done(std::uint64_t authenticatedConnection, const DesyncCaptureDone&);
    void PeerLeft(std::uint64_t connectionId);
    void Interrupt(const std::string& reason); // host loss/session end/relay stop
    void Reject(std::uint64_t authenticatedConnection, const std::string& reason);
    void Shutdown(); // outside the ENet pump: join only this module's owned worker
    std::optional<DesyncCaptureResult> TakeFinalized();
    // Separate mutable per-session evidence; never changes a final report.
    std::optional<DesyncSuppressionResult> TakeSuppressionResult();
    const DesyncCaptureStats& Stats() const noexcept;
    bool Active() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace kh2coop
