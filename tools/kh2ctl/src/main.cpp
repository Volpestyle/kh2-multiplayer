#include "kh2coop/CaptureChannel.hpp"
#include "kh2coop/WarpChannel.hpp"
#include "kh2coop/GameBridgePC.hpp"
#include "kh2coop/InputMailbox.hpp"
#include "kh2coop/Types.hpp"

#include <Windows.h>
#include <TlHelp32.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>

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
    // Integers accept a 0x prefix (room ids are usually written in hex).
    const bool hex = std::is_integral_v<T> && raw.size() > 2 && raw[0] == '0' &&
                     (raw[1] == 'x' || raw[1] == 'X');
    std::istringstream input(hex ? raw.substr(2) : raw);
    if (hex) input >> std::hex;
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

// Instance selected with the global --pid option; empty means "the only KH2
// running" (main refuses to guess when several are running).
std::optional<std::uint32_t> g_targetPid;

bool AttachGame(GameBridgePC& game) {
    return g_targetPid ? game.Attach(*g_targetPid) : game.Attach();
}

bool WaitForAttach(GameBridgePC& game, int timeoutMs, int pollMs) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

    do {
        if (AttachGame(game)) {
            return true;
        }
        SleepMs(pollMs);
    } while (std::chrono::steady_clock::now() < deadline);

    return AttachGame(game);
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
        << "},"
        << "\"enemies\":[";
    // Actors whose objentry name starts with B_ or M_ (GameBridgePC).
    const auto enemies = game.ReadEnemyStates();
    for (std::size_t i = 0; i < enemies.size(); ++i) {
        const auto& e = enemies[i];
        out << (i ? "," : "") << "{\"objectId\":" << e.objectId
            << ",\"motionId\":" << e.motionId << ",\"position\":{\"x\":" << e.position.x
            << ",\"y\":" << e.position.y << ",\"z\":" << e.position.z << "}}";
    }
    out << "]}";
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

// Mutes or unmutes every audio session the process owns on the default output
// device. Returns the number of sessions changed (0 if it hasn't opened one).
int SetProcessMute(DWORD pid, bool mute, std::string& error) {
    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioSessionManager2* manager = nullptr;
    IAudioSessionEnumerator* sessions = nullptr;
    int changed = 0;

    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator),
                                reinterpret_cast<void**>(&enumerator))) ||
        FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device)) ||
        FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                                reinterpret_cast<void**>(&manager))) ||
        FAILED(manager->GetSessionEnumerator(&sessions))) {
        error = "Could not enumerate audio sessions on the default output device";
    } else {
        int count = 0;
        sessions->GetCount(&count);
        for (int i = 0; i < count; ++i) {
            IAudioSessionControl* control = nullptr;
            IAudioSessionControl2* control2 = nullptr;
            ISimpleAudioVolume* volume = nullptr;
            DWORD sessionPid = 0;
            if (SUCCEEDED(sessions->GetSession(i, &control)) &&
                SUCCEEDED(control->QueryInterface(__uuidof(IAudioSessionControl2),
                                                  reinterpret_cast<void**>(&control2))) &&
                SUCCEEDED(control2->GetProcessId(&sessionPid)) && sessionPid == pid &&
                SUCCEEDED(control->QueryInterface(__uuidof(ISimpleAudioVolume),
                                                  reinterpret_cast<void**>(&volume))) &&
                SUCCEEDED(volume->SetMute(mute ? TRUE : FALSE, nullptr))) {
                ++changed;
            }
            if (volume) volume->Release();
            if (control2) control2->Release();
            if (control) control->Release();
        }
    }

    if (sessions) sessions->Release();
    if (manager) manager->Release();
    if (device) device->Release();
    if (enumerator) enumerator->Release();
    if (SUCCEEDED(init)) CoUninitialize();
    return changed;
}

CommandResult CmdMute(std::vector<std::string> args) {
    const auto pidRaw = ConsumeOption(args, "--pid");
    const bool off = ConsumeFlag(args, "--off");
    if (!pidRaw) throw std::runtime_error("mute requires --pid");
    if (!args.empty()) {
        throw std::runtime_error("Unexpected argument for mute: " + args.front());
    }
    const DWORD pid = ParseNumber<DWORD>(*pidRaw, "--pid");
    std::string error;
    const int sessions = SetProcessMute(pid, !off, error);
    if (!error.empty()) return MakeError(error);
    if (sessions == 0) {
        return MakeError("No audio session for PID " + std::to_string(pid) +
                         " (the game opens one once it starts playing audio)");
    }
    std::ostringstream out;
    out << "{\"ok\":true,\"processId\":" << pid << ",\"muted\":" << JsonBool(!off)
        << ",\"sessions\":" << sessions << "}";
    return {0, out.str()};
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

bool IsExtendedKey(WORD vk) {
    switch (vk) {
    case VK_UP: case VK_DOWN: case VK_LEFT: case VK_RIGHT:
    case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
    case VK_INSERT: case VK_DELETE:
        return true;
    default:
        return false;
    }
}

// KH2 reads scan codes, so a VK-only event (scan code 0) makes every key look
// the same to the game. Send the real scan code, flagged extended where needed.
bool SendVk(WORD vk, DWORD flags) {
    INPUT input {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    input.ki.dwFlags = flags | (IsExtendedKey(vk) ? KEYEVENTF_EXTENDEDKEY : 0);
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

// Raw slot buttons use the PS2 DualShock 2 bit order (the game's mapping
// table at 0x5C3420 maps 0xF09 = Select+Start+L1+R1+L2+R2 to soft reset).
constexpr std::uint16_t kRawButtonBack = 0x0001;       // Select
constexpr std::uint16_t kRawButtonL3 = 0x0002;
constexpr std::uint16_t kRawButtonR3 = 0x0004;
constexpr std::uint16_t kRawButtonStart = 0x0008;
constexpr std::uint16_t kRawButtonDpadUp = 0x0010;
constexpr std::uint16_t kRawButtonDpadRight = 0x0020;
constexpr std::uint16_t kRawButtonDpadDown = 0x0040;
constexpr std::uint16_t kRawButtonDpadLeft = 0x0080;
constexpr std::uint16_t kRawButtonL2 = 0x0100;
constexpr std::uint16_t kRawButtonR2 = 0x0200;
constexpr std::uint16_t kRawButtonL1 = 0x0400;
constexpr std::uint16_t kRawButtonR1 = 0x0800;
constexpr std::uint16_t kRawButtonTriangle = 0x1000;
constexpr std::uint16_t kRawButtonCircle = 0x2000;
constexpr std::uint16_t kRawButtonCross = 0x4000;
constexpr std::uint16_t kRawButtonSquare = 0x8000;

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
    if (name == "l2" || name == "lt") {
        buttons |= kRawButtonL2;
        return;
    }
    if (name == "r2" || name == "rt") {
        buttons |= kRawButtonR2;
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
    AttachGame(game);
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

    if (!AttachGame(game)) {
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

// ============================================================================
// In-renderer capture (VUH-1485): talks to the DLL's Present hook through
// CaptureChannel. Works for occluded and unfocused windows.
// ============================================================================

class CaptureChannelView {
public:
    explicit CaptureChannelView(DWORD pid) {
        const std::wstring name = kh2coop::CAPTURE_NAME_PREFIX + std::to_wstring(pid);
        mapping_ = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
        if (!mapping_) return;
        view_ = static_cast<kh2coop::CaptureChannel*>(MapViewOfFile(
            mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(kh2coop::CaptureChannel)));
        if (view_ && (view_->magic != kh2coop::CAPTURE_MAGIC ||
                      view_->version != kh2coop::CAPTURE_VERSION)) {
            UnmapViewOfFile(view_);
            view_ = nullptr;
        }
    }
    ~CaptureChannelView() {
        if (view_) UnmapViewOfFile(view_);
        if (mapping_) CloseHandle(mapping_);
    }
    CaptureChannelView(const CaptureChannelView&) = delete;
    CaptureChannelView& operator=(const CaptureChannelView&) = delete;

    kh2coop::CaptureChannel* get() const { return view_; }

private:
    HANDLE mapping_ {nullptr};
    kh2coop::CaptureChannel* view_ {nullptr};
};

std::string NarrowPath(const std::filesystem::path& path) { return path.string(); }

// Present rate over `windowMs`, from the DLL's present counter.
double MeasurePresentFps(kh2coop::CaptureChannel* channel, int windowMs) {
    const long start = channel->presentCount;
    const auto t0 = std::chrono::steady_clock::now();
    SleepMs(windowMs);
    const long end = channel->presentCount;
    const double seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - t0).count();
    return seconds > 0 ? (end - start) / seconds : 0.0;
}

// Fills and submits a request, then waits for the DLL to finish it.
// Returns an error string, or empty on success.
std::string RunCaptureRequest(kh2coop::CaptureChannel* channel,
                              const std::wstring& output, std::uint32_t frames,
                              std::uint32_t interval, int timeoutMs) {
    if (channel->requestSeq != channel->doneSeq) {
        return "A capture is already running on this instance";
    }
    wcsncpy_s(channel->output, output.c_str(), _TRUNCATE);
    channel->frameCount = frames;
    channel->frameInterval = interval;
    const long seq = InterlockedIncrement(&channel->requestSeq);

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (channel->doneSeq != seq) {
        if (std::chrono::steady_clock::now() > deadline) {
            return "Timed out waiting for the capture (is the game rendering?)";
        }
        SleepMs(20);
    }
    if (channel->status != static_cast<std::int32_t>(kh2coop::CaptureStatus::Ok)) {
        std::wstring message(channel->error);
        return "Capture failed (status " + std::to_string(channel->status) + "): " +
               std::filesystem::path(message).string();
    }
    return {};
}

CommandResult OpenChannelError(DWORD pid) {
    return MakeError("No capture channel for PID " + std::to_string(pid) +
                     " (inject a DLL build with the Present hook)");
}

DWORD ResolveTargetPid() {
    if (g_targetPid) return *g_targetPid;
    const auto procs = ListKh2Processes();
    if (procs.empty()) throw std::runtime_error("KH2 is not running");
    return procs.front().pid;
}

CommandResult CmdCapture(std::vector<std::string> args) {
    const auto outRaw = ConsumeOption(args, "--out");
    if (!args.empty()) {
        throw std::runtime_error("Unexpected argument for capture: " + args.front());
    }
    const DWORD pid = ResolveTargetPid();
    CaptureChannelView channel(pid);
    if (!channel.get()) return OpenChannelError(pid);

    std::filesystem::path out = outRaw
        ? std::filesystem::absolute(*outRaw)
        : RigDir() / "shots" / (std::to_string(pid) + "_" + std::to_string(NowMs()) + ".png");
    std::filesystem::create_directories(out.parent_path());

    const std::string error = RunCaptureRequest(channel.get(), out.wstring(), 1, 1, 10000);
    if (!error.empty()) return MakeError(error);

    std::ostringstream json;
    json << "{\"ok\":true,\"processId\":" << pid
         << ",\"path\":" << JsonString(NarrowPath(out))
         << ",\"width\":" << channel.get()->width
         << ",\"height\":" << channel.get()->height << "}";
    return {0, json.str()};
}

CommandResult CmdClip(std::vector<std::string> args) {
    const auto outRaw = ConsumeOption(args, "--out");
    const double seconds =
        ParseNumber<double>(ConsumeOption(args, "--seconds").value_or("3"), "--seconds");
    const int fps = ParseNumber<int>(ConsumeOption(args, "--fps").value_or("30"), "--fps");
    const bool keepFrames = ConsumeFlag(args, "--keep-frames");
    if (!args.empty()) {
        throw std::runtime_error("Unexpected argument for clip: " + args.front());
    }
    if (seconds <= 0 || seconds > 30) throw std::runtime_error("--seconds must be in (0, 30]");
    if (fps < 1 || fps > 60) throw std::runtime_error("--fps must be in [1, 60]");

    const DWORD pid = ResolveTargetPid();
    CaptureChannelView channel(pid);
    if (!channel.get()) return OpenChannelError(pid);

    const double gameFps = MeasurePresentFps(channel.get(), 500);
    if (gameFps < 1.0) return MakeError("The game isn't presenting frames");
    const std::uint32_t interval =
        static_cast<std::uint32_t>((std::max)(1.0, std::round(gameFps / fps)));
    const std::uint32_t frames = static_cast<std::uint32_t>(std::ceil(seconds * fps));

    const std::string stem = std::to_string(pid) + "_" + std::to_string(NowMs());
    const auto frameDir = RigDir() / "clips" / stem;
    std::filesystem::create_directories(frameDir);
    std::filesystem::path out = outRaw ? std::filesystem::absolute(*outRaw)
                                       : RigDir() / "clips" / (stem + ".mp4");
    std::filesystem::create_directories(out.parent_path());

    const long presentsBefore = channel.get()->presentCount;
    const auto t0 = std::chrono::steady_clock::now();
    const std::string error = RunCaptureRequest(
        channel.get(), (frameDir / L"f_%05u.bmp").wstring(), frames, interval,
        static_cast<int>(seconds * 1000) + 30000);
    const double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const double fpsDuring = (channel.get()->presentCount - presentsBefore) / elapsed;
    if (!error.empty()) return MakeError(error);

    // Frames are captured every `interval` presents, so the clip plays back
    // at the game's present rate divided by the interval.
    const double clipFps = gameFps / interval;
    std::wostringstream command;
    command << L"ffmpeg -y -loglevel error -framerate " << clipFps << L" -i \""
            << (frameDir / L"f_%05d.bmp").wstring()
            << L"\" -c:v libx264 -pix_fmt yuv420p -crf 20 \"" << out.wstring() << L"\"";
    const auto encode = RunProcessCapture(command.str(), RepoRoot());
    if (!encode.launched || encode.exitCode != 0) {
        return MakeError("ffmpeg failed (is it on PATH?): " + encode.output);
    }
    if (!keepFrames) std::filesystem::remove_all(frameDir);

    std::ostringstream json;
    json << "{\"ok\":true,\"processId\":" << pid
         << ",\"path\":" << JsonString(NarrowPath(out))
         << ",\"frames\":" << channel.get()->framesWritten
         << ",\"width\":" << channel.get()->width
         << ",\"height\":" << channel.get()->height
         << ",\"clipFps\":" << clipFps
         << ",\"gameFpsBefore\":" << gameFps
         << ",\"gameFpsDuringCapture\":" << fpsDuring << "}";
    return {0, json.str()};
}

CommandResult CmdOverlay(std::vector<std::string> args) {
    if (args.size() != 1 || (args[0] != "on" && args[0] != "off")) {
        throw std::runtime_error("overlay takes on|off");
    }
    const DWORD pid = ResolveTargetPid();
    CaptureChannelView channel(pid);
    if (!channel.get()) return OpenChannelError(pid);
    InterlockedExchange(&channel.get()->overlay, args[0] == "on" ? 1 : 0);
    std::ostringstream json;
    json << "{\"ok\":true,\"processId\":" << pid
         << ",\"overlay\":" << JsonBool(args[0] == "on") << "}";
    return {0, json.str()};
}

// ============================================================================
// Warp (VUH-1486): hand a room target to the DLL, which passes it to the
// game's own transition request on the game thread.
// ============================================================================

std::uint32_t ParseProgram(const std::optional<std::string>& raw, const char* name) {
    if (!raw) return kh2coop::WARP_DEFAULT_PROGRAM;
    return ParseNumber<std::uint32_t>(*raw, name);
}

CommandResult CmdWarp(std::vector<std::string> args) {
    const auto worldRaw = ConsumeOption(args, "--world");
    const auto roomRaw = ConsumeOption(args, "--room");
    const std::uint32_t door =
        ParseNumber<std::uint32_t>(ConsumeOption(args, "--door").value_or("0"), "--door");
    const std::uint32_t map = ParseProgram(ConsumeOption(args, "--map"), "--map");
    const std::uint32_t battle = ParseProgram(ConsumeOption(args, "--btl"), "--btl");
    const std::uint32_t event = ParseProgram(ConsumeOption(args, "--evt"), "--evt");
    const int timeoutMs = ParseNumber<int>(
        ConsumeOption(args, "--timeout-ms").value_or("30000"), "--timeout-ms");
    if (!worldRaw || !roomRaw) throw std::runtime_error("warp requires --world and --room");
    if (!args.empty()) {
        throw std::runtime_error("Unexpected argument for warp: " + args.front());
    }
    const std::uint32_t world = ParseNumber<std::uint32_t>(*worldRaw, "--world");
    const std::uint32_t room = ParseNumber<std::uint32_t>(*roomRaw, "--room");

    const DWORD pid = ResolveTargetPid();
    const std::wstring name = kh2coop::WARP_NAME_PREFIX + std::to_wstring(pid);
    HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
    if (!mapping) {
        return MakeError("No warp channel for PID " + std::to_string(pid) +
                         " (inject a DLL build with warp support)");
    }
    auto* channel = static_cast<kh2coop::WarpChannel*>(
        MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(kh2coop::WarpChannel)));
    if (!channel || channel->magic != kh2coop::WARP_MAGIC ||
        channel->version != kh2coop::WARP_VERSION) {
        if (channel) UnmapViewOfFile(channel);
        CloseHandle(mapping);
        return MakeError("Warp channel has an unexpected layout");
    }
    struct Cleanup {
        HANDLE mapping;
        void* view;
        ~Cleanup() { UnmapViewOfFile(view); CloseHandle(mapping); }
    } cleanup {mapping, channel};

    if (channel->requestSeq != channel->doneSeq) {
        return MakeError("A warp is already pending on this instance");
    }
    channel->world = world;
    channel->room = room;
    channel->door = door;
    channel->map = map;
    channel->battle = battle;
    channel->event = event;
    channel->fadeFlags = 1;
    const auto t0 = std::chrono::steady_clock::now();
    const long seq = InterlockedIncrement(&channel->requestSeq);

    const auto deadline = t0 + std::chrono::milliseconds(timeoutMs);
    while (channel->doneSeq != seq) {
        if (std::chrono::steady_clock::now() > deadline) {
            // Cancel so a held request can't fire later, then say why.
            InterlockedCompareExchange(&channel->doneSeq, seq, seq - 1);
            std::ostringstream out;
            out << "{\"ok\":false,\"processId\":" << pid
                << ",\"error\":\"Warp held by the safe-state gate (or no room running); cancelled\""
                << ",\"gate\":{\"frozen\":" << channel->controllable
                << ",\"inField\":" << channel->inField << ",\"openMenu\":" << channel->openMenu
                << ",\"cutsceneTimer\":" << channel->cutsceneTimer
                << ",\"waitedFrames\":" << channel->gateWaitFrames << "}}";
            return {1, out.str()};
        }
        SleepMs(10);
    }
    const auto status = static_cast<kh2coop::WarpStatus>(channel->status);
    if (status != kh2coop::WarpStatus::Ok) {
        return MakeError("Warp refused by the DLL, status " +
                         std::to_string(channel->status));
    }

    // World/room in NOW change as soon as the request is staged, so they
    // don't show the load finished. The DLL's frame counter only advances
    // while room entities update: it stalls during the load and resumes in
    // the new room. Arrived = target room + a stall + 30 gameplay frames
    // since frames resumed.
    constexpr int kStallMs = 150;
    constexpr long kSettleFrames = 30;
    GameBridgePC game;
    if (!AttachGame(game)) return MakeError("Could not attach to PID " + std::to_string(pid));
    const long long actorBefore = channel->liveActor;
    long lastFrame = channel->liveFrame;
    auto lastChange = std::chrono::steady_clock::now();
    bool sawStall = false;
    long resumeFrame = 0;
    double longestStallMs = 0;
    double resumeAtMs = -1;
    RoomState reached {};
    bool arrived = false;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto now = std::chrono::steady_clock::now();
        const long frame = channel->liveFrame;
        if (frame != lastFrame) {
            const double gapMs =
                std::chrono::duration<double, std::milli>(now - lastChange).count();
            if (gapMs >= kStallMs) {
                sawStall = true;
                resumeFrame = frame;
                resumeAtMs = std::chrono::duration<double, std::milli>(now - t0).count();
            }
            longestStallMs = (std::max)(longestStallMs, gapMs);
            lastFrame = frame;
            lastChange = now;
        }
        reached = game.ReadRoomState();
        if (sawStall && reached.worldId == world && reached.roomId == room &&
            !reached.inTransition && frame - resumeFrame >= kSettleFrames) {
            arrived = true;
            break;
        }
        SleepMs(10);
    }
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const long long actorAfter = channel->liveActor;

    std::ostringstream out;
    out << "{\"ok\":" << JsonBool(arrived) << ",\"processId\":" << pid
        << ",\"target\":{\"world\":" << world << ",\"room\":" << room << ",\"door\":" << door
        << ",\"map\":" << map << ",\"btl\":" << battle << ",\"evt\":" << event << "}"
        << ",\"from\":{\"world\":" << channel->fromWorld << ",\"room\":" << channel->fromRoom
        << "},\"gateAtHandOver\":{\"frozen\":" << channel->controllable
        << ",\"inField\":" << channel->inField
        << ",\"cutsceneTimer\":" << channel->cutsceneTimer
        << ",\"openMenu\":" << channel->openMenu
        << ",\"pauseBlockers\":" << channel->pauseStatus << "}"
        << ",\"seconds\":" << seconds << ",\"longestStallMs\":" << longestStallMs
        << ",\"resumedAtMs\":" << resumeAtMs
        << ",\"actorChanged\":" << JsonBool(actorAfter != actorBefore)
        << ",\"room\":" << RoomStateToJson(reached);
    if (!arrived) out << ",\"error\":\"Timed out before the target room loaded\"";
    out << "}";
    return {arrived ? 0 : 1, out.str()};
}

// ============================================================================
// peek: sample exe-relative memory over time (RE aid).
//   kh2ctl peek --rva 0x9BA928:u64,0x8EC540:u32 [--samples N] [--interval-ms N]
// ============================================================================

std::uint64_t ModuleBase(HANDLE process) {
    HMODULE module = nullptr;
    DWORD needed = 0;
    using EnumModulesFn = BOOL(WINAPI*)(HANDLE, HMODULE*, DWORD, LPDWORD);
    static const auto enumModules = reinterpret_cast<EnumModulesFn>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "K32EnumProcessModules"));
    if (!enumModules || !enumModules(process, &module, sizeof(module), &needed)) return 0;
    return reinterpret_cast<std::uint64_t>(module);
}

CommandResult CmdPeek(std::vector<std::string> args) {
    const auto spec = ConsumeOption(args, "--rva");
    const int samples =
        ParseNumber<int>(ConsumeOption(args, "--samples").value_or("1"), "--samples");
    const int intervalMs =
        ParseNumber<int>(ConsumeOption(args, "--interval-ms").value_or("50"), "--interval-ms");
    if (!spec) throw std::runtime_error("peek requires --rva RVA[:type][,RVA[:type]...]");
    if (!args.empty()) throw std::runtime_error("Unexpected argument for peek: " + args.front());

    struct Field { std::uint64_t rva; std::string type; std::size_t size; };
    std::vector<Field> fields;
    std::stringstream list(*spec);
    for (std::string item; std::getline(list, item, ',');) {
        const auto colon = item.find(':');
        Field field;
        field.rva = std::stoull(item.substr(0, colon), nullptr, 0);
        field.type = colon == std::string::npos ? "u32" : item.substr(colon + 1);
        if (field.type == "u8") field.size = 1;
        else if (field.type == "u16" || field.type == "i16") field.size = 2;
        else if (field.type == "u32" || field.type == "i32" || field.type == "f32") field.size = 4;
        else if (field.type == "u64") field.size = 8;
        else throw std::runtime_error("Unknown peek type: " + field.type);
        fields.push_back(field);
    }

    const DWORD pid = ResolveTargetPid();
    HANDLE process = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!process) return MakeError("OpenProcess failed for PID " + std::to_string(pid));
    const std::uint64_t base = ModuleBase(process);

    std::ostringstream out;
    out << "{\"ok\":true,\"processId\":" << pid << ",\"samples\":[";
    const auto t0 = std::chrono::steady_clock::now();
    for (int s = 0; s < samples; ++s) {
        if (s) SleepMs(intervalMs);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        out << (s ? "," : "") << "{\"t\":" << ms;
        for (const auto& field : fields) {
            std::uint64_t raw = 0;
            ReadProcessMemory(process, reinterpret_cast<LPCVOID>(base + field.rva), &raw,
                              field.size, nullptr);
            std::ostringstream key;
            key << "0x" << std::hex << std::uppercase << field.rva;
            out << ",\"" << key.str() << "\":";
            if (field.type == "i16") out << static_cast<std::int16_t>(raw);
            else if (field.type == "i32") out << static_cast<std::int32_t>(raw);
            else if (field.type == "f32") {
                float f;
                std::memcpy(&f, &raw, 4);
                out << f;
            } else if (field.type == "u64") {
                std::ostringstream hex;
                hex << "\"0x" << std::hex << std::uppercase << raw << "\"";
                out << hex.str();
            } else out << raw;
        }
        out << "}";
    }
    out << "]}";
    CloseHandle(process);
    return {0, out.str()};
}

CommandResult CmdFps(std::vector<std::string> args) {
    const int windowMs =
        ParseNumber<int>(ConsumeOption(args, "--window-ms").value_or("2000"), "--window-ms");
    if (!args.empty()) {
        throw std::runtime_error("Unexpected argument for fps: " + args.front());
    }
    const DWORD pid = ResolveTargetPid();
    CaptureChannelView channel(pid);
    if (!channel.get()) return OpenChannelError(pid);
    const double fps = MeasurePresentFps(channel.get(), windowMs);
    std::ostringstream json;
    json << "{\"ok\":true,\"processId\":" << pid << ",\"fps\":" << fps
         << ",\"renderer\":" << channel.get()->renderer
         << ",\"backbufferFormat\":" << channel.get()->backbufferFormat << "}";
    return {0, json.str()};
}

void PrintUsage() {
    std::cout
        << "kh2ctl commands:\n"
        << "  (game commands take --pid N to pick an instance; required when\n"
        << "   several KH2 instances are running)\n"
        << "  launch [LAUNCH_OPTS]      launch KH2 and inject the current DLL build\n"
        << "  inject --pid N [--dll PATH] [--init-timeout-ms N]\n"
        << "  instances                 list KH2 processes and whether the rig owns them\n"
        << "  kill (--pid N | --all)    kill rig-launched KH2 processes only\n"
        << "  mute --pid N [--off]      mute (or unmute) one instance's audio\n"
        << "  capture [--out x.png]     PNG from inside the renderer (works occluded)\n"
        << "  clip [--seconds S] [--fps F] [--out x.mp4] [--keep-frames]\n"
        << "  overlay on|off            debug overlay: pid, frame, world/room, fps\n"
        << "  fps [--window-ms N]       game present rate\n"
        << "  warp --world W --room R [--door D] [--map M --btl B --evt E]\n"
        << "       [--timeout-ms N]     load a room (programs default to the save's)\n"
        << "  peek --rva RVA[:u8|u16|i16|u32|i32|f32|u64][,...] [--samples N]\n"
        << "       [--interval-ms N]    sample exe-relative memory\n"
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

        // Rig commands manage processes themselves (inject/kill take their
        // own --pid). Every other command attaches to one instance: the one
        // named by --pid, or the only KH2 running.
        const bool rigCommand = command == "launch" || command == "inject" ||
                                command == "instances" || command == "kill" ||
                                command == "mute" ||
                                command == "restart" ||
                                command == "boot-load-save";
        if (!rigCommand && command != "help" && command != "--help" &&
            command != "-h") {
            if (const auto pidRaw = ConsumeOption(args, "--pid")) {
                g_targetPid = ParseNumber<std::uint32_t>(*pidRaw, "--pid");
            } else if (ListKh2Processes().size() > 1) {
                const auto error = MakeError(
                    "Several KH2 instances are running; pass --pid "
                    "(see kh2ctl instances)");
                std::cout << error.json << std::endl;
                return error.exitCode;
            }
        }

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
        } else if (command == "mute") {
            result = CmdMute(std::move(args));
        } else if (command == "capture") {
            result = CmdCapture(std::move(args));
        } else if (command == "clip") {
            result = CmdClip(std::move(args));
        } else if (command == "overlay") {
            result = CmdOverlay(std::move(args));
        } else if (command == "fps") {
            result = CmdFps(std::move(args));
        } else if (command == "warp") {
            result = CmdWarp(std::move(args));
        } else if (command == "peek") {
            result = CmdPeek(std::move(args));
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
