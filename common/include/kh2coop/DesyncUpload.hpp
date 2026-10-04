#pragma once
#include "kh2coop/DesyncProtocol.hpp"
#include <algorithm>
#include <limits>
#include <utility>

namespace kh2coop {
enum class DesyncUploadState {
    Idle, Sending, Complete, IdentityChanged, Deadline, InvalidArtifact, SendFailed, Cancelled
};

// Owner-thread coordinator. No files, sleeps or hashing in the network pump.
// The caller supplies a deadline on its own monotonic clock, established when
// the relay request arrived; finishing collection never grants extra time.
class DesyncUpload {
public:
    bool Begin(DesyncCaptureDone done, std::array<std::vector<std::uint8_t>, 4> bytes,
               std::uint64_t deadlineMs) {
        if (state_ == DesyncUploadState::Sending) return false;
        std::uint64_t logs = 0;
        if (!done.key.reportId || done.key.sessionId.size() != 32 || !done.connectionId || !deadlineMs)
            return fail(DesyncUploadState::InvalidArtifact);
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            const auto& d = done.artifacts[i];
            if (static_cast<std::size_t>(d.kind) != i || d.bytes != bytes[i].size() ||
                (i == 3 && bytes[i].size() > DESYNC_PNG_BYTES))
                return fail(DesyncUploadState::InvalidArtifact);
            if (i < 3) logs += bytes[i].size();
        }
        if (logs > DESYNC_LOG_BYTES) return fail(DesyncUploadState::InvalidArtifact);
        done_ = std::move(done);
        bytes_ = std::move(bytes);
        deadlineMs_ = deadlineMs;
        nextPumpMs_ = lastNowMs_ = 0;
        artifact_ = offset_ = 0;
        state_ = DesyncUploadState::Sending;
        return true;
    }

    template<class SendChunk, class SendDone>
    DesyncUploadState Tick(std::uint64_t nowMs, const std::string& sessionId,
                           std::uint64_t connectionId, bool admitted,
                           SendChunk&& sendChunk, SendDone&& sendDone) {
        if (state_ != DesyncUploadState::Sending) return state_;
        nowMs = std::max(nowMs, lastNowMs_);
        lastNowMs_ = nowMs;
        if (!admitted || sessionId != done_.key.sessionId || connectionId != done_.connectionId) {
            fail(DesyncUploadState::IdentityChanged);
            return state_;
        }
        if (nowMs >= deadlineMs_) {
            fail(DesyncUploadState::Deadline);
            return state_;
        }
        if (nowMs < nextPumpMs_) return state_;
        nextPumpMs_ = nowMs > std::numeric_limits<std::uint64_t>::max() - 16
                    ? std::numeric_limits<std::uint64_t>::max() : nowMs + 16;
        // At most 64KiB each 16ms. A delayed tick does not accumulate credits.
        for (unsigned n = 0; n < 4; ++n) {
            skipEmpty();
            if (artifact_ == bytes_.size()) break;
            const auto& source = bytes_[artifact_];
            const auto size = std::min<std::size_t>(DESYNC_CHUNK_BYTES, source.size() - offset_);
            DesyncArtifactChunk chunk;
            chunk.key = done_.key;
            chunk.connectionId = done_.connectionId;
            chunk.kind = static_cast<DesyncArtifactKind>(artifact_);
            chunk.offset = static_cast<std::uint32_t>(offset_);
            chunk.bytes.assign(source.begin() + offset_, source.begin() + offset_ + size);
            if (!sendChunk(chunk)) {
                fail(DesyncUploadState::SendFailed);
                return state_;
            }
            offset_ += size;
        }
        skipEmpty();
        if (artifact_ == bytes_.size()) {
            if (!sendDone(done_)) fail(DesyncUploadState::SendFailed);
            else fail(DesyncUploadState::Complete);
        }
        return state_;
    }
    void Cancel() { if (state_ == DesyncUploadState::Sending) fail(DesyncUploadState::Cancelled); }
    [[nodiscard]] DesyncUploadState State() const { return state_; }
private:
    void skipEmpty() {
        while (artifact_ < bytes_.size() && offset_ == bytes_[artifact_].size()) {
            ++artifact_; offset_ = 0;
        }
    }
    bool fail(DesyncUploadState state) {
        state_ = state;
        for (auto& b : bytes_) std::vector<std::uint8_t>().swap(b);
        return false;
    }
    DesyncUploadState state_ {DesyncUploadState::Idle};
    DesyncCaptureDone done_;
    std::array<std::vector<std::uint8_t>, 4> bytes_;
    std::uint64_t deadlineMs_ {}, nextPumpMs_ {}, lastNowMs_ {};
    std::size_t artifact_ {}, offset_ {};
};
} // namespace kh2coop
