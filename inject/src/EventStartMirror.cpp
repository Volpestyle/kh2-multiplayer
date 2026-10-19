#include "EventStartMirror.hpp"
#include "kh2coop/Protocol.hpp"
#include "kh2coop/EventScenes.hpp"
#include "kh2coop/KH2Offsets.hpp"
#include <Windows.h>
#include <MinHook.h>
#include <atomic>
#include <cstring>

namespace kh2coop::inject::eventstartmirror {
namespace {
using StartFn = void(__fastcall*)(std::uint32_t id, std::uint8_t flags, std::uint32_t* params);
constexpr std::uintptr_t RVA_EVENT_START = 0x3AC350;
// 3AC350 prologue: mov [rsp+8],rbx; mov [rsp+10h],rbp; mov [rsp+18h],rsi
constexpr std::uint8_t kStartBytes[15] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10,
                                          0x48, 0x89, 0x74, 0x24, 0x18};
std::uintptr_t g_base = 0;
LogFn g_log = nullptr;
StartFn g_original = nullptr;
std::atomic<bool> g_installed {false};
// Game thread only (the native start and the host producer both run there).
std::uintptr_t g_context = 0;
EventStart g_start {};
bool g_replaying = false;

struct Field {
    std::uint8_t now[10] {};
    std::uint8_t inField = 0, menu = 0;
    std::int32_t cutscene = -1;
    std::uintptr_t context = 1;
};
bool ReadField(Field& f) noexcept {
    __try {
        std::memcpy(f.now, reinterpret_cast<const void*>(g_base + offsets::NOW), sizeof(f.now));
        f.inField = *reinterpret_cast<const std::uint8_t*>(g_base + offsets::IN_FIELD);
        f.menu = *reinterpret_cast<const std::uint8_t*>(g_base + offsets::OPEN_MENU);
        f.cutscene = *reinterpret_cast<const std::int32_t*>(g_base + offsets::CUTSCENE_STATE);
        f.context = *reinterpret_cast<const std::uintptr_t*>(g_base + offsets::EVENT_CONTEXT);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Open(const Field& f) noexcept { return f.inField && f.menu == 0xFF && f.cutscene == 0 && f.context == 0; }
std::uint16_t EventProgram(const Field& f) noexcept { return static_cast<std::uint16_t>(f.now[8] | (f.now[9] << 8)); }
std::uintptr_t ReadContext() noexcept {
    __try { return *reinterpret_cast<const std::uintptr_t*>(g_base + offsets::EVENT_CONTEXT); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
bool CopyParams(const std::uint32_t* params, std::array<std::uint32_t, 9>& out) noexcept {
    __try { std::memcpy(out.data(), params, sizeof(out)); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void __fastcall HookedStart(std::uint32_t id, std::uint8_t flags, std::uint32_t* params) {
    Field before {};
    EventStart s {};
    const bool checked = !g_replaying && ReadField(before) && Open(before) && params && CopyParams(params, s.params) &&
        eventscenes::QualifiedStart(before.now[0], before.now[1], EventProgram(before), id, flags, s.params);
    g_original(id, flags, params);
    if (!checked) return;
    s.worldId = before.now[0]; s.roomId = before.now[1]; s.id = id; s.flags = flags;
    g_context = ReadContext();
    g_start = s;
    if (g_log) g_log("[event-mirror] native start id=%X flags=%X at=%02X/%02X evt=%u context=%llX", id, flags,
                     s.worldId, s.roomId, EventProgram(before), static_cast<unsigned long long>(g_context));
}
}  // namespace

void Install(std::uintptr_t exeBase, LogFn log) {
    char producer[2] {}, control[2] {};
    const bool host = GetEnvironmentVariableA("KH2COOP_EVENT_HOLD_PRODUCER", producer, sizeof(producer)) == 1 && producer[0] == '1';
    const bool client = GetEnvironmentVariableA("KH2COOP_EVENT_HOLD_CONTROL", control, sizeof(control)) == 1 && control[0] == '1';
    if (!host && !client) return;
    g_base = exeBase; g_log = log;
    if (std::memcmp(reinterpret_cast<const void*>(exeBase + RVA_EVENT_START), kStartBytes, sizeof(kStartBytes)) != 0) {
        if (log) log("[event-mirror] REFUSED: 3AC350 bytes differ; in-field events keep the hold");
        return;
    }
    auto* target = reinterpret_cast<void*>(exeBase + RVA_EVENT_START);
    MH_STATUS st = MH_CreateHook(target, reinterpret_cast<void*>(&HookedStart), reinterpret_cast<void**>(&g_original));
    if (st == MH_OK) st = MH_EnableHook(target);
    if (st != MH_OK) {
        if (log) log("[event-mirror] REFUSED: hook failed %d (%s); in-field events keep the hold", st, MH_StatusToString(st));
        return;
    }
    g_installed.store(true, std::memory_order_release);
    if (log) log("[event-mirror] installed rows=%zu", eventscenes::InFieldEvents.size());
}

bool Installed() noexcept { return g_installed.load(std::memory_order_acquire); }

bool StartFor(std::uintptr_t context, EventStart& out) noexcept {
    if (!Installed() || !context || context != g_context) return false;
    out = g_start;
    return true;
}

bool Replay(const EventStart& start, std::uint16_t eventProgram) noexcept {
    if (!Installed()) return false;
    const auto* row = eventscenes::QualifiedStart(start.worldId, start.roomId, eventProgram, start.id, start.flags,
                                                  start.params);
    Field f {};
    if (!row || !ReadField(f) || !Open(f) || f.now[0] != start.worldId || f.now[1] != start.roomId ||
        EventProgram(f) != eventProgram) {
        if (g_log) g_log("[event-mirror] replay refused id=%X row=%u at=%02X/%02X evt=%u open=%u host=%02X/%02X evt=%u",
                         start.id, row ? 1u : 0u, f.now[0], f.now[1], EventProgram(f), Open(f) ? 1u : 0u,
                         start.worldId, start.roomId, eventProgram);
        return false;
    }
    auto params = start.params;
    g_replaying = true;
    g_original(start.id, start.flags, params.data());
    g_replaying = false;
    const auto context = ReadContext();
    if (g_log) g_log("[event-mirror] client started the host's event %.*s context=%llX",
                     static_cast<int>(row->name.size()), row->name.data(), static_cast<unsigned long long>(context));
    return context != 0;
}
}  // namespace kh2coop::inject::eventstartmirror
