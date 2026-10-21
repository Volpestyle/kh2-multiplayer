// VUH-2632 (D17) offline controls for inject/src/DownedReturn.hpp: when a downed teammate who sits out
// comes back (a forced battle's end, a save point) and the once-per-episode latch. Pure rules.
#include "DownedReturn.hpp"

#include <cstdio>
#include <string_view>

using namespace kh2coop::inject::downedreturn;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

int main() {
    // A forced battle that ends in field play, in its room generation: one EncounterDefeat.
    { EncounterTracker t;
      CHECK(!t.Step(0, 1, 1, true));                       // no battle yet
      CHECK(!t.Step(2, 1, 1, true) && t.Forced());         // forced battle (waves)
      CHECK(!t.Step(2, 1, 1, true));                       // still fighting
      CHECK(t.Step(0, 1, 1, true));                        // cleared: the encounter is beaten
      CHECK(!t.Step(0, 1, 1, true)); }                     // once
    // An ordinary battle (1) ending is not an encounter defeat.
    { EncounterTracker t; CHECK(!t.Step(1, 1, 1, true)); CHECK(!t.Step(0, 1, 1, true)); }
    // A transition or load during a forced battle forgets it (a retry or a story handoff, not a win).
    { EncounterTracker t; (void)t.Step(2, 1, 1, true); CHECK(!t.Step(0, 2, 1, true));
      EncounterTracker u; (void)u.Step(2, 1, 1, true); CHECK(!u.Step(0, 1, 2, true)); }
    // Outside field play (an event, a menu, a downed host) the edge is not taken; it is taken once field
    // play resumes, still in the same room generation.
    { EncounterTracker t; (void)t.Step(2, 1, 1, true);
      CHECK(!t.Step(0, 1, 1, false));
      CHECK(t.Step(0, 1, 1, true)); }
    // Forced, then ordinary (1), then cleared: still the forced battle's end.
    { EncounterTracker t; (void)t.Step(2, 1, 1, true); CHECK(!t.Step(1, 1, 1, true)); CHECK(t.Step(0, 1, 1, true)); }

    // Save point: a living seat whose settled reaction is the save point's "Save" (55).
    CHECK(AtSavePoint(55, true) && !AtSavePoint(55, false) && !AtSavePoint(0x45, true) && !AtSavePoint(0, true));

    // One return per target episode; a new episode (a later downing) may return again.
    { ReturnLatch l;
      CHECK(l.Take(1, 0xAB01) && !l.Take(1, 0xAB01));
      CHECK(l.Take(1, 0xAB02));
      CHECK(l.Take(2, 0xAB01));                             // another seat is independent
      CHECK(!l.Take(3, 0xAB09) && !l.Take(1, 0));
      l.Reset(); CHECK(l.Take(1, 0xAB02)); }

    CHECK(std::string_view(KindName(Kind::EncounterDefeat)) == "encounter-defeat" &&
          std::string_view(KindName(Kind::SavePoint)) == "save-point");

    std::printf(failures ? "DownedReturn: %d FAILED\n" : "DownedReturn: all checks passed\n", failures);
    return failures ? 1 : 0;
}
