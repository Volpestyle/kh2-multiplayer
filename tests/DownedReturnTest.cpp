// VUH-2632 (D17) offline controls for inject/src/DownedReturn.hpp: when a downed teammate who sits out
// comes back (a forced battle's end, a save point) and the once-per-episode latch. Pure rules.
#include "DownedReturn.hpp"

#include <cstdint>
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

    // A room change while downed: sit out into the next room unless everyone seen was down (Continue) or
    // solo; a session whose teammates were never seen this downing sits out (unavailable is not solo).
    { auto with = [](unsigned known, unsigned downed) { DispositionLatch l; l.Observe(known, downed); return l; };
      CHECK(SitOutAfterTransition(with(1, 0), true).after == AfterTransition::SitOut);
      CHECK(SitOutAfterTransition(with(2, 1), true).why == Disposition::TeammateUp);
      CHECK(SitOutAfterTransition(with(1, 1), true).after == AfterTransition::Resolve &&
            SitOutAfterTransition(with(2, 2), true).why == Disposition::FullParty);
      const DispositionLatch none;
      CHECK(SitOutAfterTransition(none, false).after == AfterTransition::Resolve &&
            SitOutAfterTransition(none, false).why == Disposition::Solo);
      CHECK(SitOutAfterTransition(none, true).after == AfterTransition::SitOut &&
            SitOutAfterTransition(none, true).why == Disposition::Unavailable);
      DispositionLatch l = with(2, 1); l.Observe(0, 0);          // an empty sample keeps the last real one
      CHECK(l.Sampled() && l.Known() == 2 && l.Downed() == 1);
      l.Reset(); CHECK(!l.Sampled()); }

    // Regression (Quill R1), the adapter's frame order on a Proud door: the surviving host takes an
    // ordinary door while the friend is logically downed. Each frame the downed owner runs first, then
    // puppet polling, whose suspension erases every teammate pose on the transition's first frame. The
    // old actor stays current that frame, so the hold is found lost only on a later frame, when no pose
    // is left. Counting then (the old adapter) saw nobody and resolved the friend alive.
    { struct Pose { bool have; bool downed; };
      Pose puppets[2] = {{true, false}, {false, false}};         // the host, up; no third seat
      DispositionLatch latch;
      auto count = [&](unsigned& known, unsigned& downed) {
          known = downed = 0;
          for (const auto& p : puppets) if (p.have) { ++known; if (p.downed) ++downed; } };
      bool lostDecided = false;
      TransitionDecision decision {AfterTransition::Resolve, Disposition::Solo};
      unsigned oldKnown = 0, oldDowned = 0;
      for (int frame = 0; frame < 6; ++frame) {
          const bool transitionStarted = frame >= 3;              // the host's door, requested in frame 2
          const bool holdLost = frame >= 5;                       // the new room's actor, two frames later
          unsigned known = 0, downed = 0;
          count(known, downed);                                   // DownedSpike Tick: first in the frame
          latch.Observe(known, downed);
          if (holdLost && !lostDecided) {
              lostDecided = true;
              oldKnown = known; oldDowned = downed;               // what the old adapter counted here
              decision = SitOutAfterTransition(latch, true);
          }
          if (transitionStarted) for (auto& p : puppets) p = {};  // PollPuppetPoses -> UpdatePuppetSuspension
      }
      CHECK(lostDecided && oldKnown == 0 && oldDowned == 0);      // the erased poses: the old count was empty
      CHECK(decision.after == AfterTransition::SitOut && decision.why == Disposition::TeammateUp); }

    // Save point: a living seat whose settled reaction is the save point's "Save" (55).
    CHECK(AtSavePoint(55, true) && !AtSavePoint(55, false) && !AtSavePoint(0x45, true) && !AtSavePoint(0, true));

    // Per target episode: the first request, then at most two more, kReturnRetryFrames apart; a new episode
    // (a later downing) starts again.
    { ReturnLatch l;
      CHECK(l.Take(1, 0xAB01, 100) && !l.Take(1, 0xAB01, 101) && !l.Take(1, 0xAB01, 100 + kReturnRetryFrames - 1));
      CHECK(l.Take(1, 0xAB01, 100 + kReturnRetryFrames) && l.Take(1, 0xAB01, 100 + 2 * kReturnRetryFrames));
      CHECK(!l.Take(1, 0xAB01, 100 + 3 * kReturnRetryFrames) && !l.Take(1, 0xAB01, 100 + 9 * kReturnRetryFrames));
      CHECK(l.Take(1, 0xAB02, 101));
      CHECK(l.Take(2, 0xAB01, 101));                        // another seat is independent
      CHECK(!l.Take(3, 0xAB09, 101) && !l.Take(1, 0, 101));
      l.Reset(); CHECK(l.Take(1, 0xAB02, 102)); }

    // Living gate: kLivingSettleFrames of living publication in a row; any lapse starts over.
    { LivingGate g;
      for (std::uint32_t i = 0; i + 1 < kLivingSettleFrames; ++i) g.Step(true);
      CHECK(!g.Settled()); g.Step(true); CHECK(g.Settled());
      g.Step(false); CHECK(!g.Settled()); }

    // Pending encounter: waits for the living gate, sends for a bounded window, and is dropped by a room change.
    { PendingEncounter e;
      CHECK(e.Tick(1, 1, true) == PendingEncounter::Step::Idle);
      e.Arm(1, 1);
      CHECK(e.Tick(1, 1, false) == PendingEncounter::Step::Wait && e.Armed());
      CHECK(e.Tick(1, 1, true) == PendingEncounter::Step::Send);
      CHECK(e.Tick(1, 1, false) == PendingEncounter::Step::Send); // a lapse after sending began does not re-wait
      std::uint32_t sends = 2;
      while (e.Tick(1, 1, true) == PendingEncounter::Step::Send) ++sends;
      CHECK(sends == kEncounterReturnFrames && !e.Armed());
      e.Arm(1, 1); CHECK(e.Tick(2, 1, true) == PendingEncounter::Step::Dropped && !e.Armed());
      e.Arm(1, 1); CHECK(e.Tick(1, 2, false) == PendingEncounter::Step::Dropped); }

    // Regression (Quill R2), three seats: the host and seat 1 are down, seat 2 wins the encounter. The
    // relay (SessionHost) refuses a requester whose last avatar there is downed or at HP 0. The host
    // revives itself at the edge; its avatar reaches the relay a few frames later. Sending at the edge
    // (the old order) is refused, and the old once-only latch then never asked again; now the return
    // waits for the living publication and is delivered on the first request.
    auto run = [](bool gated, unsigned& refusedNotAlive, unsigned& delivered, unsigned& requests) {
        constexpr std::uint32_t edge = 1000, relayLag = 4;    // frames for a living avatar to reach the relay
        std::uint32_t revivedAt = 0;
        bool hostDowned = true, seat1Downed = true;
        refusedNotAlive = delivered = requests = 0;
        PendingEncounter pending; LivingGate living; ReturnLatch latch; EncounterTracker tracker;
        (void)tracker.Step(2, 1, 1, true);                     // the forced battle, seat 2 still fighting
        for (std::uint32_t frame = edge; frame < edge + 400; ++frame) {
            living.Step(!hostDowned);                          // this frame's local publication
            if (tracker.Step(0, 1, 1, true)) {                 // seat 2 clears the encounter
                pending.Arm(1, 1);
                hostDowned = false; revivedAt = frame;          // the host's local TryRevive, after the edge
            }
            if (pending.Tick(1, 1, gated ? living.Settled() : true) == PendingEncounter::Step::Send && seat1Downed &&
                latch.Take(1, 0x5E01, frame)) {
                ++requests;
                const bool relaySeesHostLiving = frame >= revivedAt + 1 + relayLag && !hostDowned;
                if (!relaySeesHostLiving) ++refusedNotAlive;
                else { ++delivered; seat1Downed = false; }       // the owner applies it once
            }
        }
        return !seat1Downed;
    };
    { unsigned refused = 0, delivered = 0, requests = 0;
      CHECK(run(true, refused, delivered, requests) && refused == 0 && delivered == 1 && requests == 1);
      CHECK(run(false, refused, delivered, requests) && refused == 1 && delivered == 1 && requests == 2); }

    CHECK(std::string_view(KindName(Kind::EncounterDefeat)) == "encounter-defeat" &&
          std::string_view(KindName(Kind::SavePoint)) == "save-point");

    std::printf(failures ? "DownedReturn: %d FAILED\n" : "DownedReturn: all checks passed\n", failures);
    return failures ? 1 : 0;
}
