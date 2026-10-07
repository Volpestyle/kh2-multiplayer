// ============================================================================
// SaveGuard — see SaveGuard.hpp.
//
// The hooks sit on KernelBase's exports, which every caller reaches (the
// kernel32 exports forward there, and the CRT imports the api-set names that
// resolve there), plus kernel32's own CopyFileA/MoveFileExA. CopyFile goes
// to NtCreateFile without passing CreateFileW, so copies are hooked by name.
// A path is guarded when, case-insensitively, its full path contains the
// save folder name below; both slash styles are accepted.
// ============================================================================

#include "SaveGuard.hpp"
#include "kh2coop/SaveGuardReady.hpp"

#include <Windows.h>
#include <MinHook.h>

#include <atomic>
#include <cwchar>
#include <cstdio>
#include <cstring>
#include <cwctype>

namespace kh2coop {
namespace inject {
namespace saveguard {
namespace {

constexpr wchar_t kSaveFolder[] = L"my games\\kingdom hearts hd 1.5+2.5 remix";

LogFn g_log = nullptr;
std::atomic<std::uint32_t> g_blocked {0};

using CreateFileWFn = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD,
                                      DWORD, HANDLE);
using CreateFileAFn = HANDLE(WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD,
                                      DWORD, HANDLE);
using CreateFile2Fn = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, DWORD,
                                      LPCREATEFILE2_EXTENDED_PARAMETERS);
using DeleteFileWFn = BOOL(WINAPI*)(LPCWSTR);
using MoveFileExWFn = BOOL(WINAPI*)(LPCWSTR, LPCWSTR, DWORD);
using ReplaceFileWFn = BOOL(WINAPI*)(LPCWSTR, LPCWSTR, LPCWSTR, DWORD, LPVOID, LPVOID);
using DeleteFileAFn = BOOL(WINAPI*)(LPCSTR);
using CopyFileAFn = BOOL(WINAPI*)(LPCSTR, LPCSTR, BOOL);
using CopyFileWFn = BOOL(WINAPI*)(LPCWSTR, LPCWSTR, BOOL);
using CopyFileExWFn = BOOL(WINAPI*)(LPCWSTR, LPCWSTR, LPPROGRESS_ROUTINE, LPVOID, LPBOOL, DWORD);
using CopyFile2Fn = HRESULT(WINAPI*)(PCWSTR, PCWSTR, COPYFILE2_EXTENDED_PARAMETERS*);
using MoveFileExAFn = BOOL(WINAPI*)(LPCSTR, LPCSTR, DWORD);
using MoveFileWithProgressWFn = BOOL(WINAPI*)(LPCWSTR, LPCWSTR, LPPROGRESS_ROUTINE, LPVOID, DWORD);

CreateFileWFn g_createFileW = nullptr;
CreateFileAFn g_createFileA = nullptr;
CreateFile2Fn g_createFile2 = nullptr;
DeleteFileWFn g_deleteFileW = nullptr;
MoveFileExWFn g_moveFileExW = nullptr;
ReplaceFileWFn g_replaceFileW = nullptr;
DeleteFileAFn g_deleteFileA = nullptr;
CopyFileAFn g_copyFileA = nullptr;
CopyFileWFn g_copyFileW = nullptr;
CopyFileExWFn g_copyFileExW = nullptr;
CopyFile2Fn g_copyFile2 = nullptr;
MoveFileExAFn g_moveFileExA = nullptr;
MoveFileWithProgressWFn g_moveFileWithProgressW = nullptr;

// Lower-cased full path of an extra guarded directory, for the self-test
// (KH2COOP_SAVEGUARD_TEST_DIR). Empty when unset.
wchar_t g_testDir[MAX_PATH] = {};

void Lower(wchar_t* text) {
    for (; *text; ++text) {
        *text = *text == L'/' ? L'\\' : static_cast<wchar_t>(std::towlower(*text));
    }
}

// Relative names are resolved against the current directory first, so a
// save opened by a bare file name from inside the save folder still counts.
bool IsGuardedPath(LPCWSTR path) {
    if (!path) return false;
    wchar_t full[1024];
    const DWORD n = GetFullPathNameW(path, 1024, full, nullptr);
    if (n == 0 || n >= 1024) {
        wcsncpy_s(full, path, _TRUNCATE);
    }
    Lower(full);
    return std::wcsstr(full, kSaveFolder) != nullptr ||
           (g_testDir[0] && std::wcsstr(full, g_testDir) == full);
}

// ANSI paths (the game's own imports are mostly the A variants).
struct Wide {
    wchar_t text[1024] = {};
    explicit Wide(LPCSTR path) {
        if (path) MultiByteToWideChar(CP_ACP, 0, path, -1, text, 1023);
    }
    LPCWSTR get() const { return text[0] ? text : nullptr; }
};

bool IsWriteOpen(DWORD access, DWORD disposition) {
    constexpr DWORD kWriteAccess = GENERIC_WRITE | GENERIC_ALL | FILE_WRITE_DATA |
                                   FILE_APPEND_DATA | FILE_WRITE_ATTRIBUTES | FILE_WRITE_EA |
                                   DELETE | WRITE_DAC | WRITE_OWNER;
    return (access & kWriteAccess) != 0 || disposition != OPEN_EXISTING;
}

void Deny(const char* api, LPCWSTR path) {
    const std::uint32_t n = ++g_blocked;
    if (g_log && n <= 50) {
        char narrow[600] = {};
        WideCharToMultiByte(CP_UTF8, 0, path ? path : L"?", -1, narrow, sizeof(narrow) - 1,
                            nullptr, nullptr);
        g_log("[saveguard] blocked %s #%u: %s", api, n, narrow);
    }
    SetLastError(ERROR_ACCESS_DENIED);
}

// ---- Redirect -------------------------------------------------------------
// The save writer (0x145170) hands _wfopen_s's FILE* straight to libpng
// without a NULL check, so denying the open would likely crash the game
// mid-save. Write opens are redirected instead: to the same relative path
// under g_sandbox (<KH2COOP_LOG_DIR or %TEMP%>\save_sandbox_<pid>). An open
// that keeps the contents (r+b) first gets a copy of the original. Once a
// sandbox twin exists, reads go to it too, so the game sees what it "saved".

wchar_t g_sandbox[MAX_PATH] = {};
std::atomic<std::uint32_t> g_redirected {0};

// Full path of `path` and the offset where its part below the guarded root
// starts (the root is the save folder or the self-test directory).
bool GuardedTail(LPCWSTR path, wchar_t (&full)[1024], size_t& tail) {
    if (!path) return false;
    const DWORD n = GetFullPathNameW(path, 1024, full, nullptr);
    if (n == 0 || n >= 1024) return false;
    wchar_t lowered[1024];
    wcscpy_s(lowered, full);
    Lower(lowered);
    if (const wchar_t* hit = std::wcsstr(lowered, kSaveFolder)) {
        tail = (hit - lowered) + (sizeof(kSaveFolder) / sizeof(wchar_t) - 1);
        return true;
    }
    if (g_testDir[0] && std::wcsstr(lowered, g_testDir) == lowered) {
        tail = std::wcslen(g_testDir);
        return true;
    }
    return false;
}

// Sandbox twin of a guarded path, with its parent directories created.
bool SandboxTwin(LPCWSTR path, wchar_t (&twin)[1024]) {
    wchar_t full[1024];
    size_t tail = 0;
    if (!g_sandbox[0] || !GuardedTail(path, full, tail)) return false;
    if (swprintf_s(twin, L"%s%s", g_sandbox, full + tail) < 0) return false;
    for (wchar_t* p = twin + std::wcslen(g_sandbox) + 1; *p; ++p) {
        if (*p == L'\\' || *p == L'/') {
            const wchar_t keep = *p;
            *p = L'\0';
            CreateDirectoryW(twin, nullptr);
            *p = keep;
        }
    }
    return true;
}

bool Exists(LPCWSTR path) {
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

void LogRedirect(const char* api, LPCWSTR from, LPCWSTR to) {
    const std::uint32_t n = ++g_redirected;
    if (g_log && n <= 50) {
        char a[600] = {}, b[600] = {};
        WideCharToMultiByte(CP_UTF8, 0, from, -1, a, sizeof(a) - 1, nullptr, nullptr);
        WideCharToMultiByte(CP_UTF8, 0, to, -1, b, sizeof(b) - 1, nullptr, nullptr);
        g_log("[saveguard] redirected %s #%u: %s -> %s", api, n, a, b);
    }
}

// Decides where an open of `path` goes. Returns false to open `path`
// itself; true with `twin` filled to open the sandbox twin instead.
bool RouteOpen(const char* api, LPCWSTR path, DWORD access, DWORD disposition,
               wchar_t (&twin)[1024]) {
    twin[0] = L'\0';
    if (!IsGuardedPath(path)) return false;
    const bool write = IsWriteOpen(access, disposition);
    if (!SandboxTwin(path, twin)) {
        if (write) Deny(api, path);  // no sandbox: fail closed
        return write;
    }
    const bool twinExists = Exists(twin);
    if (!write && !twinExists) return false;  // plain read of the real save
    const bool keepsContents = disposition == OPEN_EXISTING || disposition == OPEN_ALWAYS;
    if (write && !twinExists && keepsContents && Exists(path)) {
        g_copyFileW(path, twin, TRUE);  // trampoline; the destination isn't guarded
    }
    if (write) LogRedirect(api, path, twin);
    return true;
}

HANDLE WINAPI HookedCreateFileW(LPCWSTR path, DWORD access, DWORD share,
                                LPSECURITY_ATTRIBUTES sa, DWORD disposition, DWORD flags,
                                HANDLE templ) {
    wchar_t twin[1024];
    if (RouteOpen("CreateFileW", path, access, disposition, twin)) {
        if (!twin[0] || !g_sandbox[0]) return INVALID_HANDLE_VALUE;
        return g_createFileW(twin, access, share, sa, disposition, flags, templ);
    }
    return g_createFileW(path, access, share, sa, disposition, flags, templ);
}

HANDLE WINAPI HookedCreateFileA(LPCSTR path, DWORD access, DWORD share,
                                LPSECURITY_ATTRIBUTES sa, DWORD disposition, DWORD flags,
                                HANDLE templ) {
    const Wide wide(path);
    wchar_t twin[1024];
    if (RouteOpen("CreateFileA", wide.get(), access, disposition, twin)) {
        if (!twin[0] || !g_sandbox[0]) return INVALID_HANDLE_VALUE;
        return g_createFileW(twin, access, share, sa, disposition, flags, templ);
    }
    return g_createFileA(path, access, share, sa, disposition, flags, templ);
}

HANDLE WINAPI HookedCreateFile2(LPCWSTR path, DWORD access, DWORD share, DWORD disposition,
                                LPCREATEFILE2_EXTENDED_PARAMETERS params) {
    wchar_t twin[1024];
    if (RouteOpen("CreateFile2", path, access, disposition, twin)) {
        if (!twin[0] || !g_sandbox[0]) return INVALID_HANDLE_VALUE;
        return g_createFile2(twin, access, share, disposition, params);
    }
    return g_createFile2(path, access, share, disposition, params);
}

using RemoveDirectoryWFn = BOOL(WINAPI*)(LPCWSTR);
using RemoveDirectoryAFn = BOOL(WINAPI*)(LPCSTR);
RemoveDirectoryWFn g_removeDirectoryW = nullptr;
RemoveDirectoryAFn g_removeDirectoryA = nullptr;

BOOL WINAPI HookedRemoveDirectoryW(LPCWSTR path) {
    if (IsGuardedPath(path)) {
        Deny("RemoveDirectoryW", path);
        return FALSE;
    }
    return g_removeDirectoryW(path);
}

BOOL WINAPI HookedRemoveDirectoryA(LPCSTR path) {
    const Wide wide(path);
    if (IsGuardedPath(wide.get())) {
        Deny("RemoveDirectoryA", wide.get());
        return FALSE;
    }
    return g_removeDirectoryA(path);
}

BOOL WINAPI HookedDeleteFileW(LPCWSTR path) {
    if (IsGuardedPath(path)) {
        Deny("DeleteFileW", path);
        return FALSE;
    }
    return g_deleteFileW(path);
}

BOOL WINAPI HookedMoveFileExW(LPCWSTR from, LPCWSTR to, DWORD flags) {
    if (IsGuardedPath(from) || IsGuardedPath(to)) {
        Deny("MoveFileExW", IsGuardedPath(to) ? to : from);
        return FALSE;
    }
    return g_moveFileExW(from, to, flags);
}

BOOL WINAPI HookedReplaceFileW(LPCWSTR replaced, LPCWSTR replacement, LPCWSTR backup,
                               DWORD flags, LPVOID excl, LPVOID reserved) {
    if (IsGuardedPath(replaced) || IsGuardedPath(replacement) || IsGuardedPath(backup)) {
        Deny("ReplaceFileW", replaced);
        return FALSE;
    }
    return g_replaceFileW(replaced, replacement, backup, flags, excl, reserved);
}

BOOL WINAPI HookedDeleteFileA(LPCSTR path) {
    const Wide wide(path);
    if (IsGuardedPath(wide.get())) {
        Deny("DeleteFileA", wide.get());
        return FALSE;
    }
    return g_deleteFileA(path);
}

// Copies are guarded by destination only: reading a save to copy it
// elsewhere is fine.
BOOL WINAPI HookedCopyFileA(LPCSTR from, LPCSTR to, BOOL failIfExists) {
    const Wide wide(to);
    if (IsGuardedPath(wide.get())) {
        Deny("CopyFileA", wide.get());
        return FALSE;
    }
    return g_copyFileA(from, to, failIfExists);
}

BOOL WINAPI HookedCopyFileW(LPCWSTR from, LPCWSTR to, BOOL failIfExists) {
    if (IsGuardedPath(to)) {
        Deny("CopyFileW", to);
        return FALSE;
    }
    return g_copyFileW(from, to, failIfExists);
}

BOOL WINAPI HookedCopyFileExW(LPCWSTR from, LPCWSTR to, LPPROGRESS_ROUTINE progress,
                              LPVOID data, LPBOOL cancel, DWORD flags) {
    if (IsGuardedPath(to)) {
        Deny("CopyFileExW", to);
        return FALSE;
    }
    return g_copyFileExW(from, to, progress, data, cancel, flags);
}

HRESULT WINAPI HookedCopyFile2(PCWSTR from, PCWSTR to, COPYFILE2_EXTENDED_PARAMETERS* params) {
    if (IsGuardedPath(to)) {
        Deny("CopyFile2", to);
        return HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED);
    }
    return g_copyFile2(from, to, params);
}

BOOL WINAPI HookedMoveFileExA(LPCSTR from, LPCSTR to, DWORD flags) {
    const Wide wideFrom(from), wideTo(to);
    if (IsGuardedPath(wideFrom.get()) || IsGuardedPath(wideTo.get())) {
        Deny("MoveFileExA", IsGuardedPath(wideTo.get()) ? wideTo.get() : wideFrom.get());
        return FALSE;
    }
    return g_moveFileExA(from, to, flags);
}

BOOL WINAPI HookedMoveFileWithProgressW(LPCWSTR from, LPCWSTR to, LPPROGRESS_ROUTINE progress,
                                        LPVOID data, DWORD flags) {
    if (IsGuardedPath(from) || IsGuardedPath(to)) {
        Deny("MoveFileWithProgressW", IsGuardedPath(to) ? to : from);
        return FALSE;
    }
    return g_moveFileWithProgressW(from, to, progress, data, flags);
}

template <typename Fn>
bool HookApi(const wchar_t* module, const char* name, void* detour, Fn* original) {
    MH_STATUS st = MH_CreateHookApi(module, name, detour, reinterpret_cast<void**>(original));
    if (st == MH_OK) {
        void* target = reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(module), name));
        st = MH_EnableHook(target);
    }
    if (st != MH_OK && g_log) g_log("  ERROR: save guard hook %s failed: %d", name, st);
    return st == MH_OK;
}

// With KH2COOP_SAVEGUARD_TEST_DIR set, try every guarded write path against
// that directory from inside the game process and log each outcome.
void SelfTest() {
    char dirA[MAX_PATH] = {};
    WideCharToMultiByte(CP_ACP, 0, g_testDir, -1, dirA, MAX_PATH - 1, nullptr, nullptr);
    wchar_t outside[MAX_PATH], target[MAX_PATH];
    char outsideA[MAX_PATH], targetA[MAX_PATH];
    GetTempPathW(MAX_PATH, outside);
    wcscat_s(outside, L"kh2coop_guard_src.tmp");
    WideCharToMultiByte(CP_ACP, 0, outside, -1, outsideA, MAX_PATH - 1, nullptr, nullptr);
    swprintf_s(target, L"%s\\probe.sav", g_testDir);
    sprintf_s(targetA, "%s\\probe.sav", dirA);
    HANDLE src = CreateFileW(outside, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (src != INVALID_HANDLE_VALUE) CloseHandle(src);
    // An existing "save" in the guarded dir, written through the unhooked
    // trampoline, so opening it r+b and deleting it would succeed unguarded.
    static constexpr char kProbe[] = "kh2coop guard probe";
    HANDLE probe = g_createFileW(target, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (probe != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(probe, kProbe, sizeof(kProbe), &written, nullptr);
        CloseHandle(probe);
    }

    // The probe is read through the trampoline, so a sandbox twin can't
    // stand in for it.
    auto probeIntact = [&] {
        char contents[64] = {};
        DWORD read = 0;
        HANDLE check = g_createFileW(target, GENERIC_READ, FILE_SHARE_READ, nullptr,
                                     OPEN_EXISTING, 0, nullptr);
        if (check == INVALID_HANDLE_VALUE) return false;
        ReadFile(check, contents, sizeof(contents), &read, nullptr);
        CloseHandle(check);
        return read == sizeof(kProbe) && std::memcmp(contents, kProbe, sizeof(kProbe)) == 0;
    };
    int reached = 0;
    auto report = [&](const char* api, bool callSucceeded) {
        const bool intact = probeIntact();
        if (!intact) ++reached;
        g_log("[saveguard] selftest %-24s %s", api,
              !intact ? "WROTE TARGET" : callSucceeded ? "redirected" : "denied");
    };
    static constexpr char kJunk[] = "overwritten";
    auto writeAndClose = [&](HANDLE h) {
        if (h == INVALID_HANDLE_VALUE) return false;
        DWORD written = 0;
        WriteFile(h, kJunk, sizeof(kJunk), &written, nullptr);
        CloseHandle(h);
        return true;
    };
    // First, before any twin exists: the game's r+b open must see the
    // original contents (copy-on-open) and write only the twin.
    using WfopenFn = int(__cdecl*)(FILE**, const wchar_t*, const wchar_t*);
    const auto* thunk = reinterpret_cast<const std::uint8_t*>(
        reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) + 0x471BC8);
    const bool viaExe = thunk[0] == 0xFF && thunk[1] == 0x25;
    auto wfopen = viaExe ? reinterpret_cast<WfopenFn>(const_cast<std::uint8_t*>(thunk))
                         : reinterpret_cast<WfopenFn>(
                               GetProcAddress(GetModuleHandleW(L"ucrtbase"), "_wfopen_s"));
    if (wfopen) {
        static const wchar_t* const kModes[] = {L"r+b", L"wb"};
        for (const wchar_t* mode : kModes) {
            FILE* fp = nullptr;
            const bool opened = wfopen(&fp, target, mode) == 0 && fp != nullptr;
            bool sawOriginal = false;
            if (fp) {
                char head[sizeof(kProbe)] = {};
                sawOriginal = mode[0] == L'r' && fread(head, 1, sizeof(head), fp) == sizeof(head) &&
                              std::memcmp(head, kProbe, sizeof(kProbe)) == 0;
                fseek(fp, 0, SEEK_SET);
                fwrite(kJunk, 1, sizeof(kJunk), fp);
                fclose(fp);
            }
            char label[48];
            sprintf_s(label, "%s _wfopen_s(%s)%s", viaExe ? "exe" : "ucrt",
                      mode[0] == L'w' ? "wb" : "r+b",
                      mode[0] == L'r' ? (sawOriginal ? " read orig" : " NO ORIG") : "");
            report(label, opened);
        }
    }
    report("CreateFileW(write)", writeAndClose(CreateFileW(target, GENERIC_WRITE, 0, nullptr,
                                                           CREATE_ALWAYS, 0, nullptr)));
    report("CreateFileA(write)", writeAndClose(CreateFileA(targetA, GENERIC_WRITE, 0, nullptr,
                                                           CREATE_ALWAYS, 0, nullptr)));
    report("CreateFile2(write)",
           writeAndClose(CreateFile2(target, GENERIC_WRITE, 0, CREATE_ALWAYS, nullptr)));
    report("CopyFileA", CopyFileA(outsideA, targetA, FALSE));
    report("CopyFileW", CopyFileW(outside, target, FALSE));
    report("CopyFileExW", CopyFileExW(outside, target, nullptr, nullptr, nullptr, 0));
    report("CopyFile2", SUCCEEDED(CopyFile2(outside, target, nullptr)));
    report("MoveFileExA(into)", MoveFileExA(outsideA, targetA, MOVEFILE_COPY_ALLOWED));
    report("MoveFileExW(into)", MoveFileExW(outside, target, MOVEFILE_COPY_ALLOWED));
    // The exe's DeleteFileA import resolves to kernel32!DeleteFileA.
    using DeleteAFn = BOOL(WINAPI*)(LPCSTR);
    auto deleteA = reinterpret_cast<DeleteAFn>(
        GetProcAddress(GetModuleHandleW(L"kernel32"), "DeleteFileA"));
    report("DeleteFileA", deleteA(targetA));
    report("DeleteFileW", DeleteFileW(target));
    report("RemoveDirectoryW", RemoveDirectoryW(g_testDir));
    HANDLE readBack = CreateFileW(outside, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                  0, nullptr);
    const bool readOk = readBack != INVALID_HANDLE_VALUE;
    if (readOk) CloseHandle(readBack);
    DeleteFileW(outside);
    char sandbox[MAX_PATH] = {};
    WideCharToMultiByte(CP_UTF8, 0, g_sandbox, -1, sandbox, MAX_PATH - 1, nullptr, nullptr);
    g_log("[saveguard] selftest done: %d call(s) reached the target, probe %s, read elsewhere %s, "
          "sandbox %s",
          reached, probeIntact() ? "intact" : "CHANGED", readOk ? "ok" : "FAILED", sandbox);
}

} // namespace

bool Install(LogFn log) {
    g_log = log;
    char testDir[MAX_PATH] = {};
    if (GetEnvironmentVariableA("KH2COOP_SAVEGUARD_TEST_DIR", testDir, MAX_PATH) > 0) {
        wchar_t wide[MAX_PATH] = {};
        MultiByteToWideChar(CP_ACP, 0, testDir, -1, wide, MAX_PATH - 1);
        if (GetFullPathNameW(wide, MAX_PATH, g_testDir, nullptr) == 0) g_testDir[0] = L'\0';
        Lower(g_testDir);
    }
    {
        char dir[MAX_PATH] = {};
        DWORD n = GetEnvironmentVariableA("KH2COOP_LOG_DIR", dir, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) n = GetTempPathA(MAX_PATH, dir);
        wchar_t wide[MAX_PATH] = {};
        MultiByteToWideChar(CP_ACP, 0, dir, -1, wide, MAX_PATH - 1);
        wchar_t joined[MAX_PATH] = {};
        swprintf_s(joined, L"%s\\save_sandbox_%lu", wide, GetCurrentProcessId());
        if (GetFullPathNameW(joined, MAX_PATH, g_sandbox, nullptr) == 0 ||
            !(CreateDirectoryW(g_sandbox, nullptr) || GetLastError() == ERROR_ALREADY_EXISTS) ||
            IsGuardedPath(g_sandbox)) {
            g_sandbox[0] = L'\0';  // no sandbox: write opens fail closed
        }
    }
    bool ok = true;
    ok &= HookApi(L"kernelbase", "CreateFileW", reinterpret_cast<void*>(&HookedCreateFileW), &g_createFileW);
    ok &= HookApi(L"kernelbase", "CreateFileA", reinterpret_cast<void*>(&HookedCreateFileA), &g_createFileA);
    ok &= HookApi(L"kernelbase", "CreateFile2", reinterpret_cast<void*>(&HookedCreateFile2), &g_createFile2);
    ok &= HookApi(L"kernelbase", "DeleteFileW", reinterpret_cast<void*>(&HookedDeleteFileW), &g_deleteFileW);
    ok &= HookApi(L"kernelbase", "MoveFileExW", reinterpret_cast<void*>(&HookedMoveFileExW), &g_moveFileExW);
    ok &= HookApi(L"kernelbase", "ReplaceFileW", reinterpret_cast<void*>(&HookedReplaceFileW), &g_replaceFileW);
    ok &= HookApi(L"kernelbase", "DeleteFileA", reinterpret_cast<void*>(&HookedDeleteFileA), &g_deleteFileA);
    ok &= HookApi(L"kernel32", "CopyFileA", reinterpret_cast<void*>(&HookedCopyFileA), &g_copyFileA);
    ok &= HookApi(L"kernelbase", "CopyFileW", reinterpret_cast<void*>(&HookedCopyFileW), &g_copyFileW);
    ok &= HookApi(L"kernelbase", "CopyFileExW", reinterpret_cast<void*>(&HookedCopyFileExW), &g_copyFileExW);
    ok &= HookApi(L"kernelbase", "CopyFile2", reinterpret_cast<void*>(&HookedCopyFile2), &g_copyFile2);
    ok &= HookApi(L"kernel32", "MoveFileExA", reinterpret_cast<void*>(&HookedMoveFileExA), &g_moveFileExA);
    ok &= HookApi(L"kernelbase", "MoveFileWithProgressW",
                  reinterpret_cast<void*>(&HookedMoveFileWithProgressW), &g_moveFileWithProgressW);
    ok &= HookApi(L"kernelbase", "RemoveDirectoryW", reinterpret_cast<void*>(&HookedRemoveDirectoryW),
                  &g_removeDirectoryW);
    ok &= HookApi(L"kernelbase", "RemoveDirectoryA", reinterpret_cast<void*>(&HookedRemoveDirectoryA),
                  &g_removeDirectoryA);
    if (g_log) {
        char sandbox[MAX_PATH] = {};
        WideCharToMultiByte(CP_UTF8, 0, g_sandbox, -1, sandbox, MAX_PATH - 1, nullptr, nullptr);
        if (!ok) {
            g_log("  ERROR: save guard incomplete; saves are NOT protected");
        } else if (sandbox[0]) {
            g_log("  Save guard installed: writes under My Games\\KINGDOM HEARTS HD 1.5+2.5 ReMIX "
                  "go to %s; deletes/moves/copies there are denied", sandbox);
        } else {
            g_log("  Save guard installed without a sandbox: all writes there are denied");
        }
    }
    if (ok && g_testDir[0] && g_log) SelfTest();
    return ok;
}

void AcknowledgeLaunch() {
    // Signal only after all initialization that could tear down MinHook has
    // succeeded. Otherwise a later failure could remove the guard after resume.
    HANDLE ready = OpenEventW(EVENT_MODIFY_STATE, FALSE,
        preinject::ReadyEventName(GetCurrentProcessId()).c_str());
    if (ready) {
        LARGE_INTEGER ack {};
        if (!QueryPerformanceCounter(&ack)) {
            if (g_log) g_log("[saveguard] ERROR: acknowledgement QPC unavailable pid=%lu", GetCurrentProcessId());
            CloseHandle(ready);
            return;
        }
        if (g_log) g_log("[saveguard] ack signalled pid=%lu qpc=%llu tickMs=%llu",
            GetCurrentProcessId(), static_cast<unsigned long long>(ack.QuadPart),
            static_cast<unsigned long long>(GetTickCount64()));
        // This line records the signal attempt. The launcher's successful wait
        // is the independent receipt that the signal was actually observed.
        if (!SetEvent(ready) && g_log)
            g_log("[saveguard] ERROR: SetEvent failed pid=%lu error=%lu", GetCurrentProcessId(), GetLastError());
        CloseHandle(ready);
    }
}

std::uint32_t BlockedCount() { return g_blocked.load(); }

} // namespace saveguard
} // namespace inject
} // namespace kh2coop
