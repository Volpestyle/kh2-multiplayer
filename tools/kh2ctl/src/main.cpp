#include "kh2coop/GameBridgePC.hpp"
#include "kh2coop/InputMailbox.hpp"
#include "kh2coop/Types.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
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

bool FocusWindow(HWND hwnd) {
    if (IsIconic(hwnd)) {
        ShowWindow(hwnd, SW_RESTORE);
    } else {
        ShowWindow(hwnd, SW_SHOW);
    }

    const DWORD currentThread = GetCurrentThreadId();
    const DWORD targetThread = GetWindowThreadProcessId(hwnd, nullptr);
    bool attachedInput = false;

    if (targetThread != 0 && targetThread != currentThread) {
        attachedInput = AttachThreadInput(currentThread, targetThread, TRUE) != 0;
    }

    const bool ok = SetForegroundWindow(hwnd) != 0 &&
                    BringWindowToTop(hwnd) != 0;

    if (attachedInput) {
        AttachThreadInput(currentThread, targetThread, FALSE);
    }

    return ok || GetForegroundWindow() == hwnd;
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

ProcessCapture RunRestartScript(bool noBuild, bool killOnly, bool copyDll,
                                bool steam) {
    const auto script = RepoRoot() / "scripts" / "restart-kh2.ps1";
    std::wstring command = L"powershell.exe -ExecutionPolicy Bypass -File \"" +
                           script.wstring() + L"\"";
    if (noBuild) command += L" -NoBuild";
    if (killOnly) command += L" -Kill";
    if (copyDll) command += L" -CopyDll";
    if (steam) command += L" -Steam";

    return RunProcessCapture(command, RepoRoot());
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
    const bool copyDll = ConsumeFlag(args, "--copy-dll");
    const bool steam = ConsumeFlag(args, "--steam");
    if (!args.empty()) {
        throw std::runtime_error("Unexpected argument for restart: " + args.front());
    }

    const auto result = RunRestartScript(noBuild, killOnly, copyDll, steam);
    if (!result.launched) {
        return MakeError("Failed to launch restart-kh2.ps1");
    }

    std::ostringstream out;
    out << "{"
        << "\"ok\":" << JsonBool(result.exitCode == 0) << ","
        << "\"exitCode\":" << result.exitCode << ","
        << "\"output\":" << JsonString(result.output)
        << "}";
    return {result.exitCode == 0 ? 0 : static_cast<int>(result.exitCode),
            out.str()};
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
    const bool copyDll = ConsumeFlag(args, "--copy-dll");
    const bool steam = ConsumeFlag(args, "--steam");
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

    const auto restart = RunRestartScript(noBuild, false, copyDll, steam);
    if (!restart.launched) {
        return MakeError("Failed to launch restart-kh2.ps1");
    }
    if (restart.exitCode != 0) {
        std::ostringstream out;
        out << "{"
            << "\"ok\":false,"
            << "\"phase\":\"restart\","
            << "\"exitCode\":" << restart.exitCode << ","
            << "\"output\":" << JsonString(restart.output)
            << "}";
        return {static_cast<int>(restart.exitCode), out.str()};
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
            << "\"restartOutput\":" << JsonString(restart.output) << ","
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
            << "\"restartOutput\":" << JsonString(restart.output) << ","
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
        << "\"restartOutput\":" << JsonString(restart.output) << ","
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
        << "  restart [--no-build] [--kill] [--copy-dll] [--steam]\n"
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
        << "  boot-load-save --slot N [--no-build] [--copy-dll] [--steam]\n"
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
