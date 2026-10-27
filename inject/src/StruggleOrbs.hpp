#pragma once
// VUH-2466: the Day 5 Struggle's orbs follow the host's game (docs/PROLOGUE_ENCOUNTERS.md, rule A).
// Pure rules for the host's orb book and the friend's display orbs; the native hooks are in
// StruggleOrbsHook.cpp. No native memory is read here.
//
// Native (read-only Ghidra of the installed exe):
//   manager   *(exe+0x2AEA190): the Struggle's orb manager object, zero outside a match.
//   create    FUN_1403dc950(manager, descriptor, pos*) -> record. Descriptors exe+0x5CE6E0 + 0x10*type,
//             types 0..3. It scatters the record with the game's PRNG: velocity at +0x2C/+0x30/+0x34.
//             Both orb callers then set +0x28 bit 2, which stops the record's life timer: an orb leaves
//             only when collected (or with its manager).
//   drops     FUN_140410100(hit) on a hit, FUN_140410090(pos, type, count) otherwise.
//   pickups   FUN_14040ff50(_, _, record) and FUN_140410250(_, record): the first touching actor in the
//             active list takes it: FUN_140413650(record, actor, 0) starts its flight to the actor
//             (+0x48 = actor, +0x10 = the flight update), then exe+0x3FB2B0(+1, side).
// Host: every orb born at its manager gets an ID, and each collection names that ID and who took it
// (Unresolved when it cannot name one: the collection still counts).
// Friend: its own drops and pickups are suppressed; it creates the host's orbs through the same
// allocator, with the host's scatter, and starts an orb's flight only when the host collected it.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "kh2coop/Protocol.hpp"

namespace kh2coop::inject::struggleorbs {

inline constexpr std::uintptr_t kManagerRva = 0x2AEA190, kDescriptorsRva = 0x5CE6E0, kDescriptorStride = 0x10;
inline constexpr std::uintptr_t kCreateRva = 0x3DC950, kHitDropRva = 0x410100, kDropRva = 0x410090,
                                kPickARva = 0x40FF50, kPickBRva = 0x410250, kFlyRva = 0x413650, kInitRva = 0x412D40;
// Record fields.
inline constexpr std::uintptr_t kRecordUpdate = 0x10, kRecordFlags = 0x28, kRecordVelocity = 0x2C, kRecordLife = 0x3C,
                                kRecordCollector = 0x48, kRecordDescriptor = 0x50, kRecordManager = 0x58;
inline constexpr std::uint32_t kFlagCollectable = 1, kFlagKeep = 4;
// Life (+0x3C, 900 at birth) counts down in FUN_140413a80, the idle and resting update (LAB_140413230 is
// `mov dl,1; jmp FUN_140413a80`), fading the orb below its threshold; without the keep flag the record
// ends at zero. A display orb with no collector to fly to is given this much life.
inline constexpr float kFadeLife = 30.0f;

struct Shape { std::uintptr_t rva; std::uint8_t bytes[32]; };
// The first 32 bytes of each function, from the analysed exe. 410100 and 410090 read the manager
// through RIP-relative displacements to exe+0x2AEA190; 40FF50 takes the record in r8, 410250 in rdx.
inline constexpr Shape kShapes[] = {
    {kInitRva, {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x49,
                0x8B, 0xC1, 0x49, 0x8B, 0xF8, 0x4C, 0x8B, 0x4C, 0x24, 0x50, 0x48, 0x8B, 0xDA, 0x48, 0x8B, 0x92}},
    {kCreateRva, {0x48, 0x83, 0xEC, 0x28, 0x4C, 0x8D, 0x0D, 0xE5, 0x68, 0x03, 0x00, 0xE8, 0xA0, 0x75, 0x03, 0x00,
                  0x4C, 0x8B, 0xC8, 0x48, 0x85, 0xC0, 0x0F, 0x84, 0x93, 0x00, 0x00, 0x00, 0x44, 0x69, 0x05, 0x29}},
    {kHitDropRva, {0x40, 0x56, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x83, 0x3D, 0x82, 0xA0, 0x6D, 0x02, 0x00, 0x48, 0x8B,
                   0xF1, 0x0F, 0x84, 0xED, 0x00, 0x00, 0x00, 0x8B, 0x49, 0x14, 0x48, 0x89, 0x5C, 0x24, 0x40, 0x33}},
    {kDropRva, {0x40, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF1, 0x48, 0x8B, 0x0D, 0xF0, 0xA0, 0x6D, 0x02,
                0x48, 0x85, 0xC9, 0x74, 0x53, 0x45, 0x85, 0xC0, 0x7E, 0x4E, 0x48, 0x89, 0x5C, 0x24, 0x30, 0x48}},
    {kPickARva, {0x40, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41, 0xF6, 0x40, 0x28, 0x01, 0x49, 0x8B, 0xF8, 0x0F, 0x84,
                 0x96, 0x00, 0x00, 0x00, 0x33, 0xC9, 0x48, 0x89, 0x5C, 0x24, 0x30, 0xE8, 0xE0, 0x2E, 0xFC, 0xFF}},
    {kPickBRva, {0x40, 0x57, 0x48, 0x83, 0xEC, 0x20, 0xF6, 0x42, 0x28, 0x01, 0x48, 0x8B, 0xFA, 0x0F, 0x84, 0x96,
                 0x00, 0x00, 0x00, 0x33, 0xC9, 0x48, 0x89, 0x5C, 0x24, 0x30, 0xE8, 0xE1, 0x2B, 0xFC, 0xFF, 0x48}},
    {kFlyRva, {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x50, 0xF3,
               0x0F, 0x10, 0x05, 0x19, 0x04, 0x21, 0x00, 0x48, 0x8B, 0xC2, 0x48, 0x89, 0x51, 0x48, 0x48, 0x8B}},
};

// The descriptor's type, or -1 when the pointer is not one of the four orb descriptors.
constexpr int DescriptorType(std::uintptr_t exeBase, std::uintptr_t descriptor) noexcept {
    const std::uintptr_t first = exeBase + kDescriptorsRva;
    if (descriptor < first || (descriptor - first) % kDescriptorStride != 0) return -1;
    const std::uintptr_t type = (descriptor - first) / kDescriptorStride;
    return type < ORB_TYPES ? static_cast<int>(type) : -1;
}

// Host: one ID per orb, unique through the room. Each orb manager the room sees is a match (1, 2, ...): a
// replaced manager takes its orbs with it, and the IDs carry on, so the room's ledger never sees one twice.
// The manager is observed every frame (Observe): one seen gone ends its match, so the next manager is the
// next match even at the same address (A, 0, A). A loss and return within one frame is not seen.
class HostBook {
    struct Entry { std::uintptr_t record; std::uint16_t id; };
    std::uintptr_t manager_ = 0;
    std::uint16_t last_ = 0, match_ = 0;
    bool exhausted_ = false;
    std::vector<Entry> live_;
public:
    struct Orb { std::uint16_t id = 0, match = 0; };
    void Reset() { manager_ = 0; last_ = 0; match_ = 0; exhausted_ = false; live_.clear(); }  // the room ended
    std::size_t LiveCount() const { return live_.size(); }
    std::uint16_t Match() const { return match_; }
    void Observe(std::uintptr_t manager) {
        if (!manager && manager_) { live_.clear(); manager_ = 0; }
    }
    // The orb's ID and match; {0, 0} without a manager or record, or once the room has used 65535 IDs.
    Orb Birth(std::uintptr_t manager, std::uintptr_t record) {
        if (!manager || !record) return {};
        if (manager != manager_) {
            live_.clear();
            if (match_ == UINT16_MAX) exhausted_ = true; else ++match_;
            manager_ = manager;
        }
        // A record is reused only after its orb was freed; an earlier ID on it is gone.
        std::erase_if(live_, [record](const Entry& l) { return l.record == record; });
        if (exhausted_ || last_ == UINT16_MAX) { exhausted_ = true; return {}; }
        live_.push_back({record, ++last_});
        return {last_, match_};
    }
    // The collected orb, or {0, 0} when the record is not a live orb of the current manager.
    Orb Collect(std::uintptr_t manager, std::uintptr_t record) {
        if (!manager || manager != manager_) return {};
        for (auto it = live_.begin(); it != live_.end(); ++it)
            if (it->record == record) { const Orb o{it->id, match_}; live_.erase(it); return o; }
        return {};
    }
};

// Friend: the host's orbs, applied in the host's order. A birth creates a display orb unless the orb was
// already collected; a collection ends the display orb once, or (before its birth applied) keeps it from
// ever being created. A birth whose collection is already queued is never shown. Rows wait while this game
// has no orb manager. Each of this game's managers shows one host match: the first shows the newest match
// queued when it appears, each replacement the next. Rows of an earlier match are dropped (their orbs went
// with the old manager); rows of a later match wait for this game's next manager. The manager is observed
// every frame, rows or not: a manager seen gone retires every record receipt at once, and the next manager
// is the next match even at the same address (A, 0, A). A manager replaced before any row assigned a match
// leaves this game unaligned: no row says which host match the new manager shows (a delayed old row is not
// proof), so every orb row is refused until the room ends. The score is the host's either way; this friend
// then shows no orbs for the rest of that room.
struct CreateRequest { std::uint16_t id; std::uint8_t type; std::array<float, 4> point; std::array<float, 3> velocity; };
struct VanishRequest { std::uint16_t id; std::uint8_t type; std::uintptr_t record; OrbCollector collector; std::uint16_t netId; };
// elided: born and collected before this game showed it (a late join, or a burst within one frame).
// stale: a row of a match this game's manager no longer shows. refused: a row taken while unaligned.
struct DisplayStats { std::uint64_t created{}, vanished{}, elided{}, skipped{}, stale{}, refused{}, createFailed{}, vanishFailed{}, reset{}; };

class ClientDisplay {
    // Shown: a display orb is out. Gone: shown and collected, refused, or seen twice. Elided: born after its
    // collection was already queued, never shown. Early: collected before its birth arrived.
    enum class State : std::uint8_t { Shown, Gone, Elided, Early };
    struct Orb { std::uint16_t id; std::uint8_t type; std::uintptr_t record; State state; };
    std::uintptr_t manager_ = 0;
    bool lost_ = false;          // a manager was seen gone since the last one
    bool unaligned_ = false;     // replaced before any match was assigned: rows refused until the room ends
    std::uint16_t showing_ = 0;  // the host match this game's manager shows; 0 until assigned
    std::vector<SpawnEmission> queue_;
    std::vector<Orb> orbs_;
    DisplayStats stats_{};
    Orb* Find(std::uint16_t id) {
        for (auto& o : orbs_) if (o.id == id) return &o;
        return nullptr;
    }
public:
    static constexpr std::size_t kPerTick = 64;  // native calls per game frame
    void Reset() {  // the room ended
        manager_ = 0; lost_ = false; unaligned_ = false; showing_ = 0; queue_.clear(); orbs_.clear(); ++stats_.reset;
    }
    bool Unaligned() const { return unaligned_; }
    // Every frame, rows or not.
    void Observe(std::uintptr_t manager) {
        if (manager == manager_) return;
        if (!manager) {  // gone: its orbs and their records went with it
            orbs_.clear(); manager_ = 0; lost_ = true; ++stats_.reset;
            return;
        }
        // A different manager, or any manager after one was lost, is a different match.
        if (manager_ || lost_) {
            ++stats_.reset;
            if (!showing_) unaligned_ = true;
            else if (showing_ < UINT16_MAX) ++showing_;
        }
        manager_ = manager; lost_ = false; orbs_.clear();
    }
    const DisplayStats& Stats() const { return stats_; }
    std::size_t Queued() const { return queue_.size(); }
    std::uint16_t Showing() const { return showing_; }
    std::size_t Shown() const {
        std::size_t n = 0;
        for (const auto& o : orbs_) n += o.state == State::Shown;
        return n;
    }
    void Receive(const std::vector<SpawnEmission>& rows) {
        for (const auto& r : rows)
            if (r.kind != SpawnEmissionKind::Controller && r.orbId && r.orbMatch) queue_.push_back(r);
    }
    // create(request) -> record (0: the allocator refused); vanish(request) -> ended.
    template <class Create, class Vanish>
    void Tick(std::uintptr_t manager, Create&& create, Vanish&& vanish) {
        Observe(manager);
        if (!manager) return;  // rows wait for this game's match
        if (unaligned_) { stats_.refused += queue_.size(); queue_.clear(); return; }
        if (!showing_) {
            for (const auto& row : queue_) showing_ = (std::max)(showing_, row.orbMatch);
            if (!showing_) return;
        }
        std::vector<std::uint16_t> collecting;
        for (const auto& row : queue_)
            if (row.kind == SpawnEmissionKind::OrbCollect && row.orbMatch == showing_) collecting.push_back(row.orbId);
        std::size_t done = 0, used = 0;
        for (; used < queue_.size() && done < kPerTick; ++used) {
            const auto& row = queue_[used];
            if (row.orbMatch < showing_) { ++stats_.stale; continue; }
            if (row.orbMatch > showing_) break;  // the host's next match: wait for this game's next manager
            Orb* orb = Find(row.orbId);
            if (row.kind == SpawnEmissionKind::OrbBirth) {
                if (orb && orb->state == State::Early) { orb->state = State::Gone; ++stats_.elided; continue; }
                if (orb) { ++stats_.skipped; continue; }
                if (std::find(collecting.begin(), collecting.end(), row.orbId) != collecting.end()) {
                    orbs_.push_back({row.orbId, row.orbType, 0, State::Elided}); ++stats_.elided; continue;
                }
                ++done;
                const std::uintptr_t record =
                    create(CreateRequest{row.orbId, row.orbType, row.point, row.velocity});
                if (!record) { orbs_.push_back({row.orbId, row.orbType, 0, State::Gone}); ++stats_.createFailed; continue; }
                for (auto& o : orbs_) if (o.record == record) o.record = 0;  // freed and reused natively
                orbs_.push_back({row.orbId, row.orbType, record, State::Shown});
                ++stats_.created;
            } else {
                if (!orb) { orbs_.push_back({row.orbId, 0, 0, State::Early}); continue; }
                if (orb->state == State::Elided) { orb->state = State::Gone; continue; }
                if (orb->state != State::Shown) { ++stats_.skipped; continue; }
                orb->state = State::Gone;
                const std::uintptr_t record = orb->record;
                orb->record = 0;
                if (!record) { ++stats_.vanishFailed; continue; }
                ++done;
                if (vanish(VanishRequest{row.orbId, orb->type, record, row.collector, row.collectorNetId})) ++stats_.vanished;
                else ++stats_.vanishFailed;
            }
        }
        queue_.erase(queue_.begin(), queue_.begin() + static_cast<std::ptrdiff_t>(used));
    }
};

}  // namespace kh2coop::inject::struggleorbs
