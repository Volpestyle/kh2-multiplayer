#pragma once
#include "CoopHudState.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <atomic>

namespace kh2coop::inject::hud {

static_assert(std::atomic<std::uint64_t>::is_always_lock_free && std::atomic<bool>::is_always_lock_free);

// Process-lifetime storage. One game-thread writer, Present-gated reader.
// Each attempt retires the old revision before trying the lock. Invalidation
// therefore cannot be lost behind a reader or leave old identity/HP on screen.
// On contention prefer unavailable over retaining a possibly superseded row.
class Mailbox {
public:
    bool TryPublish(const Snapshot& value) noexcept {
        const auto ticket = revision_.fetch_add(1, std::memory_order_acq_rel) + 1;
        if (stopped_.load(std::memory_order_acquire) || !TryAcquireSRWLockExclusive(&lock_)) return false;
        const bool current = revision_.load(std::memory_order_acquire) == ticket &&
            !stopped_.load(std::memory_order_acquire);
        if (current) { value_ = value; valueRevision_ = ticket; }
        ReleaseSRWLockExclusive(&lock_);
        return current;
    }
    void Invalidate() noexcept { revision_.fetch_add(1, std::memory_order_acq_rel); }
    void Stop() noexcept {
        stopped_.store(true, std::memory_order_release);
        Invalidate(); // Cannot be reopened by a late game-thread callback.
    }
    bool TryCopy(std::uint64_t nowMs, Snapshot& out) noexcept {
        out = {};
        const auto ticket = revision_.load(std::memory_order_acquire);
        if (stopped_.load(std::memory_order_acquire) || !TryAcquireSRWLockShared(&lock_)) return false;
        const auto copy = value_;
        const bool matched = valueRevision_ == ticket;
        ReleaseSRWLockShared(&lock_);
        if (!matched || revision_.load(std::memory_order_acquire) != ticket ||
            stopped_.load(std::memory_order_acquire) || !Fresh(copy, nowMs)) return false;
        out = copy;
        return true;
    }
private:
    friend struct MailboxTestAccess;
    SRWLOCK lock_ = SRWLOCK_INIT;
    Snapshot value_ {};
    std::uint64_t valueRevision_ {};
    std::atomic<std::uint64_t> revision_ {1};
    std::atomic<bool> stopped_ {false};
};
} // namespace kh2coop::inject::hud
