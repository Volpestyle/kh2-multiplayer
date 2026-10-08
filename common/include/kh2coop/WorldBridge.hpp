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
// toRuntime records prefix codec bytes with captured generation32, delivery64
// and native host source64. The runtime checks them without relabeling queues.
// toDll records retain exact scoped envelopes or local transaction/reset records.
// Either side may open first; the first opener formats the rings.
// Header words: [0] magic, [1] version, [2] the runtime's session slot
// (0 = Player = host, 1/2 = clients, 0xFF = not known yet; VUH-1502),
// [3] round trip ms and [4] loss per-mille for the overlay (VUH-1493;
// WORLD_NET_UNKNOWN until the runtime is connected).
// [5] session generation, advanced before the ordered reset is enqueued.
// A framed SessionState with generation32/delivery64 is a bridge-local reset;
// it precedes the new session's world packets in the same FIFO. It never goes
// over the network. The DLL clears its epoch/warp state when consuming it.
// Bytes [24,48) hold three aligned, atomic uint64 connection IDs from the
// current roster. Zero means absent. Hit claims echo the producer's own ID;
// the host checks the attacker's current ID again before native application.
// Word [12] is explicit puppet authority: Off only for the never-armed
// standalone bridge, Unavailable during network binding/retirement, Network
// after the runtime has published a valid roster. Unknown slot is not Off.
// Bytes [56,64) hold the runtime's aligned atomic uint64 delivery serial.
// Bytes [64,88) hold per-slot delivery floors, published before a host receives
// the ordered plan. Already queued reverse claims can retire immediately.
// Bytes [88,116) hold a separate operator mailbox: state32, generation32,
// delivery64, hostConnection64, targetMask32. CLI never writes either SPSC ring.
// Bytes [120,128) hold the spawn-pick salt (VUH-1515): FNV-1a64 of the relay's
// world incarnation id, published by the runtime before it advances the session
// generation. Zero means none (an older runtime, or no session); the DLL then
// keeps the native random spawn draw. Layout-compatible: no version change.
// Word [13] (bytes52..56): runtime GetTickCount heartbeat; word [29]
// (bytes116..120): writer PID, published before each heartbeat. Version12
// requires these fields for armed sessions; ring offsets remain128.
// ============================================================================

#include "kh2coop/PacketRing.hpp"
#include "kh2coop/PuppetProvenance.hpp"
#include "kh2coop/WorldContext.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdint>
#include <array>
#include <cstdio>
#include <string>
#include <vector>

namespace kh2coop {

static constexpr const char* WORLD_BRIDGE_PREFIX = "Local\\kh2coop_world_";
static constexpr std::uint32_t WORLD_BRIDGE_MAGIC = 0x42574B32; // "2KWB"
static constexpr std::uint32_t WORLD_BRIDGE_VERSION = 12; // runtime pump liveness in reserved header words
static constexpr std::uint32_t WORLD_RING_BYTES = 1u << 20;      // 1 MiB each way
static constexpr std::uint8_t WORLD_SLOT_UNKNOWN = 0xFF;
static constexpr std::uint32_t WORLD_NET_UNKNOWN = 0xFFFFFFFFu;

class WorldBridge {
public:
    WorldBridge() = default;
    ~WorldBridge() { Close(); }
    WorldBridge(const WorldBridge&) = delete;
    WorldBridge& operator=(const WorldBridge&) = delete;

    bool Open(DWORD kh2Pid, bool existingOnly = false) {
        if (view_) return true;
        char name[128];
        std::snprintf(name, sizeof(name), "%s%lu", WORLD_BRIDGE_PREFIX,
                      static_cast<unsigned long>(kh2Pid));
        const DWORD size = static_cast<DWORD>(kHeaderBytes + 2 * kRingBytes);
        mapping_ = existingOnly
            ? OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name)
            : CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr,
                                 PAGE_READWRITE, 0, size, name);
        if (!mapping_) return false;
        const bool existed = existingOnly || GetLastError() == ERROR_ALREADY_EXISTS;
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
            header[5] = 0; // unarmed until runtime publishes a session boundary
            header[12] = static_cast<std::uint32_t>(PuppetAuthorityMode::Off);
            SetDeliverySerial(0);
            SetConnectionIds({});
            SetPeerDeliverySerials({});
            SetSpawnPickSalt(0);
            *reinterpret_cast<volatile LONG*>(view_ + 88) = 0;
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

    // Operator tools must not create a substitute for an absent live mapping.
    bool OpenExisting(DWORD kh2Pid) { return Open(kh2Pid, true); }

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
    // Native producers pass the generation captured by their admitted world
    // context, rather than reading a potentially replaced header at enqueue.
    bool SendToRuntime(const std::vector<std::uint8_t>& packet, const ProducerWorldContext& context) {
        std::vector<std::uint8_t> record;
        record.reserve(20 + packet.size());
        for (unsigned byte = 0; byte < 4; ++byte)
            record.push_back(static_cast<std::uint8_t>(context.generation >> (8 * byte)));
        for (const auto value : {context.deliverySerial, context.hostSourceSerial})
            for (unsigned byte = 0; byte < 8; ++byte)
                record.push_back(static_cast<std::uint8_t>(value >> (8 * byte)));
        record.insert(record.end(), packet.begin(), packet.end());
        return toRuntime_.push(record);
    }
    // Diagnostic compatibility only. No host source order is minted here;
    // native gameplay producers retain the context captured at observation.
    bool SendToRuntime(const std::vector<std::uint8_t>& packet, std::uint32_t generation) {
        return SendToRuntime(packet, ProducerWorldContext{generation, DeliverySerial(), 0});
    }
    // Convenience for diagnostic producers without cached native state.
    bool SendToRuntime(const std::vector<std::uint8_t>& packet) {
        return SendToRuntime(packet, SessionGeneration());
    }
    bool ReceiveFromRuntime(std::vector<std::uint8_t>& packet) { return toDll_.pop(packet); }

    // Runtime alone renews this lease, on the same pump that publishes world work.
    void PulseRuntimeWriter() {
        if (!view_) return;
        // Publish replacement identity before freshness: an old binding must
        // never receive the new writer's heartbeat before seeing its new PID.
        InterlockedExchange(reinterpret_cast<volatile LONG*>(view_ + 116), static_cast<LONG>(GetCurrentProcessId()));
        InterlockedExchange(reinterpret_cast<volatile LONG*>(view_ + 52), static_cast<LONG>(GetTickCount()));
    }
    struct WriterView { std::uint32_t pid = 0, heartbeat = 0, generation = 0; bool valid = false; };
    [[nodiscard]] WriterView RuntimeWriter() const {
        if (!view_) return {};
        const auto generation = SessionGeneration();
        auto* pid = reinterpret_cast<volatile LONG*>(view_ + 116);
        const auto before = static_cast<std::uint32_t>(InterlockedCompareExchange(pid, 0, 0));
        const auto heartbeat = static_cast<std::uint32_t>(InterlockedCompareExchange(
            reinterpret_cast<volatile LONG*>(view_ + 52), 0, 0));
        const auto after = static_cast<std::uint32_t>(InterlockedCompareExchange(pid, 0, 0));
        return before == after && SessionGeneration() == generation ? WriterView {after, heartbeat, generation, true} : WriterView {};
    }

    // Runtime side
    bool ReceiveFromDll(std::vector<std::uint8_t>& packet, ProducerWorldContext& context) {
        std::vector<std::uint8_t> record;
        if (!toRuntime_.pop(record)) return false;
        context = {};
        packet.clear();
        if (record.size() < 20) return true; // malformed record, rejected by pump
        for (unsigned byte = 0; byte < 4; ++byte)
            context.generation |= static_cast<std::uint32_t>(record[byte]) << (8 * byte);
        for (unsigned byte = 0; byte < 8; ++byte) {
            context.deliverySerial |= static_cast<std::uint64_t>(record[4 + byte]) << (8 * byte);
            context.hostSourceSerial |= static_cast<std::uint64_t>(record[12 + byte]) << (8 * byte);
        }
        packet.assign(record.begin() + 20, record.end());
        return true;
    }
    bool ReceiveFromDll(std::vector<std::uint8_t>& packet, std::uint32_t& generation) {
        ProducerWorldContext context;
        if (!ReceiveFromDll(packet, context)) return false;
        generation = context.generation;
        return true;
    }
    bool ReceiveFromDll(std::vector<std::uint8_t>& packet) {
        std::uint32_t generation = 0;
        return ReceiveFromDll(packet, generation);
    }
    bool SendToDll(const std::vector<std::uint8_t>& packet) { return toDll_.push(packet); }

    // Multiple operator processes may contend; none becomes a second producer
    // of the DLL's SPSC ring. A crashed writer leaves an explicitly busy slot,
    // never an invented complete command or a blocking native-thread lock.
    bool QueueResyncCommand(std::uint8_t targetMask, const ProducerWorldContext& context,
                            std::uint64_t hostConnection) {
        if (!view_ || !context.generation || !context.deliverySerial ||
            context.hostSourceSerial || !hostConnection ||
            (targetMask != 2 && targetMask != 4 && targetMask != 6)) return false;
        auto* state = reinterpret_cast<volatile LONG*>(view_ + 88);
        if (InterlockedCompareExchange(state, 1, 0) != 0) return false;
        *reinterpret_cast<std::uint32_t*>(view_ + 92) = context.generation;
        *reinterpret_cast<std::uint64_t*>(view_ + 96) = context.deliverySerial;
        *reinterpret_cast<std::uint64_t*>(view_ + 104) = hostConnection;
        *reinterpret_cast<std::uint32_t*>(view_ + 112) = targetMask;
        InterlockedExchange(state, 2);
        return true;
    }

    // Runtime is the sole consumer. Preserve observation-time identity; the
    // pump checks it against the current binding instead of retagging it.
    bool ReceiveResyncCommand(std::uint8_t& targetMask, ProducerWorldContext& context,
                              std::uint64_t& hostConnection) {
        if (!view_) return false;
        auto* state = reinterpret_cast<volatile LONG*>(view_ + 88);
        if (InterlockedCompareExchange(state, 3, 2) != 2) return false;
        const auto rawMask = *reinterpret_cast<const std::uint32_t*>(view_ + 112);
        targetMask = rawMask <= 0xFFu ? static_cast<std::uint8_t>(rawMask) : 0;
        context = {*reinterpret_cast<const std::uint32_t*>(view_ + 92),
                   *reinterpret_cast<const std::uint64_t*>(view_ + 96), 0};
        hostConnection = *reinterpret_cast<const std::uint64_t*>(view_ + 104);
        InterlockedExchange(state, 0);
        return true;
    }

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

    // Runtime is the sole writer (VUH-1515 spawn picks); published before the
    // session generation advances, so a reader under the new generation sees it.
    void SetSpawnPickSalt(std::uint64_t salt) {
        if (!view_) return;
        auto* word = reinterpret_cast<volatile LONG64*>(view_ + 120);
        InterlockedExchange64(word, static_cast<LONG64>(salt));
    }
    [[nodiscard]] std::uint64_t SpawnPickSalt() const {
        if (!view_) return 0;
        auto* word = reinterpret_cast<volatile LONG64*>(view_ + 120);
        return static_cast<std::uint64_t>(InterlockedCompareExchange64(word, 0, 0));
    }
    // FNV-1a 64 of the relay's world incarnation id; 0 only for an empty id.
    static std::uint64_t SpawnPickSaltFromSession(const std::string& id) {
        if (id.empty()) return 0;
        std::uint64_t h = 0xcbf29ce484222325ull;
        for (const unsigned char c : id) { h ^= c; h *= 0x100000001b3ull; }
        return h ? h : 1;
    }

    // Runtime is the sole writer. Interlocked access gives both processes an
    // immediate invalidation signal; the matching FIFO marker arms the session.
    std::uint32_t AdvanceSessionGeneration() {
        if (!view_) return 0;
        auto* generation = reinterpret_cast<volatile LONG*>(view_ + 5 * sizeof(std::uint32_t));
        auto next = static_cast<std::uint32_t>(InterlockedIncrement(generation));
        if (next == 0) next = static_cast<std::uint32_t>(InterlockedIncrement(generation));
        return next;
    }
    [[nodiscard]] std::uint32_t SessionGeneration() const {
        if (!view_) return 0;
        auto* generation = reinterpret_cast<volatile LONG*>(view_ + 5 * sizeof(std::uint32_t));
        return static_cast<std::uint32_t>(InterlockedCompareExchange(generation, 0, 0));
    }

    // Runtime is the sole publisher; native producers retain the serial from
    // their admitted observation. It is never substituted during queue drain.
    void SetDeliverySerial(std::uint64_t serial) {
        if (!view_) return;
        auto* word = reinterpret_cast<volatile LONG64*>(view_ + 56);
        InterlockedExchange64(word, static_cast<LONG64>(serial));
    }
    [[nodiscard]] std::uint64_t DeliverySerial() const {
        if (!view_) return 0;
        auto* word = reinterpret_cast<volatile LONG64*>(view_ + 56);
        return static_cast<std::uint64_t>(InterlockedCompareExchange64(word, 0, 0));
    }

    void SetPeerDeliverySerials(const std::array<std::uint64_t, 3>& serials) {
        if (!view_) return;
        for (std::size_t slot = 0; slot < serials.size(); ++slot) {
            auto* word = reinterpret_cast<volatile LONG64*>(view_ + 64 + slot * 8);
            InterlockedExchange64(word, static_cast<LONG64>(serials[slot]));
        }
    }
    [[nodiscard]] std::uint64_t PeerDeliverySerial(std::uint8_t slot) const {
        if (!view_ || slot >= 3) return 0;
        auto* word = reinterpret_cast<volatile LONG64*>(view_ + 64 + slot * 8);
        return static_cast<std::uint64_t>(InterlockedCompareExchange64(word, 0, 0));
    }

    // Runtime is the sole writer. Each slot changes atomically, including
    // retirement while a claim waits in either process's queue. A complete
    // session replacement also advances generation before publishing IDs.
    void SetConnectionIds(const std::array<std::uint64_t, 3>& ids) {
        if (!view_) return;
        for (std::size_t slot = 0; slot < ids.size(); ++slot) {
            auto* word = reinterpret_cast<volatile LONG64*>(view_ + 24 + slot * 8);
            InterlockedExchange64(word, static_cast<LONG64>(ids[slot]));
        }
    }
    [[nodiscard]] std::uint64_t ConnectionId(std::uint8_t slot) const {
        if (!view_ || slot >= 3) return 0;
        auto* word = reinterpret_cast<volatile LONG64*>(view_ + 24 + slot * 8);
        return static_cast<std::uint64_t>(InterlockedCompareExchange64(word, 0, 0));
    }
    // Explicit Off is the newly initialized, standalone DLL state. A network
    // runtime publishes Unavailable before changing any binding, then Network
    // after a valid roster is installed. Unknown slot/zero IDs alone are never
    // permission to consume standalone poses.
    void SetPuppetAuthorityMode(PuppetAuthorityMode mode) {
        if (!view_) return;
        auto* word = reinterpret_cast<volatile LONG*>(view_ + 48);
        InterlockedExchange(word, static_cast<LONG>(mode));
    }
    [[nodiscard]] PuppetAuthorityMode GetPuppetAuthorityMode() const {
        if (!view_) return PuppetAuthorityMode::Unavailable;
        auto* word = reinterpret_cast<volatile LONG*>(view_ + 48);
        const auto mode = InterlockedCompareExchange(word, 0, 0);
        return mode >= static_cast<LONG>(PuppetAuthorityMode::Unavailable) &&
               mode <= static_cast<LONG>(PuppetAuthorityMode::Network)
            ? static_cast<PuppetAuthorityMode>(mode) : PuppetAuthorityMode::Unavailable;
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
    static constexpr std::size_t kHeaderBytes = 128;
    static constexpr std::size_t kRingBytes = PacketRing::bytesFor(WORLD_RING_BYTES);

    HANDLE mapping_ = nullptr;
    std::uint8_t* view_ = nullptr;
    PacketRing toRuntime_;
    PacketRing toDll_;
};

} // namespace kh2coop
