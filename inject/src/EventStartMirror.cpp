#include "EventStartMirror.hpp"
#include "ThornDefeatNative.hpp"
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
using InstallFn = void(__fastcall*)(std::uint32_t* context);
constexpr std::uintptr_t RVA_EVENT_START = 0x3AC350;
// VUH-2487: the other event builders (entity, trigger and named events) build a context and hand it
// to 3AD2D0; its caller names the builder. Observation only, for the host's event-origin line.
constexpr std::uintptr_t RVA_EVENT_INSTALL = 0x3AD2D0;
// 3AD2D0 prologue: mov [rsp+8],rbx; mov [rsp+10h],rsi; push rdi; sub rsp,40h
constexpr std::uint8_t kInstallBytes[15] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10,
                                            0x57, 0x48, 0x83, 0xEC, 0x40};
// 3AC350 prologue: mov [rsp+8],rbx; mov [rsp+10h],rbp; mov [rsp+18h],rsi
constexpr std::uint8_t kStartBytes[15] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10,
                                          0x48, 0x89, 0x74, 0x24, 0x18};
std::uintptr_t g_base = 0;
LogFn g_log = nullptr;
StartFn g_original = nullptr;
InstallFn g_installOriginal = nullptr;
std::atomic<bool> g_installed {false};
// Game thread only (the native start and the host producer both run there).
std::uintptr_t g_context = 0;
EventStart g_start {};
bool g_checked = false;
std::uint16_t g_startProgram = 0;
std::uintptr_t g_installContext = 0, g_installCaller = 0;
bool g_replaying = false;
bool g_client = false;
// VUH-2555, friend: its own host-led stage-end start, until the caller sends it (game thread).
bool g_stagePending = false;
StageEnd g_stage {};
// VUH-2555, host: a friend's reported stage end, until the input callback makes the start.
bool g_hostStagePending = false;
StageEnd g_hostStage {};

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
    // Every start taken in open field is recorded with its arguments (the trace each checked row
    // comes from, VUH-2487); only a checked one is ever published.
    const bool sampled = !g_replaying && ReadField(before) && params && CopyParams(params, s.params);
    const bool open = sampled && Open(before);
    const auto* row = open ? eventscenes::QualifiedStart(before.now[0], before.now[1], EventProgram(before), id, flags, s.params)
                           : nullptr;
    const bool checked = row != nullptr;
    if (sampled) {
        s.worldId = before.now[0]; s.roomId = before.now[1]; s.id = id; s.flags = flags;
        if (!enemysync::PermitThorn315(s,EventProgram(before))) return;
    }
    g_original(id, flags, params);
    if (sampled) enemysync::CompleteThorn315(s,EventProgram(before),before.context,ReadContext());
    if (sampled && s.worldId==2 && s.roomId==34 && EventProgram(before)==157 && id==0x353133 &&
        eventscenes::QualifiedStart(s.worldId,s.roomId,157,id,flags,s.params) && g_log)
        g_log("[thorn-defeat] 315 entry path=native context=%llX",static_cast<unsigned long long>(ReadContext()));
    if (!open) return;
    if (g_client && eventscenes::QualifiedStageEnd(before.now[0], before.now[1], EventProgram(before), id, flags, s.params)) {
        g_stage = {};
        g_stage.worldId = before.now[0]; g_stage.roomId = before.now[1]; g_stage.eventProgram = EventProgram(before);
        g_stage.id = id; g_stage.flags = flags; g_stage.params = s.params;
        g_stagePending = true;
    }
    s.worldId = before.now[0]; s.roomId = before.now[1]; s.id = id; s.flags = flags;
    g_context = ReadContext();
    g_start = s;
    g_checked = checked;
    g_startProgram = EventProgram(before);
    if (g_log) g_log("[event-mirror] native start id=%X flags=%X at=%02X/%02X evt=%u checked=%u context=%llX "
                     "params=%X,%X,%X,%X,%X,%X,%X,%X,%X", id, flags, s.worldId, s.roomId, g_startProgram,
                     checked ? 1u : 0u, static_cast<unsigned long long>(g_context), s.params[0], s.params[1],
                     s.params[2], s.params[3], s.params[4], s.params[5], s.params[6], s.params[7], s.params[8]);
}

void __fastcall HookedInstall(std::uint32_t* context) {
    g_installContext = reinterpret_cast<std::uintptr_t>(context);
    g_installCaller = reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - g_base;
    g_installOriginal(context);
}
}  // namespace

void Install(std::uintptr_t exeBase, LogFn log) {
    char producer[2] {}, control[2] {};
    const bool host = GetEnvironmentVariableA("KH2COOP_EVENT_HOLD_PRODUCER", producer, sizeof(producer)) == 1 && producer[0] == '1';
    const bool client = GetEnvironmentVariableA("KH2COOP_EVENT_HOLD_CONTROL", control, sizeof(control)) == 1 && control[0] == '1';
    if (!host && !client) return;
    g_base = exeBase; g_log = log; g_client = client;
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
    if (!host) return;
    // Host only, observation: which builder installed an event that has no recorded start.
    auto* install = reinterpret_cast<void*>(exeBase + RVA_EVENT_INSTALL);
    st = std::memcmp(install, kInstallBytes, sizeof(kInstallBytes)) == 0 ? MH_OK : MH_ERROR_UNSUPPORTED_FUNCTION;
    if (st == MH_OK) st = MH_CreateHook(install, reinterpret_cast<void*>(&HookedInstall), reinterpret_cast<void**>(&g_installOriginal));
    if (st == MH_OK) st = MH_EnableHook(install);
    if (st != MH_OK && log) log("[event-mirror] origin trace off: 3AD2D0 hook %d (%s)", st, MH_StatusToString(st));
}

bool Installed() noexcept { return g_installed.load(std::memory_order_acquire); }

void LogOrigin(std::uintptr_t context, std::uint32_t epoch) noexcept {
    if (!g_log || !context) return;
    if (context == g_context)
        g_log("[event-mirror] host event epoch=%u origin=start id=%X flags=%X at=%02X/%02X evt=%u checked=%u", epoch,
              g_start.id, g_start.flags, g_start.worldId, g_start.roomId, g_startProgram, g_checked ? 1u : 0u);
    else if (context == g_installContext)
        g_log("[event-mirror] host event epoch=%u origin=install caller=%llX", epoch,
              static_cast<unsigned long long>(g_installCaller));
    else
        g_log("[event-mirror] host event epoch=%u origin=unknown context=%llX", epoch, static_cast<unsigned long long>(context));
}

bool StartFor(std::uintptr_t context, EventStart& out) noexcept {
    if (!Installed() || !context || context != g_context || !g_checked) return false;
    out = g_start;
    return true;
}

bool TakeStageEnd(StageEnd& out) noexcept {
    if (!g_stagePending) return false;
    g_stagePending = false;
    out = g_stage;
    return true;
}

bool ReplayStageEnd(const StageEnd& stage) noexcept {
    if (!Installed()) return false;
    g_hostStage = stage;
    g_hostStagePending = true;
    if (g_log) g_log("[event-mirror] stage end from seat %u queued id=%X at=%02X/%02X evt=%u", stage.ownerSlot, stage.id,
                     stage.worldId, stage.roomId, stage.eventProgram);
    return true;
}

void ApplyStageEnd() noexcept {
    if (!g_hostStagePending || !Installed()) return;
    g_hostStagePending = false;
    const StageEnd stage = g_hostStage;
    const auto* row = eventscenes::QualifiedStageEnd(stage.worldId, stage.roomId, stage.eventProgram, stage.id, stage.flags,
                                                     stage.params);
    Field f {};
    if (!row || !ReadField(f) || !Open(f) || f.now[0] != stage.worldId || f.now[1] != stage.roomId ||
        EventProgram(f) != stage.eventProgram) {
        if (g_log) g_log("[event-mirror] stage end from seat %u refused id=%X row=%u at=%02X/%02X evt=%u open=%u reported=%02X/%02X evt=%u",
                         stage.ownerSlot, stage.id, row ? 1u : 0u, f.now[0], f.now[1], EventProgram(f),
                         Open(f) ? 1u : 0u, stage.worldId, stage.roomId, stage.eventProgram);
        return;
    }
    auto params = stage.params;
    g_replaying = true;
    g_original(stage.id, stage.flags, params.data());
    g_replaying = false;
    const auto context = ReadContext();
    if (g_log) g_log("[event-mirror] host ended the stage the friend (seat %u) ended: %.*s context=%llX", stage.ownerSlot,
                     static_cast<int>(row->name.size()), row->name.data(), static_cast<unsigned long long>(context));
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
    if (start.worldId==2 && start.roomId==34 && eventProgram==157 && start.id==0x353133 && g_log)
        g_log("[thorn-defeat] 315 entry path=replay context=%llX",static_cast<unsigned long long>(context));
    if (g_log) g_log("[event-mirror] client started the host's event %.*s context=%llX",
                     static_cast<int>(row->name.size()), row->name.data(), static_cast<unsigned long long>(context));
    return context != 0;
}
}  // namespace kh2coop::inject::eventstartmirror
