#include "PartyEmptySeat.hpp"
#include "PlayerKit.hpp"
#include <Windows.h>
#include "MinHook.h"
#include <atomic>
#include <cstring>
#include <intrin.h>

namespace kh2coop::inject::partyempty {
namespace {
constexpr std::uintptr_t SITES[] {0x3E3670, 0x3E3680, 0x3E3830, 0x2FC5B0, 0x2FC6D0, 0x34EE00, 0x305FE0};
// Original byte sequences from the pinned 9002b2de executable. The tiny leaf
// includes its padding because MinHook needs a relay/patch span beyond RET.
constexpr std::uint8_t SELECTOR_BYTES[] {0x48,0x63,0xC2,0x0F,0xB6,0x04,0x08,0x83,0xE0,0x1F,0xC3,0xCC,0xCC,0xCC,0xCC,0xCC};
constexpr std::uint8_t KEY_BYTES[] {0x48,0x83,0xEC,0x28,0x48,0x63,0xC2,0x0F,0xB6,0x14,0x08,0x83,0xE2,0x1F,0x83,0xFA,0x12};
constexpr std::uint8_t FIND_BYTES[] {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7C,0x24,0x20,0x41,0x56};
constexpr std::uint8_t ALIAS_BYTES[] {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x20};
constexpr std::uint8_t MENU_KEY_BYTES[] {0x85,0xD2,0x78,0x11,0x3B,0x11,0x7D,0x0D,0x48,0x63,0xC2,0x48,0xC1,0xE0,0x05,0x0F,0xBF,0x44,0x08,0x0A,0xC3};
constexpr std::uintptr_t CALL_SITES[] {0x3065C8, 0x36A465, 0x36A722};
constexpr std::uint8_t CALL_BYTES[][5] {{0xE8,0xE3,0x5F,0xFF,0xFF}, {0xE8,0x66,0x22,0xF9,0xFF}, {0xE8,0xA9,0x1F,0xF9,0xFF}};
constexpr std::uint8_t ADMISSION_BYTES[] {0x40,0x53,0x48,0x83,0xEC,0x20,0x8B,0xD9};
constexpr std::uint8_t PARTY_BYTES[] {0xB0,0x01,0xC3,0xCC,0xCC,0xCC,0xCC,0xCC};
constexpr unsigned HOOK_COUNT = 7;
using AdmissionFn = std::uint8_t (__fastcall *)(std::int32_t);
using RowFn = std::int32_t (__fastcall *)(const std::uint8_t*, std::int32_t);
RowFn g_original[HOOK_COUNT] {};
std::uintptr_t g_base = 0;
std::atomic<bool> g_ready {false};
std::atomic<bool> g_retained {false};
LogFn g_abilitiesTraceLog = nullptr;
std::atomic<unsigned> g_abilitiesRootReceipts {0}, g_abilitiesItemsReceipts {0};
LogFn g_partyTraceLog = nullptr;
std::atomic<unsigned> g_partyTraceReceipts {0};
LogFn g_traceLog = nullptr;
std::atomic<unsigned> g_traceReceipts {0}; // process lifetime; never renewed by loads

// Independent of bridge/intent liveness: a mapped zero member still needs the
// native empty-selector branches until the next resolver or member restore.
std::atomic<std::uint64_t> g_owned {0};

bool Read(std::uintptr_t p, void* out, std::size_t n) {
    __try { std::memcpy(out, reinterpret_cast<const void*>(p), n); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool OwnedTuple(unsigned* missingOut = nullptr) {
    const auto owner = g_owned.load(std::memory_order_acquire);
    if (!owner || !g_ready.load(std::memory_order_acquire)) return false;
    const auto tag = static_cast<unsigned>(owner >> 48);
    if (tag < 1 || tag > 3) return false;
    if (missingOut) *missingOut = tag == 1 ? 3u : tag - 1;
    std::uint16_t members[3] {};
    return Read(g_base + playerkit::RVA_RESOLVED_MEMBERS, members, sizeof(members)) &&
        members[0] == static_cast<std::uint16_t>(owner) &&
        members[1] == static_cast<std::uint16_t>(owner >> 16) &&
        members[2] == static_cast<std::uint16_t>(owner >> 32) &&
        owner == g_owned.load(std::memory_order_acquire);
}
bool OwnedEmpty(unsigned* missingOut = nullptr) {
    unsigned missing = 3;
    if (!OwnedTuple(&missing) || missing == 3) return false;
    if (missingOut) *missingOut = missing;
    return true;
}
std::int32_t Dispatch(unsigned site, const std::uint8_t* row, std::int32_t arg) {
    const DWORD savedError = GetLastError();
    unsigned missingIndex = 0;
    std::array<std::uint8_t, 4> copy {}, projected {};
    const bool use = (site == 2 || (arg >= 0 && arg < 4)) && OwnedEmpty(&missingIndex) &&
        Read(reinterpret_cast<std::uintptr_t>(row), copy.data(), copy.size()) && ProjectRow(copy, projected, missingIndex);
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
    unsigned missingIndex = 0;
    const bool use = caller == g_base + 0x3065CD && OwnedEmpty(&missingIndex) && index >= 0 && index < 4 &&
        Read(reinterpret_cast<std::uintptr_t>(menu), &count, sizeof(count)) && count > 0 && count <= 4 && index < count &&
        Read(reinterpret_cast<std::uintptr_t>(menu) + 8 + static_cast<unsigned>(index) * 0x20, &seat, sizeof(seat)) &&
        seat >= 0 && seat < 3 && static_cast<unsigned>(seat) != missingIndex &&
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
        (key == 1 || key == 14) && OwnedTuple();
    SetLastError(savedError);
    return skip ? 0 : key;
}
void AbilitiesReceipt(bool root, std::uintptr_t caller, std::uint32_t page,
                      std::int32_t selection, std::uint16_t mask, std::uint64_t owner) {
    if (!g_abilitiesTraceLog) return;
    auto& receipts = root ? g_abilitiesRootReceipts : g_abilitiesItemsReceipts;
    auto sequence = receipts.load(std::memory_order_relaxed);
    while (sequence < 16 && !receipts.compare_exchange_weak(sequence, sequence + 1,
                std::memory_order_relaxed, std::memory_order_relaxed)) {}
    if (sequence < 16)
        g_abilitiesTraceLog("[partyempty-abilities-%s] refused seq=%u tick=%llu caller=%llX state=%u selection=%d mask=%X feature=1 owner=%X/0/%X",
            root ? "root" : "items", sequence + 1, static_cast<unsigned long long>(GetTickCount64()),
            static_cast<unsigned long long>(caller - g_base), page, selection,
            static_cast<unsigned>(mask), static_cast<unsigned>(static_cast<std::uint16_t>(owner)),
            static_cast<unsigned>(static_cast<std::uint16_t>(owner >> 32)));
}
bool RefuseItemsAbilities(std::uintptr_t caller, std::int32_t selection) {
    if (caller != g_base + 0x2F62EC || selection != -5) return false;
    const auto owner = g_owned.load(std::memory_order_acquire);
    unsigned missing = 3;
    std::uint32_t page = ~0u, finalPage = ~0u;
    std::uint16_t mask = 0, finalMask = 0;
    const bool refuse = OwnedEmpty(&missing) && missing == 1 &&
        Read(g_base + 0xBEE64C, &page, sizeof(page)) && page == 1 &&
        Read(g_base + 0xBEEC20, &mask, sizeof(mask)) &&
        Read(g_base + 0xBEE64C, &finalPage, sizeof(finalPage)) && finalPage == page &&
        Read(g_base + 0xBEEC20, &finalMask, sizeof(finalMask)) && finalMask == mask &&
        OwnedEmpty(&missing) && missing == 1 && owner == g_owned.load(std::memory_order_acquire);
    if (refuse) AbilitiesReceipt(false, caller, page, selection, mask, owner);
    return refuse;
}
std::uint8_t __fastcall ItemsAdmission(std::int32_t selection) {
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const DWORD savedError = GetLastError();
    if (RefuseItemsAbilities(caller, selection)) {
        SetLastError(savedError);
        return 0; // native feedback4 before state1/-5 cleanup or state0F request
    }
    std::uint32_t page = 0;
    std::uintptr_t menu = 0, status = 0;
    std::int32_t count = 0;
    std::int16_t seat = -1, key = 0;
    bool refuse = false;
    if (caller == g_base + 0x2F62EC && selection > 0 && OwnedEmpty() &&
        Read(g_base + 0xBEE64C, &page, sizeof(page)) && page == 1 &&
        Read(g_base + 0xBEEC28, &menu, sizeof(menu)) && menu &&
        Read(menu, &count, sizeof(count)) && count > 0 && count <= 4) {
        const auto compact = selection; // state1 has compact characters followed by Stock
        if (compact < count) {
            const auto entry = menu + static_cast<unsigned>(compact) * 0x20;
            refuse = Read(entry + 8, &seat, sizeof(seat)) && seat == 2 &&
                Read(entry + 10, &key, sizeof(key)) && (key == 1 || key == 14) &&
                Read(entry + 0x20, &status, sizeof(status)) && status != 0;
        }
    }
    SetLastError(savedError);
    if (refuse) {
        // Opt-in observation of the existing consumed callback. No hook, menu or
        // SAVE mutation; admission and the native feedback4 return stay identical.
        if (g_traceLog) {
            auto sequence = g_traceReceipts.load(std::memory_order_relaxed);
            while (sequence < 16 && !g_traceReceipts.compare_exchange_weak(sequence, sequence + 1,
                        std::memory_order_relaxed, std::memory_order_relaxed)) {}
            if (sequence < 16) {
                const auto owner = g_owned.load(std::memory_order_acquire);
                g_traceLog("[partyempty-items] refused seq=%u tick=%llu caller=%llX state=%u selection=%d menu=%llX count=%d seat=%d key=%d status=%llX owner=%X/0/%X",
                    sequence + 1, static_cast<unsigned long long>(GetTickCount64()),
                    static_cast<unsigned long long>(caller - g_base), page, selection,
                    static_cast<unsigned long long>(menu), count, static_cast<int>(seat), static_cast<int>(key),
                    static_cast<unsigned long long>(status), static_cast<unsigned>(static_cast<std::uint16_t>(owner)),
                    static_cast<unsigned>(static_cast<std::uint16_t>(owner >> 32)));
            }
        }
        SetLastError(savedError);
        return 0; // native feedback4 declines before any deeper builder
    }
    return reinterpret_cast<AdmissionFn>(g_original[5])(selection);
}

// 2F6270 forwards its input ECX unchanged to R9 at 2F62E9. The root
// caller supplies its actual compact selection; no unproved cursor ABI is read.
std::uint8_t __fastcall PartyAdmission(std::int32_t selection) {
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const DWORD savedError = GetLastError();
    const auto owner = g_owned.load(std::memory_order_acquire);
    unsigned missing = 3;
    std::uint32_t page = ~0u, finalPage = ~0u;
    std::uint16_t mask = 0, finalMask = 0;
    int feature = -1;
    if (caller == g_base + 0x2F62EC && selection >= 0 && selection < 8 &&
        OwnedEmpty(&missing) && missing == 1 &&
        Read(g_base + 0xBEE64C, &page, sizeof(page)) && page == 0 &&
        Read(g_base + 0xBEEC20, &mask, sizeof(mask))) {
        int compact = 0;
        for (unsigned bit = 0; bit < 8; ++bit) {
            if ((mask & (1u << bit)) && compact++ == selection) { feature = static_cast<int>(bit); break; }
        }
    }
    const bool refuse = (feature == 3 || feature == 1) &&
        Read(g_base + 0xBEE64C, &finalPage, sizeof(finalPage)) && finalPage == page &&
        Read(g_base + 0xBEEC20, &finalMask, sizeof(finalMask)) && finalMask == mask &&
        OwnedEmpty(&missing) && missing == 1 && owner == g_owned.load(std::memory_order_acquire);
    if (refuse && feature == 1) AbilitiesReceipt(true, caller, page, selection, mask, owner);
    if (refuse && feature == 3 && g_partyTraceLog) {
        auto sequence = g_partyTraceReceipts.load(std::memory_order_relaxed);
        while (sequence < 16 && !g_partyTraceReceipts.compare_exchange_weak(sequence, sequence + 1,
                    std::memory_order_relaxed, std::memory_order_relaxed)) {}
        if (sequence < 16)
            g_partyTraceLog("[partyempty-party] refused seq=%u tick=%llu caller=%llX state=%u selection=%d mask=%X feature=%d owner=%X/0/%X",
                sequence + 1, static_cast<unsigned long long>(GetTickCount64()),
                static_cast<unsigned long long>(caller - g_base), page, selection,
                static_cast<unsigned>(mask), feature, static_cast<unsigned>(static_cast<std::uint16_t>(owner)),
                static_cast<unsigned>(static_cast<std::uint16_t>(owner >> 32)));
    }
    SetLastError(savedError);
    return refuse ? 0 : reinterpret_cast<AdmissionFn>(g_original[6])(selection);
}

}
bool ProjectRow(const std::array<std::uint8_t, 4>& row, std::array<std::uint8_t, 4>& projected, unsigned missingIndex) {
    projected = row;
    if (missingIndex != 1 && missingIndex != 2) return false;
    bool changed = false;
    for (auto& selector : projected) {
        if (static_cast<unsigned>(selector & 0x1F) == missingIndex) { selector = static_cast<std::uint8_t>((selector & 0xE0) | 0x12); changed = true; }
    }
    return changed;
}
bool Install(std::uintptr_t base, LogFn log) {
    if (Ready()) return true;
    if (RetainsMinHookResources()) return false;
    const std::uint8_t* bytes[] {SELECTOR_BYTES, KEY_BYTES, FIND_BYTES, ALIAS_BYTES, MENU_KEY_BYTES, ADMISSION_BYTES, PARTY_BYTES};
    const std::size_t lengths[] {sizeof(SELECTOR_BYTES), sizeof(KEY_BYTES), sizeof(FIND_BYTES), sizeof(ALIAS_BYTES), sizeof(MENU_KEY_BYTES), sizeof(ADMISSION_BYTES), sizeof(PARTY_BYTES)};
    const RowFn hooks[] {Selector, Key, Find, MenuAlias, MenuKey, reinterpret_cast<RowFn>(ItemsAdmission), reinterpret_cast<RowFn>(PartyAdmission)};
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
    struct Guard { std::uintptr_t site; std::uint8_t bytes[64]; std::size_t size; };
    const Guard admissionGuards[] {
        // Accepted Items -5 and root feature1 state0F routing are admission dependencies.
        {0x34E1B7, {0x83,0xFF,0xFB,0x0F,0x84,0xDE,0x00,0x00,0x00,0x83,0xFF,0xFC,0x0F,0x84,0x8B,0x00,0x00,0x00,0x83,0xFF,0xFE,0x74,0x62,0x85,0xFF,0x0F,0x88,0x35,0x01,0x00,0x00,0x3B,0xFB,0x0F,0x8D,0x2D,0x01,0x00,0x00}, 39},
        {0x34E29E, {0xE8,0xDD,0x67,0xFB,0xFF,0xE8,0x68,0xC2,0xFA,0xFF,0x33,0xC9,0xE8,0x61,0x5C,0xFD,0xFF,0xE8,0x8C,0x03,0x00,0x00,0xB9,0x01,0x00,0x00,0x00,0xE8,0xF2,0x7C,0xFA,0xFF,0xB9,0x0F,0x00,0x00,0x00,0xE8,0x98,0x82,0xFA,0xFF}, 42},
        {0x303A64, {0xB9,0x01,0x00,0x00,0x00,0xEB,0x21,0xB9,0x0F,0x00,0x00,0x00,0xEB,0x1A,0xB9,0x14,0x00,0x00,0x00,0xEB,0x13}, 21},
        {0x303B94, {0x64,0x3A,0x30,0x00,0x6B,0x3A,0x30,0x00,0x80,0x3A,0x30,0x00,0x72,0x3A,0x30,0x00,0x79,0x3A,0x30,0x00,0xE0,0x3A,0x30,0x00,0xE0,0x3A,0x30,0x00,0x87,0x3A,0x30,0x00}, 32},

        {0x2F6270, {0x48,0x83,0xEC,0x28,0x84,0xD2,0x0F,0x85,0x96,0x00,0x00,0x00,0x83,0xF9,0xFE,0x75,0x13,0x0F,0xB6,0x54,0x24,0x50,0xB9,0x03,0x00,0x00,0x00,0x48,0x83,0xC4,0x28,0xE9,0xAC,0x54,0xFF,0xFF,0x8D,0x41,0x0D,0x83,0xF8,0x01,0x76,0x76,0x83,0xF9,0xFC,0x75,0x15,0x0F,0xB6,0x54,0x24,0x50,0x41,0x0F,0xB6,0xC8,0x83,0xC1,0x04,0x48,0x83,0xC4}, 64},
        {0x2F62B0, {0x28,0xE9,0x8A,0x54,0xFF,0xFF,0x8D,0x41,0x07,0x83,0xF8,0x02,0x76,0x14,0x8D,0x41,0x09,0x83,0xF8,0x01,0x76,0x0C,0x8D,0x41,0x0B,0x83,0xF8,0x01,0x76,0x04,0x85,0xC9,0x78,0x39,0x4D,0x85,0xC9,0x75,0x12,0x0F,0xB6,0x54,0x24,0x50,0x41,0x8D,0x49,0x02,0x48,0x83,0xC4,0x28,0xE9,0x57,0x54,0xFF,0xFF,0x41,0xFF,0xD1,0xB9,0x02,0x00,0x00}, 64},
        {0x2F62F0, {0x00,0x84,0xC0,0xBA,0x04,0x00,0x00,0x00,0x0F,0x45,0xD1,0x8B,0xCA,0x0F,0xB6,0x54,0x24,0x50,0x48,0x83,0xC4,0x28,0xE9,0x35,0x54,0xFF,0xFF,0x33,0xC0,0x48,0x83,0xC4,0x28,0xC3,0x0F,0xB6,0x54,0x24,0x50,0xB9,0x01,0x00,0x00,0x00,0x48,0x83,0xC4,0x28,0xE9,0x1B,0x54,0xFF,0xFF}, 53},
        {0x5BA6A0, {0x00,0x00,0x01,0x00,0x02,0x00,0x04,0x00,0x05,0x00}, 10},

        {0x2EB8B0, {0x48,0x0F,0xBF,0x05,0x70,0x78,0x45,0x00,0x48,0x8D,0x15,0x41,0x47,0xD1,0xFF,0x83,0xF8,0x06,0x7D,0x08,0x0F,0xBF,0x84,0x42,0x20,0x31,0x74,0x00,0x48,0x98,0x0F,0xBF,0x84,0x42,0x94,0xA6,0x5B,0x00,0x3B,0xC8,0x0F,0x94,0xC0,0xC3}, 44},
        {0x2EB740, {0x40,0x53,0x48,0x83,0xEC,0x20,0x4C,0x8D,0x05,0xB3,0x48,0xD1,0xFF,0x83,0xF9,0x06,0x7D,0x0C,0x48,0x63,0xC1,0x41,0x0F,0xBF,0x8C,0x40,0x20,0x31,0x74,0x00,0x48,0x63,0xC1,0x41,0x0F,0xBF,0x9C,0x40,0x94,0xA6,0x5B,0x00,0x84,0xD2,0x74,0x09,0x33,0xD2,0x8B,0xCB,0xE8,0xF9,0x19,0xEF,0xFF,0x8B,0xC3,0x48,0x83,0xC4,0x20,0x5B,0xC3}, 63},
        {0x743120, {0x06,0x00,0x07,0x00,0x08,0x00,0x09,0x00,0x0A,0x00,0x08,0x00}, 12},
        {0x303BC0, {0x44,0x0F,0xB7,0x05,0x58,0xB0,0x8E,0x00,0x33,0xD2,0x8B,0xC2,0x0F,0x1F,0x40,0x00,0x41,0x0F,0xA3,0xC0,0x73,0x06,0x3B,0xD1,0x74,0x0E,0xFF,0xC2,0xFF,0xC0,0x83,0xF8,0x08,0x7C,0xED,0xB8,0xFF,0xFF,0xFF,0xFF,0xC3}, 41},

        // Root callback setup and refusal branch dominate descriptor/state writes.
        {0x3039DB, {0x4C,0x8D,0x0D,0xFE,0x25,0,0,0x41,0xB0,1,0x8B,0xCF}, 12},
        {0x3039E7, {0xE8,0x84,0x28,0xFF,0xFF}, 5},
        {0x3039EC, {0x8B,0xC8,0xE8,0xBD,0x7E,0xFE,0xFF,0x84,0xC0,0x0F,0x85,0x74,1,0,0}, 15},
        {0x3039FB, {0x8D,0x47,4,0xA9,0xFD,0xFF,0xFF,0xFF,0x0F,0x84,2,1,0,0}, 14},
        {0x34E197, {0x4C,0x8D,0x0D,0x62,0x0C,0,0}, 7},
        {0x2F62E9, {0x41,0xFF,0xD1}, 3},
        {0x2F5F30, {0x8B,0x05,0x16,0x87,0x8F,0,0xC3}, 7},
        // The selected compact index and ALfalse refusal must dominate state2.
        {0x34E1A1, {0x8B,0xCF}, 2},
        {0x34E1A3, {0xE8,0xC8,0x80,0xFA,0xFF}, 5},
        {0x34E1A8, {0x8B,0xC8,0xE8,0x01,0xD7,0xF9,0xFF}, 7},
        {0x34E1AF, {0x84,0xC0}, 2},
        {0x34E1B1, {0x0F,0x85,0x54,0x01,0,0}, 6},
        // Native input chooses feedback4 for ALfalse, feedback2 for ALtrue.
        {0x2F62EC, {0xB9,0x02,0,0,0,0x84,0xC0,0xBA,0x04,0,0,0,0x0F,0x45,0xD1,0x8B,0xCA}, 17}
    };
    for (const auto& guard : admissionGuards) {
        std::uint8_t actual[64] {};
        if (!Read(base + guard.site, actual, guard.size) || std::memcmp(actual, guard.bytes, guard.size)) {
            if (log) log("[partyempty] REFUSED: Items admission boundary %llX bytes differ", static_cast<unsigned long long>(guard.site));
            return false;
        }
    }
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
    char traceSetting[2] {};
    const bool trace = GetEnvironmentVariableA("KH2COOP_ITEMS_ADMISSION_TRACE", traceSetting, sizeof(traceSetting)) == 1 && traceSetting[0] == '1';
    g_traceLog = trace ? log : nullptr;
    const bool partyTrace = GetEnvironmentVariableA("KH2COOP_PARTY_ADMISSION_TRACE", traceSetting, sizeof(traceSetting)) == 1 && traceSetting[0] == '1';
    g_partyTraceLog = partyTrace ? log : nullptr;
    const bool abilitiesTrace = GetEnvironmentVariableA("KH2COOP_ABILITIES_ADMISSION_TRACE", traceSetting, sizeof(traceSetting)) == 1 && traceSetting[0] == '1';
    g_abilitiesTraceLog = abilitiesTrace ? log : nullptr;
    g_ready.store(true, std::memory_order_release);
    if (log) log("[partyempty] seven native row/menu guards installed; compact-seat portrait, companion-history and deeper-player-Items and ordinary-root-Party/Abilities plus Items-5 admission scoped; selected-package qualification still required");
    return true;
}
bool Ready() { return g_ready.load(std::memory_order_acquire); }
bool RetainsMinHookResources() { return g_retained.load(std::memory_order_acquire); }
void NativeResolved() { g_owned.store(0, std::memory_order_release); }
void Arm(std::uint16_t remote, std::uint16_t local) {
    ArmTuple({remote, 0, local}, 1);
}
void ArmCompanionTuple(const std::array<std::uint16_t, 3>& members) {
    if (!members[0] || (members[1] != 0x5C && members[1] != 0x5D) || !members[2]) return;
    g_owned.store(static_cast<std::uint64_t>(members[0]) |
        (static_cast<std::uint64_t>(members[1]) << 16) |
        (static_cast<std::uint64_t>(members[2]) << 32) |
        (std::uint64_t{1} << 48), std::memory_order_release);
}
void ArmTuple(const std::array<std::uint16_t, 3>& members, unsigned missingIndex) {
    bool valid = missingIndex == 1 || missingIndex == 2;
    for (unsigned i = 0; i < 3; ++i)
        valid = valid && (i == missingIndex ? members[i] == 0 : members[i] != 0);
    if (!valid) return; // invalid replacement cannot retire an owned zero
    g_owned.store(static_cast<std::uint64_t>(members[0]) |
        (static_cast<std::uint64_t>(members[1]) << 16) |
        (static_cast<std::uint64_t>(members[2]) << 32) |
        (static_cast<std::uint64_t>(missingIndex + 1) << 48), std::memory_order_release);
}
bool Shutdown(const std::array<std::uint16_t, 3>* restoredNative) {
    // Caller must restore physical members first. Do not retire these hooks as
    // a side effect of a runtime death while the installed member is still zero.
    const auto owner = g_owned.load(std::memory_order_acquire);
    if (owner) {
        const auto tag = static_cast<unsigned>(owner >> 48);
        if (tag < 1 || tag > 3) return false;
        if (tag == 1) {
            // A changed or unreadable tuple is not evidence of native restoration.
            // PartyNative supplies its original tuple only after restoration and
            // the post-read complete normally; verify all three again here.
            std::array<std::uint16_t, 3> current {};
            if (!restoredNative ||
                !Read(g_base + playerkit::RVA_RESOLVED_MEMBERS, current.data(), sizeof(current)) ||
                current != *restoredNative) return false;
            const std::array<std::uint16_t, 3> owned {
                static_cast<std::uint16_t>(owner), static_cast<std::uint16_t>(owner >> 16),
                static_cast<std::uint16_t>(owner >> 32)};
            if (current == owned) return false;
        } else {
            std::uint16_t member = 0;
            if (!Read(g_base + playerkit::RVA_RESOLVED_MEMBERS + 2u * (tag - 1), &member, sizeof(member)) || member == 0) return false;
        }
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
