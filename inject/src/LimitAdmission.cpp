#include "LimitAdmission.hpp"
#include <Windows.h>
#include "MinHook.h"
#include <atomic>
#include <cstring>

namespace kh2coop::inject::limitadmission {
namespace {
#include "LimitAdmissionContract.inc"
constexpr std::uintptr_t SITES[] {0x3D88E0, 0x3E7800};
constexpr std::uintptr_t LOOKUP_SITE = 0x3E7C30;
using MenuFn = int (__fastcall *)(void*, std::uint16_t*, int);
using LookupFn = std::uintptr_t (__fastcall *)(std::uint32_t);
enum class State : unsigned { Fresh, Installing, Ready, Failed, Retired, Held };
std::atomic<State> g_state {State::Fresh};
std::atomic<unsigned> g_created {0};
std::uintptr_t g_base = 0;
ActiveFn g_active = nullptr;
LogFn g_log = nullptr;
std::atomic<MenuFn> g_menu {nullptr};
std::atomic<LookupFn> g_usable {nullptr};
LookupFn g_lookup = nullptr;

bool Matches(std::uintptr_t at, const void* bytes, std::size_t size) {
    __try { return std::memcmp(reinterpret_cast<const void*>(at), bytes, size) == 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool PolicyPublished() {
    const auto state = g_state.load(std::memory_order_acquire);
    return state == State::Ready || state == State::Held;
}
int __fastcall Menu(void* actor, std::uint16_t* command, int state) {
    // Preserve the existing successful-install menu behavior and original call order.
    const int result = g_menu.load(std::memory_order_acquire)(actor, command, state);
    if (PolicyPublished() && g_active() && command && g_lookup(*command)) return 5;
    return result;
}
std::uintptr_t __fastcall Usable(std::uint32_t command) {
    if (PolicyPublished() && g_active() && g_lookup(command & 0xFFFF)) return 0;
    return g_usable.load(std::memory_order_acquire)(command);
}
// v1.3.3 x64 MinHook emits an E9 to its owned FF25 absolute relay. The three
// full-body contracts have sufficient entry instructions; no patch-above is needed.
// Verify actual target/relay identity, not merely our last successful API result.
bool OwnedPatch(unsigned index) {
    __try {
        const auto* target = reinterpret_cast<const std::uint8_t*>(g_base + SITES[index]);
        if (target[0] != 0xE9) return false;
        std::int32_t displacement = 0;
        std::memcpy(&displacement, target + 1, sizeof(displacement));
        const auto relayAddress = static_cast<std::intptr_t>(g_base + SITES[index] + 5) + displacement;
        const auto* relay = reinterpret_cast<const std::uint8_t*>(relayAddress);
        constexpr std::uint8_t prefix[] {0xFF,0x25,0,0,0,0};
        if (std::memcmp(relay, prefix, sizeof(prefix))) return false;
        std::uintptr_t detour = 0;
        std::memcpy(&detour, relay + sizeof(prefix), sizeof(detour));
        return detour == (index == 0 ? reinterpret_cast<std::uintptr_t>(&Menu)
                                   : reinterpret_cast<std::uintptr_t>(&Usable));
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
unsigned DisableOwned() {
    const unsigned created = g_created.load(std::memory_order_acquire);
    unsigned queued = 0;
    for (unsigned i = 0; i != 2; ++i) {
        if ((created & (1u << i)) &&
            MH_QueueDisableHook(reinterpret_cast<void*>(g_base + SITES[i])) == MH_OK)
            queued |= 1u << i;
    }
    // Apply even if one queued disable failed, cancelling every possible enable.
    const auto applied = MH_ApplyQueued();
    unsigned disabled = applied == MH_OK ? queued : 0;
    for (unsigned i = 0; i != 2; ++i) {
        if (!(created & (1u << i)) || (disabled & (1u << i))) continue;
        const auto status = MH_DisableHook(reinterpret_cast<void*>(g_base + SITES[i]));
        if (status == MH_OK || status == MH_ERROR_DISABLED) disabled |= 1u << i;
    }
    return disabled;
}
bool Fail(const char* stage) {
    // Never-published callbacks delegate even if a physical patch survives rollback.
    g_state.store(State::Failed, std::memory_order_release);
    const auto created = g_created.load(std::memory_order_acquire);
    const auto disabled = created ? DisableOwned() : 0;
    if (g_log) g_log("[limit-admission] REFUSED stage=%s created=%u disabledConfirmed=%u cleanupConfirmed=%u retained=%u terminal=1",
        stage, created, disabled, disabled == created ? 1u : 0u, created ? 1u : 0u);
    return false;
}
}

bool Ready() { return g_state.load(std::memory_order_acquire) == State::Ready; }
bool RejectReinitialization() {
    const auto state = g_state.load(std::memory_order_acquire);
    return state != State::Fresh && state != State::Ready;
}
bool RetainsMinHookResources() { return g_created.load(std::memory_order_acquire) != 0; }

bool Install(std::uintptr_t base, ActiveFn active, LogFn log) {
    auto expected = State::Fresh;
    if (!g_state.compare_exchange_strong(expected, State::Installing, std::memory_order_acq_rel)) {
        if (expected != State::Ready || base != g_base || active != g_active) return false;
        if (OwnedPatch(0) && OwnedPatch(1)) return true;
        // A previously published group lost a patch: no new admission, retain policy.
        g_state.store(State::Held, std::memory_order_release);
        if (g_log) g_log("[limit-admission] REFUSED stage=enabled-identity terminal=1 retained=1");
        return false;
    }
    g_log = log;
    if (!base || !active) return Fail("arguments");
    g_base = base;
    g_active = active;
    if (!Matches(base + SITES[0], MENU_BYTES, sizeof(MENU_BYTES)) ||
        !Matches(base + SITES[1], USABLE_BYTES, sizeof(USABLE_BYTES)) ||
        !Matches(base + LOOKUP_SITE, LOOKUP_BYTES, sizeof(LOOKUP_BYTES))) return Fail("contract");
    HMODULE owner = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCSTR>(&Menu), &owner)) return Fail("module-pin");
    g_lookup = reinterpret_cast<LookupFn>(base + LOOKUP_SITE);
    void* originals[2] {};
    void* detours[] {reinterpret_cast<void*>(&Menu), reinterpret_cast<void*>(&Usable)};
    for (unsigned i = 0; i != 2; ++i) {
        if (MH_CreateHook(reinterpret_cast<void*>(base + SITES[i]), detours[i], &originals[i]) != MH_OK)
            return Fail(i == 0 ? "create-menu" : "create-usable");
        if (i == 0) g_menu.store(reinterpret_cast<MenuFn>(originals[i]), std::memory_order_release);
        else g_usable.store(reinterpret_cast<LookupFn>(originals[i]), std::memory_order_release);
        g_created.fetch_or(1u << i, std::memory_order_release);
    }
    for (unsigned i = 0; i != 2; ++i)
        if (MH_QueueEnableHook(reinterpret_cast<void*>(base + SITES[i])) != MH_OK)
            return Fail(i == 0 ? "queue-menu" : "queue-usable");
    if (MH_ApplyQueued() != MH_OK) return Fail("apply");
    if (!OwnedPatch(0) || !OwnedPatch(1)) return Fail("enabled-identity");
    g_state.store(State::Ready, std::memory_order_release);
    if (g_log) g_log("[limit-admission] ready menu=0x3D88E0 usable=0x3E7800 lookup=0x3E7C30 hooks=2 mandatory=1");
    return true;
}

void HoldForShutdown() {
    auto expected = State::Ready;
    g_state.compare_exchange_strong(expected, State::Held, std::memory_order_acq_rel);
}
void Shutdown() {
    const auto state = g_state.load(std::memory_order_acquire);
    if (state != State::Ready && state != State::Held) return;
    // Hold the existing policy during disable, then delegate only once all owned
    // targets are confirmed disabled. Original storage is never removed/reset.
    g_state.store(State::Held, std::memory_order_release);
    const auto disabled = DisableOwned();
    if (disabled == g_created.load(std::memory_order_acquire))
        g_state.store(State::Retired, std::memory_order_release);
    if (g_log) g_log("[limit-admission] retired=%u held=%u disabledConfirmed=%u retained=1",
        disabled == 3 ? 1u : 0u, disabled == 3 ? 0u : 1u, disabled);
}
bool QueuePreserveHeld() {
    if (g_state.load(std::memory_order_acquire) != State::Held) return true;
    bool ok = true;
    for (unsigned i = 0; i != 2; ++i) {
        if ((g_created.load(std::memory_order_acquire) & (1u << i)) &&
            MH_QueueEnableHook(reinterpret_cast<void*>(g_base + SITES[i])) != MH_OK) ok = false;
    }
    return ok;
}
}
