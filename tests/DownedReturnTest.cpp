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
    { SessionTeammates seen;
      CHECK(!seen.Seen()); seen.Step(true, 0xA1, 0); CHECK(!seen.Seen());
      seen.Step(true, 0xA1, 1); seen.Step(true, 0xA1, 0); CHECK(seen.Seen()); // kept after the pose is gone
      seen.Step(false, 0, 0); CHECK(seen.Seen());                   // identity unknown: nothing proven
      seen.Step(true, 0, 0); CHECK(!seen.Seen());                   // the session ended
      seen.Step(true, 0, 1); CHECK(!seen.Seen()); }                 // no session: nothing to remember

    // Regression (Quill R4 a): session A sees a teammate; replacement session B goes live with no Off
    // frame between (CheckActivationGeneration retires A without one) and has no teammate. A new downing
    // and a story transition in B must be solo, not "unavailable".
    { SessionTeammates session; DispositionLatch latch;
      for (int frame = 0; frame < 60; ++frame) session.Step(true, 0xA1, 1);   // session A, with the friend
      for (int frame = 60; frame < 180; ++frame) {
          session.Step(true, 0xB2, 0);                           // session B, alone, live every frame
          if (frame == 120) latch.Reset();                       // downed in B
          if (frame >= 120) latch.Observe(0, 0);
      }
      const auto d = SitOutAfterTransition(latch, session.Seen());
      CHECK(!session.Seen() && d.after == AfterTransition::Resolve && d.why == Disposition::Solo); }

    // Regression (Quill R4, adapter order): the replacement's first frame. The runtime retires session A's
    // puppet authority, publishes B's identity and advances the generation; the game's next frame runs
    // the downed owner first, while A's friend pose is still cached in the puppet drivers (EnemySync's
    // drain and puppet polling clear it later in that frame). Counting that pose under B's identity (the
    // 2c adapter) made B "seen" for good; its provenance names A's generation and is refused now, also
    // once B's authority is armed. B is alone: a downing there is solo.
    { using kh2coop::PuppetAuthority; using kh2coop::PuppetAuthorityMode; using kh2coop::PuppetProvenance;
      using kh2coop::PuppetProducer;
      struct Driver { bool have; std::uint32_t poseFrame; PuppetProvenance tag; std::uint8_t owner; };
      const PuppetAuthority authorityA {PuppetAuthorityMode::Network, 0, 7, {0x100, 0x101, 0}};
      const PuppetProvenance friendA {PuppetProducer::Network, 0, 7, 0x101, 0x100, 0x100};
      Driver drivers[2] = {{false, 0, {}, 255}, {false, 0, {}, 255}};
      SessionTeammates session, unvalidated; DispositionLatch latch;
      std::uint32_t frame = 0;
      auto tick = [&](bool known, std::uint64_t identity, const PuppetAuthority& now) {
          unsigned owned = 0, cached = 0;                      // DownedSpike::Tick, first in the frame
          for (int i = 0; i < 2; ++i) {
              const Driver& d = drivers[i];
              if (CountsForSession(d.have, frame - d.poseFrame, d.tag, d.owner, i, now)) ++owned;
              if (d.have && frame - d.poseFrame <= 600) ++cached;
          }
          session.Step(known, identity, owned);
          unvalidated.Step(known, identity, cached);
          return cached; };
      for (; frame < 60; ++frame) {                            // session A, the friend live
          drivers[0] = {true, frame, friendA, 1};
          (void)tick(true, 0xA1, authorityA);
      }
      CHECK(session.Seen());
      // Frame 60: B's identity is out, A's authority retired (Unavailable), A's pose still cached.
      CHECK(tick(true, 0xB2, PuppetAuthority {}) == 1);
      CHECK(!session.Seen() && unvalidated.Seen());            // the 2c adapter imported A's friend into B
      drivers[0] = {false, 0, {}, 255};                        // polling clears it later in frame 60
      ++frame;
      // B's authority is armed (generation 8, new connections); still no teammate in B.
      const PuppetAuthority authorityB {PuppetAuthorityMode::Network, 0, 8, {0x200, 0, 0}};
      for (; frame < 180; ++frame) {
          (void)tick(true, 0xB2, authorityB);
          if (frame == 120) latch.Reset();                     // downed in B
          if (frame >= 120) latch.Observe(0, 0);
      }
      const auto d = SitOutAfterTransition(latch, session.Seen());
      CHECK(d.after == AfterTransition::Resolve && d.why == Disposition::Solo);
      CHECK(SitOutAfterTransition(latch, unvalidated.Seen()).why == Disposition::Unavailable); // the bug
      // Even had A's pose survived into B's armed authority, its provenance would not count.
      CHECK(!CountsForSession(true, 0, friendA, 1, 0, authorityB));
      // In A itself, a cached owned pose counts; a stale one (over 600 frames) does not.
      CHECK(CountsForSession(true, 600, friendA, 1, 0, authorityA) && !CountsForSession(true, 601, friendA, 1, 0, authorityA)); }

    // Regression (Quill R4 b): within one session the runtime's heartbeat lapses (the role reads Off, the
    // bridge and the session identity stay) while the friend's poses are gone; the history is kept, so
    // the downed player sits out ("unavailable"), as it would without the gap.
    { SessionTeammates session; DispositionLatch latch;
      for (int frame = 0; frame < 60; ++frame) session.Step(true, 0xA1, 1);   // the friend, seen
      latch.Reset();                                              // downed
      for (int frame = 60; frame < 120; ++frame) {
          session.Step(true, 0xA1, 0);                           // frames 80-99 read role Off; same identity
          latch.Observe(0, 0);
      }
      const auto d = SitOutAfterTransition(latch, session.Seen());
      CHECK(session.Seen() && d.after == AfterTransition::SitOut && d.why == Disposition::Unavailable); }

    // Regression (lead #142): a seat alone in its session, never having seen a teammate, is downed and a
    // story transition takes it to the next room. Nobody could return it, so it must not sit out: it is
    // resolved (solo), as before D17.
    { SessionTeammates session; DispositionLatch latch;
      for (int frame = 0; frame < 120; ++frame) {
          session.Step(true, 0xA1, 0);                            // in a session, no teammate pose ever
          if (frame >= 60) latch.Observe(0, 0);                   // downed from frame 60 on; still nobody
      }
      const auto d = SitOutAfterTransition(latch, session.Seen());
      CHECK(d.after == AfterTransition::Resolve && d.why == Disposition::Solo); }

    // Regressions (Quill R3): an encounter defeat returns the downings it claimed at its edge, and no
    // other. Seat 1 is alive at the victory and newly downed 120 frames later, in the same room
    // generation; seat 2 was claimed, returned, then downed anew under a new episode. Neither later
    // downing gets the completed encounter's return (a rescan of the current puppets gave both one).
    { struct Seat { bool downed; std::uint64_t episode; };
      Seat seats[3] = {{false, 0}, {false, 0}, {true, 0x2A01}}; // the host; seat 1 up; seat 2 down at the edge
      PendingEncounter pending; LivingGate living; ReturnLatch latch;
      unsigned requests[3] {}; std::uint64_t lastEpisode[3] {};
      pending.Arm(1, 1);
      for (std::uint8_t slot = 1; slot < 3; ++slot)
          if (seats[slot].downed) (void)pending.Claim({slot, 70u + slot, seats[slot].episode});
      CHECK(pending.Claims() == 1);
      for (std::uint32_t frame = 0; frame < 400; ++frame) {
          living.Step(true);                                     // the host is up
          if (frame == 150) seats[1] = {true, 0x1B01};             // seat 1: a new downing after the victory
          if (frame == 200) seats[2] = {true, 0x2A02};             // seat 2, returned below, downed anew
          if (pending.Tick(1, 1, living.Settled()) != PendingEncounter::Step::Send) continue;
          pending.ForEachClaim([&](const ReturnClaim& c) {
              const Seat& t = seats[c.slot];
              const auto check = CheckClaim(c, true, 70u + c.slot, true, t.downed, t.episode, true);
              if (check == ClaimCheck::Send && latch.Take(c.slot, c.episode, frame)) {
                  ++requests[c.slot]; lastEpisode[c.slot] = c.episode;
                  seats[c.slot] = {false, 0};                      // delivered: the owner returns it
              }
              return check;
          });
      }
      CHECK(requests[1] == 0);                                   // not claimed at the edge: never returned
      CHECK(requests[2] == 1 && lastEpisode[2] == 0x2A01);       // its claimed downing only
      CHECK(seats[1].downed && seats[2].downed && seats[2].episode == 0x2A02);
      CHECK(!pending.Armed()); }                                 // every claim dropped: ended early

    // Everyone down, the host in its room: every teammate here downed, or a lone host.
    CHECK(EveryoneDown(true, true, true) && !EveryoneDown(true, false, true));
    CHECK(EveryoneDown(false, true, false));                       // never seen a teammate: alone
    CHECK(!EveryoneDown(false, true, true));                       // seen earlier, absent now: not everyone

    // Regression (lead #143): a host alone in its session, never having seen a teammate, is downed in its
    // room. It used to wait forever (the check needed a teammate in the room); now one second of that is
    // everyone down and the native game over runs, as the adapter counts it (60 frames).
    { SessionTeammates session; std::uint32_t allDownFrames = 0; int gameOverAt = -1;
      for (int frame = 0; frame < 200 && gameOverAt < 0; ++frame) {
          session.Step(true, 0xA1, 0);
          if (frame < 50) continue;                               // downed from frame 50
          const bool any = false, all = true;                     // no fresh teammate pose in the room
          allDownFrames = EveryoneDown(any, all, session.Seen()) ? allDownFrames + 1 : 0;
          if (allDownFrames >= 60) gameOverAt = frame;
      }
      CHECK(gameOverAt == 109); }

    // Regression (lead #142 / Quill R1): a teammate was seen earlier in the session, then its poses were
    // unavailable for this whole downing (stale or erased). That is not solo: the player sits out.
    { SessionTeammates session; DispositionLatch latch;
      for (int frame = 0; frame < 60; ++frame) session.Step(true, 0xA1, 1); // the friend, up, before the downing
      latch.Reset();                                              // BeginDowned
      for (int frame = 60; frame < 120; ++frame) { session.Step(true, 0xA1, 0); latch.Observe(0, 0); }
      const auto d = SitOutAfterTransition(latch, session.Seen());
      CHECK(d.after == AfterTransition::SitOut && d.why == Disposition::Unavailable); }

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
      CHECK(!e.Claim({1, 77, 0x5E01}));                            // not armed
      e.Arm(1, 1);
      CHECK(e.Claim({1, 77, 0x5E01}) && !e.Claim({1, 78, 0x5E02})); // one claim per seat
      CHECK(!e.Claim({3, 77, 1}) && !e.Claim({2, 0, 1}) && !e.Claim({2, 77, 0}) && e.Claims() == 1);
      CHECK(e.Tick(1, 1, false) == PendingEncounter::Step::Wait && e.Armed());
      CHECK(e.Tick(1, 1, true) == PendingEncounter::Step::Send);
      CHECK(!e.Claim({2, 79, 0x5E03}));                            // no new claim once sending began
      CHECK(e.Tick(1, 1, false) == PendingEncounter::Step::Send); // a lapse after sending began does not re-wait
      std::uint32_t sends = 2;
      while (e.Tick(1, 1, true) == PendingEncounter::Step::Send) ++sends;
      CHECK(sends == kEncounterReturnFrames && !e.Armed());
      e.Arm(1, 1); e.Claim({1, 77, 1}); CHECK(e.Tick(2, 1, true) == PendingEncounter::Step::Dropped && !e.Armed());
      e.Arm(1, 1); e.Claim({1, 77, 1}); CHECK(e.Tick(1, 2, false) == PendingEncounter::Step::Dropped);
      e.Arm(1, 1); CHECK(e.Tick(1, 1, true) == PendingEncounter::Step::Expired); // nothing claimed
      e.Arm(1, 1); e.Claim({1, 77, 1}); (void)e.Tick(1, 1, true);
      e.ForEachClaim([](const ReturnClaim&) { return ClaimCheck::Drop; });
      CHECK(e.Claims() == 0 && e.Tick(1, 1, true) == PendingEncounter::Step::Expired); }

    // A claim against the target now.
    { const ReturnClaim c {1, 77, 0x5E01};
      CHECK(CheckClaim(c, true, 77, true, true, 0x5E01, true) == ClaimCheck::Send);
      CHECK(CheckClaim(c, true, 78, true, true, 0x5E01, true) == ClaimCheck::Drop);   // another player in the seat
      CHECK(CheckClaim(c, true, 77, true, false, 0, true) == ClaimCheck::Drop);       // revived
      CHECK(CheckClaim(c, true, 77, true, true, 0x5E02, true) == ClaimCheck::Drop);   // downed anew
      CHECK(CheckClaim(c, true, 77, false, false, 0, true) == ClaimCheck::Keep);      // stale pose: wait
      CHECK(CheckClaim(c, true, 77, true, true, 0x5E01, false) == ClaimCheck::Keep);  // another room: wait
      CHECK(CheckClaim(c, false, 0, true, true, 0x5E01, true) == ClaimCheck::Keep); } // scope unknown: wait

    // Regression (Quill R2), three seats: the host and seat 1 are down, seat 2 wins the encounter. The
    // relay (SessionHost) refuses a requester whose last avatar there is downed or at HP 0. The host
    // revives itself at the edge; its avatar reaches the relay a few frames later. Sending at the edge
    // (the old order) is refused, and the old once-only latch then never asked again; now the return
    // waits for the living publication and is delivered on the first request.
    auto run = [](bool gated, unsigned& refusedNotAlive, unsigned& delivered, unsigned& requests) {
        const std::uint32_t edge = 1000, relayLag = 4;        // frames for a living avatar to reach the relay
        std::uint32_t revivedAt = 0;
        bool hostDowned = true, seat1Downed = true;
        refusedNotAlive = delivered = requests = 0;
        PendingEncounter pending; LivingGate living; ReturnLatch latch; EncounterTracker tracker;
        (void)tracker.Step(2, 1, 1, true);                     // the forced battle, seat 2 still fighting
        for (std::uint32_t frame = edge; frame < edge + 400; ++frame) {
            living.Step(!hostDowned);                          // this frame's local publication
            if (tracker.Step(0, 1, 1, true)) {                 // seat 2 clears the encounter
                pending.Arm(1, 1);
                (void)pending.Claim({1, 77, 0x5E01});           // seat 1, downed at the edge
                hostDowned = false; revivedAt = frame;          // the host's local TryRevive, after the edge
            }
            if (pending.Tick(1, 1, gated ? living.Settled() : true) != PendingEncounter::Step::Send) continue;
            pending.ForEachClaim([&](const ReturnClaim& c) {
                const auto check = CheckClaim(c, true, 77, true, seat1Downed, seat1Downed ? 0x5E01 : 0, true);
                if (check == ClaimCheck::Send && latch.Take(c.slot, c.episode, frame)) {
                    ++requests;
                    const bool relaySeesHostLiving = frame >= revivedAt + 1 + relayLag && !hostDowned;
                    if (!relaySeesHostLiving) ++refusedNotAlive;
                    else { ++delivered; seat1Downed = false; }   // the owner applies it once
                }
                return check;
            });
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
