// VUH-1489 clone neutral input (inject/src/CloneNeutralInput.inl) against fake game memory.
// Headless: no game process, no real hooks (MinHook, warp and playerkit are stand-ins).
// Runs the module with KH2COOP_CLONE_NEUTRAL_INPUT "0", unset (off) and "1" (on) in one process:
// install refusals, the +0xDB8 Gate, the 0x3A89A0 movement-update detour (FIELD_COMMAND command
// record and stick neutralised and restored, also on exception and around a nested non-gated call)
// and every pass-through case. The private mutant suite lives in the VUH-1489 rig lane.
#include <Windows.h>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "kh2coop/KH2Offsets.hpp"

// MinHook stand-ins (the real header is included by EntityHook.cpp).
enum MH_STATUS { MH_OK = 0, MH_ERROR_NOT_EXECUTABLE = 9 };
static int g_mhCreate = 0, g_mhEnable = 0;
static MH_STATUS g_mhCreateResult = MH_OK;
static void* g_mhTarget = nullptr;
static void* g_mhDetour = nullptr;
static void** g_mhOriginal = nullptr;
static MH_STATUS MH_CreateHook(void* target, void* detour, void** original) {
    ++g_mhCreate; g_mhTarget = target; g_mhDetour = detour; g_mhOriginal = original; return g_mhCreateResult;
}
static MH_STATUS MH_EnableHook(void*) { ++g_mhEnable; return MH_OK; }

namespace kh2coop {
namespace inject {
namespace offsets = kh2coop::offsets;
static uintptr_t g_exeBase = 0;
static std::uint32_t g_frameCounter = 0;
static uintptr_t g_clonesNow[2] = {0, 0};
static int g_logLines = 0;
static void Log(const char* fmt, ...) {
    ++g_logLines;
    va_list a; va_start(a, fmt); std::vprintf(fmt, a); va_end(a); std::printf("\n");
}
static bool IsPlayerClassActor(uintptr_t actor) {
    const auto obj = *reinterpret_cast<const uintptr_t*>(actor + offsets::actor::OBJENTRY_PTR);
    if (obj <= g_exeBase || obj >= g_exeBase + 0x3000000) return false;
    return *reinterpret_cast<const uint8_t*>(obj + offsets::objentry::TYPE_FLAGS) == 0;
}
namespace warp {
static bool g_pending = false;
static bool TransitionPending() { return g_pending; }
}  // namespace warp
namespace playerkit {
static bool g_blocks = false;
static bool BlocksNativeSoraPuppets() { return g_blocks; }
}  // namespace playerkit
#include "CloneNeutralInput.inl"
}  // namespace inject
}  // namespace kh2coop

using namespace kh2coop::inject;
static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

static uintptr_t& Q(uintptr_t a) { return *reinterpret_cast<uintptr_t*>(a); }

// ---- fake original for the 0x3A89A0 detour ----
struct Seen { int calls; uintptr_t actor; std::uint16_t id; std::uint8_t flags; float stick[4]; std::uint32_t target[4]; };
static Seen g_seen {}, g_seenNested {};
static int g_origMode = 0;   // 0 record, 1 record + scribble FC, 2 record + raise, 3 record + nested call
static uintptr_t g_nestedActor = 0;
static void __fastcall FakeMovementUpdate(void* actor) {
    auto* fc = reinterpret_cast<std::uint8_t*>(g_exeBase + 0x2A10620);
    const bool nested = g_origMode == 3 && reinterpret_cast<uintptr_t>(actor) == g_nestedActor;
    Seen& s = nested ? g_seenNested : g_seen;
    ++s.calls; s.actor = reinterpret_cast<uintptr_t>(actor);
    s.id = *reinterpret_cast<std::uint16_t*>(fc + 8); s.flags = fc[0xC];
    std::memcpy(s.stick, fc + 0xB50, 16); std::memcpy(s.target, fc + 0x10, 16);
    if (nested) { *reinterpret_cast<std::uint16_t*>(fc + 8) = 0x4444; return; }   // the nested actor consumes/rewrites
    if (g_origMode == 3) {
        cloneneutral::HookedMovementUpdate(reinterpret_cast<void*>(g_nestedActor));
        // Back in the clone's window: neutral again.
        g_seen.id = *reinterpret_cast<std::uint16_t*>(fc + 8); g_seen.flags = fc[0xC];
        std::memcpy(g_seen.stick, fc + 0xB50, 16); std::memcpy(g_seen.target, fc + 0x10, 16);
        return;
    }
    if (g_origMode >= 1) { std::memset(fc + 0xB50, 0x5A, 16); *reinterpret_cast<std::uint16_t*>(fc + 8) = 0x7777; fc[0xC] = 0x1F; }
    if (g_origMode == 2) RaiseException(0xE0001489, 0, 0, nullptr);
}
static bool CallDetourExpectingException(uintptr_t actor) {
    __try {
        cloneneutral::HookedMovementUpdate(reinterpret_cast<void*>(actor));
    } __except (GetExceptionCode() == 0xE0001489 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return true;
    }
    return false;
}

static std::uint8_t g_heap[0x1000 * 12];
static std::uint8_t g_fcSnapshot[0x1000];

static int Run(bool on) {
    std::memset(g_heap, 0, sizeof(g_heap));
    auto* image = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 0x3000000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!image) return 2;
    g_exeBase = reinterpret_cast<uintptr_t>(image);
    // A PE header and the pinned prologue, as in exe 9002b2de.
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image);
    dos->e_magic = IMAGE_DOS_SIGNATURE; dos->e_lfanew = 0x100;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(image + 0x100);
    nt->Signature = IMAGE_NT_SIGNATURE; nt->FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt->FileHeader.TimeDateStamp = 0x669E384A; nt->OptionalHeader.SizeOfImage = 0x2C2B000;
    std::memcpy(image + 0x3A89A0, cloneneutral::MOVEMENT_PROLOGUE, sizeof(cloneneutral::MOVEMENT_PROLOGUE));
    const uintptr_t fc = g_exeBase + 0x2A10620;
    for (int i = 0; i < 0x1000; ++i) image[0x2A10620 + i] = static_cast<std::uint8_t>(i * 7 + 3);
    *reinterpret_cast<std::uint16_t*>(fc + 8) = 0x0123; image[0x2A10620 + 0xC] = 0x3F;   // a pending command
    const float stickIn[4] = {0.5f, 0.0f, 0.25f, 1.0f}; std::memcpy(image + 0x2A10620 + 0xB50, stickIn, 16);
    std::memcpy(g_fcSnapshot, image + 0x2A10620, sizeof(g_fcSnapshot));

    const uintptr_t entry0 = g_exeBase + offsets::input::PROCESSED_ENTRY0;
    for (int i = 0; i < 0x68; ++i) image[offsets::input::PROCESSED_ENTRY0 + i] = static_cast<std::uint8_t>(0xA0 + i);
    std::uint8_t entrySnapshot[0x68]; std::memcpy(entrySnapshot, image + offsets::input::PROCESSED_ENTRY0, 0x68);

    auto descriptor = [&](uintptr_t rva, std::uint32_t id, std::uint8_t type, const char* name) {
        const uintptr_t d = g_exeBase + rva;
        *reinterpret_cast<std::uint32_t*>(d) = id; image[rva + 4] = type;
        std::memcpy(image + rva + 8, name, std::strlen(name) + 1);
        return d;
    };
    const uintptr_t sora = descriptor(0x1000000, 84, 0, "P_EX100");
    const uintptr_t other = descriptor(0x1000100, 85, 0, "P_EX110");
    const uintptr_t donald = descriptor(0x1000200, 0x5A, 1, "P_EX020");
    const uintptr_t fakeName = descriptor(0x1000300, 84, 0, "P_EX10X");

    auto actor = [&](int i, uintptr_t desc, uintptr_t pad) {
        const uintptr_t a = reinterpret_cast<uintptr_t>(g_heap) + 0x1000 * i;
        Q(a + offsets::actor::OBJENTRY_PTR) = desc; Q(a + 0xDB8) = pad; Q(a + 0xDD0) = fc; return a;
    };
    const uintptr_t foreign = 0x1234560;
    const uintptr_t head = actor(0, sora, entry0);
    const uintptr_t clone = actor(1, sora, entry0);
    const uintptr_t friendA = actor(2, donald, entry0);
    const uintptr_t otherPlayer = actor(3, other, entry0);
    const uintptr_t oddPointer = actor(4, sora, foreign);
    const uintptr_t building = actor(5, sora, entry0);   // the player pointer, not head
    const uintptr_t badName = actor(6, fakeName, entry0);
    const uintptr_t clone2 = actor(7, sora, entry0);
    const uintptr_t transClone = actor(8, sora, entry0);
    Q(g_exeBase + offsets::active_entity_list::HEAD) = head;
    Q(g_exeBase + 0x2A105D0) = building;
    g_clonesNow[0] = clone;
    auto fcIntact = [&] { return std::memcmp(image + 0x2A10620, g_fcSnapshot, sizeof(g_fcSnapshot)) == 0; };

    if (!on) {
        cloneneutral::Install(g_exeBase);
        CHECK(!cloneneutral::Enabled() && g_mhCreate == 0 && cloneneutral::g_neutral == nullptr && g_logLines == 0);
        for (uintptr_t a : {head, clone, friendA, otherPlayer, oddPointer, building, badName, clone2}) cloneneutral::Gate(a);
        for (uintptr_t a : {head, clone, friendA, otherPlayer, building, badName, clone2}) CHECK(Q(a + 0xDB8) == entry0);
        CHECK(Q(oddPointer + 0xDB8) == foreign);
        // Even if the detour were reached, it passes straight through.
        cloneneutral::g_origMovementUpdate = &FakeMovementUpdate;
        cloneneutral::HookedMovementUpdate(reinterpret_cast<void*>(clone));
        CHECK(g_seen.calls == 1 && g_seen.id == 0x0123 && g_seen.flags == 0x3F && g_seen.stick[0] == 0.5f);
        CHECK(g_logLines == 0 && fcIntact());
    } else {
        // Install refusals leave everything off.
        playerkit::g_blocks = true;
        cloneneutral::Install(g_exeBase);
        CHECK(!cloneneutral::Enabled() && g_mhCreate == 0);
        playerkit::g_blocks = false;
        image[0x3A89A0 + 20] ^= 0xFF;
        cloneneutral::Install(g_exeBase);
        CHECK(!cloneneutral::Enabled() && g_mhCreate == 0);
        image[0x3A89A0 + 20] ^= 0xFF;
        nt->FileHeader.TimeDateStamp ^= 1;
        cloneneutral::Install(g_exeBase);
        CHECK(!cloneneutral::Enabled() && g_mhCreate == 0);
        nt->FileHeader.TimeDateStamp ^= 1;
        g_mhCreateResult = MH_ERROR_NOT_EXECUTABLE;
        cloneneutral::Install(g_exeBase);
        CHECK(!cloneneutral::Enabled() && g_mhCreate == 1 && g_mhEnable == 0);
        // Gate is inert while off.
        cloneneutral::Gate(clone);
        CHECK(Q(clone + 0xDB8) == entry0);
        g_mhCreateResult = MH_OK;
        cloneneutral::Install(g_exeBase);
        CHECK(cloneneutral::Enabled() && g_mhCreate == 2 && g_mhEnable == 1);
        CHECK(g_mhTarget == reinterpret_cast<void*>(g_exeBase + 0x3A89A0));
        CHECK(g_mhDetour == reinterpret_cast<void*>(&cloneneutral::HookedMovementUpdate));
        CHECK(cloneneutral::g_stats->hookInstalled == 1 && cloneneutral::g_stats->magic == 0x33494E43);
        CHECK(reinterpret_cast<std::uint8_t*>(cloneneutral::g_stats) == cloneneutral::g_neutral + 0x100);
        *g_mhOriginal = reinterpret_cast<void*>(&FakeMovementUpdate);

        for (uintptr_t a : {head, clone, friendA, otherPlayer, oddPointer, building, badName, clone2}) cloneneutral::Gate(a);
        cloneneutral::Gate(0);
        const uintptr_t neutral = reinterpret_cast<uintptr_t>(cloneneutral::g_neutral);
        CHECK(Q(head + 0xDB8) == entry0 && Q(building + 0xDB8) == entry0);
        CHECK(Q(friendA + 0xDB8) == entry0 && Q(otherPlayer + 0xDB8) == entry0 && Q(badName + 0xDB8) == entry0);
        CHECK(Q(oddPointer + 0xDB8) == foreign);
        CHECK(Q(clone + 0xDB8) == neutral && Q(clone2 + 0xDB8) == neutral);
        CHECK(cloneneutral::g_stats->neutralized == 2 && cloneneutral::g_stats->refusedPointer == 1 &&
              cloneneutral::g_stats->restored == 0);
        std::uint8_t zero[0x40] {};
        CHECK(std::memcmp(cloneneutral::g_neutral, zero, 0x40) == 0);
        CHECK(std::memcmp(cloneneutral::g_neutral + 0x40, entrySnapshot + 0x40, 0x28) == 0);

        // ---- detour: a neutral clone runs with FC's command and stick neutralised, then restored ----
        g_seen = {}; g_origMode = 1;
        cloneneutral::HookedMovementUpdate(reinterpret_cast<void*>(clone));
        CHECK(g_seen.calls == 1 && g_seen.actor == clone);
        CHECK(g_seen.id == 0 && g_seen.flags == (0x3F & 0xE0));
        CHECK(g_seen.stick[0] == 0.0f && g_seen.stick[1] == 0.0f && g_seen.stick[2] == 0.0f && g_seen.stick[3] == 0.0f);
        CHECK(g_seen.target[0] == 0 && g_seen.target[1] == 0 && g_seen.target[2] == 0 && g_seen.target[3] == 0xFFFFFFFFu);
        CHECK(fcIntact());   // the original's scribble on stick/record is undone too
        CHECK(cloneneutral::g_stats->movementGated == 1 && cloneneutral::g_stats->commandSuppressed == 1);
        // No pending command: still gated, not counted as suppressed.
        *reinterpret_cast<std::uint16_t*>(fc + 8) = 0; g_fcSnapshot[8] = g_fcSnapshot[9] = 0;
        g_seen = {}; g_origMode = 0;
        cloneneutral::HookedMovementUpdate(reinterpret_cast<void*>(clone));
        CHECK(g_seen.calls == 1 && g_seen.stick[3] == 0.0f && fcIntact());
        CHECK(cloneneutral::g_stats->movementGated == 2 && cloneneutral::g_stats->commandSuppressed == 1);
        *reinterpret_cast<std::uint16_t*>(fc + 8) = 0x0123; g_fcSnapshot[8] = 0x23; g_fcSnapshot[9] = 0x01;
        // Nested non-gated call inside the clone's window: it sees the real values, its write is kept
        // as the window's restore value, the clone's window is neutral again afterwards.
        g_seen = {}; g_seenNested = {}; g_origMode = 3; g_nestedActor = head;
        cloneneutral::HookedMovementUpdate(reinterpret_cast<void*>(clone));
        CHECK(g_seenNested.calls == 1 && g_seenNested.id == 0x0123 && g_seenNested.flags == 0x3F && g_seenNested.stick[0] == 0.5f);
        CHECK(std::memcmp(g_seenNested.target, image + 0x2A10620 + 0x10, 16) == 0 &&
              std::memcmp(g_seenNested.target, g_fcSnapshot + 0x10, 16) == 0);
        CHECK(g_seen.id == 0 && g_seen.stick[0] == 0.0f && g_seen.target[3] == 0xFFFFFFFFu);
        CHECK(*reinterpret_cast<std::uint16_t*>(fc + 8) == 0x4444);   // nested write survives the outer restore
        CHECK(cloneneutral::g_stats->nestedPassThrough == 1 && cloneneutral::g_open == nullptr);
        *reinterpret_cast<std::uint16_t*>(fc + 8) = 0x0123; g_origMode = 0;
        CHECK(fcIntact());
        // Exception inside the original: restored by __finally, exception still propagates.
        g_seen = {}; g_origMode = 2;
        CHECK(CallDetourExpectingException(clone));
        CHECK(g_seen.calls == 1 && g_seen.id == 0 && fcIntact() && cloneneutral::g_open == nullptr);
        g_origMode = 0;
        // Pass-through cases: original sees the real FC values and nothing is counted.
        const auto gated = cloneneutral::g_stats->movementGated;
        auto passes = [&](uintptr_t a) {
            g_seen = {};
            cloneneutral::HookedMovementUpdate(reinterpret_cast<void*>(a));
            return g_seen.calls == 1 && g_seen.id == 0x0123 && g_seen.flags == 0x3F && g_seen.stick[0] == 0.5f &&
                g_seen.stick[3] == 1.0f && fcIntact();
        };
        CHECK(passes(head));                       // canonical, pad entry 0
        CHECK(passes(friendA));                    // not player-class
        CHECK(passes(otherPlayer));                // player-class, pad entry 0
        Q(friendA + 0xDB8) = neutral;              // non-player with a neutral pad
        CHECK(passes(friendA));
        Q(friendA + 0xDB8) = entry0;
        Q(clone2 + 0xDD0) = fc + 0x10;             // neutral pad but not FIELD_COMMAND
        CHECK(passes(clone2));
        Q(clone2 + 0xDD0) = fc;
        Q(g_exeBase + 0x2A105D0) = clone2;         // neutral pad but it is the player pointer
        CHECK(passes(clone2));
        Q(g_exeBase + offsets::active_entity_list::HEAD) = clone2;   // ... or the head
        Q(g_exeBase + 0x2A105D0) = building;
        CHECK(passes(clone2));
        Q(g_exeBase + offsets::active_entity_list::HEAD) = head;
        CHECK(cloneneutral::g_stats->movementGated == gated);
        CHECK(passes(0) || g_seen.calls == 1);     // null actor passes through

        // Sticky + re-zero: a consumer dirties the shared neutral state.
        cloneneutral::g_neutral[0] = 0xFF; cloneneutral::g_neutral[0x3C] = 0x7F;
        g_clonesNow[0] = 0;
        cloneneutral::Gate(clone);
        CHECK(Q(clone + 0xDB8) == neutral && std::memcmp(cloneneutral::g_neutral, zero, 0x40) == 0);

        // A neutral actor that becomes canonical gets entry 0 back (head, then player pointer).
        Q(g_exeBase + offsets::active_entity_list::HEAD) = clone;
        cloneneutral::Gate(clone);
        CHECK(Q(clone + 0xDB8) == entry0);
        Q(g_exeBase + offsets::active_entity_list::HEAD) = head;
        Q(g_exeBase + 0x2A105D0) = clone2;
        cloneneutral::Gate(clone2);
        CHECK(Q(clone2 + 0xDB8) == entry0);
        CHECK(cloneneutral::g_stats->restored == 2);
        Q(g_exeBase + 0x2A105D0) = oddPointer;
        cloneneutral::Gate(oddPointer);
        CHECK(Q(oddPointer + 0xDB8) == foreign);
        Q(g_exeBase + 0x2A105D0) = building;

        // Transition: not selected while pending; sticky and restore still active; selected after.
        const auto neutralizedBefore = cloneneutral::g_stats->neutralized;
        warp::g_pending = true;
        cloneneutral::Gate(transClone);
        CHECK(Q(transClone + 0xDB8) == entry0 && cloneneutral::g_stats->transitionSkips == 1);
        Q(clone + 0xDB8) = neutral;
        const auto rz = cloneneutral::g_stats->rezero;
        cloneneutral::Gate(clone);
        CHECK(Q(clone + 0xDB8) == neutral && cloneneutral::g_stats->rezero == rz + 1);
        Q(g_exeBase + offsets::active_entity_list::HEAD) = clone;
        cloneneutral::Gate(clone);
        CHECK(Q(clone + 0xDB8) == entry0);
        Q(g_exeBase + offsets::active_entity_list::HEAD) = head;
        warp::g_pending = false;
        cloneneutral::Gate(transClone);
        CHECK(Q(transClone + 0xDB8) == neutral && cloneneutral::g_stats->neutralized == neutralizedBefore + 1);

        // Non-player actor with a neutral pad: never touched, even as head.
        Q(friendA + 0xDB8) = neutral;
        const auto rz2 = cloneneutral::g_stats->rezero; const auto rs2 = cloneneutral::g_stats->restored;
        cloneneutral::Gate(friendA);
        Q(g_exeBase + offsets::active_entity_list::HEAD) = friendA;
        cloneneutral::Gate(friendA);
        Q(g_exeBase + offsets::active_entity_list::HEAD) = head;
        CHECK(Q(friendA + 0xDB8) == neutral && cloneneutral::g_stats->rezero == rz2 && cloneneutral::g_stats->restored == rs2);
        Q(friendA + 0xDB8) = entry0;

        const auto rz3 = cloneneutral::g_stats->rezero;
        for (int i = 0; i < 5; ++i) cloneneutral::Gate(transClone);
        CHECK(cloneneutral::g_stats->rezero == rz3 + 5);

        // Log budget is bounded.
        const int before = g_logLines;
        for (int i = 0; i < 200; ++i) { Q(clone + 0xDB8) = entry0; cloneneutral::Gate(clone); }
        CHECK(g_logLines - before <= static_cast<int>(cloneneutral::LOG_BUDGET));
    }
    // The global processed entry 0 is never written, in either mode.
    CHECK(std::memcmp(image + offsets::input::PROCESSED_ENTRY0, entrySnapshot, 0x68) == 0);
    std::printf("gate controls (%s): %s\n", on ? "on" : "off", g_fail ? "FAIL" : "PASS");
    return g_fail ? 1 : 0;
}

int main() {
    // "0" is not "1": Install does nothing.
    SetEnvironmentVariableA("KH2COOP_CLONE_NEUTRAL_INPUT", "0");
    cloneneutral::Install(0x10000);
    CHECK(!cloneneutral::Enabled() && g_mhCreate == 0 && g_logLines == 0);
    SetEnvironmentVariableA("KH2COOP_CLONE_NEUTRAL_INPUT", nullptr);
    Run(false);
    SetEnvironmentVariableA("KH2COOP_CLONE_NEUTRAL_INPUT", "1");
    Run(true);
    std::printf("CloneNeutralInputTest: %s\n", g_fail ? "FAIL" : "PASS");
    return g_fail ? 1 : 0;
}
