#pragma once
// VUH-2912 witness diagnostics (lead #76, run vuh2912-hp-09): per-actor counts of the reaction applier 0x3D6FC0's
// calls, so a hit's witness can say whether the hook ran at all in its window, ran only on other actors, ran on the
// victim without a request, or applied the victim's reaction before the witness armed (lastApplied at or before the
// hit frame). Pure bookkeeping, no game reads: EntityHook.cpp feeds it from the hook and snapshots it per witness.
#include <cstdint>

namespace kh2coop::inject::reactioncalls {

constexpr unsigned kRows = 16;
constexpr std::uint32_t kNone = UINT32_MAX;

struct Row {
    std::uintptr_t actor = 0;
    std::uint32_t calls = 0;        // every call on this actor
    std::uint32_t requests = 0;     // calls whose request id read nonzero (or could not be read)
    std::uint32_t applied = 0;      // calls that returned an entry
    std::uint32_t lastCallFrame = 0;
    std::uint32_t lastAppliedFrame = kNone;
    std::int32_t lastKind = -1;     // the last applied entry's kind (+2), -1 unread
    std::uint32_t lastRequest = kNone;
};

struct Table {
    Row rows[kRows] {};
    std::uint32_t all = 0;          // every call on any actor
    std::uint32_t evicted = 0;      // rows reused for a new actor (the least recently called one)
};

inline const Row* Find(const Table& t, std::uintptr_t actor) noexcept {
    for (const auto& r : t.rows)
        if (actor && r.actor == actor) return &r;
    return nullptr;
}

// One call: `request` is the id read before the call (kNone when unread), `applied` whether it returned an entry and
// `kind` that entry's kind (-1 when none or unread).
inline void Note(Table& t, std::uintptr_t actor, std::uint32_t request, bool applied, std::int32_t kind,
                 std::uint32_t frame) noexcept {
    ++t.all;
    if (!actor) return;
    Row* row = nullptr;
    for (auto& r : t.rows) {
        if (r.actor == actor) { row = &r; break; }
    }
    if (!row) {
        for (auto& r : t.rows) {
            if (!r.actor) { row = &r; break; }
        }
    }
    if (!row) {
        row = &t.rows[0];
        for (auto& r : t.rows)
            if (r.lastCallFrame < row->lastCallFrame) row = &r;
        ++t.evicted;
    }
    if (row->actor != actor) *row = Row{actor};
    ++row->calls;
    if (request != 0) ++row->requests;
    row->lastCallFrame = frame;
    if (applied) {
        ++row->applied;
        row->lastAppliedFrame = frame;
        row->lastKind = kind;
        row->lastRequest = request;
    }
}

// What a witness keeps when it arms, and what it logs: the calls since then, on any actor and on its victim.
struct Snapshot {
    std::uint32_t all = 0, calls = 0, requests = 0, applied = 0;
};

inline Snapshot Take(const Table& t, std::uintptr_t victim) noexcept {
    const Row* r = Find(t, victim);
    return {t.all, r ? r->calls : 0, r ? r->requests : 0, r ? r->applied : 0};
}

// The calls between `at` and now. A victim row reused for another actor in between restarts its counts, so the
// window then counts from zero rather than going negative.
inline Snapshot Since(const Table& t, std::uintptr_t victim, const Snapshot& at) noexcept {
    const Snapshot now = Take(t, victim);
    const auto delta = [](std::uint32_t a, std::uint32_t b) { return a >= b ? a - b : a; };
    return {now.all - at.all, delta(now.calls, at.calls), delta(now.requests, at.requests),
            delta(now.applied, at.applied)};
}

} // namespace kh2coop::inject::reactioncalls
