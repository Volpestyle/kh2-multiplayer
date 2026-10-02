#pragma once
// ============================================================================
// WorldBridge — discrete world events between the inject DLL and the runtime.
//
// Two PacketRings in shared memory "Local\kh2coop_world_<KH2_PID>":
//   toRuntime   DLL -> runtime   host-side detections, encoded as protocol
//                                packets (RoomTransition, EventHold,
//                                EnemyManifest/Hp/Death, ProgressUpdate,
//                                HitClaim, TransitionAck)
//   toDll       runtime -> DLL   the same packets received from the relay,
//                                for the DLL to act on (warp, hold, match
//                                enemies, apply a claimed hit, write flags)
// Payloads are exactly what the codec encodes, so the runtime forwards bytes
// without re-encoding and both sides share one message definition.
// Either side may open first; the first opener formats the rings.
// ============================================================================

#include "kh2coop/PacketRing.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <vector>

namespace kh2coop {

static constexpr const char* WORLD_BRIDGE_PREFIX = "Local\\kh2coop_world_";
static constexpr std::uint32_t WORLD_BRIDGE_MAGIC = 0x42574B32; // "2KWB"
static constexpr std::uint32_t WORLD_BRIDGE_VERSION = 1;
static constexpr std::uint32_t WORLD_RING_BYTES = 1u << 20;      // 1 MiB each way

class WorldBridge {
public:
    WorldBridge() = default;
    ~WorldBridge() { Close(); }
    WorldBridge(const WorldBridge&) = delete;
    WorldBridge& operator=(const WorldBridge&) = delete;

    bool Open(DWORD kh2Pid) {
        if (view_) return true;
        char name[128];
        std::snprintf(name, sizeof(name), "%s%lu", WORLD_BRIDGE_PREFIX,
                      static_cast<unsigned long>(kh2Pid));
        const DWORD size = static_cast<DWORD>(kHeaderBytes + 2 * kRingBytes);
        mapping_ = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr,
                                      PAGE_READWRITE, 0, size, name);
        if (!mapping_) return false;
        const bool existed = GetLastError() == ERROR_ALREADY_EXISTS;
        view_ = static_cast<std::uint8_t*>(
            MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, size));
        if (!view_) {
            Close();
            return false;
        }
        auto* header = reinterpret_cast<volatile std::uint32_t*>(view_);
        if (!existed) {
            PacketRing::format(view_ + kHeaderBytes, WORLD_RING_BYTES);
            PacketRing::format(view_ + kHeaderBytes + kRingBytes, WORLD_RING_BYTES);
            header[1] = WORLD_BRIDGE_VERSION;
            std::atomic_thread_fence(std::memory_order_release);
            header[0] = WORLD_BRIDGE_MAGIC;
        } else if (header[0] != WORLD_BRIDGE_MAGIC || header[1] != WORLD_BRIDGE_VERSION) {
            Close();
            return false;
        }
        toRuntime_ = PacketRing(view_ + kHeaderBytes);
        toDll_ = PacketRing(view_ + kHeaderBytes + kRingBytes);
        return true;
    }

    void Close() {
        if (view_) UnmapViewOfFile(view_);
        if (mapping_) CloseHandle(mapping_);
        view_ = nullptr;
        mapping_ = nullptr;
        toRuntime_ = PacketRing();
        toDll_ = PacketRing();
    }

    [[nodiscard]] bool IsOpen() const { return view_ != nullptr; }

    // DLL side
    bool SendToRuntime(const std::vector<std::uint8_t>& packet) { return toRuntime_.push(packet); }
    bool ReceiveFromRuntime(std::vector<std::uint8_t>& packet) { return toDll_.pop(packet); }

    // Runtime side
    bool ReceiveFromDll(std::vector<std::uint8_t>& packet) { return toRuntime_.pop(packet); }
    bool SendToDll(const std::vector<std::uint8_t>& packet) { return toDll_.push(packet); }

    [[nodiscard]] std::uint32_t droppedToRuntime() const { return toRuntime_.dropped(); }
    [[nodiscard]] std::uint32_t droppedToDll() const { return toDll_.dropped(); }

private:
    static constexpr std::size_t kHeaderBytes = 64;
    static constexpr std::size_t kRingBytes = PacketRing::bytesFor(WORLD_RING_BYTES);

    HANDLE mapping_ = nullptr;
    std::uint8_t* view_ = nullptr;
    PacketRing toRuntime_;
    PacketRing toDll_;
};

} // namespace kh2coop
