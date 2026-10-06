// AvatarRelayTest — VUH-1492 offline gate: owner-authoritative avatars travel
// client -> relay -> other clients under simulated latency and loss, and the
// receivers' interpolated puppets track the owners' true paths.
//
// Unit checks: AvatarState / clock codec round-trips, LinkConditioner,
// AvatarInterpolator. End to end: a SessionHost plus 3 NetworkClients on
// loopback, each link conditioned to 50 ms each way (100 ms owner -> viewer)
// with 10 ms jitter and 2% loss, two clients with skewed clocks. Each client
// moves on its own circle; every other client renders it 150 ms in the past
// and compares against the analytic truth.
//
// Distance units are KH2 world units (~1 cm), so 50 units ~ 0.5 m.
// Exit code 0 = all checks passed.

#include "kh2coop/AvatarBridge.hpp"
#include "kh2coop/AvatarCapture.hpp"
#include "kh2coop/AvatarInterpolator.hpp"
#include "kh2coop/AvatarSync.hpp"
#include "kh2coop/Codec.hpp"
#include "kh2coop/LinkConditioner.hpp"
#include "kh2coop/NetworkClient.hpp"
#include "kh2coop/SessionHost.hpp"

#include <enet/enet.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <memory>
#include <map>
#include <thread>

using namespace kh2coop;

namespace {

int g_errors = 0;
int g_checks = 0;

void check(bool ok, const std::string& what) {
    std::cout << (ok ? "  PASS: " : "  FAIL: ") << what << "\n";
    ++g_checks;
    if (!ok) ++g_errors;
}

std::uint64_t steadyMs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

float dist(const Vec3& a, const Vec3& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// ---------------------------------------------------------------------------

void testCodec() {
    std::cout << "\n=== Codec ===\n";
    AvatarState a;
    a.seq = 42;
    a.serverTimeMs = 123456789012ULL;
    a.ownerSlot = SlotType::Friend2;
    a.character = 3;
    a.colorVariant = 2;
    a.worldId = 4;
    a.roomId = 0x1A;
    a.position = {1.5f, -2.0f, 300.25f};
    a.rotationY = 1.25f;
    a.velocity = {3.0f, 0.0f, -4.0f};
    a.motionId = 151;
    a.motionTime = 12.5f;
    a.motionSpeed = 1.5f;
    a.flags = AvatarAirborne | AvatarDowned;
    a.hp = 77; a.maxHp = 120; a.mp = 9; a.maxMp = 15;

    const AvatarRelay sent {0x1234567800000001ULL, a};
    const auto pkt = encode(sent);
    const std::uint8_t* payload = nullptr;
    std::size_t size = 0;
    const auto type = decodePacketHeader(pkt.data(), pkt.size(), payload, size);
    ByteReader r(payload, size);
    AvatarRelay received;
    read(r, received);
    const auto& b = received.avatar;
    check(size == 84 && pkt.size() == 87 && r.atEnd(), "v4 relay has exact 84-byte payload");
    check(received.ownerConnectionId == sent.ownerConnectionId, "relay preserves full 64-bit connection identity");
    check(type == PacketType::AvatarRelay, "avatar relay packet type");
    check(b.seq == a.seq && b.serverTimeMs == a.serverTimeMs &&
              b.ownerSlot == a.ownerSlot && b.character == 3 &&
              b.colorVariant == 2 && b.worldId == 4 && b.roomId == 0x1A,
          "avatar identity/room fields round-trip");
    check(dist(a.position, b.position) == 0.0f && b.rotationY == a.rotationY &&
              dist(a.velocity, b.velocity) == 0.0f,
          "avatar transform round-trips");
    check(b.motionId == 151 && b.motionTime == 12.5f && b.motionSpeed == 1.5f,
          "avatar motion round-trips");
    check(b.flags == a.flags && b.hp == 77 && b.maxHp == 120 && b.mp == 9 &&
              b.maxMp == 15,
          "avatar flags and stats round-trip");

    const auto pong = encode(ClockPong {111, 222});
    decodePacketHeader(pong.data(), pong.size(), payload, size);
    ByteReader r2(payload, size);
    ClockPong p;
    read(r2, p);
    check(p.clientSendMs == 111 && p.serverMs == 222, "clock pong round-trips");

    bool threw = false;
    try { encode(a, PacketType::Heartbeat); } catch (const std::exception&) { threw = true; }
    check(threw, "encode(AvatarState) rejects a non-avatar packet type");
    threw = false;
    try { encode(a, PacketType::AvatarRelay); } catch (const std::exception&) { threw = true; }
    check(threw, "untagged AvatarState cannot be encoded as AvatarRelay");
    const auto rejectPayload = [](const std::vector<std::uint8_t>& bytes) {
        try { ByteReader reader(bytes); AvatarRelay relay; read(reader, relay); }
        catch (const std::exception&) { return true; }
        return false;
    };
    ByteWriter relayWriter; write(relayWriter, sent);
    auto shortPayload = relayWriter.data(); shortPayload.pop_back();
    check(rejectPayload(shortPayload), "relay rejects a short payload");
    auto extraPayload = relayWriter.data(); extraPayload.push_back(0);
    check(rejectPayload(extraPayload), "relay rejects an extra payload byte");
    ByteWriter oldWriter; write(oldWriter, a);
    check(oldWriter.size() == 76 && rejectPayload(oldWriter.data()), "relay rejects the old untagged 76-byte payload");
    auto trailingFrame = pkt; trailingFrame.push_back(0);
    threw = false;
    try { decodePacketHeader(trailingFrame.data(), trailingFrame.size(), payload, size); }
    catch (const std::exception&) { threw = true; }
    check(threw, "relay rejects bytes outside the framed payload");
}

void testLinkConditioner() {
    std::cout << "\n=== LinkConditioner ===\n";
    LinkConditioner lc;
    lc.configure({50, 10, 0.0f, 7});
    for (std::uint8_t i = 0; i < 20; ++i) lc.enqueue(1000, {i}, false);
    check(lc.popDue(1049).empty(), "nothing delivered before latency elapses");
    auto out = lc.popDue(1060);
    bool ordered = !out.empty();
    for (std::size_t i = 0; i < out.size(); ++i) ordered &= out[i].bytes[0] == i;
    check(out.size() == 20 && ordered, "all delivered by latency+jitter, in order");

    LinkConditioner lossy;
    lossy.configure({0, 0, 0.25f, 99});
    int unreliableKept = 0, reliableKept = 0;
    for (int i = 0; i < 4000; ++i) {
        unreliableKept += lossy.enqueue(0, {1}, false) ? 1 : 0;
        reliableKept += lossy.enqueue(0, {2}, true) ? 1 : 0;
    }
    const double lossObserved = 1.0 - unreliableKept / 4000.0;
    check(std::abs(lossObserved - 0.25) < 0.03, "unreliable loss rate ~25%");
    check(reliableKept == 4000, "reliable packets are never dropped");

    LinkConditioner a, b;
    a.configure({0, 0, 0.5f, 5});
    b.configure({0, 0, 0.5f, 5});
    bool same = true;
    for (int i = 0; i < 200; ++i) same &= a.enqueue(0, {0}, false) == b.enqueue(0, {0}, false);
    check(same, "same seed -> same drop pattern");
}

AvatarState at(std::uint64_t t, float x, std::uint32_t motion, float mtime, float rot = 0.0f) {
    AvatarState s;
    s.serverTimeMs = t;
    s.position = {x, 0.0f, 0.0f};
    s.motionId = motion;
    s.motionTime = mtime;
    s.rotationY = rot;
    return s;
}

void testInterpolator() {
    std::cout << "\n=== AvatarInterpolator ===\n";
    AvatarInterpolator in(64, 100);
    check(!in.sample(5).has_value(), "empty buffer -> no sample");
    in.push(at(1000, 0.0f, 2, 0.0f, 3.0f));
    in.push(at(1100, 100.0f, 2, 10.0f, -3.0f));
    check(!in.push(at(1050, 999.0f, 2, 0.0f)), "out-of-order snapshot rejected");

    auto mid = in.sample(1050);
    check(mid && std::abs(mid->position.x - 50.0f) < 1e-3f, "midpoint position lerps");
    check(mid && std::abs(mid->motionTime - 5.0f) < 1e-3f, "same-motion time lerps");
    // 3.0 -> -3.0 rad crosses +-pi: shortest arc goes through pi, not 0.
    check(mid && std::abs(std::abs(mid->rotationY) - 3.14159f) < 0.01f,
          "rotation takes the shortest arc");

    auto ahead = in.sample(1150);
    check(ahead && std::abs(ahead->position.x - 150.0f) < 1e-3f, "extrapolates 50 ms");
    auto capped = in.sample(1600);
    check(capped && std::abs(capped->position.x - 200.0f) < 1e-3f, "extrapolation capped at 100 ms");
    auto before = in.sample(10);
    check(before && before->position.x == 0.0f, "before oldest -> oldest");

    in.push(at(1200, 200.0f, 7, 0.0f));
    auto sw = in.sample(1150);
    check(sw && sw->motionId == 2 && std::abs(sw->motionTime - 10.0f) < 1e-3f,
          "motion switch holds the earlier motion until the boundary");
}

void testAvatarSync() {
    std::cout << "\n=== AvatarSync ===\n";
    AvatarSync sync(SlotType::Friend1, {100, 1000});
    sync.setRoster(SlotType::Friend1, {11, 22, 33});
    const auto owners = sync.puppetOwners();
    check(owners[0] == 0 && owners[1] == 2, "friend1's puppets are slots 0 and 2, in order");

    auto snap = [](SlotType owner, std::uint64_t t, float x, std::uint16_t room,
                   std::uint8_t flags = 0) {
        AvatarState s;
        s.ownerSlot = owner;
        s.serverTimeMs = t;
        s.position = {x, 0.0f, 0.0f};
        s.worldId = 4;
        s.roomId = room;
        s.flags = flags;
        return AvatarRelay {owner == SlotType::Player ? 11ULL : (owner == SlotType::Friend1 ? 22ULL : 33ULL), s};
    };
    check(!sync.onRemote(snap(SlotType::Friend1, 1000, 0.0f, 26)), "own echo ignored");
    sync.onRemote(snap(SlotType::Player, 1000, 0.0f, 26));
    sync.onRemote(snap(SlotType::Player, 1100, 100.0f, 26));
    sync.onRemote(snap(SlotType::Friend2, 1000, 0.0f, 27));
    sync.onRemote(snap(SlotType::Friend2, 1100, 0.0f, 27));

    auto t = sync.sample(1150, 4, 26);
    check(t[0].active && std::abs(t[0].pose.position.x - 50.0f) < 1e-3f &&
              t[0].pose.ownerSlot == SlotType::Player,
          "puppet 0 shows the host at render time (server - 100 ms)");
    check(!t[1].active, "puppet 1 hidden: its owner is in another room");

    t = sync.sample(3000, 4, 26);
    check(!t[0].active, "puppet hidden once its stream is stale");

    sync.onRemote(snap(SlotType::Player, 3000, 0.0f, 26, AvatarInCutscene));
    sync.onRemote(snap(SlotType::Player, 3100, 0.0f, 26, AvatarInCutscene));
    t = sync.sample(3150, 4, 26);
    check(!t[0].active, "puppet hidden while its owner is in a cutscene");
}

// These are transport/ownership controls, not native actor or hook execution.
constexpr std::array<std::uint64_t, 3> kRoster {
    0x100000001ULL, 0x200000002ULL, 0x300000003ULL};
AvatarRelay relayPose(SlotType owner, std::uint64_t id, std::uint32_t seq,
                      std::uint64_t time, float x) {
    AvatarRelay r;
    r.ownerConnectionId = id;
    r.avatar.ownerSlot = owner; r.avatar.seq = seq; r.avatar.serverTimeMs = time;
    r.avatar.worldId = 4; r.avatar.roomId = 26; r.avatar.position.x = x;
    return r;
}

void testRosterReplacement() {
    std::cout << "\n=== Avatar incarnation admission ===\n";
    AvatarSync sync(SlotType::Player); // default interpolation delay is intentional
    auto old = relayPose(SlotType::Friend1, kRoster[1], 1000, 1000, 10);
    auto other = relayPose(SlotType::Friend2, kRoster[2], 1000, 1000, 30);
    check(!sync.onRemote(old), "no pose admission before a valid roster");
    sync.setRoster(SlotType::Player, kRoster);
    check(sync.onRemote(old) && sync.onRemote(other), "both current remote incarnations admitted");
    check(!sync.onRemote(old), "duplicate timestamp rejected within one incarnation");
    auto reordered = old; reordered.avatar.serverTimeMs = 999;
    check(!sync.onRemote(reordered), "older timestamp rejected within one incarnation");
    auto wrong = old; wrong.ownerConnectionId = static_cast<std::uint32_t>(kRoster[1]);
    wrong.avatar.serverTimeMs = 1100;
    check(!sync.onRemote(wrong), "low-32-bit alias cannot authenticate a full-width owner");
    wrong.ownerConnectionId = 0;
    check(!sync.onRemote(wrong), "zero connection cannot authenticate a pose");
    check(!sync.onRemote(relayPose(SlotType::Player, kRoster[0], 1, 1100, 0)), "roster does not allow own echo");
    sync.setRoster(SlotType::Player, kRoster);
    auto sample = sync.sample(1020, 4, 26);
    check(sample[0].active && sample[0].pose.position.x == 10 && sample[0].ownerConnectionId == kRoster[1],
          "unchanged roster retains pose and its admitted identity");
    auto replacement = kRoster; replacement[1] = 0x400000002ULL;
    sync.setRoster(SlotType::Player, replacement);
    sample = sync.sample(1020, 4, 26);
    check(!sample[0].active && sample[0].ownerConnectionId == 0 && sample[1].active && sample[1].ownerConnectionId == kRoster[2],
          "replacement retires only replaced peer, preserving unrelated peer");
    old.avatar.serverTimeMs = 1200; old.avatar.seq = 1001;
    check(!sync.onRemote(old), "delayed old incarnation cannot refill cleared slot");
    auto fresh = relayPose(SlotType::Friend1, replacement[1], 1, 900, 200);
    check(sync.onRemote(fresh), "fresh incarnation admits sequence 1 and lower timestamp");
    sample = sync.sample(1020, 4, 26);
    check(sample[0].active && sample[0].pose.position.x == 200 && sample[0].pose.seq == 1 && sample[0].ownerConnectionId == replacement[1],
          "default interpolation contains only fresh pose B, never old pose A");
    auto removed = replacement; removed[1] = 0;
    sync.setRoster(SlotType::Player, removed);
    check(!sync.sample(1020, 4, 26)[0].active && !sync.onRemote(fresh), "removed peer immediately hidden and future old packets rejected");
    sync.setRoster(SlotType::Player, replacement); sync.onRemote(fresh);
    sync.clear();
    check(!sync.sample(1020, 4, 26)[0].active && sync.onRemote(fresh), "room clear removes samples but preserves authenticated roster");
    auto hostChanged = replacement; hostChanged[0] += 0x100000000ULL;
    sync.setRoster(SlotType::Player, hostChanged);
    sample = sync.sample(1020, 4, 26);
    check(!sample[0].active && !sample[1].active, "host identity replacement clears all streams");
    sync.setRoster(SlotType::Friend1, replacement); sync.onRemote(other);
    auto selfChanged = replacement; selfChanged[1] += 0x100000000ULL;
    sync.setRoster(SlotType::Friend1, selfChanged);
    check(!sync.sample(1020, 4, 26)[1].active, "self identity replacement clears remote streams");
    sync.onRemote(other); sync.setRoster(SlotType::Player, selfChanged);
    check(!sync.sample(1020, 4, 26)[1].active, "local slot reassignment clears remote streams");
    sync.onRemote(other); auto invalid = selfChanged; invalid[2] = invalid[0];
    sync.setRoster(SlotType::Player, invalid);
    check(!sync.onRemote(other) && !sync.sample(1020, 4, 26)[1].active, "duplicate roster identities invalidate authority and samples");
    sync.setRoster(SlotType::Player, {});
    check(!sync.onRemote(other), "empty roster never becomes standalone authority");
}

void testPuppetProvenance() {
    std::cout << "\n=== Cached puppet provenance ===\n";
    PuppetAuthority authority {PuppetAuthorityMode::Network, 0, 17, kRoster};
    const PuppetProvenance tag {PuppetProducer::Network, 0, 17, kRoster[1], kRoster[0], kRoster[0]};
    const auto valid = [&](const PuppetProvenance& t, const PuppetAuthority& a) {
        return ValidPuppetProvenance(t, 1, 0, a);
    };
    check(valid(tag, authority), "full-width current network pose provenance accepted");
    for (const auto local : {0u, 1u, 2u}) {
        authority.localSlot = static_cast<std::uint8_t>(local);
        int index = 0;
        for (std::uint8_t owner = 0; owner < 3; ++owner) {
            if (owner == local) continue;
            auto mapped = tag; mapped.localSlot = static_cast<std::uint8_t>(local);
            mapped.localConnectionId = kRoster[local]; mapped.ownerConnectionId = kRoster[owner];
            check(ValidPuppetProvenance(mapped, owner, index, authority) &&
                  !ValidPuppetProvenance(mapped, owner, 1 - index, authority), "network provenance obeys local/remote puppet slot mapping");
            ++index;
        }
    }
    authority.localSlot = 0;
    auto changed = authority; changed.connectionIds[1] += 0x100000000ULL;
    check(!valid(tag, changed), "cached pose rejected on owner replacement with equal low 32 bits");
    changed = authority; changed.connectionIds[1] = 0;
    check(!valid(tag, changed), "cached pose rejected on roster removal");
    changed = authority; ++changed.generation;
    check(!valid(tag, changed), "cached pose rejected on generation change");
    changed = authority; changed.connectionIds[0] += 0x100000000ULL;
    check(!valid(tag, changed), "cached pose rejected on host/self replacement");
    changed = authority; changed.mode = PuppetAuthorityMode::Unavailable;
    check(!valid(tag, changed), "unavailable authority cannot retain a network pose");
    changed = authority; changed.connectionIds[2] = changed.connectionIds[1];
    check(!valid(tag, changed), "ambiguous duplicate roster rejected");
    auto bad = tag; bad.ownerConnectionId = 2;
    check(!valid(bad, authority), "truncated tag identity rejected");
    bad = tag; bad.generation = 0;
    check(!valid(bad, authority), "zero generation rejected");
    check(!ValidPuppetProvenance(tag, 1, -1, authority) && !ValidPuppetProvenance(tag, 1, 2, authority), "invalid puppet indices rejected");
    PuppetAuthority off; off.mode = PuppetAuthorityMode::Off;
    PuppetProvenance standalone; standalone.producer = PuppetProducer::Standalone;
    check(valid(standalone, off), "explicit standalone accepted under positively known Off");
    check(!valid(standalone, PuppetAuthority {}) && !valid(standalone, authority), "standalone rejected for unknown and active network authority");
    check(!valid(tag, off), "network cached pose never converts to standalone after disconnect");
    auto dirtyOff = off; dirtyOff.connectionIds[1] = kRoster[1];
    check(!valid(standalone, dirtyOff), "Off with residual connection identity rejected");
    dirtyOff = off; dirtyOff.generation = 17;
    check(!valid(standalone, dirtyOff), "previously armed generation cannot authorize standalone after Off");
    dirtyOff = off; dirtyOff.localSlot = 0;
    check(!valid(standalone, dirtyOff), "Off with assigned local slot rejected");
    auto dirtyStandalone = standalone; dirtyStandalone.ownerConnectionId = kRoster[1];
    check(!valid(dirtyStandalone, off), "standalone producer cannot carry a network owner");
    check(!valid(PuppetProvenance {}, off), "untagged legacy pose never accepted as standalone");
}

void testAvatarBridge() {
    std::cout << "\n=== AvatarBridge (shared memory) ===\n";
    const DWORD fakePid = 0x7FFF0000u + (GetCurrentProcessId() & 0xFFFFu);
    AvatarBridge dll, runtime;
    check(dll.Open(fakePid) && runtime.Open(fakePid), "both sides open the same mapping");

    AvatarState out;
    check(!runtime.TryReadLocal(out), "no local avatar before the DLL publishes");
    AvatarState local;
    local.seq = 7;
    local.position = {1.0f, 2.0f, 3.0f};
    local.motionId = 2;
    dll.PublishLocal(local);
    check(runtime.TryReadLocal(out) && out.seq == 7 && out.position.z == 3.0f &&
              out.motionId == 2,
          "runtime reads the DLL's local avatar");
    check(!runtime.TryReadLocal(out), "unchanged slot reads as no new data");

    PuppetPose pose;
    pose.active = 1;
    pose.provenance = {PuppetProducer::Network, 0, 17, kRoster[2], kRoster[0], kRoster[0]};
    pose.pose.ownerSlot = SlotType::Friend2;
    pose.pose.position = {9.0f, 0.0f, 0.0f};
    runtime.PublishPuppet(1, pose);
    PuppetPose got;
    check(!dll.TryReadPuppet(0, got), "puppet 0 untouched");
    check(dll.TryReadPuppet(1, got) && got.active == 1 &&
              got.pose.ownerSlot == SlotType::Friend2 && got.pose.position.x == 9.0f,
          "DLL reads puppet 1's pose");
    PuppetAuthority authority {PuppetAuthorityMode::Network, 0, 17, kRoster};
    check(AVATAR_BRIDGE_VERSION == 3 && ValidPuppetProvenance(got.provenance, 2, 1, authority),
          "bridge v3 preserves full network provenance");
    check(!dll.TryReadPuppet(1, got), "cached puppet has no new shared-memory sample");
    authority.connectionIds[2] += 0x100000000ULL;
    check(!ValidPuppetProvenance(got.provenance, 2, 1, authority) && got.pose.position.x == 9.0f,
          "unchanged cached bytes lose authority after roster replacement without a new read");
}

// Byte-addressed fake memory for AvatarCapture.
struct FakeMemory {
    std::map<std::uint64_t, std::uint8_t> bytes;
    template <class T>
    void put(std::uint64_t addr, T v) {
        std::uint8_t raw[sizeof(T)];
        std::memcpy(raw, &v, sizeof(T));
        for (std::size_t i = 0; i < sizeof(T); ++i) bytes[addr + i] = raw[i];
    }
    template <class T>
    T read(std::uint64_t addr) const {
        std::uint8_t raw[sizeof(T)] = {};
        for (std::size_t i = 0; i < sizeof(T); ++i) {
            auto it = bytes.find(addr + i);
            if (it != bytes.end()) raw[i] = it->second;
        }
        T v;
        std::memcpy(&v, raw, sizeof(T));
        return v;
    }
};

void testAvatarCapture() {
    std::cout << "\n=== AvatarCapture ===\n";
    namespace o = offsets;
    const std::uint64_t exe = 0x140000000ULL;
    const std::uint64_t actor = 0x7FF600001000ULL;
    FakeMemory m;
    m.put<std::uint8_t>(exe + o::WORLD_ID, 4);
    m.put<std::uint8_t>(exe + o::ROOM_ID, 0x1A);
    m.put<std::int32_t>(exe + o::slot0::HP, 87);
    m.put<std::int32_t>(exe + o::slot0::MAX_HP, 120);
    const std::uint64_t entity = actor + o::actor::ENTITY_TRANSFORM;
    m.put<float>(entity + o::entity::POS_X, 10.0f);
    m.put<float>(entity + o::entity::POS_X + 4, -20.0f);
    m.put<float>(entity + o::entity::POS_X + 8, 30.0f);
    m.put<float>(entity + o::entity::ROT_Y, 1.5f);
    m.put<std::uint32_t>(entity + o::entity::AIRBORNE_FLAG, 1); // sticky; must not matter
    m.put<std::uint32_t>(entity + o::entity::AIRBORNE_SUB, 1);
    m.put<float>(actor + capture::ACTOR_VELOCITY, 3.0f);
    m.put<float>(actor + capture::ACTOR_VELOCITY + 8, -4.0f);
    m.put<std::uint32_t>(actor + o::actor::ANIM_ID, 151);
    m.put<float>(actor + capture::ACTOR_MOTION_TIME, 12.5f);

    const auto a = captureAvatar(m, exe, actor, false, false);
    check(a.worldId == 4 && a.roomId == 0x1A && a.hp == 87 && a.maxHp == 120,
          "room and HP come from NOW and slot 0");
    check(a.position.x == 10.0f && a.position.y == -20.0f && a.position.z == 30.0f &&
              a.rotationY == 1.5f && a.velocity.x == 3.0f && a.velocity.z == -4.0f,
          "transform from entity+0x30/+0x4C, velocity from actor+0xB98");
    check(a.motionId == 151 && a.motionTime == 12.5f && a.motionSpeed == 1.0f,
          "motion id from actor+0x180, time from actor+0x19C");
    check((a.flags & AvatarAirborne) && !(a.flags & AvatarInCutscene), "airborne flag set");

    const auto noActor = captureAvatar(m, exe, 0, false, false);
    check((noActor.flags & AvatarInCutscene) && noActor.roomId == 0x1A,
          "no actor (loading) -> flagged hidden, room still reported");
    const auto event = captureAvatar(m, exe, actor, true, true);
    check((event.flags & AvatarInCutscene) && (event.flags & AvatarDowned),
          "caller's event/downed state becomes flags");
}

// ---------------------------------------------------------------------------

constexpr std::uint16_t kPort = 17795;
constexpr float kRadius = 300.0f;    // units
constexpr float kOmega = 2.0f;       // rad/s -> ~600 units/s, a run
constexpr std::uint64_t kRenderDelayMs = 150;

Vec3 truth(int owner, std::uint64_t serverMs) {
    const float t = static_cast<float>(serverMs % 1000000) / 1000.0f;
    const float ang = kOmega * t + owner * 2.0f;
    return {owner * 1000.0f + kRadius * std::cos(ang), 0.0f, kRadius * std::sin(ang)};
}

void testEndToEnd() {
    std::cout << "\n=== End to end: 3 clients, 100 ms owner->viewer, 2% loss ===\n";
    SessionConfig cfg;
    cfg.bindAddress = "127.0.0.1";
    cfg.port = kPort;
    cfg.maxPeers = 3;
    cfg.gameBuild = "avatar-build";
    cfg.contentHash = "avatar-content";
    cfg.modHash = "avatar-mod";
    cfg.sessionId = "avatar-test";
    SessionHost host(cfg, {});
    check(host.start(), "relay starts");

    struct Viewer {
        std::array<AvatarInterpolator, 3> interp;
        std::array<int, 3> received {};
        std::array<std::uint32_t, 3> lastSeq {};
        std::array<std::uint64_t, 3> lastTime {};
        int ownEcho {0};
        int wrongRoom {0};
        int wrongConnection {0};
        std::array<std::uint64_t, 3> roster {};
        double errSum {0.0};
        std::uint64_t errCount {0};
        float errMax {0.0f};
    };
    std::array<Viewer, 3> viewers;
    std::array<std::unique_ptr<NetworkClient>, 3> clients;
    const std::array<std::int64_t, 3> skew {0, 5000, -3000};

    for (int i = 0; i < 3; ++i) {
        ClientCallbacks cb;
        cb.onSessionState = [&viewers, i](const SessionState& state) {
            viewers[i].roster = {};
            for (const auto& actor : state.actors) {
                const auto slot = static_cast<unsigned>(actor.slot);
                if (slot < 3) viewers[i].roster[slot] = actor.connectionId;
            }
        };
        cb.onAvatarState = [&viewers, i](const AvatarRelay& relay) {
            const auto& s = relay.avatar;
            auto& v = viewers[i];
            const auto owner = static_cast<int>(s.ownerSlot);
            if (owner == i) { ++v.ownEcho; return; }
            if (owner < 0 || owner > 2) return;
            if (relay.ownerConnectionId == 0 || relay.ownerConnectionId != v.roster[owner]) ++v.wrongConnection;
            if (s.worldId != 4 || s.roomId != 0x1A) ++v.wrongRoom;
            ++v.received[owner];
            v.lastSeq[owner] = s.seq; v.lastTime[owner] = s.serverTimeMs;
            v.interp[owner].push(s);
        };
        clients[i] = std::make_unique<NetworkClient>(
            "127.0.0.1", kPort, cfg.gameBuild, cfg.modHash,
            "avatar_" + std::to_string(i), static_cast<SlotType>(i), std::move(cb),
            RuntimeMode::CampaignCoop, cfg.contentHash);
        const LinkConditions link {50, 10, 0.02f, static_cast<std::uint32_t>(11 + i)};
        clients[i]->setLinkConditions(link, LinkConditions {50, 10, 0.02f,
                                      static_cast<std::uint32_t>(21 + i)});
        clients[i]->setClockSkewMs(skew[i]);
        clients[i]->connect();
    }

    const auto pump = [&](int ms) {
        const auto end = steadyMs() + ms;
        while (steadyMs() < end) {
            host.tick(0);
            for (auto& c : clients) c->tick(0);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };

    // Connect, verify, and let clock sync settle.
    const auto deadline = steadyMs() + 5000;
    while (steadyMs() < deadline && host.verifiedPeerCount() < 3) pump(10);
    check(host.verifiedPeerCount() == 3, "3 clients verified");
    pump(1500);
    {
        const auto link = clients[1]->linkStats();
        // 50 ms each way per client + jitter: app RTT sees the conditioner.
        check(link.valid && link.appRttMs >= 90 && link.appRttMs < 400,
              "linkStats: app RTT includes simulated latency (" +
                  std::to_string(link.appRttMs) + " ms)");
        check(link.lossPermille <= 1000, "linkStats: loss is a per-mille value");
    }

    for (int i = 0; i < 3; ++i) {
        const auto err = static_cast<long long>(clients[i]->estimatedServerTimeMs()) -
                         static_cast<long long>(steadyMs());
        char buf[160];
        std::snprintf(buf, sizeof(buf),
                      "client %d clock estimate within 30 ms of host (skew %lld ms, err %lld ms, rtt %u ms)",
                      i, static_cast<long long>(skew[i]), err, clients[i]->roundTripMs());
        check(clients[i]->hasClockSync() && std::llabs(err) <= 30, buf);
    }

    // Stream for 6 s at ~60 Hz; measure after a 1 s warm-up.
    const auto start = steadyMs();
    std::uint64_t nextSend = start;
    while (steadyMs() < start + 6000) {
        const auto now = steadyMs();
        if (now >= nextSend) {
            for (int i = 0; i < 3; ++i) {
                AvatarState s;
                s.serverTimeMs = clients[i]->estimatedServerTimeMs();
                s.position = truth(i, s.serverTimeMs);
                s.worldId = 4;
                s.roomId = 0x1A;
                s.motionId = 2; // RUN
                // Lie about ownership: the relay must overwrite it.
                s.ownerSlot = SlotType::Player;
                clients[i]->sendAvatar(s);
            }
            nextSend += 16;
        }
        host.tick(0);
        for (auto& c : clients) c->tick(0);

        if (now > start + 1000) {
            for (int v = 0; v < 3; ++v) {
                const auto render = clients[v]->estimatedServerTimeMs() - kRenderDelayMs;
                for (int o = 0; o < 3; ++o) {
                    if (o == v) continue;
                    const auto s = viewers[v].interp[o].sample(render);
                    if (!s) continue;
                    const float e = dist(s->position, truth(o, render));
                    viewers[v].errSum += e;
                    viewers[v].errCount += 1;
                    if (e > viewers[v].errMax) viewers[v].errMax = e;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    for (int v = 0; v < 3; ++v) {
        const auto& vw = viewers[v];
        const double mean = vw.errCount ? vw.errSum / static_cast<double>(vw.errCount) : 1e9;
        char buf[200];
        std::snprintf(buf, sizeof(buf),
                      "viewer %d: received %d/%d/%d, mean err %.1f, max err %.1f units",
                      v, vw.received[0], vw.received[1], vw.received[2], mean, vw.errMax);
        check(vw.ownEcho == 0, "viewer " + std::to_string(v) + " never receives its own avatar");
        check(vw.wrongConnection == 0, "viewer " + std::to_string(v) + " gets relay-authenticated roster connection IDs");
        check(vw.wrongRoom == 0, "viewer " + std::to_string(v) + " sees the owners' room ids");
        bool gotOthers = true;
        for (int o = 0; o < 3; ++o) {
            if (o != v) gotOthers &= vw.received[o] > 250; // ~360 sent, 2-4% lost
        }
        check(gotOthers, std::string(buf));
        check(mean < 15.0 && vw.errMax < 50.0f,
              "viewer " + std::to_string(v) + " puppets within 50 units (~0.5 m) of truth");
    }
    check(host.relayedAvatarCount() > 900, "relay forwarded the streams");
    {
        // 2% conditioner loss on each hop (owner out, viewer in) ~= 4% end to
        // end; ENet's own estimate can't see these drops.
        const auto link = clients[2]->linkStats();
        check(link.avatarLossPermille != NetworkClient::kNoAvatarLoss &&
                  link.avatarLossPermille >= 10 && link.avatarLossPermille <= 100,
              "avatar-stream loss sees simulated drops (" +
                  std::to_string(link.avatarLossPermille) + " per mille)");
    }

    const auto previousId = viewers[0].roster[1];
    clients[1]->disconnect();
    auto reconnectDeadline = steadyMs() + 3000;
    while (steadyMs() < reconnectDeadline && (host.verifiedPeerCount() != 2 || viewers[0].roster[1] != 0)) pump(10);
    check(host.verifiedPeerCount() == 2 && viewers[0].roster[1] == 0,
          "actual relay retires friend membership while host and other friend remain");
    clients[1]->connect(); reconnectDeadline = steadyMs() + 4000;
    while (steadyMs() < reconnectDeadline && (host.verifiedPeerCount() != 3 || viewers[0].roster[1] == 0 || viewers[2].roster[1] == 0)) pump(10);
    check(previousId != 0 && viewers[0].roster[1] != 0 && viewers[0].roster[1] != previousId &&
          viewers[2].roster[1] == viewers[0].roster[1], "actual relay assigns and broadcasts a fresh connection on slot reuse");
    const auto count0 = viewers[0].received[1], count2 = viewers[2].received[1];
    AvatarState fresh; fresh.seq = 1; fresh.serverTimeMs = 1; fresh.worldId = 4; fresh.roomId = 0x1A;
    // Temporarily remove configured loss for this discrete reconnect assertion.
    for (auto& c : clients) c->setLinkConditions({}, {});
    clients[1]->sendRawPacket(encode(fresh, PacketType::AvatarState), true);
    reconnectDeadline = steadyMs() + 2000;
    while (steadyMs() < reconnectDeadline && (viewers[0].received[1] == count0 || viewers[2].received[1] == count2)) pump(10);
    check(viewers[0].received[1] == count0 + 1 && viewers[2].received[1] == count2 + 1 &&
          viewers[0].lastSeq[1] == 1 && viewers[2].lastSeq[1] == 1 &&
          viewers[0].lastTime[1] == 1 && viewers[2].lastTime[1] == 1,
          "actual relay and receivers accept fresh sequence/time 1 after higher previous incarnation");
    for (auto& c : clients) c->disconnect();
    host.stop();
}

void testReceiverAndReconnect() {
    std::cout << "\n=== Actual NetworkClient admission and conditioned reconnect ===\n";
    // Synthetic relay packets exercise the real receiver/conditioners. This does
    // not emulate native puppets or claim to test the production relay handshake.
    ENetAddress address {};
    enet_address_set_host(&address, "127.0.0.1"); address.port = kPort + 2;
    ENetHost* server = enet_host_create(&address, 4, 2, 0, 0);
    check(server != nullptr, "receiver fixture binds loopback");
    if (!server) return;
    ENetPeer* peer = nullptr;
    std::vector<AvatarRelay> received;
    std::vector<AvatarState> outbound;
    AvatarSync cached(SlotType::Friend2, {0, 1000});
    int emptyRosters = 0;
    ClientCallbacks callbacks;
    callbacks.onSessionState = [&](const SessionState& state) {
        std::array<std::uint64_t, 3> ids {};
        if (state.actors.empty()) ++emptyRosters;
        for (const auto& actor : state.actors) {
            const auto slot = static_cast<unsigned>(actor.slot);
            if (slot < 3) ids[slot] = actor.connectionId;
        }
        cached.setRoster(SlotType::Friend2, ids);
    };
    callbacks.onAvatarState = [&](const AvatarRelay& r) { received.push_back(r); cached.onRemote(r); };
    NetworkClient client("127.0.0.1", address.port, "receiver-build", "m", "receiver",
                         SlotType::Friend2, std::move(callbacks), RuntimeMode::CampaignCoop, "c");
    const auto pump = [&](int ms) {
        const auto end = steadyMs() + static_cast<std::uint64_t>(ms);
        do {
            ENetEvent event {};
            while (enet_host_service(server, &event, 0) > 0) {
                if (event.type == ENET_EVENT_TYPE_CONNECT) peer = event.peer;
                if (event.type == ENET_EVENT_TYPE_DISCONNECT && peer == event.peer) peer = nullptr;
                if (event.type == ENET_EVENT_TYPE_RECEIVE) {
                    try {
                        const std::uint8_t* payload = nullptr; std::size_t n = 0;
                        if (decodePacketHeader(event.packet->data, event.packet->dataLength, payload, n) == PacketType::AvatarState) {
                            ByteReader reader(payload, n); AvatarState a; read(reader, a); outbound.push_back(a);
                        }
                    } catch (const std::exception&) { }
                    enet_packet_destroy(event.packet);
                }
            }
            client.tick(0);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (steadyMs() < end);
    };
    const auto connected = [&] {
        const auto end = steadyMs() + 3000;
        while (steadyMs() < end && (!peer || !client.isConnected())) pump(5);
        return peer && client.isConnected();
    };
    const auto send = [&](const std::vector<std::uint8_t>& bytes) {
        if (!peer) return;
        ENetPacket* packet = enet_packet_create(bytes.data(), bytes.size(), ENET_PACKET_FLAG_RELIABLE);
        if (enet_peer_send(peer, 0, packet) < 0) enet_packet_destroy(packet);
        enet_host_flush(server);
    };
    SessionState roster; roster.sessionId = "receiver-session"; roster.gameBuild = "receiver-build"; roster.modHash = "m";
    for (unsigned i = 0; i < 3; ++i) {
        SessionActor a; a.slot = static_cast<SlotType>(i); a.connectionId = kRoster[i];
        a.ownerPeerId = i == 2 ? "receiver" : "remote_" + std::to_string(i);
        roster.actors.push_back(a);
    }
    const auto awaitCount = [&](std::size_t count) {
        const auto end = steadyMs() + 1500;
        while (steadyMs() < end && received.size() < count) pump(5);
    };
    check(client.connect() && connected(), "actual NetworkClient connects to bounded synthetic relay");
    auto a = relayPose(SlotType::Friend1, kRoster[1], 1000, 1000, 10);
    send(encode(a)); pump(30);
    check(received.empty(), "NetworkClient rejects relay before roster");
    send(encode(roster)); send(encode(a)); awaitCount(1);
    check(received.size() == 1 && received.back().ownerConnectionId == kRoster[1], "NetworkClient admits full-width current roster owner");
    send(encode(a)); // duplicate sequence, then reordering despite a newer timestamp
    auto bad = a; bad.avatar.seq = 999; bad.avatar.serverTimeMs = 1200; send(encode(bad));
    bad = a; bad.avatar.seq = 0; send(encode(bad));
    bad = a; bad.avatar.seq = 1001; bad.ownerConnectionId = 2; send(encode(bad));
    bad.ownerConnectionId = 0; send(encode(bad));
    bad = relayPose(SlotType::Friend2, kRoster[2], 1001, 1200, 0); send(encode(bad));
    // All malformed/wrong-type packets precede a valid reliable barrier.
    ByteWriter old; write(old, a.avatar); send(encodePacket(PacketType::AvatarRelay, old.data()));
    auto shortFrame = encode(a); shortFrame.pop_back(); send(shortFrame);
    auto extraFrame = encode(a); extraFrame.push_back(0); send(extraFrame);
    send(encode(a.avatar, PacketType::AvatarState));
    a.avatar.seq = 1001; a.avatar.serverTimeMs = 1300; send(encode(a)); awaitCount(2);
    check(received.size() == 2 && received.back().avatar.seq == 1001,
          "duplicate/reordered/zero/wrong-owner/own-slot/old/short/extra/wrong-type rejected before valid barrier");
    const auto replacementId = 0x400000002ULL;
    roster.actors[1].connectionId = replacementId;
    send(encode(roster)); a.avatar.seq = 1002; send(encode(a));
    auto b = relayPose(SlotType::Friend1, replacementId, 1, 900, 200); send(encode(b)); awaitCount(3);
    check(received.size() == 3 && received.back().ownerConnectionId == replacementId && received.back().avatar.seq == 1,
          "real receiver rejects delayed A and admits B sequence 1 with lower timestamp after replacement");
    roster.actors.erase(roster.actors.begin() + 1); send(encode(roster));
    b.avatar.seq = 2; send(encode(b));
    auto hostPose = relayPose(SlotType::Player, kRoster[0], 1, 900, 40); send(encode(hostPose)); awaitCount(4);
    check(received.size() == 4 && received.back().avatar.ownerSlot == SlotType::Player,
          "removed member rejected while unchanged host continues");

    // Invalid roster delivery must retire the callback consumer, not merely
    // suppress subsequent packets in NetworkClient's private admission table.
    const auto rejectRoster = [&](const std::vector<std::uint8_t>& bytes, const char* description) {
        const auto count = received.size(); const auto emptyBefore = emptyRosters;
        send(bytes);
        const auto end = steadyMs() + 1000;
        while (steadyMs() < end && emptyRosters == emptyBefore) pump(5);
        hostPose.avatar.seq += 1; send(encode(hostPose)); pump(30);
        const auto targets = cached.sample(1000, 4, 26);
        check(emptyRosters == emptyBefore + 1 && !targets[0].active && !targets[1].active && received.size() == count,
              description);
        send(encode(roster)); send(encode(hostPose)); awaitCount(count + 1);
        check(received.size() == count + 1 && cached.sample(1000, 4, 26)[0].active,
              "valid roster recovery admits the otherwise unchanged packet after retirement");
    };
    auto invalidRoster = roster;
    invalidRoster.actors.back().connectionId = invalidRoster.actors.front().connectionId;
    rejectRoster(encode(invalidRoster), "duplicate connection roster emits empty callback, retires cached pose and rejects old relay");
    invalidRoster = roster; invalidRoster.actors.pop_back();
    rejectRoster(encode(invalidRoster), "missing self roster emits empty callback and immediately retires pose");
    invalidRoster = roster; invalidRoster.actors.erase(invalidRoster.actors.begin());
    rejectRoster(encode(invalidRoster), "missing host roster emits empty callback and immediately retires pose");
    auto truncatedRoster = encode(roster); truncatedRoster.pop_back();
    rejectRoster(truncatedRoster, "truncated SessionState frame emits empty callback and immediately retires pose");

    // Enqueue outbound data, then disconnect before its 400 ms deadline.
    const auto beforeQueue = received.size();
    client.setLinkConditions({400, 0, 0.0f, 1}, {400, 0, 0.0f, 2});
    AvatarState queued; queued.position.x = 900; client.sendAvatar(queued);
    hostPose.avatar.seq += 1; hostPose.avatar.position.x = 901; send(encode(hostPose));
    pump(80); // allow incoming ENet delivery into the delayed receive conditioner
    const auto beforeDisconnect = received.size();
    check(beforeDisconnect == beforeQueue && outbound.empty(), "conditioned inbound/outbound markers remain delayed before disconnect");
    client.disconnect(); pump(30);
    check(client.connect() && connected(), "same NetworkClient reconnects with existing condition settings");
    roster.actors.back().connectionId += 0x100000000ULL;
    send(encode(roster));
    hostPose.avatar.seq = 1; hostPose.avatar.serverTimeMs = 800; hostPose.avatar.position.x = 42;
    send(encode(hostPose));
    queued.position.x = 903; client.sendAvatar(queued); // deliberately before delayed roster admission
    const auto admittedDeadline = steadyMs() + 2000;
    while (steadyMs() < admittedDeadline && !client.ready()) pump(5);
    check(client.ready(), "reconnected transport waits for actual roster admission before fresh send");
    queued.position.x = 902; client.sendAvatar(queued);
    pump(650);
    bool oldInbound = false, oldOutbound = false, freshOutbound = false, unreadyOutbound = false;
    for (const auto& r : received) oldInbound |= r.avatar.position.x == 901;
    for (const auto& r : outbound) {
        oldOutbound |= r.position.x == 900;
        unreadyOutbound |= r.position.x == 903;
        freshOutbound |= r.position.x == 902 && r.seq == 1;
    }
    check(!oldInbound && received.size() == beforeDisconnect + 1 && received.back().avatar.position.x == 42,
          "reconnect discards delayed old inbound scope and admits new low sequence");
    check(!unreadyOutbound, "pre-ready avatar send is dropped instead of queued for later admission");
    check(!oldOutbound && freshOutbound, "reconnect discards delayed old outbound packet and resets send sequence");
    client.disconnect(); enet_host_destroy(server);
}

void testVersionReject() {
    std::cout << "\n=== Version mismatch: client hears why it was refused ===\n";
    SessionConfig cfg;
    cfg.port = kPort + 1;
    cfg.maxPeers = 3;
    cfg.gameBuild = "host-build";
    cfg.contentHash = "c";
    cfg.modHash = "m";
    cfg.sessionId = "reject-test";
    {
        SessionConfig bad = cfg;
        bad.bindAddress = "not-an-ip";
        SessionHost badHost(bad, {});
        check(!badHost.start(), "relay refuses an invalid --bind address");
    }
    cfg.bindAddress = "127.0.0.1"; // --bind: clients on that address still connect
    SessionHost host(cfg, {});
    check(host.start(), "relay starts (bound to 127.0.0.1)");

    std::string reason;
    bool disconnected = false;
    ClientCallbacks cb;
    cb.onRejected = [&reason](const HelloReject& r) { reason = r.reason; };
    cb.onDisconnected = [&disconnected]() { disconnected = true; };
    NetworkClient client("127.0.0.1", cfg.port, "other-build", cfg.modHash, "stale",
                         SlotType::Friend1, std::move(cb), RuntimeMode::CampaignCoop,
                         cfg.contentHash);
    client.connect();
    const auto deadline = steadyMs() + 3000;
    while (steadyMs() < deadline && !disconnected) {
        host.tick(0);
        client.tick(0);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(disconnected, "mismatched client is disconnected");
    check(reason.find("Version mismatch") != std::string::npos &&
              reason.find("other-build") != std::string::npos &&
              reason.find("relay expects build=host-build") != std::string::npos,
          "client received the reject reason: " + reason);
    check(host.verifiedPeerCount() == 0, "mismatched client never verified");
    client.disconnect();
    reason.clear(); disconnected = false;
    ClientCallbacks legacyCallbacks;
    legacyCallbacks.onRejected = [&](const HelloReject& r) { reason = r.reason; };
    legacyCallbacks.onDisconnected = [&] { disconnected = true; };
    NetworkClient legacy("127.0.0.1", cfg.port, cfg.gameBuild, cfg.modHash, "legacy-v3",
                         SlotType::Friend1, std::move(legacyCallbacks), RuntimeMode::CampaignCoop, cfg.contentHash, 3);
    legacy.connect();
    const auto legacyDeadline = steadyMs() + 3000;
    while (steadyMs() < legacyDeadline && !disconnected) {
        host.tick(0); legacy.tick(0); std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(PROTOCOL_VERSION == 10 && disconnected && reason == "Protocol mismatch: client=3 server=" + std::to_string(PROTOCOL_VERSION) && host.verifiedPeerCount() == 0,
          "otherwise matching legacy v3 peer is rejected for exact protocol mismatch with free capacity");
}

} // namespace

int main() {
    if (enet_initialize() != 0) {
        std::cerr << "enet_initialize failed\n";
        return 2;
    }
    testCodec();
    testLinkConditioner();
    testInterpolator();
    testAvatarSync();
    testRosterReplacement();
    testPuppetProvenance();
    testAvatarBridge();
    testAvatarCapture();
    testEndToEnd();
    testReceiverAndReconnect();
    testVersionReject();
    enet_deinitialize();

    std::cout << "\n=======================================\n"
              << (g_errors == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED: " + std::to_string(g_errors))
              << " (" << g_checks << " checks)\n";
    return g_errors == 0 ? 0 : 1;
}
