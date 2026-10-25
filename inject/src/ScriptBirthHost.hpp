#pragma once
// VUH-2605 S3: the host's record of script-born children it announces to clients.
//
// Twilight Thorn's script creates its Creepers with sysobj_appear (1/0). The host's ScriptMirror
// observer hands each exact return here. A birth is published (255/15) only once the host's own census
// has announced the child, so the client always receives a child manifest it can wait for; it repeats on
// a heartbeat while the child is still that announced enemy and its parent lives, and ends (255/16)
// exactly once when the child is no longer that enemy.
//
// A child netId is published at most once per room: a later return (including one at a reused address
// whose census row still carries the old netId) waits for a netId this ledger has never announced, and
// is dropped unpublished if none appears. Rows belong to one session generation; a row from another
// generation is dropped without any record. Pure bookkeeping: no native reads, writes or wire encoding.
#include <array>
#include <cstddef>
#include <cstdint>

namespace kh2coop::inject::scriptbirth {

inline constexpr std::uint32_t kParentObjectId = 307; // Twilight Thorn
inline constexpr std::uint32_t kChildObjectId = 317;  // Creeper
inline constexpr std::size_t kHostCap = 16;
inline constexpr std::size_t kAnnouncedCap = 64;      // child netIds announced this room
inline constexpr std::uint32_t kMapFrames = 300;      // a return the census never announces fresh is dropped unpublished
inline constexpr std::uint32_t kHeartbeatFrames = 60;

enum class Observe : std::uint8_t { Added, Refused, Full, Exhausted };
enum class Emit : std::uint8_t { Begin, End };

struct HostBirth {
    std::uint64_t occurrence{};
    std::uintptr_t child{};
    std::uint32_t generation{};
    std::uint16_t parentNetId{}, childNetId{};
    std::uint32_t observedFrame{}, sentFrame{};
    bool live{};
};

class HostBirths {
public:
    // One exact native return. A new return at the address of an earlier, still-recorded child is a new
    // body: the earlier row ends first (published rows send their end), so no address carries two births.
    Observe Add(std::uint16_t parentNetId, std::uintptr_t child, std::uint32_t frame, std::uint32_t generation) noexcept {
        if (!parentNetId || !child || !generation) return Observe::Refused;
        if (next_ == 0) return Observe::Exhausted;
        for (auto& r : rows_) if (r.live && r.child == child) Retire(r);
        HostBirth* free = nullptr;
        for (auto& r : rows_) if (!r.live && !pendingEnd(r)) { free = &r; break; }
        if (!free) return Observe::Full;
        *free = {};
        free->occurrence = next_;
        next_ = next_ == UINT64_MAX ? 0 : next_ + 1; // never wraps: later returns are refused
        free->child = child;
        free->generation = generation;
        free->parentNetId = parentNetId;
        free->observedFrame = frame;
        free->live = true;
        return Observe::Added;
    }
    // resolve(child) is the host census netId of that exact child (0: not announced / no longer announced).
    // parentLive(netId) says whether that parent is still a live announced enemy: a dead parent grants no
    // new begin or heartbeat, but its published children still end.
    // emit(kind, row) sends one record and returns false when the sender is full; it is retried next tick.
    template <class Resolve, class ParentLive, class Send>
    void Tick(std::uint32_t frame, std::uint32_t generation, Resolve&& resolve, ParentLive&& parentLive, Send&& emit) {
        // Rows from another session generation are never published or ended: clients reset with it.
        for (auto& r : rows_) if (r.occurrence && r.generation != generation) r = {};
        // Ends first, so a replaced address's old child is closed before its successor can begin.
        for (auto& r : rows_)
            if (pendingEnd(r) && emit(Emit::End, static_cast<const HostBirth&>(r))) r = {};
        for (auto& r : rows_) {
            if (!r.live) continue;
            const bool parent = parentLive(r.parentNetId);
            const std::uint16_t now = resolve(r.child);
            if (!r.childNetId) {
                if (!parent) { r = {}; continue; } // a retired parent creates nothing new
                if (now && now != r.parentNetId && !Announced(now) && Remember(now)) {
                    r.childNetId = now;
                    if (emit(Emit::Begin, static_cast<const HostBirth&>(r))) r.sentFrame = frame;
                    else r.sentFrame = 0;
                } else if (frame - r.observedFrame >= kMapFrames) {
                    r = {}; // never announced as a fresh child: nothing was published, so nothing to end
                }
                continue;
            }
            if (now != r.childNetId) { Retire(r); if (emit(Emit::End, static_cast<const HostBirth&>(r))) r = {}; continue; }
            if (parent && (!r.sentFrame || frame - r.sentFrame >= kHeartbeatFrames))
                if (emit(Emit::Begin, static_cast<const HostBirth&>(r))) r.sentFrame = frame;
        }
    }
    // Room/session change: the client resets its receipts with the room, so nothing is ended on the wire.
    void Reset() noexcept { rows_ = {}; announced_ = {}; announcedCount_ = 0; }
    std::size_t Live() const noexcept {
        std::size_t n = 0;
        for (const auto& r : rows_) n += r.live ? 1 : 0;
        return n;
    }
    std::uint64_t NextOccurrence() const noexcept { return next_; }
    // The published, still-live child at this address under this netId (the host's motion permit).
    const HostBirth* Published(std::uintptr_t child, std::uint16_t childNetId) const noexcept {
        if (!child || !childNetId) return nullptr;
        for (const auto& r : rows_) if (r.live && r.child == child && r.childNetId == childNetId) return &r;
        return nullptr;
    }

private:
    static bool pendingEnd(const HostBirth& r) noexcept { return !r.live && r.occurrence && r.childNetId; }
    static void Retire(HostBirth& r) noexcept {
        if (r.childNetId) r.live = false;  // keep the row until its end is sent
        else r = {};                       // unpublished: drop silently
    }
    bool Announced(std::uint16_t id) const noexcept {
        for (std::size_t i = 0; i < announcedCount_; ++i) if (announced_[i] == id) return true;
        return false;
    }
    bool Remember(std::uint16_t id) noexcept {
        if (announcedCount_ >= announced_.size()) return false; // full: publish nothing rather than reuse
        announced_[announcedCount_++] = id;
        return true;
    }
    std::array<HostBirth, kHostCap> rows_{};
    std::array<std::uint16_t, kAnnouncedCap> announced_{};
    std::size_t announcedCount_ = 0;
    std::uint64_t next_ = 1; // never rewinds, including Reset(): an occurrence is never reused
};

} // namespace kh2coop::inject::scriptbirth
