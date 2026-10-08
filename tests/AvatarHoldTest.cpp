// AvatarHoldTest — VUH-1787 offline gate: a remote avatar stream that stalls
// for 1-3 s keeps its puppet active, held at its last pose and marked
// AvatarHeld, and releases only past releaseAfterMs. Room, cutscene, roster,
// session and connection exits stay immediate. Pure AvatarSync: no ENet, no
// sockets, no KH2. Ends with a seeded 60 Hz soak of random stalls.
// Exit code 0 = all checks passed.

#include "kh2coop/AvatarSync.hpp"
#include "PuppetHold.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>

using namespace kh2coop;

namespace {

int g_errors = 0;
int g_checks = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_errors;
        std::cout << "  FAIL: " << what << "\n";
    } else {
        std::cout << "  ok:   " << what << "\n";
    }
}

constexpr std::array<std::uint64_t, 3> kRoster {0x100000001ULL, 0x200000002ULL, 0x300000003ULL};
constexpr std::uint16_t kWorld = 4;
constexpr std::uint16_t kRoom = 26;

AvatarRelay pose(SlotType owner, std::uint64_t t, float x, std::uint16_t room = kRoom,
                 std::uint8_t flags = 0) {
    AvatarRelay r;
    r.ownerConnectionId = kRoster[static_cast<unsigned>(owner)];
    r.avatar.ownerSlot = owner;
    r.avatar.seq = static_cast<std::uint32_t>(t);
    r.avatar.serverTimeMs = t;
    r.avatar.worldId = kWorld;
    r.avatar.roomId = room;
    r.avatar.position = {x, 0.0f, 0.0f};
    r.avatar.flags = flags;
    return r;
}

bool held(const PuppetTarget& t) { return t.active && (t.pose.flags & AvatarHeld); }
bool live(const PuppetTarget& t) { return t.active && !(t.pose.flags & AvatarHeld); }

// Local player is Friend1, so puppet 0 shows the host (slot 0).
// Feed a still host at x=500 every 50 ms up to `until`.
AvatarSync streamed(std::uint64_t until, AvatarSync::Config config = {}) {
    AvatarSync sync(SlotType::Friend1, config);
    sync.setRoster(SlotType::Friend1, kRoster);
    for (std::uint64_t t = 1000; t <= until; t += 50) sync.onRemote(pose(SlotType::Player, t, 500.0f));
    return sync;
}

void testStalls() {
    std::cout << "\n=== Injected stalls ===\n";
    for (const std::uint64_t stall : {1000ULL, 1500ULL, 2000ULL, 2500ULL, 2900ULL}) {
        auto sync = streamed(5000);
        bool alwaysActive = true, everHeld = false, posStable = true;
        for (std::uint64_t now = 5000; now <= 5000 + stall; now += 16) {
            const auto t = sync.sample(now, kWorld, kRoom)[0];
            alwaysActive = alwaysActive && t.active;
            everHeld = everHeld || held(t);
            posStable = posStable && std::fabs(t.pose.position.x - 500.0f) < 1e-3f;
        }
        sync.onRemote(pose(SlotType::Player, 5000 + stall, 500.0f));
        const auto resumed = sync.sample(5000 + stall + 1, kWorld, kRoom)[0];
        const std::string ms = std::to_string(stall) + " ms";
        check(alwaysActive, ms + " stall: puppet never released");
        check(everHeld == (stall > 1000), ms + " stall: held only past staleAfterMs");
        check(posStable, ms + " stall: held at the last pose");
        check(live(resumed), ms + " stall: resumes live, marker cleared");
    }

    auto sync = streamed(5000);
    check(live(sync.sample(6000, kWorld, kRoom)[0]), "exactly 1000 ms old is still live");
    check(held(sync.sample(6001, kWorld, kRoom)[0]), "1001 ms old is held");
    check(held(sync.sample(8000, kWorld, kRoom)[0]), "3000 ms old is still held");
    check(!sync.sample(8001, kWorld, kRoom)[0].active, "3001 ms old is released");
    check(!sync.sample(8100, kWorld, kRoom)[0].active, "3.1 s stall stays released");
    sync.onRemote(pose(SlotType::Player, 8100, 700.0f));
    sync.onRemote(pose(SlotType::Player, 8150, 700.0f));
    const auto back = sync.sample(8300, kWorld, kRoom)[0];
    check(live(back) && std::fabs(back.pose.position.x - 700.0f) < 1e-3f, "stream after release shows again");
}

void testHeldPoseContinuity() {
    std::cout << "\n=== Held pose continuity ===\n";
    // Moving host: +100 units per 100 ms. The last live sample is the capped
    // 100 ms extrapolation; the held pose must equal it (no snap back).
    AvatarSync sync(SlotType::Friend1, {});
    sync.setRoster(SlotType::Friend1, kRoster);
    for (std::uint64_t t = 1000; t <= 2000; t += 100) sync.onRemote(pose(SlotType::Player, t, static_cast<float>(t)));
    const auto lastLive = sync.sample(3000, kWorld, kRoom)[0];
    const auto firstHeld = sync.sample(3001, kWorld, kRoom)[0];
    const auto lateHeld = sync.sample(4900, kWorld, kRoom)[0];
    check(live(lastLive) && held(firstHeld) && held(lateHeld), "live then held");
    check(std::fabs(lastLive.pose.position.x - firstHeld.pose.position.x) < 1e-3f &&
              std::fabs(firstHeld.pose.position.x - lateHeld.pose.position.x) < 1e-3f,
          "held pose is continuous with the last live pose and does not drift");
    check(std::fabs(firstHeld.pose.position.x - 2100.0f) < 1e-3f, "held pose is capped at 100 ms of extrapolation");
}

void testImmediateExits() {
    std::cout << "\n=== Immediate releases inside a hold ===\n";
    auto sync = streamed(5000);
    check(held(sync.sample(6500, kWorld, kRoom)[0]), "precondition: held");
    check(!sync.sample(6500, kWorld, kRoom + 1)[0].active, "local room change releases a held puppet at once");

    sync = streamed(5000);
    sync.clear();
    check(!sync.sample(6500, kWorld, kRoom)[0].active, "clear() (session/world reset) releases at once");

    sync = streamed(5000);
    auto replaced = kRoster; replaced[0] += 0x100000000ULL;
    sync.setRoster(SlotType::Friend1, replaced);
    check(!sync.sample(6500, kWorld, kRoom)[0].active, "host replacement releases at once");

    sync = streamed(5000);
    sync.setRoster(static_cast<SlotType>(0xFF), {});
    check(!sync.sample(6500, kWorld, kRoom)[0].active, "connection end (empty roster) releases at once");

    sync = streamed(5000);
    sync.onRemote(pose(SlotType::Player, 5050, 500.0f, kRoom, AvatarInCutscene));
    check(!sync.sample(5100 + 1200, kWorld, kRoom)[0].active, "owner's cutscene snapshot then stall: not shown");

    sync = streamed(5000);
    sync.onRemote(pose(SlotType::Player, 5050, 500.0f, kRoom + 1));
    check(!sync.sample(5050 + 1500, kWorld, kRoom)[0].active, "owner left the room then stalled: not shown");
}

void testMarkerIsLocal() {
    std::cout << "\n=== AvatarHeld is receiver-local ===\n";
    AvatarSync sync(SlotType::Friend1, {});
    sync.setRoster(SlotType::Friend1, kRoster);
    sync.onRemote(pose(SlotType::Player, 1000, 0.0f, kRoom, AvatarHeld | AvatarDowned));
    sync.onRemote(pose(SlotType::Player, 1100, 0.0f, kRoom, AvatarHeld | AvatarDowned));
    const auto t = sync.sample(1200, kWorld, kRoom)[0];
    check(live(t), "peer-supplied AvatarHeld is stripped on admission");
    check((t.pose.flags & AvatarDowned) != 0, "other flags are kept");
    check(held(sync.sample(2200, kWorld, kRoom)[0]) &&
              (sync.sample(2200, kWorld, kRoom)[0].pose.flags & AvatarDowned),
          "held pose keeps the owner's last flags");
}

void testConfig() {
    std::cout << "\n=== Config ===\n";
    auto legacy = streamed(5000, {120, 1000, 1000});
    check(legacy.releaseAfterMs() == 1000, "releaseAfterMs=1000 is the old timeout");
    check(live(legacy.sample(6000, kWorld, kRoom)[0]) && !legacy.sample(6001, kWorld, kRoom)[0].active,
          "releaseAfterMs=1000 releases at 1 s with no held band");
    auto clamped = streamed(5000, {120, 1000, 200});
    check(clamped.releaseAfterMs() == 1000 && !clamped.sample(6001, kWorld, kRoom)[0].active,
          "releaseAfterMs below staleAfterMs clamps to staleAfterMs");
    check(AvatarSync::Config {}.releaseAfterMs == 3000, "default hold releases after 3000 ms");
}

void testSoak() {
    std::cout << "\n=== Soak: 60 Hz, two owners, 10 min, random 0.2-3.5 s stalls ===\n";
    std::mt19937 rng(1787);
    std::uniform_int_distribution<int> gapMs(2000, 12000), stallMs(200, 3500);
    AvatarSync sync(SlotType::Player, {});
    sync.setRoster(SlotType::Player, kRoster);
    constexpr std::uint64_t kStart = 10000, kEnd = kStart + 600000;
    struct Owner {
        SlotType slot;
        std::uint64_t stallStart, stallEnd {0}, newest {0}, lastAge {0};
        float x {0.0f};
        bool wasActive {false};
    };
    std::array<Owner, 2> owners {{{SlotType::Friend1, kStart + 3000}, {SlotType::Friend2, kStart + 5000}}};
    int violations = 0, stalls = 0, over1s = 0, over3s = 0, releases = 0;
    for (std::uint64_t now = kStart; now < kEnd; now += 16) {
        for (auto& o : owners) {
            if (now >= o.stallStart && o.stallEnd < o.stallStart) {       // stall begins
                o.stallEnd = now + static_cast<std::uint64_t>(stallMs(rng));
                ++stalls;
            }
            if (now >= o.stallEnd && o.stallEnd >= o.stallStart) {       // stall over
                o.stallStart = now + static_cast<std::uint64_t>(gapMs(rng));
            }
            const bool stalled = now >= o.stallStart && now < o.stallEnd;
            if (!stalled && now % 48 == 0) {
                o.x += 1.0f;                                                // ~21 units/s
                if (sync.onRemote(pose(o.slot, now, o.x))) o.newest = now;
            }
        }
        const auto t = sync.sample(now, kWorld, kRoom);
        for (int i = 0; i < 2; ++i) {
            auto& o = owners[i];
            if (!o.newest) continue;
            const std::uint64_t age = now - o.newest;
            // Edges, so a stall that ends while its data is still old counts once.
            if (age > 1000 && o.lastAge <= 1000) ++over1s;
            if (age > 3000 && o.lastAge <= 3000) ++over3s;
            o.lastAge = age;
            const bool expectActive = age <= 3000, expectHeld = age > 1000 && age <= 3000;
            if (t[i].active != expectActive || (t[i].active && held(t[i]) != expectHeld)) ++violations;
            // Held = newest plus at most 100 ms of extrapolation (~2.1 units here).
            if (held(t[i]) && std::fabs(t[i].pose.position.x - o.x) > 2.5f) ++violations;
            if (o.wasActive && !t[i].active) ++releases;
            o.wasActive = t[i].active;
        }
    }
    std::cout << "  stalls=" << stalls << " over1s=" << over1s << " over3s=" << over3s
              << " releases=" << releases << " (the old 1 s rule: " << over1s << ")\n";
    check(violations == 0, "every tick: active iff age <= 3000, held iff 1000 < age <= 3000, held near the last pose");
    check(stalls > 100 && over3s > 0 && over1s > over3s, "soak exercised short and long stalls");
    check(releases == over3s, "releases happen exactly when data age crosses 3 s");
}


void testWorldAndMovedResume() {
    std::cout << "\n=== World switch and moved resume (rev2) ===\n";
    auto sync = streamed(5000);
    check(held(sync.sample(6500, kWorld, kRoom)[0]), "precondition: held");
    check(!sync.sample(6500, kWorld + 1, kRoom)[0].active, "local world change releases a held puppet at once");

    sync = streamed(5000);
    auto other = pose(SlotType::Player, 5050, 500.0f);
    other.avatar.worldId = kWorld + 1;
    sync.onRemote(other);
    check(!sync.sample(5050 + 1500, kWorld, kRoom)[0].active, "owner switched world then stalled: not shown");

    // Owner stalls for 2 s at x=500, then reappears at x=900 and keeps streaming.
    sync = streamed(5000);
    auto moving = pose(SlotType::Player, 5050, 500.0f);
    moving.avatar.velocity = {5.0f, 0.0f, 5.0f};
    sync.onRemote(moving);
    const auto movingLive = sync.sample(5300, kWorld, kRoom)[0];
    check(live(movingLive) && movingLive.pose.velocity.x == 5.0f, "control: a live pose carries its velocity");
    const auto h = sync.sample(6900, kWorld, kRoom)[0];
    check(held(h) && h.pose.velocity.x == 0.0f && h.pose.velocity.z == 0.0f, "held pose has zero velocity");
    for (std::uint64_t t = 7000; t <= 7400; t += 50) sync.onRemote(pose(SlotType::Player, t, 900.0f));
    bool allActive = true, anyHeld = false;
    float first = 0.0f;
    for (std::uint64_t now = 7001; now <= 7400; now += 16) {
        const auto t = sync.sample(now, kWorld, kRoom)[0];
        if (now == 7001) first = t.pose.position.x;
        allActive = allActive && t.active;
        anyHeld = anyHeld || held(t);
    }
    const auto settled = sync.sample(7400, kWorld, kRoom)[0];
    check(allActive && !anyHeld, "moved resume stays active and live");
    check(first > 500.0f && first <= 900.0f, "first resumed sample is on the way to the new position");
    check(std::fabs(settled.pose.position.x - 900.0f) < 1e-3f, "puppet reaches the moved owner within the render delay");
}

void testHoldEnvParsing() {
    std::cout << "\n=== KH2COOP_AVATAR_HOLD_MS parsing ===\n";
    check(!parseAvatarHoldMs(""), "empty rejected");
    check(!parseAvatarHoldMs("999"), "999 rejected");
    check(parseAvatarHoldMs("1000") == 1000u, "1000 accepted (old behaviour)");
    check(parseAvatarHoldMs("3000") == 3000u, "3000 accepted");
    check(parseAvatarHoldMs("10000") == 10000u, "10000 accepted");
    check(!parseAvatarHoldMs("10001"), "10001 rejected");
    check(!parseAvatarHoldMs("100000"), "6 digits rejected");
    check(!parseAvatarHoldMs("3000ms") && !parseAvatarHoldMs(" 3000") && !parseAvatarHoldMs("+3000") &&
              !parseAvatarHoldMs("-1") && !parseAvatarHoldMs("3e3"),
          "non-digits rejected");
    check(!parseAvatarHoldMs("99999999999999999999"), "overflow-length input rejected");
}

void testOwnRoomLoad() {
    std::cout << "\n=== Sender: own room load is flagged, no door ghost (B1) ===\n";
    namespace ph = kh2coop::inject::puppethold;
    ph::LocalPublisher pub;
    check(!pub.OnTransition(1), "no re-publish before any capture");
    AvatarState a; a.worldId = kWorld; a.roomId = kRoom; a.position = {500.0f, 0.0f, 0.0f};
    const auto live = pub.Captured(a, false);
    check(!(live.flags & AvatarInCutscene), "normal frame is not flagged");
    const auto flagged = pub.OnTransition(77);
    check(flagged && (flagged->flags & AvatarInCutscene) && flagged->seq == 77 &&
              flagged->position.x == 500.0f, "transition re-publishes the last pose with the cutscene flag");
    check(!pub.OnTransition(78), "flag published once per transition");
    const auto during = pub.Captured(a, true);
    check((during.flags & AvatarInCutscene) != 0, "capture while the load is pending is flagged");
    check(!pub.OnTransition(79), "already flagged: no extra re-publish");
    pub.Captured(a, false);
    check(pub.OnTransition(80).has_value(), "next transition re-arms after a live frame");

    // Receiver side: the owner's last snapshot before its load gap is the
    // flagged one, so the puppet hides at once instead of holding a ghost.
    auto sync = streamed(5000);
    AvatarRelay door = pose(SlotType::Player, 5016, 500.0f, kRoom, AvatarInCutscene);
    sync.onRemote(door);
    bool ghost = false;
    for (std::uint64_t now = 5020; now <= 5016 + 3500; now += 16)
        ghost = ghost || (sync.sample(now, kWorld, kRoom)[0].active && now > 5016 + latency::kAvatarDelayMs);
    check(!ghost, "receiver shows no door ghost through a 3.5 s own-load gap");
    const auto unflaggedGap = streamed(5000).sample(5000 + 2500, kWorld, kRoom)[0];
    check(held(unflaggedGap), "control: the same gap without the flag would hold (the ghost B1 removes)");
}

void testHeldMotion() {
    std::cout << "\n=== Receiver: held puppet idles after a short grace (F1) ===\n";
    namespace ph = kh2coop::inject::puppethold;
    constexpr std::uint32_t kRun = 2, kIdle = 0, kDown = 77;
    const std::uint8_t heldFlags = AvatarHeld;
    check(ph::HeldMotion(kRun, 0, 100, kIdle) == kRun, "live pose keeps its motion");
    check(ph::HeldMotion(kRun, heldFlags, ph::kHeldIdleFrames - 1, kIdle) == kRun, "held under the grace keeps its motion");
    check(ph::HeldMotion(kRun, heldFlags, ph::kHeldIdleFrames, kIdle) == kIdle, "held past the grace idles");
    check(ph::HeldMotion(kDown, heldFlags | AvatarDowned, 1000, kIdle) == kDown, "held downed pose keeps its clip");
    check(!ph::SnapMotionTime(heldFlags) && ph::SnapMotionTime(0), "motion clock snaps only for live poses");
    ph::HeldClock clock;
    clock.Note(false, 10);
    check(clock.Frames(20) == 0, "clock idle while live");
    clock.Note(true, 30); clock.Note(true, 31); clock.Note(true, 44);
    check(clock.Frames(45) == 15, "clock counts from the first held frame");
    clock.Note(false, 46);
    check(clock.Frames(50) == 0, "resume resets the clock");
    clock.Note(true, 60);
    check(clock.Frames(61) == 1, "a new hold restarts the count");
}

} // namespace

int main() {
    testStalls();
    testHeldPoseContinuity();
    testImmediateExits();
    testMarkerIsLocal();
    testConfig();
    testWorldAndMovedResume();
    testHoldEnvParsing();
    testOwnRoomLoad();
    testHeldMotion();
    testSoak();
    std::cout << "\n=======================================\n"
              << (g_errors == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED: " + std::to_string(g_errors))
              << " (" << g_checks << " checks)\n";
    return g_errors == 0 ? 0 : 1;
}
