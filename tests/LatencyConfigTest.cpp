#include "kh2coop/AvatarSync.hpp"
#include "EnemyMirrorState.hpp"
#include <iostream>
#include <string_view>

namespace {
int checks = 0, failures = 0;
void check(bool ok, std::string_view name) {
    ++checks;
    if (!ok) ++failures;
    std::cout << (ok ? "PASS: " : "FAIL: ") << name << '\n';
}
}

int main() {
    using namespace kh2coop;
    namespace em = inject::enemymirror;
    check(AvatarSync::Config{}.renderDelayMs == 80 && em::Stream{}.delayFrames() == 6,
          "unset defaults use qualified 80 ms and 6 frames");
    for (auto text : {"1", "50", "80", "120", "500"})
        check(latency::parseAvatarDelayMs(text).has_value(), "avatar bounds and A/B values accepted");
    for (auto text : {"4", "6", "9", "18"})
        check(latency::parseEnemyDelayFrames(text).has_value(), "enemy bounds and A/B values accepted");
    for (auto text : {"", "-1", "+4", "4 ", " 4", "4x", "4.0", "99999999999999999999"}) {
        check(!latency::parseAvatarDelayMs(text), "malformed avatar setting rejected");
        check(!latency::parseEnemyDelayFrames(text), "malformed enemy setting rejected");
    }
    check(!latency::parseAvatarDelayMs("501"), "avatar over bound rejected");
    for (auto text : {"0", "00", "000", "501", "999", "-1", "120x"}) {
        const auto parsed = latency::parseAvatarDelayMs(text);
        check(!parsed && parsed.value_or(latency::kAvatarDelayMs) == 80,
              "invalid avatar environment setting retains 80 ms fallback");
    }
    for (auto text : {"0", "00", "000", "3", "19", "999", "-1", "9x"}) {
        const auto parsed = latency::parseEnemyDelayFrames(text);
        check(!parsed && em::Stream(parsed.value_or(latency::kEnemyDelayFrames)).delayFrames() == 6,
              "invalid enemy environment setting retains six-frame fallback");
    }
    check(!latency::parseEnemyDelayFrames("3") && !latency::parseEnemyDelayFrames("19"),
          "enemy bound preserves cursor hysteresis and maximum lag");
    check(em::Stream(0).delayFrames() == 6 && em::Stream(19).delayFrames() == 6,
          "invalid direct enemy configuration falls back to default");

    AvatarSync baseline(SlotType::Player);
    AvatarSync::Config config;
    config.renderDelayMs = 50;
    AvatarSync candidate(SlotType::Player, config);
    baseline.setRoster(SlotType::Player, {1, 2, 0});
    candidate.setRoster(SlotType::Player, {1, 2, 0});
    for (std::uint32_t t = 600; t <= 1000; t += 100) {
        AvatarRelay r;
        r.ownerConnectionId = 2;
        r.avatar.ownerSlot = SlotType::Friend1;
        r.avatar.serverTimeMs = t;
        r.avatar.seq = t;
        r.avatar.worldId = 4; r.avatar.roomId = 26;
        r.avatar.position.x = static_cast<float>(t);
        check(baseline.onRemote(r) && candidate.onRemote(r), "both buffers admit the same authenticated snapshots");
    }
    const auto b = baseline.sample(1000, 4, 26)[0];
    const auto c = candidate.sample(1000, 4, 26)[0];
    check(b.active && c.active && b.pose.position.x == 920 && c.pose.position.x == 950,
          "50 ms avatar configuration changes sampling, not ownership or capture");
    check((baseline.sample(2200, 4, 26)[0].pose.flags & AvatarHeld) &&
          (candidate.sample(2200, 4, 26)[0].pose.flags & AvatarHeld), "hold threshold unchanged in both arms");
    check(!baseline.sample(4001, 4, 26)[0].active && !candidate.sample(4001, 4, 26)[0].active,
          "release threshold unchanged in both arms");

    em::Stream defaultStream, explicitDefault(6), tuned(4);
    EnemyMotion packet;
    packet.epoch = 1; packet.sequence = 1; packet.hostFrame = 100;
    EnemyMotionEntry entry;
    entry.netId = 1; entry.objectId = em::kShadowObjectId;
    packet.entries.push_back(entry);
    defaultStream.Ingest(packet, 1); explicitDefault.Ingest(packet, 1); tuned.Ingest(packet, 1);
    defaultStream.Tick(1); explicitDefault.Tick(1); tuned.Tick(1);
    check(defaultStream.cursor() == 94 && tuned.cursor() == 96, "configured enemy delay applies at first cursor");
    for (std::uint32_t local = 2; local <= 180; ++local) {
        if (local % 3 == 0 && (local < 60 || local > 100)) {
            ++packet.sequence; packet.hostFrame += 3;
            defaultStream.Ingest(packet, local); explicitDefault.Ingest(packet, local); tuned.Ingest(packet, local);
        }
        defaultStream.Tick(local); explicitDefault.Tick(local); tuned.Tick(local);
        check(defaultStream.cursor() == explicitDefault.cursor(), "default and explicit 6-frame stream agree through stall/recovery");
    }
    check(tuned.stats().underrunFrames > 0 && tuned.stats().releases > 0 && tuned.stats().retakes > 0,
          "stall observations count clock clamps, releases and retakes");
    tuned.Reset(2);
    packet.epoch = 2; ++packet.sequence; packet.hostFrame = 500;
    tuned.Ingest(packet, 181); tuned.Tick(181);
    check(tuned.cursor() == 496 && tuned.delayFrames() == 4, "epoch reset retains configured delay");
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
