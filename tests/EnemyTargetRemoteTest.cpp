// VUH-1515 enemy targeting of remote clones: pure policy controls (EnemyTargetRemote.hpp).
// No game process, no native reads. The adapter (EnemyTargetRemote.inl) is covered by the
// lane's fake-image harness; live behaviour by the fixture in docs/ENEMY_TARGET_REMOTE.md.
#include "EnemyTargetRemote.hpp"

#include <cstdio>

namespace R = kh2coop::inject::enemytarget::rules;

static int g_fail = 0, g_pass = 0;
#define CHECK(name, cond) do { if (cond) ++g_pass; else { ++g_fail; std::printf("FAIL %s\n", name); } } while (0)

static R::Candidate C(bool valid, float x, float z) { return {valid, x, z}; }

int main() {
    using P = R::Policy;
    // Families.
    CHECK("allow Shadow 302", R::AllowedObject(302));
    CHECK("refuse other ids", !R::AllowedObject(303) && !R::AllowedObject(0) && !R::AllowedObject(84));

    // Player form.
    R::Slot s {0x100, 0, 0, 0};
    CHECK("mode 0 naming player", R::PlayerForm(s, true));
    s.mode = 2; CHECK("mode 2 naming player", R::PlayerForm(s, true));
    s.mode = 1; CHECK("mode 1 refused", !R::PlayerForm(s, true));
    s.mode = 3; CHECK("mode 3 refused", !R::PlayerForm(s, true));
    s.mode = 0; s.aux = 5; CHECK("aux != 0 refused", !R::PlayerForm(s, true));
    s.aux = 0; CHECK("not naming the canonical player refused", !R::PlayerForm(s, false));

    // Nearest, deterministic.
    std::uint32_t none[3] {};
    R::Candidate c[3] = {C(true, 0, 0), C(true, 0, 500), C(false, 0, 0)};
    CHECK("nearest local", R::Choose(c, 0, 100, nullptr, none, 1000) == 0);
    CHECK("nearest clone", R::Choose(c, 0, 400, nullptr, none, 1000) == 1);
    CHECK("tie keeps lower index", R::Choose(c, 0, 250, nullptr, none, 1000) == 0);
    R::Candidate empty[3] = {};
    CHECK("no valid candidate -> native", R::Choose(empty, 0, 0, nullptr, none, 1000) == -1);
    R::Candidate onlyClone[3] = {C(false, 0, 0), C(true, 0, 500), C(false, 0, 0)};
    CHECK("downed/absent local: the clone", R::Choose(onlyClone, 0, 0, nullptr, none, 1000) == 1);
    CHECK("y is ignored (horizontal)", R::HDist(0, 0, 3, 4) == 5.0f);

    // Hold time.
    R::Assignment a {}; a.used = true; a.target = 0; a.since = 1000;
    CHECK("hold keeps the current target", R::Choose(c, 0, 490, &a, none, 1000 + P::MinHoldFrames - 1) == 0);
    CHECK("after hold, switch when nearer by more than the margin", R::Choose(c, 0, 490, &a, none, 1000 + P::MinHoldFrames) == 1);

    // Hysteresis.
    a.since = 0;
    // enemy at z=300: local 300, clone 200 -> only 100 nearer (< 150): keep local.
    CHECK("hysteresis keeps current within the margin", R::Choose(c, 0, 300, &a, none, 10000) == 0);
    // enemy at z=340: local 340, clone 160 -> 180 nearer: switch.
    CHECK("hysteresis switches beyond the margin", R::Choose(c, 0, 340, &a, none, 10000) == 1);

    // Invalid current target is dropped regardless of hold.
    R::Assignment onClone {}; onClone.used = true; onClone.target = 1; onClone.since = 9990;
    R::Candidate cloneGone[3] = {C(true, 0, 0), C(false, 0, 500), C(false, 0, 0)};
    CHECK("current clone gone -> local even during hold", R::Choose(cloneGone, 0, 450, &onClone, none, 10000) == 0);

    // Per-clone cap.
    std::uint32_t full[3] = {0, P::CloneCap, 0};
    CHECK("cap: a full clone is skipped", R::Choose(c, 0, 450, nullptr, full, 10000) == 0);
    CHECK("cap: the clone's own current enemy is not counted out", R::Choose(c, 0, 450, &onClone, full, 10000) == 1);
    R::Candidate onlyFull[3] = {C(false, 0, 0), C(true, 0, 500), C(false, 0, 0)};
    CHECK("cap with no other candidate -> native", R::Choose(onlyFull, 0, 450, nullptr, full, 10000) == -1);
    std::uint32_t localLoad[3] = {100, 0, 0};
    CHECK("local player has no cap", R::Choose(c, 0, 10, nullptr, localLoad, 10000) == 0);

    // Clone candidacy.
    R::CloneFacts f {};
    f.puppetActive = f.playerClass = f.distinctStatus = f.actorLive = f.poseFresh = f.sameRoom = true;
    f.ownerSlot = 1;
    CHECK("clone candidate", R::CloneCandidate(f));
    { auto g = f; g.downed = true; CHECK("downed clone skipped", !R::CloneCandidate(g)); }
    { auto g = f; g.inCutscene = true; CHECK("cutscene clone skipped", !R::CloneCandidate(g)); }
    { auto g = f; g.distinctStatus = false; CHECK("shared status skipped", !R::CloneCandidate(g)); }
    { auto g = f; g.poseFresh = false; CHECK("stale pose skipped", !R::CloneCandidate(g)); }
    { auto g = f; g.sameRoom = false; CHECK("other room skipped", !R::CloneCandidate(g)); }
    { auto g = f; g.playerClass = false; CHECK("companion puppet skipped", !R::CloneCandidate(g)); }
    { auto g = f; g.ownerSlot = 0; CHECK("owner slot 0 (host) skipped", !R::CloneCandidate(g)); }
    CHECK("liveness flags", R::ActorLive(0, 0, 1) && !R::ActorLive(0x80000, 0, 1) && !R::ActorLive(0x10000000, 0, 1) &&
                            !R::ActorLive(0, 4, 1) && !R::ActorLive(0, 0, 0));

    // Owner application.
    R::OwnerFacts o {}; o.damage = 8; o.hp = 24; o.canonical = true; o.coveredMs = 2000;
    CHECK("owner applies", R::OwnerApply(o) == R::ApplyRefusal::None);
    { auto g = o; g.damage = 0; CHECK("zero damage refused", R::OwnerApply(g) == R::ApplyRefusal::Amount); }
    { auto g = o; g.damage = 10000; CHECK("huge damage refused", R::OwnerApply(g) == R::ApplyRefusal::Amount); }
    { auto g = o; g.canonical = false; CHECK("non-canonical refused", R::OwnerApply(g) == R::ApplyRefusal::NotCanonical); }
    { auto g = o; g.transition = true; CHECK("transition refused", R::OwnerApply(g) == R::ApplyRefusal::Transition); }
    { auto g = o; g.inEvent = true; CHECK("event refused", R::OwnerApply(g) == R::ApplyRefusal::Event); }
    { auto g = o; g.dead = true; CHECK("dead/downed refused", R::OwnerApply(g) == R::ApplyRefusal::Dead); }
    { auto g = o; g.hp = 0; CHECK("hp 0 refused", R::OwnerApply(g) == R::ApplyRefusal::Dead); }
    { auto g = o; g.invulnFrames = 30.0f; CHECK("invulnerable refused", R::OwnerApply(g) == R::ApplyRefusal::Invulnerable); }
    { auto g = o; g.damage = 30; CHECK("lethal value is passed to the native funnel", R::OwnerApply(g) == R::ApplyRefusal::None); }

    { auto g = o; g.invulnFrames = 30.0f; g.invulnFromVeto = true; CHECK("i-frames from our own cancel do not refuse (S3)", R::OwnerApply(g) == R::ApplyRefusal::None); }
    { auto g = o; g.coveredMs = 1499; CHECK("S7: coverage under 1.5 s -> Stale", R::OwnerApply(g) == R::ApplyRefusal::Stale); }
    { auto g = o; g.coveredMs = 0; g.damage = 0; CHECK("amount checked before staleness", R::OwnerApply(g) == R::ApplyRefusal::Amount); }
    // N2 episode.
    { R::VetoEpisode e {}; R::EpisodeOnVeto(e, 0.0f, 100); R::EpisodeTick(e, 20.0f, 101);
      CHECK("episode: rise within 2 frames of a cancel on a 0 timer is ours", R::EpisodeOverrides(e, false));
      CHECK("episode: never during revive grace", !R::EpisodeOverrides(e, true));
      R::EpisodeTick(e, 19.0f, 102); CHECK("episode: counting down keeps it", R::EpisodeOverrides(e, false));
      R::EpisodeTick(e, 80.0f, 103); CHECK("episode: raised again (guard/grace) ends it", !R::EpisodeOverrides(e, false)); }
    { R::VetoEpisode e {}; R::EpisodeOnVeto(e, 5.0f, 100); R::EpisodeTick(e, 20.0f, 101);
      CHECK("episode: a running timer at the cancel never arms", !R::EpisodeOverrides(e, false)); }
    { R::VetoEpisode e {}; R::EpisodeOnVeto(e, 0.0f, 100); R::EpisodeTick(e, 0.0f, 101); R::EpisodeTick(e, 0.0f, 102);
      R::EpisodeTick(e, 0.0f, 103); R::EpisodeTick(e, 20.0f, 104);
      CHECK("episode: a late rise is not ours", !R::EpisodeOverrides(e, false)); }
    { R::VetoEpisode e {}; R::EpisodeOnVeto(e, 0.0f, 100); R::EpisodeTick(e, 20.0f, 101); R::EpisodeTick(e, 0.0f, 150);
      CHECK("episode: ends when the timer reaches 0", !R::EpisodeOverrides(e, false)); }

    // TargetAuthority (B2/B3).
    bool valid[3] = {true, true, false};
    std::uint8_t owner[3] = {0, 1, 2};
    CHECK("slot mask: clone 1 owned by slot 1", R::SlotMask(true, true, valid, owner) == 0x02);
    CHECK("slot mask: empty when inactive", R::SlotMask(false, true, valid, owner) == 0);
    CHECK("slot mask: empty when not swapped", R::SlotMask(true, false, valid, owner) == 0);
    { bool v2[3] = {true, true, true}; std::uint8_t o2[3] = {0, 2, 1};
      CHECK("slot mask: both clones by owner slot", R::SlotMask(true, true, v2, o2) == 0x06); }
    { bool v3[3] = {true, true, false}; std::uint8_t o3[3] = {0, 0, 2};
      CHECK("slot mask: owner slot 0 never set", R::SlotMask(true, true, v3, o3) == 0); }
    CHECK("cancel: held forward advertisement naming us", R::OwnerCancels(true, 0, 0x02, 1, 1, 302));
    CHECK("cancel: not held -> native", !R::OwnerCancels(false, 0, 0x02, 1, 1, 302));
    CHECK("cancel: mirror mode -> native (B3)", !R::OwnerCancels(true, 1, 0x02, 1, 1, 302));
    CHECK("cancel: our bit clear -> native", !R::OwnerCancels(true, 0, 0x04, 1, 1, 302));
    CHECK("cancel: other family -> native", !R::OwnerCancels(true, 0, 0x02, 1, 1, 303));
    CHECK("cancel: family bit clear -> native", !R::OwnerCancels(true, 0, 0x02, 0, 1, 302));
    CHECK("cancel: slot 0 (host) never", !R::OwnerCancels(true, 0, 0x07, 1, 0, 302));

    // S9 and S10.
    CHECK("S9: coverage ready at 1.5 s", R::CoverageReady(1500) && !R::CoverageReady(1499));
    CHECK("S10: a revive event with graceActor set and a live timer latches", R::GraceLatch(false, true, true, 120.0f));
    CHECK("S10: a stale graceActor without a revive event does not latch", !R::GraceLatch(false, false, true, 20.0f));
    CHECK("S10: a revive event with the live timer at 0 does not latch", !R::GraceLatch(false, true, true, 0.0f));
    CHECK("S10: the latch holds while the timer runs", R::GraceLatch(true, false, true, 60.0f));
    CHECK("S10: the latch clears at 0", !R::GraceLatch(true, false, true, 0.0f));

    // Shape constant sanity (the live bytes are checked by the lane's static_bytes against the exe).
    CHECK("target_search bytes end in jmp rel32 to the selector", R::kTargetSearch[70] == 0xE9 && R::kTargetSearch[0] == 0x48);

    std::printf("EnemyTargetRemoteTest: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
