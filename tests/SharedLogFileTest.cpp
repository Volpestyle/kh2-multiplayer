// The Steam broker log must be readable by the rig while the game holds it open (VUH-1493 probe 03:
// the runner got EACCES polling a log opened with _wfopen_s, which denies all sharing).
#include "SharedLogFile.hpp"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>

static int g_checks = 0, g_failures = 0;
#define CHECK(name, cond) do { ++g_checks; if (cond) std::printf("PASS %s\n", name); else { ++g_failures; std::printf("FAIL %s\n", name); } } while (0)

static std::wstring TempPath(const wchar_t* leaf) {
    wchar_t dir[MAX_PATH]{};
    GetTempPathW(MAX_PATH, dir);
    std::wstring p = std::wstring(dir) + L"kh2coop_sharedlog_" + std::to_wstring(GetCurrentProcessId()) + L"_" + leaf;
    DeleteFileW(p.c_str());
    return p;
}

// What a reader such as Python's open(..., 'rb') does: GENERIC_READ, sharing read and write.
static std::string ReadWhileOpen(const std::wstring& path, bool* opened) {
    const HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    *opened = h != INVALID_HANDLE_VALUE;
    if (!*opened) return {};
    char buf[256]{}; DWORD n = 0;
    ReadFile(h, buf, sizeof(buf) - 1, &n, nullptr);
    CloseHandle(h);
    return std::string(buf, n);
}

int main() {
    const std::wstring path = TempPath(L"broker.log");
    FILE* log = kh2coop::CreateSharedLogW(path.c_str());
    CHECK("creates a new log", log != nullptr);
    if (!log) return 1;
    std::fprintf(log, "[steam-broker] ready\n");

    bool opened = false;
    const std::string seen = ReadWhileOpen(path, &opened);
    CHECK("a reader can open the log while the writer holds it", opened);
    CHECK("unbuffered: the reader sees the line at once (text mode CRLF)", seen == "[steam-broker] ready\r\n");

    // (A _wfopen_s reader would itself ask for no sharing and so cannot coexist with any writer; readers use _wfsopen.)
    FILE* shared = _wfsopen(path.c_str(), L"rb", _SH_DENYNO);
    CHECK("a CRT reader (_wfsopen rb, _SH_DENYNO) can open it", shared != nullptr);
    if (shared) std::fclose(shared);

    const HANDLE w2 = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK("a second writer is refused (deny-write kept)", w2 == INVALID_HANDLE_VALUE && GetLastError() == ERROR_SHARING_VIOLATION);
    if (w2 != INVALID_HANDLE_VALUE) CloseHandle(w2);

    CHECK("a second CreateSharedLogW while the first is open fails", kh2coop::CreateSharedLogW(path.c_str()) == nullptr);
    std::fclose(log);
    FILE* again = kh2coop::CreateSharedLogW(path.c_str());
    CHECK("create-new: an existing (closed) log is never reopened or truncated", again == nullptr);
    if (again) std::fclose(again);
    bool reopened = false;
    CHECK("the closed log keeps its content", ReadWhileOpen(path, &reopened) == "[steam-broker] ready\r\n" && reopened);

    // The old open, for contrast: _wfopen_s(..., "wx") denies every reader while it is held.
    const std::wstring old = TempPath(L"old.log");
    FILE* legacy = nullptr;
    if (_wfopen_s(&legacy, old.c_str(), L"wx") == 0 && legacy) {
        bool legacyOpened = true;
        ReadWhileOpen(old, &legacyOpened);
        CHECK("control: _wfopen_s wx denies a concurrent reader (the probe 03 failure)", !legacyOpened);
        std::fclose(legacy);
    } else {
        CHECK("control: _wfopen_s wx opened", false);
    }
    DeleteFileW(path.c_str());
    DeleteFileW(old.c_str());
    std::printf("RESULT %d checks %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
