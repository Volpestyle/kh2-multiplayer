#pragma once
#include "kh2coop/Protocol.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace kh2coop {
inline constexpr std::uint32_t DESYNC_CHUNK_BYTES = 16u * 1024u;
inline constexpr std::uint32_t DESYNC_LOG_BYTES = 512u * 1024u; // aggregate kinds 0..2 / peer
inline constexpr std::uint32_t DESYNC_PNG_BYTES = 16u * 1024u * 1024u;
inline constexpr std::uint64_t DESYNC_DEADLINE_MS = 30000;
inline constexpr std::uint64_t DESYNC_CADENCE_MS = 60000;
inline constexpr std::uint32_t DESYNC_SESSION_QUOTA = 8;
inline constexpr std::size_t DESYNC_EXTRA_TRIGGERS = 16;
inline constexpr std::size_t DESYNC_MAX_CHUNKS_PER_PEER = 2048;

struct DesyncKey {
    std::string sessionId; // exactly 32 lowercase hexadecimal characters
    std::uint64_t reportId {0}; // nonzero, never reused during relay lifetime
    bool operator==(const DesyncKey&) const = default;
};
enum class DesyncArtifactKind : std::uint8_t { Metadata=0, RuntimeLog=1, InjectLog=2, ScreenshotPng=3 };
enum class DesyncArtifactStatus : std::uint8_t {
    Complete=0, Unavailable=1, ReadError=2, Timeout=3, Interrupted=4, Oversize=5
};
struct DesyncCaptureRequest {
    DesyncKey key;
    std::array<std::uint64_t, 3> connections {}; // fixed slot-indexed expected roster; zero=absent
    std::uint32_t epoch {0};
    std::uint8_t divergedSlot {0xFF}, fields {0};
    StateHash hostHash {}, clientHash {};
    std::uint64_t hostReceiptSeq {0}, clientReceiptSeq {0};
    std::uint64_t hostReceiptMs {0}, clientReceiptMs {0};
    std::uint64_t comparisonSeq {0}, triggerMs {0}, deadlineMs {0}; // relay clock, NOT native frame
    std::uint32_t remainingMs {30000}; // recomputed at relay request emission; peer clocks are independent
};
struct DesyncArtifactDescriptor {
    DesyncArtifactKind kind {DesyncArtifactKind::Metadata};
    DesyncArtifactStatus status {DesyncArtifactStatus::Unavailable};
    std::uint32_t bytes {0};
    std::array<std::uint8_t, 32> sha256 {};
    std::uint64_t sourceBytes {0}, rangeBegin {0}, rangeEnd {0};
    std::uint64_t startedMs {0}, finishedMs {0}; // source peer clock, not comparable across peers
    bool truncated {false};
    std::uint32_t errorCode {0};
    std::string sourceLabel; // <=128 bytes, descriptive only; NEVER a relay path
    std::string error;       // <=256 bytes
};
struct DesyncArtifactChunk {
    DesyncKey key;
    std::uint64_t connectionId {0};
    DesyncArtifactKind kind {DesyncArtifactKind::Metadata};
    std::uint32_t offset {0};
    std::vector<std::uint8_t> bytes; // 1..16KiB; ordered contiguous offsets, exact duplicates allowed
};
struct DesyncCaptureDone {
    DesyncKey key;
    std::uint64_t connectionId {0};
    std::array<DesyncArtifactDescriptor, 4> artifacts {}; // kinds 0,1,2,3 exactly once, in order
};
// Exact raw-byte SHA-256 shared by collector and relay; no file I/O.
std::array<std::uint8_t, 32> desyncSha256(std::span<const std::uint8_t> bytes);
std::string desyncDigestHex(const std::array<std::uint8_t, 32>& digest);
// Bounded PNG structure/CRC only, not decompression or native capture provenance.
bool desyncPngStructure(std::span<const std::uint8_t> bytes) noexcept;
} // namespace kh2coop
