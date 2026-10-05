#include "CoopHudMailbox.hpp"
#include <atomic>
#include <climits>
#include <cstdio>
#include <cwchar>
#include <thread>

using namespace kh2coop;
using namespace kh2coop::inject::hud;
namespace kh2coop::inject::hud {
struct MailboxTestAccess {
    static void HoldShared(Mailbox& m) { AcquireSRWLockShared(&m.lock_); }
    static void ReleaseShared(Mailbox& m) { ReleaseSRWLockShared(&m.lock_); }
    static void HoldExclusive(Mailbox& m) { AcquireSRWLockExclusive(&m.lock_); }
    static void ReleaseExclusive(Mailbox& m) { ReleaseSRWLockExclusive(&m.lock_); }
};
}
namespace {
int checks = 0, failures = 0;
void Check(bool yes, const char* name) {
    ++checks;
    if (!yes) ++failures;
    std::printf("%s %s\n", yes ? "PASS" : "FAIL", name);
}
Input Fixture(std::uint8_t localSlot) {
    Input in;
    in.before = {PuppetAuthorityMode::Network, localSlot, 7, {101, 102, 103}};
    in.after = in.before;
    in.localAvailable = in.gameplayCurrent = true;
    in.local.worldId = 4; in.local.roomId = 26; in.local.hp = 18; in.local.maxHp = 24;
    int index = 0;
    for (std::uint8_t slot = 0; slot < 3; ++slot) {
        if (slot == localSlot) continue;
        auto& remote = in.remote[static_cast<std::size_t>(index++)];
        remote.active = true;
        remote.avatar = in.local;
        remote.avatar.ownerSlot = static_cast<SlotType>(slot);
        remote.avatar.hp = 10 + slot;
        remote.provenance = {PuppetProducer::Network, localSlot, 7,
            in.after.connectionIds[slot], in.after.connectionIds[localSlot], 101};
    }
    return in;
}
void ProjectionAndText() {
    for (std::uint8_t local = 0; local < 3; ++local) {
        auto in = Fixture(local);
        auto snap = Project(in, 100, 42);
        const auto text = Format(snap);
        Check(snap.networkCurrent && snap.localSlot == local && snap.members[local].hp == 18 &&
            text.available && std::wcsstr(text.rows[local].label, L"You") != nullptr &&
            std::wcsstr(text.rows[0].label, L"Host") != nullptr, "all local network slots map You/Host correctly");
        for (std::uint8_t slot = 0; slot < 3; ++slot) {
            if (slot != local) Check(snap.members[slot].hp == 10 + slot &&
                snap.members[slot].connectionId == 101u + slot, "remote HP follows owner slot, not friend index");
        }
    }
    auto input = Fixture(1);
    for (unsigned variant = 0; variant < 8; ++variant) {
        auto bad = input;
        if (variant == 0) bad.localAvailable = false;
        if (variant == 1) bad.gameplayCurrent = false;
        if (variant == 2) bad.local.flags = AvatarInCutscene;
        if (variant == 3) ++bad.after.generation;
        if (variant == 4) bad.before.mode = bad.after.mode = PuppetAuthorityMode::Unavailable;
        if (variant == 5) bad.before.connectionIds[0] = bad.after.connectionIds[0] = 0;
        if (variant == 6) bad.before.connectionIds[2] = bad.after.connectionIds[2] = 102;
        if (variant == 7) bad.before.localSlot = bad.after.localSlot = 255;
        const auto snap = Project(bad, 100, 42);
        Check(!snap.networkCurrent && !Format(snap).available && !snap.members[1].hpValid,
              "incomplete/retired/mixed authority or local event cannot disclose stale health");
    }
    for (unsigned variant = 0; variant < 7; ++variant) {
        auto bad = input;
        auto& remote = bad.remote[0]; // Owner 0 on the P2 machine.
        if (variant == 0) remote.active = false;
        if (variant == 1) ++remote.provenance.generation;
        if (variant == 2) ++remote.provenance.ownerConnectionId;
        if (variant == 3) remote.avatar.worldId = 5;
        if (variant == 4) remote.avatar.flags = AvatarInCutscene;
        if (variant == 5) remote.avatar.ownerSlot = static_cast<SlotType>(255);
        if (variant == 6) remote.provenance.producer = PuppetProducer::Standalone;
        const auto snap = Project(bad, 100, 42);
        Check(snap.networkCurrent && snap.members[0].state == RowState::Waiting &&
              !snap.members[0].hpValid && !Format(snap).rows[0].showBar &&
              std::wcscmp(Format(snap).rows[0].status, L"Waiting for avatar") == 0,
              "unqualified remote is Waiting, with no reason or HP invented");
    }
    auto empty = input;
    empty.before.connectionIds[2] = empty.after.connectionIds[2] = 0;
    Check(std::wcscmp(Format(Project(empty, 100, 42)).rows[2].status, L"Open slot") == 0,
          "departed slot clears HP despite retained old remote sample");
    for (const auto hp : {-1, 0, 1, 6, 18, 24, 25, INT_MAX}) {
        auto in = input; in.local.hp = hp;
        const auto snap = Project(in, 100, 42);
        const auto row = Format(snap).rows[1];
        Check(row.showBar == (hp >= 0 && hp <= 24) && row.fillPixels >= 0 && row.fillPixels <= TrackWidth,
              "HP bounds control validity and fill (including zero without downed inference)");
    }
    for (const auto maxHp : {0, -1, INT_MIN}) {
        auto in = input; in.local.maxHp = maxHp;
        Check(std::wcscmp(Format(Project(in, 100, 42)).rows[1].health, L"HP --") == 0,
              "unmapped/invalid maximum is unavailable, never a full or empty fake bar");
    }
    auto large = input; large.local.hp = large.local.maxHp = INT_MAX;
    const auto largest = Format(Project(large, 100, 42)).rows[1];
    Check(largest.fillPixels == 220 && std::wcscmp(largest.health, L"2147483647 / 2147483647") == 0,
          "full int32 HP safely formats and scales without multiply overflow");
    auto flags = input; flags.local.flags = AvatarDowned; flags.local.mp = flags.local.maxMp = 999;
    const auto truthful = Format(Project(flags, 100, 42)).rows[1];
    Check(std::wcscmp(truthful.health, L"18 / 24") == 0 && truthful.status[0] == L'\0',
          "unsupported downed/MP values do not produce HUD semantics");
    const auto base = Project(input, 100, 42);
    auto changed = base; ++changed.sampledAtMs; ++changed.frame; --changed.members[1].hp;
    Check(SameDisplayScope(base, changed), "ordinary numeric update uses existing refresh cadence");
    changed.members[0].connectionId = 999;
    Check(!SameDisplayScope(base, changed), "replacement owner invalidates cached pixels immediately");
    Check(!SameDisplayScope(base, Snapshot {}), "expired/unavailable snapshot invalidates cached pixels immediately");
}
void MailboxRules() {
    Mailbox mailbox;
    Snapshot out;
    const auto good = Project(Fixture(1), 100, 42);
    Check(!mailbox.TryCopy(100, out), "unpublished mailbox unavailable");
    Check(mailbox.TryPublish(good) && mailbox.TryCopy(100, out) && out.members[1].hp == 18,
          "published fixed snapshot copies consistently");
    Check(mailbox.TryCopy(1099, out) && !mailbox.TryCopy(1100, out) && !out.networkCurrent,
          "strict 1000ms freshness expires data and clears room/rows");
    Check(!mailbox.TryCopy(99, out), "clock regression fails unavailable");
    MailboxTestAccess::HoldShared(mailbox);
    bool published = true;
    std::thread writer([&] { published = mailbox.TryPublish(good); }); writer.join();
    MailboxTestAccess::ReleaseShared(mailbox);
    Check(!published && !mailbox.TryCopy(100, out), "writer contention returns immediately and retires old data");
    Check(mailbox.TryPublish(good), "owner can publish new data after missed try-lock");
    MailboxTestAccess::HoldExclusive(mailbox);
    bool copied = true;
    std::thread reader([&] { copied = mailbox.TryCopy(100, out); }); reader.join();
    MailboxTestAccess::ReleaseExclusive(mailbox);
    Check(!copied && !out.networkCurrent, "reader contention returns unavailable, never stale prior output");
    MailboxTestAccess::HoldShared(mailbox);
    std::thread invalidator([&] { mailbox.Invalidate(); }); invalidator.join();
    MailboxTestAccess::ReleaseShared(mailbox);
    Check(!mailbox.TryCopy(100, out), "invalidation is not lost when SRW lock is occupied");
    auto unavailable = good; unavailable.networkCurrent = false; unavailable.members = {};
    Check(mailbox.TryPublish(unavailable) && mailbox.TryCopy(100, out) && !Format(out).available && out.locationValid,
          "unavailable network can retain fresh captured room without disclosing HP");
    mailbox.Stop();
    Check(!mailbox.TryPublish(good) && !mailbox.TryCopy(100, out), "shutdown is sticky against late owner publications");
}
void ConcurrentCopies() {
    Mailbox mailbox;
    auto seed = Project(Fixture(1), 100, 1);
    for (auto& member : seed.members) { member.hp = 1; member.maxHp = 50000; }
    (void)mailbox.TryPublish(seed);
    std::atomic<bool> done {false};
    unsigned bad = 0, reads = 0;
    std::thread writer([&] {
        for (std::uint32_t n = 2; n <= 50000; ++n) {
            auto value = seed;
            value.frame = n;
            for (auto& member : value.members) member.hp = static_cast<std::int32_t>(n);
            (void)mailbox.TryPublish(value);
            if (n % 100 == 0) std::this_thread::yield();
        }
        done.store(true, std::memory_order_release);
    });
    do {
        Snapshot s;
        if (mailbox.TryCopy(101, s)) {
            ++reads;
            for (const auto& member : s.members)
                if (member.hp != static_cast<std::int32_t>(s.frame) || member.maxHp != 50000) ++bad;
        }
    } while (!done.load(std::memory_order_acquire));
    writer.join();
    Check(reads > 0 && bad == 0, "concurrent 50000-publication copies have no torn frame/member tuple");
    std::printf("concurrentCopies=%u inconsistentCopies=%u\n", reads, bad);
}
}
int main() {
    ProjectionAndText();
    MailboxRules();
    ConcurrentCopies();
    std::printf("checks=%d failures=%d\n", checks, failures);
    return failures ? 1 : 0;
}
