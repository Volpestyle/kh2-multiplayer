#include "kh2coop/Codec.hpp"
#include "kh2coop/ProgressAllowList.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace kh2coop;
namespace {
int checks{}, failures{};
void check(bool ok, const char* label) {
    ++checks; if (!ok) ++failures;
    std::cout << (ok ? "PASS: " : "FAIL: ") << label << '\n';
}
template<class F> bool rejects(F&& action) {
    try { action(); } catch (const std::exception&) { return true; }
    return false;
}
// Synthetic codec evidence only: these values do not establish native history.
ResyncSnapshot fixture() {
    ResyncSnapshot s;
    s.room = {1, 5, 6, 0, 1, 1, 0}; s.hold = {1, false, 0};
    s.progress.version = 1; s.progress.full = true;
    for (const auto& r : verifiedProgressAllowList())
        s.progress.spans.push_back({r.offset, std::vector<std::uint8_t>(r.length, 0)});
    s.hpSequence = 12; s.coverageMask = ResyncNativeComplete; s.livingCount = 5;
    s.generation = 2; s.transitionSerial = 2; s.loadSerial = 3;
    s.captureFrameBefore = 100; s.captureFrameAfter = 100;
    NativeRecordContentDefinition d;
    d.layoutSha256[0] = 1; d.location = {5,6,0,1,1,0}; d.groupKey = 808476514;
    d.header[0] = 2; d.header[2] = 30; d.header[4] = 5; d.header[0xE] = 1;
    constexpr std::array<std::uint8_t, 5> ids{11, 12, 13, 14, 18};
    for (std::uint16_t i = 0; i < 5; ++i) {
        std::array<std::uint8_t, 64> record{};
        record[0] = 0x2e; record[1] = 1; record[0x1c] = 2;
        record[0x1e] = ids[i]; record[0x2a] = 8; d.records.push_back(record);
        ResyncEnemyState e; e.identity.netId = static_cast<std::uint16_t>(i + 1);
        e.identity.spawnIndex = i; e.identity.battleProgram = 1; e.identity.objectId = 302;
        e.objectType = 4; e.hp = 17; e.maxHp = 20; e.record = {0, i}; s.enemies.push_back(e);
    }
    s.recordDefinitions.push_back(d);
    ResyncActivationReplay a;
    a.definitionIndex = 0; a.point = {10, -20, 30, 1};
    a.before.flags = 2; a.before.currentCount = 5; a.before.initialCount = 5;
    a.before.nativeType = 2; a.before.headerId = 30; a.before.recordCount = 5;
    a.before.cacheRoom = 6; a.before.cacheAge = 77;
    a.current = a.before; a.current.flags = 10; a.current.currentCount = 0; a.current.activation = 1;
    a.current.cacheAge = 80;
    std::copy(ids.begin(), ids.end(), a.current.cacheIds.begin());
    a.firstUpdateSequence = 15; a.hostTransition = 2; a.hostLoad = 3;
    s.activationReplay = a; s.nativeFingerprint = resyncNativeFingerprint(s);
    return s;
}
// Frozen pre-trailer layout, independently written so absence is byte-compatible.
std::vector<std::uint8_t> legacyBytes(const ResyncSnapshot& s) {
    ByteWriter w;
    write(w, s.room); write(w, s.hold); write(w, s.progress); w.writeU64(s.hpSequence);
    w.writeU16(static_cast<std::uint16_t>(s.enemies.size()));
    for (const auto& e : s.enemies) {
        w.writeU16(e.identity.netId); w.writeU16(e.identity.battleProgram); w.writeU16(e.identity.spawnIndex);
        w.writeU32(e.identity.objectId); write(w, e.identity.spawnPosition); w.writeU32(e.objectType);
        w.writeI32(e.hp); w.writeI32(e.maxHp); w.writeU8(static_cast<std::uint8_t>(e.life));
        w.writeU16(e.record.definitionIndex); w.writeU16(e.record.recordIndex);
    }
    w.writeU32(s.livingCount); w.writeU32(s.deadCount); w.writeU32(s.coverageMask); w.writeU32(s.generation);
    w.writeU32(s.transitionSerial); w.writeU32(s.loadSerial); w.writeU64(s.captureFrameBefore); w.writeU64(s.captureFrameAfter);
    for (const auto b : s.nativeFingerprint) w.writeU8(b);
    w.writeU16(static_cast<std::uint16_t>(s.recordDefinitions.size()));
    for (const auto& d : s.recordDefinitions) {
        for (const auto b : d.layoutSha256) w.writeU8(b);
        w.writeU16(d.location.world); w.writeU16(d.location.room); w.writeU16(d.location.door);
        w.writeU16(d.location.mapProgram); w.writeU16(d.location.battleProgram); w.writeU16(d.location.eventProgram);
        w.writeU32(d.groupKey);
        for (const auto b : d.header) w.writeU8(b);
        for (const auto& r : d.records) for (const auto b : r) w.writeU8(b);
    }
    return w.take();
}
void roundtrip() {
    const auto s = fixture(); const auto wire = encodeResyncSnapshot(s);
    const auto decoded = decodeResyncSnapshot(wire);
    check(decoded.activationReplay == s.activationReplay, "all replay fields round trip");
    check(encodeResyncSnapshot(decoded) == wire, "canonical wire round trip");
    auto old = s; old.activationReplay.reset(); const auto baseline = legacyBytes(old);
    check(encodeResyncSnapshot(old) == baseline && !decodeResyncSnapshot(baseline).activationReplay,
        "absent metadata preserves frozen legacy bytes and decodes");
    check(wire.size() == baseline.size() + 1128 && std::equal(baseline.begin(), baseline.end(), wire.begin()),
        "one fixed bounded trailer preserves original prefix");
    check(resyncNativeFingerprint(old) == resyncNativeFingerprint(s), "history excluded from canonical fingerprint");
    check(desyncSha256(baseline) != desyncSha256(wire), "snapshot SHA covers trailer presence");
    auto changed = s; changed.activationReplay->point[0] += 1;
    changed.activationReplay->current.cacheAge += 1;
    check(resyncNativeFingerprint(changed) == s.nativeFingerprint &&
        desyncSha256(encodeResyncSnapshot(changed)) != desyncSha256(wire), "historical input and state covered by SHA only");
    changed = s; changed.recordDefinitions[0].header[0xE] = 0; changed.activationReplay->current.activation = 0;
    check(resyncNativeFingerprint(changed) == s.nativeFingerprint, "actual activation marker zero or one retains reviewed projection");
    ResyncBegin begin; begin.key = {std::string(32, 'a'), 100, 1}; begin.room = s.room;
    begin.targets[0] = {1,200,9}; begin.targetCount = 1; begin.snapshotCut = 1000;
    begin.totalBytes = static_cast<std::uint32_t>(wire.size()); begin.partCount = 1; begin.sha256 = desyncSha256(wire);
    ResyncPart part; part.key = begin.key; part.snapshotCut = begin.snapshotCut; part.bytes = wire;
    ResyncEnd end; end.key = begin.key; end.snapshotCut = begin.snapshotCut;
    end.totalBytes = begin.totalBytes; end.sha256 = begin.sha256;
    ResyncAssembler assembler;
    check(assembler.Begin(begin) && assembler.Part(part), "assembler stages complete immutable replay");
    const auto assembled = assembler.End(end);
    check(assembled && assembled->activationReplay == s.activationReplay, "assembler validates replay snapshot SHA");
    part.bytes.back() ^= 1;
    check(assembler.Begin(begin) && assembler.Part(part) && !assembler.End(end), "tampered trailer fails immutable SHA");
}
void malformed() {
    const auto s = fixture(); const auto bytes = encodeResyncSnapshot(s); const auto base = bytes.size() - 1128;
    bool everyTruncation = true;
    for (std::size_t n = base + 1; n < bytes.size(); ++n) {
        auto truncated = bytes; truncated.resize(n);
        everyTruncation &= rejects([&] { decodeResyncSnapshot(truncated); });
    }
    check(everyTruncation, "every partial trailer truncation rejected");
    for (const auto offset : {0u, 4u, 6u}) {
        auto corrupt = bytes; corrupt[base + offset] ^= 0x80;
        check(rejects([&] { decodeResyncSnapshot(corrupt); }), "unknown tag/version or invalid length rejected");
    }
    auto extra = bytes; extra.push_back(0);
    check(rejects([&] { decodeResyncSnapshot(extra); }), "trailing garbage rejected");
    extra = bytes; extra.insert(extra.end(), bytes.begin() + static_cast<std::ptrdiff_t>(base), bytes.end());
    check(rejects([&] { decodeResyncSnapshot(extra); }), "multiple trailers rejected");
    // NaN historical point at payload+2. The canonical fingerprint deliberately
    // cannot catch this, so decoder must perform semantic trailer validation.
    extra = bytes; extra[base + 10] = 0; extra[base + 11] = 0;
    extra[base + 12] = 0xc0; extra[base + 13] = 0x7f;
    check(rejects([&] { decodeResyncSnapshot(extra); }), "wire NaN rejected despite unchanged canonical fingerprint");
    extra = bytes; extra[base + 8] = 0xff; extra[base + 9] = 0xff;
    check(rejects([&] { decodeResyncSnapshot(extra); }), "wire invalid definition reference rejected");
    auto sentinel = fixture(); sentinel.hpSequence = 999;
    ByteReader r(extra);
    check(rejects([&] { read(r, sentinel); }) && sentinel.hpSequence == 999,
        "failed decode leaves caller output unchanged");
}
template<class F> void invalid(F&& mutate, const char* label) {
    auto s = fixture();
    check(rejects([&] { mutate(s); s.nativeFingerprint = resyncNativeFingerprint(s); encodeResyncSnapshot(s); }), label);
}
void semantic() {
    invalid([](auto& s) { s.activationReplay->point[3] = std::numeric_limits<float>::infinity(); }, "nonfinite float4 rejected");
    invalid([](auto& s) { s.activationReplay->current.cooldown = std::numeric_limits<float>::quiet_NaN(); }, "nonfinite controller cooldown rejected");
    invalid([](auto& s) { s.activationReplay->definitionIndex = 1; }, "missing selected definition rejected");
    invalid([](auto& s) { s.enemies[0].record.recordIndex = 1; }, "duplicate record references rejected");
    invalid([](auto& s) { s.enemies.pop_back(); --s.livingCount; }, "partial living pack rejected");
    invalid([](auto& s) { s.enemies[0].life = ResyncLife::ObservedDeadHistory; s.enemies[0].hp = 0; --s.livingCount; ++s.deadCount; }, "death history rejected");
    invalid([](auto& s) { s.enemies[0].objectType = 3; }, "non-MOB object type rejected");
    invalid([](auto& s) { s.hold.active = true; }, "held snapshot rejected");
    invalid([](auto& s) { s.recordDefinitions[0].groupKey++; }, "other controller key rejected");
    invalid([](auto& s) { s.recordDefinitions[0].header[0] = 1; }, "other native definition type rejected");
    invalid([](auto& s) { s.recordDefinitions[0].records[0][0x1e] = 10; }, "other raw record ID rejected");
    invalid([](auto& s) { s.recordDefinitions[0].records[0][0x1c] = 1; }, "other record mode rejected");
    invalid([](auto& s) { s.recordDefinitions[0].records[0][0x2a] = 9; }, "other record delay rejected");
    invalid([](auto& s) { s.recordDefinitions[0].records[0][0x30] = 1; }, "record flags rejected");
    invalid([](auto& s) { s.activationReplay->before.currentCount = 0; }, "incorrect zero remaining-population baseline rejected");
    invalid([](auto& s) { s.activationReplay->before.initialCount = 0; }, "incorrect initial count rejected");
    invalid([](auto& s) { s.activationReplay->before.flags = 10; }, "activated baseline flags rejected");
    invalid([](auto& s) { s.activationReplay->before.stage = 1; }, "advanced baseline stage rejected");
    invalid([](auto& s) { s.activationReplay->before.cooldown = 1; }, "noninitial baseline cooldown rejected");
    invalid([](auto& s) { s.activationReplay->before.activation = 1; }, "historical before marker must retain actual zero");
    invalid([](auto& s) { s.activationReplay->before.cacheIds[0] = 11; }, "nonempty before cache rejected");
    invalid([](auto& s) { s.activationReplay->current.activation = 2; }, "invalid activation marker rejected");
    invalid([](auto& s) { s.activationReplay->current.activation = 0; }, "current activation must match actual selected header");
    invalid([](auto& s) { s.activationReplay->current.nativeType = 1; }, "state native type mismatch rejected");
    invalid([](auto& s) { s.activationReplay->current.headerId = 31; }, "state header ID mismatch rejected");
    invalid([](auto& s) { s.activationReplay->current.recordCount = 4; }, "state record count mismatch rejected");
    invalid([](auto& s) { s.activationReplay->current.cacheRoom = 7; }, "cache room mismatch rejected");
    invalid([](auto& s) { s.activationReplay->current.cacheIds[5] = 11; }, "duplicate cache record rejected");
    invalid([](auto& s) { s.activationReplay->current.cacheIds[5] = 99; }, "unrelated cache record rejected");
    invalid([](auto& s) { s.activationReplay->current.cacheIds[0] = 0; }, "partial current cache rejected");
    invalid([](auto& s) { s.activationReplay->current.cacheIds.fill(0); }, "empty current cache rejected");
    invalid([](auto& s) { s.activationReplay->current.currentCount = 6; }, "out-of-range current population rejected");
    invalid([](auto& s) { s.activationReplay->current.initialCount = 4; }, "current initial population mismatch rejected");
    invalid([](auto& s) { s.activationReplay->firstUpdateSequence = 0; }, "missing first update sequence rejected");
    invalid([](auto& s) { s.activationReplay->hostTransition = 0; }, "missing host transition rejected");
    invalid([](auto& s) { s.activationReplay->hostLoad++; }, "stale host load rejected");
    invalid([](auto& s) { s.nativeFingerprint[0] ^= 1; encodeResyncSnapshot(s); }, "existing canonical fingerprint check remains enforced");
    invalid([](auto& s) { s.coverageMask = ResyncComplete; s.recordDefinitions.clear(); for (auto& e : s.enemies) e.record = {}; }, "metadata requires native content coverage");
}
} // namespace
int main() {
    try { roundtrip(); malformed(); semantic(); }
    catch (const std::exception& e) { std::cerr << "Unexpected exception: " << e.what() << '\n'; return 1; }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
