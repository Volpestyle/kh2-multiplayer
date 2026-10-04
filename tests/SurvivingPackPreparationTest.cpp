#include "kh2coop/SurvivingPackPreparation.hpp"
#include "kh2coop/ProgressAllowList.hpp"
#include <iostream>
#include <stdexcept>
#include <string_view>
using namespace kh2coop;
namespace {
int checks = 0, failures = 0;
void check(bool value, const char* label) {
    ++checks; if (!value) ++failures;
    std::cout << (value ? "PASS: " : "FAIL: ") << label << '\n';
}
template<std::size_t N> std::array<std::uint8_t, N> hex(std::string_view value) {
    if (value.size() != N * 2) throw std::runtime_error("fixture width");
    auto digit = [](char c) -> unsigned {
        if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a' + 10);
        throw std::runtime_error("fixture digit");
    };
    std::array<std::uint8_t, N> result{};
    for (std::size_t i = 0; i < N; ++i)
        result[i] = static_cast<std::uint8_t>(digit(value[i*2])*16 + digit(value[i*2+1]));
    return result;
}
struct RawDefinition { std::uint32_t group; const char* header; std::vector<const char*> records; };
// Exact ten definition / 26 record bytes from the accepted 020622 catalog.
// HP, transport, tickets, listed facts and outcomes below are synthetic inputs;
// no fixture here asserts native pending exclusion or executed native calls.
const std::vector<RawDefinition> rawDefinitions{
    {808476525u, "0100010003000000000000000000000000000000000000000000000006000000000000000000000000000000", {"36020000b4ab92b6fdff81c300c0f344000000000000000000000000010003000000000000000000000000000000000000000000000000000000000000000000","3702000000001643000082c30040e744000000000000000000000000010004000000000000000000000000000000000000000000000000000000000000000000","38020000000016c3000082c30040e744000000000000000000000000010005000000000000000000000000000000000000000000000000000000000000000000"}},
    {808476525u, "0100040003000000000000000000000000000000000000000000000006010000000000000000000000000000", {"360200005556003899ef0f3900c0cbc400000000db0f49c000000000010106000000000000000000000000000000000000000000000000000000000000000000","37020000000016c3f58414390040bfc400000000db0f49c000000000010107000000000000000000000000000000000000000000000000000000000000000000","3802000000001643f58414390040bfc400000000db0f49c000000000010108000000000000000000000000000000000000000000000000000000000000000000"}},
    {808542061u, "01000d0000000100000000000000000000000000000000000000000000000100000000000000000000000000", {}},
    {808542061u, "01000e0000000100000000000000010000000000000000000000000000000000000000000000000000000000", {}},
    {825319277u, "01000f000000020000000000000000000000000000000000000000000d000200000000000000000000000000", {}},
    {808935277u, "0100110003000000000000000000000000000000000000000000000000000000000000000000000000000000", {"410100000080984305fd7fbf00c0c6c40000000091fc4740000000000000ab000200000000000000000000000000000000000000000000000000000000000000","41010000007037c5828be6380080d4c400000000db0fc93f0000000000000a000000000000000000000000000000000000000000000000000000000000000000","4101000000203745bcec123800007a4400000000db0fc9bf00000000000009000100000000000000000000000000000000000000000000000000000000000000"}},
    {808476514u, "02001e0005000700000000000000000000000000000000000000000000000800000000000000000000000000", {"2e010000c9d9fcc107000cc38726934400000000000000000000000002000b000000000000000000000008000000000000000000000000000000000000000000","2e0100006132b84307000cc38726934400000000000000000000000002000c000000000000000000000008000000000000000000000000000000000000000000","2e0100009fcdd7c307000cc38726934400000000000000000000000002000d000000000000000000000008000000000000000000000000000000000000000000","2e01000032993344000034c38726ac4400000000000000000000000002000e000000000000000000000008000000000000000000000000000000000000000000","2e010000ce6643c4070020c38726ac44000000000000000000000000020012000000000000000000000008000000000000000000000000000000000000000000"}},
    {808476514u, "02001f0005000100000000000000000000000000000000000000000000000000000000000000000000000000", {"2e0100004e5920c2179e99bf2b87aec3000000000000000000000000020013000000000000000000000000000000000000000000000000000000000000000000","2e010000b2a66f423d9999bf2b87e0c3000000000000000000000000020014000000000000000000000000000000000000000000000000000000000000000000","04000000d7f4cc43000000802b87c7c3000000000000000000000000020015000000000000000000000000000000000000000000000000000000000000000000","04000000290bc3c3000000802b87c7c3000000000000000000000000020016000000000000000000000000000000000000000000000000000000000000000000","0400000047eac4420000c8c268e1f2c3000000000000000000000000020017000000000000000000000019000000000000000000000000000000000000000000"}},
    {808476514u, "0200200003000100000000000000000000000000000000000000000000000000000000000000000000000000", {"2e0100001fe12bc50cfe53b7cd08afc4000000000000000000000000020031000000000000000000000000000000000000000000000000000000000000000000","2e0100001f8122c58b53e6b7cd48cec4000000000000000000000000020032000000000000000000000000000000000000000000000000000000000000000000","2e0100001fc128c50cfe53b7cd88edc4000000000000000000000000020033000000000000000000000000000000000000000000000000000000000000000000"}},
    {808476514u, "0200210004000100000000000000000000000000000000000000000000000000000000000000000000000000", {"040000000806204500000080a88a9d44000000000000000000000000020034000000000000000000000019000000000000000000000000000000000000000000","0400000008862c45000048c24d157044000000000000000000000000020035000000000000000000000019000000000000000000000000000000000000000000","04000000084626450000c8c24d152544000000000000000000000000020036000000000000000000000019000000000000000000000000000000000000000000","0400000008c61945000048c34d157044000000000000000000000000020037000000000000000000000019000000000000000000000000000000000000000000"}},
};
struct Fixture {
    SurvivingPackIntent intent;
    std::vector<NativeRecordContentDefinition> local;
    std::vector<NativeRecordContentCandidate> candidates;
    std::vector<SurvivingPackMember> members;
    KnownControllerMutationTicket ticket{10, 0, true, false, true};
    Fixture() {
        auto& s = intent.snapshot;
        s.room = {1, 5, 6, 0, 1, 1, 0}; s.hold = {1, false, 0};
        s.progress.version = 1; s.progress.full = true;
        for (const auto& r : verifiedProgressAllowList())
            s.progress.spans.push_back({r.offset, std::vector<std::uint8_t>(r.length, 0)});
        s.hpSequence = 12; s.coverageMask = ResyncNativeComplete; s.livingCount = 5;
        s.generation = 2; s.transitionSerial = 2; s.loadSerial = 3;
        s.captureFrameBefore = 100; s.captureFrameAfter = 100;
        for (const auto& raw : rawDefinitions) {
            NativeRecordContentDefinition d;
            d.layoutSha256 = hex<32>("9002b2de6a1f91a790bd0673de125d1cf833f7942bfec827cdcf6ba64d5849ed");
            d.location = {5,6,0,1,1,0}; d.groupKey = raw.group; d.header = hex<44>(raw.header);
            for (const auto* bytes : raw.records) d.records.push_back(hex<64>(bytes));
            s.recordDefinitions.push_back(std::move(d));
        }
        for (std::uint16_t i = 0; i < 5; ++i) {
            ResyncEnemyState e; e.identity.netId = static_cast<std::uint16_t>(i + 1);
            e.identity.spawnIndex = i; e.identity.battleProgram = 1; e.identity.objectId = 302;
            e.objectType = 4; e.hp = 17; e.maxHp = 20; e.record = {6, i};
            s.enemies.push_back(e);
        }
        intent.begin.key = {std::string(32, 'a'), 100, 1}; intent.begin.snapshotCut = 1000;
        intent.target = {1, 200, 9}; intent.begin.targets[0] = intent.target;
        intent.begin.targetCount = 1; intent.begin.room = s.room;
        intent.context = {7, 9, 0}; intent.loadBefore = 3; intent.deadlineMs = 10000;
        Seal(); local = s.recordDefinitions; Catalog();
    }
    void Seal() {
        intent.snapshot.nativeFingerprint = resyncNativeFingerprint(intent.snapshot);
        const auto bytes = encodeResyncSnapshot(intent.snapshot);
        intent.begin.totalBytes = static_cast<std::uint32_t>(bytes.size());
        intent.begin.partCount = static_cast<std::uint16_t>((bytes.size() + RESYNC_MAX_PART_BYTES - 1) / RESYNC_MAX_PART_BYTES);
        intent.begin.sha256 = desyncSha256(bytes);
    }
    void Catalog() {
        candidates.clear();
        for (const auto& d : local) candidates.push_back({&d, NativeRecordContentStatus::Complete, true});
    }
    void Members(std::size_t count) {
        members.clear();
        for (std::size_t i = 0; i < count; ++i)
            members.push_back({{6, static_cast<std::uint16_t>(i)},302,4,17,20,true});
    }
    SurvivingPackFacts Facts(bool listedEmpty = false) const {
        SurvivingPackFacts f;
        f.context = intent.context; f.load = 4; f.arrived = true; f.mutation = ticket;
        f.catalogStatus = NativeRecordContentStatus::Complete; f.catalog = candidates;
        f.readyMembersComplete = true; f.readyMembers = members;
        if (listedEmpty) {
            f.listed = SurvivingPackListed::NoSelectedReferences;
            f.cacheAvailable = true;
        }
        return f;
    }
    SurvivingPackPreparation Start(bool observe = true) const {
        SurvivingPackPreparation p;
        if (!p.Begin(true, intent, 100)) throw std::runtime_error("valid Begin failed");
        if (observe) p.Observe(Facts(true), 101);
        return p;
    }
};
using C = SurvivingPackClassification;
using R = SurvivingPackReason;
using O = SurvivingPackOutcome;
using S = SurvivingPackSlotState;
void intentControls() {
    Fixture f; SurvivingPackPreparation p;
    check(p.Classification() == C::Disabled && !p.Begin(false, f.intent, 100) && !p.Intent(), "default off retains no intent");
    check(p.Begin(true, f.intent, 100) && p.Intent()->snapshot.recordDefinitions.size() == 10, "complete immutable ten-definition five-member intent");
    auto duplicate = f.intent; duplicate.deadlineMs = 20000;
    check(p.Begin(true, duplicate, 200) && p.Intent()->deadlineMs == 10000, "duplicate intent cannot refresh deadline");
    duplicate.deadlineMs = 9000;
    check(p.Begin(true, duplicate, 200) && p.Intent()->deadlineMs == 9000, "duplicate earlier deadline tightens");
    f.intent.snapshot.enemies[0].hp = 3;
    check(p.Intent()->snapshot.enemies[0].hp == 17, "caller mutation cannot change copied source cut");
    check(!p.Continue(1001, false, 9000) && p.Terminal() && p.Reason() == R::Deadline, "deadline is inclusive and terminal");
    Fixture next; ++next.intent.begin.key.requestId;
    check(!p.Begin(true, next.intent, 201) && p.Terminal(), "new request cannot rearm tombstone");
    auto other = next.Start(); ++next.intent.context.generation;
    check(!other.Begin(true, next.intent, 202) && other.Reason() == R::DifferentIntent, "different active scope cancels");
    Fixture shape; shape.intent.snapshot.recordDefinitions[6].records[0][0x30] = 1; shape.Seal();
    SurvivingPackPreparation unsupported;
    check(!unsupported.Begin(true, shape.intent, 100) && unsupported.Classification() == C::Unsupported && !unsupported.Terminal(), "future-stage subset unsupported without cancellation claim");
    Fixture good;
    check(!unsupported.Begin(true, good.intent, 101), "unsupported object has no automatic rearm");
    Fixture wrong; wrong.intent.begin.sha256[0] ^= 1; SurvivingPackPreparation invalid;
    check(!invalid.Begin(true, wrong.intent, 100) && invalid.Reason() == R::InvalidIntent && !invalid.Terminal(), "bad immutable digest unavailable distinct from cancellation");
    Fixture overlong; overlong.intent.deadlineMs = 100 + RESYNC_TIMEOUT_MS + 1; SurvivingPackPreparation bounded;
    check(!bounded.Begin(true, overlong.intent, 100) && bounded.Reason() == R::InvalidIntent, "first intent cannot extend beyond existing maximum deadline budget");
    for (unsigned mode = 0; mode < 5; ++mode) {
        Fixture bad;
        auto& record = bad.intent.snapshot.recordDefinitions[6].records[0];
        if (mode == 0) record[0x1c] = 1;
        if (mode == 1) record[0x1d] = 1;
        if (mode == 2) record[0x2a] = 0;
        if (mode == 3) record[0x1e] = 0;
        if (mode == 4) record[0x1f] = 0x80;
        bad.Seal(); SurvivingPackPreparation q;
        check(!q.Begin(true, bad.intent, 100) && q.Classification() == C::Unsupported, "wrong mode/position/delay/zero/high ID rejects behavioral subset");
    }
}
void sampledFacts() {
    Fixture f; auto p = f.Start(false); auto facts = f.Facts();
    check(p.Observe(facts, 101) == C::Unavailable && (p.MissingFacts() & PackMissingListed), "zero ready census does not become listed empty");
    check(!(p.MissingFacts() & PackMissingMembers), "complete zero ready census retains its independent completeness");
    check((p.MissingFacts() & PackMissingPendingCoverage) && (p.MissingFacts() & PackMissingCache), "missing cache and pending facts remain explicit");
    check(p.Observe(f.Facts(true), 102) == C::ListedEmptyUnqualified && (p.MissingFacts() & PackMissingNativeExecutionAuthority), "complete supplied empty samples remain unqualified");
    f.Members(3);
    check(p.Observe(f.Facts(), 103) == C::PartialSet, "three exact current references are partial");
    f.Members(5);
    check(p.Observe(f.Facts(), 104) == C::FullSetAlreadyPresent && (p.MissingFacts() & PackMissingListed), "all five exact memberships useful without inventing listed coverage");
    f.members[0].hp = 20;
    check(p.Observe(f.Facts(), 105) == C::FullSetAlreadyPresent, "full-set classification is not HP convergence ACK");
    check(p.Observe(f.Facts(true), 106) == C::Conflict, "ready actors contradict a supplied empty raw list");
    f.Members(5); f.local[6].header[14] ^= 1;
    check(p.Observe(f.Facts(), 107) == C::FullSetAlreadyPresent, "activation byte alone remains comparison compatible");
    f.local[6].header[15] ^= 1;
    check(p.Observe(f.Facts(), 108) == C::Conflict, "other selected header byte cannot normalize away");
    f.local[6].header[15] ^= 1; f.local[7].records[0][10] ^= 1;
    check(p.Observe(f.Facts(), 109) == C::Conflict, "full unselected record drift prevents full catalog match");
    f.local[7].records[0][10] ^= 1; f.members[1].record = f.members[0].record;
    check(p.Observe(f.Facts(), 110) == C::Conflict, "duplicate ready record association rejected");
    f.Members(5); f.members[0].exactMembership = false;
    check(p.Observe(f.Facts(), 111) == C::Unavailable, "unqualified actor reference is unavailable");
    f.Members(5); f.members[0].hp = 21;
    check(p.Observe(f.Facts(), 112) == C::Conflict, "native HP over maximum does not classify full set");
    f.Members(5); f.candidates[1].associationUnique = false;
    check(p.Observe(f.Facts(), 113) == C::Conflict, "unselected physical association ambiguity rejected");
    f.candidates[1].associationUnique = true; facts = f.Facts(); facts.catalogStatus = NativeRecordContentStatus::Partial;
    check(p.Observe(facts, 114) == C::Unavailable, "partial parent catalog cannot produce passing subset");
    for (auto listed : {SurvivingPackListed::Pending, SurvivingPackListed::Unclassifiable, SurvivingPackListed::Conflict}) {
        facts = f.Facts(); facts.listed = listed;
        check(p.Observe(facts, 115) == C::Conflict, "pending/unclassifiable/list conflict not absence");
    }
    facts = f.Facts(); facts.cacheAvailable = true; facts.cacheConflict = true;
    check(p.Observe(facts, 116) == C::Conflict, "cached entry not silently missing actor");
    facts = f.Facts(); facts.readyMembersComplete = false;
    check(p.Observe(facts, 117) == C::Unavailable, "partial ready traversal not population completion");
    Fixture reorder; std::swap(reorder.local[6], reorder.local[7]); reorder.Catalog(); reorder.Members(5);
    for (auto& member : reorder.members) member.record.definitionIndex = 7;
    auto moved = reorder.Start(false);
    check(moved.Observe(reorder.Facts(), 101) == C::FullSetAlreadyPresent, "local table order maps by exact content not slot");
}
void mutationControls() {
    Fixture f; auto p = f.Start(false); auto facts = f.Facts(true); facts.arrived = false;
    check(p.Observe(facts, 101) == C::Unavailable && p.TargetLoad() == 0, "prearrival cannot attach mutation baseline");
    facts.arrived = true; facts.load = f.intent.loadBefore;
    check(p.Observe(facts, 102) == C::Unavailable && p.TargetLoad() == 0, "old load cannot attach mutation baseline");
    facts.load = 4; facts.mutation.revision = 20;
    check(p.Observe(facts, 103) == C::ListedEmptyUnqualified && p.TargetLoad() == 4, "expected bootstrap init before fresh load does not cancel");
    ++facts.mutation.revision;
    check(p.Observe(facts, 104) == C::Terminal && p.Reason() == R::MutationChanged, "identical-byte same-load reinit ticket change cancels");
    facts.mutation.revision = 20;
    check(p.Observe(facts, 105) == C::Terminal, "restored ticket cannot undo tombstone");
    for (unsigned mode = 0; mode < 6; ++mode) {
        auto q = f.Start(); facts = f.Facts(true);
        if (mode == 0) facts.mutation.available = false;
        if (mode == 1) facts.mutation.inFlight = 1;
        if (mode == 2) facts.mutation.poisoned = true;
        if (mode == 3) facts.mutation.coverageComplete = false;
        if (mode == 4) ++facts.load;
        if (mode == 5) ++facts.context.deliverySerial;
        check(q.Observe(facts, 102) == C::Terminal, "postbaseline unavailable/inflight/poison/coverage/load/context change cancels");
    }
    auto inconsistent = f.Start(false); facts = f.Facts(true); facts.mutation.coverageComplete = false;
    check(inconsistent.Observe(facts, 101) == C::Unavailable && inconsistent.TargetLoad() == 0, "inconsistent available ticket without coverage cannot attach baseline");
    auto q = f.Start(false); facts = f.Facts(true); facts.mutation.available = false;
    check(q.Observe(facts, 101) == C::Unavailable && !q.Terminal() && q.TargetLoad() == 0, "initial missing known-boundary witness is unavailable");
}
void outcomesAndContinuation() {
    Fixture f; auto p = f.Start();
    check(p.ObserveOutcome(0, 1, O::Entry, f.ticket, 102), "actual entry observation retained before return");
    check(!p.ObserveOutcome(0, 1, O::Entry, f.ticket, 103) && !p.Terminal(), "same invocation receipt idempotent without second admission");
    check(p.ObserveOutcome(0, 1, O::NonNullReturn, f.ticket, 104) && p.Slots()[0].state == S::Pending, "nonnull unreadable return retained pending");
    check(p.Observe(f.Facts(true), 105) == C::Conflict && p.Reason() == R::Pending, "empty ready/list sample cannot release pending slot");
    check(!p.ObserveOutcome(0, 1, O::NonNullReturn, f.ticket, 106) && p.Slots()[0].state == S::Pending, "duplicate return cannot evict pending receipt");
    check(p.ObserveOutcome(0, 1, O::Ready, f.ticket, 107), "separate qualified later readiness receipt");
    f.Members(1);
    check(p.Observe(f.Facts(), 108) == C::PartialSet, "one ready own result still partial");
    check(p.ObserveOutcome(1, 2, O::Entry, f.ticket, 109), "later invocation receipt can preserve multi-turn progress");
    check(!p.ObserveOutcome(1, 2, O::NullReturn, f.ticket, 110) && p.Terminal() && p.Slots()[0].state == S::Ready, "known null stops without erasing earlier native partial outcome");
    check(!p.ObserveOutcome(1, 3, O::Entry, f.ticket, 111) && p.HasOwnAttempt(), "terminal partial cannot retry or evict");
    check(!p.Begin(true, f.intent, 112) && p.Terminal(), "duplicate intent cannot rearm partial operation");
    for (unsigned mode = 0; mode < 4; ++mode) {
        auto q = f.Start(); q.ObserveOutcome(0, 10, O::Entry, f.ticket, 102);
        if (mode == 0) q.ObserveOutcome(0, 10, O::Ready, f.ticket, 103);
        if (mode == 1) q.ObserveOutcome(1, 10, O::Entry, f.ticket, 103);
        if (mode == 2) q.ObserveOutcome(1, 11, O::Entry, f.ticket, 103);
        if (mode == 3) q.ObserveOutcome(0, 10, O::Unknown, f.ticket, 103);
        check(q.Terminal(), "ready-before-return/duplicate invocation/entered overlap/unknown cannot retry");
    }
    auto lost = f.Start(); lost.ObserveOutcome(0, 1, O::Entry, f.ticket, 102);
    lost.ObserveOutcome(0, 1, O::NonNullReturn, f.ticket, 103); lost.ObserveOutcome(0, 1, O::Ready, f.ticket, 104);
    f.Members(0);
    check(lost.Observe(f.Facts(true), 105) == C::Terminal, "disappeared own ready actor cannot become fresh empty baseline");
    auto hp = f.Start();
    check(hp.Continue(999, true, 102) && !hp.Terminal(), "precut packet cannot cancel immutable cut");
    check(hp.Continue(1100, false, 103) && hp.LastSourceSerial() == 1100, "equal heartbeat retains max serial without material cancellation");
    check(!hp.Continue(1001, true, 104) && hp.Reason() == R::MaterialChange, "admitted reordered postcut death or positive HP still cancels");
    f.Members(5);
    check(hp.Observe(f.Facts(), 105) == C::Terminal, "later old-cut full set cannot claim completion after material change");
    check(!hp.Continue(1200, false, 106), "equal later heartbeat cannot resurrect cancelled authority");
    auto sameCut = f.Start(false);
    check(sameCut.Continue(1000, true, 103) && !sameCut.Terminal(), "equal cut is retired even when caller reports material difference");
    auto deadline = f.Start();
    check(!deadline.ObserveOutcome(0, 1, O::Entry, f.ticket, 10000) && deadline.Terminal() && !deadline.HasOwnAttempt(), "expired deadline records no attempted entry");
    auto changed = f.Start(); changed.ObserveOutcome(0, 1, O::Entry, f.ticket, 102);
    auto newTicket = f.ticket; ++newTicket.revision;
    check(!changed.ObserveOutcome(0, 1, O::NonNullReturn, newTicket, 103) && changed.Terminal() && changed.Slots()[0].state == S::Entered, "mutation before return preserves unresolved entry and cancels");
    auto stray = f.Start();
    check(!stray.ObserveOutcome(0, 1, O::NonNullReturn, f.ticket, 102) && stray.Terminal(), "return without matching entry is terminal");
    auto overflow = f.Start();
    check(!overflow.ObserveOutcome(5, 1, O::Entry, f.ticket, 102) && overflow.Terminal(), "sixth slot cannot exceed fixed five-record ledger");
    auto material = f.Start(false);
    check(!material.Continue(1001, true, 103) && material.Terminal(), "caller-admitted postcut material boolean cancels without packet-parser claim");
    auto cancelled = f.Start(false); cancelled.Cancel();
    check(cancelled.Terminal() && !cancelled.Begin(true, f.intent, 105), "explicit cancellation leaves persistent tombstone");
}
// New supplied raw/cache joins only. The native reader and root actor/C/R join
// have separate tests; these facts never prove pre-link creator exclusion.
void occupancyJoinControls() {
    Fixture f; f.Members(5); auto p = f.Start(false);
    auto joined = [&]() {
        auto facts = f.Facts(); facts.listed = SurvivingPackListed::SelectedReadyReferences;
        facts.listedReadyMask = 31; facts.cacheAvailable = true;
        facts.cacheSelectedMask = 31; facts.cacheEntryCount = 5;
        facts.controller = {true, 2, 5, 5, 0.0f, 0, 1};
        return facts;
    };
    auto facts = joined();
    check(p.Observe(facts, 101) == C::FullSetAlreadyPresent, "complete selected-ready raw join and five cached IDs classify present");
    check((p.MissingFacts() & PackMissingGlobalPrelinkCoverage) &&
          (p.MissingFacts() & PackMissingNativeExecutionAuthority), "full sampled join cannot clear global prelink or execution limits");
    check(!(p.MissingFacts() & (PackMissingListed | PackMissingCache | PackMissingControllerState)), "qualified listed cache controller samples clear only their own missing bits");
    check(p.ControllerSample().available && p.ControllerSample().currentCount == 5 &&
          p.ControllerSample().activation == 1, "controller diagnostics retained without inferred initialization history");
    facts.controller.stage = 3; facts.controller.cooldown = 8.0f; facts.controller.flags = 10;
    check(p.Observe(facts, 102) == C::FullSetAlreadyPresent && p.ControllerSample().stage == 3 &&
          p.ControllerSample().cooldown == 8.0f, "native stage cooldown and flags remain sampled diagnostics not repair or admission");
    facts = joined(); facts.cacheEntryCount = 6;
    check(p.Observe(facts, 103) == C::FullSetAlreadyPresent, "checked unrelated cache entry does not erase exact existing selected membership");
    facts = joined(); facts.cacheConflict = true;
    check(p.Observe(facts, 104) == C::Conflict, "duplicate or unresolved cache provenance conflicts despite matching mask");
    facts = joined(); facts.cacheSelectedMask = 15; facts.cacheEntryCount = 4;
    check(p.Observe(facts, 105) == C::Conflict, "missing cache member in otherwise complete five-ready join holds");
    facts = joined(); facts.listedReadyMask = 15;
    check(p.Observe(facts, 106) == C::Conflict, "incomplete selected raw mask cannot match full ready set");
    facts = joined(); facts.listedReadyMask = 0;
    check(p.Observe(facts, 107) == C::Conflict, "selected-ready status cannot smuggle empty mask");
    facts = joined(); facts.listedReadyMask = 0x80;
    check(p.Observe(facts, 108) == C::Conflict, "out-of-range listed native record bit rejected");
    facts = joined(); facts.cacheSelectedMask = 0x80;
    check(p.Observe(facts, 109) == C::Conflict, "out-of-range cache native record bit rejected");
    facts = joined(); facts.cacheEntryCount = 257;
    check(p.Observe(facts, 110) == C::Conflict, "cache sample bounded to 256 raw slots");
    facts = joined(); facts.cacheEntryCount = 4;
    check(p.Observe(facts, 111) == C::Conflict, "five selected cache bits cannot fit four reported entries");
    facts = joined(); facts.listed = static_cast<SurvivingPackListed>(255);
    check(p.Observe(facts, 112) == C::Conflict, "unknown listed classifier value held");
    for (auto state : {SurvivingPackListed::Pending, SurvivingPackListed::Deferred, SurvivingPackListed::Unclassifiable}) {
        facts = joined(); facts.listed = state;
        check(p.Observe(facts, 113) == C::Conflict, "pending deferred or unclassifiable raw nodes cannot become five-ready");
    }
    facts = joined(); facts.cacheAvailable = false;
    check(p.Observe(facts, 114) == C::FullSetAlreadyPresent && (p.MissingFacts() & PackMissingCache), "exact present membership remains useful while cache read unavailable stays explicit");
    facts = joined(); facts.listed = SurvivingPackListed::Unavailable; facts.listedReadyMask = 0;
    check(p.Observe(facts, 115) == C::FullSetAlreadyPresent && (p.MissingFacts() & PackMissingListed), "ready membership never upgrades unavailable raw traversal");
    facts = joined(); facts.controller.available = false;
    check(p.Observe(facts, 116) == C::FullSetAlreadyPresent && (p.MissingFacts() & PackMissingControllerState), "missing selected state does not masquerade sampled controller qualification");
    f.Members(3); facts = joined(); facts.readyMembers = f.members;
    facts.listedReadyMask = 7; facts.cacheSelectedMask = 7; facts.cacheEntryCount = 3;
    check(p.Observe(facts, 117) == C::PartialSet, "three exact listed ready cached members stay partial");
    f.Members(0); facts = f.Facts(true);
    check(p.Observe(facts, 118) == C::ListedEmptyUnqualified &&
          (p.MissingFacts() & PackMissingGlobalPrelinkCoverage), "complete sampled empty raw plus empty cache needs no invented global coverage");
    facts.cacheEntryCount = 1;
    check(p.Observe(facts, 119) == C::Conflict, "nonempty unrelated room cache cannot become fresh empty enrollment baseline");
    facts = f.Facts(true); facts.cacheSelectedMask = 1; facts.cacheEntryCount = 1;
    check(p.Observe(facts, 120) == C::Conflict, "cached selected record without current ready actor is not missing permission");
    facts = f.Facts(true); facts.listedReadyMask = 1;
    check(p.Observe(facts, 121) == C::Conflict, "empty raw status with positive selected mask is inconsistent");
    facts = f.Facts(true); facts.cacheAvailable = false;
    check(p.Observe(facts, 122) == C::Unavailable && (p.MissingFacts() & PackMissingCache), "empty listed sample with unread cache remains unavailable");
    facts = f.Facts(true); facts.listed = SurvivingPackListed::Unavailable;
    check(p.Observe(facts, 123) == C::Unavailable && (p.MissingFacts() & PackMissingListed), "empty ready and cache do not imply complete raw traversal");
    auto pending = f.Start(); pending.ObserveOutcome(0, 1, O::Entry, f.ticket, 102);
    pending.ObserveOutcome(0, 1, O::NonNullReturn, f.ticket, 103);
    check(pending.Observe(f.Facts(true), 104) == C::Conflict && pending.Slots()[0].state == S::Pending, "sampled empty raw/cache cannot evict pre-readiness own return");
    facts = f.Facts(true); facts.listed = SurvivingPackListed::Pending;
    check(pending.Observe(facts, 105) == C::Conflict && !pending.Terminal() && pending.Slots()[0].state == S::Pending, "listed delayed readiness holds own pending receipt without terminal retry permission");
    facts = f.Facts(true); facts.listed = SurvivingPackListed::Deferred;
    check(pending.Observe(facts, 105) == C::Terminal && pending.Slots()[0].state == S::Pending, "deferred conflict after own return retains terminal partial evidence");
    check(!pending.Begin(true, f.intent, 106), "occupancy failure cannot rearm prior pending attempt");
    auto mutableSource = f.Start();
    mutableSource.Continue(1100, false, 102); mutableSource.Continue(1001, true, 103);
    check(mutableSource.Observe(f.Facts(true), 104) == C::Terminal, "new empty-join classification cannot revive ordered material cancellation");
    auto reinit = f.Start(); facts = f.Facts(true); ++facts.mutation.revision;
    check(reinit.Observe(facts, 104) == C::Terminal, "identical empty raw/cache bytes cannot hide mutation revision change");
    auto expired = f.Start();
    check(expired.Observe(f.Facts(true), 10000) == C::Terminal, "sampled-empty facts cannot refresh original deadline");
}

} // namespace
int main(int argc, char** argv) {
    std::cout << std::unitbuf;
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--occupancy-join-only") occupancyJoinControls();
        else if (argc == 1) { intentControls(); sampledFacts(); mutationControls(); outcomesAndContinuation(); occupancyJoinControls(); }
        else throw std::runtime_error("unknown test selection");
    }
    catch (const std::exception& e) { std::cerr << "EXCEPTION: " << e.what() << '\n'; return 2; }
    std::cout << checks - failures << " PASS / " << failures << " FAIL\n";
    return failures ? 1 : 0;
}
