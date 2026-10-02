#pragma once
// ============================================================================
// WarpChannel — room warp requests from kh2ctl to the inject DLL (VUH-1486).
//
// Naming: "Local\kh2coop_warp_<KH2_PID>". The DLL creates the mapping at
// init. kh2ctl fills the target and bumps requestSeq; on the game thread, at
// the start of a frame's entity update, the DLL hands the target to the
// game's own transition request (the function room-script Jumps use) and
// sets doneSeq = requestSeq. kh2ctl then waits for the room to load.
// Fields the DLL writes are marked [dll]; the rest are [client].
// ============================================================================

#include <cstdint>

namespace kh2coop {

constexpr const wchar_t* WARP_NAME_PREFIX = L"Local\\kh2coop_warp_";
constexpr std::uint32_t WARP_MAGIC = 0x50574B48;  // "HKWP"
constexpr std::uint32_t WARP_VERSION = 1;
constexpr std::uint16_t WARP_DEFAULT_PROGRAM = 0xFFFF;  // use the save's per-room program

enum class WarpStatus : std::int32_t {
    Ok = 0,              // request handed to the game
    Unavailable = 1,     // transition function didn't match the expected bytes
    NotInRoom = 2,       // no live room to warp from (title, loading)
};

#pragma pack(push, 4)
struct WarpChannel {
    std::uint32_t magic;           // [dll]
    std::uint32_t version;         // [dll]
    volatile long requestSeq;      // [client]
    volatile long doneSeq;         // [dll]
    std::int32_t status;           // [dll] WarpStatus
    std::uint32_t world;           // [client]
    std::uint32_t room;            // [client]
    std::uint32_t door;            // [client] entrance / spawn
    std::uint32_t map;             // [client] WARP_DEFAULT_PROGRAM = per-room default
    std::uint32_t battle;          // [client]
    std::uint32_t event;           // [client]
    std::uint32_t fadeFlags;       // [client] 2nd argument of the request (1 = normal)
    // [dll] state when the request was handed over, for gate diagnostics.
    std::uint32_t fromWorld;
    std::uint32_t fromRoom;
    std::int32_t pauseStatus;
    std::int32_t controllable;
    std::int32_t cutsceneTimer;
    std::int32_t openMenu;
    std::uint32_t frame;           // [dll] DLL frame counter at hand-over
    // [dll] updated every gameplay frame. The DLL's frame counter only
    // advances while room entities update, so it stalls during a load.
    volatile long liveFrame;
    volatile long long liveActor;  // entity list head (Sora's actor) this frame
};
#pragma pack(pop)

} // namespace kh2coop
