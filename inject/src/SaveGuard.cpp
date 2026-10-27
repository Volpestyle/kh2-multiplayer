// ============================================================================
// SaveGuard — see SaveGuard.hpp.
//
// The hooks sit on KernelBase's exports, which every caller reaches (the
// kernel32 exports forward there, and the CRT imports the api-set names that
// resolve there), plus kernel32's own CopyFileA/MoveFileExA. CopyFile goes
// to NtCreateFile without passing CreateFileW, so copies are hooked by name.
// A path is guarded when, case-insensitively, its full path contains the
// save folder name below; both slash styles are accepted.
//
// Installed KH2ReSource assets are guarded the same way (VUH-2775). The
// native cipher movie (Axa::CCipherMovie, 0x13DA10) decrypts a movie's first
// 256 bytes in place before Media Foundation opens it, and a restore thread
// (0x13EDC0) writes the encrypted header back after playback. A read-only
// open would skip the decrypt and the restore would retry forever, so the
// redirect below gives the game a sandbox copy to decrypt and play instead.
// ============================================================================

#include "SaveGuard.hpp"
#include "kh2coop/SaveGuardReady.hpp"

#include <Windows.h>
#include <MinHook.h>

#include <algorithm>
#include <atomic>
#include <cwchar>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <string>
#include <vector>

namespace kh2coop {
namespace inject {
namespace saveguard {
namespace {

constexpr wchar_t kSaveFolder[] = L"my games\\kingdom hearts hd 1.5+2.5 remix";
// Installed asset root, matched as a whole path component; its sandbox twins
// live under kAssetTwins, a name that is itself unguarded.
constexpr wchar_t kAssetFolder[] = L"\\kh2resource";
constexpr wchar_t kAssetTwins[] = L"\\install-assets\\";

// End of the first "\kh2resource" component in a lowered full path (pointing
// at its following separator or at the terminator), or nullptr. A sibling
// such as "\kh2resource-other" doesn't match.
const wchar_t* AssetRootEnd(const wchar_t* lowered) {
    constexpr size_t n = sizeof(kAssetFolder) / sizeof(wchar_t) - 1;
    for (const wchar_t* hit = std::wcsstr(lowered, kAssetFolder); hit; hit = std::wcsstr(hit + 1, kAssetFolder)) {
        if (hit[n] == L'\\' || hit[n] == L'\0') return hit + n;
    }
    return nullptr;
}

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

// The final path of `path`, lower-cased with '\' separators: 8.3 names
// expanded, junctions and symlinks followed, a "\\?\" prefix dropped. A part
// that doesn't exist yet is resolved through its nearest existing ancestor
// and appended as spelled. Only drive-letter paths are opened (never a device
// such as a HID or a pipe), through the unhooked CreateFileW, with no access.
// False when the path can't be resolved; callers then use its spelling.
bool ResolvedLower(LPCWSTR path, wchar_t (&out)[1024]) {
    if (!path) return false;
    wchar_t full[1024];
    const DWORD n = GetFullPathNameW(path, 1024, full, nullptr);
    if (n == 0 || n >= 1024) return false;
    const wchar_t* body = std::wcsncmp(full, L"\\\\?\\", 4) == 0 ? full + 4 : full;
    if (!std::iswalpha(body[0]) || body[1] != L':' || body[2] != L'\\') return false;
    const auto open = g_createFileW ? g_createFileW : &CreateFileW;  // before Install hooks, the real one
    std::wstring prefix(body), rest;
    for (int depth = 0; depth < 64; ++depth) {
        HANDLE file = open(prefix.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            wchar_t final[1024];
            const DWORD length = GetFinalPathNameByHandleW(file, final, 1024, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
            CloseHandle(file);
            if (length == 0 || length >= 1024) return false;
            std::wstring result = final;
            if (result.rfind(L"\\\\?\\UNC\\", 0) == 0) result = L"\\\\" + result.substr(8);
            else if (result.rfind(L"\\\\?\\", 0) == 0) result = result.substr(4);
            result += rest;
            if (result.size() >= 1024) return false;
            wcscpy_s(out, result.c_str());
            Lower(out);
            return true;
        }
        const DWORD error = GetLastError();
        const size_t slash = prefix.find_last_of(L'\\');
        if ((error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) || slash == std::wstring::npos ||
            slash < 3) {
            return false;
        }
        rest = prefix.substr(slash) + rest;
        prefix.resize(slash);
    }
    return false;
}

// What a path is to the guard. Saves (and the self-test directory) are
// matched by spelling exactly as before VUH-2775. An installed asset is
// matched on the resolved path when `resolve` is set (write opens and the
// destructive APIs), so an 8.3 name or a junction alias of KH2ReSource is
// still the install; its spelling also counts. `full` and `tail` give the
// path used for the twin and where its part below the guarded root starts.
enum class Kind { None, Save, Asset, AssetRoot };

Kind Classify(LPCWSTR path, bool resolve, wchar_t (&full)[1024], size_t& tail) {
    if (!path) return Kind::None;
    const DWORD n = GetFullPathNameW(path, 1024, full, nullptr);
    if (n == 0 || n >= 1024) {
        wcsncpy_s(full, path, _TRUNCATE);
    }
    wchar_t lowered[1024];
    wcscpy_s(lowered, full);
    Lower(lowered);
    if (const wchar_t* hit = std::wcsstr(lowered, kSaveFolder)) {
        tail = (hit - lowered) + (sizeof(kSaveFolder) / sizeof(wchar_t) - 1);
        return Kind::Save;
    }
    wchar_t resolved[1024];
    const wchar_t* asset = lowered;
    if (resolve && ResolvedLower(path, resolved) && AssetRootEnd(resolved)) {
        wcscpy_s(full, resolved);
        asset = resolved;
    }
    if (const wchar_t* end = AssetRootEnd(asset)) {
        if (*end == L'\0' || end[1] == L'\0') return Kind::AssetRoot;  // the root itself has no twin
        tail = (end - asset) + 1;
        return Kind::Asset;
    }
    if (g_testDir[0] && std::wcsstr(lowered, g_testDir) == lowered) {
        tail = std::wcslen(g_testDir);
        return Kind::Save;
    }
    return Kind::None;
}

// Relative names are resolved against the current directory first, so a
// save opened by a bare file name from inside the save folder still counts.
bool IsGuardedPath(LPCWSTR path) {
    wchar_t full[1024];
    size_t tail = 0;
    return Classify(path, true, full, tail) != Kind::None;
}

// The pre-VUH-2775 predicate (save folder or self-test directory, by
// spelling), which alone decides whether the save sandbox is usable.
bool IsSavePath(LPCWSTR path) {
    wchar_t full[1024];
    size_t tail = 0;
    return Classify(path, false, full, tail) == Kind::Save;
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
// Whether install-asset twins may use the sandbox: false when it is missing,
// a reparse point, or resolves under an asset root or the save folder. Saves
// keep using the sandbox either way.
bool g_assetTwinsOk = false;
// Serializes asset twin initialization, so concurrent first opens copy once.
SRWLOCK g_twinLock = SRWLOCK_INIT;

// Creates the parent directories of a sandbox twin.
void CreateParents(wchar_t (&twin)[1024]) {
    for (wchar_t* p = twin + std::wcslen(g_sandbox) + 1; *p; ++p) {
        if (*p == L'\\' || *p == L'/') {
            const wchar_t keep = *p;
            *p = L'\0';
            CreateDirectoryW(twin, nullptr);
            *p = keep;
        }
    }
}

bool Exists(LPCWSTR path) {
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

// An asset twin path is used only if nothing from the sandbox root down to
// the twin is a reparse point and the twin doesn't resolve into a guarded
// original: a junctioned install-assets would otherwise write the install.
bool TwinPathSafe(const wchar_t* twin) {
    const std::wstring path(twin);
    for (size_t end = std::wcslen(g_sandbox);; end = path.find(L'\\', end + 1)) {
        const DWORD attributes = GetFileAttributesW(path.substr(0, end).c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
        if (end == std::wstring::npos) break;
    }
    wchar_t resolved[1024];
    return ResolvedLower(twin, resolved) && !AssetRootEnd(resolved) && !std::wcsstr(resolved, kSaveFolder);
}

// ---- Asset twin cleanup ---------------------------------------------------
// A post-boss movie twin is ~63 MB and every instance has its own sandbox, so
// at install the install-assets trees of older sandboxes in the same log
// directory are removed. The newest kKeptAssetSandboxes (about three
// two-instance runs) and any whose process may still be running are kept.
// Only "<dir>\save_sandbox_<digits>\install-assets" trees are ever touched;
// saved files beside them stay. This runs before the launch acknowledgement,
// so the work is capped: a scan that finds more sandboxes than it can rank
// prunes nothing, and removal stops at a node, depth or time budget, leaving
// the rest for a later install.
constexpr size_t kKeptAssetSandboxes = 6;
constexpr size_t kMaxSandboxScan = 256;
constexpr int kMaxTreeDepth = 16;
constexpr std::uint32_t kMaxRemovals = 1024;
constexpr ULONGLONG kMaxPruneMs = 1000;

struct PruneBudget {
    std::uint32_t removals = kMaxRemovals;
    ULONGLONG deadline = 0;
    bool exhausted = false;
    bool Spend() {
        if (exhausted || removals == 0 || GetTickCount64() > deadline) return !(exhausted = true);
        --removals;
        return true;
    }
};

void RemoveTree(const std::wstring& dir, int depth, PruneBudget& budget) {
    if (depth > kMaxTreeDepth) {
        budget.exhausted = true;
        return;
    }
    WIN32_FIND_DATAW found {};
    HANDLE find = FindFirstFileW((dir + L"\\*").c_str(), &found);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring name = found.cFileName;
            if (name == L"." || name == L"..") continue;
            const std::wstring child = dir + L"\\" + name;
            if (found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                if (!budget.Spend()) break;
                if (!RemoveDirectoryW(child.c_str())) DeleteFileW(child.c_str());  // never follow a link
            } else if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                RemoveTree(child, depth + 1, budget);
            } else {
                if (!budget.Spend()) break;
                SetFileAttributesW(child.c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(child.c_str());
            }
        } while (!budget.exhausted && FindNextFileW(find, &found));
        FindClose(find);
    }
    if (budget.Spend()) RemoveDirectoryW(dir.c_str());  // fails harmlessly if anything was left
}

// Only a process that definitely no longer exists counts as finished; an
// unreadable or ambiguous liveness result keeps the sandbox.
bool MaybeRunning(DWORD pid) {
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!process) return GetLastError() != ERROR_INVALID_PARAMETER;
    const bool exited = WaitForSingleObject(process, 0) == WAIT_OBJECT_0;
    CloseHandle(process);
    return !exited;
}

void PruneAssetTwins(const wchar_t* sandbox) {
    const wchar_t* slash = std::wcsrchr(sandbox, L'\\');
    if (!slash) return;
    const std::wstring parent(sandbox, slash);
    struct Old { FILETIME written; DWORD pid; std::wstring assets; };
    std::vector<Old> olds;
    size_t scanned = 0;
    WIN32_FIND_DATAW found {};
    HANDLE find = FindFirstFileW((parent + L"\\save_sandbox_*").c_str(), &found);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        if (++scanned > kMaxSandboxScan) break;
        const std::wstring name = found.cFileName;
        const std::wstring digits = name.substr(std::wcslen(L"save_sandbox_"));
        if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || digits.empty() ||
            digits.find_first_not_of(L"0123456789") != std::wstring::npos || digits.size() > 10) continue;
        const std::wstring dir = parent + L"\\" + name;
        if (_wcsicmp(dir.c_str(), sandbox) == 0) continue;
        const std::wstring assets = dir + L"\\install-assets";
        WIN32_FILE_ATTRIBUTE_DATA info {};
        if (!GetFileAttributesExW(assets.c_str(), GetFileExInfoStandard, &info) ||
            !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
        olds.push_back({found.ftLastWriteTime, static_cast<DWORD>(std::wcstoul(digits.c_str(), nullptr, 10)), assets});
    } while (FindNextFileW(find, &found));
    FindClose(find);
    if (scanned > kMaxSandboxScan) {
        if (g_log) g_log("[saveguard] prune skipped: more than %zu sandboxes to rank", kMaxSandboxScan);
        return;
    }
    std::sort(olds.begin(), olds.end(), [](const Old& a, const Old& b) {
        return CompareFileTime(&a.written, &b.written) > 0;  // newest first
    });
    PruneBudget budget;
    budget.deadline = GetTickCount64() + kMaxPruneMs;
    std::uint32_t pruned = 0, live = 0;
    for (size_t i = kKeptAssetSandboxes; i < olds.size() && !budget.exhausted; ++i) {
        if (MaybeRunning(olds[i].pid)) {
            ++live;
            continue;
        }
        RemoveTree(olds[i].assets, 0, budget);
        ++pruned;
    }
    if (g_log && (pruned || live)) {
        g_log("[saveguard] pruned install-assets twins of %u old sandboxes (kept newest %zu, %u possibly running; "
              "removals %u/%u, budget %s)", pruned, kKeptAssetSandboxes, live, kMaxRemovals - budget.removals,
              kMaxRemovals, budget.exhausted ? "exhausted, rest left for a later install" : "ok");
    }
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

// Copies an installed asset to its twin under g_twinLock. The copy goes to a
// ".partial" name first and is renamed into place only when complete, so a
// twin that exists is always a whole copy. False (nothing published) on any
// failure.
bool CopyAssetTwin(LPCWSTR path, const wchar_t* twin) {
    wchar_t partial[1040];
    if (swprintf_s(partial, L"%s.partial", twin) < 0) return false;
    DeleteFileW(partial);  // a stale partial from an earlier failure; never guarded
    // The trampolines the public CopyFileW/MoveFileExW call internally, so no
    // hook judges the guard's own copy.
    if (!g_copyFileExW(path, partial, nullptr, nullptr, nullptr, 0) ||
        !g_moveFileWithProgressW(partial, twin, nullptr, nullptr, 0)) {
        const DWORD error = GetLastError();
        DeleteFileW(partial);
        if (g_log) g_log("[saveguard] asset twin copy failed: error %lu", error);
        return false;
    }
    if (g_log) {
        char narrow[600] = {};
        WideCharToMultiByte(CP_UTF8, 0, twin, -1, narrow, sizeof(narrow) - 1, nullptr, nullptr);
        g_log("[saveguard] asset twin ready: %s", narrow);
    }
    return true;
}

// An installed asset: a write goes to its twin (a whole copy made once), and
// a read follows an existing twin. Any doubt about the twin fails a write
// rather than letting it reach the install.
bool RouteAsset(const char* api, LPCWSTR path, const wchar_t* below, bool write, DWORD disposition,
                wchar_t (&twin)[1024]) {
    if (!g_assetTwinsOk || swprintf_s(twin, L"%s%s%s", g_sandbox, kAssetTwins, below) < 0) {
        twin[0] = L'\0';
        if (write) Deny(api, path);
        return write;
    }
    if (!write && !Exists(twin)) return false;  // the common case: no twin, read the install
    AcquireSRWLockExclusive(&g_twinLock);
    bool route = true;
    if (!write) {
        route = TwinPathSafe(twin);  // an unsafe twin path: read the install itself, which is harmless
    } else {
        CreateParents(twin);
        const bool keepsContents = disposition == OPEN_EXISTING || disposition == OPEN_ALWAYS;
        if (TwinPathSafe(twin) &&
            (Exists(twin) || !keepsContents || !Exists(path) || CopyAssetTwin(path, twin))) {
            LogRedirect(api, path, twin);
        } else {
            twin[0] = L'\0';
            Deny(api, path);
        }
    }
    ReleaseSRWLockExclusive(&g_twinLock);
    return route;
}

// Decides where an open of `path` goes. Returns false to open `path`
// itself; true with `twin` filled to open the sandbox twin instead, or
// empty to fail the open.
bool RouteOpen(const char* api, LPCWSTR path, DWORD access, DWORD disposition,
               wchar_t (&twin)[1024]) {
    twin[0] = L'\0';
    const bool write = IsWriteOpen(access, disposition);
    wchar_t full[1024];
    size_t tail = 0;
    const Kind kind = Classify(path, write, full, tail);
    if (kind == Kind::None) return false;
    if (kind == Kind::AssetRoot) {  // the KH2ReSource root itself: no twin
        if (write) Deny(api, path);
        return write;
    }
    if (kind == Kind::Asset) return RouteAsset(api, path, full + tail, write, disposition, twin);
    // A save: routed exactly as before VUH-2775.
    if (!g_sandbox[0] || swprintf_s(twin, L"%s%s", g_sandbox, full + tail) < 0) {
        twin[0] = L'\0';
        if (write) Deny(api, path);  // no sandbox: fail closed
        return write;
    }
    if (write) CreateParents(twin);
    const bool twinExists = Exists(twin);
    if (!write && !twinExists) return false;  // plain read of the real save
    const bool keepsContents = disposition == OPEN_EXISTING || disposition == OPEN_ALWAYS;
    if (write && !twinExists && keepsContents && Exists(path)) {
        // CopyFileW's own CopyFileExW call is hooked, and a sandbox under
        // KH2ReSource is guarded for assets; the save copy calls past it.
        g_copyFileExW(path, twin, nullptr, nullptr, nullptr, COPY_FILE_FAIL_IF_EXISTS);
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

// Path-based attribute writes (handle-based ones need write access, which
// RouteOpen already covers). The asset pruner runs before these hooks.
using SetFileAttributesWFn = BOOL(WINAPI*)(LPCWSTR, DWORD);
using SetFileAttributesAFn = BOOL(WINAPI*)(LPCSTR, DWORD);
SetFileAttributesWFn g_setFileAttributesW = nullptr;
SetFileAttributesAFn g_setFileAttributesA = nullptr;

BOOL WINAPI HookedSetFileAttributesW(LPCWSTR path, DWORD attributes) {
    if (IsGuardedPath(path)) {
        Deny("SetFileAttributesW", path);
        return FALSE;
    }
    return g_setFileAttributesW(path, attributes);
}

BOOL WINAPI HookedSetFileAttributesA(LPCSTR path, DWORD attributes) {
    const Wide wide(path);
    if (IsGuardedPath(wide.get())) {
        Deny("SetFileAttributesA", wide.get());
        return FALSE;
    }
    return g_setFileAttributesA(path, attributes);
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
        // Whether saves may use the sandbox is decided as before VUH-2775.
        if (GetFullPathNameW(joined, MAX_PATH, g_sandbox, nullptr) == 0 ||
            !(CreateDirectoryW(g_sandbox, nullptr) || GetLastError() == ERROR_ALREADY_EXISTS) ||
            IsSavePath(g_sandbox)) {
            g_sandbox[0] = L'\0';  // no sandbox: write opens fail closed
        }
        // Asset twins additionally need a sandbox that is no link and lies
        // outside every asset root and the save folder, however it's spelled.
        wchar_t lowered[1024] = {}, resolved[1024] = {};
        if (g_sandbox[0]) {
            wcscpy_s(lowered, g_sandbox);
            Lower(lowered);
        }
        const DWORD attributes = g_sandbox[0] ? GetFileAttributesW(g_sandbox) : INVALID_FILE_ATTRIBUTES;
        g_assetTwinsOk = attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
                         !AssetRootEnd(lowered) && ResolvedLower(g_sandbox, resolved) && !AssetRootEnd(resolved) &&
                         !std::wcsstr(resolved, kSaveFolder);
        if (g_sandbox[0] && !g_assetTwinsOk && g_log) {
            g_log("[saveguard] sandbox unusable for install-asset twins; asset writes are denied");
        }
    }
    if (g_assetTwinsOk) PruneAssetTwins(g_sandbox);
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
    ok &= HookApi(L"kernelbase", "SetFileAttributesW", reinterpret_cast<void*>(&HookedSetFileAttributesW),
                  &g_setFileAttributesW);
    ok &= HookApi(L"kernelbase", "SetFileAttributesA", reinterpret_cast<void*>(&HookedSetFileAttributesA),
                  &g_setFileAttributesA);
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
