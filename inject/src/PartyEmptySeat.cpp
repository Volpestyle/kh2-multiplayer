#include "PartyEmptySeat.hpp"
#include "PlayerKit.hpp"
#include <Windows.h>
#include "MinHook.h"
#include <atomic>
#include <cstring>
#include <intrin.h>

namespace kh2coop::inject::partyempty {
namespace {
constexpr std::uintptr_t SITES[] {0x3E3670, 0x3E3680, 0x3E3830, 0x2FC5B0, 0x2FC6D0};
// Original byte sequences from the pinned 9002b2de executable. The tiny leaf
// includes its padding because MinHook needs a relay/patch span beyond RET.
constexpr std::uint8_t SELECTOR_BYTES[] {0x48,0x63,0xC2,0x0F,0xB6,0x04,0x08,0x83,0xE0,0x1F,0xC3,0xCC,0xCC,0xCC,0xCC,0xCC};
constexpr std::uint8_t KEY_BYTES[] {0x48,0x83,0xEC,0x28,0x48,0x63,0xC2,0x0F,0xB6,0x14,0x08,0x83,0xE2,0x1F,0x83,0xFA,0x12};
constexpr std::uint8_t FIND_BYTES[] {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7C,0x24,0x20,0x41,0x56};
constexpr std::uint8_t ALIAS_BYTES[] {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x20};
constexpr std::uint8_t MENU_KEY_BYTES[] {0x85,0xD2,0x78,0x11,0x3B,0x11,0x7D,0x0D,0x48,0x63,0xC2,0x48,0xC1,0xE0,0x05,0x0F,0xBF,0x44,0x08,0x0A,0xC3};
constexpr std::uintptr_t CALL_SITES[] {0x3065C8, 0x36A465, 0x36A722};
constexpr std::uint8_t CALL_BYTES[][5] {{0xE8,0xE3,0x5F,0xFF,0xFF}, {0xE8,0x66,0x22,0xF9,0xFF}, {0xE8,0xA9,0x1F,0xF9,0xFF}};
constexpr unsigned HOOK_COUNT = 5;
using RowFn = std::int32_t (__fastcall *)(const std::uint8_t*, std::int32_t);
RowFn g_original[HOOK_COUNT] {};
std::uintptr_t g_base = 0;
std::atomic<bool> g_ready {false};
std::atomic<bool> g_retained {false};
// Independent of bridge/intent liveness: a mapped zero member still needs the
// native empty-selector branches until the next resolver or member restore.
std::atomic<std::uint64_t> g_owned {0};

bool Read(std::uintptr_t p, void* out, std::size_t n) {
    __try { std::memcpy(out, reinterpret_cast<const void*>(p), n); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool OwnedEmpty() {
    const auto owner = g_owned.load(std::memory_order_acquire);
    if (!owner || !g_ready.load(std::memory_order_acquire)) return false;
    std::uint16_t members[3] {};
    return Read(g_base + playerkit::RVA_RESOLVED_MEMBERS, members, sizeof(members)) &&
        members[0] == static_cast<std::uint16_t>(owner) && members[1] == 0 &&
        members[2] == static_cast<std::uint16_t>(owner >> 32) &&
        owner == g_owned.load(std::memory_order_acquire);
}
std::int32_t Dispatch(unsigned site, const std::uint8_t* row, std::int32_t arg) {
    const DWORD savedError = GetLastError();
    std::array<std::uint8_t, 4> copy {}, projected {};
    const bool use = (site == 2 || (arg >= 0 && arg < 4)) && OwnedEmpty() &&
        Read(reinterpret_cast<std::uintptr_t>(row), copy.data(), copy.size()) && ProjectRow(copy, projected);
    SetLastError(savedError);
    // Native wrappers inspect the row synchronously and do not retain it.
    // In particular FIND preserves the first match for key zero (empty seat).
    return g_original[site](use ? projected.data() : row, arg);
}
std::int32_t __fastcall Selector(const std::uint8_t* row, std::int32_t seat) { return Dispatch(0, row, seat); }
std::int32_t __fastcall Key(const std::uint8_t* row, std::int32_t seat) { return Dispatch(1, row, seat); }
std::int32_t __fastcall Find(const std::uint8_t* row, std::int32_t key) { return Dispatch(2, row, key); }
// Only the proven pause caller supplies a compact index to an original-seat API.
// Preserve all other callers, including 2FC920, which has already translated it.
std::int32_t __fastcall MenuAlias(const std::uint8_t* menu, std::int32_t index) {
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const DWORD savedError = GetLastError();
    std::int32_t count = 0;
    std::int16_t seat = -1;
    std::uintptr_t status = 0;
    const bool use = caller == g_base + 0x3065CD && OwnedEmpty() && index >= 0 && index < 4 &&
        Read(reinterpret_cast<std::uintptr_t>(menu), &count, sizeof(count)) && count > 0 && count <= 4 && index < count &&
        Read(reinterpret_cast<std::uintptr_t>(menu) + 8 + static_cast<unsigned>(index) * 0x20, &seat, sizeof(seat)) &&
        (seat == 0 || seat == 2) &&
        Read(reinterpret_cast<std::uintptr_t>(menu) + 0x20 + static_cast<unsigned>(index) * 0x20, &status, sizeof(status)) && status != 0;
    SetLastError(savedError);
    return g_original[3](menu, use ? seat : index);
}
std::int32_t __fastcall MenuKey(const std::uint8_t* menu, std::int32_t index) {
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const auto key = g_original[4](menu, index);
    const DWORD savedError = GetLastError();
    // These two native companion-only loops already skip key zero. Sora (1)
    // and Roxas (14, normalized to 1) have no companion history row: otherwise
    // native code computes (1-2)*0x30, then a huge unsigned bitset index.
    // The loops' separate player-history block and every other reader delegate.
    const bool skip = (caller == g_base + 0x36A46A || caller == g_base + 0x36A727) &&
        (key == 1 || key == 14) && OwnedEmpty();
    SetLastError(savedError);
    return skip ? 0 : key;
}
}
bool ProjectRow(const std::array<std::uint8_t, 4>& row, std::array<std::uint8_t, 4>& projected) {
    projected = row;
    bool changed = false;
    for (auto& selector : projected) {
        if ((selector & 0x1F) == 1) { selector = static_cast<std::uint8_t>((selector & 0xE0) | 0x12); changed = true; }
    }
    return changed;
}
bool Install(std::uintptr_t base, LogFn log) {
    if (Ready()) return true;
    if (RetainsMinHookResources()) return false;
    const std::uint8_t* bytes[] {SELECTOR_BYTES, KEY_BYTES, FIND_BYTES, ALIAS_BYTES, MENU_KEY_BYTES};
    const std::size_t lengths[] {sizeof(SELECTOR_BYTES), sizeof(KEY_BYTES), sizeof(FIND_BYTES), sizeof(ALIAS_BYTES), sizeof(MENU_KEY_BYTES)};
    const RowFn hooks[] {Selector, Key, Find, MenuAlias, MenuKey};
    for (unsigned i = 0; i < HOOK_COUNT; ++i) {
        std::uint8_t actual[32] {};
        if (!Read(base + SITES[i], actual, lengths[i]) || std::memcmp(actual, bytes[i], lengths[i])) {
            if (log) log("[partyempty] REFUSED: row boundary %llX bytes differ; empty member not admitted", static_cast<unsigned long long>(SITES[i]));
            return false;
        }
    }
    for (unsigned i = 0; i < 3; ++i) {
        std::uint8_t actual[5] {};
        if (!Read(base + CALL_SITES[i], actual, sizeof(actual)) || std::memcmp(actual, CALL_BYTES[i], sizeof(actual))) {
            if (log) log("[partyempty] REFUSED: menu caller %llX bytes differ; empty member not admitted", static_cast<unsigned long long>(CALL_SITES[i]));
            return false;
        }
    }
    // Explicit DLL unload cannot strand a retained row detour if member restore
    // faults. Empty-seat support deliberately keeps its code mapped until exit.
    HMODULE module = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCSTR>(&Install), &module)) {
        if (log) log("[partyempty] REFUSED: cannot pin guard code for its physical ownership lifetime");
        return false;
    }
    g_base = base;
    unsigned created = 0;
    for (; created < HOOK_COUNT; ++created) {
        if (MH_CreateHook(reinterpret_cast<void*>(base + SITES[created]), reinterpret_cast<void*>(hooks[created]),
                          reinterpret_cast<void**>(&g_original[created])) != MH_OK) break;
        // A callback can be in flight even after disable. Pin its original
        // trampoline until process exit, including failed enable/rollback.
        g_retained.store(true, std::memory_order_release);
    }
    bool armed = created == HOOK_COUNT;
    for (unsigned i = 0; armed && i < HOOK_COUNT; ++i)
        armed = MH_QueueEnableHook(reinterpret_cast<void*>(base + SITES[i])) == MH_OK;
    if (armed) armed = MH_ApplyQueued() == MH_OK;
    if (!armed) {
        for (unsigned i = 0; i < created; ++i) MH_QueueDisableHook(reinterpret_cast<void*>(base + SITES[i]));
        MH_ApplyQueued();
        if (log) log("[partyempty] REFUSED: row/menu hooks unavailable; originals retained; empty member not admitted");
        return false;
    }
    g_ready.store(true, std::memory_order_release);
    if (log) log("[partyempty] five native row/menu guards installed; compact-seat portrait and companion-history readers scoped; selected-package qualification still required");
    return true;
}
bool Ready() { return g_ready.load(std::memory_order_acquire); }
bool RetainsMinHookResources() { return g_retained.load(std::memory_order_acquire); }
void NativeResolved() { g_owned.store(0, std::memory_order_release); }
void Arm(std::uint16_t remote, std::uint16_t local) {
    g_owned.store(remote && local ? static_cast<std::uint64_t>(remote) | (static_cast<std::uint64_t>(local) << 32) : 0,
                  std::memory_order_release);
}
bool Shutdown() {
    // Caller must restore physical members first. Do not retire these hooks as
    // a side effect of a runtime death while the installed member is still zero.
    if (g_owned.load(std::memory_order_acquire)) {
        std::uint16_t member = 0;
        if (!Read(g_base + playerkit::RVA_RESOLVED_MEMBERS + 2, &member, sizeof(member)) || member == 0) return false;
    }
    g_owned.store(0, std::memory_order_release);
    if (!g_ready.exchange(false, std::memory_order_acq_rel)) return true;
    bool disabled = true;
    for (const auto site : SITES)
        disabled = (MH_QueueDisableHook(reinterpret_cast<void*>(g_base + site)) == MH_OK) && disabled;
    return (MH_ApplyQueued() == MH_OK) && disabled;
}
bool DisableOtherHooks() {
    // MinHook applies queued desired states in one frozen-thread transaction;
    // these already enabled rows remain enabled throughout that transaction.
    if (MH_QueueDisableHook(MH_ALL_HOOKS) != MH_OK) return false;
    if (Ready()) {
        for (const auto site : SITES)
            if (MH_QueueEnableHook(reinterpret_cast<void*>(g_base + site)) != MH_OK) return false;
    }
    return MH_ApplyQueued() == MH_OK;
}
}
