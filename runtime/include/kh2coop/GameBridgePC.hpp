#pragma once
#include "kh2coop/IGameBridge.hpp"
#include "kh2coop/KH2Offsets.hpp"

#include <cstdint>
#include <string>

namespace kh2coop {

// ---------------------------------------------------------------------------
// GameBridgePC — concrete IGameBridge for KH2 Final Mix PC.
//
// On Windows: attaches to the KH2 process and reads/writes game memory
// using ReadProcessMemory / WriteProcessMemory (external process pattern,
// matching OpenKH's Hypervisor approach).
//
// On non-Windows: compiles as a stub that always returns false/empty.
// This lets the project build on macOS/Linux for server-only dev.
// ---------------------------------------------------------------------------
class GameBridgePC : public IGameBridge {
public:
    GameBridgePC();
    ~GameBridgePC() override;

    // Attempt to find and attach to the KH2 process.
    // Call this once at startup. Returns true if the process was found.
    bool Attach();

    // Detach from the process.
    void Detach();

    // --- IGameBridge interface ---
    bool IsAttached() const override;
    RoomState ReadRoomState() const override;
    std::optional<ActorState> ReadActorState(SlotType slot) const override;
    std::vector<EnemyState> ReadEnemyStates() const override;
    bool WriteCameraTarget(SlotType slot) override;
    bool RestoreVanillaCamera() override;
    bool InjectOwnedInput(SlotType slot, const InputFrame& input) override;
    bool ApplyReplicaActorState(const ActorState& state) override;
    bool ApplyReplicaEnemyState(const EnemyState& state) override;

    // -----------------------------------------------------------------------
    // Per-frame update
    //
    // Call once per frame from the runtime main loop. Detects room
    // transitions and automatically re-discovers entity addresses.
    // Also re-points the camera actor pointer if retargeted (since the
    // game may reset it).
    // -----------------------------------------------------------------------
    void Tick();

    // -----------------------------------------------------------------------
    // Entity discovery
    //
    // The entity struct base changes per room transition. Call this after
    // each room change (or periodically) to re-discover entity addresses.
    // Returns true if the player entity struct was found for at least slot 0.
    // Normally called automatically by Tick(), but can be called manually.
    // -----------------------------------------------------------------------
    bool DiscoverEntityAddresses();

    // Check if entity addresses have been discovered for the current room.
    bool HasEntityAddresses() const;

private:
    // Low-level memory helpers (Windows-only implementations).
    template <typename T>
    T readMem(std::uint64_t offset) const;

    template <typename T>
    bool writeMem(std::uint64_t offset, T value);

    // Read/write using ABSOLUTE address (not relative to baseAddress_).
    template <typename T>
    T readAbs(std::uint64_t absoluteAddr) const;

    template <typename T>
    bool writeAbs(std::uint64_t absoluteAddr, T value);

    float readFloat(std::uint64_t offset) const;
    std::uint32_t readU32(std::uint64_t offset) const;
    std::int32_t readI32(std::uint64_t offset) const;
    std::uint8_t readU8(std::uint64_t offset) const;
    std::uint64_t readPtr(std::uint64_t offset) const;

    // Resolve unit slot base address for a given slot (HP/MP/stats — static).
    std::uint64_t actorBase(SlotType slot) const;

    // Resolve enemy base address for a given index.
    std::uint64_t enemyBase(std::uint32_t index) const;

    // Scan the exe data section to find an entity struct by vtable pattern.
    // Returns the absolute address of the entity struct base, or 0 if not found.
    std::uint64_t scanForEntityStruct() const;

    // Find the player's buffer entry index by matching position between
    // the entity struct and the buffer array entries.
    std::int32_t findBufferSlot(std::uint64_t entityStructAddr) const;

    // Process state (Windows-only, void* to avoid Win32 headers in the header).
    void* processHandle_{nullptr};
    std::uint64_t baseAddress_{0};
    bool attached_{false};

    // Discovered entity addresses (change per room transition).
    // These are ABSOLUTE addresses (baseAddress_ already included).
    std::uint64_t entityStructAddr_{0};  // player entity struct base
    std::int32_t  bufferSlotIndex_{-1};  // player's index in the buffer array

    // Cached room ID to detect room transitions and re-discover.
    std::uint8_t lastWorldId_{0};
    std::uint8_t lastRoomId_{0};

    // Camera retargeting state.
    // When retargeted, we allocate a fake actor object in the target process
    // and point the camera struct's actor pointer at it. We must continuously
    // update the fake entity's position each frame.
    std::uint64_t fakeActorAddr_{0};       // allocated fake actor object (in target process)
    std::uint64_t origCameraActorPtr_{0};  // saved original camera actor pointer for restore
    bool cameraRetargeted_{false};         // true if camera is currently overridden

    // The KH2 process name to search for.
    static constexpr const char* KH2_PROCESS_NAME = "KINGDOM HEARTS II FINAL MIX.exe";
};

} // namespace kh2coop
