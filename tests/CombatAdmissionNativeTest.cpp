// VUH-2897: the combat-admission adapter (inject/src/CombatAdmissionNative.hpp) over an
// owned executable image. The real pinned teardown bytes (xor ecx,ecx; call 3BF2E0; mov
// ecx,1; call 3BF2E0) run here, so the detour sees the true 3ACC03/3ACC0D return addresses,
// and the real 3BF2C0 setter bytes write the fake CONTROLLABLE word. MinHook is stubbed: a
// jmp at 3BF2E0 stands in for the detour and the "original" is a recording fake.
#include <Windows.h>
#include <MinHook.h>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>
#include "../inject/src/CombatAdmissionNative.hpp"

namespace ca = kh2coop::inject::combatadmission;
namespace cd = ca::detail;
namespace trace = kh2coop::inject::axeltrace;
namespace off = kh2coop::offsets;

static unsigned checks = 0, failures = 0;
static void Check(bool pass, const char* what) {
    ++checks; if (!pass) { ++failures; std::printf("FAIL %s\n", what); }
}
static std::vector<std::string> output;
static void Log(const char* fmt, ...) {
    char line[4096]; va_list args; va_start(args, fmt);
    vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, args); va_end(args);
    output.emplace_back(line);
    SetLastError(999); // the adapter must preserve the native caller's value
}
static bool Has(const char* text) {
    for (const auto& row : output) if (row.find(text) != std::string::npos) return true;
    return false;
}
static std::vector<void*> hooked;
static void __fastcall FakeUnfreeze(std::uintptr_t);
MH_STATUS WINAPI MH_CreateHook(LPVOID target, LPVOID, LPVOID* original) {
    hooked.push_back(target);
    *original = reinterpret_cast<LPVOID>(&FakeUnfreeze);
    return MH_OK;
}
MH_STATUS WINAPI MH_EnableHook(LPVOID) { return MH_OK; }
MH_STATUS WINAPI MH_DisableHook(LPVOID) { return MH_OK; }
MH_STATUS WINAPI MH_RemoveHook(LPVOID) { return MH_OK; }

static unsigned char* image = nullptr;
static volatile std::uint32_t* mask = nullptr;
struct Call { std::uintptr_t group; DWORD tid; };
static std::vector<Call> natives;
static void __fastcall FakeUnfreeze(std::uintptr_t group) { // the original 3BF2E0's effect
    *mask &= ~(1u << (group & 31));
    natives.push_back({group, GetCurrentThreadId()});
    SetLastError(123);
}
static DWORD ownerTid = 0;
static bool OwnerThread() { return GetCurrentThreadId() == ownerTid; }
static std::uint32_t load = 9, transition = 12;
static bool pendingTransition = false;
static bool Identity(std::uint32_t& l, std::uint32_t& t, bool& pending) {
    l = load; t = transition; pending = pendingTransition; return true;
}
static ca::SessionView session;
static bool Session(ca::SessionView& s) { s = session; return true; }
static std::uint64_t now = 1000;
static std::uint64_t Clock() noexcept { return now; }

static void SetNow(const ca::Destination& d) {
    image[off::NOW] = static_cast<unsigned char>(d.world); image[off::NOW + 1] = static_cast<unsigned char>(d.room);
    image[off::NOW + 2] = static_cast<unsigned char>(d.door);
    *reinterpret_cast<std::uint16_t*>(image + off::NOW + 4) = d.map;
    *reinterpret_cast<std::uint16_t*>(image + off::NOW + 6) = d.battle;
    *reinterpret_cast<std::uint16_t*>(image + off::NOW + 8) = d.event;
}
static void SetEventContext(std::uintptr_t context) { *reinterpret_cast<std::uintptr_t*>(image + off::EVENT_CONTEXT) = context; }
// An installed event: context+0x48 -> descriptor, flag byte +2 bit 3 set = install spared 0/1.
struct FakeEvent {
    alignas(8) unsigned char context[0x50] {};
    unsigned char descriptor[4] {};
    explicit FakeEvent(bool sparesParty) {
        const auto d = reinterpret_cast<std::uintptr_t>(descriptor);
        std::memcpy(context + 0x48, &d, sizeof(d));
        descriptor[2] = sparesParty ? 0x08 : 0x00;
    }
    std::uintptr_t Address() const { return reinterpret_cast<std::uintptr_t>(context); }
};
static void RunTeardown() { reinterpret_cast<void(*)()>(image + 0x3ACBF8)(); }
// One armed Mansion -> Axel load, the intro's teardown, then its context clears.
static void HoldOnce(std::uint32_t preMask = 3) {
    ++transition; ++load;
    ca::NoteHostRequest({2, 0x13, 0, 134, 134, 134}, ca::AxelTarget, transition);
    ca::NoteLoad(load, transition);
    SetNow(ca::AxelTarget); SetEventContext(0x7000); *mask = preMask;
    natives.clear();
    RunTeardown();
    SetEventContext(0);
}
static std::vector<std::uintptr_t> Groups() {
    std::vector<std::uintptr_t> g; for (const auto& c : natives) g.push_back(c.group); return g;
}

int main() {
    image = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x2B00000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!image) return 2;
    const auto base = reinterpret_cast<std::uintptr_t>(image);
    mask = reinterpret_cast<volatile std::uint32_t*>(image + off::CONTROLLABLE);
    const unsigned char unfreeze[] = {0x8B,0xC1,0x48,0x8D,0x15,0xFF,0x7E,0x65,0x02,0x48,0xC1,0xE8,0x05,0x83,0xE1,0x1F};
    const unsigned char freeze[] = {0x8B,0xC1,0x48,0x8D,0x15,0x1F,0x7F,0x65,0x02,0x48,0xC1,0xE8,0x05,0x83,0xE1,0x1F,
                                    0x48,0x8D,0x14,0x82,0xB8,0x01,0x00,0x00,0x00,0xD3,0xE0,0x09,0x02,0xC3};
    // sub rsp,28h | pinned 3ACBFC..3ACC0C | add rsp,28h; ret
    const unsigned char teardown[] = {0x48,0x83,0xEC,0x28, 0x33,0xC9,0xE8,0xDD,0x26,0x01,0x00,0xB9,0x01,0x00,0x00,0x00,
                                      0xE8,0xD3,0x26,0x01,0x00, 0x48,0x83,0xC4,0x28,0xC3};
    std::memcpy(image + 0x3BF2E0, unfreeze, sizeof(unfreeze));
    std::memcpy(image + 0x3BF2C0, freeze, sizeof(freeze));
    std::memcpy(image + 0x3ACBF8, teardown, sizeof(teardown));
    image[off::OPEN_MENU] = 0xFF; image[off::IN_FIELD] = 1;
    ownerTid = GetCurrentThreadId();
    ca::Providers providers {&OwnerThread, &Identity, &Session};

    image[0x3BF2C0 + 3] ^= 1;
    ca::Install(base, Log, providers);
    Check(!ca::Installed() && hooked.empty() && Has("REFUSED bytes"), "a mismatched setter pin refuses the barrier");
    image[0x3BF2C0 + 3] ^= 1;
    image[0x3ACBFC + 8] ^= 1;
    ca::Install(base, Log, providers);
    Check(!ca::Installed() && hooked.empty(), "a mismatched teardown call-site pin refuses the barrier");
    image[0x3ACBFC + 8] ^= 1;
    ca::Install(base, Log, {&OwnerThread, nullptr, &Session});
    Check(!ca::Installed() && hooked.empty(), "missing providers refuse the barrier");
    SetEnvironmentVariableA("KH2COOP_COMBAT_ADMISSION", nullptr);
    ca::Install(base, Log, providers);
    Check(ca::Installed() && hooked.size() == 1 && hooked[0] == image + 0x3BF2E0 && Has("configured=1 enabled=1"),
          "matching pins install one detour, on 3BF2E0, enabled by default");
    // Stand-in for MinHook's patch: mov rax, detour; jmp rax.
    image[0x3BF2E0] = 0x48; image[0x3BF2E1] = 0xB8;
    const auto detour = reinterpret_cast<std::uintptr_t>(&cd::Unfreeze);
    std::memcpy(image + 0x3BF2E2, &detour, sizeof(detour));
    image[0x3BF2EA] = 0xFF; image[0x3BF2EB] = 0xE0;
    cd::clock = &Clock;
    session.host = true; session.generation = 4; session.hostDelivery = 70; session.publicationPending = true;
    session.connection[1] = 101; session.delivery[1] = 201;

    // Not armed: both releases forward, in order, preserving LastError.
    SetNow(ca::AxelTarget); SetEventContext(0x7000); *mask = 3; natives.clear();
    SetLastError(55); RunTeardown();
    Check(Groups() == std::vector<std::uintptr_t>{0, 1} && *mask == 0 && !ca::Live() && GetLastError() == 123,
          "unarmed teardown forwards 0 then 1 unchanged");

    // Armed: both deferred, BOSS/ZAKO frozen by the real setter, nothing forwarded.
    output.clear(); SetLastError(66);
    HoldOnce();
    Check(natives.empty() && *mask == 0b11011 && ca::Live() && Has("deferred group=0") && Has("deferred group=1"),
          "armed teardown defers 0/1 and freezes 3/4 through 3BF2C0");
    Check(GetLastError() == 66, "a deferral preserves the native LastError");
    Check(cd::machine.Current().owned == 0b11000, "lease owns exactly the 3/4 bits it set");
    Check(ca::PublicationHoldQualifies() && ca::PublicationChallenge() != 0, "scoped publication proof after the intro");
    const auto challenge = ca::PublicationChallenge();
    ca::NotePublished(5, challenge);
    Check(!ca::PublicationHoldQualifies(), "the relaxation closes after one publication");
    Check(ca::Ack(1, 101, 201, 5, ca::AxelTarget, challenge ^ 1) == ca::AckResult::Challenge && natives.empty(),
          "a wrong challenge releases nothing");
    {   std::thread other([&] { (void)ca::Ack(1, 101, 201, 5, ca::AxelTarget, challenge); ca::Service(); });
        other.join();
        Check(natives.empty() && cd::machine.Held(), "another thread can neither ACK nor service the lease");
    }
    output.clear(); SetLastError(77);
    Check(ca::Ack(1, 101, 201, 5, ca::AxelTarget, challenge) == ca::AckResult::Accepted, "valid ACK accepted");
    Check(Groups() == std::vector<std::uintptr_t>{0, 1, 3, 4} && *mask == 0 && !ca::Live() && Has("released admitted"),
          "the last ACK releases 0, 1, then 3, 4 on the same owner pass");
    Check(GetLastError() == 77, "service preserves the caller's LastError");
    for (const auto& c : natives) if (c.tid != ownerTid) Check(false, "every release runs on the owner thread");
    natives.clear(); ca::Service();
    Check(natives.empty(), "released exactly once");

    // Deadline: 15 s from the first deferral, released on the first owner service at expiry.
    output.clear();
    HoldOnce();
    ca::NotePublished(6, ca::PublicationChallenge());
    now += ca::DeadlineMs - 1; ca::Service();
    Check(natives.empty() && ca::Live(), "no release at 14.999 s");
    now += 1; ca::Service();
    Check(Groups() == std::vector<std::uintptr_t>{0, 1, 3, 4} && Has("outcome=deadline failed=1") &&
          Has("released failed-barrier native-fallback") && Has("missingSeats=2"),
          "deadline: named failed barrier, missing seat, one ordered release at expiry");

    // Event installed while releasing: nothing (3/4 included) is touched until it clears.
    output.clear();
    HoldOnce();
    session.hostDowned = true; SetEventContext(0x9000); ca::Service();
    Check(natives.empty() && *mask == 0b11011 && Has("release-waiting-event"), "a new event blocks every clear");
    SetEventContext(0); ca::Service(); session.hostDowned = false;
    Check(Groups() == std::vector<std::uintptr_t>{0, 1, 3, 4} && Has("outcome=host-downed"),
          "host downing releases through the one path once the event clears");
    // (0x9000 is unmapped: its descriptor read faults under SEH and counts as freezing.)

    // An event whose descriptor spared the party never froze 0/1: the deadline is hard.
    {   FakeEvent sparing(true);
        output.clear();
        HoldOnce();
        SetEventContext(sparing.Address());
        now += ca::DeadlineMs; ca::Service();
        Check(Groups() == std::vector<std::uintptr_t>{0, 1, 3, 4} && *mask == 0 && Has("outcome=deadline") &&
              Has("release-via event-spares-party") && !ca::Live(),
              "a sparing event cannot extend the hold: deadline releases 0, 1, 3, 4 through it");
        SetEventContext(0);
    }
    // An event that froze 0/1 itself keeps the release waiting past the deadline until it ends.
    {   FakeEvent freezing(false);
        output.clear();
        HoldOnce();
        SetEventContext(freezing.Address());
        now += ca::DeadlineMs; ca::Service();
        now += 60000; ca::Service();
        Check(natives.empty() && *mask == 0b11011 && Has("outcome=deadline") && Has("release-waiting-event") && ca::Live(),
              "a freezing event holds every clear past the deadline");
        *mask &= ~3u; SetEventContext(0); ca::Service(); // its teardown cleared 0/1 natively
        Check(Groups() == std::vector<std::uintptr_t>{3, 4} && *mask == 0 && !Has("release-via"),
              "after the freezing event ends, only the lease's own 3/4 are released");
    }

    // A BOSS bit the game set before the lease armed survives release.
    HoldOnce(0b01011);
    Check(cd::machine.Current().owned == 0b10000 && *mask == 0b11011, "only the free ZAKO bit is owned");
    session.hostDowned = true; ca::Service(); session.hostDowned = false;
    Check(Groups() == std::vector<std::uintptr_t>{0, 1, 4} && *mask == 0b01000,
          "a downing releases; the game's own BOSS bit stays set");

    // A peer downing in this lease's own published room releases; one from an earlier epoch never does.
    output.clear();
    HoldOnce();
    ca::NotePublished(9, ca::PublicationChallenge());
    session.peerDownedEpoch[0] = 8; ca::Service();
    Check(natives.empty() && ca::Live(), "a peer still downed in the previous room's epoch does not end the hold");
    session.peerDownedEpoch[0] = 9; ca::Service(); session.peerDownedEpoch[0] = 0;
    Check(Groups() == std::vector<std::uintptr_t>{0, 1, 3, 4} && Has("outcome=peer-downed"),
          "a peer downed in this lease's room releases through the one path");

    // Continue / new request while held: released before the request is noted.
    output.clear();
    HoldOnce();
    ca::NoteHostRequest(ca::AxelTarget, ca::AxelTarget, transition + 1);
    Check(Groups() == std::vector<std::uintptr_t>{0, 1, 3, 4} && Has("decided incarnation") &&
          Has("outcome=native-transition") && !cd::machine.Armed(),
          "a native request releases while the old identity matches, and Axel->Axel never re-arms");

    // As warp issues it: BeginTransition (serial+1, pending) runs before NoteHostRequest.
    output.clear();
    HoldOnce();
    ++transition; pendingTransition = true;
    ca::NoteHostRequest(ca::AxelTarget, ca::AxelTarget, transition);
    Check(Groups() == std::vector<std::uintptr_t>{0, 1, 3, 4} && !ca::Live(),
          "the request's own pass releases although its transition is already pending");
    pendingTransition = false;

    // VUH-2906: host downed, game over, Continue lands at the 02/15 checkpoint (logged, never armed);
    // the route back's Mansion -> Axel request arms a fresh lease and challenge.
    {   output.clear();
        HoldOnce();
        const auto firstChallenge = ca::PublicationChallenge();
        const auto firstIncarnation = cd::machine.Current().incarnation;
        ca::NotePublished(7, firstChallenge);
        session.hostDowned = true; ca::Service(); session.hostDowned = false;
        Check(Groups() == std::vector<std::uintptr_t>{0, 1, 3, 4} && !ca::Live() && Has("outcome=host-downed"),
              "setup: the host's downing released the first lease");
        constexpr ca::Destination checkpoint {2, 0x15, 99, 2, 0, 0};
        ++transition; pendingTransition = true; natives.clear();
        ca::NoteHostRequest(ca::AxelTarget, checkpoint, transition, true);
        Check(Has("continue-retry from=02/14 btl=137 to=02/15 door=99 map=2 btl=0 evt=0") && Has("armed=0") &&
              !cd::machine.Requested(), "the Continue to the checkpoint is logged with its destination and never arms");
        ++load; ca::NoteLoad(load, transition); pendingTransition = false;
        Check(natives.empty() && !ca::Live(), "the checkpoint load starts no lease");
        output.clear();
        HoldOnce();
        const auto retryChallenge = ca::PublicationChallenge();
        Check(natives.empty() && *mask == 0b11011 && cd::machine.Current().incarnation == firstIncarnation + 1 &&
              retryChallenge && retryChallenge != firstChallenge && !Has("continue-retry"),
              "the route back's intro is held again, 3/4 owned, under a new incarnation and challenge");
        ca::NotePublished(8, retryChallenge);
        Check(ca::Ack(1, 101, 201, 7, ca::AxelTarget, firstChallenge) == ca::AckResult::Challenge && natives.empty(),
              "the first lease's ACK cannot release the route back");
        Check(ca::Ack(1, 101, 201, 8, ca::AxelTarget, retryChallenge) == ca::AckResult::Accepted &&
              Groups() == std::vector<std::uintptr_t>{0, 1, 3, 4} && *mask == 0 && !ca::Live(),
              "the route back's own ACK releases 0, 1, 3, 4");
        // Without the game-over task, an Axel -> Axel request neither arms nor logs.
        output.clear();
        ca::NoteHostRequest(ca::AxelTarget, ca::AxelTarget, transition + 1);
        Check(!cd::machine.Requested() && !Has("continue-retry"), "an ordinary Axel -> Axel request neither arms nor logs");
    }

    // Reload landed first: retired, nothing replayed into the new room.
    output.clear();
    HoldOnce();
    ++load; ca::NoteLoad(load, transition);
    Check(natives.empty() && !ca::Live() && Has("retired-without-release") && Has("outcome=load-changed"),
          "a new load retires the lease without touching the new room (S2)");

    // A teardown on another thread is never deferred.
    ++transition; ++load;
    ca::NoteHostRequest({2, 0x13, 0, 134, 134, 134}, ca::AxelTarget, transition);
    ca::NoteLoad(load, transition);
    SetNow(ca::AxelTarget); SetEventContext(0x7000); *mask = 3; natives.clear();
    {   std::thread other(RunTeardown); other.join(); }
    Check(Groups() == std::vector<std::uintptr_t>{0, 1} && !ca::Live(), "an off-thread teardown forwards");
    SetEventContext(0);

    // Solo never arms.
    session.connection[1] = 0;
    HoldOnce();
    Check(Groups() == std::vector<std::uintptr_t>{0, 1} && !ca::Live(), "solo forwards natively");
    session.connection[1] = 101;

    // Shutdown from an unloader thread: it signals; the owner's service releases.
    output.clear();
    HoldOnce();
    {   std::thread unloader([] { ca::Shutdown(5000); });
        for (unsigned i = 0; i < 2000 && ca::Live(); ++i) { ca::Service(); Sleep(1); }
        unloader.join();
    }
    Check(Groups() == std::vector<std::uintptr_t>{0, 1, 3, 4} && Has("outcome=shutdown") && !ca::Installed(),
          "off-thread shutdown is released by the owner thread, then the barrier stands down");
    for (const auto& c : natives) if (c.tid != ownerTid) Check(false, "shutdown never clears off the owner thread");
    natives.clear(); *mask = 3; SetEventContext(0x7000); RunTeardown(); SetEventContext(0);
    Check(Groups() == std::vector<std::uintptr_t>{0, 1}, "after shutdown the detour only forwards");

    // The trace installs its three hooks and never a second one on 3BF2E0.
    {   hooked.clear();
        const unsigned char dispatch[] = {0x45,0x33,0xC0,0x33,0xD2,0xE9,0x36,0x01,0,0};
        const unsigned char intro[] = {0x40,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xF9,0x48,0x8B,0x0D,0x68,0x54,0x66,0x02};
        const unsigned char loadTask[] = {0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x50};
        std::memcpy(image + 0x14FC20, dispatch, sizeof(dispatch));
        std::memcpy(image + 0x3AC000, intro, sizeof(intro));
        std::memcpy(image + 0x152680, loadTask, sizeof(loadTask));
        SetEnvironmentVariableA("KH2COOP_AXEL_BOUNDARY_TRACE", "1");
        trace::Install(base, Log);
        bool ownsUnfreeze = false;
        for (auto* t : hooked) if (t == image + 0x3BF2E0) ownsUnfreeze = true;
        Check(trace::Enabled() && hooked.size() == 3 && !ownsUnfreeze, "trace and barrier coexist: one 3BF2E0 detour");
        trace::Shutdown();
    }
    VirtualFree(image, 0, MEM_RELEASE);
    std::printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
