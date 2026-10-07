// Offline control DLL: actual production SaveGuard, no game hooks or Steam.
#include <Windows.h>
#include <MinHook.h>
#include "SaveGuard.hpp"
#include <cstdio>
#include <cstdarg>

FILE* g_guardLog = nullptr;
void GuardLog(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(g_guardLog, format, args);
    va_end(args);
    fputc('\n', g_guardLog);
    fflush(g_guardLog);
}

HANDLE WINAPI UnusedCreateFileW(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE) {
    return INVALID_HANDLE_VALUE; // registered but never enabled or invoked
}

DWORD WINAPI InstallGuard(void*) {
    wchar_t directory[MAX_PATH] {}, path[MAX_PATH] {};
    if (!GetEnvironmentVariableW(L"KH2COOP_LOG_DIR", directory, MAX_PATH)) return 3;
    swprintf_s(path, L"%s\\preinject_guard_%lu.log", directory, GetCurrentProcessId());
    if (_wfopen_s(&g_guardLog, path, L"w") != 0 || !g_guardLog) return 3;
    if (MH_Initialize() != MH_OK) return 1;
    wchar_t fault[2] {};
    if (GetEnvironmentVariableW(L"KH2COOP_PREINJECT_TEST_FAIL_GUARD", fault, 2)) {
        // Force the real guard's CreateFileW hook installation to fail, while
        // its remaining hooks still install. An incomplete guard must not ack.
        void* original = nullptr;
        if (MH_CreateHookApi(L"kernelbase", "CreateFileW",
                            reinterpret_cast<void*>(&UnusedCreateFileW), &original) != MH_OK)
            return 2;
    }
    if (!kh2coop::inject::saveguard::Install(&GuardLog)) return 1;
    kh2coop::inject::saveguard::AcknowledgeLaunch();
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        HANDLE thread = CreateThread(nullptr, 0, InstallGuard, nullptr, 0, nullptr);
        if (!thread) return FALSE;
        CloseHandle(thread);
    }
    return TRUE;
}
