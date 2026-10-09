// VUH-1788: the pure client population planner and cull decision
// (inject/src/EnemyPopulation.hpp). No game, no hooks.
#include "EnemyPopulation.hpp"
#include <cstdio>
#include <vector>

using namespace kh2coop::inject::enemypop;
static int g_fail = 0, g_checks = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

static HostView Missing(std::uint16_t id) {
    HostView v;
    v.netId = id; v.objectId = 302; v.allowed = true; v.loadedObject = true; v.streamFresh = true;
    return v;
}
static Identity Id(std::uintptr_t a) { return {a, a + 0x10, a + 0x20}; }

int main() {
    Planner p;
    p.Rebase(1);
    std::vector<HostView> hosts {Missing(6)};
    CHECK(p.Plan(hosts, 1000, true) == 0);                       // starts the missing clock
    CHECK(p.Plan(hosts, 1000 + kMissingFrames - 1, true) == 0);  // not yet
    CHECK(p.Plan(hosts, 1000 + kMissingFrames, true) == 6);      // missing long enough
    CHECK(p.Plan(hosts, 1000 + kMissingFrames, false) == 0);     // never while unsafe
    // Bound, dead, unallowlisted, unloaded object (C5), stale or already-forced: never picked, clock reset.
    for (int k = 0; k < 6; ++k) {
        Planner q; q.Rebase(1);
        HostView v = Missing(7);
        if (k == 0) v.boundLocally = true;
        if (k == 1) v.dead = true;
        if (k == 2) v.allowed = false;
        if (k == 3) v.streamFresh = false;
        if (k == 4) v.forcedPresent = true;
        if (k == 5) v.loadedObject = false;
        std::vector<HostView> one {v};
        (void)q.Plan(one, 10, true);
        CHECK(q.Plan(one, 10 + kMissingFrames, true) == 0);
    }
    Planner r; r.Rebase(1);
    std::vector<HostView> flap {Missing(8)};
    (void)r.Plan(flap, 100, true);
    flap[0].boundLocally = true;
    (void)r.Plan(flap, 150, true);
    flap[0].boundLocally = false;
    (void)r.Plan(flap, 160, true);
    CHECK(r.Plan(flap, 100 + kMissingFrames, true) == 0 && r.Plan(flap, 160 + kMissingFrames, true) == 8);
    // A blocked Shadow must not starve a cheaper already-loaded family.
    Planner budget; budget.Rebase(1);
    std::vector<HostView> admission {Missing(7), Missing(8)};
    admission[0].admissionAvailable = false;
    admission[1].objectId = 4;
    (void)budget.Plan(admission, 10, true);
    CHECK(budget.Plan(admission, 10 + kMissingFrames, true) == 8);
    CHECK(budget.forcedCount() == 0); // budget waits are not constructor attempts
    admission[0].admissionAvailable = true;
    admission[1].boundLocally = true;
    CHECK(budget.Plan(admission, 11 + kMissingFrames, true) == 7); // missing clock retained
    admission[0].admissionAvailable = false;
    CHECK(budget.Plan(admission, 12 + kMissingFrames, true) == 0);
    admission[0].admissionAvailable = true;
    admission[0].loadedObject = false;
    CHECK(budget.Plan(admission, 13 + kMissingFrames, true) == 0); // never bypass C5
    // Attempts: identity-matched (C1), not by address alone.
    p.Attempted(6, Id(0xABC000), 1200);
    CHECK(p.forcedCount() == 1 && p.ForcedFor(Id(0xABC000)) && p.ForcedFor(Id(0xABC000))->netId == 6);
    CHECK(!p.ForcedFor(Identity {0xABC000, 0x1, 0x2}) && !p.ForcedFor(Identity {}));  // reused address, other objentry/status
    std::vector<HostView> two {Missing(6), Missing(9)};
    two[0].forcedPresent = true;
    (void)p.Plan(two, 1201, true);
    CHECK(p.Plan(two, 1201 + kMissingFrames, true) == 9);
    p.Attempted(9, Identity {}, 1201 + kMissingFrames);                      // native refusal (null)
    CHECK(p.forcedCount() == 1);
    CHECK(p.Plan(two, 1201 + kMissingFrames + kSpawnGap - 1, true) == 0);
    CHECK(p.Plan(two, 1201 + kMissingFrames + kRetryFrames - 1, true) == 0);
    CHECK(p.Plan(two, 1201 + kMissingFrames + kRetryFrames, true) == 9);
    // Two missing netIds: after one forced spawn the other waits the spawn gap.
    Planner gp; gp.Rebase(1);
    std::vector<HostView> pair {Missing(11), Missing(12)};
    (void)gp.Plan(pair, 5, true);
    CHECK(gp.Plan(pair, 5 + kMissingFrames, true) == 11);
    gp.Attempted(11, Id(0xB000), 5 + kMissingFrames);
    pair[0].forcedPresent = true;
    CHECK(gp.Plan(pair, 5 + kMissingFrames + 1, true) == 0);
    CHECK(gp.Plan(pair, 5 + kMissingFrames + kSpawnGap, true) == 12);
    // At most kMaxAttempts per netId.
    Planner a; a.Rebase(1);
    std::vector<HostView> h {Missing(4)};
    std::uint32_t f = 10;
    (void)a.Plan(h, f, true);
    int picks = 0;
    for (int i = 0; i < 10; ++i) {
        f += kRetryFrames + kMissingFrames;
        (void)a.Plan(h, f - kMissingFrames, true);
        if (a.Plan(h, f, true) == 4) { ++picks; a.Attempted(4, Identity {}, f); }
    }
    CHECK(picks == kMaxAttempts);
    // At most kMaxForced per epoch.
    Planner m; m.Rebase(1);
    std::vector<HostView> many;
    for (std::uint16_t id = 1; id <= 12; ++id) many.push_back(Missing(id));
    std::uint32_t g = 1;
    (void)m.Plan(many, g, true);
    g += kMissingFrames;
    int made = 0;
    for (int i = 0; i < 20; ++i, g += kSpawnGap) {
        const auto id = m.Plan(many, g, true);
        if (id) { m.Attempted(id, Id(0x1000u * (made + 1)), g); ++made; many[id - 1].forcedPresent = true; }
    }
    CHECK(made == static_cast<int>(kMaxForced) && m.forcedCount() == kMaxForced);
    m.Forget(0x1000);
    CHECK(!m.ForcedFor(Id(0x1000)) && m.ForcedFor(Id(0x2000)));
    // C1: Rebase (epoch change / retire) keeps forced entries with their old epoch; Clear (own load) drops them.
    m.Rebase(2);
    const Forced* kept = m.ForcedFor(Id(0x2000));
    CHECK(m.epoch() == 2 && m.forcedCount() == 0 && kept && kept->epoch == 1);
    CHECK(kept && ForceRemove(*kept, true, false, 2));  // its epoch moved: removed
    const auto gen = m.generation();
    m.Clear();
    CHECK(!m.ForcedFor(Id(0x2000)) && m.epoch() == 0 && m.generation() == gen + 1);
    m.Rebase(3);
    CHECK(m.generation() == gen + 2);  // both bump the history generation (rev3 S2)
    // ForceRemove / ForcedHold / Yield (S1).
    Forced fc {6, Id(0xABC000), 1, 0, false, false};
    CHECK(!ForceRemove(fc, true, false, 1) && ForceRemove(fc, true, true, 1) && ForceRemove(fc, false, false, 1) &&
          ForceRemove(fc, true, false, 2) && !ForceRemove(Forced {}, false, true, 1));
    CHECK(ForcedHold(fc, true, false, 1) && !ForcedHold(fc, true, true, 1) && !ForcedHold(Forced {}, true, false, 1));
    Planner y; y.Rebase(1);
    y.Attempted(5, Id(0xD000), 10);
    y.MarkYield(0xD000);
    CHECK(y.NoteUnbound(0xD000) == 1 && y.NoteUnbound(0xD000) == 2 && y.NoteUnbound(0x9999) == 0);  // rev4 R2
    y.MarkBound(0xD000);
    CHECK(y.ForcedFor(Id(0xD000))->boundOnce);
    const Forced* yf = y.ForcedFor(Id(0xD000));
    CHECK(yf && yf->yield && ForceRemove(*yf, true, false, 1));
    // The factory admission (0x3A1F00): weight <= limit - used.
    CHECK(BudgetAllows(10.0f, 4.0f, 6) && !BudgetAllows(10.0f, 4.5f, 6) && BudgetAllows(1.0f, 0.0f, 0) && !BudgetAllows(0.0f, 0.0f, 1));
    // CullDecision(nativeRemove, forceRemove, recentlyBound, living, running, sinceDriven, forcedHold).
    CHECK(CullDecision(false, false, true, true, true, 0, false) == -1);                   // native keeps
    CHECK(CullDecision(true, false, true, true, true, 0, false) == 0);                     // driven: hold
    CHECK(CullDecision(true, false, true, true, false, kCullHoldFrames, false) == 0);      // recently driven
    CHECK(CullDecision(true, false, true, true, false, kCullHoldFrames + 1, false) == -1); // too long
    CHECK(CullDecision(true, false, true, false, true, 0, false) == -1);                   // dying
    CHECK(CullDecision(true, false, false, true, true, 0, false) == -1);                   // not bound recently
    CHECK(CullDecision(true, false, false, true, false, 0xFFFFFFFFu, true) == 0);          // forced copy (C2)
    CHECK(CullDecision(true, false, false, false, false, 0xFFFFFFFFu, true) == -1);        // forced but dying
    CHECK(CullDecision(false, true, false, false, false, 0, false) == 1);                  // forced removal
    std::printf("EnemyPopulationTest: %d checks, %s\n", g_checks, g_fail ? "FAIL" : "PASS");
    return g_fail ? 1 : 0;
}
