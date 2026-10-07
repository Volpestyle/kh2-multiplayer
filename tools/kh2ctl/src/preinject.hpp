#pragma once
#include <Windows.h>
#include <filesystem>
#include <string>
#include "kh2coop/SaveGuardReady.hpp"

namespace kh2coop::preinject {

struct Receipt {
    ULONGLONG ackWaitMs {0};
    bool ackObserved {false};
    DWORD resumePrevCount {0};
    LONGLONG resumedAtTick {0}; // QPC ticks sampled immediately after ResumeThread
};

// The caller must create this process with CREATE_SUSPENDED. Until release,
// every failure (including exceptions) terminates only this owned child.
class Child {
public:
    explicit Child(PROCESS_INFORMATION info) : info_(info) {}
    Child(const Child&) = delete;
    Child& operator=(const Child&) = delete;
    ~Child() {
        if (!released_) {
            TerminateProcess(info_.hProcess, 1);
            WaitForSingleObject(info_.hProcess, 5000);
        }
        if (ready_) CloseHandle(ready_);
        CloseHandle(info_.hThread);
        CloseHandle(info_.hProcess);
    }

    template<class Inject>
    std::string ProtectAndResume(Inject inject, DWORD timeoutMs) {
        ready_ = CreateEventW(nullptr, TRUE, FALSE,
                             ReadyEventName(info_.dwProcessId).c_str());
        const DWORD error = GetLastError();
        if (!ready_ || error == ERROR_ALREADY_EXISTS)
            return "Cannot create a fresh save-guard acknowledgement event";
        const auto injectionError = inject(info_.dwProcessId);
        if (!injectionError.empty()) return injectionError;
        HANDLE waits[] = {ready_, info_.hProcess};
        const ULONGLONG waitStarted = GetTickCount64();
        const DWORD waited = WaitForMultipleObjects(2, waits, FALSE, timeoutMs);
        receipt_.ackWaitMs = GetTickCount64() - waitStarted;
        if (waited != WAIT_OBJECT_0)
            return "Save guard did not acknowledge successful installation before resume";
        receipt_.ackObserved = true;
        // A count other than one violates the suspended-launch contract.
        receipt_.resumePrevCount = ResumeThread(info_.hThread);
        LARGE_INTEGER resumed {};
        if (!QueryPerformanceCounter(&resumed))
            return "Could not timestamp the protected main-thread resume";
        receipt_.resumedAtTick = resumed.QuadPart;
        if (receipt_.resumePrevCount != 1)
            return "Could not resume the protected main thread";
        return {};
    }

    void Release() { released_ = true; }
    const Receipt& Evidence() const { return receipt_; }

private:
    PROCESS_INFORMATION info_ {};
    HANDLE ready_ {nullptr};
    bool released_ {false};
    Receipt receipt_ {};
};

// Shared by normal injection and suspended launch. On timeout the target may
// still be reading the path; retain that allocation until process exit.
inline std::string InjectDll(DWORD pid, const std::filesystem::path& dll) {
    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION |
        PROCESS_VM_WRITE | PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!process) return "OpenProcess failed: " + std::to_string(GetLastError());
    const auto path = dll.wstring();
    const SIZE_T bytes = (path.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(process, nullptr, bytes,
                                  MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    std::string error;
    bool pending = false;
    if (!remote) {
        error = "VirtualAllocEx failed: " + std::to_string(GetLastError());
    } else if (!WriteProcessMemory(process, remote, path.c_str(), bytes, nullptr)) {
        error = "WriteProcessMemory failed: " + std::to_string(GetLastError());
    } else {
        auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
            GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
        HANDLE thread = CreateRemoteThread(process, nullptr, 0, loadLibrary, remote, 0, nullptr);
        if (!thread) {
            error = "CreateRemoteThread failed: " + std::to_string(GetLastError());
        } else {
            pending = WaitForSingleObject(thread, 15000) != WAIT_OBJECT_0;
            if (pending) {
                error = "LoadLibraryW did not return within 15 s";
            } else {
                DWORD moduleLow = 0;
                if (!GetExitCodeThread(thread, &moduleLow) || moduleLow == 0)
                    error = "LoadLibraryW returned NULL in target";
            }
            CloseHandle(thread);
        }
    }
    if (remote && !pending) VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    CloseHandle(process);
    return error;
}
} // namespace kh2coop::preinject
