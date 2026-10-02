#include "kh2coop/GameBridgePC.hpp"
#include "kh2coop/InputMailbox.hpp"
#include "kh2coop/Types.hpp"

#include <Windows.h>
#include <TlHelp32.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifndef KH2COOP_SOURCE_DIR
#define KH2COOP_SOURCE_DIR "."
#endif

namespace {

using kh2coop::GameBridgePC;
using kh2coop::InputButtons;
using kh2coop::InputFrame;
using kh2coop::MailboxWriter;
using kh2coop::RoomState;

struct CommandResult {
    int exitCode {0};
    std::string json;
};

struct ProcessCapture {
    bool launched {false};
    DWORD exitCode {0};
    std::string output;
};

constexpr int kDefaultAttachTimeoutMs = 5000;
constexpr int kDefaultPollMs = 250;

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return value;
}

std::string JsonEscape(const std::string& value) {
    std::ostringstream escaped;
    for (char ch : value) {
        switch (ch) {
        case '\\':
            escaped << "\\\\";
            break;
        case '"':
            escaped << "\\\"";
            break;
        case '\b':
            escaped << "\\b";
            break;
        case '\f':
            escaped << "\\f";
            break;
        case '\n':
            escaped << "\\n";
            break;
        case '\r':
            escaped << "\\r";
            break;
        case '\t':
            escaped << "\\t";
            break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20) {
                const unsigned char code = static_cast<unsigned char>(ch);
                escaped << "\\u"
                        << "00"
                        << "0123456789abcdef"[(code >> 4) & 0x0F]
                        << "0123456789abcdef"[code & 0x0F];
            } else {
                escaped << ch;
            }
            break;
        }
    }
    return escaped.str();
}

std::string JsonBool(bool value) {
    return value ? "true" : "false";
}

std::string JsonString(const std::string& value) {
    return "\"" + JsonEscape(value) + "\"";
}

template <typename T>
T ParseNumber(const std::string& raw, const char* flagName) {
    std::istringstream input(raw);
    T value {};
    input >> value;
    if (!input || !input.eof()) {
        throw std::runtime_error(std::string("Invalid value for ") + flagName +
                                 ": " + raw);
    }
    return value;
}

bool ConsumeFlag(std::vector<std::string>& args, const std::string& flag) {
    const auto it = std::find(args.begin(), args.end(), flag);
    if (it == args.end()) {
        return false;
    }
    args.erase(it);
    return true;
}

std::optional<std::string> ConsumeOption(std::vector<std::string>& args,
                                         const std::string& flag) {
    const auto it = std::find(args.begin(), args.end(), flag);
    if (it == args.end()) {
        return std::nullopt;
    }
    if (std::next(it) == args.end()) {
        throw std::runtime_error("Missing value for " + flag);
    }

    const auto value = *std::next(it);
    args.erase(it, std::next(it, 2));
    return value;
}

std::filesystem::path RepoRoot() {
    return std::filesystem::path(KH2COOP_SOURCE_DIR);
}

std::uint64_t NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()).count();
}

void SleepMs(int durationMs) {
    if (durationMs > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(durationMs));
    }
}

bool WaitForAttach(GameBridgePC& game, int timeoutMs, int pollMs) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

    do {
        if (game.Attach()) {
            return true;
        }
        SleepMs(pollMs);
    } while (std::chrono::steady_clock::now() < deadline);

    return game.Attach();
}

std::string RoomStateToJson(const RoomState& room) {
    std::ostringstream out;
    out << "{"
        << "\"worldId\":" << room.worldId << ","
        << "\"roomId\":" << room.roomId << ","
        << "\"mapProgram\":" << room.mapProgram << ","
        << "\"battleProgram\":" << room.battleProgram << ","
        << "\"eventProgram\":" << room.eventProgram << ","
        << "\"inTransition\":" << JsonBool(room.inTransition) << ","
        << "\"inCutscene\":" << JsonBool(room.inCutscene)
        << "}";
    return out.str();
}

std::string ActorStateToJson(
    const std::optional<kh2coop::ActorState>& actor) {
    if (!actor.has_value()) {
        return "null";
    }

    std::ostringstream out;
    out << "{"
        << "\"actorId\":" << actor->actorId << ","
        << "\"slot\":" << static_cast<int>(actor->slot) << ","
        << "\"position\":{"
            << "\"x\":" << actor->position.x << ","
            << "\"y\":" << actor->position.y << ","
            << "\"z\":" << actor->position.z
        << "},"
        << "\"rotationY\":" << actor->rotationY << ","
        << "\"velocity\":{"
            << "\"x\":" << actor->velocity.x << ","
            << "\"y\":" << actor->velocity.y << ","
            << "\"z\":" << actor->velocity.z
        << "},"
        << "\"motionId\":" << actor->motionId << ","
        << "\"action\":" << static_cast<int>(actor->action) << ","
        << "\"comboStep\":" << actor->comboStep << ","
        << "\"hp\":" << actor->hp << ","
        << "\"mp\":" << actor->mp << ","
        << "\"drive\":" << actor->drive << ","
        << "\"targetId\":" << actor->targetId << ","
        << "\"airborne\":" << JsonBool(actor->airborne) << ","
        << "\"invuln\":" << JsonBool(actor->invuln) << ","
        << "\"staggered\":" << JsonBool(actor->staggered) << ","
        << "\"downed\":" << JsonBool(actor->downed)
        << "}";
    return out.str();
}

CommandResult MakeError(std::string error, int exitCode = 1) {
    return {exitCode, "{\"ok\":false,\"error\":" + JsonString(error) + "}"};
}

CommandResult MakeAttachTimeout(const char* context) {
    return MakeError(std::string("Timed out waiting for KH2 process for ") +
                     context);
}

CommandResult BuildStateJson(GameBridgePC& game) {
    if (!game.IsAttached()) {
        return {0, "{\"ok\":true,\"attached\":false}"};
    }

    game.Tick();
    const auto room = game.ReadRoomState();
    const bool atTitleOrLoading = room.worldId == 0xFFU || room.roomId == 0xFFU;
    const auto player = game.ReadActorState(kh2coop::SlotType::Player);
    const auto friend1 = game.ReadActorState(kh2coop::SlotType::Friend1);
    const auto friend2 = game.ReadActorState(kh2coop::SlotType::Friend2);

    std::ostringstream out;
    out << "{"
        << "\"ok\":true,"
        << "\"attached\":true,"
        << "\"processId\":" << game.ProcessId() << ","
        << "\"atTitleOrLoading\":" << JsonBool(atTitleOrLoading) << ","
        << "\"room\":" << RoomStateToJson(room) << ","
        << "\"actors\":{"
            << "\"player\":" << ActorStateToJson(player) << ","
            << "\"friend1\":" << ActorStateToJson(friend1) << ","
            << "\"friend2\":" << ActorStateToJson(friend2)
        << "}"
        << "}";
    return {0, out.str()};
}

template <typename Predicate>
CommandResult WaitForRoomState(const char* label, Predicate&& predicate,
                               int timeoutMs, int pollMs) {
    GameBridgePC game;
    if (!WaitForAttach(game, timeoutMs, pollMs)) {
        return MakeAttachTimeout(label);
    }

    const auto startedAt = std::chrono::steady_clock::now();
    const auto deadline = startedAt + std::chrono::milliseconds(timeoutMs);

    while (std::chrono::steady_clock::now() <= deadline) {
        game.Tick();
        const auto room = game.ReadRoomState();
        if (predicate(room)) {
            const auto waitedMs = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - startedAt).count());

            std::ostringstream out;
            out << "{"
                << "\"ok\":true,"
                << "\"processId\":" << game.ProcessId() << ","
                << "\"waitedMs\":" << waitedMs << ","
                << "\"room\":" << RoomStateToJson(room)
                << "}";
            return {0, out.str()};
        }
        SleepMs(pollMs);
    }

    const auto room = game.ReadRoomState();
    std::ostringstream out;
    out << "{"
        << "\"ok\":false,"
        << "\"timeout\":true,"
        << "\"processId\":" << game.ProcessId() << ","
        << "\"room\":" << RoomStateToJson(room) << ","
        << "\"error\":" << JsonString(std::string("Timed out waiting for ") +
                                      label) << "}";
    return {1, out.str()};
}

BOOL CALLBACK EnumWindowsForPid(HWND hwnd, LPARAM lParam) {
    auto* ctx = reinterpret_cast<std::pair<DWORD, HWND>*>(lParam);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != ctx->first || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER)) {
        return TRUE;
    }
    ctx->second = hwnd;
    return FALSE;
}

std::optional<HWND> FindWindowForPid(DWORD pid) {
    std::pair<DWORD, HWND> ctx {pid, nullptr};
    EnumWindows(EnumWindowsForPid, reinterpret_cast<LPARAM>(&ctx));
    if (!ctx.second) {
        return std::nullopt;
    }
    return ctx.second;
}

std::string WindowTitle(HWND hwnd) {
    wchar_t buffer[512] = {};
    const int copied = GetWindowTextW(hwnd, buffer,
                                      static_cast<int>(std::size(buffer)));
    if (copied <= 0) {
        return {};
    }

    const int needed = WideCharToMultiByte(
        CP_UTF8, 0, buffer, copied, nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return {};
    }

    std::string utf8(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, buffer, copied,
                        utf8.data(), needed, nullptr, nullptr);
    return utf8;
}

// ============================================================================
// Rig: hands-free launch, injection, and owned-process tracking
//
// The rig only ever kills KH2 processes it launched. Ownership is recorded as
// (pid, creation time) in build/rig/owned.txt so a reused PID never matches.
// ============================================================================

constexpr const wchar_t* kKh2ExeName = L"KINGDOM HEARTS II FINAL MIX.exe";
constexpr const wchar_t* kDefaultGameDir =
    L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\"
    L"KINGDOM HEARTS -HD 1.5+2.5 ReMIX-";

std::filesystem::path RigDir() {
    return RepoRoot() / "build" / "rig";
}

std::filesystem::path GameDir(const std::optional<std::string>& override) {
    if (override) return std::filesystem::path(*override);
    wchar_t buffer[MAX_PATH] = {};
    const DWORD len = GetEnvironmentVariableW(L"KH2_GAME_DIR", buffer, MAX_PATH);
    if (len > 0 && len < MAX_PATH) return std::filesystem::path(buffer);
    return std::filesystem::path(kDefaultGameDir);
}

std::uint64_t ProcessCreationTime(HANDLE process) {
    FILETIME created {}, exited {}, kernel {}, user {};
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user)) return 0;
    return (static_cast<std::uint64_t>(created.dwHighDateTime) << 32) |
           created.dwLowDateTime;
}

struct Kh2Process {
    DWORD pid {0};
    std::uint64_t creationTime {0};
};

std::vector<Kh2Process> ListKh2Processes() {
    std::vector<Kh2Process> result;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return result;
    PROCESSENTRY32W entry {};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Process32FirstW(snapshot, &entry); more;
         more = Process32NextW(snapshot, &entry)) {
        if (_wcsicmp(entry.szExeFile, kKh2ExeName) != 0) continue;
        Kh2Process proc {entry.th32ProcessID, 0};
        if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                   entry.th32ProcessID)) {
            proc.creationTime = ProcessCreationTime(h);
            CloseHandle(h);
        }
        result.push_back(proc);
    }
    CloseHandle(snapshot);
    return result;
}

std::vector<Kh2Process> ReadOwned() {
    std::vector<Kh2Process> owned;
    std::ifstream in(RigDir() / "owned.txt");
    Kh2Process proc;
    while (in >> proc.pid >> proc.creationTime) owned.push_back(proc);
    return owned;
}

void WriteOwned(const std::vector<Kh2Process>& owned) {
    std::filesystem::create_directories(RigDir());
    std::ofstream out(RigDir() / "owned.txt", std::ios::trunc);
    for (const auto& proc : owned) {
        out << proc.pid << " " << proc.creationTime << "\n";
    }
}

bool IsOwned(const Kh2Process& proc, const std::vector<Kh2Process>& owned) {
    return std::any_of(owned.begin(), owned.end(), [&](const Kh2Process& o) {
        return o.pid == proc.pid && o.creationTime == proc.creationTime;
    });
}

// Drops registry entries whose process is gone.
std::vector<Kh2Process> PruneOwned() {
    const auto live = ListKh2Processes();
    std::vector<Kh2Process> kept;
    for (const auto& o : ReadOwned()) {
        if (IsOwned(o, live)) kept.push_back(o);
    }
    WriteOwned(kept);
    return kept;
}

// LoadLibraryW in the target via a remote thread. Returns an error or empty.
std::string InjectDll(DWORD pid, const std::filesystem::path& dll) {
    HANDLE process = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE |
            PROCESS_VM_READ | PROCESS_QUERY_INFORMATION,
        FALSE, pid);
    if (!process) return "OpenProcess failed: " + std::to_string(GetLastError());

    const std::wstring path = dll.wstring();
    const SIZE_T bytes = (path.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(process, nullptr, bytes,
                                  MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    std::string error;
    if (!remote) {
        error = "VirtualAllocEx failed: " + std::to_string(GetLastError());
    } else if (!WriteProcessMemory(process, remote, path.c_str(), bytes, nullptr)) {
        error = "WriteProcessMemory failed: " + std::to_string(GetLastError());
    } else {
        auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
            GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
        HANDLE thread = CreateRemoteThread(process, nullptr, 0, loadLibrary,
                                           remote, 0, nullptr);
        if (!thread) {
            error = "CreateRemoteThread failed: " + std::to_string(GetLastError());
        } else {
            if (WaitForSingleObject(thread, 15000) != WAIT_OBJECT_0) {
                error = "LoadLibraryW did not return within 15 s";
            } else {
                DWORD moduleLow = 0;
                GetExitCodeThread(thread, &moduleLow);
                if (moduleLow == 0) error = "LoadLibraryW returned NULL in target";
            }
            CloseHandle(thread);
        }
    }
    if (remote) VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    CloseHandle(process);
    return error;
}

// Copies the staged DLL to a unique file so rebuilds never hit a locked DLL.
std::filesystem::path StageDllCopy(const std::optional<std::string>& dllOverride) {
    const std::filesystem::path source = dllOverride
        ? std::filesystem::path(*dllOverride)
        : RepoRoot() / "build" / "inject" / "staging" / "kh2coop_inject.dll";
    if (!std::filesystem::exists(source)) {
        throw std::runtime_error("Inject DLL not found: " + source.string() +
                                 " (build the kh2coop_inject target)");
    }
    const auto dir = RigDir() / "dll";
    std::filesystem::create_directories(dir);
    const auto dest = dir / ("kh2coop_inject_" + std::to_string(NowMs()) + ".dll");
    std::filesystem::copy_file(source, dest);
    return dest;
}

std::filesystem::path LogPathForPid(DWORD pid) {
    return RigDir() / "logs" / ("kh2coop_inject_" + std::to_string(pid) + ".log");
}

// Waits for the DLL's init log to report its hooks; returns hook lines/errors.
void CollectInitLog(DWORD pid, int timeoutMs, std::vector<std::string>& hooks,
                    std::vector<std::string>& errors, bool& complete) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    complete = false;
    do {
        hooks.clear();
        errors.clear();
        std::ifstream in(LogPathForPid(pid));
        std::string line;
        while (std::getline(in, line)) {
            if (line.find("hook installed") != std::string::npos) {
                hooks.push_back(line.substr(line.find_first_not_of(' ')));
            }
            if (line.find("ERROR") != std::string::npos) errors.push_back(line);
            if (line.find("InputCollector hook installed") != std::string::npos) {
                complete = true;
            }
        }
        if (complete || !errors.empty()) return;
        SleepMs(250);
    } while (std::chrono::steady_clock::now() < deadline);
}

std::string JsonStringArray(const std::vector<std::string>& values) {
    std::string out = "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) out += ",";
        out += JsonString(values[i]);
    }
    return out + "]";
}

CommandResult InjectAndReport(DWORD pid, const std::optional<std::string>& dllOverride,
                              int initTimeoutMs, const char* command) {
    const auto dll = StageDllCopy(dllOverride);
    const std::string error = InjectDll(pid, dll);
    if (!error.empty()) return MakeError(error);

    std::vector<std::string> hooks, errors;
    bool complete = false;
    CollectInitLog(pid, initTimeoutMs, hooks, errors, complete);

    std::string title;
    if (auto hwnd = FindWindowForPid(pid)) title = WindowTitle(*hwnd);

    std::ostringstream out;
    out << "{"
        << "\"ok\":" << JsonBool(complete && errors.empty()) << ","
        << "\"command\":" << JsonString(command) << ","
        << "\"processId\":" << pid << ","
        << "\"windowTitle\":" << JsonString(title) << ","
        << "\"dll\":" << JsonString(dll.string()) << ","
        << "\"log\":" << JsonString(LogPathForPid(pid).string()) << ","
        << "\"hooksInstalled\":" << JsonBool(complete) << ","
        << "\"hooks\":" << JsonStringArray(hooks) << ","
        << "\"errors\":" << JsonStringArray(errors)
        << "}";
    return {complete && errors.empty() ? 0 : 1, out.str()};
}

struct LaunchOptions {
    std::optional<std::string> gameDir;
    std::optional<std::string> dll;
    bool noInject {false};
    int windowTimeoutMs {60000};
    int settleMs {1500};
    int initTimeoutMs {15000};
};

// Consumes the launch options shared by launch, restart and boot-load-save.
LaunchOptions ConsumeLaunchOptions(std::vector<std::string>& args) {
    LaunchOptions options;
    options.gameDir = ConsumeOption(args, "--game-dir");
    options.dll = ConsumeOption(args, "--dll");
    options.noInject = ConsumeFlag(args, "--no-inject");
    options.windowTimeoutMs =
        ParseNumber<int>(ConsumeOption(args, "--window-timeout-ms").value_or("60000"),
                         "--window-timeout-ms");
    options.settleMs =
        ParseNumber<int>(ConsumeOption(args, "--settle-ms").value_or("1500"),
                         "--settle-ms");
    options.initTimeoutMs =
        ParseNumber<int>(ConsumeOption(args, "--init-timeout-ms").value_or("15000"),
                         "--init-timeout-ms");
    return options;
}

CommandResult LaunchInstance(const LaunchOptions& options, const char* command) {
    const auto& dllOverride = options.dll;
    const bool noInject = options.noInject;
    const int windowTimeoutMs = options.windowTimeoutMs;
    const int settleMs = options.settleMs;
    const int initTimeoutMs = options.initTimeoutMs;

    const auto gameDir = GameDir(options.gameDir);
    const auto exe = gameDir / kKh2ExeName;
    if (!std::filesystem::exists(exe)) {
        return MakeError("KH2 executable not found: " + exe.string());
    }

    // The DLL reads KH2COOP_LOG_DIR at init; the child inherits it.
    const auto logDir = RigDir() / "logs";
    std::filesystem::create_directories(logDir);
    SetEnvironmentVariableW(L"KH2COOP_LOG_DIR", logDir.wstring().c_str());

    STARTUPINFOW startup {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info {};
    std::wstring commandLine = L"\"" + exe.wstring() + L"\"";
    if (!CreateProcessW(exe.wstring().c_str(), commandLine.data(), nullptr,
                        nullptr, FALSE, 0, nullptr, gameDir.wstring().c_str(),
                        &startup, &info)) {
        return MakeError("CreateProcessW failed: " + std::to_string(GetLastError()));
    }

    auto owned = PruneOwned();
    owned.push_back({info.dwProcessId, ProcessCreationTime(info.hProcess)});
    WriteOwned(owned);

    // Wait for the game window so the CRT and loader are fully up.
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(windowTimeoutMs);
    bool windowFound = false;
    while (std::chrono::steady_clock::now() < deadline) {
        if (WaitForSingleObject(info.hProcess, 0) == WAIT_OBJECT_0) {
            DWORD exitCode = 0;
            GetExitCodeProcess(info.hProcess, &exitCode);
            CloseHandle(info.hThread);
            CloseHandle(info.hProcess);
            return MakeError("KH2 exited during startup with code " +
                             std::to_string(exitCode));
        }
        if (FindWindowForPid(info.dwProcessId)) {
            windowFound = true;
            break;
        }
        SleepMs(250);
    }
    CloseHandle(info.hThread);
    CloseHandle(info.hProcess);
    if (!windowFound) {
        return MakeError("KH2 window did not appear for PID " +
                         std::to_string(info.dwProcessId));
    }
    SleepMs(settleMs);

    if (noInject) {
        std::ostringstream out;
        out << "{\"ok\":true,\"command\":" << JsonString(command)
            << ",\"processId\":" << info.dwProcessId << ",\"injected\":false}";
        return {0, out.str()};
    }
    return InjectAndReport(info.dwProcessId, dllOverride, initTimeoutMs, command);
}

CommandResult CmdLaunch(std::vector<std::string> args) {
    const auto options = ConsumeLaunchOptions(args);
    if (!args.empty()) {
        throw std::runtime_error("Unexpected argument for launch: " + args.front());
    }
    return LaunchInstance(options, "launch");
}

CommandResult CmdInject(std::vector<std::string> args) {
    const auto pidRaw = ConsumeOption(args, "--pid");
    const auto dllOverride = ConsumeOption(args, "--dll");
    const int initTimeoutMs =
        ParseNumber<int>(ConsumeOption(args, "--init-timeout-ms").value_or("15000"),
                         "--init-timeout-ms");
    if (!pidRaw) throw std::runtime_error("inject requires --pid");
    if (!args.empty()) {
        throw std::runtime_error("Unexpected argument for inject: " + args.front());
    }
    return InjectAndReport(ParseNumber<DWORD>(*pidRaw, "--pid"), dllOverride,
                           initTimeoutMs, "inject");
}

CommandResult CmdInstances(std::vector<std::string> args) {
    if (!args.empty()) {
        throw std::runtime_error("Unexpected argument for instances: " + args.front());
    }
    const auto owned = PruneOwned();
    std::ostringstream out;
    out << "{\"ok\":true,\"instances\":[";
    bool first = true;
    for (const auto& proc : ListKh2Processes()) {
        std::string title;
        if (auto hwnd = FindWindowForPid(proc.pid)) title = WindowTitle(*hwnd);
        out << (first ? "" : ",") << "{"
            << "\"processId\":" << proc.pid << ","
            << "\"owned\":" << JsonBool(IsOwned(proc, owned)) << ","
            << "\"windowTitle\":" << JsonString(title) << ","
            << "\"log\":" << JsonString(LogPathForPid(proc.pid).string())
            << "}";
        first = false;
    }
    out << "]}";
    return {0, out.str()};
}

std::string JsonDwordArray(const std::vector<DWORD>& values) {
    std::string out = "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) out += ",";
        out += std::to_string(values[i]);
    }
    return out + "]";
}

// Kills rig-owned KH2 processes (all, or only `pid`). Unowned ones are listed
// in skippedUnowned and left running.
void KillOwned(std::optional<DWORD> pid, std::vector<DWORD>& killed,
               std::vector<DWORD>& skippedUnowned) {
    const auto owned = PruneOwned();
    for (const auto& proc : ListKh2Processes()) {
        if (pid && proc.pid != *pid) continue;
        if (!IsOwned(proc, owned)) {
            skippedUnowned.push_back(proc.pid);
            continue;
        }
        if (HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, proc.pid)) {
            TerminateProcess(h, 0);
            WaitForSingleObject(h, 10000);
            CloseHandle(h);
            killed.push_back(proc.pid);
        }
    }
    PruneOwned();
}

// Kills rig-owned KH2 processes only. Unowned ones (James playing) are left.
CommandResult CmdKill(std::vector<std::string> args) {
    const auto pidRaw = ConsumeOption(args, "--pid");
    const bool all = ConsumeFlag(args, "--all");
    if (!args.empty()) {
        throw std::runtime_error("Unexpected argument for kill: " + args.front());
    }
    if (!pidRaw && !all) throw std::runtime_error("kill requires --pid N or --all");

    std::optional<DWORD> pid;
    if (pidRaw) pid = ParseNumber<DWORD>(*pidRaw, "--pid");
    std::vector<DWORD> killed, skippedUnowned;
    KillOwned(pid, killed, skippedUnowned);

    std::ostringstream out;
    out << "{\"ok\":true,\"killed\":" << JsonDwordArray(killed)
        << ",\"skippedUnowned\":" << JsonDwordArray(skippedUnowned) << "}";
    return {0, out.str()};
}

bool FocusWindow(HWND hwnd) {
    if (IsIconic(hwnd)) {
        ShowWindow(hwnd, SW_RESTORE);
    } else {
        ShowWindow(hwnd, SW_SHOW);
    }

    // The foreground lock only lets the thread that owns the current
    // foreground window hand focus away, so attach to that thread's input
    // queue (attaching to the target's thread is not enough).
    const DWORD currentThread = GetCurrentThreadId();
    const HWND foreground = GetForegroundWindow();
    const DWORD foregroundThread =
        foreground ? GetWindowThreadProcessId(foreground, nullptr) : 0;
    bool attachedInput = false;
    if (foregroundThread != 0 && foregroundThread != currentThread) {
        attachedInput = AttachThreadInput(currentThread, foregroundThread, TRUE) != 0;
    }

    SetForegroundWindow(hwnd);
    BringWindowToTop(hwnd);

    if (attachedInput) {
        AttachThreadInput(currentThread, foregroundThread, FALSE);
    }

    if (GetForegroundWindow() != hwnd) {
        // Fallback: a synthetic ALT press counts as the last input event,
        // which unlocks SetForegroundWindow for this process.
        INPUT alt[2] {};
        alt[0].type = INPUT_KEYBOARD;
        alt[0].ki.wVk = VK_MENU;
        alt[1] = alt[0];
        alt[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, alt, sizeof(INPUT));
        SetForegroundWindow(hwnd);
        BringWindowToTop(hwnd);
    }

    for (int i = 0; i < 20 && GetForegroundWindow() != hwnd; ++i) SleepMs(25);
    return GetForegroundWindow() == hwnd;
}

struct KeySpec {
    WORD vk {0};
    bool shift {false};
    bool ctrl {false};
    bool alt {false};
};

std::optional<KeySpec> ParseKeySpec(const std::string& rawName) {
    const auto name = ToLower(rawName);
    static const std::array<std::pair<const char*, WORD>, 19> namedKeys {{
        {"enter", VK_RETURN},
        {"return", VK_RETURN},
        {"space", VK_SPACE},
        {"up", VK_UP},
        {"down", VK_DOWN},
        {"left", VK_LEFT},
        {"right", VK_RIGHT},
        {"escape", VK_ESCAPE},
        {"esc", VK_ESCAPE},
        {"tab", VK_TAB},
        {"backspace", VK_BACK},
        {"delete", VK_DELETE},
        {"home", VK_HOME},
        {"end", VK_END},
        {"pageup", VK_PRIOR},
        {"pagedown", VK_NEXT},
        {"f1", VK_F1},
        {"f5", VK_F5},
        {"f8", VK_F8},
    }};

    for (const auto& [label, vk] : namedKeys) {
        if (name == label) {
            return KeySpec {vk, false, false, false};
        }
    }

    if (name.size() == 1) {
        const SHORT encoded = VkKeyScanA(name[0]);
        if (encoded == -1) {
            return std::nullopt;
        }

        const BYTE vk = LOBYTE(encoded);
        const BYTE mods = HIBYTE(encoded);
        return KeySpec {
            vk,
            (mods & 1) != 0,
            (mods & 2) != 0,
            (mods & 4) != 0
        };
    }

    return std::nullopt;
}

bool SendVk(WORD vk, DWORD flags) {
    INPUT input {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.dwFlags = flags;
    return SendInput(1, &input, sizeof(INPUT)) == 1;
}

bool SendKeyPress(const KeySpec& spec, int durationMs) {
    if (spec.shift && !SendVk(VK_SHIFT, 0)) return false;
    if (spec.ctrl && !SendVk(VK_CONTROL, 0)) return false;
    if (spec.alt && !SendVk(VK_MENU, 0)) return false;
    if (!SendVk(spec.vk, 0)) return false;

    SleepMs(durationMs);

    bool ok = SendVk(spec.vk, KEYEVENTF_KEYUP);
    if (spec.alt) ok = SendVk(VK_MENU, KEYEVENTF_KEYUP) && ok;
    if (spec.ctrl) ok = SendVk(VK_CONTROL, KEYEVENTF_KEYUP) && ok;
    if (spec.shift) ok = SendVk(VK_SHIFT, KEYEVENTF_KEYUP) && ok;
    return ok;
}

constexpr std::uint16_t kRawButtonDpadUp = 0x0001;
constexpr std::uint16_t kRawButtonDpadDown = 0x0002;
constexpr std::uint16_t kRawButtonDpadLeft = 0x0004;
constexpr std::uint16_t kRawButtonDpadRight = 0x0008;
constexpr std::uint16_t kRawButtonStart = 0x0010;
constexpr std::uint16_t kRawButtonBack = 0x0020;
constexpr std::uint16_t kRawButtonL3 = 0x0040;
constexpr std::uint16_t kRawButtonR3 = 0x0080;
constexpr std::uint16_t kRawButtonL1 = 0x0100;
constexpr std::uint16_t kRawButtonR1 = 0x0200;
constexpr std::uint16_t kRawButtonCross = 0x1000;
constexpr std::uint16_t kRawButtonCircle = 0x2000;
constexpr std::uint16_t kRawButtonSquare = 0x4000;
constexpr std::uint16_t kRawButtonTriangle = 0x8000;

std::uint32_t ParseFriendMailboxSlot(const std::string& raw) {
    const auto lower = ToLower(raw);
    if (lower == "friend1" || lower == "friend_1" || lower == "1" ||
        lower == "p2") {
        return kh2coop::MAILBOX_SLOT_FRIEND1;
    }
    if (lower == "friend2" || lower == "friend_2" || lower == "2" ||
        lower == "p3") {
        return kh2coop::MAILBOX_SLOT_FRIEND2;
    }

    throw std::runtime_error(
        "Unsupported slot '" + raw + "'. Use friend1 or friend2.");
}

void ApplyRawButtonName(std::uint16_t& buttons, const std::string& rawName) {
    const auto name = ToLower(rawName);
    if (name.empty()) return;
    if (name == "cross" || name == "a" || name == "confirm") {
        buttons |= kRawButtonCross;
        return;
    }
    if (name == "circle" || name == "b" || name == "cancel") {
        buttons |= kRawButtonCircle;
        return;
    }
    if (name == "square" || name == "x") {
        buttons |= kRawButtonSquare;
        return;
    }
    if (name == "triangle" || name == "y" || name == "menu") {
        buttons |= kRawButtonTriangle;
        return;
    }
    if (name == "l1" || name == "lb") {
        buttons |= kRawButtonL1;
        return;
    }
    if (name == "r1" || name == "rb" || name == "lockon" || name == "lock-on") {
        buttons |= kRawButtonR1;
        return;
    }
    if (name == "start") {
        buttons |= kRawButtonStart;
        return;
    }
    if (name == "select" || name == "back") {
        buttons |= kRawButtonBack;
        return;
    }
    if (name == "l3") {
        buttons |= kRawButtonL3;
        return;
    }
    if (name == "r3") {
        buttons |= kRawButtonR3;
        return;
    }
    if (name == "dup" || name == "dpadup" || name == "up") {
        buttons |= kRawButtonDpadUp;
        return;
    }
    if (name == "ddown" || name == "dpaddown" || name == "down") {
        buttons |= kRawButtonDpadDown;
        return;
    }
    if (name == "dleft" || name == "dpadleft" || name == "left") {
        buttons |= kRawButtonDpadLeft;
        return;
    }
    if (name == "dright" || name == "dpadright" || name == "right") {
        buttons |= kRawButtonDpadRight;
        return;
    }

    throw std::runtime_error("Unsupported raw button name: " + rawName);
}

void ApplyButtonName(InputButtons& buttons, const std::string& rawName) {
    const auto name = ToLower(rawName);
    if (name.empty()) return;
    if (name == "attack") {
        buttons.attack = true;
        return;
    }
    if (name == "jump") {
        buttons.jump = true;
        return;
    }
    if (name == "guard") {
        buttons.guard = true;
        return;
    }
    if (name == "dodge") {
        buttons.dodge = true;
        return;
    }
    if (name == "lockon" || name == "lock-on") {
        buttons.lockOn = true;
        return;
    }
    if (name == "magic1") {
        buttons.magic1 = true;
        return;
    }
    if (name == "magic2") {
        buttons.magic2 = true;
        return;
    }
    if (name == "special1") {
        buttons.special1 = true;
        return;
    }
    if (name == "special2") {
        buttons.special2 = true;
        return;
    }

    throw std::runtime_error("Unsupported button name: " + rawName);
}

void ApplyButtonsCsv(InputButtons& buttons, const std::string& csv) {
    std::istringstream input(csv);
    std::string token;
    while (std::getline(input, token, ',')) {
        token.erase(std::remove_if(token.begin(), token.end(),
                                   [](unsigned char ch) {
                                       return std::isspace(ch) != 0;
                                   }),
                    token.end());
        if (!token.empty()) {
            ApplyButtonName(buttons, token);
        }
    }
}

void ApplyRawButtonsCsv(std::uint16_t& buttons, const std::string& csv) {
    std::istringstream input(csv);
    std::string token;
    while (std::getline(input, token, ',')) {
        token.erase(std::remove_if(token.begin(), token.end(),
                                   [](unsigned char ch) {
                                       return std::isspace(ch) != 0;
                                   }),
                    token.end());
        if (!token.empty()) {
            ApplyRawButtonName(buttons, token);
        }
    }
}

ProcessCapture RunProcessCapture(const std::wstring& commandLine,
                                 const std::filesystem::path& workingDirectory) {
    SECURITY_ATTRIBUTES sa {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
        return {};
    }

    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si {};
    si.cb = sizeof(si);
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi {};
    std::wstring mutableCommand = commandLine;
    std::wstring workdirWide = workingDirectory.wstring();

    const BOOL created = CreateProcessW(
        nullptr,
        mutableCommand.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        workdirWide.c_str(),
        &si,
        &pi);

    CloseHandle(writePipe);
    writePipe = nullptr;

    ProcessCapture capture {};
    capture.launched = created != 0;
    if (!created) {
        CloseHandle(readPipe);
        return capture;
    }

    std::string output;
    char buffer[4096];
    DWORD bytesRead = 0;
    while (ReadFile(readPipe, buffer, sizeof(buffer), &bytesRead, nullptr) &&
           bytesRead > 0) {
        output.append(buffer, buffer + bytesRead);
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &capture.exitCode);
    capture.output = std::move(output);

    CloseHandle(readPipe);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return capture;
}

bool WriteMailboxPulse(std::uint32_t mailboxSlot, const InputFrame& frame,
                       std::uint16_t rawButtons, int durationMs,
                       std::uint32_t& pidOut) {
    GameBridgePC game;
    if (!WaitForAttach(game, kDefaultAttachTimeoutMs, kDefaultPollMs)) {
        return false;
    }

    MailboxWriter writer;
    pidOut = game.ProcessId();
    if (!writer.Create(pidOut)) {
        return false;
    }

    InputFrame pulse = frame;
    pulse.clientTimeMs = NowMs();
    writer.WriteSlot(static_cast<int>(mailboxSlot), pulse, rawButtons);

    SleepMs(durationMs);

    InputFrame release {};
    release.clientTimeMs = NowMs();
    writer.WriteSlot(static_cast<int>(mailboxSlot), release, 0);
    return true;
}

// Rig restart: refuse if a KH2 the rig didn't launch is running (James may be
// playing), kill rig-owned instances, rebuild the DLL, launch and inject.
CommandResult RigRestart(bool noBuild, bool killOnly, const LaunchOptions& options,
                         const char* command) {
    const auto owned = PruneOwned();
    std::vector<DWORD> unowned;
    for (const auto& proc : ListKh2Processes()) {
        if (!IsOwned(proc, owned)) unowned.push_back(proc.pid);
    }
    if (!unowned.empty()) {
        std::ostringstream out;
        out << "{\"ok\":false,\"command\":" << JsonString(command)
            << ",\"phase\":\"preflight\",\"error\":"
            << JsonString("KH2 is running outside the rig (James may be playing); "
                          "refusing to restart")
            << ",\"unowned\":" << JsonDwordArray(unowned) << "}";
        return {1, out.str()};
    }

    std::vector<DWORD> killed, skippedUnowned;
    KillOwned(std::nullopt, killed, skippedUnowned);
    if (killOnly) {
        std::ostringstream out;
        out << "{\"ok\":true,\"command\":" << JsonString(command)
            << ",\"killed\":" << JsonDwordArray(killed) << "}";
        return {0, out.str()};
    }

    if (!noBuild) {
        const auto build = RunProcessCapture(
            L"cmake --build \"" + (RepoRoot() / "build").wstring() +
                L"\" --target kh2coop_inject --config Release",
            RepoRoot());
        if (!build.launched || build.exitCode != 0) {
            std::ostringstream out;
            out << "{\"ok\":false,\"command\":" << JsonString(command)
                << ",\"phase\":\"build\",\"exitCode\":" << build.exitCode
                << ",\"output\":" << JsonString(build.output) << "}";
            return {1, out.str()};
        }
    }

    return LaunchInstance(options, command);
}

bool DriveLoadSaveMenu(GameBridgePC& game, int slot, const KeySpec& confirmSpec,
                       const KeySpec& downSpec, int wakePresses,
                       int wakeDelayMs, int stepDelayMs,
                       int postSelectDelayMs, int finalConfirmPresses,
                       std::string& error) {
    const auto hwnd = FindWindowForPid(game.ProcessId());
    if (!hwnd.has_value()) {
        error = "Could not find KH2 window for load-save";
        return false;
    }
    if (!FocusWindow(*hwnd)) {
        error = "Failed to focus KH2 window for load-save";
        return false;
    }

    SleepMs(100);

    for (int i = 0; i < wakePresses; ++i) {
        if (!SendKeyPress(confirmSpec, 60)) {
            error = "Failed to send wake confirm key";
            return false;
        }
        SleepMs(wakeDelayMs);
    }

    for (int i = 1; i < slot; ++i) {
        if (!SendKeyPress(downSpec, 60)) {
            error = "Failed to send down key while selecting save";
            return false;
        }
        SleepMs(stepDelayMs);
    }

    if (!SendKeyPress(confirmSpec, 60)) {
        error = "Failed to send save select confirm key";
        return false;
    }
    SleepMs(postSelectDelayMs);

    for (int i = 0; i < finalConfirmPresses; ++i) {
        if (!SendKeyPress(confirmSpec, 60)) {
            error = "Failed to send final confirm key";
            return false;
        }
        SleepMs(postSelectDelayMs);
    }

    return true;
}

CommandResult CmdRestart(std::vector<std::string> args) {
    const bool noBuild = ConsumeFlag(args, "--no-build");
    const bool killOnly = ConsumeFlag(args, "--kill");
    const auto options = ConsumeLaunchOptions(args);
    if (!args.empty()) {
        throw std::runtime_error("Unexpected argument for restart: " + args.front());
    }
    return RigRestart(noBuild, killOnly, options, "restart");
}

CommandResult CmdState(std::vector<std::string> args) {
    if (!args.empty()) {
        throw std::runtime_error("state does not accept positional arguments");
    }

    GameBridgePC game;
    game.Attach();
    return BuildStateJson(game);
}

CommandResult CmdWaitTitle(std::vector<std::string> args) {
    const int timeoutMs = ParseNumber<int>(
        ConsumeOption(args, "--timeout-ms").value_or("60000"),
        "--timeout-ms");
    const int pollMs = ParseNumber<int>(
        ConsumeOption(args, "--poll-ms").value_or("250"), "--poll-ms");
    if (!args.empty()) {
        throw std::runtime_error("Unexpected argument for wait-title: " +
                                 args.front());
    }

    return WaitForRoomState(
        "title/loading screen",
        [](const RoomState& room) {
            return room.worldId == 0xFFU && room.roomId == 0xFFU;
        },
        timeoutMs, pollMs);
}

CommandResult CmdWaitInGame(std::vector<std::string> args) {
    const int timeoutMs = ParseNumber<int>(
        ConsumeOption(args, "--timeout-ms").value_or("60000"),
        "--timeout-ms");
    const int pollMs = ParseNumber<int>(
        ConsumeOption(args, "--poll-ms").value_or("250"), "--poll-ms");
    if (!args.empty()) {
        throw std::runtime_error("Unexpected argument for wait-ingame: " +
                                 args.front());
    }

    return WaitForRoomState(
        "in-game room",
        [](const RoomState& room) {
            return room.worldId != 0xFFU && room.roomId != 0xFFU &&
                   !room.inTransition;
        },
        timeoutMs, pollMs);
}

CommandResult CmdWaitRoom(std::vector<std::string> args) {
    const auto worldRaw = ConsumeOption(args, "--world");
    const auto roomRaw = ConsumeOption(args, "--room");
    if (!worldRaw || !roomRaw) {
        throw std::runtime_error("wait-room requires --world and --room");
    }

    const auto world = ParseNumber<std::uint32_t>(*worldRaw, "--world");
    const auto room = ParseNumber<std::uint32_t>(*roomRaw, "--room");
    const int timeoutMs = ParseNumber<int>(
        ConsumeOption(args, "--timeout-ms").value_or("60000"),
        "--timeout-ms");
    const int pollMs = ParseNumber<int>(
        ConsumeOption(args, "--poll-ms").value_or("250"), "--poll-ms");
    if (!args.empty()) {
        throw std::runtime_error("Unexpected argument for wait-room: " +
                                 args.front());
    }

    return WaitForRoomState(
        "target room",
        [world, room](const RoomState& current) {
            return current.worldId == world &&
                   current.roomId == room &&
                   !current.inTransition;
        },
        timeoutMs, pollMs);
}

CommandResult CmdFocus(std::vector<std::string> args) {
    if (!args.empty()) {
        throw std::runtime_error("focus does not accept positional arguments");
    }

    GameBridgePC game;
    if (!WaitForAttach(game, kDefaultAttachTimeoutMs, kDefaultPollMs)) {
        return MakeAttachTimeout("focus");
    }

    const auto hwnd = FindWindowForPid(game.ProcessId());
    if (!hwnd.has_value()) {
        return MakeError("Could not find KH2 window");
    }

    const bool focused = FocusWindow(*hwnd);
    std::ostringstream out;
    out << "{"
        << "\"ok\":" << JsonBool(focused) << ","
        << "\"processId\":" << game.ProcessId() << ","
        << "\"windowTitle\":" << JsonString(WindowTitle(*hwnd))
        << "}";
    return {focused ? 0 : 1, out.str()};
}

CommandResult CmdSendKey(std::vector<std::string> args, bool holdMode) {
    const auto keyName = ConsumeOption(args, "--key");
    if (!keyName) {
        throw std::runtime_error("Missing --key");
    }
    const int durationMs = ParseNumber<int>(
        ConsumeOption(args, "--duration-ms").value_or(holdMode ? "500" : "60"),
        "--duration-ms");
    const bool focusFirst = !ConsumeFlag(args, "--no-focus");
    if (!args.empty()) {
        throw std::runtime_error("Unexpected argument for key command: " +
                                 args.front());
    }

    const auto spec = ParseKeySpec(*keyName);
    if (!spec.has_value()) {
        return MakeError("Unsupported key name: " + *keyName);
    }

    GameBridgePC game;
    if (!WaitForAttach(game, kDefaultAttachTimeoutMs, kDefaultPollMs)) {
        return MakeAttachTimeout("key input");
    }

    if (focusFirst) {
        const auto hwnd = FindWindowForPid(game.ProcessId());
        if (!hwnd.has_value()) {
            return MakeError("Could not find KH2 window for key input");
        }
        if (!FocusWindow(*hwnd)) {
            return MakeError("Failed to focus KH2 window before key input");
        }
        SleepMs(100);
    }

    const bool sent = SendKeyPress(*spec, durationMs);
    std::ostringstream out;
    out << "{"
        << "\"ok\":" << JsonBool(sent) << ","
        << "\"processId\":" << game.ProcessId() << ","
        << "\"key\":" << JsonString(*keyName) << ","
        << "\"durationMs\":" << durationMs
        << "}";
    return {sent ? 0 : 1, out.str()};
}

CommandResult CmdLoadSave(std::vector<std::string> args) {
    const auto slotRaw = ConsumeOption(args, "--slot");
    if (!slotRaw) {
        throw std::runtime_error("load-save requires --slot");
    }

    const int slot = ParseNumber<int>(*slotRaw, "--slot");
    if (slot < 1) {
        throw std::runtime_error("--slot must be >= 1");
    }

    const std::string confirmKey =
        ConsumeOption(args, "--confirm-key").value_or("enter");
    const std::string downKey =
        ConsumeOption(args, "--down-key").value_or("down");
    const int wakePresses = ParseNumber<int>(
        ConsumeOption(args, "--wake-presses").value_or("1"),
        "--wake-presses");
    const int wakeDelayMs = ParseNumber<int>(
        ConsumeOption(args, "--wake-delay-ms").value_or("1000"),
        "--wake-delay-ms");
    const int stepDelayMs = ParseNumber<int>(
        ConsumeOption(args, "--step-delay-ms").value_or("250"),
        "--step-delay-ms");
    const int postSelectDelayMs = ParseNumber<int>(
        ConsumeOption(args, "--post-select-delay-ms").value_or("800"),
        "--post-select-delay-ms");
    const int finalConfirmPresses = ParseNumber<int>(
        ConsumeOption(args, "--final-confirm-presses").value_or("1"),
        "--final-confirm-presses");
    const int loadTimeoutMs = ParseNumber<int>(
        ConsumeOption(args, "--load-timeout-ms").value_or("60000"),
        "--load-timeout-ms");
    if (!args.empty()) {
        throw std::runtime_error("Unexpected argument for load-save: " +
                                 args.front());
    }

    const auto confirmSpec = ParseKeySpec(confirmKey);
    const auto downSpec = ParseKeySpec(downKey);
    if (!confirmSpec.has_value()) {
        return MakeError("Unsupported confirm key: " + confirmKey);
    }
    if (!downSpec.has_value()) {
        return MakeError("Unsupported down key: " + downKey);
    }

    GameBridgePC game;
    if (!WaitForAttach(game, kDefaultAttachTimeoutMs, kDefaultPollMs)) {
        return MakeAttachTimeout("load-save");
    }

    std::string menuError;
    if (!DriveLoadSaveMenu(game, slot, *confirmSpec, *downSpec,
                           wakePresses, wakeDelayMs, stepDelayMs,
                           postSelectDelayMs, finalConfirmPresses,
                           menuError)) {
        return MakeError(menuError);
    }

    auto result = WaitForRoomState(
        "loaded save to enter a room",
        [](const RoomState& room) {
            return room.worldId != 0xFFU && room.roomId != 0xFFU &&
                   !room.inTransition;
        },
        loadTimeoutMs, kDefaultPollMs);

    if (result.exitCode == 0) {
        std::ostringstream out;
        out << "{"
            << "\"ok\":true,"
            << "\"processId\":" << game.ProcessId() << ","
            << "\"slot\":" << slot << ","
            << "\"confirmKey\":" << JsonString(confirmKey) << ","
            << "\"downKey\":" << JsonString(downKey) << ","
            << "\"result\":" << result.json
            << "}";
        return {0, out.str()};
    }

    return result;
}

CommandResult CmdBootLoadSave(std::vector<std::string> args) {
    const auto slotRaw = ConsumeOption(args, "--slot");
    if (!slotRaw) {
        throw std::runtime_error("boot-load-save requires --slot");
    }

    const int slot = ParseNumber<int>(*slotRaw, "--slot");
    if (slot < 1) {
        throw std::runtime_error("--slot must be >= 1");
    }

    const bool noBuild = ConsumeFlag(args, "--no-build");
    const auto launchOptions = ConsumeLaunchOptions(args);
    const std::string confirmKey =
        ConsumeOption(args, "--confirm-key").value_or("enter");
    const std::string downKey =
        ConsumeOption(args, "--down-key").value_or("down");
    const int titleTimeoutMs = ParseNumber<int>(
        ConsumeOption(args, "--title-timeout-ms").value_or("60000"),
        "--title-timeout-ms");
    const int wakePresses = ParseNumber<int>(
        ConsumeOption(args, "--wake-presses").value_or("1"),
        "--wake-presses");
    const int wakeDelayMs = ParseNumber<int>(
        ConsumeOption(args, "--wake-delay-ms").value_or("1000"),
        "--wake-delay-ms");
    const int stepDelayMs = ParseNumber<int>(
        ConsumeOption(args, "--step-delay-ms").value_or("250"),
        "--step-delay-ms");
    const int postSelectDelayMs = ParseNumber<int>(
        ConsumeOption(args, "--post-select-delay-ms").value_or("800"),
        "--post-select-delay-ms");
    const int finalConfirmPresses = ParseNumber<int>(
        ConsumeOption(args, "--final-confirm-presses").value_or("1"),
        "--final-confirm-presses");
    const int loadTimeoutMs = ParseNumber<int>(
        ConsumeOption(args, "--load-timeout-ms").value_or("60000"),
        "--load-timeout-ms");
    if (!args.empty()) {
        throw std::runtime_error("Unexpected argument for boot-load-save: " +
                                 args.front());
    }

    const auto restart = RigRestart(noBuild, false, launchOptions, "boot-load-save");
    if (restart.exitCode != 0) {
        return restart;
    }

    const auto confirmSpec = ParseKeySpec(confirmKey);
    const auto downSpec = ParseKeySpec(downKey);
    if (!confirmSpec.has_value()) {
        return MakeError("Unsupported confirm key: " + confirmKey);
    }
    if (!downSpec.has_value()) {
        return MakeError("Unsupported down key: " + downKey);
    }

    GameBridgePC game;
    if (!WaitForAttach(game, titleTimeoutMs, kDefaultPollMs)) {
        return MakeAttachTimeout("boot-load-save");
    }

    const auto titleWait = WaitForRoomState(
        "title/loading screen",
        [](const RoomState& room) {
            return room.worldId == 0xFFU && room.roomId == 0xFFU;
        },
        titleTimeoutMs, kDefaultPollMs);
    if (titleWait.exitCode != 0) {
        std::ostringstream out;
        out << "{"
            << "\"ok\":false,"
            << "\"phase\":\"wait-title\","
            << "\"launch\":" << restart.json << ","
            << "\"result\":" << titleWait.json
            << "}";
        return {titleWait.exitCode, out.str()};
    }

    if (!game.Attach()) {
        return MakeAttachTimeout("boot-load-save after title wait");
    }

    std::string menuError;
    if (!DriveLoadSaveMenu(game, slot, *confirmSpec, *downSpec,
                           wakePresses, wakeDelayMs, stepDelayMs,
                           postSelectDelayMs, finalConfirmPresses,
                           menuError)) {
        return MakeError(menuError);
    }

    const auto roomWait = WaitForRoomState(
        "loaded save to enter a room",
        [](const RoomState& room) {
            return room.worldId != 0xFFU && room.roomId != 0xFFU &&
                   !room.inTransition;
        },
        loadTimeoutMs, kDefaultPollMs);

    if (roomWait.exitCode != 0) {
        std::ostringstream out;
        out << "{"
            << "\"ok\":false,"
            << "\"phase\":\"wait-room\","
            << "\"launch\":" << restart.json << ","
            << "\"result\":" << roomWait.json
            << "}";
        return {roomWait.exitCode, out.str()};
    }

    std::ostringstream out;
    out << "{"
        << "\"ok\":true,"
        << "\"slot\":" << slot << ","
        << "\"confirmKey\":" << JsonString(confirmKey) << ","
        << "\"downKey\":" << JsonString(downKey) << ","
        << "\"launch\":" << restart.json << ","
        << "\"result\":" << roomWait.json
        << "}";
    return {0, out.str()};
}

CommandResult CmdInput(std::vector<std::string> args) {
    const auto slotRaw = ConsumeOption(args, "--slot");
    if (!slotRaw) {
        throw std::runtime_error("input requires --slot");
    }

    const std::uint32_t slotIndex = ParseFriendMailboxSlot(*slotRaw);
    const int durationMs = ParseNumber<int>(
        ConsumeOption(args, "--duration-ms").value_or("100"),
        "--duration-ms");

    InputFrame frame {};
    frame.leftStickX = ParseNumber<float>(
        ConsumeOption(args, "--lx").value_or("0"), "--lx");
    frame.leftStickY = ParseNumber<float>(
        ConsumeOption(args, "--ly").value_or("0"), "--ly");
    frame.rightStickX = ParseNumber<float>(
        ConsumeOption(args, "--rx").value_or("0"), "--rx");
    frame.rightStickY = ParseNumber<float>(
        ConsumeOption(args, "--ry").value_or("0"), "--ry");

    if (const auto buttons = ConsumeOption(args, "--buttons")) {
        ApplyButtonsCsv(frame.buttons, *buttons);
    }
    if (const auto targetId = ConsumeOption(args, "--target-id")) {
        frame.requestedTargetId = ParseNumber<std::uint32_t>(
            *targetId, "--target-id");
    }
    if (!args.empty()) {
        throw std::runtime_error("Unexpected argument for input: " +
                                 args.front());
    }

    std::uint32_t pid = 0;
    if (!WriteMailboxPulse(slotIndex, frame, 0, durationMs, pid)) {
        return MakeError(
            "Failed to publish mailbox input. KH2 may not be running.");
    }

    std::ostringstream out;
    out << "{"
        << "\"ok\":true,"
        << "\"processId\":" << pid << ","
        << "\"slot\":" << JsonString(*slotRaw) << ","
        << "\"durationMs\":" << durationMs << ","
        << "\"leftStick\":{"
            << "\"x\":" << frame.leftStickX << ","
            << "\"y\":" << frame.leftStickY
        << "},"
        << "\"rightStick\":{"
            << "\"x\":" << frame.rightStickX << ","
            << "\"y\":" << frame.rightStickY
        << "},"
        << "\"targetId\":" << frame.requestedTargetId
        << "}";
    return {0, out.str()};
}

CommandResult CmdMove(std::vector<std::string> args) {
    const auto slotRaw = ConsumeOption(args, "--slot");
    if (!slotRaw) {
        throw std::runtime_error("move requires --slot");
    }

    const std::uint32_t slotIndex = ParseFriendMailboxSlot(*slotRaw);
    const int durationMs = ParseNumber<int>(
        ConsumeOption(args, "--duration-ms").value_or("500"),
        "--duration-ms");
    const float x = ParseNumber<float>(
        ConsumeOption(args, "--x").value_or("0"), "--x");
    const float y = ParseNumber<float>(
        ConsumeOption(args, "--y").value_or("1"), "--y");
    if (!args.empty()) {
        throw std::runtime_error("Unexpected argument for move: " +
                                 args.front());
    }

    InputFrame frame {};
    frame.leftStickX = x;
    frame.leftStickY = y;

    std::uint32_t pid = 0;
    if (!WriteMailboxPulse(slotIndex, frame, 0, durationMs, pid)) {
        return MakeError("Failed to publish mailbox movement");
    }

    std::ostringstream out;
    out << "{"
        << "\"ok\":true,"
        << "\"processId\":" << pid << ","
        << "\"slot\":" << JsonString(*slotRaw) << ","
        << "\"durationMs\":" << durationMs << ","
        << "\"x\":" << x << ","
        << "\"y\":" << y
        << "}";
    return {0, out.str()};
}

CommandResult CmdPress(std::vector<std::string> args) {
    const auto slotRaw = ConsumeOption(args, "--slot");
    const auto buttonRaw = ConsumeOption(args, "--button");
    if (!slotRaw || !buttonRaw) {
        throw std::runtime_error("press requires --slot and --button");
    }

    const std::uint32_t slotIndex = ParseFriendMailboxSlot(*slotRaw);
    const int durationMs = ParseNumber<int>(
        ConsumeOption(args, "--duration-ms").value_or("100"),
        "--duration-ms");
    if (!args.empty()) {
        throw std::runtime_error("Unexpected argument for press: " +
                                 args.front());
    }

    InputFrame frame {};
    ApplyButtonName(frame.buttons, *buttonRaw);

    std::uint32_t pid = 0;
    if (!WriteMailboxPulse(slotIndex, frame, 0, durationMs, pid)) {
        return MakeError("Failed to publish mailbox button press");
    }

    std::ostringstream out;
    out << "{"
        << "\"ok\":true,"
        << "\"processId\":" << pid << ","
        << "\"slot\":" << JsonString(*slotRaw) << ","
        << "\"button\":" << JsonString(*buttonRaw) << ","
        << "\"durationMs\":" << durationMs
        << "}";
    return {0, out.str()};
}

CommandResult CmdPlayerInput(std::vector<std::string> args) {
    const int durationMs = ParseNumber<int>(
        ConsumeOption(args, "--duration-ms").value_or("100"),
        "--duration-ms");

    InputFrame frame {};
    frame.leftStickX = ParseNumber<float>(
        ConsumeOption(args, "--lx").value_or("0"), "--lx");
    frame.leftStickY = ParseNumber<float>(
        ConsumeOption(args, "--ly").value_or("0"), "--ly");
    frame.rightStickX = ParseNumber<float>(
        ConsumeOption(args, "--rx").value_or("0"), "--rx");
    frame.rightStickY = ParseNumber<float>(
        ConsumeOption(args, "--ry").value_or("0"), "--ry");

    std::uint16_t rawButtons = 0;
    if (const auto buttons = ConsumeOption(args, "--buttons")) {
        ApplyRawButtonsCsv(rawButtons, *buttons);
    }
    if (!args.empty()) {
        throw std::runtime_error("Unexpected argument for player-input: " +
                                 args.front());
    }

    std::uint32_t pid = 0;
    if (!WriteMailboxPulse(kh2coop::MAILBOX_SLOT_PLAYER, frame, rawButtons,
                           durationMs, pid)) {
        return MakeError("Failed to publish player mailbox input");
    }

    std::ostringstream out;
    out << "{"
        << "\"ok\":true,"
        << "\"processId\":" << pid << ","
        << "\"slot\":\"player\","
        << "\"durationMs\":" << durationMs << ","
        << "\"rawButtons\":" << rawButtons << ","
        << "\"leftStick\":{"
            << "\"x\":" << frame.leftStickX << ","
            << "\"y\":" << frame.leftStickY
        << "},"
        << "\"rightStick\":{"
            << "\"x\":" << frame.rightStickX << ","
            << "\"y\":" << frame.rightStickY
        << "}"
        << "}";
    return {0, out.str()};
}

CommandResult CmdPlayerMove(std::vector<std::string> args) {
    const int durationMs = ParseNumber<int>(
        ConsumeOption(args, "--duration-ms").value_or("500"),
        "--duration-ms");
    const float x = ParseNumber<float>(
        ConsumeOption(args, "--x").value_or("0"), "--x");
    const float y = ParseNumber<float>(
        ConsumeOption(args, "--y").value_or("1"), "--y");
    if (!args.empty()) {
        throw std::runtime_error("Unexpected argument for player-move: " +
                                 args.front());
    }

    InputFrame frame {};
    frame.leftStickX = x;
    frame.leftStickY = y;

    std::uint32_t pid = 0;
    if (!WriteMailboxPulse(kh2coop::MAILBOX_SLOT_PLAYER, frame, 0,
                           durationMs, pid)) {
        return MakeError("Failed to publish player mailbox movement");
    }

    std::ostringstream out;
    out << "{"
        << "\"ok\":true,"
        << "\"processId\":" << pid << ","
        << "\"slot\":\"player\","
        << "\"durationMs\":" << durationMs << ","
        << "\"x\":" << x << ","
        << "\"y\":" << y
        << "}";
    return {0, out.str()};
}

CommandResult CmdPlayerPress(std::vector<std::string> args) {
    const auto buttonRaw = ConsumeOption(args, "--button");
    if (!buttonRaw) {
        throw std::runtime_error("player-press requires --button");
    }

    const int durationMs = ParseNumber<int>(
        ConsumeOption(args, "--duration-ms").value_or("100"),
        "--duration-ms");
    if (!args.empty()) {
        throw std::runtime_error("Unexpected argument for player-press: " +
                                 args.front());
    }

    std::uint16_t rawButtons = 0;
    ApplyRawButtonName(rawButtons, *buttonRaw);

    std::uint32_t pid = 0;
    if (!WriteMailboxPulse(kh2coop::MAILBOX_SLOT_PLAYER, InputFrame {},
                           rawButtons, durationMs, pid)) {
        return MakeError("Failed to publish player mailbox button press");
    }

    std::ostringstream out;
    out << "{"
        << "\"ok\":true,"
        << "\"processId\":" << pid << ","
        << "\"slot\":\"player\","
        << "\"button\":" << JsonString(*buttonRaw) << ","
        << "\"rawButtons\":" << rawButtons << ","
        << "\"durationMs\":" << durationMs
        << "}";
    return {0, out.str()};
}

void PrintUsage() {
    std::cout
        << "kh2ctl commands:\n"
        << "  launch [LAUNCH_OPTS]      launch KH2 and inject the current DLL build\n"
        << "  inject --pid N [--dll PATH] [--init-timeout-ms N]\n"
        << "  instances                 list KH2 processes and whether the rig owns them\n"
        << "  kill (--pid N | --all)    kill rig-launched KH2 processes only\n"
        << "  restart [--no-build] [--kill] [LAUNCH_OPTS]\n"
        << "      LAUNCH_OPTS: [--game-dir DIR] [--dll PATH] [--no-inject]\n"
        << "                   [--window-timeout-ms N] [--settle-ms N]\n"
        << "                   [--init-timeout-ms N]\n"
        << "  state\n"
        << "  wait-title [--timeout-ms N] [--poll-ms N]\n"
        << "  wait-ingame [--timeout-ms N] [--poll-ms N]\n"
        << "  wait-room --world N --room N [--timeout-ms N] [--poll-ms N]\n"
        << "  focus\n"
        << "  tap-key --key KEY [--duration-ms N] [--no-focus]\n"
        << "  hold-key --key KEY [--duration-ms N] [--no-focus]\n"
        << "  load-save --slot N [--confirm-key KEY] [--down-key KEY]\n"
        << "            [--wake-presses N] [--wake-delay-ms N]\n"
        << "            [--step-delay-ms N] [--post-select-delay-ms N]\n"
        << "            [--final-confirm-presses N] [--load-timeout-ms N]\n"
        << "  boot-load-save --slot N [--no-build] [LAUNCH_OPTS]\n"
        << "                 [--confirm-key KEY] [--down-key KEY]\n"
        << "                 [--title-timeout-ms N] [--wake-presses N]\n"
        << "                 [--wake-delay-ms N] [--step-delay-ms N]\n"
        << "                 [--post-select-delay-ms N]\n"
        << "                 [--final-confirm-presses N] [--load-timeout-ms N]\n"
        << "  player-input [--lx X] [--ly Y] [--rx X] [--ry Y]\n"
        << "               [--buttons cross,circle,...] [--duration-ms N]\n"
        << "  player-move [--x X] [--y Y] [--duration-ms N]\n"
        << "  player-press --button NAME [--duration-ms N]\n"
        << "  input --slot friend1|friend2 [--lx X] [--ly Y] [--rx X] [--ry Y]\n"
        << "        [--buttons attack,jump,...] [--target-id N] [--duration-ms N]\n"
        << "  move --slot friend1|friend2 [--x X] [--y Y] [--duration-ms N]\n"
        << "  press --slot friend1|friend2 --button NAME [--duration-ms N]\n"
        << "\n"
        << "All successful commands print a single JSON object to stdout.\n";
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        if (argc < 2) {
            PrintUsage();
            return 0;
        }

        const std::string command = ToLower(argv[1]);
        std::vector<std::string> args(argv + 2, argv + argc);

        CommandResult result;
        if (command == "help" || command == "--help" || command == "-h") {
            PrintUsage();
            return 0;
        } else if (command == "launch") {
            result = CmdLaunch(std::move(args));
        } else if (command == "inject") {
            result = CmdInject(std::move(args));
        } else if (command == "instances") {
            result = CmdInstances(std::move(args));
        } else if (command == "kill") {
            result = CmdKill(std::move(args));
        } else if (command == "restart") {
            result = CmdRestart(std::move(args));
        } else if (command == "state") {
            result = CmdState(std::move(args));
        } else if (command == "wait-title") {
            result = CmdWaitTitle(std::move(args));
        } else if (command == "wait-ingame") {
            result = CmdWaitInGame(std::move(args));
        } else if (command == "wait-room") {
            result = CmdWaitRoom(std::move(args));
        } else if (command == "focus") {
            result = CmdFocus(std::move(args));
        } else if (command == "tap-key") {
            result = CmdSendKey(std::move(args), false);
        } else if (command == "hold-key") {
            result = CmdSendKey(std::move(args), true);
        } else if (command == "load-save") {
            result = CmdLoadSave(std::move(args));
        } else if (command == "boot-load-save") {
            result = CmdBootLoadSave(std::move(args));
        } else if (command == "player-input") {
            result = CmdPlayerInput(std::move(args));
        } else if (command == "player-move") {
            result = CmdPlayerMove(std::move(args));
        } else if (command == "player-press") {
            result = CmdPlayerPress(std::move(args));
        } else if (command == "input") {
            result = CmdInput(std::move(args));
        } else if (command == "move") {
            result = CmdMove(std::move(args));
        } else if (command == "press") {
            result = CmdPress(std::move(args));
        } else {
            result = MakeError("Unknown command: " + command);
        }

        std::cout << result.json << "\n";
        return result.exitCode;
    } catch (const std::exception& ex) {
        std::cout << "{\"ok\":false,\"error\":"
                  << JsonString(ex.what()) << "}\n";
        return 1;
    }
}
