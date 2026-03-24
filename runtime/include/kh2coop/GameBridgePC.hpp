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

private:
    // Low-level memory helpers (Windows-only implementations).
    template <typename T>
    T readMem(std::uint64_t offset) const;

    template <typename T>
    bool writeMem(std::uint64_t offset, T value);

    float readFloat(std::uint64_t offset) const;
    std::uint32_t readU32(std::uint64_t offset) const;
    std::int32_t readI32(std::uint64_t offset) const;
    std::uint8_t readU8(std::uint64_t offset) const;
    std::uint64_t readPtr(std::uint64_t offset) const;

    // Resolve actor base address for a given slot.
    std::uint64_t actorBase(SlotType slot) const;

    // Resolve enemy base address for a given index.
    std::uint64_t enemyBase(std::uint32_t index) const;

    // Process state (Windows-only, void* to avoid Win32 headers in the header).
    void* processHandle_{nullptr};
    std::uint64_t baseAddress_{0};
    bool attached_{false};

    // The KH2 process name to search for.
    static constexpr const char* KH2_PROCESS_NAME = "KINGDOM HEARTS II FINAL MIX.exe";
};

} // namespace kh2coop
