#include "kh2coop/GameBridgePC.hpp"
#include "kh2coop/KH2Offsets.hpp"

#include <array>
#include <cmath>
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
    if (!Module32First(modSnap, &modEntry)) {
        // Module enumeration failed — cannot determine base address.
        CloseHandle(modSnap);
        CloseHandle(handle);
        return false;
    }
    baseAddress_ = reinterpret_cast<std::uint64_t>(modEntry.modBaseAddr);
    CloseHandle(modSnap);

    if (baseAddress_ == 0) {
        // Base address resolved to zero — unusable.
        CloseHandle(handle);
        return false;
    }

    processHandle_ = handle;
    attached_ = true;
    return true;
#else
    // Non-Windows stub.
    return false;
#endif
}

void GameBridgePC::Detach() {
    // Restore camera BEFORE closing the process handle, since
    // RestoreVanillaCamera needs to write to the target process.
    if (cameraRetargeted_) {
        RestoreVanillaCamera();
    }

#ifdef _WIN32
    if (processHandle_) {
        CloseHandle(static_cast<HANDLE>(processHandle_));
        processHandle_ = nullptr;
    }
#endif

    attached_ = false;
    baseAddress_ = 0;
    entityStructAddr_ = 0;
    friend1EntityAddr_ = 0;
    friend2EntityAddr_ = 0;
    bufferSlotIndex_ = -1;
    lastWorldId_ = 0;
    lastRoomId_ = 0;
    fakeActorAddr_ = 0;
    origCameraActorPtr_ = 0;
    cameraRetargeted_ = false;
}

bool GameBridgePC::IsAttached() const { return attached_; }

// ==========================================================================
// Low-level memory access
// ==========================================================================

#ifdef _WIN32

template <typename T>
T GameBridgePC::readMem(std::uint64_t offset) const {
    T value{};
    if (!processHandle_) return value;
    SIZE_T bytesRead = 0;
    BOOL ok = ReadProcessMemory(static_cast<HANDLE>(processHandle_),
                                reinterpret_cast<LPCVOID>(baseAddress_ + offset),
                                &value, sizeof(T), &bytesRead);
    if (!ok || bytesRead != sizeof(T)) return T{};
    return value;
}

template <typename T>
bool GameBridgePC::writeMem(std::uint64_t offset, T value) {
    if (!processHandle_) return false;
    SIZE_T bytesWritten = 0;
    BOOL ok = WriteProcessMemory(static_cast<HANDLE>(processHandle_),
                                 reinterpret_cast<LPVOID>(baseAddress_ + offset),
                                 &value, sizeof(T), &bytesWritten);
    return ok && bytesWritten == sizeof(T);
}

template <typename T>
T GameBridgePC::readAbs(std::uint64_t absoluteAddr) const {
    T value{};
    if (!processHandle_) return value;
    SIZE_T bytesRead = 0;
    BOOL ok = ReadProcessMemory(static_cast<HANDLE>(processHandle_),
                                reinterpret_cast<LPCVOID>(absoluteAddr),
                                &value, sizeof(T), &bytesRead);
    if (!ok || bytesRead != sizeof(T)) return T{};
    return value;
}

template <typename T>
bool GameBridgePC::writeAbs(std::uint64_t absoluteAddr, T value) {
    if (!processHandle_) return false;
    SIZE_T bytesWritten = 0;
    BOOL ok = WriteProcessMemory(static_cast<HANDLE>(processHandle_),
                                 reinterpret_cast<LPVOID>(absoluteAddr),
                                 &value, sizeof(T), &bytesWritten);
    return ok && bytesWritten == sizeof(T);
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
template <typename T>
T GameBridgePC::readAbs(std::uint64_t) const { return T{}; }
template <typename T>
bool GameBridgePC::writeAbs(std::uint64_t, T) { return false; }

#endif

// ==========================================================================
// Address resolution — unit slots (static, for HP/MP/stats)
// ==========================================================================

std::uint64_t GameBridgePC::actorBase(SlotType slot) const {
    using namespace offsets;
    // Unit slot data is at a static offset from exe base.
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
// Entity struct discovery (Session 3 procedure)
//
// The player entity struct lives in the exe data section at a room-dependent
// address. We find it by scanning for the vtable pattern:
//   - A QWORD that points back into the same 0x2500000..0x2600000 module region
//   - Position W component (float 1.0) at +0x3C from that QWORD
//
// After finding the entity struct, we match its position against the buffer
// array entries to find the player's buffer slot index.
// ==========================================================================

std::uint64_t GameBridgePC::scanForEntityStruct() const {
#ifdef _WIN32
    using namespace offsets;

    // --- Strategy 1: Use the camera's actor pointer (most reliable) ---
    // The game's camera struct contains a pointer to the actor object that
    // the camera follows. In vanilla KH2, this is always the player.
    // Actor object + ACTOR_TO_ENTITY (0x640) = entity transform struct.
    {
        std::uint64_t camStructAddr = baseAddress_ + CAMERA_STRUCT;
        std::uint64_t actorPtr = readAbs<std::uint64_t>(camStructAddr + camera::ACTOR_PTR);

        if (actorPtr != 0 && actorPtr > baseAddress_ &&
            actorPtr < baseAddress_ + 0x3000000) {
            std::uint64_t candidate = actorPtr + camera::ACTOR_TO_ENTITY;

            // Validate: check vtable pointer, W component, and movement state.
            std::uint64_t vtableVal = readAbs<std::uint64_t>(candidate + entity::VTABLE_PTR);
            float posW = readAbs<float>(candidate + entity::POS_W);
            std::uint32_t moveState = readAbs<std::uint32_t>(candidate + entity::MOVE_STATE);

            if (vtableVal >= baseAddress_ + entity_discovery::VTABLE_RANGE_LO &&
                vtableVal <  baseAddress_ + entity_discovery::VTABLE_RANGE_HI &&
                std::fabs(posW - entity_discovery::POS_W_EXPECTED) < 0.001f &&
                (moveState == 2 || moveState == 3)) {
                return candidate;
            }
        }
    }

    // --- Strategy 2: Scan exe data section (fallback) ---
    // If the camera pointer is unavailable (e.g., during transitions),
    // fall back to scanning the entity data region for a vtable that also
    // points back into that region plus the W=1.0 pattern. Additional
    // heuristic: moveState must be 2 or 3 to filter out false positives from
    // metadata structs.
    constexpr std::uint64_t SCAN_STEP = 0x10;  // entity structs are aligned
    constexpr std::uint64_t CHUNK_SIZE = 4096;

    const std::uint64_t scanStart = baseAddress_ + entity_discovery::SCAN_START;
    const std::uint64_t scanEnd   = baseAddress_ + entity_discovery::SCAN_END;
    const std::uint64_t vtableLo  = baseAddress_ + entity_discovery::VTABLE_RANGE_LO;
    const std::uint64_t vtableHi  = baseAddress_ + entity_discovery::VTABLE_RANGE_HI;

    // Read memory in chunks to reduce RPM calls.
    std::uint8_t chunkBuf[CHUNK_SIZE];

    for (std::uint64_t chunkAddr = scanStart; chunkAddr < scanEnd; chunkAddr += CHUNK_SIZE) {
        SIZE_T bytesRead = 0;
        if (!ReadProcessMemory(static_cast<HANDLE>(processHandle_),
                               reinterpret_cast<LPCVOID>(chunkAddr),
                               chunkBuf, CHUNK_SIZE, &bytesRead)) {
            continue;
        }
        if (bytesRead < entity::AIRBORNE_SUB + sizeof(std::uint32_t))
            continue;

        // Scan within this chunk.
        for (std::uint64_t i = 0; i + entity::AIRBORNE_SUB + sizeof(std::uint32_t) <= bytesRead; i += SCAN_STEP) {
            // Check vtable pointer (QWORD at offset 0).
            std::uint64_t vtableVal = 0;
            std::memcpy(&vtableVal, &chunkBuf[i], sizeof(vtableVal));

            if (vtableVal < vtableLo || vtableVal >= vtableHi)
                continue;

            // Check position W component at +0x3C (should be 1.0).
            float posW = 0.0f;
            std::memcpy(&posW, &chunkBuf[i + entity::POS_W], sizeof(posW));

            if (std::fabs(posW - entity_discovery::POS_W_EXPECTED) > 0.001f)
                continue;

            // Check moveState at +0x100 (must be 2=grounded or 3=airborne).
            // This filters out false positives from metadata/vtable structs
            // which had garbage moveState values (e.g., 937165234).
            std::uint32_t moveState = 0;
            std::memcpy(&moveState, &chunkBuf[i + entity::MOVE_STATE], sizeof(moveState));

            if (moveState != 2 && moveState != 3)
                continue;

            // Candidate found. Return the absolute address.
            return chunkAddr + i;
        }
    }
#endif
    return 0;
}

std::int32_t GameBridgePC::findBufferSlot(std::uint64_t entityStructAbsAddr) const {
#ifdef _WIN32
    using namespace offsets;

    if (entityStructAbsAddr == 0) return -1;

    // Read the position from the entity struct.
    float entityX = readAbs<float>(entityStructAbsAddr + entity::POS_X);
    float entityY = readAbs<float>(entityStructAbsAddr + entity::POS_Y);
    float entityZ = readAbs<float>(entityStructAbsAddr + entity::POS_Z);

    // Scan buffer array entries to find a matching position.
    // The buffer array is at a static offset. Try up to 32 entries.
    constexpr int MAX_BUFFER_ENTRIES = 32;
    const std::uint64_t bufArrayBase = baseAddress_ + buffer::ARRAY_BASE;

    for (int i = 0; i < MAX_BUFFER_ENTRIES; ++i) {
        std::uint64_t entryAddr = bufArrayBase + static_cast<std::uint64_t>(i) * buffer::ENTRY_STRIDE;

        float bx = readAbs<float>(entryAddr + buffer::ENTRY_POS_X);
        float by = readAbs<float>(entryAddr + buffer::ENTRY_POS_Y);
        float bz = readAbs<float>(entryAddr + buffer::ENTRY_POS_Z);

        // Check for W = 1.0 to confirm this is a valid entry.
        float bw = readAbs<float>(entryAddr + buffer::ENTRY_POS_W);
        if (std::fabs(bw - 1.0f) > 0.01f)
            continue;

        // Match position within a small tolerance.
        constexpr float TOL = 0.1f;
        if (std::fabs(bx - entityX) < TOL &&
            std::fabs(by - entityY) < TOL &&
            std::fabs(bz - entityZ) < TOL) {
            return i;
        }
    }
#else
    (void)entityStructAbsAddr;
#endif
    return -1;
}

bool GameBridgePC::DiscoverEntityAddresses() {
    if (!attached_) return false;

    entityStructAddr_ = scanForEntityStruct();
    if (entityStructAddr_ == 0) return false;

    bufferSlotIndex_ = findBufferSlot(entityStructAddr_);

    // -----------------------------------------------------------------------
    // Friend entity discovery  (RE Session 2026-03-26)
    //
    // The first friend unit slot (Slot 1) stores actor object pointers to
    // both friend party members at +0x220 and +0x228 within the slot.
    // Entity struct is at actor + ACTOR_TO_ENTITY (0x640), same layout as
    // the player entity.
    //
    // Friends have moveState=0 (AI-controlled) vs player moveState=2/3.
    // These pointers are dynamic — they change per room transition.
    // -----------------------------------------------------------------------
    friend1EntityAddr_ = 0;
    friend2EntityAddr_ = 0;

#ifdef _WIN32
    {
        using namespace offsets;
        const std::uint64_t slot1Addr = baseAddress_ + SLOT0_BASE + SLOT_STRIDE;

        // Read Friend 1 actor pointer from Slot1 + 0x220.
        std::uint64_t f1Actor = readAbs<std::uint64_t>(slot1Addr + slot::FRIEND1_ACTOR_PTR);
        if (f1Actor != 0 && f1Actor > baseAddress_ &&
            f1Actor < baseAddress_ + 0x3000000) {
            std::uint64_t candidate = f1Actor + camera::ACTOR_TO_ENTITY;
            // Validate: W component should be 1.0 at entity + POS_W.
            float posW = readAbs<float>(candidate + entity::POS_W);
            if (std::fabs(posW - entity_discovery::POS_W_EXPECTED) < 0.01f) {
                friend1EntityAddr_ = candidate;
            }
        }

        // Read Friend 2 actor pointer from Slot1 + 0x228.
        std::uint64_t f2Actor = readAbs<std::uint64_t>(slot1Addr + slot::FRIEND2_ACTOR_PTR);
        if (f2Actor != 0 && f2Actor > baseAddress_ &&
            f2Actor < baseAddress_ + 0x3000000) {
            std::uint64_t candidate = f2Actor + camera::ACTOR_TO_ENTITY;
            float posW = readAbs<float>(candidate + entity::POS_W);
            if (std::fabs(posW - entity_discovery::POS_W_EXPECTED) < 0.01f) {
                friend2EntityAddr_ = candidate;
            }
        }
    }
#endif

    // Update cached room so we know when to re-discover.
    lastWorldId_ = readU8(offsets::WORLD_ID);
    lastRoomId_  = readU8(offsets::ROOM_ID);

    return true;
}

bool GameBridgePC::HasEntityAddresses() const {
    return entityStructAddr_ != 0;
}

// ==========================================================================
// Per-frame update
// ==========================================================================

void GameBridgePC::Tick() {
    if (!attached_) return;

    // Detect room transitions and re-discover entity addresses.
    std::uint8_t curWorld = readU8(offsets::WORLD_ID);
    std::uint8_t curRoom  = readU8(offsets::ROOM_ID);

    // World 255 / Room 255 = not in-game (title screen, loading, etc.)
    if (curWorld == 0xFF || curRoom == 0xFF) {
        entityStructAddr_ = 0;
        friend1EntityAddr_ = 0;
        friend2EntityAddr_ = 0;
        bufferSlotIndex_ = -1;
        return;
    }

    bool roomChanged = (curWorld != lastWorldId_) || (curRoom != lastRoomId_);
    bool needsDiscovery = roomChanged || (entityStructAddr_ == 0);

    if (needsDiscovery) {
        DiscoverEntityAddresses();
    }

    // Re-point camera actor pointer if retargeted (the game may reset it).
    if (cameraRetargeted_ && fakeActorAddr_ != 0) {
        std::uint64_t camStructAddr = baseAddress_ + offsets::CAMERA_STRUCT;
        std::uint64_t curPtr = readAbs<std::uint64_t>(camStructAddr + offsets::camera::ACTOR_PTR);
        if (curPtr != fakeActorAddr_) {
            writeAbs<std::uint64_t>(camStructAddr + offsets::camera::ACTOR_PTR, fakeActorAddr_);
        }
    }
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
    rs.inTransition = (rs.worldId == 0xFFU || rs.roomId == 0xFFU);

    // Transition/cutscene: use cutscene timer as a proxy.
    // A non-zero cutscene timer means a cutscene is playing.
    if (CUTSCENE_TIMER != 0)
        rs.inCutscene = readU32(CUTSCENE_TIMER) != 0;

    return rs;
}

std::optional<ActorState> GameBridgePC::ReadActorState(SlotType slot) const {
    using namespace offsets;
    if (!attached_) return std::nullopt;

    ActorState a;
    a.actorId = static_cast<std::uint32_t>(slot);
    a.slot = slot;

    // --- Unit slot data (static offsets: HP, MP, etc.) ---
    auto slotBase = actorBase(slot);
    a.hp = readI32(slotBase + slot::HP);
    a.mp = 0;  // TODO: MP offset within slot not yet confirmed

    // --- Entity transform data (dynamic, requires discovery) ---
    // Resolve entity struct address for this slot.
    std::uint64_t entityAddr = 0;
    if (slot == SlotType::Player) {
        entityAddr = entityStructAddr_;
    } else if (slot == SlotType::Friend1) {
        entityAddr = friend1EntityAddr_;
    } else if (slot == SlotType::Friend2) {
        entityAddr = friend2EntityAddr_;
    }

    if (entityAddr != 0) {
        a.position.x = readAbs<float>(entityAddr + entity::POS_X);
        a.position.y = readAbs<float>(entityAddr + entity::POS_Y);
        a.position.z = readAbs<float>(entityAddr + entity::POS_Z);
        a.rotationY  = readAbs<float>(entityAddr + entity::ROT_Y);

        // Velocity: only Y is known (airborne vertical velocity).
        a.velocity.y = readAbs<float>(entityAddr + entity::VEL_Y);

        // Airborne state from the entity struct flags.
        std::uint32_t moveState = readAbs<std::uint32_t>(entityAddr + entity::MOVE_STATE);
        std::uint32_t airFlag   = readAbs<std::uint32_t>(entityAddr + entity::AIRBORNE_FLAG);
        // Player uses moveState 2=ground, 3=air. Friends use moveState 0
        // (AI-controlled) so we rely on airborne flag for them.
        a.airborne = (moveState == 3) || (airFlag == 1);

        // Derive a basic action state from movement flags.
        if (a.airborne) {
            a.action = ActionState::Jump;
        } else {
            a.action = ActionState::Idle;
        }
    }
    // If entity address is 0, position/rotation data remains zeroed.
    // HP is still read from the static unit slot above.

    return a;
}

std::vector<EnemyState> GameBridgePC::ReadEnemyStates() const {
    if (!attached_) return {};

    // TODO: Read enemy count, iterate enemy list, populate EnemyState for each.
    // Requires enemy::LIST_PTR, enemy::COUNT, enemy::STRIDE offsets.

    return {};
}

// ==========================================================================
// IGameBridge — write operations
// ==========================================================================

bool GameBridgePC::WriteCameraTarget(SlotType slot) {
    if (!attached_) return false;

#ifdef _WIN32
    using namespace offsets;

    const std::uint64_t camStructAddr = baseAddress_ + CAMERA_STRUCT;

    // If targeting slot 0 (player), just restore vanilla — slot 0 is the default.
    if (slot == SlotType::Player) {
        return RestoreVanillaCamera();
    }

    // For Friend1/Friend2: we need that slot's entity position.
    // TODO: Once Friend slot entity discovery is implemented, point the
    // camera directly at that actor object. For now, this method supports
    // being called with a position that was previously set via
    // ApplyReplicaActorState, using a fake actor object in the target process.

    // Save original camera actor pointer if not already saved.
    if (!cameraRetargeted_) {
        origCameraActorPtr_ = readAbs<std::uint64_t>(camStructAddr + camera::ACTOR_PTR);
        if (origCameraActorPtr_ == 0) {
            return false;
        }
    }

    // Allocate fake actor object if needed (0x700 bytes in the target process).
    if (fakeActorAddr_ == 0) {
        // Copy the original actor object so all other fields remain valid.
        // We use VirtualAllocEx to allocate in the target process.
        LPVOID alloc = VirtualAllocEx(
            static_cast<HANDLE>(processHandle_), nullptr, 0x700,
            MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!alloc) return false;
        const auto allocatedActorAddr = reinterpret_cast<std::uint64_t>(alloc);

        // Copy the original actor object's data into the fake one.
        std::array<std::uint8_t, 0x700> buf {};
        SIZE_T bytesRead = 0;
        BOOL readOk = ReadProcessMemory(static_cast<HANDLE>(processHandle_),
                                        reinterpret_cast<LPCVOID>(origCameraActorPtr_),
                                        buf.data(), buf.size(), &bytesRead);
        if (!readOk || bytesRead != buf.size()) {
            VirtualFreeEx(static_cast<HANDLE>(processHandle_), alloc, 0,
                          MEM_RELEASE);
            return false;
        }

        SIZE_T bytesWritten = 0;
        BOOL writeOk = WriteProcessMemory(static_cast<HANDLE>(processHandle_),
                                          reinterpret_cast<LPVOID>(allocatedActorAddr),
                                          buf.data(), buf.size(), &bytesWritten);
        if (!writeOk || bytesWritten != buf.size()) {
            VirtualFreeEx(static_cast<HANDLE>(processHandle_), alloc, 0,
                          MEM_RELEASE);
            return false;
        }

        fakeActorAddr_ = allocatedActorAddr;
    }

    // Point the camera at the fake actor and record which slot we're following.
    if (!writeAbs<std::uint64_t>(camStructAddr + camera::ACTOR_PTR,
                                 fakeActorAddr_)) {
        return false;
    }
    cameraRetargeted_ = true;
    cameraTargetSlot_ = slot;

    // Note: The caller must continuously update the fake entity's position
    // each frame via ApplyReplicaActorState() for the matching slot, since
    // the game re-reads from the actor pointer every frame.

    return true;
#else
    (void)slot;
    return false;
#endif
}

bool GameBridgePC::RestoreVanillaCamera() {
    if (!attached_) return false;

#ifdef _WIN32
    using namespace offsets;

    if (!cameraRetargeted_) return true;  // already vanilla

    const std::uint64_t camStructAddr = baseAddress_ + CAMERA_STRUCT;

    // Restore the original actor pointer.
    if (origCameraActorPtr_ != 0) {
        writeAbs<std::uint64_t>(camStructAddr + camera::ACTOR_PTR, origCameraActorPtr_);
    }

    // Free the fake actor memory in the target process.
    if (fakeActorAddr_ != 0) {
        VirtualFreeEx(static_cast<HANDLE>(processHandle_),
                      reinterpret_cast<LPVOID>(fakeActorAddr_),
                      0, MEM_RELEASE);
        fakeActorAddr_ = 0;
    }

    origCameraActorPtr_ = 0;
    cameraRetargeted_ = false;
    cameraTargetSlot_ = SlotType::Player;
    return true;
#else
    return false;
#endif
}

bool GameBridgePC::InjectOwnedInput(SlotType /*slot*/,
                                    const InputFrame& /*input*/) {
    if (!attached_) return false;

    // TODO: Write input state into the game's input buffer for the given slot.
    // Requires per-slot input injection path RE.

    return false;
}

bool GameBridgePC::ApplyReplicaActorState(const ActorState& state) {
    if (!attached_) return false;

#ifdef _WIN32
    using namespace offsets;

    bool ok = true;

    // --- Write to the game's entity struct ---
    // Resolve entity struct address for this slot.
    std::uint64_t entityAddr = 0;
    if (state.slot == SlotType::Player) {
        entityAddr = entityStructAddr_;
    } else if (state.slot == SlotType::Friend1) {
        entityAddr = friend1EntityAddr_;
    } else if (state.slot == SlotType::Friend2) {
        entityAddr = friend2EntityAddr_;
    }

    if (entityAddr != 0) {
        // Write position to entity struct (authoritative source).
        ok &= writeAbs<float>(entityAddr + entity::POS_X, state.position.x);
        ok &= writeAbs<float>(entityAddr + entity::POS_Y, state.position.y);
        ok &= writeAbs<float>(entityAddr + entity::POS_Z, state.position.z);
        ok &= writeAbs<float>(entityAddr + entity::POS_W, 1.0f);

        // Write rotation.
        ok &= writeAbs<float>(entityAddr + entity::ROT_Y, state.rotationY);
        ok &= writeAbs<float>(entityAddr + entity::COS_FACING, std::cos(state.rotationY));
        ok &= writeAbs<float>(entityAddr + entity::SIN_FACING, std::sin(state.rotationY));

        // Write airborne flags.
        // Note: friends use moveState=0 normally (AI-controlled). When writing
        // replicated state, we use moveState 2/3 (player-style) to force the
        // position. The game's AI will reclaim control on the next frame for
        // non-owned slots, so continuous writes are needed.
        if (state.airborne) {
            ok &= writeAbs<std::uint32_t>(entityAddr + entity::AIRBORNE_FLAG, 1);
            ok &= writeAbs<std::uint32_t>(entityAddr + entity::MOVE_STATE, 3);
            ok &= writeAbs<std::uint32_t>(entityAddr + entity::AIRBORNE_SUB, 1);
            ok &= writeAbs<float>(entityAddr + entity::VEL_Y, state.velocity.y);
        } else {
            ok &= writeAbs<std::uint32_t>(entityAddr + entity::AIRBORNE_FLAG, 0);
            ok &= writeAbs<std::uint32_t>(entityAddr + entity::MOVE_STATE, 2);
            ok &= writeAbs<std::uint32_t>(entityAddr + entity::AIRBORNE_SUB, 0);
        }

        // Dual-write: also write position to the buffer array entry (slot 0 only).
        // In physics-active rooms, the game recomputes position each frame from
        // the buffer, so we must write to both to prevent overwrite within ~500ms.
        // TODO: Find buffer slot indices for Friend1/Friend2.
        if (state.slot == SlotType::Player && bufferSlotIndex_ >= 0) {
            std::uint64_t bufEntry = baseAddress_ + buffer::ARRAY_BASE +
                static_cast<std::uint64_t>(bufferSlotIndex_) * buffer::ENTRY_STRIDE;

            ok &= writeAbs<float>(bufEntry + buffer::ENTRY_POS_X, state.position.x);
            ok &= writeAbs<float>(bufEntry + buffer::ENTRY_POS_Y, state.position.y);
            ok &= writeAbs<float>(bufEntry + buffer::ENTRY_POS_Z, state.position.z);
            ok &= writeAbs<float>(bufEntry + buffer::ENTRY_POS_W, 1.0f);
        }
    }

    // --- Write HP to the static unit slot (works for all slots) ---
    // Always write HP, including 0 (KO). Using hp >= 0 rather than > 0
    // ensures authoritative death can be replicated. Negative HP values
    // (uninitialized / sentinel) are skipped.
    if (state.hp >= 0) {
        ok &= writeMem<std::int32_t>(actorBase(state.slot) + slot::HP, state.hp);
    }

    // --- Update camera fake actor if retargeted to THIS slot ---
    // Only update the fake entity's position when the snapshot matches the
    // slot the camera is following. Without this check, the last-processed
    // snapshot from any slot would move the camera, causing it to jump
    // between actors.
    if (cameraRetargeted_ && fakeActorAddr_ != 0 &&
        state.slot == cameraTargetSlot_) {
        std::uint64_t fakeEntity = fakeActorAddr_ + camera::ACTOR_TO_ENTITY;
        ok &= writeAbs<float>(fakeEntity + entity::POS_X, state.position.x);
        ok &= writeAbs<float>(fakeEntity + entity::POS_Y, state.position.y);
        ok &= writeAbs<float>(fakeEntity + entity::POS_Z, state.position.z);
        ok &= writeAbs<float>(fakeEntity + entity::POS_W, 1.0f);
    }

    return ok;
#else
    (void)state;
    return false;
#endif
}

bool GameBridgePC::ApplyReplicaEnemyState(const EnemyState& /*state*/) {
    if (!attached_) return false;

    // TODO: Write replicated enemy state into game memory.
    // Requires enemy entity struct discovery.

    return false;
}

} // namespace kh2coop
