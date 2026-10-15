// Concurrent writers on kh2ctl's ownership registry (build/rig/owned.txt).
// The parent spawns copies of itself: writers that each append their own rows
// the way kh2ctl launch does (lock, prune, append, write), pruners that rewrite
// nothing, and a lock-free reader standing in for desk-scenario.ps1. No row may
// be lost, and the reader must never see the list shrink or a partial row.
// "--control" repeats the run with the old unlocked truncate-and-write to show
// the test can see the race; its result is printed, not asserted.
#include "owned_registry.hpp"
#include <cstdio>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace r = owned_registry;
namespace fs = std::filesystem;

constexpr int Writers = 6, Rows = 40, Pruners = 2;

void OldWrite(const fs::path& file, const std::vector<r::Entry>& rows) {
    std::ofstream out(file, std::ios::trunc);
    for (const auto& row : rows) out << row.pid << " " << row.creationTime << "\n";
}

int Writer(const fs::path& file, int id, bool old) {
    for (int k = 0; k < Rows; ++k) {
        const r::Entry mine {static_cast<DWORD>(id * 1000 + k), static_cast<std::uint64_t>(k + 1)};
        if (old) {
            auto rows = r::Read(file);
            rows.push_back(mine);
            OldWrite(file, rows);
        } else {
            r::Lock lock(file);
            if (!lock.Held()) return 3;
            auto rows = r::Read(file);
            auto live = rows; // every recorded game is still running
            rows = r::Keep(rows, live);
            rows.push_back(mine);
            if (!r::WriteAtomic(file, rows)) return 4;
        }
    }
    return 0;
}

int Pruner(const fs::path& file, bool old) {
    for (int k = 0; k < Rows * 2; ++k) {
        if (old) {
            const auto rows = r::Read(file);
            OldWrite(file, rows); // the old prune always rewrote
        } else {
            r::Lock lock(file);
            if (!lock.Held()) return 3;
            const auto rows = r::Read(file);
            r::Prune(file, rows);
        }
    }
    return 0;
}

// Lock-free, like the desk guard: the row count must never drop and every
// line must be a whole "pid creationTime" pair.
int Reader(const fs::path& file, const fs::path& stop) {
    std::size_t most = 0;
    int shrinks = 0, partial = 0, openFailures = 0;
    while (!fs::exists(stop)) {
        std::ifstream in(file);
        // An open can fail while a rename replaces the file. Readers retry
        // (desk-scenario.ps1 does); it is not an empty list.
        if (!in.is_open()) { ++openFailures; continue; }
        std::string line;
        std::size_t count = 0;
        while (std::getline(in, line)) {
            std::istringstream fields(line);
            unsigned long pid = 0;
            unsigned long long created = 0;
            std::string extra;
            if (!(fields >> pid >> created) || (fields >> extra)) ++partial;
            ++count;
        }
        if (count < most) ++shrinks;
        if (count > most) most = count;
    }
    std::printf("reader shrinks=%d partial=%d openFailures=%d\n", shrinks, partial, openFailures);
    return shrinks || partial ? 1 : 0;
}

// kh2ctl's own lock-free readers use r::Read, which retries those opens:
// it must never return fewer rows than it has already seen.
int RetryingReader(const fs::path& file, const fs::path& stop) {
    std::size_t most = 0;
    int shrinks = 0;
    while (!fs::exists(stop)) {
        const auto count = r::Read(file).size();
        if (count < most) ++shrinks;
        if (count > most) most = count;
    }
    std::printf("retrying reader shrinks=%d\n", shrinks);
    return shrinks ? 1 : 0;
}

PROCESS_INFORMATION Spawn(const std::wstring& args) {
    wchar_t self[MAX_PATH] {};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring command = L"\"" + std::wstring(self) + L"\" " + args;
    STARTUPINFOW startup {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info {};
    CreateProcessW(self, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &info);
    return info;
}

DWORD Wait(PROCESS_INFORMATION& info) {
    DWORD code = 99;
    if (!info.hProcess) return code;
    WaitForSingleObject(info.hProcess, 120000);
    GetExitCodeProcess(info.hProcess, &code);
    CloseHandle(info.hThread);
    CloseHandle(info.hProcess);
    return code;
}

// Returns the number of lost rows; failures counts child/reader failures.
int Run(const fs::path& dir, bool old, int& failures) {
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    const auto file = dir / "owned.txt";
    const auto stop = dir / "stop";
    const std::wstring mode = old ? L" old" : L" new";
    auto reader = Spawn(L"--reader \"" + file.wstring() + L"\" \"" + stop.wstring() + L"\"");
    auto retrying = Spawn(L"--retrying-reader \"" + file.wstring() + L"\" \"" + stop.wstring() + L"\"");
    std::vector<PROCESS_INFORMATION> children;
    for (int id = 1; id <= Writers; ++id)
        children.push_back(Spawn(L"--writer \"" + file.wstring() + L"\" " + std::to_wstring(id) + mode));
    for (int i = 0; i < Pruners; ++i)
        children.push_back(Spawn(L"--pruner \"" + file.wstring() + L"\"" + mode));
    for (auto& child : children) failures += Wait(child) != 0;
    std::ofstream(stop) << "stop";
    const bool readerFailed = Wait(reader) != 0;
    const bool retryingFailed = Wait(retrying) != 0;
    if (!old) failures += readerFailed + retryingFailed;
    std::set<std::pair<DWORD, std::uint64_t>> seen;
    for (const auto& row : r::Read(file)) seen.insert({row.pid, row.creationTime});
    int lost = 0;
    for (int id = 1; id <= Writers; ++id)
        for (int k = 0; k < Rows; ++k)
            lost += !seen.count({static_cast<DWORD>(id * 1000 + k), static_cast<std::uint64_t>(k + 1)});
    return lost;
}

bool FileId(const fs::path& file, BY_HANDLE_FILE_INFORMATION& info) {
    HANDLE h = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    const bool ok = GetFileInformationByHandle(h, &info) != 0;
    CloseHandle(h);
    return ok;
}

int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "";
    if (mode == "--writer" && argc > 4) return Writer(argv[2], std::atoi(argv[3]), std::string(argv[4]) == "old");
    if (mode == "--pruner" && argc > 3) return Pruner(argv[2], std::string(argv[3]) == "old");
    if (mode == "--reader" && argc > 3) return Reader(argv[2], argv[3]);
    if (mode == "--retrying-reader" && argc > 3) return RetryingReader(argv[2], argv[3]);

    int failures = 0, checks = 0;
    auto check = [&](bool yes, const char* name) { ++checks; if (!yes) { ++failures; std::printf("FAIL %s\n", name); } };
    wchar_t temp[MAX_PATH] {};
    GetTempPathW(MAX_PATH, temp);
    const auto root = fs::path(temp) / ("kh2coop_owned_registry_" + std::to_string(GetCurrentProcessId()));

    int childFailures = 0;
    const int lost = Run(root / "new", false, childFailures);
    check(childFailures == 0, "every locked writer, pruner and the reader succeeded");
    check(lost == 0, "no writer's row was lost under concurrent launch-style writes");
    std::printf("locked: rows=%d lost=%d childFailures=%d\n", Writers * Rows, lost, childFailures);

    // A prune that keeps everything must not touch the file.
    const auto file = root / "new" / "owned.txt";
    BY_HANDLE_FILE_INFORMATION before {}, after {};
    check(FileId(file, before), "registry readable");
    { r::Lock lock(file); r::Prune(file, r::Read(file)); }
    check(FileId(file, after) && before.nFileIndexLow == after.nFileIndexLow &&
          before.nFileIndexHigh == after.nFileIndexHigh &&
          before.ftLastWriteTime.dwLowDateTime == after.ftLastWriteTime.dwLowDateTime,
          "an unchanged prune does not rewrite the registry");

    // An entry whose live creation time can't be read is kept, not dropped.
    const std::vector<r::Entry> rows {{10, 5}, {11, 6}};
    const auto kept = r::Keep(rows, {{10, 0}, {12, 7}});
    check(kept.size() == 1 && kept[0].pid == 10, "unreadable live creation time keeps its row; gone pid drops");
    check(r::Keep(rows, {{11, 9}}).empty(), "a reused pid with a different creation time drops the row");

    if (argc > 1 && mode == "--control") {
        int controlFailures = 0;
        const int controlLost = Run(root / "old", true, controlFailures);
        std::printf("control (old unlocked truncate): rows=%d lost=%d\n", Writers * Rows, controlLost);
    }
    std::error_code ec;
    fs::remove_all(root, ec);
    std::printf("checks=%d failures=%d\n", checks, failures);
    return failures ? 1 : 0;
}
