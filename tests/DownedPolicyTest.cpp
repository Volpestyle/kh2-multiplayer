// VUH-2632 (D17) offline controls for inject/src/DownedPolicy.hpp: revive availability and hold time by
// the session's native difficulty, the automation override and the Proud/Critical switch. Pure rules.
#include "DownedPolicy.hpp"

#include <cstdio>

using namespace kh2coop::inject::downedpolicy;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

int main() {
    const Config product {};
    // James: the quick revive is for Beginner and automation; Standard revives but slower.
    CHECK(For(Difficulty::Beginner, product).reviveAllowed && For(Difficulty::Beginner, product).holdFrames == 60);
    CHECK(For(Difficulty::Standard, product).reviveAllowed && For(Difficulty::Standard, product).holdFrames == 300);
    CHECK(For(Difficulty::Standard, product).holdFrames > For(Difficulty::Beginner, product).holdFrames);
    // Proud/Critical: no partner revive by default (the lead's reading, pending James).
    CHECK(!For(Difficulty::Proud, product).reviveAllowed && !For(Difficulty::Critical, product).reviveAllowed);
    // Until the host's difficulty is known, Beginner rules apply.
    CHECK(For(Difficulty::Unknown, product).reviveAllowed && For(Difficulty::Unknown, product).holdFrames == 60);

    // Automation fixtures force the quick revive on every difficulty.
    const Config fixture = ParseConfig("beginner", "", "");
    for (const auto d : {Difficulty::Beginner, Difficulty::Standard, Difficulty::Proud, Difficulty::Critical})
        CHECK(For(d, fixture).reviveAllowed && For(d, fixture).holdFrames == 60);

    // The one switch for Proud/Critical: "standard" gives them Standard's rule.
    const Config hardStandard = ParseConfig("", "", "standard");
    CHECK(For(Difficulty::Proud, hardStandard).reviveAllowed && For(Difficulty::Proud, hardStandard).holdFrames == 300);
    CHECK(For(Difficulty::Critical, hardStandard).reviveAllowed);
    CHECK(!For(Difficulty::Proud, ParseConfig("", "", "off")).reviveAllowed);
    CHECK(!For(Difficulty::Proud, ParseConfig("", "", "nonsense")).reviveAllowed);

    // Standard's hold is tunable and clamped to 60..1800 frames.
    CHECK(For(Difficulty::Standard, ParseConfig("", "480", "")).holdFrames == 480);
    CHECK(For(Difficulty::Standard, ParseConfig("", "10", "")).holdFrames == 60);
    CHECK(For(Difficulty::Standard, ParseConfig("", "99999", "")).holdFrames == 1800);
    CHECK(For(Difficulty::Standard, ParseConfig("", "12a", "")).holdFrames == 300);   // not a number: default
    CHECK(For(Difficulty::Standard, ParseConfig("", "123456", "")).holdFrames == 300); // too long: default
    CHECK(For(Difficulty::Beginner, ParseConfig("", "900", "")).holdFrames == 60);     // Beginner is unaffected
    CHECK(!ParseConfig("Beginner", "", "").forceBeginner && !ParseConfig("", "", "").forceBeginner);

    // The save byte.
    CHECK(DifficultyFromByte(0) == Difficulty::Beginner && DifficultyFromByte(1) == Difficulty::Standard &&
          DifficultyFromByte(2) == Difficulty::Proud && DifficultyFromByte(3) == Difficulty::Critical &&
          DifficultyFromByte(4) == Difficulty::Unknown && DifficultyFromByte(0xFF) == Difficulty::Unknown);

    std::printf(failures ? "DownedPolicy: %d FAILED\n" : "DownedPolicy: all checks passed\n", failures);
    return failures ? 1 : 0;
}
