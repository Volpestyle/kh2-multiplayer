#pragma once
#include <cstdint>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace kh2coop {
enum class CaptureLeaseStatus { Acquired, Busy, Abandoned, Error };
// Cooperative caller serialization only. The mailbox's unfinished sequence is
// a separate fence and MUST be checked after acquisition, including after timeout.
class CaptureLease {
public:
    CaptureLease() = default;
    ~CaptureLease() { Release(); }
    CaptureLease(const CaptureLease&) = delete;
    CaptureLease& operator=(const CaptureLease&) = delete;
    CaptureLeaseStatus Acquire(std::uint32_t pid, std::uint32_t waitMs = 0) noexcept {
        Release();
        error_ = 0;
#ifdef _WIN32
        if (!pid || waitMs > 1000) { error_ = ERROR_INVALID_PARAMETER; return CaptureLeaseStatus::Error; }
        try {
            const auto name = L"Local\\kh2coop_capture_caller_" + std::to_wstring(pid);
            handle_ = CreateMutexW(nullptr, FALSE, name.c_str());
            if (!handle_) { error_ = GetLastError(); return CaptureLeaseStatus::Error; }
            const auto result = WaitForSingleObject(handle_, waitMs);
            if (result == WAIT_OBJECT_0) { owned_ = true; return CaptureLeaseStatus::Acquired; }
            if (result == WAIT_ABANDONED) {
                owned_ = true;
                // Report abandoned ownership explicitly, never write on this acquisition.
                Release();
                return CaptureLeaseStatus::Abandoned;
            }
            error_ = result == WAIT_FAILED ? GetLastError() : 0;
            Release();
            return result == WAIT_TIMEOUT ? CaptureLeaseStatus::Busy : CaptureLeaseStatus::Error;
        } catch (...) { Release(); error_ = ERROR_NOT_ENOUGH_MEMORY; return CaptureLeaseStatus::Error; }
#else
        (void)pid; (void)waitMs;
        return CaptureLeaseStatus::Error;
#endif
    }
    void Release() noexcept {
#ifdef _WIN32
        if (owned_) ReleaseMutex(handle_);
        owned_ = false;
        if (handle_) CloseHandle(handle_);
        handle_ = nullptr;
#endif
    }
    std::uint32_t Error() const noexcept { return error_; }
private:
    std::uint32_t error_ {};
#ifdef _WIN32
    HANDLE handle_ {};
    bool owned_ {};
#endif
};
} // namespace kh2coop
