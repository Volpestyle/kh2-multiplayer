#include "PuppetCommandGuard.hpp"
#include <Windows.h>
#include "MinHook.h"
#include <atomic>
#include <cstring>

namespace kh2coop::inject::puppetcommand {
namespace {
constexpr std::uintptr_t SITES[] {0x401D60, 0x405440, 0x405A60};
constexpr std::uint8_t MENU_BYTES[] {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7C,0x24,0x20};
constexpr std::uint8_t EXECUTE_BYTES[] {0x48,0x89,0x5C,0x24,0x10,0x56,0x48,0x83,0xEC,0x30,0x44,0x0F,0xB7,0x42,0x02};
constexpr std::uint8_t SUMMON_BYTES[] {0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x30,0xF3,0x0F,0x10,0x15,0x3A,0xE1,0x21,0x00};
using MenuFn = std::uint64_t (__fastcall *)(const std::uint16_t*, void*);
using ExecuteFn = void (__fastcall *)(void*, const std::uint16_t*);
MenuFn g_menu = nullptr;
ExecuteFn g_execute = nullptr, g_summon = nullptr;
ActiveFn g_active = nullptr;
std::uintptr_t g_base = 0;
std::atomic<bool> g_ready {false};
std::atomic<unsigned> g_created {0};
bool g_retired = false;

bool Matches(std::uintptr_t address, const void* expected, std::size_t size) {
    __try { return std::memcmp(reinterpret_cast<const void*>(address), expected, size) == 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Restricted() { return g_ready.load(std::memory_order_acquire) && g_active && g_active(); }
bool Activation(const std::uint16_t* command) {
    // The native entry's category is u16+2, not its ID or form selector.
    // An unavailable category cannot grant permission while restricted.
    __try { return !command || command[1] == 4 || command[1] == 0x48; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
}
std::uint64_t __fastcall Menu(const std::uint16_t* command, void* actor) {
    if (Restricted() && Activation(command)) return 5; // native unavailable
    return g_menu(command, actor);
}
void __fastcall Execute(void* actor, const std::uint16_t* command) {
    if (Restricted() && Activation(command)) return;
    g_execute(actor, command);
}
void __fastcall Summon(void* actor, const std::uint16_t* command) {
    // Separate registered callback: it has no category dispatch of its own.
    if (Restricted()) return;
    g_summon(actor, command);
}
bool DisableCreated() {
    bool queued = true;
    const unsigned count = g_created.load(std::memory_order_acquire);
    for (unsigned i = 0; i < count; ++i)
        if (MH_QueueDisableHook(reinterpret_cast<void*>(g_base + SITES[i])) != MH_OK) queued = false;
    // Apply even after one queue failure: retire every gate that can be retired.
    const bool applied = MH_ApplyQueued() == MH_OK;
    return queued && applied;
}
}

bool Install(std::uintptr_t base, ActiveFn active, LogFn log) {
    if (g_ready.load(std::memory_order_acquire)) return base == g_base && active == g_active;
    if (g_retired || !base || !active) return false;
    if (!Matches(base + SITES[0], MENU_BYTES, sizeof(MENU_BYTES)) ||
        !Matches(base + SITES[1], EXECUTE_BYTES, sizeof(EXECUTE_BYTES)) ||
        !Matches(base + SITES[2], SUMMON_BYTES, sizeof(SUMMON_BYTES))) {
        if (log) log("[puppet-command] REFUSED: native command entry bytes differ; puppets not admitted");
        return false;
    }
    HMODULE owner = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCSTR>(&Menu), &owner)) {
        if (log) log("[puppet-command] REFUSED: callback module cannot be pinned");
        return false;
    }
    g_base = base;
    g_active = active;
    void* detours[] {reinterpret_cast<void*>(&Menu), reinterpret_cast<void*>(&Execute), reinterpret_cast<void*>(&Summon)};
    void** originals[] {reinterpret_cast<void**>(&g_menu), reinterpret_cast<void**>(&g_execute), reinterpret_cast<void**>(&g_summon)};
    bool armed = true;
    for (unsigned i = 0; i < 3; ++i) {
        if (MH_CreateHook(reinterpret_cast<void*>(base + SITES[i]), detours[i], originals[i]) != MH_OK) { armed = false; break; }
        g_created.store(i + 1, std::memory_order_release);
    }
    for (unsigned i = 0; armed && i < 3; ++i)
        armed = MH_QueueEnableHook(reinterpret_cast<void*>(base + SITES[i])) == MH_OK;
    if (armed) armed = MH_ApplyQueued() == MH_OK;
    if (!armed) {
        g_retired = true;
        const bool disabled = DisableCreated();
        if (log) log("[puppet-command] REFUSED: all three native gates required; rollbackDisabled=%u retained=%u puppets not admitted",
            disabled ? 1u : 0u, g_created.load(std::memory_order_acquire));
        return false;
    }
    g_ready.store(true, std::memory_order_release);
    if (log) log("[puppet-command] ready menu=0x401D60 execute=0x405440 summon=0x405A60 driveType=4 summonType=0x48 noGaugeWrites=1");
    return true;
}
bool Ready() { return g_ready.load(std::memory_order_acquire); }
bool RetainsMinHookResources() { return g_created.load(std::memory_order_acquire) != 0; }
void Shutdown() {
    // Retired callbacks delegate; their original pointers/trampolines remain
    // valid even if one hook cannot be disabled or a native call is in flight.
    g_ready.store(false, std::memory_order_release);
    g_retired = true;
    DisableCreated();
}
}
