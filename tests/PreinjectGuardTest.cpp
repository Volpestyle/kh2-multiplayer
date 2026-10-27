// Starts only copies of this harmless executable. All file writes are in a
// uniquely owned temp directory; no game process, save, network or Steam API.
#include "../tools/kh2ctl/src/preinject.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <thread>
#include <vector>
#include <iostream>
#include <stdexcept>
#include <regex>

namespace fs = std::filesystem;
using namespace kh2coop::preinject;
int failures = 0;
void Check(bool value, const char* name) {
    std::cout << (value ? "PASS " : "FAIL ") << name << '\n';
    if (!value) ++failures;
}
std::string Read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
void Write(const fs::path& path, const char* text) { std::ofstream(path) << text; }

int wmain(int argc, wchar_t** argv) {
    if (argc == 4 && std::wstring(argv[1]) == L"--child") {
        FILE* file = nullptr;
        _wfopen_s(&file, argv[2], L"r+b");
        if (!file) _wfopen_s(&file, argv[2], L"wb");
        if (!file) return 2;
        fwrite("boot-write", 1, 10, file);
        fclose(file);
        std::string readBack = Read(argv[2]).substr(0, 10);  // follows the twin once it exists
        const BOOL deleted = DeleteFileA(fs::path(argv[2]).string().c_str());
        Write(argv[3], ((deleted ? "deleted|" : "blocked|") + readBack).c_str());
        return deleted ? 3 : 0;
    }
    if (argc >= 5 && std::wstring(argv[1]) == L"--ops") {
        // argv[2]: marker; then (op, path) pairs, each reported in order (VUH-2763 review controls).
        //   rw    fopen r+b, write "patched!" at 0       -> wrote|failed
        //   ow    CreateFileW GENERIC_WRITE OPEN_ALWAYS   -> opened|failed
        //   attr  SetFileAttributesW READONLY|HIDDEN      -> set|denied
        //   attrA SetFileAttributesA READONLY|HIDDEN      -> set|denied
        //   race  8 threads at once: r+b, read the last 8 bytes, write "patched!" at 0 -> race:<tails seen>
        //   head  fopen rb, first 8 bytes; mhead the same through KernelBase CreateFile2 (Media Foundation's open)
        //   undo  fopen r+b, write "restored" at 0 (the game's restore thread) -> wrote|failed
        std::string out, retries;
        for (int i = 3; i + 1 < argc; i += 2) {
            const std::wstring op = argv[i];
            const wchar_t* path = argv[i + 1];
            std::string result;
            if (op == L"rw" || op == L"undo") {
                FILE* file = nullptr;
                const bool opened = _wfopen_s(&file, path, L"r+b") == 0 && file;
                const bool wrote = opened && fwrite(op == L"rw" ? "patched!" : "restored", 1, 8, file) == 8;
                result = opened && fclose(file) == 0 && wrote ? "wrote" : "failed";
            } else if (op == L"head") {
                FILE* file = nullptr;
                char bytes[9] {};
                if (_wfopen_s(&file, path, L"rb") == 0 && file) {
                    fread(bytes, 1, 8, file);
                    fclose(file);
                }
                result = bytes;
            } else if (op == L"mhead") {
                char bytes[9] {};
                DWORD read = 0;
                HANDLE file = CreateFile2(path, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, nullptr);
                if (file != INVALID_HANDLE_VALUE) {
                    ReadFile(file, bytes, 8, &read, nullptr);
                    CloseHandle(file);
                }
                result = bytes;
            } else if (op == L"ow") {
                HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, 0, nullptr);
                result = file != INVALID_HANDLE_VALUE ? "opened" : "failed";
                if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
            } else if (op == L"attr") {
                result = SetFileAttributesW(path, FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN) ? "set" : "denied";
            } else if (op == L"attrA") {
                result = SetFileAttributesA(fs::path(path).string().c_str(), FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN)
                             ? "set" : "denied";
            } else if (op == L"race") {
                std::vector<std::string> tails(8);
                std::vector<std::thread> threads;
                std::atomic<int> sharingRetries {0};
                HANDLE start = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                for (int t = 0; t < 8; ++t) {
                    threads.emplace_back([&, t] {
                        WaitForSingleObject(start, INFINITE);
                        // Openers get transient ERROR_SHARING_VIOLATIONs here (10-16 retries per run on
                        // 2026-10-27, the same with edf3d5d's guard); the holder hasn't been identified. Only
                        // that error is retried, a bounded number of times, and the count is reported; every
                        // opener must still see the whole copy.
                        FILE* file = nullptr;
                        for (int attempt = 0; attempt < 40; ++attempt) {
                            if (_wfopen_s(&file, path, L"r+b") == 0 && file) break;
                            if (GetLastError() != ERROR_SHARING_VIOLATION) break;
                            ++sharingRetries;
                            Sleep(50);
                        }
                        if (!file) {
                            tails[t] = "open-failed-" + std::to_string(GetLastError());
                            return;
                        }
                        char tail[9] {};
                        fseek(file, -8, SEEK_END);
                        tails[t] = fread(tail, 1, 8, file) == 8 ? tail : "short";
                        fseek(file, 0, SEEK_SET);
                        fwrite("patched!", 1, 8, file);
                        fclose(file);
                    });
                }
                SetEvent(start);
                for (auto& thread : threads) thread.join();
                CloseHandle(start);
                std::sort(tails.begin(), tails.end());
                tails.erase(std::unique(tails.begin(), tails.end()), tails.end());
                result = "race:";
                for (const auto& tail : tails) result += tail + ";";
                retries = " sharing-retries=" + std::to_string(sharingRetries.load());
            }
            out += (out.empty() ? "" : "|") + result;
        }
        Write(argv[2], (out + retries).c_str());
        return 0;
    }
    if (argc == 4 && std::wstring(argv[1]) == L"--movie-child") {
        // argv[2]: an installed cipher movie, played the way the game does (VUH-2763): a read, the decrypt
        // write of the header, the CRT and Media Foundation (KernelBase CreateFile2) reads, the restore write.
        auto head = [](const wchar_t* path) {
            FILE* file = nullptr;
            char bytes[9] {};
            if (_wfopen_s(&file, path, L"rb") == 0 && file) {
                fread(bytes, 1, 8, file);
                fclose(file);
            }
            return std::string(bytes);
        };
        auto patch = [](const wchar_t* path, const char* text) {
            FILE* file = nullptr;
            if (_wfopen_s(&file, path, L"r+b") != 0 || !file) return std::string("failed");
            const bool wrote = fwrite(text, 1, 8, file) == 8;
            return std::string(fclose(file) == 0 && wrote ? "wrote" : "failed");
        };
        auto mediaHead = [](const wchar_t* path) {
            char bytes[9] {};
            DWORD read = 0;
            HANDLE file = CreateFile2(path, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, nullptr);
            if (file != INVALID_HANDLE_VALUE) {
                ReadFile(file, bytes, 8, &read, nullptr);
                CloseHandle(file);
            }
            return std::string(bytes);
        };
        // One step per statement: operands of + are evaluated in no fixed order.
        std::string out = head(argv[2]);
        out += "|" + patch(argv[2], "decoded!");
        out += "|" + head(argv[2]);
        out += "|" + mediaHead(argv[2]);
        out += "|" + patch(argv[2], "restored");
        out += "|" + head(argv[2]);
        Write(argv[3], out.c_str());
        return 0;
    }
    if (argc == 4 && std::wstring(argv[1]) == L"--child-root") {
        // argv[2]: an empty exact KH2ReSource root, whose sibling KH2ReSource-other sits beside it.
        const fs::path assetRoot = argv[2], sibling = assetRoot.wstring() + L"-other";
        HANDLE dir = CreateFileW(assetRoot.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                 OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        const bool wrote = dir != INVALID_HANDLE_VALUE;
        if (wrote) CloseHandle(dir);
        const bool moved = MoveFileExW(assetRoot.c_str(), (assetRoot.wstring() + L"-moved").c_str(), 0) != FALSE;
        const bool movedA = MoveFileExA(assetRoot.string().c_str(), (assetRoot.string() + "-movedA").c_str(), 0) != FALSE;
        const bool removed = RemoveDirectoryW(assetRoot.c_str()) != FALSE;
        const bool siblingMoved = MoveFileExW(sibling.c_str(), (sibling.wstring() + L"-moved").c_str(), 0) != FALSE;
        Write(argv[3], (std::string("write=") + (wrote ? "1" : "0") + " move=" + (moved ? "1" : "0") +
                        " moveA=" + (movedA ? "1" : "0") + " remove=" + (removed ? "1" : "0") +
                        " sibling=" + (siblingMoved ? "1" : "0")).c_str());
        return 0;
    }
    wchar_t exe[MAX_PATH] {}, temp[MAX_PATH] {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    GetTempPathW(MAX_PATH, temp);
    const auto root = fs::path(temp) / (L"kh2_preinject_" + std::to_wstring(GetCurrentProcessId()));
    if (!fs::create_directory(root)) return 4; // never reuse or remove an unowned directory
    const auto guarded = root / L"My Games" / L"KINGDOM HEARTS HD 1.5+2.5 ReMIX" / L"Steam" / L"test";
    fs::create_directories(guarded);
    // An installed cipher movie's folder (VUH-2775): guarded by its KH2ReSource root.
    const auto asset = root / L"STEAM" / L"juefigs" / L"KH2ReSource" / L"zmovie" / L"en";
    fs::create_directories(asset);
    const auto logs = root / "logs";
    fs::create_directory(logs);
    // Eight finished sandboxes with asset twins, oldest first. Their PIDs are not
    // multiples of four, so no process can own them; one non-numeric look-alike.
    auto oldSandbox = [&](const std::wstring& name, int age) {
        const auto dir = logs / name;
        fs::create_directories(dir / L"install-assets" / L"zmovie");
        fs::create_directories(dir / L"Steam");
        Write(dir / L"install-assets" / L"zmovie" / L"me4.mp4", "twin");
        Write(dir / L"Steam" / L"keep.png", "saved");
        fs::last_write_time(dir, fs::file_time_type::clock::now() - std::chrono::hours(age));
        return dir;
    };
    std::vector<fs::path> olds;
    for (int i = 1; i <= 8; ++i) olds.push_back(oldSandbox(L"save_sandbox_" + std::to_wstring(9000000 + 4 * i + 1), 10 - i));
    const auto lookalike = oldSandbox(L"save_sandbox_x9", 20);
    SetEnvironmentVariableW(L"KH2COOP_LOG_DIR", logs.c_str());
    // This logged fixture must never inherit an out-of-lane self-test target.
    SetEnvironmentVariableW(L"KH2COOP_SAVEGUARD_TEST_DIR", nullptr);
    SetEnvironmentVariableW(L"KH2COOP_PREINJECT_TEST_FAIL_GUARD", nullptr);
    const auto dll = fs::path(exe).parent_path() / L"kh2coop_preinject_guard.dll";
    auto spawn = [&](const fs::path& save, const fs::path& marker, const wchar_t* mode = L"--child") {
        STARTUPINFOW startup {};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION info {};
        std::wstring command = L"\"" + std::wstring(exe) + L"\" " + mode + L" \"" + save.wstring() + L"\" \"" + marker.wstring() + L"\"";
        if (!CreateProcessW(exe, command.data(), nullptr, nullptr, FALSE,
                            CREATE_SUSPENDED, nullptr, root.c_str(), &startup, &info))
            throw std::runtime_error("offline child creation failed");
        return info;
    };
    for (int kind = 0; kind < 4; ++kind) {
        const bool existing = kind != 0, movie = kind == 3;
        const auto save = movie ? asset / L"3.mp4" : guarded / (std::to_string(kind) + ".png");
        const auto marker = root / (std::to_string(kind) + ".marker");
        const char* original = kind == 2 ? "" : movie ? "encrypted-header+payload" : "original-save";
        if (existing) Write(save, original);
        auto info = spawn(save, marker);
        {
            Child child(info);
            const auto error = child.ProtectAndResume([&](DWORD pid) { return InjectDll(pid, dll); }, 10000);
            Check(error.empty(), "actual SaveGuard acknowledged before resume");
            if (error.empty()) {
                const auto& receipt = child.Evidence();
                Check(receipt.ackObserved && receipt.resumePrevCount == 1 && receipt.resumedAtTick > 0 && receipt.ackWaitMs <= 10000,
                      "launcher records successful wait, exact resume count and QPC tick");
                Check(WaitForSingleObject(info.hProcess, 10000) == WAIT_OBJECT_0, "protected child exits");
                DWORD code = 99;
                GetExitCodeProcess(info.hProcess, &code);
                Check(code == 0 && Read(marker) == "blocked|boot-write",
                      "first entry-point CRT write redirects, reads follow the twin and delete is denied");
                Check(existing ? fs::exists(save) && Read(save) == original : !fs::exists(save), "nonempty, empty and missing original paths preserved");
                const auto sandbox = logs / (L"save_sandbox_" + std::to_wstring(info.dwProcessId));
                const auto twin = movie ? sandbox / L"install-assets" / L"zmovie" / L"en" / save.filename()
                                        : sandbox / L"Steam" / L"test" / save.filename();
                Check(Read(twin).find("boot-write") == 0, "boot write reaches only sandbox");
                if (movie) Check(Read(twin) == "boot-write" + std::string(original).substr(10),
                                 "installed asset twin starts as a full copy of the original");
                if (kind == 0) {
                    bool pruned = true, kept = true, saves = true;
                    for (size_t i = 0; i < olds.size(); ++i) {
                        const bool exists = fs::exists(olds[i] / L"install-assets");
                        if (i < 2) pruned &= !exists;
                        else kept &= exists;
                        saves &= Read(olds[i] / L"Steam" / L"keep.png") == "saved";
                    }
                    Check(pruned && kept && saves && fs::exists(lookalike / L"install-assets"),
                          "install removes only the oldest finished sandboxes' asset twins, keeping six and saves");
                }
                const auto text = Read(logs / (L"preinject_guard_" + std::to_wstring(info.dwProcessId) + L".log"));
                std::smatch ack;
                const bool found = std::regex_search(text, ack, std::regex(R"(\[saveguard\] ack signalled pid=(\d+) qpc=(\d+) tickMs=(\d+))"));
                Check(found && std::stoul(ack[1].str()) == info.dwProcessId &&
                      std::stoll(ack[2].str()) > 0 && std::stoll(ack[2].str()) < receipt.resumedAtTick &&
                      text.find("Save guard installed:") < static_cast<std::size_t>(ack.position()),
                      "actual DLL install/ack log precedes observed main-thread resume");
            }
        }
    }
    {
        // The exact asset root (no trailing separator) is guarded as a whole component: its write open,
        // moves and removal are denied and it gets no twin, while a "KH2ReSource-other" sibling stays
        // ordinary. The same install meets an oversized old asset tree and stops at its removal budget.
        const auto emptyRoot = root / L"other" / L"KH2ReSource";
        fs::create_directories(emptyRoot);
        fs::create_directories(root / L"other" / L"KH2ReSource-other");
        const auto big = logs / L"save_sandbox_9000101";
        fs::create_directories(big / L"install-assets" / L"bulk");
        for (int i = 0; i < 1100; ++i) Write(big / L"install-assets" / L"bulk" / (std::to_wstring(i) + L".bin"), "x");
        fs::last_write_time(big, fs::file_time_type::clock::now() - std::chrono::hours(30));
        const auto marker = root / "root.marker";
        auto info = spawn(emptyRoot, marker, L"--child-root");
        Child child(info);
        const auto error = child.ProtectAndResume([&](DWORD pid) { return InjectDll(pid, dll); }, 10000);
        Check(error.empty(), "install with an over-budget old asset tree still acknowledges before resume");
        if (error.empty()) {
            Check(WaitForSingleObject(info.hProcess, 10000) == WAIT_OBJECT_0, "root-control child exits");
            Check(Read(marker) == "write=0 move=0 moveA=0 remove=0 sibling=1" && fs::exists(emptyRoot),
                  "exact KH2ReSource root write/move/removal denied; sibling directory unaffected");
            const auto sandbox = logs / (L"save_sandbox_" + std::to_wstring(info.dwProcessId));
            Check(!fs::exists(sandbox / L"install-assets"), "asset root gets no sandbox twin");
            const auto text = Read(logs / (L"preinject_guard_" + std::to_wstring(info.dwProcessId) + L".log"));
            Check(fs::exists(big / L"install-assets") && !fs::exists(olds[2] / L"install-assets") &&
                      text.find("budget exhausted, rest left for a later install") != std::string::npos,
                  "prune stops at its removal budget, logs it and leaves the rest for a later install");
        }
    }
    for (int mode = 0; mode < 5; ++mode) {
        const auto marker = root / ("failure_" + std::to_string(mode));
        const auto save = guarded / "untouched.png";
        if (mode == 4) SetEnvironmentVariableW(L"KH2COOP_PREINJECT_TEST_FAIL_GUARD", L"1");
        auto info = spawn(save, marker);
        SetEnvironmentVariableW(L"KH2COOP_PREINJECT_TEST_FAIL_GUARD", nullptr);
        HANDLE observed = OpenProcess(SYNCHRONIZE, FALSE, info.dwProcessId);
        HANDLE collision = mode == 3 ? CreateEventW(nullptr, TRUE, TRUE, ReadyEventName(info.dwProcessId).c_str()) : nullptr;
        {
            Child child(info);
            try {
                const auto error = child.ProtectAndResume([&](DWORD pid) -> std::string {
                    if (mode == 0) return InjectDll(pid, root / "absent.dll");
                    if (mode == 1) return {}; // old DLL / incomplete installation: no acknowledgement
                    if (mode == 2) throw std::runtime_error("injection exception");
                    if (mode == 4) return InjectDll(pid, dll);
                    return {};
                }, mode == 4 ? 1000 : 50);
                Check(!error.empty(), "failed injection, absent acknowledgement or stale event refuses resume");
                Check(!child.Evidence().ackObserved, "failed protection never reports an observed acknowledgement");
            } catch (const std::runtime_error&) { Check(mode == 2, "exception unwinds owned child"); }
        }
        Check(observed && WaitForSingleObject(observed, 0) == WAIT_OBJECT_0 && !fs::exists(marker) && !fs::exists(save),
              "failure terminates child before entry point or file mutation");
        if (observed) CloseHandle(observed);
        if (collision) CloseHandle(collision);
    }
    {
        // A whole playback (VUH-2763): a read before any write opens the installed movie; the decrypt write,
        // the later reads (Media Foundation's CreateFile2 included) and the restore all use the twin, and the
        // installed file's bytes and mtime never change. Last, so its sandbox can't affect the prune checks.
        const auto movie = asset / L"me4.mp4";
        const char* original = "SCRAMBLED-HEADER+payload";
        Write(movie, original);
        const auto stamp = fs::last_write_time(movie);
        const auto marker = root / "movie.marker";
        auto info = spawn(movie, marker, L"--movie-child");
        Child child(info);
        const auto error = child.ProtectAndResume([&](DWORD pid) { return InjectDll(pid, dll); }, 10000);
        Check(error.empty(), "movie playback child: actual SaveGuard acknowledged before resume");
        if (error.empty()) {
            Check(WaitForSingleObject(info.hProcess, 10000) == WAIT_OBJECT_0, "movie playback child exits");
            Check(Read(marker) == "SCRAMBLE|wrote|decoded!|decoded!|wrote|restored",
                  "movie read before any write sees the install; decrypt, CRT/CreateFile2 reads and restore use the twin");
            Check(Read(movie) == original && fs::last_write_time(movie) == stamp,
                  "installed movie bytes and mtime unchanged through playback");
            const auto twin = logs / (L"save_sandbox_" + std::to_wstring(info.dwProcessId)) / L"install-assets" /
                              L"zmovie" / L"en" / L"me4.mp4";
            Check(Read(twin) == std::string("restored") + (original + 8), "movie twin holds the copy plus both writes");
        }
    }
    // Review controls (VUH-2763, Rhea ddc42716): each fails without its fix.
    auto runOps = [&](const std::vector<std::wstring>& ops, const fs::path& marker,
                      const std::function<void(DWORD)>& beforeResume) {
        std::wstring command = L"\"" + std::wstring(exe) + L"\" --ops \"" + marker.wstring() + L"\"";
        for (const auto& op : ops) command += L" \"" + op + L"\"";
        STARTUPINFOW startup {};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION info {};
        if (!CreateProcessW(exe, command.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, root.c_str(),
                            &startup, &info))
            throw std::runtime_error("offline ops child creation failed");
        const DWORD pid = info.dwProcessId;
        if (beforeResume) beforeResume(pid);
        Child child(info);
        const auto error = child.ProtectAndResume([&](DWORD id) { return InjectDll(id, dll); }, 10000);
        const bool exited = error.empty() && WaitForSingleObject(info.hProcess, 30000) == WAIT_OBJECT_0;
        return std::make_pair(exited, pid);
    };
    auto twinOf = [&](DWORD pid, const wchar_t* name) {
        return logs / (L"save_sandbox_" + std::to_wstring(pid)) / L"install-assets" / L"zmovie" / L"en" / name;
    };
    auto guardLog = [&](const fs::path& dir, DWORD pid) {
        return Read(dir / (L"preinject_guard_" + std::to_wstring(pid) + L".log"));
    };
    {
        // 1. Aliases of the installed root: a junction with no KH2ReSource component, an 8.3 name, and "\\?\".
        for (const wchar_t* name : {L"a1.mp4", L"a2.mp4", L"a3.mp4"}) Write(asset / name, "ORIGINAL-alias");
        const auto alias = root / L"alias";
        const std::wstring junction = L"cmd /c mklink /J \"" + alias.wstring() + L"\" \"" + asset.wstring() + L"\" >nul";
        Check(_wsystem(junction.c_str()) == 0 && fs::exists(alias / L"a1.mp4"), "alias junction to the movie folder");
        wchar_t shortName[MAX_PATH] {};
        GetShortPathNameW((asset / L"a2.mp4").c_str(), shortName, MAX_PATH);
        const bool shortNames = std::wstring(shortName).find(L'~') != std::wstring::npos;
        if (!shortNames) std::cout << "NOTE 8.3 names are disabled on this volume; the short-name case is not exercised\n";
        const std::wstring extended = L"\\\\?\\" + (asset / L"a3.mp4").wstring();
        const auto marker = root / "alias.marker";
        // Per alias, a playback: read, decrypt write, CRT and CreateFile2 reads, restore. Reads after the write
        // must resolve the alias the same way the write did (Rhea eb389de1), or they see the install.
        std::vector<std::wstring> ops;
        for (const std::wstring& path : {(alias / L"a1.mp4").wstring(),
                                         shortNames ? std::wstring(shortName) : (asset / L"a2.mp4").wstring(), extended})
            for (const wchar_t* op : {L"head", L"rw", L"head", L"mhead", L"undo"}) ops.insert(ops.end(), {op, path});
        auto [exited, pid] = runOps(ops, marker, nullptr);
        const std::string playback = "ORIGINAL|wrote|patched!|patched!|wrote";
        Check(exited && Read(marker) == playback + "|" + playback + "|" + playback,
              "aliased movie playback: writes succeed and later reads through each alias see the twin");
        Check(Read(asset / L"a1.mp4") == "ORIGINAL-alias" && Read(asset / L"a2.mp4") == "ORIGINAL-alias" &&
                  Read(asset / L"a3.mp4") == "ORIGINAL-alias",
              "junction, 8.3 and \\\\?\\ aliases of an installed movie never write the install");
        Check(Read(twinOf(pid, L"a1.mp4")) == "restored-alias" && Read(twinOf(pid, L"a2.mp4")) == "restored-alias" &&
                  Read(twinOf(pid, L"a3.mp4")) == "restored-alias",
              "aliased movie writes land on the resolved twin");
        RemoveDirectoryW(alias.c_str());  // the junction only, never its target
    }
    {
        // 2. The current sandbox's install-assets is a junction back to the installed root: fail closed.
        Write(asset / L"b.mp4", "ORIGINAL-b");
        const auto marker = root / "junction.marker";
        fs::path planted;
        auto [exited, pid] = runOps({L"rw", (asset / L"b.mp4").wstring()}, marker, [&](DWORD id) {
            const auto sandbox = logs / (L"save_sandbox_" + std::to_wstring(id));
            fs::create_directories(sandbox);
            planted = sandbox / L"install-assets";
            const std::wstring link = L"cmd /c mklink /J \"" + planted.wstring() + L"\" \"" +
                                      (root / L"STEAM" / L"juefigs" / L"KH2ReSource").wstring() + L"\" >nul";
            _wsystem(link.c_str());
        });
        Check(fs::exists(planted / L"zmovie" / L"en" / L"b.mp4"), "planted install-assets junction resolves to the install");
        Check(exited && Read(marker) == "failed" && Read(asset / L"b.mp4") == "ORIGINAL-b",
              "a junctioned install-assets is refused and the installed movie is unchanged");
        RemoveDirectoryW(planted.c_str());
        (void)pid;
    }
    {
        // 3a. Eight threads open the same movie for writing at once: one whole copy, every opener sees it.
        std::string big(32 << 20, 'x');
        big.replace(big.size() - 8, 8, "TAILTAIL");
        std::ofstream(asset / L"big.mp4", std::ios::binary).write(big.data(), static_cast<std::streamsize>(big.size()));
        const auto marker = root / "race.marker";
        auto [exited, pid] = runOps({L"race", (asset / L"big.mp4").wstring()}, marker, nullptr);
        const auto text = guardLog(logs, pid);
        size_t ready = 0;
        for (size_t at = text.find("asset twin ready"); at != std::string::npos; at = text.find("asset twin ready", at + 1))
            ++ready;
        const auto raced = Read(marker);
        const size_t space = raced.find(' ');  // "race:<tails> sharing-retries=N"
        Check(exited && raced.substr(0, space) == "race:TAILTAIL;" && ready == 1,
              "concurrent first opens copy the movie once and all see the whole copy");
        std::cout << "NOTE race " << (space == std::string::npos ? "no retry count" : raced.substr(space + 1)) << '\n';
        Check(Read(asset / L"big.mp4") == big && fs::file_size(twinOf(pid, L"big.mp4")) == big.size(),
              "raced movie: install unchanged, twin complete");
        // 3b. The copy fails (source locked): an OPEN_ALWAYS write fails and publishes no twin.
        Write(asset / L"c.mp4", "ORIGINAL-c");
        HANDLE lock = CreateFileW((asset / L"c.mp4").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        const auto failMarker = root / "copyfail.marker";
        auto [failExited, failPid] = runOps({L"ow", (asset / L"c.mp4").wstring()}, failMarker, nullptr);
        if (lock != INVALID_HANDLE_VALUE) CloseHandle(lock);
        const auto failedTwin = twinOf(failPid, L"c.mp4");
        Check(failExited && Read(failMarker) == "failed" && !fs::exists(failedTwin) &&
                  !fs::exists(failedTwin.wstring() + L".partial") && Read(asset / L"c.mp4") == "ORIGINAL-c",
              "a failed twin copy fails the open, leaves no twin and never reaches the install");
    }
    {
        // 3c. A planted twin .partial that links to another installed movie (Rhea eb389de1): the copy must not
        // write through it. The open fails, both installed movies are unchanged, and the link is left as found.
        Write(asset / L"p.mp4", "ORIGINAL-p");
        Write(asset / L"q.mp4", "ORIGINAL-q");
        const auto stampP = fs::last_write_time(asset / L"p.mp4"), stampQ = fs::last_write_time(asset / L"q.mp4");
        const auto marker = root / "partial.marker";
        fs::path partial;
        bool linked = false;
        auto [exited, pid] = runOps({L"rw", (asset / L"p.mp4").wstring()}, marker, [&](DWORD id) {
            partial = twinOf(id, L"p.mp4").wstring() + L".partial";
            fs::create_directories(partial.parent_path());
            linked = CreateSymbolicLinkW(partial.c_str(), (asset / L"q.mp4").c_str(),
                                         SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != FALSE;
        });
        if (!linked) {
            std::cout << "NOTE this account can't create file symlinks; the planted-partial case is not exercised\n";
        } else {
            Check(exited && Read(marker) == "failed", "a planted .partial symlink fails the movie open");
            Check(Read(asset / L"p.mp4") == "ORIGINAL-p" && Read(asset / L"q.mp4") == "ORIGINAL-q" &&
                      fs::last_write_time(asset / L"p.mp4") == stampP && fs::last_write_time(asset / L"q.mp4") == stampQ,
                  "planted .partial: neither installed movie's bytes or mtime change");
            Check(!fs::exists(twinOf(pid, L"p.mp4")), "planted .partial: no twin is published");
            DeleteFileW(partial.c_str());  // the link only, never its target
        }
    }
    {
        // 4. Path-based attribute writes on an installed movie are denied.
        Write(asset / L"d.mp4", "ORIGINAL-d");
        const DWORD before = GetFileAttributesW((asset / L"d.mp4").c_str());
        const auto marker = root / "attr.marker";
        auto [exited, pid] = runOps({L"attr", (asset / L"d.mp4").wstring(), L"attrA", (asset / L"d.mp4").wstring()},
                                    marker, nullptr);
        Check(exited && Read(marker) == "denied|denied" && GetFileAttributesW((asset / L"d.mp4").c_str()) == before,
              "SetFileAttributesW/A on an installed movie are denied; attributes unchanged");
        (void)pid;
    }
    {
        // 5. A log directory under KH2ReSource: saves still route to the sandbox; asset writes fail closed.
        const auto inside = root / L"STEAM" / L"juefigs" / L"KH2ReSource" / L"logs";
        fs::create_directories(inside);
        Write(guarded / L"e.png", "original-save");
        Write(asset / L"e.mp4", "ORIGINAL-e");
        SetEnvironmentVariableW(L"KH2COOP_LOG_DIR", inside.c_str());
        const auto marker = root / "insidelogs.marker";
        auto [exited, pid] = runOps({L"rw", (guarded / L"e.png").wstring(), L"rw", (asset / L"e.mp4").wstring()},
                                    marker, nullptr);
        SetEnvironmentVariableW(L"KH2COOP_LOG_DIR", logs.c_str());
        const auto saveTwin = inside / (L"save_sandbox_" + std::to_wstring(pid)) / L"Steam" / L"test" / L"e.png";
        Check(exited && Read(marker) == "wrote|failed" && Read(guarded / L"e.png") == "original-save" &&
                  Read(saveTwin) == "patched!-save" && Read(asset / L"e.mp4") == "ORIGINAL-e",
              "logs under KH2ReSource keep save routing; asset writes are denied");
    }
    fs::remove_all(root); // exactly the temp directory successfully created above
    return failures ? 1 : 0;
}
