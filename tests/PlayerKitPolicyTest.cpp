// PlayerKit pure policy (VUH-1513): local kit decisions, the puppet guard, the roster byte and
// the receiver-side remote layout (member 0 = clone/puppet target, member 3 = own player).
// Compiled with KH2COOP_PLAYERKIT_POLICY_ONLY: no game process, no MinHook, no native reads.
#define KH2COOP_PLAYERKIT_POLICY_ONLY 1
#include "PlayerKit.cpp"

#include <cstdio>
#include <initializer_list>
#include <cstring>

using namespace kh2coop::inject::playerkit;

static int g_fail = 0, g_pass = 0;
#define CHECK(name, cond) do { if (cond) ++g_pass; else { ++g_fail; std::printf("FAIL %s\n", name); } } while (0)

static LoadContext Ctx(std::uint8_t world, std::uint16_t resolved0) {
    LoadContext c; c.world = world; c.room = 0x1A; c.resolved0 = resolved0; return c;
}

// VUH-2679: a client whose room was built in the two-player clone layout asks once for the join replay after the
// roster grows to three seats, on a settled field frame; the replayed room is accepted once at the same epoch.
static void CloneRebuild() {
    const auto step = [](CloneRebuildFacts f, CloneRebuildStep want, const char* reason) {
        const char* why = ""; return CloneRebuildDecide(f, why) == want && std::strcmp(why, reason) == 0; };
    CloneRebuildFacts outgrown; outgrown.client = true; outgrown.builtWithClone = true; outgrown.threeSeats = true;
    outgrown.settledField = true;
    CHECK("an outgrown clone room asks for its replay", step(outgrown, CloneRebuildStep::Request, "clone-outgrown"));
    auto f = outgrown; f.client = false;
    CHECK("the host (or a seat without a host epoch) never asks", step(f, CloneRebuildStep::None, "not-client"));
    f = outgrown; f.builtWithClone = false;
    CHECK("a room built natively (any non-clone world, or a 3-seat load) never asks", step(f, CloneRebuildStep::None, "native-layout"));
    f = outgrown; f.threeSeats = false;
    CHECK("two-player sessions keep their clone room (7331a05 unchanged)", step(f, CloneRebuildStep::None, "two-seats"));
    f = outgrown; f.requested = true;
    CHECK("one request per build", step(f, CloneRebuildStep::None, "requested"));
    f = outgrown; f.settledField = false;
    CHECK("an event, cutscene, menu or transition defers it", step(f, CloneRebuildStep::None, "deferred"));

    // Sequences. A tick model: facts per frame, a latch per build (transition), one request at most per build.
    struct Seat { std::uint32_t transition = 1, requestedFor = 0; unsigned requests = 0;
        bool clone = false, settled = true;
        void tick(bool client, bool threeSeats) {
            CloneRebuildFacts x; x.client = client; x.builtWithClone = clone; x.threeSeats = threeSeats;
            x.requested = requestedFor == transition; x.settledField = settled;
            const char* why = "";
            if (CloneRebuildDecide(x, why) == CloneRebuildStep::Request) { ++requests; requestedFor = transition; }
        }
        void load(bool threeSeats) { ++transition; clone = !threeSeats; } // a clone world: two seats build the clone
    };
    {   // D14: Friend1 loads with two seats; Friend2 joins; Friend1 rebuilds once; Friend2 and the host never.
        Seat friend1, friend2, host; friend1.load(false); friend2.load(true);
        for (int i = 0; i < 5; ++i) { friend1.tick(true, true); friend2.tick(true, true); host.tick(false, true); }
        CHECK("2->3 in a clone room: exactly one request, on that seat only",
              friend1.requests == 1 && friend2.requests == 0 && host.requests == 0);
        friend1.load(true); // the replayed room, built with three seats
        for (int i = 0; i < 5; ++i) friend1.tick(true, true);
        CHECK("the rebuilt room never asks again", friend1.requests == 1 && !friend1.clone);
    }
    {   // 3->2->3 with no load in between: the room was built natively, nothing to rebuild.
        Seat s; s.load(true);
        for (int i = 0; i < 3; ++i) s.tick(true, true);
        for (int i = 0; i < 3; ++i) s.tick(true, false);
        for (int i = 0; i < 3; ++i) s.tick(true, true);
        CHECK("3->2->3 without a load: no request", s.requests == 0);
        s.load(false); // a load during the two-seat stretch builds the clone
        for (int i = 0; i < 3; ++i) s.tick(true, false);
        for (int i = 0; i < 3; ++i) s.tick(true, true);
        CHECK("3->2 (load) ->3: one request for that growth", s.requests == 1);
    }
    {   // Growth during an event: deferred, then exactly one request once settled.
        Seat s; s.load(false); s.settled = false;
        for (int i = 0; i < 5; ++i) s.tick(true, true);
        const bool deferred = s.requests == 0;
        s.settled = true;
        for (int i = 0; i < 5; ++i) s.tick(true, true);
        CHECK("growth during an event is deferred to a settled field frame, then asked once", deferred && s.requests == 1);
    }
    {   // A non-clone world never builds the clone, whatever the seat count (BcCloneWorld).
        Seat s; s.transition = 1;
        for (int i = 0; i < 3; ++i) s.tick(true, false);
        for (int i = 0; i < 3; ++i) s.tick(true, true);
        CHECK("a non-clone world: no request", s.requests == 0 && !BcCloneWorld(1) && !BcCloneWorld(6) &&
              BcCloneWorld(2) && BcCloneWorld(4) && BcCloneWorld(5));
    }
    // The replay is accepted once, only for this seat's request, only while that epoch is the host's.
    CHECK("the replayed room is accepted at the same epoch for the request", AcceptReplayedRoom(true, 7, 7, 7));
    CHECK("no request: a same-epoch room is not accepted", !AcceptReplayedRoom(false, 7, 7, 7) && !AcceptReplayedRoom(true, 0, 0, 0));
    CHECK("another epoch's room is not the replay", !AcceptReplayedRoom(true, 7, 8, 7) && !AcceptReplayedRoom(true, 7, 6, 7));
    CHECK("a request superseded by a newer host epoch is not accepted", !AcceptReplayedRoom(true, 7, 7, 8));
    // A lost request: retried once, then abandoned; the seat never leaves its admitted epoch while waiting.
    CHECK("waiting before the timeout", CloneReplayWait(7, 7, REPLAY_TIMEOUT_MS - 1, 1) == ReplayWait::Waiting);
    CHECK("one retry after the timeout", CloneReplayWait(7, 7, REPLAY_TIMEOUT_MS, 1) == ReplayWait::Retry);
    CHECK("abandoned after the second timeout", CloneReplayWait(7, 7, REPLAY_TIMEOUT_MS, 2) == ReplayWait::Abandon);
    CHECK("a newer host epoch supersedes the wait at any time", CloneReplayWait(7, 8, 0, 1) == ReplayWait::Superseded &&
          CloneReplayWait(7, 8, REPLAY_TIMEOUT_MS * 5, 2) == ReplayWait::Superseded);
}

int main() {
    CloneRebuild();
    // Regression: ordinary BC default party acquires a private-clone selector
    // only inside the enabled live two-player scope, then restores only our row.
    {
        auto c = Ctx(5, SORA); c.room = 6;
        bool owned = false;
        bool ok = BcCloneEligible(true, true, true, c);
        auto row = BcCloneRow(BC_NATIVE_ROW, ok, owned);
        ok = ok && row == BC_CLONE_ROW && owned;
        row = BcCloneRow(row, true, owned); // repeat setup is idempotent
        ok = ok && row == BC_CLONE_ROW && owned;
        ok = ok && !BcCloneEligible(false, true, true, c) &&
            !BcCloneEligible(true, false, true, c) && !BcCloneEligible(true, true, false, c);
        c.evtProgram = 1; ok = ok && !BcCloneEligible(true, true, true, c);
        c.evtProgram = 0; c.eventContext = 1; ok = ok && !BcCloneEligible(true, true, true, c);
        c.eventContext = 0; c.cutsceneState = 1; ok = ok && !BcCloneEligible(true, true, true, c);
        c.cutsceneState = 0; c.world = 6; ok = ok && !BcCloneEligible(true, true, true, c);
        row = BcCloneRow(row, false, owned); ok = ok && row == BC_NATIVE_ROW && !owned;
        row = BcCloneRow(0x12020300, true, owned); ok = ok && row == 0x12020300 && !owned;
        row = BcCloneRow(BC_NATIVE_ROW, true, owned);
        row = BcCloneRow(0x12020300, false, owned); // story changed the row: preserve it
        ok = ok && row == 0x12020300 && !owned;
        CHECK("automatic BC clone setup and owned restoration", ok);
    }
    {
        bool owned = false;
        auto row = BcCloneRow(BC_NATIVE_ROW, true, owned);
        row = BcCloneRow(row, false, owned, true); // lease expired after selectors consumed
        bool ok = owned && row == BC_CLONE_ROW; // Factory still receives private status
        row = BcCloneRow(row, false, owned, false); // load complete / next pre-resolver
        ok = ok && !owned && row == BC_NATIVE_ROW;
        CHECK("lease expiry during BC construction retains applied private scope", ok);
    }
    for (const auto world : {2, 4}) {
        auto c = Ctx(static_cast<std::uint8_t>(world), SORA);
        bool owned = false;
        bool ok = BcCloneEligible(true, true, true, c);
        auto row = BcCloneRow(BC_NATIVE_ROW, ok, owned);
        ok = ok && owned && row == BC_CLONE_ROW;
        row = BcCloneRow(row, false, owned);
        ok = ok && !owned && row == BC_NATIVE_ROW;
        c.resolved0 = ROXAS; ok = ok && !BcCloneEligible(true, true, true, c);
        c.resolved0 = SORA; c.evtProgram = 1; ok = ok && !BcCloneEligible(true, true, true, c);
        c.evtProgram = 0; c.eventContext = 1; ok = ok && !BcCloneEligible(true, true, true, c);
        c.eventContext = 0; c.cutsceneState = 1; ok = ok && !BcCloneEligible(true, true, true, c);
        c.cutsceneState = 0; ok = ok && !BcCloneEligible(true, false, true, c) && !BcCloneEligible(false, true, true, c);
        row = BcCloneRow(0x12020300, true, owned);
        ok = ok && row == 0x12020300 && !owned;
        CHECK(world == 2 ? "TT default Sora clone; prologue/story/3player native" : "HB default Sora clone; story/3player native", ok);
    }
    std::uint16_t k = 1;
    // Parsing: exact allow-list, numeric only, unset = off.
    CHECK("parse empty", ParseKit("", k) && k == 0);
    CHECK("parse 0", ParseKit("0", k) && k == 0);
    CHECK("parse 0x5A", ParseKit("0x5A", k) && k == ROXAS);
    CHECK("parse 90", ParseKit("90", k) && k == ROXAS);
    CHECK("refuse 803 (first run: Roxas only)", !ParseKit("803", k));
    CHECK("refuse 0x323", !ParseKit("0x323", k));
    CHECK("Mickey 91 accepted (qualified, run 20261007-115923)", ParseKit("91", k) && k == 91);
    CHECK("refuse 803 (dual-wield later)", !ParseKit("803", k));
    CHECK("refuse sora id 84", !ParseKit("84", k));
    CHECK("refuse donald 92", !ParseKit("92", k));
    CHECK("refuse riku 2073", !ParseKit("2073", k));
    CHECK("refuse text", !ParseKit("roxas", k));
    CHECK("refuse trailing", !ParseKit("0x5Az", k));
    CHECK("refuse negative", !ParseKit("-1", k));
    CHECK("refuse big", !ParseKit("70000", k));

    // Decision matrix.
    CHECK("apply GoA", Decide(ROXAS, Ctx(4, SORA)) == Reason::Applied);
    CHECK("BB not qualified in the first run", Decide(ROXAS, Ctx(5, SORA)) == Reason::WorldNotQualified);
    CHECK("dual-wield disabled", Decide(ROXAS_DW, Ctx(4, SORA)) == Reason::Disabled);
    CHECK("Mickey applies", Decide(MICKEY, Ctx(4, SORA)) == Reason::Applied);
    CHECK("solo: Roxas allowed", SoloKitAllowed(ROXAS));
    CHECK("solo: Mickey refused (party kits only until a solo run)", !SoloKitAllowed(MICKEY));
    CHECK("solo: Sora/0 are not kits", !SoloKitAllowed(SORA) && !SoloKitAllowed(0));
    CHECK("dual-wield disabled", Decide(ROXAS_DW, Ctx(4, SORA)) == Reason::Disabled);
    CHECK("disabled kit 0", Decide(0, Ctx(4, SORA)) == Reason::Disabled);
    CHECK("world TT refused", Decide(ROXAS, Ctx(2, SORA)) == Reason::WorldNotQualified);
    CHECK("world NM refused", Decide(ROXAS, Ctx(14, 0x2B5)) == Reason::WorldNotQualified);
    CHECK("world map refused", Decide(ROXAS, Ctx(15, SORA)) == Reason::WorldNotQualified);
    { auto c = Ctx(4, SORA); c.evtProgram = 3; CHECK("event room", Decide(ROXAS, c) == Reason::EventRoom); }
    { auto c = Ctx(4, SORA); c.eventContext = 0x1234; CHECK("event context", Decide(ROXAS, c) == Reason::EventActive); }
    { auto c = Ctx(4, SORA); c.cutsceneState = 3; CHECK("cutscene", Decide(ROXAS, c) == Reason::EventActive); }
    CHECK("native costume not sora", Decide(ROXAS, Ctx(4, 0x2B5)) == Reason::NativeNotSora);
    CHECK("native zero not sora", Decide(ROXAS, Ctx(4, 0)) == Reason::NativeNotSora);
    CHECK("already kit", Decide(ROXAS, Ctx(4, ROXAS)) == Reason::AlreadyKit);

    // Apply/restore on a synthetic resolved array: only member 0 ever changes.
    std::uint16_t arr[18], ref[18];
    for (int i = 0; i < 18; ++i) arr[i] = ref[i] = static_cast<std::uint16_t>(0x54 + i);
    arr[1] = ref[1] = 0x5C; arr[2] = ref[2] = 0x5D;
    std::uint16_t orig = 0;
    CHECK("apply writes kit", ApplyAfterResolve(ROXAS, Ctx(4, SORA), arr, &orig) == Reason::Applied && arr[0] == ROXAS && orig == SORA);
    CHECK("apply leaves others", std::memcmp(arr + 1, ref + 1, sizeof(arr) - 2) == 0);
    CHECK("restore exact", RestoreResolved(ROXAS, orig, arr) && arr[0] == SORA && std::memcmp(arr, ref, sizeof(arr)) == 0);
    arr[0] = 0x2B5; // a later native load (another world) resolved its own player
    CHECK("restore never overwrites native", RestoreResolved(ROXAS, SORA, arr) && arr[0] == 0x2B5);
    arr[0] = ROXAS;
    CHECK("restore without record is a no-op", RestoreResolved(ROXAS, 0, arr) && arr[0] == ROXAS);
    arr[0] = SORA;
    { auto c = Ctx(4, SORA); arr[0] = 0x2B5; // value changed between read and apply
      CHECK("changed under us", ApplyAfterResolve(ROXAS, c, arr, &orig) == Reason::NativeNotSora && arr[0] == 0x2B5); }
    arr[0] = SORA;
    { auto c = Ctx(2, SORA); CHECK("refused world untouched", ApplyAfterResolve(ROXAS, c, arr, &orig) == Reason::WorldNotQualified && arr[0] == SORA); }
    { auto c = Ctx(4, SORA); c.cutsceneState = 1; CHECK("event untouched", ApplyAfterResolve(ROXAS, c, arr, &orig) == Reason::EventActive && arr[0] == SORA); }

    // Puppet guard: any set, non-"0" value blocks native-Sora clone puppets.
    CHECK("guard unset", !KitEnvBlocksPuppets(nullptr) && !KitEnvBlocksPuppets(""));
    CHECK("guard zero", !KitEnvBlocksPuppets("0"));
    CHECK("guard roxas", KitEnvBlocksPuppets("0x5A") && KitEnvBlocksPuppets("90"));
    CHECK("guard refused kit still blocks", KitEnvBlocksPuppets("84") && KitEnvBlocksPuppets("roxas"));
    CHECK("guard 0x0 blocks (not literal 0)", KitEnvBlocksPuppets("0x0"));

    // Roster byte for the avatar stream.
    CHECK("roster sora", RosterFromObjectId(SORA) == 0);
    CHECK("roster roxas", RosterFromObjectId(ROXAS) == 1);
    CHECK("roster dw", RosterFromObjectId(ROXAS_DW) == 2);
    CHECK("roster mickey", RosterFromObjectId(MICKEY) == 3);
    CHECK("roster unknown", RosterFromObjectId(0x2B5) == 0 && RosterFromObjectId(92) == 0);

    // Remote kit (receiver side, rev 4): member 0 feeds the clone (puppet target, built first),
    // member 3 feeds the receiver's own player (Friend1 spawn, built last -> canonical player).
    auto R = [](std::uint8_t world, std::uint8_t r1, std::uint8_t roster) {
        RemoteContext c; c.load.world = world; c.load.room = 0x1A; c.load.resolved0 = SORA;
        c.row[0] = 0; c.row[1] = r1; c.row[2] = 2; c.row[3] = 0x12; c.native3 = 0x819; c.roster = roster; return c; };
    RemoteValues v;
    CHECK("remote env exact 1", RemoteEnvRequested("1") && !RemoteEnvRequested("") && !RemoteEnvRequested("0") && !RemoteEnvRequested("11") && !RemoteEnvRequested(nullptr));
    CHECK("remote roxas: clone member0, own player member3 Sora", DecideRemote(R(4, 3, 1), &v) == RemoteReason::Applied && v.member0 == ROXAS && v.member3 == SORA);
    CHECK("remote before stream: both Sora", DecideRemote(R(4, 3, 0), &v) == RemoteReason::Applied && v.member0 == SORA && v.member3 == SORA);
    CHECK("remote unqualified roster shows Sora", DecideRemote(R(4, 3, 2), &v) == RemoteReason::Applied && v.member0 == SORA);
    CHECK("remote world 5 refused", DecideRemote(R(5, 3, 1), &v) == RemoteReason::WorldNotQualified);
    CHECK("remote selector 0 row refused", DecideRemote(R(4, 0, 1), &v) == RemoteReason::RowNotRemoteLayout);
    CHECK("remote vanilla row refused", DecideRemote(R(4, 1, 1), &v) == RemoteReason::RowNotRemoteLayout);
    { auto c = R(4, 3, 1); c.row[3] = 3; CHECK("remote world-ally slot in use refused", DecideRemote(c, &v) == RemoteReason::RowNotRemoteLayout); }
    { auto c = R(4, 3, 1); c.load.evtProgram = 2; CHECK("remote event room refused", DecideRemote(c, &v) == RemoteReason::EventRoom); }
    { auto c = R(4, 3, 1); c.load.cutsceneState = 3; CHECK("remote cutscene refused", DecideRemote(c, &v) == RemoteReason::EventActive); }
    { auto c = R(4, 3, 1); c.load.resolved0 = 0x2B5; CHECK("remote native member0 not Sora refused", DecideRemote(c, &v) == RemoteReason::NativeNotSora); }
    {
        std::uint16_t res[18]; for (int i = 0; i < 18; ++i) res[i] = static_cast<std::uint16_t>(0x100 + i);
        res[0] = SORA; res[3] = 0x819; RemoteValues rorig, set;
        CHECK("remote apply writes members 0 and 3 only", ApplyRemote(R(4, 3, 1), res, &rorig, &set) == RemoteReason::Applied &&
              res[0] == ROXAS && res[3] == SORA && rorig.member0 == SORA && rorig.member3 == 0x819 && set.member0 == ROXAS && set.member3 == SORA);
        bool others = true; for (int i = 1; i < 18; ++i) if (i != 3 && res[i] != 0x100 + i) others = false;
        CHECK("remote apply leaves other members", others);
        CHECK("remote restore exact", RestoreRemote(set, rorig, true, res) && res[0] == SORA && res[3] == 0x819);
        res[0] = 0x2B5; res[3] = 0x5E; CHECK("remote restore never overwrites native", RestoreRemote(set, rorig, true, res) && res[0] == 0x2B5 && res[3] == 0x5E);
        res[0] = ROXAS; res[3] = SORA; CHECK("remote restore without record no-op", RestoreRemote(set, rorig, false, res) && res[0] == ROXAS && res[3] == SORA);
        res[0] = SORA; res[3] = 0x5E; CHECK("remote changed under us", ApplyRemote(R(4, 3, 1), res, &rorig, &set) == RemoteReason::ChangedUnderUs && res[0] == SORA && res[3] == 0x5E);
        res[0] = ROXAS; res[3] = 0x819; CHECK("remote member0 changed under us", ApplyRemote(R(4, 3, 1), res, &rorig, &set) == RemoteReason::ChangedUnderUs && res[0] == ROXAS);
    }
    // Compile-time facts: a constant CHECK condition trips C4127 under /WX on VS 2022.
    static_assert(PUPPET_TARGET_MEMBER == 0 && OWN_PLAYER_MEMBER == 3 && FRIEND1_SELECTOR == 3, "member roles");
    CHECK("reason names", std::strcmp(RemoteReasonName(RemoteReason::ChangedUnderUs), "changed-under-us") == 0 &&
          std::strcmp(RemoteReasonName(RemoteReason::NativeNotSora), "native-not-sora") == 0);

    std::printf("{\"ok\":%s,\"passed\":%d,\"failed\":%d,\"scope\":\"PlayerKit pure policy; no game\"}\n",
                g_fail ? "false" : "true", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
