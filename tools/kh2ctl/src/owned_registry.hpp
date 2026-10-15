#pragma once
// The rig's ownership registry, build/rig/owned.txt: one "pid creationTime"
// row per KH2 instance this checkout launched. Several kh2ctl processes use it
// at once during a run (launch, fight, polls, kill), and desk-scenario.ps1
// reads it to tell the run's own games from foreign ones. So:
// - every read-modify-write holds one named mutex per registry file;
// - a write goes to a temp file that is renamed over the registry, so a reader
//   sees the old or the new list, never an empty or partial one;
// - prune rewrites only when its result differs from what it read.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace owned_registry {

struct Entry {
    DWORD pid {0};
    std::uint64_t creationTime {0};
    bool operator==(const Entry&) const = default;
};

// Opening can fail for a moment while a writer's rename replaces the file
// (the old file is delete-pending). That is not an empty list, so retry; only
// a registry that does not exist reads as empty.
inline std::vector<Entry> Read(const std::filesystem::path& file) {
    std::vector<Entry> rows;
    for (int attempt = 0; attempt < 100; ++attempt) {
        std::ifstream in(file);
        if (in.is_open()) {
            Entry row;
            while (in >> row.pid >> row.creationTime) rows.push_back(row);
            return rows;
        }
        std::error_code ec;
        if (!std::filesystem::exists(file, ec) && !ec) return rows;
        Sleep(5);
    }
    return rows;
}

// Holds the registry's named mutex for its lifetime. The name is derived from
// the registry path, so separate checkouts never contend. Global\ spans the
// agent (session 0) and desk (session 1) processes that share one rig.
class Lock {
public:
    explicit Lock(const std::filesystem::path& file, DWORD timeoutMs = 10000) {
        auto key = std::filesystem::absolute(file).lexically_normal().wstring();
        for (auto& ch : key) ch = static_cast<wchar_t>(towlower(ch));
        const auto hash = std::hash<std::wstring>{}(key);
        const auto name = L"Global\\kh2coop_rig_owned_" + std::to_wstring(hash);
        mutex_ = CreateMutexW(nullptr, FALSE, name.c_str());
        if (!mutex_) mutex_ = CreateMutexW(nullptr, FALSE, (L"Local\\kh2coop_rig_owned_" + std::to_wstring(hash)).c_str());
        if (!mutex_) return;
        const DWORD wait = WaitForSingleObject(mutex_, timeoutMs);
        held_ = wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED; // an abandoned holder's file write was atomic
    }
    ~Lock() {
        if (held_) ReleaseMutex(mutex_);
        if (mutex_) CloseHandle(mutex_);
    }
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
    bool Held() const { return held_; }
private:
    HANDLE mutex_ {};
    bool held_ {false};
};

// Callers hold Lock. Readers may have the file open without delete sharing,
// so the replacing rename retries briefly instead of failing the command.
inline bool WriteAtomic(const std::filesystem::path& file, const std::vector<Entry>& rows) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    auto temp = file;
    temp += L".tmp" + std::to_wstring(GetCurrentProcessId());
    {
        std::ofstream out(temp, std::ios::trunc);
        for (const auto& row : rows) out << row.pid << " " << row.creationTime << "\n";
        out.flush();
        if (!out) { std::filesystem::remove(temp, ec); return false; }
    }
    for (int attempt = 0; attempt < 200; ++attempt) {
        if (MoveFileExW(temp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
        Sleep(10);
    }
    std::filesystem::remove(temp, ec);
    return false;
}

// An entry stays while a live process has its pid and creation time. A live
// pid whose creation time could not be read (live.creationTime == 0) also
// keeps it: an unreadable query is not evidence that the game exited.
inline std::vector<Entry> Keep(const std::vector<Entry>& rows, const std::vector<Entry>& live) {
    std::vector<Entry> kept;
    for (const auto& row : rows)
        for (const auto& proc : live)
            if (proc.pid == row.pid && (proc.creationTime == row.creationTime || proc.creationTime == 0)) {
                kept.push_back(row);
                break;
            }
    return kept;
}

// Callers hold Lock. Returns the kept rows; writes only when they changed.
inline std::vector<Entry> Prune(const std::filesystem::path& file, const std::vector<Entry>& live) {
    const auto rows = Read(file);
    auto kept = Keep(rows, live);
    if (kept != rows) WriteAtomic(file, kept);
    return kept;
}

} // namespace owned_registry
