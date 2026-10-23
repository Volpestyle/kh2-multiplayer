// Production Warp.cpp through the existing NativeHitClaimTest owned-memory seam.
#define main OriginalNativeHitClaimMain
#include "NativeHitClaimTest.cpp"
#undef main
#include <string>

namespace {
namespace w = kh2coop::inject::recoverywarp;
namespace p = kh2coop::inject::progresssync;
std::vector<std::string> receipts;
std::vector<std::string> seals;
void CauseLog(const char* format, ...) {
    char output[4096] {};
    va_list args; va_start(args, format);
    vsnprintf_s(output, sizeof(output), _TRUNCATE, format, args); va_end(args);
    if (std::string(output).starts_with("[load-cause]")) {
        receipts.emplace_back(output);
        std::cout << output << '\n';
    }
    if (std::string(output).starts_with("[load-cause-seal]")) {
        seals.emplace_back(output); std::cout << output << '\n';
    }
}
bool Has(const char* event, const char* origin, bool available = true) {
    const std::string action = std::string(" event=") + event + " ";
    const std::string source = std::string(" origin=") + origin + " ";
    const std::string availability = std::string(" available=") + (available ? "1 " : "0 ");
    for (const auto& row : receipts)
        if (row.find(action) != std::string::npos && row.find(source) != std::string::npos &&
            row.find(availability) != std::string::npos) return true;
    return false;
}
unsigned Count(const char* event) {
    unsigned count = 0; const std::string needle = std::string(" event=") + event + " ";
    for (const auto& row : receipts) if (row.find(needle) != std::string::npos) ++count;
    return count;
}
void Start() {
    ResetWorld(); receipts.clear(); seals.clear(); w::g_hostCause = {}; w::g_loadEvidenceSequence = 0;
    w::g_loadLastSealFrame = 0;
    w::g_loadSealEmitted = false;
    w::g_log = CauseLog;
    p::g_clientFull = true; p::g_desiredGeneration = 1; p::g_desiredApplied = false;
}
kh2coop::ProducerWorldContext Context() { return {1, 1, 0}; }
kh2coop::WorldScope Scope() { return {std::string(32, 'a'), 100, 1, 33, 101, 1}; }
kh2coop::ResyncBegin Begin(kh2coop::ResyncPhase phase = kh2coop::ResyncPhase::Bootstrap) {
    kh2coop::ResyncBegin result;
    result.key = {std::string(32, 'a'), 100, 17}; result.room = arrivalTarget;
    result.phase = phase; result.snapshotCut = 33;
    result.targetCount = 1; result.targets[0] = {1, 101, 1}; result.sha256.fill(0xAA);
    return result;
}
void IssueCompleteArrive() {
    w::IssueHostTransition();
    Check(warpCalls == 1 && w::g_transitionPending, "actual issue invokes one original and opens transition");
    Check(!w::HostTransitionArrived(9), "arrival unavailable before actual CompleteLoad");
    w::CompleteLoad();
    Check(w::HostTransitionArrived(9), "actual completion allows matching arrival");
}
void TestOrdinaryAndBootstrap() {
    Start(); auto context = Context(); auto scope = Scope();
    Check(w::QueueHostTransition(arrivalTarget, context, &scope, nullptr, nullptr), "ordinary admitted queue accepted");
    IssueCompleteArrive();
    Check(Has("queue","ordinary_host_room") && Has("issue","ordinary_host_room") &&
          Has("load","ordinary_host_room") && Has("arrival","ordinary_host_room"),
          "ordinary full causal chain attributed with actual client hostSource zero");
    const auto count = receipts.size(); w::HostTransitionArrived(9); w::IssueHostTransition();
    Check(receipts.size() == count && warpCalls == 1, "repeated arrival/issue emits no extra event or original");
    Check(w::g_hostCause.context.hostSourceSerial == 0 && receipts[0].find("hostSource=33 ") != std::string::npos,
          "diagnostic host source comes from admitted scope without rewriting context");

    Start(); auto begin = Begin();
    Check(w::QueueHostTransition(arrivalTarget, context, nullptr, &begin, &begin.targets[0]), "Bootstrap admitted queue accepted");
    IssueCompleteArrive();
    Check(Has("queue","resync_bootstrap") && Has("issue","resync_bootstrap") &&
          Has("load","resync_bootstrap") && Has("arrival","resync_bootstrap"), "Bootstrap full causal chain attributed");
    Check(receipts[0].find("request=17 target=101 targetDelivery=1 phase=0 cut=33") != std::string::npos,
          "Bootstrap key/target/phase/cut retained in receipt");
    Check(w::g_loadSerial == 12 && w::g_transitionSerial == 8 && w::g_hostCause.loadBefore == 11,
          "actual load/transition serials retained rather than inferred from queue");

    Start(); begin = Begin(kh2coop::ResyncPhase::Checkpoint);
    w::QueueHostTransition(arrivalTarget, context, nullptr, &begin, &begin.targets[0]);
    IssueCompleteArrive();
    Check(Has("load","resync_checkpoint") && !Has("load","resync_bootstrap"),
          "diagnostic records forbidden Checkpoint load honestly instead of relabeling it");
}
// VUH-2344: a client's own event exit to the host's target opens the gate.
void TestBlockedNativeExitMatch() {
    Start(); auto context = Context(); auto scope = Scope();
    w::QueueHostTransition(arrivalTarget, context, &scope, nullptr, nullptr);
    Put(image + offsets::CUTSCENE_STATE, std::int32_t{2});  // the client's event is active
    w::IssueHostTransition();
    Check(warpCalls == 0 && w::g_hostQueued && !p::g_desiredApplied, "an unsafe client waits for its safe state");
    w::LocationPacket exit {}; exit.world = 5; exit.room = 7; exit.door = 3;
    w::g_blockedExit = exit; w::g_blockedExitLoad = w::g_loadSerial;
    w::IssueHostTransition();
    Check(warpCalls == 0, "a blocked exit to another room does not bypass the safe-state gate");
    exit.room = 2; w::g_blockedExit = exit; w::g_blockedExitLoad = w::g_loadSerial - 1;
    w::IssueHostTransition();
    Check(warpCalls == 0, "a blocked exit from before the last load is stale");
    w::g_blockedExitLoad = w::g_loadSerial;
    w::IssueHostTransition();
    Check(warpCalls == 1 && w::g_transitionPending && !w::g_blockedExit && p::g_desiredApplied,
          "the host target issues, with progress applied, at the client's own matching blocked exit");

    // Only the event checks are relaxed: every other gate still holds at a matching, current exit.
    const auto atMatchingExit = [&](const char* label, auto perturb) {
        Start(); auto c = Context(); auto sc = Scope();
        w::QueueHostTransition(arrivalTarget, c, &sc, nullptr, nullptr);
        Put(image + offsets::CUTSCENE_STATE, std::int32_t{2});
        w::LocationPacket match {}; match.world = 5; match.room = 2; match.door = 3;
        w::g_blockedExit = match; w::g_blockedExitLoad = w::g_loadSerial;
        perturb();
        w::IssueHostTransition();
        Check(warpCalls == 0 && !p::g_desiredApplied, label);
    };
    atMatchingExit("an open menu still blocks issue and progress at a matching exit",
                   [] { Put(image + offsets::OPEN_MENU, std::uint8_t{0x0A}); });
    atMatchingExit("leaving the field still blocks issue and progress at a matching exit",
                   [] { Put(image + offsets::IN_FIELD, std::uint8_t{0}); });
    atMatchingExit("a pending transition still blocks issue and progress at a matching exit",
                   [] { w::g_transitionPending = true; });
}
// VUH-2464: a title-time client blocked at its own opening exit follows the host's first prologue field.
void TestBlockedExitToPrologueSite() {
    Start(); auto context = Context(); auto scope = Scope();
    kh2coop::RoomTransition tram {};
    tram.epoch = 1; tram.worldId = 2; tram.roomId = 7; tram.door = 0;
    tram.mapProgram = tram.battleProgram = tram.eventProgram = 95;
    w::QueueHostTransition(tram, context, &scope, nullptr, nullptr);
    Put(image + offsets::CUTSCENE_STATE, std::int32_t{2}); // the client's opening event is still active
    w::LocationPacket movieExit {}; movieExit.world = 2; movieExit.room = 1; movieExit.map = movieExit.battle = movieExit.event = 56;
    w::g_blockedExit = movieExit; w::g_blockedExitLoad = w::g_loadSerial - 1;
    w::IssueHostTransition();
    Check(warpCalls == 0 && w::g_hostQueued, "a stale opening exit does not issue the prologue target");
    w::g_blockedExitLoad = w::g_loadSerial;
    w::IssueHostTransition();
    Check(warpCalls == 1 && w::g_transitionPending && !w::g_blockedExit && p::g_desiredApplied,
          "the prologue's first field issues, with progress applied, at the client's own opening exit");

    Start(); context = Context(); scope = Scope();
    tram.eventProgram = 96; // not a checked prologue site
    w::QueueHostTransition(tram, context, &scope, nullptr, nullptr);
    Put(image + offsets::CUTSCENE_STATE, std::int32_t{2});
    w::g_blockedExit = movieExit; w::g_blockedExitLoad = w::g_loadSerial;
    w::IssueHostTransition();
    Check(warpCalls == 0 && w::g_hostQueued, "an unpinned target still waits for the safe field gate");
}

// VUH-2356: the "Waiting for host..." indicator at a client's own blocked exit.
void TestWaitingAtBlockedExit() {
    Start(); w::g_blockedExit.reset(); w::RefreshWaitingAtExit();
    Check(!w::WaitingAtBlockedExit(), "no blocked exit: not waiting");
    w::LocationPacket exit {}; exit.world = 2; exit.room = 0x13;
    w::g_blockedExit = exit; w::g_blockedExitLoad = w::g_loadSerial; w::RefreshWaitingAtExit();
    Check(!w::WaitingAtBlockedExit(), "a free client that bumped a door the host didn't take keeps playing");
    Put(image + offsets::CUTSCENE_STATE, std::int32_t{2}); w::RefreshWaitingAtExit();
    Check(w::WaitingAtBlockedExit(), "a client whose own event is at a blocked exit waits for the host");
    w::g_transitionPending = true; w::RefreshWaitingAtExit();
    Check(!w::WaitingAtBlockedExit(), "a pending load is not waiting at the exit");
    w::g_transitionPending = false; w::CompleteLoad();
    Check(!w::WaitingAtBlockedExit() && !w::g_blockedExit, "the next completed load ends the wait");
    w::g_blockedExit = exit; w::g_blockedExitLoad = w::g_loadSerial; w::RefreshWaitingAtExit();
    w::SetClientAuthority(false);
    Check(!w::WaitingAtBlockedExit(), "losing client authority ends the wait");
}
void TestRetireSupersedeAndDrift() {
    Start(); auto context = Context(); auto scope = Scope();
    w::QueueHostTransition(arrivalTarget, context, &scope, nullptr, nullptr);
    const auto firstCause = w::g_hostCause.id;
    w::QueueHostTransition(arrivalTarget, context, &scope, nullptr, nullptr);
    Check(Has("superseded","ordinary_host_room") && w::g_hostCause.id != firstCause && Count("queue") == 2,
          "supersede preserves old receipt and gives replacement distinct cause");
    w::SetClientAuthority(false); w::IssueHostTransition();
    Check(Has("retire","ordinary_host_room") && !w::g_hostQueued && !w::g_hostCause.id && warpCalls == 0,
          "authority retirement clears cause and queued original");
    w::CompleteLoad();
    Check(Has("load","unattributed",false), "post-retirement load cannot borrow old cause");

    Start(); w::QueueHostTransition(arrivalTarget, context, &scope, nullptr, nullptr); ++g_bridge.generation;
    w::IssueHostTransition();
    Check(warpCalls == 0 && !Has("issue","ordinary_host_room"), "generation drift before issue blocks original");
    w::CompleteLoad();
    Check(Has("load","unattributed",false), "generation-drift load evidence unavailable");

    Start(); w::QueueHostTransition(arrivalTarget, context, &scope, nullptr, nullptr); w::IssueHostTransition();
    ++g_bridge.deliverySerial; w::CompleteLoad();
    Check(Has("load","unattributed",false) && !w::HostTransitionArrived(9), "ordered delivery drift prevents attribution and arrival");

    Start(); w::QueueHostTransition(arrivalTarget, context, &scope, nullptr, nullptr); w::IssueHostTransition();
    ++w::g_transitionSerial; w::CompleteLoad();
    Check(Has("load","unattributed",false), "intervening native transition invalidates load attribution");

    Start(); w::QueueHostTransition(arrivalTarget, context, &scope, nullptr, nullptr); w::IssueHostTransition();
    w::QueueHostTransition(arrivalTarget, context, &scope, nullptr, nullptr); w::CompleteLoad();
    Check(Has("superseded","ordinary_host_room") && Has("load","unattributed",false) &&
          !w::HostTransitionArrived(9), "replacement queued during issued load cannot inherit old completion");

    Start(); w::QueueHostTransition(arrivalTarget, context, &scope, nullptr, nullptr); w::IssueHostTransition();
    Put(image + offsets::EVENT_PROGRAM, std::uint16_t{9}); w::CompleteLoad();
    Check(Has("load","unattributed",false) && !w::HostTransitionArrived(9), "actual completion at wrong full tuple stays unattributed");

    Start(); w::CompleteLoad();
    Check(Has("load","unattributed",false), "unsolicited native completion has no invented cause");
    Start(); w::QueueHostTransition(arrivalTarget, context, &scope, nullptr, nullptr); w::IssueHostTransition(); w::CompleteLoad();
    receipts.clear(); w::CompleteLoad(); w::HostTransitionArrived(9);
    Check(Has("load","unattributed",false) && Has("arrival","unattributed",false), "second completion cannot reuse first cause");
}
void TestInvalidDiagnosticJoins() {
    auto context = Context();
    for (int mutation = 0; mutation < 6; ++mutation) {
        Start(); auto begin = Begin(); auto target = begin.targets[0];
        if (mutation == 0) target.connectionId = 999;
        if (mutation == 1) target.deliverySerial = 999;
        if (mutation == 2) begin.room.eventProgram ^= 1;
        if (mutation == 3) begin.targetCount = 0;
        if (mutation == 4) begin.key.requestId = 0;
        if (mutation == 5) begin.snapshotCut = 0;
        Check(w::QueueHostTransition(arrivalTarget, context, nullptr, &begin, &target),
              "invalid diagnostic join does not alter pre-existing queue authority");
        Check(Has("queue","unattributed",false), "invalid target/key/cut/tuple join explicitly unavailable");
    }
    Start(); w::QueueHostTransition(arrivalTarget);
    Check(Has("queue","unattributed",false), "legacy overload never invents causal origin");
    Start(); auto scope = Scope(); scope.targetDeliverySerial = 2;
    w::QueueHostTransition(arrivalTarget, context, &scope, nullptr, nullptr);
    Check(Has("queue","unattributed",false), "scope/context delivery mismatch unavailable");

    Start(); scope = Scope(); context.deliverySerial = scope.targetDeliverySerial = 999;
    w::QueueHostTransition(arrivalTarget, context, &scope, nullptr, nullptr);
    Check(Has("queue","unattributed",false), "current bridge delivery must match diagnostic context (adversary)");

    Start(); scope = Scope(); context = Context();
    w::QueueHostTransition(arrivalTarget, context, &scope, nullptr, nullptr);
    g_bridge.deliverySerial = g_orderedDeliverySerial = 2; w::IssueHostTransition(); w::CompleteLoad();
    Check(Has("issue","unattributed",false) && Has("load","unattributed",false),
          "same-generation later ordered delivery cannot reuse old diagnostic cause");

    Start(); w::g_loadEvidenceSequence = UINT64_MAX;
    w::QueueHostTransition(arrivalTarget, Context(), &scope, nullptr, nullptr);
    Check(receipts.size() == 1 && receipts[0].find("complete=0") != std::string::npos && w::g_hostCause.id == 0,
          "receipt sequence exhaustion explicitly invalidates completeness");
}
// VUH-2679: the relay's replay of a clone-layout room is queued like any accepted host room (same epoch, same
// location). If an event or cutscene starts mid-rebuild, nothing is issued until the field settles; then one load.
void TestReplayDuringEvent() {
    Start(); auto context = Context(); auto scope = Scope();
    Check(w::QueueHostTransition(arrivalTarget, context, &scope, nullptr, nullptr), "the replayed room is queued");
    Put(image + offsets::EVENT_CONTEXT, uintptr_t{0x1234});
    w::IssueHostTransition(); w::IssueHostTransition();
    Check(warpCalls == 0 && w::g_hostQueued && !w::g_transitionPending, "an event mid-rebuild holds the reload");
    Put(image + offsets::EVENT_CONTEXT, uintptr_t{0}); Put(image + offsets::CUTSCENE_STATE, std::int32_t{2});
    w::IssueHostTransition();
    Check(warpCalls == 0 && w::g_hostQueued, "so does a cutscene");
    Put(image + offsets::CUTSCENE_STATE, std::int32_t{0});
    IssueCompleteArrive();
    w::IssueHostTransition();
    Check(warpCalls == 1 && Count("load") == 1, "once settled: exactly one reload, then arrival");
}
// VUH-2679 r3 (Quill r2 R4): the clone-room rebuild's replay through the production issue seam. It is issued in the
// call that accepts it, with RebuildSafeField rechecked inside IssueHostTransition, or withdrawn with the arrived room
// put back; it is never left queued for a later frame. Ordinary host rooms keep their own gates.
void TestCloneRebuildIssue() {
    namespace es = kh2coop::inject::enemysync;
    const auto arrive = [] {
        Start(); auto c = Context(); auto sc = Scope();
        w::QueueHostTransition(arrivalTarget, c, &sc, nullptr, nullptr); IssueCompleteArrive();
        return warpCalls;
    };
    const auto replay = [] { auto c = Context(); auto sc = Scope(); return w::QueueCloneRebuild(arrivalTarget, c, &sc); };
    auto calls = arrive();
    const bool queued = replay() && w::g_hostCause.cloneRebuild && w::g_hostQueued;
    Check(queued && w::IssueCloneRebuild() && warpCalls == calls + 1 && w::g_transitionPending && !w::g_hostQueued &&
          w::g_hostIssued && w::g_hostCause.issued, "a clone rebuild at a safe field is issued natively at once");
    const auto withdrawn = [&](const char* label, auto perturb, auto restore) {
        calls = arrive();
        const auto arrivedCause = w::g_hostCause.id;
        const bool q = replay();
        perturb();
        w::IssueHostTransition();  // the gate inside the native issue itself
        const bool held = warpCalls == calls && w::g_hostQueued;
        const bool issued = w::IssueCloneRebuild();
        restore();
        for (int i = 0; i < 3; ++i) w::OnFrameStart(400 + i, player);  // later frames, now safe: nothing left to issue
        Check(q && held && !issued && warpCalls == calls && !w::g_hostQueued && w::g_hostIssued && !w::g_hostCause.cloneRebuild &&
              w::g_hostCause.id == arrivedCause && w::MatchesArrivedHostTransition(arrivalTarget), label);
    };
    withdrawn("queued, then a battle (BATTLE_STATUS=2): not issued, the arrived room stays, never reloaded later",
              [] { Put(image + offsets::BATTLE_STATUS, std::int32_t{2}); },
              [] { Put(image + offsets::BATTLE_STATUS, std::int32_t{0}); });
    withdrawn("queued, then this seat downed: not issued",
              [] { es::g_localDowned.downed = true; }, [] { es::g_localDowned.downed = false; });
    withdrawn("queued, then the host's event hold: not issued",
              [] { es::g_host.holdActive = true; }, [] { es::g_host.holdActive = false; });
    withdrawn("queued, then a boss population: not issued",
              [] { es::Spawn s; s.present = true; s.objectType = 3; s.objectId = 307; es::g_inst.spawns.push_back(s); },
              [] { es::g_inst.spawns.clear(); });
    // An event defers nothing: the replay is withdrawn, and the event ending into combat finds nothing queued.
    withdrawn("an event at the replay: withdrawn; the event ending into combat issues nothing",
              [] { Put(image + offsets::EVENT_CONTEXT, uintptr_t{0x1234}); },
              [] { Put(image + offsets::EVENT_CONTEXT, uintptr_t{0}); Put(image + offsets::BATTLE_STATUS, std::int32_t{2}); });
    Put(image + offsets::BATTLE_STATUS, std::int32_t{0});
    // Ordinary host rooms are unchanged: a new host room issues in combat, and needs no clone permit.
    Start(); auto c = Context(); auto sc = Scope();
    Put(image + offsets::BATTLE_STATUS, std::int32_t{2});
    w::QueueHostTransition(arrivalTarget, c, &sc, nullptr, nullptr);
    Check(!w::g_hostCause.cloneRebuild && !w::IssueCloneRebuild() && w::g_hostQueued, "an ordinary host room is not a clone rebuild");
    w::IssueHostTransition();
    Check(warpCalls == 1 && w::g_transitionPending, "an ordinary host room still issues during a battle");
    Put(image + offsets::BATTLE_STATUS, std::int32_t{0});

    // The issue-readiness fact the request, the accept and the issue gate share (no writes).
    calls = arrive();
    const char* why = "";
    const bool ready = w::CloneIssueReady() && es::CloneRebuildIssueSafe(why);
    Put(image + offsets::OPEN_MENU, std::uint8_t{0x0A});
    const bool menu = !w::CloneIssueReady();
    Put(image + offsets::OPEN_MENU, std::uint8_t{0xFF}); p::g_clientFull = false;
    const bool progress = !w::CloneIssueReady() && !es::CloneRebuildIssueSafe(why) && std::string(why) == "issue-wait";
    p::g_clientFull = true;
    // An owned script camera is gameplay to progress (SafeGameplay) but not a safe warp (SafeToWarp): not ready.
    // The camera proof is synthetic here (as in TestOwnedCameraGameplay); this exercises the readiness composition.
    SetScriptCameraCurrent([](uintptr_t context, std::int32_t) noexcept { return context == image + 0x71000; });
    Put(image + offsets::CUTSCENE_STATE, std::int32_t{2}); Put(image + offsets::EVENT_CONTEXT, image + 0x71000);
    const bool camera = p::RoomBoundaryReady(1) && !w::CloneIssueReady();
    SetScriptCameraCurrent(nullptr);
    Put(image + offsets::CUTSCENE_STATE, std::int32_t{0}); Put(image + offsets::EVENT_CONTEXT, uintptr_t{0});
    Check(ready && menu && progress && camera && warpCalls == calls,
          "issue readiness: an arrived quiet field is ready; an open menu, progress not yet whole or an owned script camera is not, and nothing issues");

    // The EnemySync glue: the relay's replay through the real RoomTransition admission, with this seat's request sent.
    const auto armed = [&] {
        const auto n = arrive();
        es::g_cloneRebuild = {}; es::g_cloneRebuild.phase = kh2coop::inject::playerkit::RebuildPhase::Awaiting;
        es::g_cloneRebuild.id = es::CloneRebuildIdentity(); es::g_cloneRebuild.seq = 1; es::g_cloneRebuild.attempts = 1;
        return n;
    };
    calls = armed();
    QueueHostWorld(encode(arrivalTarget)); es::ReceiveWorldPackets();
    Check(warpCalls == calls + 1 && w::g_transitionPending && w::g_hostCause.cloneRebuild &&
          es::g_cloneRebuild.phase == kh2coop::inject::playerkit::RebuildPhase::Idle && es::g_host.epoch == 9 && !es::g_host.arrived,
          "glue: the replay of this seat's request is issued natively at once and consumed; the seat now awaits its reload");
    calls = armed();
    Put(image + offsets::BATTLE_STATUS, std::int32_t{2});
    QueueHostWorld(encode(arrivalTarget)); es::ReceiveWorldPackets();
    for (int i = 0; i < 3; ++i) w::OnFrameStart(500 + i, player);
    Check(warpCalls == calls && !w::g_hostQueued && w::MatchesArrivedHostTransition(arrivalTarget) &&
          es::g_cloneRebuild.phase == kh2coop::inject::playerkit::RebuildPhase::AckTrue && es::g_host.epoch == 9 && es::g_host.arrived,
          "glue: a replay arriving in battle is refused, the arrived room untouched, and arrived=true is owed");
    Put(image + offsets::BATTLE_STATUS, std::int32_t{0});
    es::g_cloneRebuild = {};
}
void TestIntervalSeal() {
    Start();
    w::OnFrameStart(100, player);
    Check(seals.size() == 1 && seals[0].find("seq=0 complete=1") != std::string::npos,
          "first observed frame emits zero-event interval watermark");
    w::OnFrameStart(100,player); w::OnFrameStart(219,player);
    Check(seals.size() == 1 && w::g_loadEvidenceSequence == 0 && warpCalls == 0,
          "same/119 elapsed frames add no seal, event or native call");
    w::OnFrameStart(220,player);
    Check(seals.size() == 2 && warpCalls == 0, "120 elapsed frames emit next seal without scheduling");
    auto context = Context(); auto scope = Scope();
    w::QueueHostTransition(arrivalTarget,context,&scope,nullptr, nullptr);
    w::OnFrameStart(340,player);
    Check(seals.size() == 3 && seals.back().find("seq=1 complete=1") != std::string::npos &&
          seals.back().find("queued=1 issued=0 pending=0") != std::string::npos && warpCalls == 1 &&
          w::g_loadEvidenceSequence == 2, "seal snapshots preceding queue event then existing frame path issues exactly once");
    w::OnFrameStart(460,player);
    Check(seals.back().find("seq=2 complete=1") != std::string::npos && warpCalls == 1,
          "pending seal captures issue highwater without reissuing");
    w::CompleteLoad(); w::HostTransitionArrived(9); w::OnFrameStart(580,player);
    Check(seals.back().find("seq=4 complete=1") != std::string::npos &&
          seals.back().find("load=12 transition=8") != std::string::npos &&
          seals.back().find("scopeCurrent=1") != std::string::npos && warpCalls == 1,
          "post-arrival seal exposes all four actual rows and completed native scope");
    ++g_bridge.generation; w::OnFrameStart(700,player);
    Check(seals.back().find("scopeCurrent=0") != std::string::npos, "seal does not present drifted capture as current");
    w::g_loadEvidenceSequence = UINT64_MAX; w::OnFrameStart(820,player);
    Check(seals.back().find("complete=0") != std::string::npos && warpCalls == 1,
          "overflow interval seal invalidates completeness without native work");
    Start(); w::OnFrameStart(0,player); w::OnFrameStart(0,player); w::OnFrameStart(1,player);
    Check(seals.size() == 1, "frame-zero first seal must not bypass120-frame cadence");
}
}

int main() try {
    std::cout << std::unitbuf;
    image = reinterpret_cast<uintptr_t>(VirtualAlloc(nullptr,0x3000000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if (!image) return 2;
    TestOrdinaryAndBootstrap(); TestBlockedNativeExitMatch(); TestBlockedExitToPrologueSite(); TestWaitingAtBlockedExit(); TestRetireSupersedeAndDrift(); TestInvalidDiagnosticJoins(); TestIntervalSeal(); TestReplayDuringEvent(); TestCloneRebuildIssue();
    Check(hookCalls == 0, "no hook installation occurs in owned-memory controls");
    VirtualFree(reinterpret_cast<void*>(image),0,MEM_RELEASE);
    std::cout << "ERRORS=" << errors << '\n';
    return errors ? 1 : 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
