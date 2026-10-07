// VUH-1504 offline controls for inject/src/RevivePrompt.hpp (hold timing, cancels,
// per-episode latch, native reaction-command yield, hidden states) and the revive
// prompt row in CoopHudState.hpp's Format. Pure rules only; no game process.
#include "RevivePrompt.hpp"
#include "CoopHudState.hpp"

#include <cstdio>

using namespace kh2coop::inject;
using namespace kh2coop::inject::reviveprompt;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static Facts Good(bool tri = false) {
    Facts f {};
    f.enabled = f.localCanonical = true;
    f.localHp = 20;
    f.targetValid = true; f.targetSlot = 1; f.targetEpisode = 0xAB00000001ull; f.distance = 90.0f;
    f.triangle = tri;
    return f;
}
// Hold Triangle for n frames then release; returns the release frame's output.
static Output HoldRelease(State& s, int n, Facts base = Good()) {
    Output o {};
    for (int i = 0; i < n; ++i) { auto f = base; f.triangle = true; o = Step(s, f); if (o.fire) return o; }
    auto f = base; f.triangle = false; return Step(s, f);
}

int main() {
    { State s; auto o = Step(s, Good()); CHECK(o.kind == Kind::Prompt && o.slot == 1 && o.progress == 0 && !o.fire); }
    // Timing: a full 60-frame hold fires on RELEASE, never while held.
    { State s; Output o {}; (void)Step(s, Good());
      for (int i = 0; i < 60; ++i) { o = Step(s, Good(true)); CHECK(!o.fire); }
      CHECK(o.progress == 1000);
      o = Step(s, Good(false)); CHECK(o.fire && o.slot == 1 && o.kind == Kind::Reviving); }
    { State s; (void)Step(s, Good()); auto o = HoldRelease(s, 59); CHECK(!o.fire); }           // one frame short
    { State s; (void)Step(s, Good()); auto o = HoldRelease(s, 30); CHECK(!o.fire && o.progress == 0); } // early release resets
    { State s; (void)Step(s, Good()); for (int i = 0; i < 200; ++i) (void)Step(s, Good(true)); auto o = Step(s, Good()); CHECK(o.fire); } // long hold still one fire
    // Latch: one request per episode; a new (re-minted) episode re-arms.
    { State s; (void)Step(s, Good()); CHECK(HoldRelease(s, 60).fire);
      auto o = Step(s, Good()); CHECK(o.kind == Kind::Reviving && o.hide == Hide::Fired);
      CHECK(!HoldRelease(s, 60).fire);
      auto f = Good(); f.targetEpisode += 1; (void)Step(s, f); CHECK(HoldRelease(s, 60, f).fire); }
    // "Reviving" shows for at most kPendingFrames, then the prompt stays hidden (Fired).
    { State s; (void)Step(s, Good()); (void)HoldRelease(s, 60); Output o {};
      for (std::uint32_t i = 0; i < kPendingFrames + 2; ++i) o = Step(s, Good());
      CHECK(o.kind == Kind::Hidden && o.hide == Hide::Fired); }
    // Cancels mid-hold: out of range, hit, menu, event, transition, local downed, target change.
    auto cancelled = [](auto mutate) {
        State s; (void)Step(s, Good());
        for (int i = 0; i < 40; ++i) (void)Step(s, Good(true));
        auto f = Good(true); mutate(f); (void)Step(s, f);
        for (int i = 0; i < 25; ++i) (void)Step(s, Good(true));  // keep holding after the cancel
        return !Step(s, Good(false)).fire;                        // needs a fresh press
    };
    CHECK(cancelled([](Facts& f) { f.distance = kRange + 1.0f; }));
    CHECK(cancelled([](Facts& f) { f.localHp = 15; }));          // took damage
    CHECK(cancelled([](Facts& f) { f.menuOpen = true; }));
    CHECK(cancelled([](Facts& f) { f.inEvent = true; }));
    CHECK(cancelled([](Facts& f) { f.transition = true; }));
    CHECK(cancelled([](Facts& f) { f.localDowned = true; }));
    CHECK(cancelled([](Facts& f) { f.targetSlot = 2; }));
    CHECK(cancelled([](Facts& f) { f.targetValid = false; }));
    CHECK(cancelled([](Facts& f) { f.nativeReaction = 0x59; }));
    // Native reaction command yields: hidden, no progress, no fire.
    { State s; auto f = Good(true); f.nativeReaction = 0x59; Output o {};
      for (int i = 0; i < 80; ++i) o = Step(s, f);
      CHECK(o.kind == Kind::Hidden && o.hide == Hide::NativeReaction);
      f.triangle = false; CHECK(!Step(s, f).fire); }
    // Hidden states each name their reason.
    { auto hidden = [](auto mutate, Hide want) { State s; auto f = Good(); mutate(f); auto o = Step(s, f); return o.kind == Kind::Hidden && o.hide == want; };
      CHECK(hidden([](Facts& f) { f.enabled = false; }, Hide::Off));
      CHECK(hidden([](Facts& f) { f.localCanonical = false; }, Hide::LocalUnavailable));
      CHECK(hidden([](Facts& f) { f.localDowned = true; }, Hide::LocalDowned));
      CHECK(hidden([](Facts& f) { f.inEvent = true; }, Hide::Event));
      CHECK(hidden([](Facts& f) { f.menuOpen = true; }, Hide::Menu));
      CHECK(hidden([](Facts& f) { f.transition = true; }, Hide::Transition));
      CHECK(hidden([](Facts& f) { f.nativeReaction = 1; }, Hide::NativeReaction));
      CHECK(hidden([](Facts& f) { f.targetValid = false; }, Hide::NoTarget));
      CHECK(hidden([](Facts& f) { f.targetEpisode = 0; }, Hide::NoTarget));
      CHECK(hidden([](Facts& f) { f.targetSlot = 3; }, Hide::NoTarget));
      CHECK(hidden([](Facts& f) { f.distance = kRange + 0.01f; }, Hide::OutOfRange)); }
    { State s; auto f = Good(); f.distance = kRange; CHECK(Step(s, f).kind == Kind::Prompt); }  // inclusive boundary
    // Triangle already held when the prompt appears must be released and re-pressed.
    { State s; Output o {};
      auto far = Good(true); far.distance = kRange + 50.0f;
      for (int i = 0; i < 10; ++i) (void)Step(s, far);           // holding while walking up
      for (int i = 0; i < 70; ++i) o = Step(s, Good(true));
      CHECK(o.progress == 0);
      CHECK(!Step(s, Good(false)).fire);
      CHECK(HoldRelease(s, 60).fire); }

    // L1+Triangle (native shortcut) never counts as a revive hold.
    CHECK(TriangleHeld(0x1000) && !TriangleHeld(0x1400) && !TriangleHeld(0x0400) && !TriangleHeld(0));
    // HUD: the overlay stays 130 px; the prompt lives in the downed teammate's row.
    static_assert(hud::Height == 130, "overlay height unchanged");
    { hud::Snapshot snap {}; snap.networkCurrent = true; snap.localSlot = 0;
      snap.members[1].state = hud::RowState::Available; snap.members[1].hpValid = true; snap.members[1].hp = 0; snap.members[1].maxHp = 24;
      auto text = hud::Format(snap);
      CHECK(!text.rows[1].prompt && text.rows[1].fillPixels == 0);           // flag off: unchanged row
      snap.promptKind = 1; snap.promptSlot = 1; snap.promptProgress = 500;
      text = hud::Format(snap);
      CHECK(text.rows[1].prompt && text.rows[1].showBar && text.rows[1].fillPixels == hud::TrackWidth / 2 &&
            std::wcsstr(text.rows[1].health, L"Hold") && !text.rows[0].prompt && !text.rows[2].prompt);
      CHECK(std::wcslen(text.rows[1].health) <= hud::HealthColumnChars);   // fits the HP column, no ellipsis
      snap.promptKind = 2; text = hud::Format(snap);
      CHECK(text.rows[1].prompt && std::wcsstr(text.rows[1].health, L"Reviving") && text.rows[1].fillPixels == hud::TrackWidth);
      CHECK(std::wcslen(text.rows[1].health) <= hud::HealthColumnChars);
      snap.promptSlot = 3; text = hud::Format(snap); CHECK(!text.rows[1].prompt);  // out-of-range slot ignored
      hud::Snapshot a = snap, b = snap; a.promptKind = b.promptKind = 1; a.promptSlot = b.promptSlot = 1;
      a.promptProgress = 410; b.promptProgress = 490;
      CHECK(hud::SameDisplayScope(a, b)); b.promptProgress = 510; CHECK(!hud::SameDisplayScope(a, b)); }

    std::printf("{\"ok\":%s,\"failures\":%d}\n", failures ? "false" : "true", failures);
    return failures ? 1 : 0;
}
