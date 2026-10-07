#pragma once
// ============================================================================
// AvatarBridge — shared memory between the inject DLL and the runtime for
// avatar telemetry (plan D9: frame-accurate work in the DLL, networking in
// the runtime).
//
//   local        DLL -> runtime   the local player's avatar, every frame
//   puppets[2]   runtime -> DLL   interpolated poses for friend slots 1 and 2
//
// Naming: "Local\kh2coop_avatar_<KH2_PID>". Either side may open first; the
// first opener initializes the header. "Local\" is per Windows session, so
// the runtime must run in the same session as KH2 (see VUH-1483).
//
// Each slot is a single-writer seqlock (same scheme as InputMailbox): the
// writer makes the sequence odd, copies the payload, then makes it even; the
// reader retries nothing and simply keeps its previous value on a torn or
// unchanged read. The game thread never blocks.
// ============================================================================

#include "kh2coop/Types.hpp"
#include "kh2coop/DownedState.hpp"
#include "kh2coop/PuppetProvenance.hpp"
#include "kh2coop/HudRosterSlot.hpp"

// Lean Windows headers so ENet's winsock2 can be included alongside.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace kh2coop {

static_assert(std::is_trivially_copyable_v<AvatarState>,
              "AvatarState crosses process boundaries by memcpy");

static constexpr const char* AVATAR_BRIDGE_PREFIX = "Local\\kh2coop_avatar_";
static constexpr std::uint32_t AVATAR_BRIDGE_MAGIC = 0x42564B32; // "2KVB"
static constexpr std::uint32_t AVATAR_BRIDGE_VERSION = 4;
static constexpr int AVATAR_BRIDGE_PUPPETS = 2; // friend slots 1 and 2

// A pose the DLL should apply to a friend-slot puppet.
struct PuppetPose {
    std::uint8_t active {0};      // 0 = hide / leave the slot alone
    std::uint8_t _pad[7] {};
    PuppetProvenance provenance {};
    AvatarState pose {};          // pose.ownerSlot = which player this is
};

static_assert(std::is_trivially_copyable_v<PuppetPose>);

template <typename T>
struct alignas(64) SeqlockSlot {
    volatile std::uint32_t sequence;
    std::uint32_t _pad;
    T value;

    void write(const T& v) {
        const std::uint32_t seq = sequence;
        sequence = seq + 1;
        std::atomic_thread_fence(std::memory_order_release);
        std::memcpy(const_cast<T*>(&value), &v, sizeof(T));
        std::atomic_thread_fence(std::memory_order_release);
        sequence = seq + 2;
    }

    // True only for a new, consistent value; `lastSeq` tracks what was seen.
    bool tryRead(T& out, std::uint32_t& lastSeq) const {
        const std::uint32_t s1 = sequence;
        if ((s1 & 1u) || s1 == lastSeq) return false;
        std::atomic_thread_fence(std::memory_order_acquire);
        T copy;
        std::memcpy(&copy, const_cast<const T*>(&value), sizeof(T));
        std::atomic_thread_fence(std::memory_order_acquire);
        if (sequence != s1) return false;
        out = copy;
        lastSeq = s1;
        return true;
    }
};

struct AvatarBridgeLayout {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t _reserved[14];
    SeqlockSlot<AvatarState> local;
    SeqlockSlot<PuppetPose> puppets[AVATAR_BRIDGE_PUPPETS];
    hudnames::Slot rosterNames; // v3 append; v2 peers must reject, never silently mix.
    SeqlockSlot<LocalDownedState> localDowned; // v4; native owner is sole writer
};

class AvatarBridge {
public:
    AvatarBridge() = default;
    ~AvatarBridge() { Close(); }
    AvatarBridge(const AvatarBridge&) = delete;
    AvatarBridge& operator=(const AvatarBridge&) = delete;

    // The DLL passes GetCurrentProcessId(); the runtime passes KH2's PID.
    bool Open(DWORD kh2Pid, bool existingOnly = false) {
        if (view_) return true;
        char name[128];
        std::snprintf(name, sizeof(name), "%s%lu", AVATAR_BRIDGE_PREFIX,
                      static_cast<unsigned long>(kh2Pid));
        mapping_ = existingOnly ? OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name)
                               : CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr,
                                      PAGE_READWRITE, 0,
                                      static_cast<DWORD>(sizeof(AvatarBridgeLayout)),
                                      name);
        if (!mapping_) return false;
        const bool existed = existingOnly || GetLastError() == ERROR_ALREADY_EXISTS;
        view_ = static_cast<AvatarBridgeLayout*>(MapViewOfFile(
            mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(AvatarBridgeLayout)));
        if (!view_) {
            Close();
            return false;
        }
        if (!existed) {
            std::memset(view_, 0, sizeof(AvatarBridgeLayout));
            view_->version = AVATAR_BRIDGE_VERSION;
            std::atomic_thread_fence(std::memory_order_release);
            view_->magic = AVATAR_BRIDGE_MAGIC;
        } else if (view_->magic != AVATAR_BRIDGE_MAGIC ||
                   view_->version != AVATAR_BRIDGE_VERSION) {
            Close();
            return false;
        }
        return true;
    }

    // Observation must not create or initialize a mapping for an absent PID.
    bool OpenExisting(DWORD kh2Pid) { return Open(kh2Pid, true); }

    void Close() {
        if (view_) UnmapViewOfFile(view_);
        if (mapping_) CloseHandle(mapping_);
        view_ = nullptr;
        mapping_ = nullptr;
        lastLocalSeq_ = 0;
        for (auto& s : lastPuppetSeq_) s = 0;
    }

    [[nodiscard]] bool IsOpen() const { return view_ != nullptr; }

    // DLL side.
    void PublishLocal(const AvatarState& a) { if (view_) view_->local.write(a); }
    bool TryReadPuppet(int index, PuppetPose& out) {
        if (!view_ || index < 0 || index >= AVATAR_BRIDGE_PUPPETS) return false;
        return view_->puppets[index].tryRead(out, lastPuppetSeq_[index]);
    }

    // Names never use last-value caching: partial publication is unavailable.
    bool TryReadRosterNames(hudnames::Roster& out) {
        out = {};
        return view_ && view_->rosterNames.TryRead(out);
    }
    bool PublishRosterNames(const hudnames::Roster& names) {
        return view_ && view_->rosterNames.TryWrite(names);
    }

    // Native integration hook: call from the checked world owner every frame.
    void SetLocalDownedState(const LocalDownedState& state) {
        if (view_) view_->localDowned.write(state);
    }
    bool ReadLocalDownedState(LocalDownedState& out) const {
        out = {};
        if (!view_) return false;
        std::uint32_t sequence = 0; // snapshot, not last-value caching
        return view_->localDowned.tryRead(out, sequence);
    }

    // Runtime side.
    bool TryReadLocal(AvatarState& out) {
        return view_ && view_->local.tryRead(out, lastLocalSeq_);
    }
    void PublishPuppet(int index, const PuppetPose& p) {
        if (view_ && index >= 0 && index < AVATAR_BRIDGE_PUPPETS) {
            view_->puppets[index].write(p);
        }
    }

private:
    HANDLE mapping_ = nullptr;
    AvatarBridgeLayout* view_ = nullptr;
    std::uint32_t lastLocalSeq_ = 0;
    std::uint32_t lastPuppetSeq_[AVATAR_BRIDGE_PUPPETS] = {};
};

} // namespace kh2coop
