// ============================================================================
// Warp — see Warp.hpp.
//
// The game requests every room change through one function (RVA 0x152990 on
// Steam Global; called by the room-script Jump handler at 0x3A46E0 and the
// world-map flow at 0x154E20):
//
//   void RequestTransition(const LocationPacket* to, uint32 fadeFlags,
//                          int mode, uint8 flag, int extra);
//
// It starts the room-load task and writes the packet into the NOW staging
// slot (0x717018), from which the load commits world/room/door/programs.
// Programs of 0xFFFF mean "use the save's per-room default", as the world
// map path (0x154E20) does. Calling it with (packet, 1, 0, 0, 0) mirrors that
// path. It must run on the game thread, so requests are handed over from the
// PerEntityUpdate hook at the start of a frame.
// ============================================================================

#include "Warp.hpp"
#include "EnemySync.hpp"
#include "ProgressSync.hpp"

#include "kh2coop/KH2Offsets.hpp"
#include "kh2coop/Protocol.hpp"
#include "kh2coop/WarpChannel.hpp"

#include <Windows.h>
#include <MinHook.h>

#include <cstdio>
#include <cstring>

namespace kh2coop {
namespace inject {
namespace warp {
namespace {

constexpr std::uint64_t RVA_REQUEST_TRANSITION = 0x152990;
constexpr std::uint64_t RVA_LOAD_COMPLETE = 0x152CD0;
constexpr std::uint64_t RVA_LOAD_COMPLETE_DIRECT = 0x152F40;
// First 24 bytes on Steam Global (includes a RIP-relative cmp, so they pin
// this exact build).
constexpr std::uint8_t kRequestTransitionBytes[] = {
    0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89,
    0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xec, 0x20, 0x80, 0x3d, 0x26, 0x7f,
};
constexpr std::uint8_t kLoadCompleteBytes[] = {
    0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x80, 0x3d, 0x2b, 0x43, 0x5c, 0x00,
    0x0f, 0x48, 0x8b, 0xd9, 0x75, 0x05, 0xe8, 0x59, 0xac, 0xfa, 0xff, 0xc6,
};
constexpr std::uint8_t kLoadCompleteDirectBytes[] = {
    0x48, 0x83, 0xec, 0x28, 0x80, 0x3d, 0xbd, 0x40, 0x5c, 0x00, 0x0f, 0x75,
    0x05, 0xe8, 0xee, 0xa9, 0xfa, 0xff, 0xc6, 0x05, 0x77, 0x79, 0x86, 0x00,
};


#pragma pack(push, 1)
struct LocationPacket {
    std::uint8_t world;
    std::uint8_t room;
    std::uint16_t door;
    std::uint16_t map;
    std::uint16_t battle;
    std::uint16_t event;
    std::uint8_t pad[6];
};
#pragma pack(pop)
static_assert(sizeof(LocationPacket) == 16, "packet is read as 8 bytes + a short");

using RequestTransitionFn = void(__fastcall*)(const LocationPacket*, std::uint32_t, int,
                                              std::uint8_t, int);

LogFn g_log = nullptr;
uintptr_t g_exeBase = 0;
RequestTransitionFn g_requestTransition = nullptr;
using LoadCompleteFn = void(__fastcall*)(void*);
using LoadCompleteDirectFn = void(__fastcall*)();
LoadCompleteFn g_loadComplete = nullptr;
LoadCompleteDirectFn g_loadCompleteDirect = nullptr;
bool g_ready = false;
bool g_clientAuthority = false;
bool g_transitionPending = false;
std::uint32_t g_transitionSerial = 0;
std::uint32_t g_loadSerial = 0;
std::uint32_t g_blockedNativeExits = 0;
RoomTransition g_hostTarget {};
bool g_hostQueued = false;
bool g_hostIssued = false;
std::uint32_t g_hostIssueLoad = 0;
HANDLE g_mapping = nullptr;
WarpChannel* g_channel = nullptr;

template <typename T>
T ReadExe(std::uint64_t rva) {
    T value {};
    std::memcpy(&value, reinterpret_cast<const void*>(g_exeBase + rva), sizeof(T));
    return value;
}

void Complete(WarpStatus status) {
    g_channel->status = static_cast<std::int32_t>(status);
    InterlockedExchange(&g_channel->doneSeq, g_channel->requestSeq);
}

// Safe-state gate (addresses from VUH-1486's static analysis, verified live):
// nothing frozen (events freeze entity groups), the room is live, no menu,
// no active event or timeline. The elapsed timer survives event completion.
// Loading never reaches here: the caller only
// runs inside entity updates.
bool SafeToWarp() {
    g_channel->controllable = ReadExe<std::int32_t>(offsets::CONTROLLABLE);
    g_channel->inField = ReadExe<std::uint8_t>(offsets::IN_FIELD);
    g_channel->openMenu = ReadExe<std::uint8_t>(offsets::OPEN_MENU);
    g_channel->cutsceneTimer = ReadExe<std::int32_t>(offsets::CUTSCENE_TIMER);
    g_channel->pauseStatus = ReadExe<std::int32_t>(offsets::PAUSE_STATUS);
    __try {
        return g_channel->controllable == 0 && g_channel->inField != 0 &&
               g_channel->openMenu == 0xFF &&
               *reinterpret_cast<const std::int32_t*>(g_exeBase + offsets::CUTSCENE_STATE) == 0 &&
               *reinterpret_cast<const uintptr_t*>(g_exeBase + offsets::EVENT_CONTEXT) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void BeginTransition() {
    ++g_transitionSerial;
    g_transitionPending = true;
}

void __fastcall HookedRequestTransition(const LocationPacket* to, std::uint32_t fade,
                                        int mode, std::uint8_t flag, int extra) {
    if (enemysync::HasClientAuthority()) {
        ++g_blockedNativeExits;
        if (g_log) g_log("[warp] client native exit blocked count=%u target=%02X/%02X door=%u map=%u btl=%u evt=%u",
                         g_blockedNativeExits, to->world, to->room, to->door, to->map,
                         to->battle, to->event);
        return;
    }
    BeginTransition();
    g_requestTransition(to, fade, mode, flag, extra);
}

void CompleteLoad() {
    ++g_loadSerial;
    g_transitionPending = false;
    const auto location = ReadLocation();
    if (g_log) g_log("[warp] load complete serial=%u transition=%u room=%02X/%02X door=%u map=%u btl=%u evt=%u",
                     g_loadSerial, g_transitionSerial, location.worldId, location.roomId,
                     location.door, location.mapProgram, location.battleProgram, location.eventProgram);
}

void __fastcall HookedLoadComplete(void* task) {
    g_loadComplete(task);
    CompleteLoad();  // all native room finalizers have returned
}

void __fastcall HookedLoadCompleteDirect() {
    g_loadCompleteDirect();
    CompleteLoad();
}

void IssueHostTransition() {
    if (!g_hostQueued || g_transitionPending || !SafeToWarp()) return;
    // Room initialization reads programs and chest flags from SAVE. Wait for
    // the complete host snapshot and apply it before starting the native load.
    if (!progresssync::ApplyAtRoomBoundary()) return;
    LocationPacket packet {};
    packet.world = static_cast<std::uint8_t>(g_hostTarget.worldId);
    packet.room = static_cast<std::uint8_t>(g_hostTarget.roomId);
    packet.door = g_hostTarget.door;
    packet.map = g_hostTarget.mapProgram;
    packet.battle = g_hostTarget.battleProgram;
    packet.event = g_hostTarget.eventProgram;
    g_hostQueued = false;
    g_hostIssued = true;
    g_hostIssueLoad = g_loadSerial;
    BeginTransition();
    if (g_log) g_log("[warp] client issued epoch=%u transition=%u", g_hostTarget.epoch,
                     g_transitionSerial);
    // Only this authority path bypasses the native-exit detour.
    g_requestTransition(&packet, 1, 0, 0, 0);
}

void HandOver(std::uint32_t frame) {
    const std::uint8_t world = ReadExe<std::uint8_t>(offsets::WORLD_ID);
    const std::uint8_t room = ReadExe<std::uint8_t>(offsets::ROOM_ID);

    g_channel->fromWorld = world;
    g_channel->fromRoom = room;
    g_channel->frame = frame;

    if (!g_ready || g_clientAuthority) {
        Complete(WarpStatus::Unavailable);
        return;
    }
    if (world == 0xFF || room == 0xFF) {
        Complete(WarpStatus::NotInRoom);
        return;
    }
    if (g_transitionPending || !SafeToWarp()) {
        ++g_channel->gateWaitFrames;  // keep the request pending
        return;
    }

    LocationPacket packet {};
    packet.world = static_cast<std::uint8_t>(g_channel->world);
    packet.room = static_cast<std::uint8_t>(g_channel->room);
    packet.door = static_cast<std::uint16_t>(g_channel->door);
    packet.map = static_cast<std::uint16_t>(g_channel->map);
    packet.battle = static_cast<std::uint16_t>(g_channel->battle);
    packet.event = static_cast<std::uint16_t>(g_channel->event);
    const std::uint32_t fade = g_channel->fadeFlags == 0 ? 1 : g_channel->fadeFlags;

    if (g_log) {
        g_log("Warp: %02X/%02X -> %02X/%02X door %u map %04X btl %04X evt %04X "
              "(waited %u frames; pause=%d frozen=%d infield=%d cut=%d menu=%d frame=%u)",
              world, room, packet.world, packet.room, packet.door, packet.map,
              packet.battle, packet.event, g_channel->gateWaitFrames,
              g_channel->pauseStatus, g_channel->controllable, g_channel->inField,
              g_channel->cutsceneTimer, g_channel->openMenu, frame);
    }
    BeginTransition();
    g_requestTransition(&packet, fade, 0, 0, 0);
    Complete(WarpStatus::Ok);
}

} // namespace

bool Install(uintptr_t exeBase, LogFn log) {
    g_log = log;
    g_exeBase = exeBase;

    wchar_t name[96];
    swprintf_s(name, L"%s%lu", WARP_NAME_PREFIX, GetCurrentProcessId());
    g_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                   sizeof(WarpChannel), name);
    if (g_mapping) {
        g_channel = static_cast<WarpChannel*>(
            MapViewOfFile(g_mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(WarpChannel)));
    }
    if (!g_channel) {
        if (g_log) g_log("  Warp: channel creation failed (%lu)", GetLastError());
        return false;
    }
    std::memset(g_channel, 0, sizeof(WarpChannel));
    g_channel->magic = WARP_MAGIC;
    g_channel->version = WARP_VERSION;

    struct Hook {
        std::uint64_t rva;
        const std::uint8_t* bytes;
        std::size_t size;
        LPVOID detour;
        LPVOID* original;
    };
    const Hook hooks[] = {
        {RVA_REQUEST_TRANSITION, kRequestTransitionBytes, sizeof(kRequestTransitionBytes),
         reinterpret_cast<LPVOID>(&HookedRequestTransition), reinterpret_cast<LPVOID*>(&g_requestTransition)},
        {RVA_LOAD_COMPLETE, kLoadCompleteBytes, sizeof(kLoadCompleteBytes),
         reinterpret_cast<LPVOID>(&HookedLoadComplete), reinterpret_cast<LPVOID*>(&g_loadComplete)},
        {RVA_LOAD_COMPLETE_DIRECT, kLoadCompleteDirectBytes, sizeof(kLoadCompleteDirectBytes),
         reinterpret_cast<LPVOID>(&HookedLoadCompleteDirect), reinterpret_cast<LPVOID*>(&g_loadCompleteDirect)},
    };
    for (const auto& hook : hooks) {
        if (std::memcmp(reinterpret_cast<const void*>(exeBase + hook.rva), hook.bytes, hook.size) != 0) {
            if (g_log) g_log("  Warp: bytes mismatch at RVA 0x%llX; transition sync disabled",
                             static_cast<unsigned long long>(hook.rva));
            return false;
        }
    }
    for (const auto& hook : hooks) {
        auto* target = reinterpret_cast<LPVOID>(exeBase + hook.rva);
        const auto created = MH_CreateHook(target, hook.detour, hook.original);
        if (created != MH_OK || MH_EnableHook(target) != MH_OK) {
            if (g_log) g_log("  Warp: hook setup failed at RVA 0x%llX",
                             static_cast<unsigned long long>(hook.rva));
            for (const auto& cleanup : hooks) {
                auto* address = reinterpret_cast<LPVOID>(exeBase + cleanup.rva);
                MH_DisableHook(address);
                MH_RemoveHook(address);
                *cleanup.original = nullptr;
            }
            return false;
        }
    }
    g_ready = true;
    if (g_log) g_log("  Warp ready (request + native load-completion hooks verified)");
    return true;
}

void OnFrameStart(std::uint32_t frame, uintptr_t listHead) {
    if (!g_channel) return;
    InterlockedExchange(&g_channel->liveFrame, static_cast<long>(frame));
    InterlockedExchange64(&g_channel->liveActor, static_cast<long long>(listHead));
    if (g_ready && g_clientAuthority) IssueHostTransition();
    if (g_channel->requestSeq == g_channel->doneSeq) {
        g_channel->gateWaitFrames = 0;
        return;
    }
    HandOver(frame);
}

void SetClientAuthority(bool enabled) {
    g_clientAuthority = enabled;
    if (!enabled) {
        g_hostQueued = false;
        g_hostIssued = false;
    }
}

bool QueueHostTransition(const RoomTransition& target) {
    // NOW stores world, room and door as bytes; reject truncation.
    if (!g_ready || !g_clientAuthority || target.worldId >= 0xFF || target.roomId >= 0xFF ||
        target.door > 0xFF) return false;
    g_hostTarget = target;
    g_hostQueued = true;
    g_hostIssued = false;
    if (g_log) g_log("[warp] client queued epoch=%u target=%02X/%02X door=%u map=%u btl=%u evt=%u",
                     target.epoch, target.worldId, target.roomId, target.door,
                     target.mapProgram, target.battleProgram, target.eventProgram);
    return true;
}

RoomTransition ReadLocation() {
    RoomTransition result;
    result.worldId = ReadExe<std::uint8_t>(offsets::WORLD_ID);
    result.roomId = ReadExe<std::uint8_t>(offsets::ROOM_ID);
    result.door = ReadExe<std::uint8_t>(offsets::NOW + 2);
    result.mapProgram = ReadExe<std::uint16_t>(offsets::MAP_PROGRAM);
    result.battleProgram = ReadExe<std::uint16_t>(offsets::BATTLE_PROGRAM);
    result.eventProgram = ReadExe<std::uint16_t>(offsets::EVENT_PROGRAM);
    return result;
}

bool HostTransitionArrived(std::uint32_t epoch) {
    if (!g_hostIssued || epoch != g_hostTarget.epoch || g_hostIssueLoad == g_loadSerial ||
        TransitionPending() || !SafeToWarp()) return false;
    const auto location = ReadLocation();
    return location.worldId == g_hostTarget.worldId && location.roomId == g_hostTarget.roomId &&
           location.door == g_hostTarget.door && location.mapProgram == g_hostTarget.mapProgram &&
           location.battleProgram == g_hostTarget.battleProgram && location.eventProgram == g_hostTarget.eventProgram;
}

bool TransitionPending() {
    // If lifecycle hooks could not be verified, actor writers must stay
    // suspended: reverting to NOW/timing guesses would reintroduce stale pointers.
    return !g_ready || g_transitionPending || ReadExe<std::uint8_t>(offsets::IN_FIELD) == 0;
}

std::uint32_t TransitionSerial() { return g_transitionSerial; }
std::uint32_t LoadSerial() { return g_loadSerial; }

void Shutdown() {
    g_clientAuthority = false;
    g_ready = false;
    for (const auto rva : {RVA_REQUEST_TRANSITION, RVA_LOAD_COMPLETE, RVA_LOAD_COMPLETE_DIRECT}) {
        auto* address = reinterpret_cast<LPVOID>(g_exeBase + rva);
        MH_DisableHook(address);
        MH_RemoveHook(address);
    }
    if (g_channel) {
        UnmapViewOfFile(g_channel);
        g_channel = nullptr;
    }
    if (g_mapping) {
        CloseHandle(g_mapping);
        g_mapping = nullptr;
    }
}

} // namespace warp
} // namespace inject
} // namespace kh2coop
