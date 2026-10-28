// VUH-2912 (lead #76, run vuh2912-hp-09) offline controls for inject/src/ReactionCalls.hpp, the per-actor counts of
// 0x3D6FC0's calls that each hit witness snapshots, and inject/src/StatsMark.hpp, the runner's request to log the
// mirror's stats at a mark. Pure bookkeeping only; no game process.
#include "ReactionCalls.hpp"
#include "StatsMark.hpp"

#include <cstddef>
#include <cstdio>

using namespace kh2coop::inject;
namespace rc = kh2coop::inject::reactioncalls;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

constexpr std::uintptr_t kVictim = 0x7FF7ABE7E050, kAxel = 0x7FF7ABE98DA0;

int main() {
    // Every call counts on its actor; a request is any id but 0 (an unread id counts too); applied keeps the frame,
    // kind and request of the last entry.
    {
        rc::Table t;
        rc::Note(t, kVictim, 0, false, -1, 10);
        rc::Note(t, kVictim, 7, true, 2, 11);
        rc::Note(t, kVictim, rc::kNone, false, -1, 12);
        rc::Note(t, kAxel, 0, false, -1, 12);
        const rc::Row* v = rc::Find(t, kVictim);
        CHECK(v && v->calls == 3 && v->requests == 2 && v->applied == 1);
        CHECK(v && v->lastAppliedFrame == 11 && v->lastKind == 2 && v->lastRequest == 7 && v->lastCallFrame == 12);
        CHECK(t.all == 4);
        CHECK(rc::Find(t, kAxel) && rc::Find(t, kAxel)->calls == 1);
        CHECK(!rc::Find(t, 0x1234));
        CHECK(!rc::Find(t, 0));
    }
    // Never applied: no frame, kind or request.
    {
        rc::Table t;
        rc::Note(t, kVictim, 0, false, -1, 5);
        const rc::Row* v = rc::Find(t, kVictim);
        CHECK(v && v->lastAppliedFrame == rc::kNone && v->lastKind == -1 && v->lastRequest == rc::kNone);
    }
    // A witness window: what arrived after the snapshot, on any actor and on the victim.
    {
        rc::Table t;
        rc::Note(t, kVictim, 7, true, 2, 100);   // applied in the hit frame, before the witness arms
        const rc::Snapshot armed = rc::Take(t, kVictim);
        CHECK(armed.all == 1 && armed.calls == 1 && armed.applied == 1);
        rc::Note(t, kAxel, 0, false, -1, 101);
        rc::Note(t, kVictim, 0, false, -1, 101);
        rc::Note(t, kVictim, 0, false, -1, 102);
        const rc::Snapshot w = rc::Since(t, kVictim, armed);
        CHECK(w.all == 3 && w.calls == 2 && w.requests == 0 && w.applied == 0);
        CHECK(rc::Find(t, kVictim)->lastAppliedFrame == 100);  // the applied call before arming stays visible
    }
    // Hook never ran in the window, or ran only on others.
    {
        rc::Table t;
        const rc::Snapshot armed = rc::Take(t, kVictim);
        CHECK(armed.all == 0 && armed.calls == 0);
        CHECK(rc::Since(t, kVictim, armed).all == 0);
        rc::Note(t, kAxel, 0, false, -1, 1);
        const rc::Snapshot w = rc::Since(t, kVictim, armed);
        CHECK(w.all == 1 && w.calls == 0);
    }
    // A full table reuses the least recently called row and counts it; the victim kept busy stays.
    {
        rc::Table t;
        for (unsigned i = 0; i < rc::kRows; ++i) rc::Note(t, 0x1000 + i, 0, false, -1, i);
        rc::Note(t, 0x1000, 0, false, -1, 50);   // the first row is now the most recent
        rc::Note(t, kVictim, 0, false, -1, 51);  // evicts 0x1001, called at frame 1
        CHECK(t.evicted == 1);
        CHECK(rc::Find(t, kVictim) && !rc::Find(t, 0x1001) && rc::Find(t, 0x1000));
        CHECK(rc::Find(t, kVictim)->calls == 1);
    }
    // A victim row reused for another actor during the window restarts its counts instead of going negative.
    {
        rc::Table t;
        for (int i = 0; i < 3; ++i) rc::Note(t, kVictim, 0, false, -1, 1);
        const rc::Snapshot armed = rc::Take(t, kVictim);
        rc::Row* row = const_cast<rc::Row*>(rc::Find(t, kVictim));
        *row = rc::Row{kVictim};
        row->calls = 1;
        CHECK(rc::Since(t, kVictim, armed).calls == 1);
    }
    // Null actors count toward all, never a row.
    {
        rc::Table t;
        rc::Note(t, 0, 0, false, -1, 1);
        CHECK(t.all == 1 && !rc::Find(t, 0) && t.evicted == 0);
    }

    // Stats marks: the layout run.py writes, and one log per new request.
    {
        static_assert(offsetof(statsmark::Channel, magic) == 0, "magic");
        static_assert(offsetof(statsmark::Channel, version) == 4, "version");
        static_assert(offsetof(statsmark::Channel, requested) == 8, "requested");
        static_assert(offsetof(statsmark::Channel, logged) == 12, "logged");
        statsmark::Channel c {statsmark::kMagic, statsmark::kVersion, 0, 0};
        CHECK(!statsmark::Due(c, 0));
        c.requested = 1;
        CHECK(statsmark::Due(c, 0));
        CHECK(!statsmark::Due(c, 1));
        c.magic = 0;
        CHECK(!statsmark::Due(c, 0));
    }

    if (failures) std::fprintf(stderr, "%d failure(s)\n", failures);
    else std::printf("reaction calls and stats marks: all checks passed\n");
    return failures ? 1 : 0;
}
