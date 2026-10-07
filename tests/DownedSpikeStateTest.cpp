// VUH-1504 offline controls for inject/src/DownedSpikeState.hpp: gate
// decisions, revive HP rules, publish kinds, episode minting and re-minting,
// and the fixture channel layout (printed as JSON; the live fixture
// run_downed.py mirrors it). Pure rules only; no game process.
#include "DownedSpikeState.hpp"

#include <cstdio>

using namespace kh2coop::inject::downedspike;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static GateFacts Good() {
    GateFacts f {};
    f.enabled = f.ownerThread = f.actorTracked = f.actorIsPlayer = f.actorIsHead = true;
    f.deadFlag = f.taskIdle = f.stateReady = true;
    return f;
}

int main() {
    CHECK(Decide(Good()) == GateReason::Intercept);
    { auto f = Good(); f.enabled = false; CHECK(Decide(f) == GateReason::Off); }
    { auto f = Good(); f.ownerThread = false; CHECK(Decide(f) == GateReason::ForeignThread); }
    { auto f = Good(); f.actorTracked = false; CHECK(Decide(f) == GateReason::NotCanonical); }
    { auto f = Good(); f.actorIsPlayer = false; CHECK(Decide(f) == GateReason::NotCanonical); }
    { auto f = Good(); f.actorIsHead = false; CHECK(Decide(f) == GateReason::NotCanonical); }
    { auto f = Good(); f.stateReady = false; CHECK(Decide(f) == GateReason::WrongState); }
    { auto f = Good(); f.deadFlag = false; CHECK(Decide(f) == GateReason::NotDead); }
    { auto f = Good(); f.taskIdle = false; CHECK(Decide(f) == GateReason::TaskActive); }
    // Every single-fact mutation refuses.
    for (int bit = 0; bit < 8; ++bit) {
        auto f = Good();
        bool* fields[] = {&f.enabled, &f.ownerThread, &f.actorTracked, &f.actorIsPlayer,
                          &f.actorIsHead, &f.deadFlag, &f.taskIdle, &f.stateReady};
        *fields[bit] = false;
        CHECK(Decide(f) != GateReason::Intercept);
    }

    CHECK(ReviveTarget(0) == 0);
    CHECK(ReviveTarget(-5) == 0);
    CHECK(ReviveTarget(1) == 1);
    CHECK(ReviveTarget(3) == 1);
    CHECK(ReviveTarget(4) == 1);
    CHECK(ReviveTarget(20) == 5);
    CHECK(ReviveTarget(160) == 40);
    CHECK(ReviveTarget(2147483647) == 536870911);

    CHECK(GuardedReviveDelta(160, 0, 0, 160, 40) == 40);   // the native +max heal from 0
    CHECK(GuardedReviveDelta(160, 0, 10, 160, 40) == 30);
    CHECK(GuardedReviveDelta(5, 0, 0, 160, 40) == 5);      // not the +max shape: untouched
    CHECK(GuardedReviveDelta(161, 0, 0, 160, 40) == 161);
    CHECK(GuardedReviveDelta(160, 1, 0, 160, 40) == 160);  // not HP
    CHECK(GuardedReviveDelta(-5, 0, 0, 160, 40) == -5);    // damage untouched
    CHECK(GuardedReviveDelta(0, 0, 0, 160, 40) == 0);
    CHECK(GuardedReviveDelta(160, 0, 40, 160, 40) == 160); // already at target
    CHECK(GuardedReviveDelta(160, 0, 0, 160, 0) == 160);   // no target
    CHECK(GuardedReviveDelta(30, 0, 0, 30, 40) == 30);     // target above max
    CHECK(GuardedReviveDelta(160, 0, -1, 160, 40) == 160);

    CHECK(PublishFor(State::Off, false) == PublishKind::None);
    CHECK(PublishFor(State::Off, true) == PublishKind::None);
    CHECK(PublishFor(State::Ready, false) == PublishKind::Alive);
    CHECK(PublishFor(State::Ready, true) == PublishKind::Unavailable);
    CHECK(PublishFor(State::Downed, true) == PublishKind::Downed);
    CHECK(PublishFor(State::Refused, true) == PublishKind::Unavailable); // never downed=false while dead
    CHECK(PublishFor(State::Refused, false) == PublishKind::Alive);

    CHECK(StickActive(0.0f, 1.0f) && StickActive(-0.2f, 0.0f) && !StickActive(0.05f, 0.05f) && !StickActive(0.0f, 0.0f));
    CHECK(ShouldRemint(true, true, 0, true));                       // native refusal while downed
    CHECK(ShouldRemint(true, true, kEpisodeRemintFrames, false));   // no accepted request in time
    CHECK(!ShouldRemint(true, true, kEpisodeRemintFrames - 1, false));
    CHECK(!ShouldRemint(false, true, 0, true));                      // not downed (revived/refused)
    CHECK(!ShouldRemint(true, false, 0, true));                      // dead flag already cleared
    CHECK(FirstEpisodeBase(0) == (1ull << 32));
    CHECK(FirstEpisodeBase(0xABCD1234u) == (0xABCD1234ull << 32));
    {
        const auto base = FirstEpisodeBase(0xFFFFFFFFu);
        auto e = base;
        for (int i = 0; i < 5; ++i) { const auto n = NextEpisode(e); CHECK(n > e && n != 0); e = n; }
        CHECK(NextEpisode(base) != base && (NextEpisode(base) >> 32) == 0xFFFFFFFFu);
    }

    std::printf("{\"ok\":%s,\"size\":%zu,\"offsets\":{", failures ? "false" : "true", sizeof(Channel));
    std::printf("%s\"magic\":%zu", "", offsetof(Channel, magic));
    std::printf("%s\"version\":%zu", ",", offsetof(Channel, version));
    std::printf("%s\"requestSeq\":%zu", ",", offsetof(Channel, requestSeq));
    std::printf("%s\"doneSeq\":%zu", ",", offsetof(Channel, doneSeq));
    std::printf("%s\"command\":%zu", ",", offsetof(Channel, command));
    std::printf("%s\"result\":%zu", ",", offsetof(Channel, result));
    std::printf("%s\"liveFrame\":%zu", ",", offsetof(Channel, liveFrame));
    std::printf("%s\"state\":%zu", ",", offsetof(Channel, state));
    std::printf("%s\"actor\":%zu", ",", offsetof(Channel, actor));
    std::printf("%s\"hp\":%zu", ",", offsetof(Channel, hp));
    std::printf("%s\"maxHp\":%zu", ",", offsetof(Channel, maxHp));
    std::printf("%s\"flags9B8\":%zu", ",", offsetof(Channel, flags9B8));
    std::printf("%s\"deadAction\":%zu", ",", offsetof(Channel, deadAction));
    std::printf("%s\"controllerOff\":%zu", ",", offsetof(Channel, controllerOff));
    std::printf("%s\"publishKind\":%zu", ",", offsetof(Channel, publishKind));
    std::printf("%s\"gameOverTask\":%zu", ",", offsetof(Channel, gameOverTask));
    std::printf("%s\"episode\":%zu", ",", offsetof(Channel, episode));
    std::printf("%s\"gateCount\":%zu", ",", offsetof(Channel, gateCount));
    std::printf("%s\"passCount\":%zu", ",", offsetof(Channel, passCount));
    std::printf("%s\"killCount\":%zu", ",", offsetof(Channel, killCount));
    std::printf("%s\"reviveCount\":%zu", ",", offsetof(Channel, reviveCount));
    std::printf("%s\"downedFrame\":%zu", ",", offsetof(Channel, downedFrame));
    std::printf("%s\"downedFrames\":%zu", ",", offsetof(Channel, downedFrames));
    std::printf("%s\"actionLostFrames\":%zu", ",", offsetof(Channel, actionLostFrames));
    std::printf("%s\"deadFlagLostFrames\":%zu", ",", offsetof(Channel, deadFlagLostFrames));
    std::printf("%s\"controllerOnFrames\":%zu", ",", offsetof(Channel, controllerOnFrames));
    std::printf("%s\"statCallsWhileDowned\":%zu", ",", offsetof(Channel, statCallsWhileDowned));
    std::printf("%s\"hpChangesWhileDowned\":%zu", ",", offsetof(Channel, hpChangesWhileDowned));
    std::printf("%s\"reviveTarget\":%zu", ",", offsetof(Channel, reviveTarget));
    std::printf("%s\"reviveHpAfter\":%zu", ",", offsetof(Channel, reviveHpAfter));
    std::printf("%s\"reviveGuardUsed\":%zu", ",", offsetof(Channel, reviveGuardUsed));
    std::printf("%s\"installMask\":%zu", ",", offsetof(Channel, installMask));
    std::printf("%s\"lastGateMode\":%zu", ",", offsetof(Channel, lastGateMode));
    std::printf("%s\"lastPassReason\":%zu", ",", offsetof(Channel, lastPassReason));
    std::printf("%s\"hitAttemptsWhileDowned\":%zu", ",", offsetof(Channel, hitAttemptsWhileDowned));
    std::printf("%s\"enemyHitAttemptsWhileDowned\":%zu", ",", offsetof(Channel, enemyHitAttemptsWhileDowned));
    std::printf("%s\"holdLostReason\":%zu", ",", offsetof(Channel, holdLostReason));
    std::printf("%s\"posX\":%zu", ",", offsetof(Channel, posX));
    std::printf("%s\"posY\":%zu", ",", offsetof(Channel, posY));
    std::printf("%s\"posZ\":%zu", ",", offsetof(Channel, posZ));
    std::printf("%s\"velX\":%zu", ",", offsetof(Channel, velX));
    std::printf("%s\"velZ\":%zu", ",", offsetof(Channel, velZ));
    std::printf("%s\"motionId\":%zu", ",", offsetof(Channel, motionId));
    std::printf("%s\"motionTime\":%zu", ",", offsetof(Channel, motionTime));
    std::printf("%s\"stickX\":%zu", ",", offsetof(Channel, stickX));
    std::printf("%s\"stickY\":%zu", ",", offsetof(Channel, stickY));
    std::printf("%s\"inputActive\":%zu", ",", offsetof(Channel, inputActive));
    std::printf("%s\"inputFramesWhileDowned\":%zu", ",", offsetof(Channel, inputFramesWhileDowned));
    std::printf("%s\"noInputFramesWhileDowned\":%zu", ",", offsetof(Channel, noInputFramesWhileDowned));
    std::printf("%s\"driftWithInput\":%zu", ",", offsetof(Channel, driftWithInput));
    std::printf("%s\"driftWithoutInput\":%zu", ",", offsetof(Channel, driftWithoutInput));
    std::printf("%s\"arg\":%zu", ",", offsetof(Channel, arg));
    std::printf("%s\"invulnTimer\":%zu", ",", offsetof(Channel, invulnTimer));
    std::printf("%s\"graceHits\":%zu", ",", offsetof(Channel, graceHits));
    std::printf("%s\"graceEnemyHits\":%zu", ",", offsetof(Channel, graceEnemyHits));
    std::printf("%s\"graceHpDrops\":%zu", ",", offsetof(Channel, graceHpDrops));
    std::printf("%s\"graceActive\":%zu", ",", offsetof(Channel, graceActive));
    std::printf("%s\"netPublished\":%zu", ",", offsetof(Channel, netPublished));
    std::printf("%s\"netEpoch\":%zu", ",", offsetof(Channel, netEpoch));
    std::printf("%s\"reviveSeen\":%zu", ",", offsetof(Channel, reviveSeen));
    std::printf("%s\"reviveGateRefused\":%zu", ",", offsetof(Channel, reviveGateRefused));
    std::printf("%s\"reviveConsumed\":%zu", ",", offsetof(Channel, reviveConsumed));
    std::printf("%s\"reviveNativeRefused\":%zu", ",", offsetof(Channel, reviveNativeRefused));
    std::printf("%s\"revivedByRequest\":%zu", ",", offsetof(Channel, revivedByRequest));
    std::printf("%s\"requestsSent\":%zu", ",", offsetof(Channel, requestsSent));
    std::printf("%s\"requestFailures\":%zu", ",", offsetof(Channel, requestFailures));
    std::printf("%s\"puppetSlot\":%zu", ",", offsetof(Channel, puppetSlot));
    std::printf("%s\"puppetDowned\":%zu", ",", offsetof(Channel, puppetDowned));
    std::printf("%s\"puppetEpisode\":%zu", ",", offsetof(Channel, puppetEpisode));
    std::printf("%s\"lastSentSeq\":%zu", ",", offsetof(Channel, lastSentSeq));
    std::printf("%s\"localSlot\":%zu", ",", offsetof(Channel, localSlot));
    std::printf("%s\"episodeRemints\":%zu", ",", offsetof(Channel, episodeRemints));
    std::printf("%s\"episodeFrames\":%zu", ",", offsetof(Channel, episodeFrames));
    std::puts("}}");
    return failures ? 1 : 0;
}
