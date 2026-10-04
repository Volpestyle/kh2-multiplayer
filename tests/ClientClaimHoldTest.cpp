#define main OriginalNativeHitHarnessMain
#include "NativeHitClaimTest.cpp"
#undef main
#include <string>

namespace {
std::vector<std::string> claimRows;
bool changeHpAtMatch = false;
void ClaimLog(const char* format, ...) {
    char line[4096] {};
    va_list args; va_start(args, format); vsnprintf(line, sizeof(line), format, args); va_end(args);
    claimRows.emplace_back(line);
    if (changeHpAtMatch && claimRows.back().find("action=match-begin") != std::string::npos)
        Put(status, std::int32_t{999});
}
void HoldSetup(bool replace = true) {
    Reset(true); g_clientClaimHold = {}; g_clientManifestRevision = 0; g_activationRecovery.reset();
    changeHpAtMatch = false;
    claimRows.clear(); g_log = ClaimLog;
    QueueHpManifest(9, replace); QueueHostWorld(encode(OrderedHp(1, 1000))); ReceiveWorldPackets();
    g_bridge.outgoing.clear();
}
bool HasReceipt(const char* part) {
    return std::any_of(claimRows.begin(), claimRows.end(), [&](const auto& row) { return row.find(part) != std::string::npos; });
}
}
int main() try {
    std::cout << std::unitbuf;
    image = reinterpret_cast<uintptr_t>(VirtualAlloc(nullptr, 0x3000000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!image) return 2;
    HoldSetup();
    Check(g_host.manifestComplete && g_host.enemies[1].hpKnown && g_host.enemies[1].maxHp == 1000,
          "actual framed replace manifest and HP retain complete knowledge and maximum HP");
    Check(!RecordLocalPlayerEnemyHit(LocalHit()) && g_bridge.outgoing.empty() && g_localClaimSequence == 0,
          "generic pre-plan hold drops claim without queue or sequence mutation");
    Check(ReleaseClientClaims(31) && !g_clientClaimHold.held, "complete natural census HP readback releases without replay object");
    Check(HasReceipt("action=release") && HasReceipt("hpObserved=1000 maxHpObserved=1000") && HasReceipt("objectType=4") &&
          HasReceipt("transactionAvailable=0 session=unavailable"), "release has typed raw-row witness and honest missing transaction identity");
    Check(RecordLocalPlayerEnemyHit(LocalHit()) && g_bridge.outgoing.size() == 1 && HasReceipt("action=submitted"),
          "qualified post-release claim reaches production encoder and world enqueue");
    Check(HasReceipt("claimAvailable=1 claimSeq=1 claimNetId=1") && HasReceipt("matchComplete=1 missing=0 extra=0 conflicts=0"),
          "typed claim receipt binds actual claim identity and complete release counts");
    const auto oldId = g_clientClaimHold.id;
    ++g_bridge.generation;
    Check(!RecordLocalPlayerEnemyHit(LocalHit()) && g_clientClaimHold.held && g_bridge.outgoing.size() == 1 &&
          g_activationOrderedGeneration == 0, "generation header retires old release before queued ordered reset is consumed");
    Check(!ReleaseClientClaims(32), "old full census cannot release missing ordered scope");
    Check(g_clientClaimHold.id == oldId, "unavailable scope does not mint a false admitted incarnation");

    HoldSetup(); Check(ReleaseClientClaims(32), "plan boundary setup released");
    auto claimPlan = NativePlan(); claimPlan.stage = ResyncPlanStage::Fenced;
    ReceiveResyncPlan(claimPlan);
    Check(g_resyncPlan && g_clientClaimHold.held && !ReleaseClientClaims(32) &&
          !RecordLocalPlayerEnemyHit(LocalHit()) && g_bridge.outgoing.empty(),
          "admitted new plan holds before ordered reset or native load can arrive");
    HoldSetup(); g_resyncWriteFence = ResyncWriteFence::Failed;
    Check(!ReleaseClientClaims(32), "failed resync fence cannot release an old full census");
    HoldSetup(false);
    Check(!g_host.manifestComplete && !ReleaseClientClaims(33), "append plus HP cannot invent complete manifest universe");
    HoldSetup(); g_host.enemies[1].hpKnown = false;
    Check(!ReleaseClientClaims(34), "unknown HP remains held");
    HoldSetup(); g_host.enemies[1].maxHp = 999;
    Check(!ReleaseClientClaims(35), "maximum HP mismatch remains held");
    HoldSetup(); Put(status, 999);
    Check(!ReleaseClientClaims(36), "native HP mismatch cannot release based on desired values");
    Check(ClientFrame(CaptureNativeCensus()) && Read<int>(status) == 1000 && ReleaseClientClaims(37),
          "one release predicate accepts actual readback after existing ClientFrame HP store");
    HoldSetup(); Put(player + offsets::actor::LINKED_NEXT_HANDLE, std::uint32_t{0});
    Check(!ReleaseClientClaims(38), "incomplete canonical linked census remains held");
    HoldSetup(); g_inst.spawns.push_back(g_inst.spawns.front());
    Check(!ReleaseClientClaims(39), "duplicate current netId binding remains held");
    HoldSetup(); auto extra = g_host.enemies[1]; extra.spawnIndex = 1; g_host.enemies[2] = extra;
    Check(!ReleaseClientClaims(40), "missing living manifest member cannot pass subset match");
    HoldSetup(); g_host.enemies.clear();
    Check(!ReleaseClientClaims(41), "empty manifest is not vacuous release");
    HoldSetup(); g_host.enemies[1].dead = true;
    Check(!ReleaseClientClaims(42), "native living extra to admitted living universe remains held");
    HoldSetup(); g_inst.spawns[0].status += 16;
    Check(!ReleaseClientClaims(43), "changed binding metadata cannot release");
    HoldSetup(); Check(ReleaseClientClaims(44), "load rearm setup released"); ++load;
    Check(!RecordLocalPlayerEnemyHit(LocalHit()) && g_clientClaimHold.held && g_bridge.outgoing.empty(),
          "native load change re-arms before claim publication");
    HoldSetup(); Check(ReleaseClientClaims(45), "transition rearm setup released"); ++transition;
    Check(!RecordLocalPlayerEnemyHit(LocalHit()) && g_clientClaimHold.held, "transition change re-arms without generation change");
    HoldSetup(); Check(ReleaseClientClaims(46), "delivery rearm setup released"); ++g_bridge.deliverySerial;
    Check(!RecordLocalPlayerEnemyHit(LocalHit()) && g_clientClaimHold.held, "delivery change holds without ordered marker");
    HoldSetup(); Check(ReleaseClientClaims(47), "roster rearm setup released"); ++g_bridge.connections[2];
    Check(!RecordLocalPlayerEnemyHit(LocalHit()) && g_clientClaimHold.held, "surviving peer replacement changes release scope");
    HoldSetup(); g_bridge.replaceGenerationOnRead = g_bridge.generationReads + 9;
    Check(!ReleaseClientClaims(48) && g_clientClaimHold.held, "generation change during readback cannot clear hold");
    HoldSetup(); changeHpAtMatch = true;
    Check(!ReleaseClientClaims(48) && g_clientClaimHold.held, "HP mutation after witness rows begins rejects final readback bookend");
    HoldSetup(); Check(ReleaseClientClaims(49), "HP revision setup released");
    QueueHostWorld(encode(OrderedHp(2, 900))); ReceiveWorldPackets();
    Check(!RecordLocalPlayerEnemyHit(LocalHit()) && g_clientClaimHold.held, "changed admitted HP re-arms before next claim");
    Check(ClientFrame(CaptureNativeCensus()) && ReleaseClientClaims(50), "fresh stable changed HP readback releases");
    HoldSetup(); g_activationRecovery.emplace(); g_activationRecovery->reconciled = false;
    Check(!ReleaseClientClaims(51), "replay cannot use generic release before exact reconciliation");
    g_activationRecovery->phase = ActivationRecoveryPhase::LiveHold; g_activationRecovery->reconciled = true;
    Check(ReleaseClientClaims(52), "reconciled replay uses same complete generic manifest predicate");
    HoldSetup(); g_activationRecovery.emplace(); g_activationRecovery->reconciled = true;
    g_activationRecovery->phase = ActivationRecoveryPhase::Failed;
    Check(!ReleaseClientClaims(52), "failed replay cannot inherit generic release from prior reconciliation");
    HoldSetup(); g_clientClaimHold.sequence = 4096;
    (void)EnsureClientClaimScope(); LogClientClaim("seal", "test-final");
    Check(g_clientClaimHold.receiptGaps == 1 && HasReceipt("receiptGaps=1") && HasReceipt("action=seal"),
          "bounded receipt suppression is disclosed by final seal");
    HoldSetup(); g_clientClaimHold.id = UINT64_MAX;
    Check(!ReleaseClientClaims(53) && g_clientClaimHold.poisoned, "hold identity exhaustion refuses reuse");
    HoldSetup(); g_host.manifestComplete = false;
    auto malformed = encode(EnemyManifest{9, true, {{1, 6, 0, 309, {}}}}); malformed.push_back(0xA5);
    QueueHostWorld(malformed); ReceiveWorldPackets();
    Check(!g_host.manifestComplete && !ReleaseClientClaims(54), "malformed replace cannot grant complete manifest knowledge");
    Check(nativeCalls == 0 && warpCalls == 0 && hookCalls == 0,
          "claim hold controls issue no native damage/warp/hook calls");
    ResetWorld();
    namespace w = kh2coop::inject::recoverywarp;
    w::g_hostCause = {}; w::g_log = ClaimLog;
    auto nextRoom = arrivalTarget; ++nextRoom.epoch;
    QueueHostWorld(encode(nextRoom)); ReceiveWorldPackets();
    Check(w::g_hostCause.available && w::g_hostCause.scope && !w::g_hostCause.begin &&
          w::g_hostCause.context.hostSourceSerial == 0 && w::g_hostCause.scope->hostSourceSerial != 0 &&
          w::g_hostQueued && warpCalls == 0,
          "actual ordinary RoomTransition caller passes admitted source context to root Warp overload without issuing native work");
    VirtualFree(reinterpret_cast<void*>(image), 0, MEM_RELEASE);
    std::cout << "GENERIC HOLD FAILURES=" << errors << '\n';
    return errors ? 1 : 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 3; }
