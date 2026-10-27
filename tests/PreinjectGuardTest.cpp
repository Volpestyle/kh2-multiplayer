// Starts only copies of this harmless executable. All file writes are in a
// uniquely owned temp directory; no game process, save, network or Steam API.
#include "../tools/kh2ctl/src/preinject.hpp"
#include <cstdio>
#include <fstream>
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
        const BOOL deleted = DeleteFileA(fs::path(argv[2]).string().c_str());
        Write(argv[3], deleted ? "deleted" : "blocked");
        return deleted ? 3 : 0;
    }
    // Run from a copied exe so its directory stands in for the install root
    // (VUH-2763): movie, non-movie install file, outside file, twin tail, marker.
    if (argc == 7 && std::wstring(argv[1]) == L"--movie-child") {
        auto head = [](const wchar_t* path) {
            FILE* file = nullptr;
            char bytes[9] {};
            if (_wfopen_s(&file, path, L"rb") == 0 && file) {
                fread(bytes, 1, 8, file);
                fclose(file);
            }
            return std::string(bytes);
        };
        // The game's header write: fopen r+b, 8 bytes at offset 0.
        auto patch = [](const wchar_t* path, const char* text) {
            FILE* file = nullptr;
            if (_wfopen_s(&file, path, L"r+b") != 0 || !file) return std::string("failed");
            const bool wrote = fwrite(text, 1, 8, file) == 8;
            return std::string(fclose(file) == 0 && wrote ? "wrote" : "failed");
        };
        // Media Foundation's open goes through KernelBase CreateFile2.
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
        wchar_t logs[MAX_PATH] {};
        GetEnvironmentVariableW(L"KH2COOP_LOG_DIR", logs, MAX_PATH);
        const auto twin = fs::path(logs) / (L"save_sandbox_" + std::to_wstring(GetCurrentProcessId())) / argv[5];
        // One step per statement: operands of + are evaluated in no fixed order.
        std::string out = head(argv[2]);
        out += fs::exists(twin) ? "|twin-early" : "|no-twin";
        out += "|" + patch(argv[2], "decoded!");
        out += "|" + head(argv[2]);
        out += "|" + mediaHead(argv[2]);
        out += "|" + patch(argv[2], "scramble");
        out += "|" + head(argv[2]);
        out += "|" + patch(argv[3], "install!");
        out += "|" + patch(argv[4], "outside!");
        Write(argv[6], out.c_str());
        return 0;
    }
    wchar_t exe[MAX_PATH] {}, temp[MAX_PATH] {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    GetTempPathW(MAX_PATH, temp);
    const auto root = fs::path(temp) / (L"kh2_preinject_" + std::to_wstring(GetCurrentProcessId()));
    if (!fs::create_directory(root)) return 4; // never reuse or remove an unowned directory
    const auto guarded = root / L"My Games" / L"KINGDOM HEARTS HD 1.5+2.5 ReMIX" / L"Steam" / L"test";
    fs::create_directories(guarded);
    const auto logs = root / "logs";
    fs::create_directory(logs);
    SetEnvironmentVariableW(L"KH2COOP_LOG_DIR", logs.c_str());
    // This logged fixture must never inherit an out-of-lane self-test target.
    SetEnvironmentVariableW(L"KH2COOP_SAVEGUARD_TEST_DIR", nullptr);
    SetEnvironmentVariableW(L"KH2COOP_PREINJECT_TEST_FAIL_GUARD", nullptr);
    const auto dll = fs::path(exe).parent_path() / L"kh2coop_preinject_guard.dll";
    auto spawn = [&](const fs::path& save, const fs::path& marker) {
        STARTUPINFOW startup {};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION info {};
        std::wstring command = L"\"" + std::wstring(exe) + L"\" --child \"" + save.wstring() + L"\" \"" + marker.wstring() + L"\"";
        if (!CreateProcessW(exe, command.data(), nullptr, nullptr, FALSE,
                            CREATE_SUSPENDED, nullptr, root.c_str(), &startup, &info))
            throw std::runtime_error("offline child creation failed");
        return info;
    };
    for (int kind = 0; kind < 3; ++kind) {
        const bool existing = kind != 0;
        const auto save = guarded / (std::to_string(kind) + ".png");
        const auto marker = root / (std::to_string(kind) + ".marker");
        const char* original = kind == 2 ? "" : "original-save";
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
                Check(code == 0 && Read(marker) == "blocked", "first entry-point CRT write redirects and delete is denied");
                Check(existing ? fs::exists(save) && Read(save) == original : !fs::exists(save), "nonempty, empty and missing original paths preserved");
                const auto twin = logs / (L"save_sandbox_" + std::to_wstring(info.dwProcessId)) / L"Steam" / L"test" / save.filename();
                Check(Read(twin).find("boot-write") == 0, "boot write reaches only sandbox");
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
        // A shipped movie's first bytes are scrambled; the game decodes them
        // in place for playback and scrambles them again (VUH-2763).
        const auto install = root / L"install";
        const auto movies = install / L"STEAM" / L"juefigs" / L"KH2ReSource" / L"zmovie" / L"en";
        fs::create_directories(movies);
        const auto gameExe = install / fs::path(exe).filename();
        fs::copy_file(exe, gameExe);
        const auto movie = movies / L"me4.mp4";
        const auto data = install / L"STEAM" / L"other.dat";
        const auto outside = root / L"outside.mp4";
        const char* original = "SCRAMBLED-HEADER+payload";
        Write(movie, original);
        Write(data, "install-data");
        Write(outside, "outside-original");
        const auto stamp = fs::last_write_time(movie);
        const auto tail = fs::path(L"STEAM") / L"juefigs" / L"KH2ReSource" / L"zmovie" / L"en" / L"me4.mp4";
        const auto marker = root / L"movie.marker";
        STARTUPINFOW startup {};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION info {};
        std::wstring command = L"\"" + gameExe.wstring() + L"\" --movie-child \"" + movie.wstring() + L"\" \"" +
                               data.wstring() + L"\" \"" + outside.wstring() + L"\" \"" + tail.wstring() + L"\" \"" +
                               marker.wstring() + L"\"";
        if (!CreateProcessW(gameExe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr,
                            install.c_str(), &startup, &info))
            throw std::runtime_error("offline movie child creation failed");
        Child child(info);
        const auto error = child.ProtectAndResume([&](DWORD pid) { return InjectDll(pid, dll); }, 10000);
        Check(error.empty(), "movie child: actual SaveGuard acknowledged before resume");
        if (error.empty()) {
            Check(WaitForSingleObject(info.hProcess, 10000) == WAIT_OBJECT_0, "movie child exits");
            Check(Read(marker) == "SCRAMBLE|no-twin|wrote|decoded!|decoded!|wrote|scramble|wrote|wrote",
                  "movie: read before any write sees the install file; header writes and later reads use the twin");
            Check(Read(movie) == original && fs::last_write_time(movie) == stamp,
                  "install movie bytes and mtime unchanged");
            const auto twin = logs / (L"save_sandbox_" + std::to_wstring(info.dwProcessId)) / tail;
            Check(Read(twin) == std::string("scramble") + (original + 8), "movie twin holds the copy plus both writes");
            Check(Read(data) == "install!data", "non-movie install file passes through");
            Check(Read(outside) == "outside!original", "movie outside the install passes through");
            const auto text = Read(logs / (L"preinject_guard_" + std::to_wstring(info.dwProcessId) + L".log"));
            Check(text.find("[saveguard] redirected CreateFileW #1: ")!= std::string::npos &&
                  text.find("me4.mp4 -> ") != std::string::npos && text.find("other.dat") == std::string::npos &&
                  text.find("twin copy failed") == std::string::npos,
                  "movie redirects are logged; non-movie and outside paths are not");
        }
    }
    fs::remove_all(root); // exactly the temp directory successfully created above
    return failures ? 1 : 0;
}
