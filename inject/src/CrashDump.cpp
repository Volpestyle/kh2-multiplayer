// ============================================================================
// CrashDump — see CrashDump.hpp.
// ============================================================================

#include "CrashDump.hpp"

#include <Windows.h>
#include <DbgHelp.h>
#include <MinHook.h>

#include <cstdio>

namespace kh2coop {
namespace inject {
namespace crashdump {
namespace {

LogFn g_log = nullptr;
LPTOP_LEVEL_EXCEPTION_FILTER g_next = nullptr;  // the game's filter, if any
volatile LONG g_dumping = 0;

using SetFilterFn = LPTOP_LEVEL_EXCEPTION_FILTER(WINAPI*)(LPTOP_LEVEL_EXCEPTION_FILTER);
SetFilterFn g_origSetFilter = nullptr;

LONG WINAPI OnUnhandled(EXCEPTION_POINTERS* info) {
    // One dump per process, even if several threads fault at once.
    if (InterlockedExchange(&g_dumping, 1) == 0) {
        char dir[MAX_PATH] = ".";
        const DWORD n = GetEnvironmentVariableA("KH2COOP_LOG_DIR", dir, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) strcpy_s(dir, ".");
        char path[MAX_PATH];
        sprintf_s(path, "%s\\kh2coop_crash_%lu.dmp", dir, GetCurrentProcessId());
        const DWORD code = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0;
        const void* address =
            info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionAddress : nullptr;
        HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        BOOL written = FALSE;
        if (file != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION mei {GetCurrentThreadId(), info, FALSE};
            const auto type = static_cast<MINIDUMP_TYPE>(
                MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo |
                MiniDumpWithUnloadedModules);
            written = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type,
                                        info ? &mei : nullptr, nullptr, nullptr);
            CloseHandle(file);
        }
        if (g_log) {
            g_log("[crash] unhandled exception 0x%08lX at %p; minidump %s: %s", code, address,
                  written ? "written" : "FAILED", path);
        }
    }
    return g_next ? g_next(info) : EXCEPTION_CONTINUE_SEARCH;
}

// Keeps our filter on top: whatever the game installs becomes the next link.
LPTOP_LEVEL_EXCEPTION_FILTER WINAPI HookedSetFilter(LPTOP_LEVEL_EXCEPTION_FILTER filter) {
    if (filter == &OnUnhandled) return g_origSetFilter(filter);
    LPTOP_LEVEL_EXCEPTION_FILTER previous = g_next;
    g_next = filter;
    if (g_log) g_log("  Crash dump: chained the game's exception filter %p", filter);
    return previous;
}

} // namespace

bool Install(LogFn log) {
    g_log = log;
    g_next = SetUnhandledExceptionFilter(&OnUnhandled);
    MH_STATUS st = MH_CreateHookApi(L"kernelbase", "SetUnhandledExceptionFilter",
                                    reinterpret_cast<void*>(&HookedSetFilter),
                                    reinterpret_cast<void**>(&g_origSetFilter));
    if (st == MH_OK) {
        st = MH_EnableHook(reinterpret_cast<void*>(
            GetProcAddress(GetModuleHandleW(L"kernelbase"), "SetUnhandledExceptionFilter")));
    }
    if (g_log) {
        g_log(st == MH_OK ? "  Crash dump filter installed"
                          : "  WARNING: crash dump filter installed, but the game may replace it (%d)",
              st);
    }
    return st == MH_OK;
}

} // namespace crashdump
} // namespace inject
} // namespace kh2coop
