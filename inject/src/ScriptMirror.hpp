#pragma once
// VUH-2252 (D14 item 3): mirror the host's script calls on its mirrored enemies.
// A client runs no brain for a mirrored enemy (EnemyMirror.inl), so the calls its
// script makes on itself never happen there: a tilt, a texture animation, a shadow.
// The host sees each one at the bdscript trap table and sends it in a ScriptCall
// only when its value changed; the client replays it on its own copy.
// Pure rules only: no game memory, no Windows calls. The native adapter is
// ScriptMirror.inl (included inside EntityHook's namespace). See docs/SCRIPT_MIRROR.md.
//
// Each trap here was decompiled (Steam 9002b2de): it reads only its argument array
// (a u64 per argument, the handle in the low word), decodes the actor through
// 0x4AD270 twice (argument, then its +4), and calls or writes once. None reads the
// running script thread (exe+0x2A250B0) or its VM, so it can run from frame code.
// Trap names are OpenKH's BdxTraps.cs; the call list follows TCM-Murray's
// world-mirror research (GPL-3.0, docs/research/PRIOR_ART.md). No code is copied.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace kh2coop::inject::scriptmirror::rules {

inline constexpr std::uint32_t kTagAddress = 0x52444140;  // '@ADR' (a handle)
inline constexpr std::uint32_t kTagInt = 0x544E4940;      // '@INT'
inline constexpr std::uint32_t kTagFloat = 0x544C4640;    // '@FLT'
inline constexpr std::uintptr_t kTrapTables = 0x753490;   // *(exe + this + 8 * table) = table, 16-byte entries {fn, flags}

// How the client replays a call: through the original trap with its own actor's handle, or
// (a vector argument, which only a script-stack handle can carry) through the trap's helper.
enum class Replay : std::uint8_t { Trap, XyzRotHelper };
// Calls in one group overwrite each other's effect, so only the latest value per group counts.
enum class Group : std::uint8_t { Rotation, Texanm, Shadow, Scissoring };

struct Trap {
    std::uint8_t table;
    std::uint16_t index;
    std::uint32_t fnRva;       // the table entry's native function
    std::uint32_t flags;       // the entry's flags: low 16 bits = script argument count (actor included)
    std::uint8_t values;       // values sent after the actor (a vector sends its 4 floats)
    bool vector;               // the one argument after the actor is a float4 handle
    Replay replay;
    Group group;
    bool keyedByValue;         // the first value names what it acts on (texanm id): one key per value
    const char* name;
};
inline constexpr Trap kTraps[] = {
    {1, 1, 0x42BF60, 0x00000002, 1, false, Replay::Trap, Group::Rotation, false, "obj_set_rot"},  // 0x3B6560(actor, angle)
    {8, 2, 0x537E70, 0x00000002, 4, true, Replay::XyzRotHelper, Group::Rotation, false, "obj_set_xyzrot"}, // 0x3B65F0(actor, float4*)
    {1, 85, 0x42F820, 0x00000002, 1, false, Replay::Trap, Group::Texanm, true, "obj_texanm_start"},  // 0x3CD8A0(actor, id)
    {1, 86, 0x42F850, 0x00000002, 1, false, Replay::Trap, Group::Texanm, true, "obj_texanm_stop"},   // 0x3CD8B0(actor, id)
    {6, 45, 0x53E960, 0x00000001, 0, false, Replay::Trap, Group::Shadow, false, "obj_hide_shadow"},  // actor+0x124 |= 0x200
    {6, 64, 0x53EE90, 0x00000001, 0, false, Replay::Trap, Group::Shadow, false, "obj_show_shadow"},  // 0x3CD820(actor)
    {6, 65, 0x53EEB0, 0x00000002, 1, false, Replay::Trap, Group::Scissoring, false, "obj_set_scissoring"}, // actor+0x8C8 bit 0x20
};
inline constexpr std::size_t kTrapCount = sizeof(kTraps) / sizeof(kTraps[0]);
inline int FindTrap(std::uint8_t table, std::uint16_t index) noexcept {
    for (std::size_t i = 0; i < kTrapCount; ++i)
        if (kTraps[i].table == table && kTraps[i].index == index) return static_cast<int>(i);
    return -1;
}
inline std::uintptr_t EntryRva(std::uintptr_t tableRva, std::uint16_t index) noexcept { return tableRva + 16u * index; }

// Exact function bodies (Steam 9002b2de); a slot is swapped or replayed only after a match.
inline constexpr std::uint8_t kObjSetRot[49] = {
    0x48, 0x83, 0xEC, 0x38, 0x0F, 0x29, 0x74, 0x24, 0x20, 0xF3, 0x0F, 0x10, 0x71, 0x08, 0x8B, 0x09, 0xE8, 0xFB, 0x12, 0x08,
    0x00, 0x8B, 0x48, 0x04, 0xE8, 0xF3, 0x12, 0x08, 0x00, 0x48, 0x8B, 0xC8, 0x0F, 0x28, 0xCE, 0x0F, 0x28, 0x74, 0x24, 0x20,
    0x48, 0x83, 0xC4, 0x38, 0xE9, 0xCF, 0xA5, 0xF8, 0xFF};
inline constexpr std::uint8_t kObjSetXyzRot[124] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20,
    0x48, 0x8B, 0xF1, 0x8B, 0x49, 0x08, 0xE8, 0xE1, 0x53, 0xF7, 0xFF, 0x33, 0xED, 0x48, 0x8B, 0xF8, 0x8B, 0xDD, 0x66, 0x66,
    0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x10, 0x04, 0x9F, 0x0F, 0x5A, 0xC0, 0xFF, 0x15, 0x82, 0x39,
    0x04, 0x00, 0x85, 0xC0, 0x74, 0x03, 0x89, 0x2C, 0x9F, 0x48, 0xFF, 0xC3, 0x48, 0x83, 0xFB, 0x04, 0x7C, 0xE2, 0x8B, 0x0E,
    0xE8, 0xAB, 0x53, 0xF7, 0xFF, 0x8B, 0x48, 0x04, 0xE8, 0xA3, 0x53, 0xF7, 0xFF, 0x48, 0x8B, 0xC8, 0x48, 0x8B, 0xD7, 0x48,
    0x8B, 0x5C, 0x24, 0x30, 0x48, 0x8B, 0x6C, 0x24, 0x38, 0x48, 0x8B, 0x74, 0x24, 0x40, 0x48, 0x83, 0xC4, 0x20, 0x5F, 0xE9,
    0x04, 0xE7, 0xE7, 0xFF};
inline constexpr std::uint8_t kTexanmStart[39] = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0x59, 0x08, 0x8B, 0x09, 0xE8, 0x40, 0xDA, 0x07, 0x00, 0x8B, 0x48, 0x04, 0xE8,
    0x38, 0xDA, 0x07, 0x00, 0x48, 0x8B, 0xC8, 0x8B, 0xD3, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xE9, 0x59, 0xE0, 0xF9, 0xFF};
inline constexpr std::uint8_t kTexanmStop[39] = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0x59, 0x08, 0x8B, 0x09, 0xE8, 0x10, 0xDA, 0x07, 0x00, 0x8B, 0x48, 0x04, 0xE8,
    0x08, 0xDA, 0x07, 0x00, 0x48, 0x8B, 0xC8, 0x8B, 0xD3, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xE9, 0x39, 0xE0, 0xF9, 0xFF};
inline constexpr std::uint8_t kHideShadow[34] = {
    0x48, 0x83, 0xEC, 0x28, 0x8B, 0x09, 0xE8, 0x05, 0xE9, 0xF6, 0xFF, 0x8B, 0x48, 0x04, 0xE8, 0xFD, 0xE8, 0xF6, 0xFF, 0x81,
    0x88, 0x24, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x48, 0x83, 0xC4, 0x28, 0xC3};
inline constexpr std::uint8_t kShowShadow[31] = {
    0x48, 0x83, 0xEC, 0x28, 0x8B, 0x09, 0xE8, 0xD5, 0xE3, 0xF6, 0xFF, 0x8B, 0x48, 0x04, 0xE8, 0xCD, 0xE3, 0xF6, 0xFF, 0x48,
    0x8B, 0xC8, 0x48, 0x83, 0xC4, 0x28, 0xE9, 0x71, 0xE9, 0xE8, 0xFF};
inline constexpr std::uint8_t kSetScissoring[54] = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0x59, 0x08, 0x8B, 0x09, 0xE8, 0xB0, 0xE3, 0xF6, 0xFF, 0x8B, 0x48, 0x04, 0xE8,
    0xA8, 0xE3, 0xF6, 0xFF, 0x85, 0xDB, 0x74, 0x0D, 0x83, 0x88, 0xC8, 0x08, 0x00, 0x00, 0x20, 0x48, 0x83, 0xC4, 0x20, 0x5B,
    0xC3, 0x83, 0xA0, 0xC8, 0x08, 0x00, 0x00, 0xDF, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3};
// 0x3B65F0(actor, const float4*): copies the vector to actor+0x680, its y to +0x68C, sets bit 3 of +0x9B8.
inline constexpr std::uintptr_t kXyzRotHelperRva = 0x3B65F0;
inline constexpr std::uint8_t kXyzRotHelper[55] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF9, 0x48, 0x8B, 0xDA, 0x48, 0x81, 0xC1, 0x80,
    0x06, 0x00, 0x00, 0xE8, 0x54, 0x28, 0xDF, 0xFF, 0x8B, 0x43, 0x04, 0x48, 0x8B, 0x5C, 0x24, 0x30, 0x89, 0x87, 0x8C, 0x06,
    0x00, 0x00, 0x83, 0x8F, 0xB8, 0x09, 0x00, 0x00, 0x08, 0x48, 0x83, 0xC4, 0x20, 0x5F, 0xC3};
struct Body { const std::uint8_t* bytes; std::size_t size; };
inline constexpr Body kBodies[kTrapCount] = {
    {kObjSetRot, sizeof(kObjSetRot)}, {kObjSetXyzRot, sizeof(kObjSetXyzRot)}, {kTexanmStart, sizeof(kTexanmStart)},
    {kTexanmStop, sizeof(kTexanmStop)}, {kHideShadow, sizeof(kHideShadow)}, {kShowShadow, sizeof(kShowShadow)},
    {kSetScissoring, sizeof(kSetScissoring)}};
inline bool BodyMatches(std::size_t trap, const std::uint8_t* bytes) noexcept {
    return trap < kTrapCount && std::memcmp(bytes, kBodies[trap].bytes, kBodies[trap].size) == 0;
}

inline std::uint64_t Tagged(std::uint32_t tag, std::uint32_t bits) noexcept {
    return (static_cast<std::uint64_t>(tag) << 32) | bits;
}
inline std::uint32_t TagOf(std::uint64_t v) noexcept { return static_cast<std::uint32_t>(v >> 32); }
inline std::uint32_t BitsOf(std::uint64_t v) noexcept { return static_cast<std::uint32_t>(v); }
inline float FloatOf(std::uint64_t v) noexcept { float f; const auto b = BitsOf(v); std::memcpy(&f, &b, sizeof(f)); return f; }

// The wire codec's value rules (Protocol.hpp validScriptCallEntry): every value the trap uses is
// '@INT' or a finite '@FLT' within kMaxFloat (a vector: four '@FLT'), every unused one is zero.
inline constexpr float kMaxFloat = 100000.0f;
inline bool ValidValues(std::size_t trap, const std::uint64_t* values) noexcept {
    if (trap >= kTrapCount) return false;
    const auto& t = kTraps[trap];
    for (unsigned k = 0; k < 4; ++k) {
        if (k >= t.values) { if (values[k] != 0) return false; continue; }
        const auto tag = TagOf(values[k]);
        if (tag == kTagFloat) {
            const float f = FloatOf(values[k]);
            if (!std::isfinite(f) || std::fabs(f) > kMaxFloat) return false;
        } else if (tag != kTagInt || t.vector) return false;
    }
    return true;
}

// What one observed call says, already reduced to the wire values (tagged scalars or a float4).
struct Call {
    std::uint16_t netId = 0;
    std::uint32_t objectId = 0;
    std::uint8_t trap = 0;         // index into kTraps
    std::uint32_t effect = 0;      // host effect key (effect calls, next slice); 0 for actor calls
    std::uint64_t values[4] {};
};
inline bool SameValue(const Call& a, const Call& b) noexcept {
    return a.trap == b.trap && a.effect == b.effect && std::memcmp(a.values, b.values, sizeof(a.values)) == 0;
}
// netId, group and (texanm) the id the call acts on.
inline std::uint64_t KeyOf(const Call& c) noexcept {
    const auto& t = kTraps[c.trap];
    const std::uint32_t sub = t.keyedByValue ? BitsOf(c.values[0]) : 0;
    return (static_cast<std::uint64_t>(c.netId) << 48) | (static_cast<std::uint64_t>(t.group) << 40) | sub;
}

// Host: send on change. A call whose value equals the last sent (or still queued) one for its key
// is a repeat and costs nothing on the wire; a changed value replaces a still-queued one for the
// same key; everything else queues in call order. Sent values become the new baseline only when
// the packet carrying them was accepted for sending. Reset on every room or session change.
class Sender {
public:
    enum class Outcome : std::uint8_t { Queued, Replaced, Repeat, Full };
    static constexpr std::size_t kPendingCap = 256, kKeyCap = 4096;

    Outcome Observe(const Call& c) {
        const auto key = KeyOf(c);
        for (auto& p : pending_) {
            if (KeyOf(p) != key) continue;
            if (SameValue(p, c)) return Outcome::Repeat;
            p = c;
            return Outcome::Replaced;
        }
        const auto found = sent_.find(key);
        if (found != sent_.end() && SameValue(found->second, c)) return Outcome::Repeat;
        if (pending_.size() >= kPendingCap) return Outcome::Full;
        pending_.push_back(c);
        return Outcome::Queued;
    }
    // The oldest queued calls, at most max.
    std::size_t Peek(Call* out, std::size_t max) const noexcept {
        const std::size_t n = pending_.size() < max ? pending_.size() : max;
        for (std::size_t i = 0; i < n; ++i) out[i] = pending_[i];
        return n;
    }
    // The first n queued calls were accepted for sending: they become the baseline.
    void Commit(std::size_t n) {
        if (n > pending_.size()) n = pending_.size();
        for (std::size_t i = 0; i < n; ++i) {
            if (sent_.size() >= kKeyCap && !sent_.count(KeyOf(pending_[i]))) sent_.clear();  // bounded; resends are harmless
            sent_[KeyOf(pending_[i])] = pending_[i];
        }
        pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(n));
    }
    void Reset() { pending_.clear(); sent_.clear(); }
    std::size_t Pending() const noexcept { return pending_.size(); }

private:
    std::vector<Call> pending_;
    std::unordered_map<std::uint64_t, Call> sent_;
};

// Client: a call for a netId whose copy is not driven yet (spawn race) waits here, latest value per
// key, and is replayed once the copy is driven; it is dropped after kHoldFrames.
class Hold {
public:
    static constexpr std::size_t kCap = 64;
    static constexpr std::uint32_t kHoldFrames = 180;
    struct Item { Call call; std::uint32_t frame; };

    bool Put(const Call& c, std::uint32_t frame) {
        const auto key = KeyOf(c);
        for (auto& i : items_) if (KeyOf(i.call) == key) { i = {c, frame}; return true; }
        if (items_.size() >= kCap) return false;
        items_.push_back({c, frame});
        return true;
    }
    // Drops expired items; hands the rest to ready(call) in arrival order and removes those it took.
    template <typename Ready> std::size_t Drain(std::uint32_t frame, Ready&& ready, std::size_t& expired) {
        std::size_t taken = 0;
        expired = 0;
        for (std::size_t i = 0; i < items_.size();) {
            if (frame - items_[i].frame > kHoldFrames) { items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(i)); ++expired; continue; }
            if (ready(items_[i].call)) { items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(i)); ++taken; continue; }
            ++i;
        }
        return taken;
    }
    void Erase(std::uint64_t key) {
        for (std::size_t i = 0; i < items_.size(); ++i)
            if (KeyOf(items_[i].call) == key) { items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(i)); return; }
    }
    void Reset() { items_.clear(); }
    std::size_t Size() const noexcept { return items_.size(); }

private:
    std::vector<Item> items_;
};

// Per-second counters (both roles); a line is logged for every second with any activity.
struct Second {
    std::uint32_t calls = 0, unmapped = 0, notMirrored = 0, repeats = 0, replaced = 0, queued = 0, full = 0;
    std::uint32_t packets = 0, records = 0, sendFailed = 0;           // host
    std::uint32_t received = 0, applied = 0, held = 0, heldApplied = 0, expired = 0, refused = 0; // client
    std::uint32_t perTrap[kTrapCount] {};                             // host calls / client records by trap
    bool Any() const noexcept {
        return calls || unmapped || notMirrored || packets || received || refused || expired || held;
    }
};

} // namespace kh2coop::inject::scriptmirror::rules
