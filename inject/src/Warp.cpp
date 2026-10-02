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

#include "kh2coop/KH2Offsets.hpp"
#include "kh2coop/WarpChannel.hpp"

#include <Windows.h>

#include <cstdio>
#include <cstring>

namespace kh2coop {
namespace inject {
namespace warp {
namespace {

constexpr std::uint64_t RVA_REQUEST_TRANSITION = 0x152990;
// First 24 bytes on Steam Global (includes a RIP-relative cmp, so they pin
// this exact build).
constexpr std::uint8_t kRequestTransitionBytes[] = {
    0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89,
    0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xec, 0x20, 0x80, 0x3d, 0x26, 0x7f,
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
// no cutscene timer running. Loading never reaches here: the caller only
// runs inside entity updates.
bool SafeToWarp() {
    g_channel->controllable = ReadExe<std::int32_t>(offsets::CONTROLLABLE);
    g_channel->inField = ReadExe<std::uint8_t>(offsets::IN_FIELD);
    g_channel->openMenu = ReadExe<std::uint8_t>(offsets::OPEN_MENU);
    g_channel->cutsceneTimer = ReadExe<std::int32_t>(offsets::CUTSCENE_TIMER);
    g_channel->pauseStatus = ReadExe<std::int32_t>(offsets::PAUSE_STATUS);
    return g_channel->controllable == 0 && g_channel->inField != 0 &&
           g_channel->openMenu == 0xFF && g_channel->cutsceneTimer == 0;
}

void HandOver(std::uint32_t frame) {
    const std::uint8_t world = ReadExe<std::uint8_t>(offsets::WORLD_ID);
    const std::uint8_t room = ReadExe<std::uint8_t>(offsets::ROOM_ID);

    g_channel->fromWorld = world;
    g_channel->fromRoom = room;
    g_channel->frame = frame;

    if (!g_requestTransition) {
        Complete(WarpStatus::Unavailable);
        return;
    }
    if (world == 0xFF || room == 0xFF) {
        Complete(WarpStatus::NotInRoom);
        return;
    }
    if (!SafeToWarp()) {
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

    const auto* target =
        reinterpret_cast<const std::uint8_t*>(exeBase + RVA_REQUEST_TRANSITION);
    if (std::memcmp(target, kRequestTransitionBytes, sizeof(kRequestTransitionBytes)) != 0) {
        if (g_log) g_log("  Warp: transition function bytes don't match this build; warp disabled");
        return false;
    }
    g_requestTransition = reinterpret_cast<RequestTransitionFn>(exeBase + RVA_REQUEST_TRANSITION);
    if (g_log) g_log("  Warp ready (transition request at RVA 0x%llX)",
                     static_cast<unsigned long long>(RVA_REQUEST_TRANSITION));
    return true;
}

void OnFrameStart(std::uint32_t frame, uintptr_t listHead) {
    if (!g_channel) return;
    InterlockedExchange(&g_channel->liveFrame, static_cast<long>(frame));
    InterlockedExchange64(&g_channel->liveActor, static_cast<long long>(listHead));
    if (g_channel->requestSeq == g_channel->doneSeq) {
        g_channel->gateWaitFrames = 0;
        return;
    }
    HandOver(frame);
}

void Shutdown() {
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
