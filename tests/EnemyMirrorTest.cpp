// VUH-1515 step 2: EnemyMotion codec, relay forwarding rules and the pure
// client stream state (inject/src/EnemyMirrorState.hpp). Real ENet relay on
// loopback for the forwarding cases; no KH2, no hooks.
#include "kh2coop/Codec.hpp"
#include "WorldWireFixture.hpp"
#include "kh2coop/SessionHost.hpp"
#include "EnemyMirrorState.hpp"
#include <enet/enet.h>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <string>
#include <limits>
#include <thread>
#include <vector>

using namespace kh2coop;
namespace em = kh2coop::inject::enemymirror;

namespace {
int checks = 0, failures = 0;
void check(bool good, const char* label) {
    ++checks;
    if (!good) ++failures;
    std::cout << (good ? "PASS: " : "FAIL: ") << label << '\n';
}

EnemyMotionEntry row(std::uint16_t netId, float x, std::uint32_t motion = 2, float time = 10.0f, float rot = 0.5f) {
    EnemyMotionEntry e;
    e.netId = netId;
    e.objectId = em::kShadowObjectId;
    e.motionId = motion;
    e.motionTime = time;
    e.position = {x, -20.0f, 5.0f};
    e.rotationY = rot;
    return e;
}
EnemyMotion motion(std::uint32_t epoch, std::uint64_t sequence, std::uint32_t frame, std::vector<EnemyMotionEntry> rows) {
    EnemyMotion m;
    m.epoch = epoch;
    m.sequence = sequence;
    m.hostFrame = frame;
    m.entries = std::move(rows);
    return m;
}
template <class F> bool throws(F&& f) {
    try { f(); } catch (const std::exception&) { return true; }
    return false;
}
bool decodeMotion(const std::vector<std::uint8_t>& packet, EnemyMotion& out) {
    const std::uint8_t* payload = nullptr;
    std::size_t length = 0;
    if (decodePacketHeader(packet.data(), packet.size(), payload, length) != PacketType::EnemyMotion) return false;
    ByteReader r(payload, length);
    read(r, out);
    return r.atEnd();
}

void testCodec() {
    const auto original = motion(7, 0xFEDCBA9876543210ULL, 1234, {row(1, 100.0f), row(5, -3.5f, 9, 33.25f, -2.0f)});
    const auto packet = encode(original);
    const std::uint8_t* payload = nullptr;
    std::size_t length = 0;
    check(decodePacketHeader(packet.data(), packet.size(), payload, length) == PacketType::EnemyMotion &&
              length == ENEMY_MOTION_HEADER_BYTES + 2 * ENEMY_MOTION_ENTRY_BYTES,
          "EnemyMotion wire size is header18 + 31 per entry");
    EnemyMotion round;
    check(decodeMotion(packet, round) && round.epoch == 7 && round.sequence == original.sequence && round.hostFrame == 1234 &&
              round.entries.size() == 2 && round.entries[1].netId == 5 && round.entries[1].motionId == 9 &&
              round.entries[1].motionTime == 33.25f && round.entries[1].rotationY == -2.0f &&
              round.entries[0].position.x == 100.0f && round.entries[0].objectId == em::kShadowObjectId,
          "EnemyMotion codec round trip preserves every field");
    check(isWorldPacket(PacketType::EnemyMotion) && isScopedWorldPacket(PacketType::EnemyMotion) &&
              !isMaterialWorldPacket(PacketType::EnemyMotion) && !isEphemeralWorldPacket(PacketType::EnemyMotion),
          "EnemyMotion is a scoped world packet, not material, not ephemeral");
    check(validateScopedWorldPacket(packet) == PacketType::EnemyMotion, "scoped world validation accepts a valid EnemyMotion");
    check(throws([] { (void)encode(motion(7, 0, 1, {row(1, 0)})); }), "encoder refuses sequence 0");
    check(throws([] { (void)encode(motion(0, 1, 1, {row(1, 0)})); }), "encoder refuses epoch 0");
    check(throws([] { (void)encode(motion(7, 1, 1, {row(1, 0), row(1, 2)})); }), "encoder refuses duplicate netId");
    check(throws([] { auto m = motion(7, 1, 1, {row(1, 0)}); m.entries[0].flags = 3; (void)encode(m); }), "encoder refuses reserved flag bits");
    check(throws([] { auto m = motion(7, 1, 1, {row(1, 0)}); m.entries[0].flags = 0; (void)encode(m); }), "encoder refuses a not-alive entry");
    check(throws([] { (void)encode(motion(7, 1, 1, {row(0, 0)})); }), "encoder refuses netId 0");
    check(throws([] { auto m = motion(7, 1, 1, {row(1, 0)}); m.entries[0].position.y = std::numeric_limits<float>::quiet_NaN(); (void)encode(m); }),
          "encoder refuses non-finite position");
    check(throws([] { std::vector<EnemyMotionEntry> rows; for (std::uint16_t i = 1; i <= 33; ++i) rows.push_back(row(i, 0)); (void)encode(motion(7, 1, 1, rows)); }),
          "encoder refuses more than 32 entries");
    // S3/S5 sanity bounds.
    check(throws([] { auto m = motion(7, 1, 1, {row(1, 0)}); m.entries[0].motionId = ENEMY_MOTION_MAX_MOTION_ID; (void)encode(m); }),
          "encoder refuses a motion id at the cap");
    check(throws([] { (void)encode(motion(7, 1, 1, {row(1, 0, 2, -1.0f)})); }), "encoder refuses a negative motion time");
    check(throws([] { (void)encode(motion(7, 1, 1, {row(1, 0, 2, 10001.0f)})); }), "encoder refuses a motion time past 10000");
    check(throws([] { (void)encode(motion(7, 1, 1, {row(1, 0, 2, 10.0f, 1e30f)})); }), "encoder refuses a huge yaw (1e30)");
    check(throws([] { (void)encode(motion(7, 1, 1, {row(1, 2e5f)})); }), "encoder refuses a position past 1e5");
    check(!throws([] { (void)encode(motion(7, 1, 1, {row(1, 99999.0f, 4095, 10000.0f, -64.0f)})); }), "values at the bounds are accepted");
    for (unsigned mode = 0; mode < 3; ++mode) {
        auto bad = packet;
        if (mode == 0) bad.pop_back();
        else if (mode == 1) bad.push_back(0);
        else { ByteWriter w; write(w, original); auto b = w.take(); b.pop_back(); bad = encodePacket(PacketType::EnemyMotion, b); }
        check(throws([&] { EnemyMotion m; (void)decodeMotion(bad, m); }), "truncated or suffixed EnemyMotion frame is rejected");
    }
    // A decoded NaN is rejected too (the wire is not trusted).
    ByteWriter w;
    w.writeU32(7); w.writeU64(1); w.writeU32(1); w.writeU16(1);
    w.writeU16(1); w.writeU32(em::kShadowObjectId); w.writeU32(2); w.writeF32(std::numeric_limits<float>::infinity());
    w.writeF32(0); w.writeF32(0); w.writeF32(0); w.writeF32(0); w.writeU8(ENEMY_MOTION_ALIVE);
    const auto inf = encodePacket(PacketType::EnemyMotion, w.data());
    check(throws([&] { EnemyMotion m; (void)decodeMotion(inf, m); }), "decoder refuses a non-finite motion time");
}

void testStream() {
    // Retain the established nine-frame timing vectors as explicit override
    // coverage; LatencyConfigTest covers the qualified six-frame default.
    em::Stream s(9);
    // Two samples are needed before a new netId is drivable (hysteresis).
    check(s.Ingest(motion(7, 1, 300, {row(1, 0.0f, 2, 10.0f, 0.0f)}), 1000), "first sample accepted");
    s.Tick(1000);  // natural cursor = 300 - DELAY(9) = 291
    check(!s.Drivable(1, 1000), "a new netId is not drivable after one sample");
    check(s.Ingest(motion(7, 2, 303, {row(1, 30.0f, 2, 13.0f, 0.3f)}), 1003), "second sample accepted");
    check(!s.Ingest(motion(7, 2, 306, {row(1, 99.0f)}), 1003) && !s.Ingest(motion(7, 1, 306, {row(1, 99.0f)}), 1003),
          "equal or older sequence is rejected");
    s.Tick(1003);  // 292
    check(s.Drivable(1, 1003), "drivable after two samples");
    em::Pose p;
    check(s.PoseAt(1, 1003, p) && p.position.x == 0.0f && p.motionTime == 10.0f && p.netId == 1,
          "cursor before the oldest sample holds the oldest pose");
    for (std::uint32_t f = 1004; f <= 1012; ++f) s.Tick(f);  // 301, inside 300..303
    check(s.PoseAt(1, 1012, p) && p.cursor == 301.0, "cursor advances one host frame per local frame");
    check(std::fabs(p.position.x - 10.0f) < 1e-3f && std::fabs(p.motionTime - 11.0f) < 1e-3f && std::fabs(p.rotationY - 0.1f) < 1e-4f,
          "position, time and yaw interpolate linearly between brackets");
    s.Tick(1013);
    s.Tick(1014);  // natural 303 > newest-1: displayed cursor held at 302, overflow 1
    check(s.PoseAt(1, 1014, p) && p.cursor == 302.0, "displayed cursor never runs past newest-1");
    check(std::fabs(p.motionTime - 13.0f) < 1e-3f, "while the cursor is held the motion time keeps running (S1)");
    for (std::uint32_t f = 1015; f <= 1024; ++f) s.Tick(f);  // 10 more frames with no sample
    check(s.PoseAt(1, 1024, p) && std::fabs(p.position.x - 20.0f) < 1e-3f && std::fabs(p.motionTime - 23.0f) < 1e-3f,
          "no sample for 10 frames: position held, motion time still advancing one per frame");
    // The late burst arrives: the natural cursor already matches it, so time continues without a rewind.
    s.Ingest(motion(7, 3, 318, {row(1, 60.0f, 2, 28.0f, 0.3f)}), 1025);
    s.Ingest(motion(7, 4, 321, {row(1, 90.0f, 2, 31.0f, 0.3f)}), 1025);
    s.Tick(1025);  // natural 314
    check(s.PoseAt(1, 1025, p) && p.cursor == 314.0 && std::fabs(p.motionTime - 24.0f) < 1e-3f,
          "after a stall the motion time continues where it ran to (no re-run)");
    // Yaw takes the shortest arc across +-pi; a huge finite yaw never hangs (S3).
    check(std::fabs(std::fabs(em::LerpAngle(3.0f, -3.0f, 0.5f)) - 3.14159265f) < 1e-3f,
          "yaw interpolation wraps through pi, not through zero");
    const float wrapped = em::WrapPi(1e30f), lerped = em::LerpAngle(0.0f, 1e30f, 0.5f);
    check(std::isfinite(wrapped) && std::fabs(wrapped) <= 3.1416f && std::isfinite(lerped) && std::fabs(lerped) <= 3.1416f &&
              std::fabs(em::WrapPi(7.0f) - (7.0f - 6.2831853f)) < 1e-5f,
          "WrapPi is closed form: 1e30 returns a bounded angle at once");
    // Stale: no sample for more than 30 local frames releases the netId.
    for (std::uint32_t f = 1026; f <= 1056; ++f) s.Tick(f);
    check(!s.Drivable(1, 1056) && s.stats().releases == 1, "a stream gap of more than 30 frames releases the netId");
    // Re-take needs two new samples.
    s.Ingest(motion(7, 5, 400, {row(1, 0.0f)}), 1057); s.Tick(1057);
    check(!s.Drivable(1, 1057), "one sample after a release is not enough");
    s.Ingest(motion(7, 6, 403, {row(1, 3.0f)}), 1058); s.Tick(1058);
    check(s.Drivable(1, 1058) && s.stats().retakes >= 2, "two samples after a release re-take the netId");
    // Large host-frame jump snaps the cursor back to newest-DELAY.
    s.Ingest(motion(7, 7, 900, {row(1, 3.0f)}), 1059); s.Tick(1059);
    check(s.PoseAt(1, 1059, p) && p.cursor == 891.0, "cursor outside newest-18 snaps to newest-9");
    // A lag a little past DELAY+3 catches up two frames per tick.
    em::Stream c(9);
    c.Ingest(motion(3, 1, 100, {row(1, 0.0f)}), 1); c.Tick(1);   // 91
    c.Ingest(motion(3, 2, 103, {row(1, 0.0f)}), 2); c.Tick(2);   // lag 12 -> 92
    c.Ingest(motion(3, 3, 109, {row(1, 0.0f)}), 3); c.Tick(3);   // lag 17 -> 94
    check(c.PoseAt(1, 3, p) && p.cursor == 94.0, "a lag past DELAY+3 catches up two frames per tick");
    // Motion change between brackets: s0's motion continues with its own time, then the new one renders.
    em::Stream m(9);
    m.Ingest(motion(3, 1, 10, {row(1, 0.0f, 2, 10.0f)}), 1); m.Tick(1);   // 1
    m.Ingest(motion(3, 2, 13, {row(1, 0.0f, 2, 13.0f)}), 2); m.Tick(2);   // 2
    m.Ingest(motion(3, 3, 16, {row(1, 0.0f, 9, 0.0f)}), 3); m.Tick(3);    // lag 14 -> 4
    for (std::uint32_t f = 4; f <= 13; ++f) m.Tick(f);                     // 14
    check(m.PoseAt(1, 13, p) && p.cursor == 14.0 && p.motionId == 2 && std::fabs(p.motionTime - 14.0f) < 1e-3f,
          "motion change: the bracket's first motion keeps advancing");
    // Live stream with the cursor only 5 behind: it holds (rev2 stall rule) until the stream goes
    // quiet for more than a publish interval, then advances to 16.
    m.Ingest(motion(3, 4, 19, {row(1, 0.0f, 9, 3.0f)}), 14);
    for (std::uint32_t f = 14; f <= 20; ++f) m.Tick(f);  // 14 held x5, then 15, 16
    check(m.PoseAt(1, 20, p) && p.cursor == 16.0 && p.motionId == 9 && std::fabs(p.motionTime) < 1e-3f,
          "after the boundary the new motion is rendered");
    // Held inside the last bracket: position between the two newest, time running.
    em::Stream h(9);
    h.Ingest(motion(3, 1, 10, {row(2, 5.0f, 2, 1.0f)}), 1); h.Tick(1);
    h.Ingest(motion(3, 2, 13, {row(2, 8.0f, 2, 4.0f)}), 2); h.Tick(2);
    for (std::uint32_t f = 3; f <= 14; ++f) h.Tick(f);  // natural 14, displayed 12
    check(h.PoseAt(2, 14, p) && p.cursor == 12.0 && std::fabs(p.position.x - 7.0f) < 1e-3f && std::fabs(p.motionTime - 5.0f) < 1e-3f,
          "cursor held at newest-1 stays inside the bracket, time advancing by the overflow");
    // Families: a non-allowlisted object id is never tracked.
    em::Stream f(9);
    auto other = row(4, 0.0f); other.objectId = 309;
    f.Ingest(motion(3, 1, 10, {other}), 1); f.Ingest(motion(3, 2, 13, {other}), 2); f.Tick(2);
    check(!f.Drivable(4, 2), "non-allowlisted family is ignored");
    em::Stream batStream(9);
    auto bat = row(8, 0.0f); bat.objectId = em::kHookBatObjectId;
    batStream.Ingest(motion(3, 1, 10, {bat}), 1); batStream.Ingest(motion(3, 2, 13, {bat}), 2); batStream.Tick(2);
    em::Pose sp;
    check(batStream.Drivable(8, 2) && batStream.PoseAt(8, 2, sp) && sp.objectId == em::kHookBatObjectId, "a Hook Bat (objectId 4) stream is tracked and drivable");
    em::Stream soldierStream(9);
    auto soldier = row(9, 0.0f); soldier.objectId = em::kSoldierObjectId;
    soldierStream.Ingest(motion(3, 1, 10, {soldier, other}), 1); soldierStream.Ingest(motion(3, 2, 13, {soldier, other}), 2); soldierStream.Tick(2);
    check(soldierStream.Drivable(9, 2) && soldierStream.PoseAt(9, 2, sp) && sp.objectId == em::kSoldierObjectId && !soldierStream.Drivable(4, 2),
          "a Soldier (objectId 301) stream is tracked and drivable beside an ignored family");
    // A new epoch resets everything.
    s.Ingest(motion(8, 1, 5, {row(1, 0.0f)}), 1060);
    check(s.epoch() == 8 && !s.Drivable(1, 1060) && s.stats().resets >= 1, "a new epoch resets the stream and its sequence floor");
    // Out-of-order sample for one netId inside a newer packet is dropped.
    em::Stream o(9);
    o.Ingest(motion(3, 1, 20, {row(1, 0.0f)}), 1);
    o.Ingest(motion(3, 2, 15, {row(1, 9.0f)}), 2);
    check(o.stats().staleSamples == 1, "a per-netId sample older than its newest is dropped");
    // S4: a restarted host reuses the epoch with low sequences and frames. Without a reset the
    // stream would refuse it; after Reset(0) (RetireWorldSession / RoomTransition) it is live again.
    em::Stream r(9);
    r.Ingest(motion(1, 50, 5000, {row(1, 0.0f)}), 1); r.Ingest(motion(1, 51, 5003, {row(1, 0.0f)}), 2); r.Tick(2);
    check(!r.Ingest(motion(1, 1, 3, {row(1, 0.0f)}), 3), "same epoch, restarted sequence: refused without a reset");
    r.Reset(0);
    check(r.Ingest(motion(1, 1, 3, {row(1, 0.0f)}), 4) && r.Ingest(motion(1, 2, 6, {row(1, 1.0f)}), 5), "after Reset(0) the restarted host is accepted");
    r.Tick(5);
    check(r.Drivable(1, 5) && r.stats().staleSamples == 0, "and its restarted host frames are not dropped as stale");
    // N6: a host death erases the track; it never counts as a stream release.
    const auto releases = r.stats().releases;
    r.Erase(1);
    for (std::uint32_t t = 6; t <= 40; ++t) r.Tick(t);
    check(!r.Drivable(1, 40) && r.stats().releases == releases, "an erased (dead) netId is gone without a release");
    // rev-2 N-c: at most 256 tracks; further new netIds are counted and ignored, known ones keep updating.
    em::Stream cap(9);
    for (std::uint64_t k = 0; k < 9; ++k) {
        std::vector<EnemyMotionEntry> rows;
        for (std::uint16_t i = 0; i < 32; ++i) rows.push_back(row(static_cast<std::uint16_t>(k * 32 + i + 1), 0.0f));
        cap.Ingest(motion(3, k + 1, static_cast<std::uint32_t>(10 + 3 * k), rows), static_cast<std::uint32_t>(k + 1));
    }
    cap.Ingest(motion(3, 10, 40, {row(1, 0.0f)}), 10);
    cap.Tick(10);
    check(cap.tracks() == em::kMaxTracks && cap.stats().trackCap == 32 && cap.Drivable(1, 10) && !cap.Drivable(257, 10),
          "the client keeps at most 256 tracks per epoch");
    check(cap.cursor() >= 0.0 && em::Stream{}.cursor() < 0.0, "cursor() is the displayed cursor, negative before any sample");
}

void testHelpers() {
    const std::uintptr_t at = 0x140419B10, brain = 0x1403B4460;
    std::uint8_t good[8] = {0x48, 0x8B, 0xCA, 0xE9, 0, 0, 0, 0};
    const std::int32_t rel = static_cast<std::int32_t>(static_cast<std::intptr_t>(brain) - static_cast<std::intptr_t>(at + 8));
    std::memcpy(good + 4, &rel, 4);
    check(em::BrainThunk(good, at, brain), "brain thunk shape: mov rcx,rdx; jmp 0x3B4460 accepted");
    std::uint8_t bad[8];
    std::memcpy(bad, good, 8); bad[2] = 0xC9;
    check(!em::BrainThunk(bad, at, brain) && !em::BrainThunk(good, at + 0x10, brain), "other bytes or another jump target refused");
    check(em::ClampMotionTime(50.0f, 40.0f) == 39.5f && em::ClampMotionTime(10.0f, 40.0f) == 10.0f &&
              em::ClampMotionTime(10.0f, 0.0f) == 10.0f && em::ClampMotionTime(-1.0f, 40.0f) == 0.0f,
          "motion time clamps just inside a finite motion end");
    check(em::BlendWeight(em::kBlendFrames) > 0.0f && em::BlendWeight(em::kBlendFrames) < em::BlendWeight(1) &&
              em::BlendWeight(1) < 1.0f && em::BlendWeight(0) == 1.0f,
          "take-over blend weight rises monotonically to the stream pose");
    check(em::FamilyAllowed(302) && em::FamilyAllowed(4) && em::FamilyAllowed(301) && em::FamilyAllowed(1838) &&
              em::FamilyAllowed(1839) && em::FamilyAllowed(1849) && !em::FamilyAllowed(309) && !em::FamilyAllowed(0) &&
              !em::FamilyAllowed(5) && !em::FamilyAllowed(84) && !em::FamilyAllowed(300) && !em::FamilyAllowed(306) &&
              !em::FamilyAllowed(1365) && !em::FamilyAllowed(1837) && !em::FamilyAllowed(1840) && !em::FamilyAllowed(1848) &&
              !em::FamilyAllowed(1850),
          "allowlist keeps Shadow (302), Hook Bat (4), Soldier (301) and its skins (1838/1839/1849): not their neighbours, the Shadow skin 1840, RAW 1365 or the player (84)");
    check(em::FamilyAllowed(17) && em::FamilyAllowed(303) && em::FamilyAllowed(368) && em::FamilyAllowed(367) &&
              !em::FamilyAllowed(16) && em::FamilyAllowed(18) && !em::FamilyAllowed(2025) && !em::FamilyAllowed(2409) &&
              em::FamilyAllowed(305) && em::FamilyAllowed(120) && !em::FamilyAllowed(119) && !em::FamilyAllowed(121) &&
              !em::FamilyAllowed(1833) && !em::FamilyAllowed(1836) && !em::FamilyAllowed(1845) && !em::FamilyAllowed(366) &&
              !em::FamilyAllowed(122) && !em::FamilyAllowed(73) && !em::FamilyAllowed(369) &&
              em::FamilyAllowed(10) && !em::FamilyAllowed(9) && !em::FamilyAllowed(11) &&
              em::FamilyAllowed(304) && em::FamilyAllowed(1843) && em::FamilyAllowed(1889) && !em::FamilyAllowed(1842) &&
              !em::FamilyAllowed(1844) && !em::FamilyAllowed(1888) && !em::FamilyAllowed(1890),
          "batches 2-4 allow 17, 303, 368, 10, 304 (+1843/1889) and the batch-4 test families 305, 18, 120, 367: not their skins (1833, 1836, 1845), neighbours, other skins (2025, 2409) or type-21 122/73");
    {
        char line[128] {};
        const auto n = em::FormatFamilies(line, sizeof(line));
        check(n == std::string("302,4,301,1838,1839,1849,17,303,368,10,304,1843,1889,305,18,120,367,318,310,312").size() &&
                  std::string(line) == "302,4,301,1838,1839,1849,17,303,368,10,304,1843,1889,305,18,120,367,318,310,312",
              "the configured line prints every allowlisted family, in order");
        char tiny[9] {};
        em::FormatFamilies(tiny, sizeof(tiny));
        check(std::string(tiny) == "302,4", "a short buffer drops whole families, never prints a cut id");
        check(em::FormatFamilies(nullptr, 8) == 0 && em::FormatFamilies(tiny, 0) == 0, "no buffer: nothing written");
    }
    {  // batch 2: each new family streams beside an ignored family (309)
        em::Stream b2(9);
        std::vector<EnemyMotionEntry> rows;
        std::uint16_t net = 40;
        for (const std::uint32_t oid : {em::kLanceSoldierObjectId, em::kLargeBodyObjectId, em::kGargoyleWarriorObjectId,
                                        em::kNightwalkerObjectId, em::kRapidThrusterObjectId, em::kArmoredKnightObjectId,
                                        em::kDrillerMoleObjectId, em::kNeoshadowObjectId, em::kGargoyleKnightObjectId}) {
            auto r = row(net++, 0.0f);
            r.objectId = oid;
            rows.push_back(r);
        }
        auto ignored = row(49, 0.0f);
        ignored.objectId = 309;
        rows.push_back(ignored);
        b2.Ingest(motion(3, 1, 10, rows), 1); b2.Ingest(motion(3, 2, 13, rows), 2); b2.Tick(2);
        em::Pose bp;
        bool all = !b2.Drivable(49, 2);
        for (std::uint16_t n = 40; n < 49; ++n) all = all && b2.Drivable(n, 2) && b2.PoseAt(n, 2, bp) && bp.objectId == rows[n - 40].objectId;
        check(all, "batch 2-4 family streams (17, 303, 368, 10, 304, 305, 18, 120, 367) are tracked and drivable; 309 is not");
    }
    {  // batch 5: exactly four new type-4 families, not adjacent natives or bosses
        check(em::FamilyAllowed(318) && !em::FamilyAllowed(317) && em::FamilyAllowed(310) && em::FamilyAllowed(312) &&
                  !em::FamilyAllowed(309) && !em::FamilyAllowed(311) && !em::FamilyAllowed(313) &&
                  !em::FamilyAllowed(314) && !em::FamilyAllowed(315) && !em::FamilyAllowed(316) &&
                  !em::FamilyAllowed(319) && !em::FamilyAllowed(1365),
              "batch5 allows Dusk/Samurai/Dancer; Creeper, Assassin/Sniper/neighbours and RAW stay native");
        em::Stream b5(9);
        std::vector<EnemyMotionEntry> rows;
        std::uint16_t net = 80;
        for (const auto oid : {em::kDuskObjectId, em::kSamuraiObjectId, em::kDancerObjectId}) {
            auto r = row(net++, 12.0f); r.objectId = oid; rows.push_back(r);
        }
        auto ignored = row(84, 0.0f); ignored.objectId = 311; rows.push_back(ignored);
        auto creeper = row(85, 0.0f); creeper.objectId = em::kCreeperObjectId; rows.push_back(creeper);
        b5.Ingest(motion(5, 1, 100, rows), 1); b5.Ingest(motion(5, 2, 103, rows), 2); b5.Tick(2);
        em::Pose pose;
        bool all = !b5.Drivable(84, 2) && !b5.Drivable(85, 2);
        for (std::uint16_t n = 80; n < 83; ++n)
            all = all && b5.Drivable(n, 2) && b5.PoseAt(n, 2, pose) && pose.objectId == rows[n - 80].objectId;
        check(all, "batch5 streams are tracked and drivable beside ignored Sniper311 and Creeper317");
    }
    {  // each Soldier skin streams like the base family
        em::Stream skins(9);
        std::vector<EnemyMotionEntry> rows;
        std::uint16_t net = 30;
        for (const auto oid : em::kSoldierSkinObjectIds) {
            auto r = row(net++, 0.0f);
            r.objectId = oid;
            rows.push_back(r);
        }
        skins.Ingest(motion(3, 1, 10, rows), 1); skins.Ingest(motion(3, 2, 13, rows), 2); skins.Tick(2);
        em::Pose sk;
        bool all = true;
        for (std::uint16_t n = 30; n < 33; ++n) all = all && skins.Drivable(n, 2) && skins.PoseAt(n, 2, sk) && sk.objectId == rows[n - 30].objectId;
        check(all, "Soldier skins 1838, 1839 and 1849 are tracked and drivable");
    }
    {  // each Rapid Thruster skin streams like the base family
        em::Stream skins(9);
        std::vector<EnemyMotionEntry> rows;
        std::uint16_t net = 34;
        for (const auto oid : em::kRapidThrusterSkinObjectIds) {
            auto r = row(net++, 0.0f);
            r.objectId = oid;
            rows.push_back(r);
        }
        skins.Ingest(motion(3, 1, 10, rows), 1); skins.Ingest(motion(3, 2, 13, rows), 2); skins.Tick(2);
        em::Pose sk;
        bool all = true;
        for (std::uint16_t n = 34; n < 36; ++n) all = all && skins.Drivable(n, 2) && skins.PoseAt(n, 2, sk) && sk.objectId == rows[n - 34].objectId;
        check(all, "Rapid Thruster skins 1843 and 1889 are tracked and drivable");
    }
    // Hook Bat lane rev2: a HOST stall (frames stop, then resume at the normal rate from where they
    // stopped) must not pin the cursor at newest-1 forever: the lag returns to about DELAY-3 and trace frames
    // (cursor % 30 == 0) come back; the motion-time overflow goes back to 0.
    {
        em::Stream st(9);
        std::uint32_t local = 1, host = 300;
        std::uint64_t seq = 1;
        auto feed = [&](int frames, bool hostRuns) {
            for (int k = 0; k < frames; ++k, ++local) {
                if (hostRuns) {
                    ++host;
                    if (host % em::kPublishInterval == 0) st.Ingest(motion(3, seq++, host, {row(1, 0.0f, 2, static_cast<float>(host))}), local);
                }
                st.Tick(local);
            }
        };
        feed(300, true);
        em::Pose pre;
        check(st.PoseAt(1, local - 1, pre) && host - pre.cursor <= em::kDelay + 3, "steady stream: cursor about DELAY behind");
        feed(25, false);  // a 25-frame host stall (under the 30-frame release)
        feed(600, true);  // the host resumes at the normal rate
        em::Pose post;
        const bool drivable = st.PoseAt(1, local - 1, post);
        check(drivable && host - post.cursor >= em::kDelay - 3 && host - post.cursor <= em::kDelay + 3,
              "after a host stall the cursor returns to about DELAY-3 behind (not pinned at newest-1)");
        check(drivable && std::fabs(post.motionTime - static_cast<float>(post.cursor)) < 1.5f,
              "after a host stall the motion time matches the cursor again (no permanent overflow)");
        bool traceFrame = false;
        for (int k = 0; k < 60 && !traceFrame; ++k) {
            feed(1, true);
            em::Pose p30;
            traceFrame = st.PoseAt(1, local - 1, p30) && std::fmod(p30.cursor, 30.0) == 0.0;
        }
        check(traceFrame, "after a host stall the cursor reaches trace frames (% 30) again");
    }
    // Live fixture-03: host Shadows chased the host's Sora 3103 u from their BB-courtyard spawns.
    check(em::kMaxFromSpawn >= 2.0f * 3103.0f && em::kMaxFromSpawn < ENEMY_MOTION_MAX_COORD,
          "the S5 spawn bound is a garbage bound, not a leash (2x the live courtyard chase, below the codec bound)");
}

void testRelay() {
    SessionConfig cfg;
    cfg.port = 17851; cfg.bindAddress = "127.0.0.1"; cfg.gameBuild = "mirror-build"; cfg.modHash = "m"; cfg.contentHash = "c";
    SessionHost relay(cfg);
    check(relay.start(), "mirror relay starts on loopback");
    if (!relay.isRunning()) return;
    std::vector<EnemyMotion> friendSeen, hostSeen, lateSeen;
    std::uint32_t friendProgress = 0, lateProgress = 0;
    const auto callbacks = [](std::vector<EnemyMotion>& seen, std::uint32_t* progress) {
        ClientCallbacks cb;
        cb.onWorldPacket = [&seen](const std::vector<std::uint8_t>& bytes) {
            EnemyMotion m;
            if (!bytes.empty() && bytes[0] == static_cast<std::uint8_t>(PacketType::EnemyMotion) && decodeMotion(bytes, m)) seen.push_back(m);
        };
        if (progress) cb.onProgressUpdate = [progress](const ProgressUpdate& m) { *progress = m.version; };
        return cb;
    };
    NetworkClient host("127.0.0.1", cfg.port, cfg.gameBuild, cfg.modHash, "mirror-host", SlotType::Player, callbacks(hostSeen, nullptr), RuntimeMode::CampaignCoop, cfg.contentHash);
    NetworkClient friendClient("127.0.0.1", cfg.port, cfg.gameBuild, cfg.modHash, "mirror-friend", SlotType::Friend1, callbacks(friendSeen, &friendProgress), RuntimeMode::CampaignCoop, cfg.contentHash);
    NetworkClient late("127.0.0.1", cfg.port, cfg.gameBuild, cfg.modHash, "mirror-late", SlotType::Friend2, callbacks(lateSeen, &lateProgress), RuntimeMode::CampaignCoop, cfg.contentHash);
    const auto pump = [&] { relay.tick(0); for (auto* c : {&host, &friendClient, &late}) { c->tick(0); c->sendHeartbeat(); } };
    const auto wait = [&](const std::function<bool()>& cond) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(4);
        while (!cond() && std::chrono::steady_clock::now() < end) { pump(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        return cond();
    };
    host.connect(); friendClient.connect();
    const bool admitted = wait([&] { return host.worldReady() && friendClient.worldReady() && relay.verifiedPeerCount() == 2; });
    check(admitted, "host and friend admitted before world sends");
    if (!admitted) return;
    host.sendRoomTransition(RoomTransition{7, 5, 6, 0, 0, 1, 0});
    EnemyManifest manifest; manifest.epoch = 7; manifest.replace = true;
    EnemyManifestEntry e; e.netId = 1; e.objectId = em::kShadowObjectId; manifest.entries = {e};
    host.sendEnemyManifest(manifest);
    std::uint32_t marker = 0;
    const auto barrier = [&] { const auto next = ++marker; host.sendProgressUpdate(ProgressUpdate{next, false, {}}); return wait([&] { return friendProgress == next; }); };
    check(barrier(), "room and manifest processed");
    worldfixture::send(host, encode(motion(7, 5, 100, {row(1, 10.0f)})), false);
    check(wait([&] { return friendSeen.size() == 1; }) && friendSeen[0].sequence == 5 && friendSeen[0].entries[0].position.x == 10.0f,
          "host EnemyMotion reaches the friend's raw world delivery");
    const auto rejectedBy = [&](const std::vector<std::uint8_t>& bytes, NetworkClient& from, const char* label) {
        const auto before = relay.rejectedWorldMessages();
        const auto seen = friendSeen.size();
        worldfixture::send(from, bytes, true);
        check(wait([&] { return relay.rejectedWorldMessages() == before + 1; }) && barrier() && friendSeen.size() == seen, label);
    };
    rejectedBy(encode(motion(7, 5, 103, {row(1, 11.0f)})), host, "relay rejects an equal sequence");
    rejectedBy(encode(motion(7, 4, 103, {row(1, 11.0f)})), host, "relay rejects an older sequence");
    rejectedBy(encode(motion(8, 9, 103, {row(1, 11.0f)})), host, "relay rejects a wrong room epoch");
    rejectedBy(encode(motion(7, 50, 103, {row(1, 11.0f)})), friendClient, "relay rejects EnemyMotion from a non-host peer");
    worldfixture::send(host, encode(motion(7, 6, 106, {row(1, 12.0f)})), false);
    check(wait([&] { return friendSeen.size() == 2 && friendSeen[1].sequence == 6; }), "the next host sequence is still forwarded");
    check(hostSeen.empty(), "the host never receives its own EnemyMotion back");
    // Late joiner: the relay replays cached world state but never EnemyMotion.
    late.connect();
    check(wait([&] { return late.worldReady(); }), "late peer admitted");
    const auto next = ++marker;
    host.sendProgressUpdate(ProgressUpdate{next, false, {}});
    check(wait([&] { return lateProgress == next; }) && lateSeen.empty(), "EnemyMotion is not cached or replayed to a late joiner");
    worldfixture::send(host, encode(motion(7, 7, 109, {row(1, 13.0f)})), false);
    check(wait([&] { return lateSeen.size() == 1 && friendSeen.size() == 3; }), "a live EnemyMotion reaches both clients after the join");
    late.disconnect(); friendClient.disconnect(); host.disconnect(); relay.stop();
}
}  // namespace

int main() {
    if (enet_initialize() != 0) return 2;
    testCodec();
    testStream();
    testHelpers();
    testRelay();
    enet_deinitialize();
    std::cout << checks - failures << "/" << checks << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
