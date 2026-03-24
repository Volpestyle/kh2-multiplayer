#include "kh2coop/GameBridgePC.hpp"
#include "kh2coop/KH2Offsets.hpp"

#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace kh2coop {

// ==========================================================================
// Construction / destruction
// ==========================================================================

GameBridgePC::GameBridgePC() = default;

GameBridgePC::~GameBridgePC() { Detach(); }

// ==========================================================================
// Process attachment
// ==========================================================================

bool GameBridgePC::Attach() {
#ifdef _WIN32
    if (attached_) return true;

    // Enumerate processes to find KH2.
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32 entry;
    entry.dwSize = sizeof(entry);
    DWORD pid = 0;

    if (Process32First(snap, &entry)) {
        do {
            if (std::strcmp(entry.szExeFile, KH2_PROCESS_NAME) == 0) {
                pid = entry.th32ProcessID;
                break;
            }
        } while (Process32Next(snap, &entry));
    }
    CloseHandle(snap);

    if (pid == 0) return false;

    HANDLE handle = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE |
                                PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION,
                                FALSE, pid);
    if (!handle) return false;

    // Find the base address of the main module.
    HANDLE modSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    if (modSnap == INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
        return false;
    }

    MODULEENTRY32 modEntry;
    modEntry.dwSize = sizeof(modEntry);
    if (Module32First(modSnap, &modEntry)) {
        baseAddress_ = reinterpret_cast<std::uint64_t>(modEntry.modBaseAddr);
    }
    CloseHandle(modSnap);

    processHandle_ = handle;
    attached_ = true;
    return true;
#else
    // Non-Windows stub.
    return false;
#endif
}

void GameBridgePC::Detach() {
#ifdef _WIN32
    if (processHandle_) {
        CloseHandle(static_cast<HANDLE>(processHandle_));
        processHandle_ = nullptr;
    }
#endif
    attached_ = false;
    baseAddress_ = 0;
}

bool GameBridgePC::IsAttached() const { return attached_; }

// ==========================================================================
// Low-level memory access
// ==========================================================================

#ifdef _WIN32

template <typename T>
T GameBridgePC::readMem(std::uint64_t offset) const {
    T value{};
    SIZE_T bytesRead = 0;
    ReadProcessMemory(static_cast<HANDLE>(processHandle_),
                      reinterpret_cast<LPCVOID>(baseAddress_ + offset),
                      &value, sizeof(T), &bytesRead);
    return value;
}

template <typename T>
bool GameBridgePC::writeMem(std::uint64_t offset, T value) {
    SIZE_T bytesWritten = 0;
    return WriteProcessMemory(static_cast<HANDLE>(processHandle_),
                              reinterpret_cast<LPVOID>(baseAddress_ + offset),
                              &value, sizeof(T), &bytesWritten) != 0;
}

float GameBridgePC::readFloat(std::uint64_t offset) const {
    return readMem<float>(offset);
}
std::uint32_t GameBridgePC::readU32(std::uint64_t offset) const {
    return readMem<std::uint32_t>(offset);
}
std::int32_t GameBridgePC::readI32(std::uint64_t offset) const {
    return readMem<std::int32_t>(offset);
}
std::uint8_t GameBridgePC::readU8(std::uint64_t offset) const {
    return readMem<std::uint8_t>(offset);
}
std::uint64_t GameBridgePC::readPtr(std::uint64_t offset) const {
    return readMem<std::uint64_t>(offset);
}

#else

// Non-Windows stubs.
float GameBridgePC::readFloat(std::uint64_t) const { return 0.0f; }
std::uint32_t GameBridgePC::readU32(std::uint64_t) const { return 0; }
std::int32_t GameBridgePC::readI32(std::uint64_t) const { return 0; }
std::uint8_t GameBridgePC::readU8(std::uint64_t) const { return 0; }
std::uint64_t GameBridgePC::readPtr(std::uint64_t) const { return 0; }

template <typename T>
T GameBridgePC::readMem(std::uint64_t) const { return T{}; }
template <typename T>
bool GameBridgePC::writeMem(std::uint64_t, T) { return false; }

#endif

// ==========================================================================
// Address resolution
// ==========================================================================

std::uint64_t GameBridgePC::actorBase(SlotType slot) const {
    using namespace offsets;
    // Unit slot data is at a static address, not behind a pointer.
    return SLOT0_BASE + static_cast<std::uint64_t>(slot) * SLOT_STRIDE;
}

std::uint64_t GameBridgePC::enemyBase(std::uint32_t index) const {
    using namespace offsets;
    if (enemy::LIST_PTR == 0) return 0;
    auto listBase = readPtr(enemy::LIST_PTR);
    if (listBase == 0) return 0;
    return listBase + static_cast<std::uint64_t>(index) * enemy::STRIDE;
}

// ==========================================================================
// IGameBridge — read operations
// ==========================================================================

RoomState GameBridgePC::ReadRoomState() const {
    using namespace offsets;
    RoomState rs;
    if (!attached_) return rs;

    rs.worldId = readU8(WORLD_ID);
    rs.roomId = readU8(ROOM_ID);
    rs.mapProgram = static_cast<std::uint32_t>(readMem<std::uint16_t>(MAP_PROGRAM));
    rs.battleProgram = static_cast<std::uint32_t>(readMem<std::uint16_t>(BATTLE_PROGRAM));
    rs.eventProgram = static_cast<std::uint32_t>(readMem<std::uint16_t>(EVENT_PROGRAM));

    // Transition/cutscene: use cutscene timer as a proxy.
    // A non-zero cutscene timer means a cutscene is playing.
    if (CUTSCENE_TIMER != 0)
        rs.inCutscene = readU32(CUTSCENE_TIMER) != 0;

    return rs;
}

std::optional<ActorState> GameBridgePC::ReadActorState(SlotType slot) const {
    using namespace offsets;
    if (!attached_) return std::nullopt;

    auto base = actorBase(slot);
    if (base == 0) return std::nullopt;

    // Note: these reads use ABSOLUTE addresses (base is already resolved).
    // We need to read directly, not via baseAddress_ + offset.
    // For now, this uses the readMem template which adds baseAddress_.
    // TODO: Switch to absolute reads once offsets are verified.

    ActorState a;
    a.actorId = static_cast<std::uint32_t>(slot);
    a.slot = slot;

    // Placeholder: fill from memory once offsets are known.
    // Each read here will be: readMem<float>(base + actor::POS_X) using absolute addressing.
    // For now, return a zeroed state to indicate "connected but no data."

    return a;
}

std::vector<EnemyState> GameBridgePC::ReadEnemyStates() const {
    if (!attached_) return {};

    // TODO: Read enemy count, iterate enemy list, populate EnemyState for each.
    // Requires ENEMY_LIST_PTR, ENEMY_COUNT, ENEMY_STRIDE offsets.

    return {};
}

// ==========================================================================
// IGameBridge — write operations
// ==========================================================================

bool GameBridgePC::WriteCameraTarget(SlotType /*slot*/) {
    if (!attached_) return false;

    // TODO: Write the camera target pointer to point at the owned slot's actor.
    // Requires CAMERA_PTR + camera::TARGET_ENTITY offsets.

    return false;
}

bool GameBridgePC::RestoreVanillaCamera() {
    if (!attached_) return false;

    // TODO: Restore the camera target to slot 0 (vanilla behavior).

    return false;
}

bool GameBridgePC::InjectOwnedInput(SlotType /*slot*/,
                                    const InputFrame& /*input*/) {
    if (!attached_) return false;

    // TODO: Write input state into the game's input buffer for the given slot.
    // Requires INPUT_BUFFER offset and per-slot input injection path.

    return false;
}

bool GameBridgePC::ApplyReplicaActorState(const ActorState& /*state*/) {
    if (!attached_) return false;

    // TODO: Write replicated actor position/state into game memory.
    // This is used on non-host clients to update remote player positions.

    return false;
}

bool GameBridgePC::ApplyReplicaEnemyState(const EnemyState& /*state*/) {
    if (!attached_) return false;

    // TODO: Write replicated enemy state into game memory.

    return false;
}

} // namespace kh2coop
