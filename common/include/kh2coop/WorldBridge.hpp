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
// Header words: [0] magic, [1] version, [2] the runtime's session slot
// (0 = Player = host, 1/2 = clients, 0xFF = not known yet; VUH-1502),
// [3] round trip ms and [4] loss per-mille for the overlay (VUH-1493;
// WORLD_NET_UNKNOWN until the runtime is connected).
// A framed SessionState with an empty payload is a bridge-local session reset;
// it precedes the new session's world packets in the same FIFO. It never goes
// over the network. The DLL clears its epoch/warp state when consuming it.
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
static constexpr std::uint32_t WORLD_BRIDGE_VERSION = 4; // 4: durable session reset
static constexpr std::uint32_t WORLD_RING_BYTES = 1u << 20;      // 1 MiB each way
static constexpr std::uint8_t WORLD_SLOT_UNKNOWN = 0xFF;
static constexpr std::uint32_t WORLD_NET_UNKNOWN = 0xFFFFFFFFu;

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
            header[2] = WORLD_SLOT_UNKNOWN; // a zeroed word would read as host
            header[3] = WORLD_NET_UNKNOWN;
            header[4] = WORLD_NET_UNKNOWN;
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

    // Runtime sets its session slot once known; the DLL reads it to decide
    // whether it is the host (slot 0). WORLD_SLOT_UNKNOWN until set.
    void SetLocalSlot(std::uint8_t slot) {
        if (view_) reinterpret_cast<volatile std::uint32_t*>(view_)[2] = slot;
    }
    [[nodiscard]] std::uint8_t LocalSlot() const {
        if (!view_) return WORLD_SLOT_UNKNOWN;
        return static_cast<std::uint8_t>(
            reinterpret_cast<const volatile std::uint32_t*>(view_)[2]);
    }
    // Runtime publishes link quality; the DLL overlay reads it. Pass
    // WORLD_NET_UNKNOWN for both when disconnected.
    void SetNetStats(std::uint32_t rttMs, std::uint32_t lossPermille) {
        if (!view_) return;
        auto* header = reinterpret_cast<volatile std::uint32_t*>(view_);
        header[3] = rttMs;
        header[4] = lossPermille;
    }
    struct NetStatsView {
        std::uint32_t rttMs {WORLD_NET_UNKNOWN};
        std::uint32_t lossPermille {WORLD_NET_UNKNOWN};
    };
    [[nodiscard]] NetStatsView NetStats() const {
        NetStatsView out;
        if (!view_) return out;
        const auto* header = reinterpret_cast<const volatile std::uint32_t*>(view_);
        out.rttMs = header[3];
        out.lossPermille = header[4];
        return out;
    }

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
