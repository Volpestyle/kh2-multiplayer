// Starts only copies of this harmless executable. All file writes are in a
// uniquely owned temp directory; no game process, save, network or Steam API.
#include "../tools/kh2ctl/src/preinject.hpp"
#include <chrono>
#include <cstdio>
#include <fstream>
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
    fs::remove_all(root); // exactly the temp directory successfully created above
    return failures ? 1 : 0;
}
