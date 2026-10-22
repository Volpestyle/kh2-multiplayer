// ============================================================================
// DownedSpike.inl — VUH-1504 downed/revive spike, native adapter.
//
// Included inside EntityHook's namespace, immediately before
// HookedApplyStatDelta. Uses EntityHook's statics defined above that point:
// Log, g_exeBase, g_soraActor, g_frameCounter, ReadHitTrace,
// PFN_ApplyStatDelta and g_origApplyStatDelta. Pure rules and the fixture
// channel layout are in DownedSpikeState.hpp.
//
// Default off: nothing is hooked, mapped, read or logged unless the process
// was started with KH2COOP_DOWNED_SPIKE=1. When on:
//   - the game-over requests 0x3FCD20(actor) and 0x3FCAD0(actor) are skipped
//     for the canonical local player after the native death has already run
//     (dead flag, controller off, ACTION_DEADSORA), so Sora stays downed;
//   - every frame the owner publishes LocalDownedState through AvatarBridge,
//     and an admitted ReviveRequest (enemysync) runs the native revive
//     0x3AA8D0 with the one +max heal rewritten to 25% max, a ~2 s native
//     ignore-hit grace and an idle stand-up motion.
// KH2COOP_DOWNED_SPIKE is the feature flag. The name keeps "spike" so the
// live-proven fixtures and evidence stay valid.
// Test-only, behind KH2COOP_DOWNED_SPIKE_FIXTURE=1: a shared-memory channel
// accepting Kill (native ApplyStatDelta to 0 HP), Revive (debug TryRevive)
// and RequestRevive (debug requester), and publishing per-frame readouts.
// KH2COOP_DOWNED_SPIKE_CONTROL=1 (implies the channel) keeps only the channel
// and Kill: no gate or revive hook, so the native game over runs.
// No HP write other than through the native funnel, no saves.
// See docs/DOWNED_REVIVE.md.
// ============================================================================

namespace downedspike {

constexpr uint64_t RVA_GAMEOVER_REQ0 = 0x3FCD20;   // mode 0 request, death handlers only
constexpr uint64_t RVA_GAMEOVER_REQ3 = 0x3FCAD0;   // mode 3 request (actor from death, 0 from mission fail)
constexpr uint64_t RVA_REVIVE = 0x3AA8D0;          // native player revive (reraise callback target)
constexpr uint64_t RVA_RESOLVE = 0x4AD270;         // handle -> object
constexpr uint64_t RVA_APPLY_STAT = 0x3D2EB0;      // ApplyStatDelta (hooked entry)
constexpr uint64_t RVA_GAMEOVER_TASK = 0x2AE8050;  // active game-over task pointer
constexpr uint64_t RVA_PLAYER = 0x2A105D0;         // canonical native player
constexpr uint64_t RVA_ACTION_TABLE = 0x750D30;    // action singletons by id
constexpr uint64_t RVA_ACTION_DEADSORA = 0x750BA8; // action 0x29
constexpr uint64_t RVA_VT_DEADSORA = 0x5CDF10;     // YS::ACTION_DEADSORA vftable
constexpr uint32_t ACTION_DEAD = 0x29;
constexpr uint64_t RVA_SET_INVULN = 0x3D7E40;     // movss [rcx+0xD70], xmm1; ret
constexpr uint64_t RVA_SET_MOTION = 0x3C86A0;     // (motCtrl=actor+0x158, id, float, float): 0x405A00 passes 0x36, XMM2=XMM3=0
constexpr uint64_t RVA_SYS_FLAGS = 0x2A11400;      // g_sys400; bit 17 = death handlers skip the request
constexpr uint64_t RVA_SUMMON_STATE = 0x2A24EDC;   // nonzero = summon active (death dismisses / reraise)
constexpr uint64_t RVA_NOW = 0x717008;             // world u8, room u8, door u8, -, map/btl/evt u16 at +4/+6/+8

constexpr uint8_t kReq0Bytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x83,
                                  0x3D, 0x22, 0xB3, 0x6E, 0x02, 0x00, 0x48, 0x8B};
constexpr uint8_t kReq3Bytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
                                  0xD9, 0xB9, 0x88, 0x00, 0x00, 0x00};
constexpr uint8_t kReviveBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
                                    0xEC, 0x30, 0x83, 0xA1, 0xB8, 0x09, 0x00, 0x00, 0xFB};
constexpr uint8_t kSetMotionBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
                                       0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x0F};
constexpr uint8_t kSetInvulnBytes[] = {0xF3, 0x0F, 0x11, 0x89, 0x70, 0x0D, 0x00, 0x00, 0xC3};
constexpr uint8_t kResolveBytes[] = {0x85, 0xC9, 0x75, 0x03, 0x33, 0xC0, 0xC3, 0xE9,
                                     0x74, 0x01, 0x00, 0x00};

// The death handlers reach both requests by tail jmp (mov rcx,rbx; add rsp,20h;
// pop rbx; jmp), so RAX becomes the handler's return value. Their only static
// consumer, the vtbl+0xB0 call in 0x3D2EB0/0x3D2C50, discards it (mov eax,edi
// follows). Natively RAX is the task pointer, 0 on allocation failure, or
// unchanged when a task already exists; a skip returns 0 (the no-task path).
using PFN_Request = uintptr_t(__fastcall*)(uintptr_t actor);
using PFN_Revive = void(__fastcall*)(uintptr_t actor);
using PFN_Resolve = uintptr_t(__fastcall*)(uint32_t handle);
using PFN_SetInvuln = void(__fastcall*)(uintptr_t actor, float frames);
// Exact callee ABI (ghidra/setmotion-disasm.txt): RCX motCtrl, EDX id, XMM2/XMM3 floats
// (saved to XMM7/XMM6 and forwarded to 0x3C88C0). The death caller 0x405A00 zeroes
// XMM2/XMM3 (xorps) before calling with EDX=0x36; R8/R9 are not read.
using PFN_SetMotion = void(__fastcall*)(uintptr_t motCtrl, int id, float startTime, float blend);

struct Guard {
    bool active = false, used = false;
    uintptr_t actor = 0;
    int target = 0, maxHp = 0;
};
struct Room {
    uint8_t world = 0, room = 0, door = 0;
    uint16_t map = 0, battle = 0, event = 0;
    bool ok = false;
    bool operator==(const Room& o) const {
        return ok && o.ok && world == o.world && room == o.room && door == o.door && map == o.map &&
            battle == o.battle && event == o.event;
    }
};
struct Snap {
    bool canonical = false, inEvent = true;
    Room room {};
    uint32_t handle0 = 0;    // actor+0 own handle (resolves the handler)
    uintptr_t objentry = 0;
    float pos[3] {}, velX = 0.0f, velZ = 0.0f, motionTime = 0.0f, stickX = 0.0f, stickY = 0.0f;
    uint32_t motionId = 0;
    uintptr_t actor = 0, ctrl = 0, aux = 0, status = 0, task = 0;
    int hp = 0, maxHp = 0;
    uint32_t flags = 0;
    bool deadAction = false, controllerOff = false;
};
struct SpikeState {
    bool requested = false, control = false;
    State state = State::Off;
    DWORD thread = 0;
    uint32_t installMask = 0;
    PFN_Request origReq0 = nullptr, origReq3 = nullptr;
    PFN_Revive revive = nullptr;
    PFN_Resolve resolve = nullptr;
    PFN_SetInvuln setInvuln = nullptr;
    PFN_SetMotion setMotion = nullptr;
    uintptr_t graceActor = 0;
    int graceLastHp = 0;
    uint32_t graceHits = 0, graceEnemyHits = 0, graceHpDrops = 0;
    float invulnTimer = 0.0f;
    HANDLE mapping = nullptr;
    Channel* ch = nullptr;
    uintptr_t downedActor = 0;
    int downedHp = 0;
    Room downedRoom {};
    uint32_t downedHandle0 = 0;
    uintptr_t downedObjentry = 0;
    uint64_t episode = 0; // FirstEpisodeBase(boot nonce) + n; base alone = never downed
    uint32_t episodeFrames = 0, episodeRemints = 0;
    uint32_t hitAttempts = 0, enemyHits = 0, hitLogs = 0, holdLost = 0;
    uint32_t inputFrames = 0, noInputFrames = 0;
    float driftInput = 0.0f, driftNoInput = 0.0f, lastPos[3] {};
    bool lastPosValid = false;
    PublishKind published = PublishKind::None;
    uint32_t downedFrame = 0, gateLogs = 0, passLogs = 0;
    Guard guard {};
    // mirrored into the channel
    uint32_t gateCount = 0, passCount = 0, killCount = 0, reviveCount = 0;
    uint32_t downedFrames = 0, actionLost = 0, deadFlagLost = 0, controllerOn = 0, statCalls = 0;
    int32_t hpChanges = 0, reviveTarget = 0, reviveHpAfter = 0;
    uint32_t reviveGuardUsed = 0, lastGateMode = 0, lastPassReason = 0;
    // VUH-2465 logical downing (see LogicalScope): engine HP 1, dead bit clear.
    bool logical = false, reviving = false, resolvePending = false, fullPartyDown = false;
    bool sitOutPending = false; // VUH-2632 (D17): re-down in the next room, still sitting out
    // VUH-2640 (F1): the link expired in a boss fight and this seat was not downed yet: the outage's
    // downing is pending in the room of the expiry (the duty itself is g_outageDuty).
    bool outagePending = false;
    Room outageRoom {};
    uint32_t outageRecoveryAt = 0;   // the recovery Bootstrap serial at that expiry
    uint32_t sitOuts = 0, returnsSent = 0;
    // VUH-2656: this downing's identity (one per BeginDowned) and the host's mission running here then.
    uint64_t downingSerial = 0, downingSession = 0;
    uint32_t downingTransition = 0, downingLoad = 0;
    char encounterFile[24] {};
    bool encounterKnown = false;
    uint32_t hostEndSerial = 0;
    uint32_t allDownFrames = 0, pinWrites = 0, fullPartyLoad = 0;
    float downedPos[3] {};
    uint32_t logicalSkips = 0, healsUndone = 0, motionRequests = 0, motionFrame = 0;
};
static SpikeState g_down;
// VUH-2632 (D17): the teammates the downed player last saw in its room, for the sit-out decision.
static downedreturn::DispositionLatch g_disposition;
static downedreturn::SessionTeammates g_sessionTeammates;
// A return that reached this downing during an event (applied when it ends, or 25% in the next room).
static downedreturn::OwedReturn g_owedReturn;
static bool g_outageExpirySeen = false;       // VUH-2640: this expired lifetime was judged
static downedreturn::OutageDuty g_outageDuty; // VUH-2640: open from the qualified expiry until its positive end
static uint64_t g_lastLiveIdentity = 0;       // VUH-2640: this seat's session while its lifetime was live
static downedreturn::BossReceipt g_bossReceipt {}; // VUH-2640: the last host-owned boss bind, with its scope
static uint32_t g_lastLiveGeneration = 0;
static uint32_t g_sitOutArrivalSeen = 0;      // VUH-2640 Leg A: the last sit-out arrival judged
static uint64_t g_outageMovedEpisode = 0;     // VUH-2640 Leg A trace: the episode already logged as moved
// VUH-2640 Leg A veil (downedreturn::SitOutVeilStep): the qualified arrival it serves and the copy it hid.
struct SitOutVeil {
    enemysync::SitOutArrival arrival {};  // serial 0: none latched
    bool attempted = false, veiled = false;
    uintptr_t actor = 0;
    uint32_t handle = 0, transition = 0, load = 0;  // the hidden copy and the world it was hidden in
};
static SitOutVeil g_veil;
static unsigned g_veilLogs = 0;
// Read by the input collector and the 3D2060 hit check (game thread; atomics for the record).
static std::atomic<bool> g_logicalHeld {false};
static std::atomic<uintptr_t> g_logicalActor {0};

// P3 network contract state (docs/DOWNED_REVIVE.md); see PublishLocalDownedState.
struct NetState {
    kh2coop::LocalDownedState published {}; // this frame's publication (enemysync's gate judges it)
    uint32_t nativeRefused = 0, revivedByRequest = 0;
    uint32_t requestsSent = 0, requestFailures = 0;
    uint64_t lastSentSeq = 0;
    uint8_t localSlot = 0xFF;
};
static NetState g_net;

// Revive prompt state (KH2COOP_REVIVE_PROMPT=1); see PromptTick.
struct PromptRuntime {
    bool requested = false;
    reviveprompt::State state {};
    reviveprompt::Output out {};
    uint32_t triangleFrames = 0, fires = 0, logs = 0, reactLogs = 0, yieldLogs = 0;
    uint16_t reactLib = 0, reactShifted = 0;
    reviveprompt::RcGate rcGate {};
    reviveprompt::Hide lastHide = reviveprompt::Hide::Off;
    bool yielding = false;
    downedpolicy::Difficulty difficulty = static_cast<downedpolicy::Difficulty>(0xFE); // last logged
};
static PromptRuntime g_prompt;
static std::atomic<bool> g_promptTriangle {false};

static bool GraceActive() { return g_down.graceActor != 0 && g_down.invulnTimer > 0.0f; }

// VUH-2465: Roxas (any P_EX110 model, including dual-wield Roxas in the Axel fights) is downed logically,
// never natively. The native dead
// bit (actor+0x9B8 bit 2) stops the room's spawn evaluation, so later waves would never come while a
// player is held natively dead. The lethal stat delta is clamped to leave engine HP 1 with the dead
// bit clear; the episode contract publishes the player as downed with HP 0. While downed the player
// takes no stat change (StatDelta), no hit (3D2060 via AllyHit, and the native ignore-hit timer kept
// up), and no input (HoldInput). Sora keeps the native held-death path above.
// The local player's own model, not the party row: the Axel fight's dual-wield Roxas is another member
// (run 20261019-145329: the prologue-scope test missed it and Axel's hit took the native death).
static bool LogicalScope(uintptr_t objentry) {
    char name[8] {};
    __try { std::memcpy(name, reinterpret_cast<const void*>(objentry + 8), 7); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return std::memcmp(name, "P_EX110", 7) == 0;
}
static bool LogicallyDowned() { return g_down.state == State::Downed && g_down.logical; }
static uintptr_t LogicalDownedActor() { return g_logicalActor.load(std::memory_order_acquire); }

static bool Matches(uintptr_t exeBase, uint64_t rva, const uint8_t* bytes, size_t n) {
    __try {
        return std::memcmp(reinterpret_cast<const void*>(exeBase + rva), bytes, n) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static bool OwnerThread() { return g_down.thread != 0 && GetCurrentThreadId() == g_down.thread; }

static bool Canonical(uintptr_t actor) {
    uintptr_t player = 0, head = 0;
    return actor != 0 && actor == g_soraActor &&
        ReadHitTrace(g_exeBase + RVA_PLAYER, player) && player == actor &&
        ReadHitTrace(g_exeBase + offsets::active_entity_list::HEAD, head) && head == actor;
}

static bool ResolveIsDeadAction(uintptr_t actor) {
    uint32_t handle = 0;
    if (!g_down.resolve || !ReadHitTrace(actor + 0xC, handle)) return false;
    __try {
        return g_down.resolve(handle) == g_exeBase + RVA_ACTION_DEADSORA;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static Room ReadRoom() {
    Room r {};
    r.ok = ReadHitTrace(g_exeBase + RVA_NOW, r.world) && ReadHitTrace(g_exeBase + RVA_NOW + 1, r.room) &&
        ReadHitTrace(g_exeBase + RVA_NOW + 2, r.door) && ReadHitTrace(g_exeBase + RVA_NOW + 4, r.map) &&
        ReadHitTrace(g_exeBase + RVA_NOW + 6, r.battle) && ReadHitTrace(g_exeBase + RVA_NOW + 8, r.event);
    return r;
}

static Snap Capture(uintptr_t actor) {
    Snap s {};
    s.actor = actor;
    s.canonical = Canonical(actor);
    s.room = ReadRoom();
    int32_t cutscene = 1;
    uintptr_t eventContext = 1;
    s.inEvent = !(ReadHitTrace(g_exeBase + offsets::CUTSCENE_STATE, cutscene) && cutscene == 0 &&
                  ReadHitTrace(g_exeBase + offsets::EVENT_CONTEXT, eventContext) && eventContext == 0);
    ReadHitTrace(g_exeBase + RVA_GAMEOVER_TASK, s.task);
    ReadHitTrace(g_exeBase + offsets::input::PROCESSED_ENTRY0 + 0x30, s.stickX);
    ReadHitTrace(g_exeBase + offsets::input::PROCESSED_ENTRY0 + 0x34, s.stickY);
    if (!actor) return s;
    const uintptr_t pos = actor + offsets::actor::ENTITY_TRANSFORM + offsets::entity::POS_X;
    ReadHitTrace(pos, s.pos[0]);
    ReadHitTrace(pos + 4, s.pos[1]);
    ReadHitTrace(pos + 8, s.pos[2]);
    ReadHitTrace(actor + 0xB98, s.velX);
    ReadHitTrace(actor + 0xBA0, s.velZ);
    ReadHitTrace(actor + offsets::actor::ANIM_ID, s.motionId);
    ReadHitTrace(actor + 0x19C, s.motionTime);
    ReadHitTrace(actor + 0x9B8, s.flags);
    ReadHitTrace(actor, s.handle0);
    ReadHitTrace(actor + offsets::actor::OBJENTRY_PTR, s.objentry);
    ReadHitTrace(actor + 0xDC0, s.ctrl);
    ReadHitTrace(actor + 0xDD0, s.aux);
    // VUH-2464: follow pointers only from a player-class head. An event's list head is not one: its
    // +0x5C0 held float data (0xBF365525) at the Tram Common Armor Shop event, and the guarded read's
    // first-chance AV took the game down through the CLR's vectored handler (runs 175014, 182550).
    std::uint8_t type = 0xFF;
    const bool player = s.objentry > g_exeBase && s.objentry < g_exeBase + 0x3000000 &&
        ReadHitTrace(s.objentry + offsets::objentry::TYPE_FLAGS, type) && type == 0;
    constexpr uintptr_t STATUS_POOL = 0x2A17290, STATUS_STRIDE = 0x278, STATUS_RECORDS = 80;
    if (player && ReadHitTrace(actor + 0x5C0, s.status) && s.status >= g_exeBase + STATUS_POOL &&
        s.status < g_exeBase + STATUS_POOL + STATUS_STRIDE * STATUS_RECORDS &&
        (s.status - g_exeBase - STATUS_POOL) % STATUS_STRIDE == 0) {
        ReadHitTrace(s.status, s.hp);
        ReadHitTrace(s.status + 4, s.maxHp);
    } else s.status = 0;
    uint32_t c24 = 0;
    s.controllerOff = player && s.ctrl && ReadHitTrace(s.ctrl + 0x24, c24) && (c24 & 1U) != 0;
    s.deadAction = player && ResolveIsDeadAction(actor);
    return s;
}

static void Publish(const Snap& s) {
    Channel* c = g_down.ch;
    if (!c) return;
    c->state = static_cast<uint32_t>(g_down.state);
    c->actor = s.actor;
    c->hp = s.hp;
    c->maxHp = s.maxHp;
    c->flags9B8 = s.flags;
    c->deadAction = s.deadAction ? 1U : 0U;
    c->controllerOff = s.controllerOff ? 1U : 0U;
    c->publishKind = static_cast<uint32_t>(g_down.published);
    c->episode = g_down.episode;
    c->gameOverTask = s.task;
    c->gateCount = g_down.gateCount;
    c->passCount = g_down.passCount;
    c->killCount = g_down.killCount;
    c->reviveCount = g_down.reviveCount;
    c->downedFrame = g_down.downedFrame;
    c->downedFrames = g_down.downedFrames;
    c->actionLostFrames = g_down.actionLost;
    c->deadFlagLostFrames = g_down.deadFlagLost;
    c->controllerOnFrames = g_down.controllerOn;
    c->statCallsWhileDowned = g_down.statCalls;
    c->hpChangesWhileDowned = g_down.hpChanges;
    c->reviveTarget = g_down.reviveTarget;
    c->reviveHpAfter = g_down.reviveHpAfter;
    c->reviveGuardUsed = g_down.reviveGuardUsed;
    c->installMask = g_down.installMask;
    c->lastGateMode = g_down.lastGateMode;
    c->lastPassReason = g_down.lastPassReason;
    c->hitAttemptsWhileDowned = g_down.hitAttempts;
    c->enemyHitAttemptsWhileDowned = g_down.enemyHits;
    c->holdLostReason = g_down.holdLost;
    c->posX = s.pos[0];
    c->posY = s.pos[1];
    c->posZ = s.pos[2];
    c->velX = s.velX;
    c->velZ = s.velZ;
    c->motionId = s.motionId;
    c->motionTime = s.motionTime;
    c->stickX = s.stickX;
    c->stickY = s.stickY;
    c->inputActive = StickActive(s.stickX, s.stickY) ? 1U : 0U;
    c->inputFramesWhileDowned = g_down.inputFrames;
    c->noInputFramesWhileDowned = g_down.noInputFrames;
    c->driftWithInput = g_down.driftInput;
    c->driftWithoutInput = g_down.driftNoInput;
    c->invulnTimer = g_down.invulnTimer;
    c->graceHits = g_down.graceHits;
    c->graceEnemyHits = g_down.graceEnemyHits;
    c->graceHpDrops = g_down.graceHpDrops;
    c->graceActive = GraceActive() ? 1U : 0U;
    c->netPublished = g_net.published.sampledAtMs ? 1U : 0U;
    c->netEpoch = g_net.published.epoch;
    const auto rs = enemysync::GetReviveStats();
    c->reviveSeen = static_cast<uint32_t>(rs.seen);
    c->reviveGateRefused = static_cast<uint32_t>(rs.gateRefused + rs.admissionRefused);
    c->reviveConsumed = static_cast<uint32_t>(rs.consumed);
    c->reviveNativeRefused = g_net.nativeRefused;
    c->revivedByRequest = g_net.revivedByRequest;
    c->requestsSent = g_net.requestsSent;
    c->requestFailures = g_net.requestFailures;
    for (int k = 0; k < 2; ++k) {
        const auto& d = g_puppets[k];
        c->puppetSlot[k] = d.have ? static_cast<uint32_t>(d.pose.pose.ownerSlot) : 0xFFu;
        c->puppetDowned[k] = d.have && (d.pose.pose.flags & kh2coop::AvatarDowned) ? 1U : 0U;
        c->puppetEpisode[k] = d.have ? d.pose.pose.downedEpisode : 0;
    }
    c->lastSentSeq = g_net.lastSentSeq;
    c->localSlot = g_net.localSlot;
    c->episodeRemints = g_down.episodeRemints;
    c->episodeFrames = g_down.episodeFrames;
    c->promptKind = static_cast<uint32_t>(g_prompt.out.kind);
    c->promptSlot = g_prompt.out.slot;
    c->promptProgress = g_prompt.out.progress;
    c->promptHide = static_cast<uint32_t>(g_prompt.out.hide);
    c->triangleFrames = g_prompt.triangleFrames;
    c->promptFires = g_prompt.fires;
    c->reactCmdLib = g_prompt.reactLib;
    c->reactCmdShifted = g_prompt.reactShifted;
    InterlockedExchange(&c->liveFrame, static_cast<long>(g_frameCounter));
}

// ---------------------------------------------------------------------------
// Game-over request detours (game thread when they matter)
// ---------------------------------------------------------------------------
static void BeginDowned(uintptr_t actor, int hp, uint32_t mode);

static bool Intercept(uintptr_t actor, uint32_t mode) {
    if (g_down.fullPartyDown) {
        // VUH-2465: every player is down; the host's own game over and Continue are the retry.
        Log("[downed] gate pass mode=%u full party down: native game over frame=%u", mode, g_frameCounter);
        return false;
    }
    GateFacts f {};
    f.enabled = !g_down.control && g_down.state != State::Off && g_down.state != State::Refused;
    f.ownerThread = OwnerThread();
    f.actorTracked = actor != 0 && actor == g_soraActor;
    uintptr_t player = 0, head = 0, task = 1;
    uint32_t flags = 0;
    f.actorIsPlayer = actor != 0 && ReadHitTrace(g_exeBase + RVA_PLAYER, player) && player == actor;
    f.actorIsHead = actor != 0 && ReadHitTrace(g_exeBase + offsets::active_entity_list::HEAD, head) && head == actor;
    f.stateReady = g_down.state == State::Ready;
    f.deadFlag = actor != 0 && ReadHitTrace(actor + 0x9B8, flags) && (flags & 4U) != 0;
    f.taskIdle = ReadHitTrace(g_exeBase + RVA_GAMEOVER_TASK, task) && task == 0;
    const GateReason reason = actor == 0 ? GateReason::NoActor : Decide(f);
    if (reason != GateReason::Intercept) {
        ++g_down.passCount;
        g_down.lastPassReason = static_cast<uint32_t>(reason);
        if (g_down.passLogs < 16) {
            ++g_down.passLogs;
            Log("[downed] gate pass mode=%u actor=%llX reason=%s frame=%u", mode,
                static_cast<unsigned long long>(actor), GateReasonName(reason), g_frameCounter);
        }
        return false;
    }
    int hp = -1;
    uintptr_t status = 0;
    if (ReadHitTrace(actor + 0x5C0, status) && status) ReadHitTrace(status, hp);
    BeginDowned(actor, hp, mode);
    if (g_down.gateLogs < 16) {
        ++g_down.gateLogs;
        Log("[downed] gate intercept mode=%u episode=%llX actor=%llX hp=%d flags9B8=0x%X room=%u/%u/%u/%u/%u/%u frame=%u",
            mode, static_cast<unsigned long long>(g_down.episode), static_cast<unsigned long long>(actor), hp, flags,
            g_down.downedRoom.world, g_down.downedRoom.room, g_down.downedRoom.door, g_down.downedRoom.map,
            g_down.downedRoom.battle, g_down.downedRoom.event, g_frameCounter);
    }
    return true;
}

// One new downing: a fresh episode and cleared per-episode counters (both paths).
static void BeginDowned(uintptr_t actor, int hp, uint32_t mode) {
    g_down.state = State::Downed;
    g_down.downedActor = actor;
    g_down.downedHp = hp;
    g_down.downedFrame = g_frameCounter;
    g_down.downedRoom = ReadRoom();
    g_down.downedHandle0 = 0;
    g_down.downedObjentry = 0;
    ReadHitTrace(actor, g_down.downedHandle0);
    ReadHitTrace(actor + offsets::actor::OBJENTRY_PTR, g_down.downedObjentry);
    g_down.episode = NextEpisode(g_down.episode); // P3 boundary: new per episode, never reused
    g_disposition.Reset();
    g_owedReturn.Clear();
    ++g_down.downingSerial;
    g_down.downingTransition = warp::TransitionSerial();
    g_down.downingLoad = warp::LoadSerial();
    if (!enemysync::SessionIdentity(g_down.downingSession)) g_down.downingSession = 0;
    {
        // VUH-2656/2660: the encounter this seat goes down in: the host's current mission, else its own
        // native mission (downed before the host's first MissionState for it), else unknown.
        char hostFile[24] {}, localFile[24] {};
        const bool client = enemysync::ActivationRole() == 2;
        const bool hostCurrent = client && enemysync::CurrentHostMission(hostFile);
        const bool localReadable = client && !hostCurrent && enemysync::LocalNativeMission(localFile);
        const auto source = downedreturn::EncounterAtDowning(hostCurrent, localReadable);
        g_down.encounterKnown = source != downedreturn::EncounterSource::Unknown;
        std::memcpy(g_down.encounterFile, source == downedreturn::EncounterSource::Host ? hostFile : localFile,
                    sizeof(g_down.encounterFile));
        if (client)
            Log("[downed] encounter at downing source=%s file=%s frame=%u", downedreturn::EncounterSourceName(source),
                g_down.encounterKnown ? g_down.encounterFile : "-", g_frameCounter);
    }
    g_down.episodeFrames = 0;
    g_down.hitAttempts = g_down.enemyHits = g_down.hitLogs = g_down.holdLost = 0;
    g_down.inputFrames = g_down.noInputFrames = 0;
    g_down.driftInput = g_down.driftNoInput = 0.0f;
    g_down.lastPosValid = false;
    g_down.downedFrames = g_down.actionLost = g_down.deadFlagLost = g_down.controllerOn = g_down.statCalls = 0;
    g_down.hpChanges = 0;
    g_down.logicalSkips = g_down.healsUndone = g_down.motionRequests = g_down.pinWrites = 0;
    {
        const uintptr_t pos = actor + offsets::actor::ENTITY_TRANSFORM + offsets::entity::POS_X;
        for (int k = 0; k < 3; ++k) ReadHitTrace(pos + 4 * k, g_down.downedPos[k]);
    }
    g_down.motionFrame = g_frameCounter;
    g_down.lastGateMode = mode;
    ++g_down.gateCount;
}

static uintptr_t __fastcall GameOverRequest0(uintptr_t actor) {
    if (Intercept(actor, 0)) return 0;
    return g_down.origReq0(actor);
}
static uintptr_t __fastcall GameOverRequest3(uintptr_t actor) {
    if (actor != 0 && Intercept(actor, 3)) return 0;
    if (actor == 0) {
        ++g_down.passCount;
        g_down.lastPassReason = static_cast<uint32_t>(GateReason::NoActor);
    }
    return g_down.origReq3(actor);
}

// HookedBuildHit, right after the original returns a record. Counts attempted
// hits on the downed actor; invulnerability is proven only if some arrive.
static bool AttackerIsEnemy(void* atk, uintptr_t self, uintptr_t* attackerOut, uint8_t* typeOut);

static void NoteHit(void* atk, void* victim) {
    if (!g_down.requested || !OwnerThread()) return;
    if (g_down.state == State::Ready && GraceActive() && reinterpret_cast<uintptr_t>(victim) == g_down.graceActor) {
        ++g_down.graceHits;
        if (AttackerIsEnemy(atk, g_down.graceActor, nullptr, nullptr)) ++g_down.graceEnemyHits;
        return;
    }
    if (g_down.state != State::Downed) return;
    if (reinterpret_cast<uintptr_t>(victim) != g_down.downedActor) return;
    ++g_down.hitAttempts;
    uint32_t ownerHandle = 0;
    uintptr_t attacker = 0, objentry = 0;
    uint8_t type = 0xFF;
    if (g_down.resolve && ReadHitTrace(reinterpret_cast<uintptr_t>(atk) + 0x10, ownerHandle)) {
        __try { attacker = g_down.resolve(ownerHandle); } __except (EXCEPTION_EXECUTE_HANDLER) { attacker = 0; }
    }
    std::uint32_t objectId=0;
    if (attacker && attacker != g_down.downedActor &&
        ReadHitTrace(attacker + offsets::actor::OBJENTRY_PTR, objentry) && objentry &&
        ReadHitTrace(objentry + offsets::objentry::OBJECT_ID, objectId) &&
        ReadHitTrace(objentry + offsets::objentry::TYPE_FLAGS, type) && enemyclass::Combat(type,objectId))
        ++g_down.enemyHits;
    if (g_down.hitLogs < 8) {
        ++g_down.hitLogs;
        Log("[downed] hit attempt on downed actor attacker=%llX type=%u count=%u enemy=%u frame=%u",
            static_cast<unsigned long long>(attacker), type, g_down.hitAttempts, g_down.enemyHits, g_frameCounter);
    }
}

static bool AttackerIsEnemy(void* atk, uintptr_t self, uintptr_t* attackerOut, uint8_t* typeOut) {
    uint32_t ownerHandle = 0;
    uintptr_t attacker = 0, objentry = 0;
    uint8_t type = 0xFF;
    if (g_down.resolve && ReadHitTrace(reinterpret_cast<uintptr_t>(atk) + 0x10, ownerHandle)) {
        __try { attacker = g_down.resolve(ownerHandle); } __except (EXCEPTION_EXECUTE_HANDLER) { attacker = 0; }
    }
    std::uint32_t objectId=0;
    const bool enemy = attacker && attacker != self &&
        ReadHitTrace(attacker + offsets::actor::OBJENTRY_PTR, objentry) && objentry &&
        ReadHitTrace(objentry + offsets::objentry::OBJECT_ID, objectId) &&
        ReadHitTrace(objentry + offsets::objentry::TYPE_FLAGS, type) && enemyclass::Combat(type,objectId);
    if (attackerOut) *attackerOut = attacker;
    if (typeOut) *typeOut = type;
    return enemy;
}

// ---------------------------------------------------------------------------
// HookedApplyStatDelta, first statement. Rewrites only the guarded revive heal.
// ---------------------------------------------------------------------------
// `skip`: apply nothing and return `skipResult` (the unchanged stat) instead of calling the native body.
static int StatDelta(void* actorPtr, int delta, int idx, bool& skip, int& skipResult) {
    skip = false;
    if (!g_down.requested || !OwnerThread()) return delta;
    const auto actor = reinterpret_cast<uintptr_t>(actorPtr);
    if (g_down.reviving && actor == g_down.downedActor) return delta; // the logical revive heal itself
    if (LogicallyDowned() && actor == g_down.downedActor) {
        // No HP change of any kind while logically downed: damage, a party Cure or an item are not
        // revives, and HP must stay 1 so the native death never runs.
        ++g_down.statCalls;
        if (idx == 0) {
            uintptr_t status = 0;
            int hp = 1;
            if (ReadHitTrace(actor + 0x5C0, status) && status) ReadHitTrace(status, hp);
            skip = true; skipResult = hp;
            ++g_down.logicalSkips;
        }
        return delta;
    }
    if (!g_down.control && g_down.state == State::Ready && idx == 0 && delta < 0 && !g_down.guard.active &&
        actor == g_soraActor) {
        const Snap s = Capture(actor);
        if (!s.status || s.hp + delta > 0 || !s.objentry || !LogicalScope(s.objentry)) return delta; // not lethal, or not Roxas
        // The local player (the native player pointer), with or without an event or another actor at the
        // head of the entity list: a lethal hit that slips through here becomes the native death this
        // path exists to avoid (run 20261019-140845: the friend died natively in the Basement Hall).
        uintptr_t player = 0;
        const bool local = actor == g_soraActor && ReadHitTrace(g_exeBase + RVA_PLAYER, player) && player == actor;
        if (!local || s.hp <= 0 || s.maxHp <= 0 || (s.flags & 4U) || s.task != 0) {
            if (local && g_down.passLogs < 16) {
                ++g_down.passLogs;
                Log("[downed] logical intercept skipped hp=%d/%d flags9B8=0x%X task=%llX frame=%u", s.hp, s.maxHp, s.flags,
                    static_cast<unsigned long long>(s.task), g_frameCounter);
            }
            return delta;
        }
        BeginDowned(actor, 0, 4);
        g_down.logical = true;
        g_logicalActor.store(actor, std::memory_order_release);
        if (g_down.gateLogs < 16) {
            ++g_down.gateLogs;
            Log("[downed] gate intercept mode=4 logical=1 episode=%llX actor=%llX hp=%d delta=%d -> engine hp 1, dead bit clear room=%u/%u/%u/%u/%u/%u frame=%u",
                static_cast<unsigned long long>(g_down.episode), static_cast<unsigned long long>(actor), s.hp, delta,
                g_down.downedRoom.world, g_down.downedRoom.room, g_down.downedRoom.door, g_down.downedRoom.map,
                g_down.downedRoom.battle, g_down.downedRoom.event, g_frameCounter);
        }
        if (g_down.setMotion) {
            bool fault = false;
            __try { g_down.setMotion(actor + 0x158, kDownedMotion, 0.0f, 0.0f); } __except (EXCEPTION_EXECUTE_HANDLER) { fault = true; }
            if (fault) Log("[downed] logical downed motion fault");
        }
        if (s.hp == 1) { skip = true; skipResult = 1; return delta; }
        return 1 - s.hp;
    }
    Guard& g = g_down.guard;
    if (g.active && !g.used && actor == g.actor) {
        uintptr_t status = 0;
        int hp = -1;
        if (ReadHitTrace(actor + 0x5C0, status) && status && ReadHitTrace(status, hp)) {
            const int rewritten = GuardedReviveDelta(delta, idx, hp, g.maxHp, g.target);
            if (rewritten != delta) {
                g.used = true;
                Log("[downed] revive heal rewritten delta=%d -> %d hp=%d target=%d", delta, rewritten, hp, g.target);
                return rewritten;
            }
        }
        return delta;
    }
    if (g_down.state == State::Downed && actor == g_down.downedActor) ++g_down.statCalls;
    return delta;
}

// ---------------------------------------------------------------------------
// Commands (frame start, owner thread)
// ---------------------------------------------------------------------------
static Result Kill(const Snap& s) {
    if (g_down.state != State::Ready) return Result::WrongState;
    if (!s.canonical) return Result::NotCanonical;
    if (s.inEvent) return Result::InEvent;
    if (s.hp <= 0 || s.maxHp <= 0 || (s.flags & 4U) || s.task != 0) return Result::Unsafe;
    // Branches in 0x404D60 that never reach the gated request: g_sys400 bit 17
    // (script signal 0x5A), drive form (actor+0xDE0 in 1..6 or 0xC) and summon state.
    uint32_t sys = 0, form = 0, summon = 0;
    if (!ReadHitTrace(g_exeBase + RVA_SYS_FLAGS, sys) || !ReadHitTrace(s.actor + 0xDE0, form) ||
        !ReadHitTrace(g_exeBase + RVA_SUMMON_STATE, summon)) return Result::Unsafe;
    if ((sys & 0x20000U) || (form >= 1 && form <= 6) || form == 0xC || summon != 0) {
        Log("[downed] kill refused: non-gated death branch sys400=0x%X form=%u summon=%u", sys, form, summon);
        return Result::Branch;
    }
    // reactFlag 0 matches an ordinary hit: ApplyHitDamage passes 0 to vtbl+0xE8 for damage.
    const auto apply = reinterpret_cast<PFN_ApplyStatDelta>(g_exeBase + RVA_APPLY_STAT);
    int result = -1;
    ++g_down.killCount;
    Log("[downed] kill actor=%llX hp=%d/%d frame=%u control=%u", static_cast<unsigned long long>(s.actor),
        s.hp, s.maxHp, g_frameCounter, g_down.control ? 1u : 0u);
    __try {
        result = apply(reinterpret_cast<void*>(s.actor), -s.hp, 0, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_down.state = State::Refused;
        Log("[downed] kill FAULT; spike refused");
        return Result::Fault;
    }
    const Snap after = Capture(s.actor);
    Log("[downed] kill returned=%d hp=%d flags9B8=0x%X deadAction=%u controllerOff=%u task=%llX state=%u",
        result, after.hp, after.flags, after.deadAction ? 1u : 0u, after.controllerOff ? 1u : 0u,
        static_cast<unsigned long long>(after.task), static_cast<unsigned>(g_down.state));
    if (g_down.control) return after.task != 0 ? Result::Ok : Result::Incomplete;
    return g_down.state == State::Downed && after.task == 0 ? Result::Ok : Result::Incomplete;
}

// The single revive entry. The debug command calls it today; P3's owner
// consume-once ReviveRequest helper calls it later with the accepted episode.
// SessionHost's room/range/sequence checks don't replace these fresh native
// owner checks: owner thread, canonical local player, still downed in the same
// episode, native dead flag, no game-over task, outside events, pointers live.
// VUH-2465: revive a logically downed player through the native stat funnel: heal to 25% of max
// (at least 1), the game's ignore-hit grace and an idle stand-up motion. No dead bit to clear.
static Result ReviveLogical(uint64_t episode, const Snap& s) {
    if (!s.status || s.maxHp <= 0 || s.hp <= 0 || (s.flags & 4U) || s.task != 0) return Result::Unsafe;
    const int target = ReviveTarget(s.maxHp);
    g_down.reviveTarget = target;
    ++g_down.reviveCount;
    Log("[downed] revive episode=%llX actor=%llX hp=%d/%d target=%d logical=1 frame=%u",
        static_cast<unsigned long long>(episode), static_cast<unsigned long long>(s.actor), s.hp, s.maxHp, target,
        g_frameCounter);
    ReleaseDownedSpectate();
    const auto apply = reinterpret_cast<PFN_ApplyStatDelta>(g_exeBase + RVA_APPLY_STAT);
    bool fault = false;
    g_down.reviving = true;
    __try {
        if (target > s.hp) (void)apply(reinterpret_cast<void*>(s.actor), target - s.hp, 0, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) { fault = true; }
    g_down.reviving = false;
    const Snap after = Capture(s.actor);
    g_down.reviveHpAfter = after.hp;
    const bool ok = !fault && after.hp == target && !(after.flags & 4U);
    if (ok) {
        g_down.state = State::Ready;
        g_down.logical = false;
        g_down.downedActor = 0;
        g_logicalActor.store(0, std::memory_order_release);
        g_logicalHeld.store(false, std::memory_order_release);
    }
    Log("[downed] revive returned hp=%d/%d flags9B8=0x%X logical=1 fault=%u state=%u", after.hp, after.maxHp, after.flags,
        fault ? 1u : 0u, static_cast<unsigned>(g_down.state));
    if (!ok) return fault ? Result::Fault : Result::Incomplete;
    if (g_down.setInvuln) {
        bool graceFault = false;
        __try { g_down.setInvuln(s.actor, kReviveGraceFrames); } __except (EXCEPTION_EXECUTE_HANDLER) { graceFault = true; }
        g_down.graceActor = graceFault ? 0 : s.actor;
        g_down.graceLastHp = after.hp;
        g_down.graceHits = g_down.graceEnemyHits = g_down.graceHpDrops = 0;
        Log("[downed] revive grace frames=%.0f fault=%u", kReviveGraceFrames, graceFault ? 1u : 0u);
    }
    if (g_down.setMotion) {
        bool motionFault = false;
        __try { g_down.setMotion(s.actor + 0x158, kStandMotion, 0.0f, 0.0f); } __except (EXCEPTION_EXECUTE_HANDLER) { motionFault = true; }
        Log("[downed] revive stand motion requested=%d fault=%u", kStandMotion, motionFault ? 1u : 0u);
    }
    return Result::Ok;
}

static Result TryRevive(uint64_t episode) {
    if (!g_down.requested || g_down.control || !g_down.revive) return Result::Off;
    if (!OwnerThread()) return Result::NotReady;
    if (g_down.state != State::Downed) return Result::WrongState;
    if (episode == 0 || episode != g_down.episode) return Result::WrongState;
    const Snap s = Capture(g_soraActor);
    if (!s.canonical || s.actor != g_down.downedActor) return Result::NotCanonical;
    if (s.inEvent) return Result::InEvent;
    if (g_down.logical) return ReviveLogical(episode, s);
    if (!(s.flags & 4U) || !s.ctrl || !s.aux || !s.status || s.maxHp <= 0 || s.task != 0) return Result::Unsafe;
    const int target = ReviveTarget(s.maxHp);
    g_down.reviveTarget = target;
    g_down.guard = {true, false, s.actor, target, s.maxHp};
    ++g_down.reviveCount;
    Log("[downed] revive episode=%llX actor=%llX hp=%d/%d target=%d frame=%u",
        static_cast<unsigned long long>(episode),
        static_cast<unsigned long long>(s.actor), s.hp, s.maxHp, target, g_frameCounter);
    bool fault = false;
    ReleaseDownedSpectate(); // native revive begins: camera selection is no longer owned
    __try {
        __try {
            g_down.revive(s.actor);
        } __except (EXCEPTION_EXECUTE_HANDLER) { fault = true; }
    } __finally {
        g_down.reviveGuardUsed = g_down.guard.used ? 1U : 0U;
        g_down.guard = {};
    }
    if (fault) {
        g_down.state = State::Refused;
        Log("[downed] revive FAULT; spike refused");
        return Result::Fault;
    }
    const Snap after = Capture(s.actor);
    g_down.reviveHpAfter = after.hp;
    const bool cleared = (after.flags & 4U) == 0;
    g_down.state = cleared ? State::Ready : State::Refused;
    g_down.downedActor = 0;
    Log("[downed] revive returned hp=%d/%d flags9B8=0x%X deadAction=%u controllerOff=%u guardUsed=%u state=%u",
        after.hp, after.maxHp, after.flags, after.deadAction ? 1u : 0u, after.controllerOff ? 1u : 0u,
        g_down.reviveGuardUsed, static_cast<unsigned>(g_down.state));
    const bool ok = cleared && after.hp == target && !after.deadAction && !after.controllerOff && g_down.reviveGuardUsed;
    if (ok && g_down.setInvuln) {
        // Post-revive grace: the game's own ignore-hit timer (0x3D65A0 skips non-heal
        // hits while actor+0xD70 > 0), set through its own setter 0x3D7E40.
        bool graceFault = false;
        __try { g_down.setInvuln(s.actor, kReviveGraceFrames); } __except (EXCEPTION_EXECUTE_HANDLER) { graceFault = true; }
        g_down.graceActor = graceFault ? 0 : s.actor;
        g_down.graceLastHp = after.hp;
        g_down.graceHits = g_down.graceEnemyHits = g_down.graceHpDrops = 0;
        Log("[downed] revive grace frames=%.0f fault=%u", kReviveGraceFrames, graceFault ? 1u : 0u);
    }
    if (ok && g_down.setMotion && OwnerThread()) {
        // Stand up: 0x3AA8D0 requests no motion, so the death motion 0x36 stays
        // current until movement picks one (pair 224936: 14 s with no input on a
        // networked client). Request idle through the native setter, once.
        uint32_t before = 0, afterMotion = 0;
        ReadHitTrace(s.actor + offsets::actor::ANIM_ID, before);
        bool motionFault = false;
        __try { g_down.setMotion(s.actor + 0x158, kStandMotion, 0.0f, 0.0f); } __except (EXCEPTION_EXECUTE_HANDLER) { motionFault = true; }
        ReadHitTrace(s.actor + offsets::actor::ANIM_ID, afterMotion);
        Log("[downed] revive stand motion %u -> %u (requested %d) fault=%u", before, afterMotion, kStandMotion,
            motionFault ? 1u : 0u);
    }
    return ok ? Result::Ok : Result::Incomplete;
}

// ---------------------------------------------------------------------------
// P3 network contract (docs/DOWNED_REVIVE.md, protocol11 / AvatarBridge4)
// ---------------------------------------------------------------------------

// Called every owner frame from Tick (alive and downed). Unavailable -> {}.
static void PublishLocalDownedState(PublishKind kind, uint64_t episode) {
    g_down.published = kind;
    kh2coop::LocalDownedState state {};
    enemysync::DownedScope scope;
    if ((kind == PublishKind::Alive || kind == PublishKind::Downed) && enemysync::CaptureDownedScope(scope)) {
        state.context = scope.context;
        state.epoch = scope.epoch;
        state.worldId = scope.worldId;
        state.roomId = scope.roomId;
        state.door = scope.door;
        state.mapProgram = scope.mapProgram;
        state.battleProgram = scope.battleProgram;
        state.eventProgram = scope.eventProgram;
        state.episode = kind == PublishKind::Downed ? episode : 0;
        state.sampledAtMs = GetTickCount64();
        state.downed = kind == PublishKind::Downed;
        g_net.localSlot = scope.localSlot;
    }
    g_net.published = state;
    enemysync::NoteLocalDownedState(state);
    if (g_avatarBridge.IsOpen()) g_avatarBridge.SetLocalDownedState(state);
}

// enemysync::AdmitReviveRequest -> here, owner thread, same frame as Tick's
// publication, after envelope admission and ReviveOwnerGate::Consume reserved
// the episode. Exactly one TryRevive(episode), which reruns every fresh native
// owner check; a refusal is final.
// S1: mint a fresh episode while still downed so a later request can target it.
// Strictly increasing, never reused: every floor (SessionHost revivedEpisode,
// NetworkClient receivedReviveEpisode_, ReviveOwnerGate consumedEpisode_) is "<=".
static void RemintEpisode(const char* why) {
    g_down.episode = NextEpisode(g_down.episode);
    g_down.episodeFrames = 0;
    ++g_down.episodeRemints;
    Log("[downed] episode re-minted reason=%s episode=%llX remints=%u", why,
        static_cast<unsigned long long>(g_down.episode), g_down.episodeRemints);
}

// VUH-2632: the session's native difficulty. The host reads its own save byte (save + 0x2498); a friend
// keeps Beginner rules until the host's value arrives.
static downedpolicy::Difficulty LocalNativeDifficulty() {
    std::array<std::uint8_t, 4> magic {};
    std::uint8_t raw = 0xFF;
    if (!ReadHitTraceMemory(g_exeBase + 0x9A98B0, magic.data(), magic.size()) ||
        magic != std::array<std::uint8_t, 4> {'K', 'H', '2', 'J'} || !ReadHitTrace(g_exeBase + 0x9A98B0 + 0x2498, raw))
        return downedpolicy::Difficulty::Unknown;
    return downedpolicy::DifficultyFromByte(raw);
}
static downedpolicy::Difficulty SessionDifficulty() {
    // A client follows the host's value from its RoomTransition (v32); unknown until one arrives.
    return enemysync::ActivationRole() == 2 ? downedpolicy::DifficultyFromByte(enemysync::SessionNativeDifficulty())
                                            : LocalNativeDifficulty();
}
// KH2COOP_REVIVE_POLICY / _HOLD_STANDARD / _HARD, read once.
static const downedpolicy::Config& ReviveConfig() {
    static const downedpolicy::Config config = [] {
        char policy[16] {}, hold[8] {}, hard[16] {};
        if (GetEnvironmentVariableA("KH2COOP_REVIVE_POLICY", policy, sizeof(policy)) >= sizeof(policy)) policy[0] = 0;
        if (GetEnvironmentVariableA("KH2COOP_REVIVE_HOLD_STANDARD", hold, sizeof(hold)) >= sizeof(hold)) hold[0] = 0;
        if (GetEnvironmentVariableA("KH2COOP_REVIVE_HARD", hard, sizeof(hard)) >= sizeof(hard)) hard[0] = 0;
        return downedpolicy::ParseConfig(policy, hold, hard);
    }();
    return config;
}
static downedpolicy::Policy SessionRevivePolicy() { return downedpolicy::For(SessionDifficulty(), ReviveConfig()); }

static int ApplyReviveRequest(uint64_t episode, uint8_t requesterSlot, uint64_t seq, uint8_t kind) {
    // VUH-2632: the session's rule holds on the owner too; on Proud/Critical (switch off) a partner
    // revive is refused here whoever sent it, and the downed player sits out until a return. A return
    // (an encounter defeat, a save point) comes back on every difficulty.
    const bool allowed = kind != 0 || SessionRevivePolicy().reviveAllowed; // 0: a partner revive
    const Result r = allowed ? TryRevive(episode) : Result::WrongState;
    if (!allowed)
        Log("[downed] revive-request refused by policy difficulty=%s requester=%u episode=%llX",
            downedpolicy::DifficultyName(SessionDifficulty()), static_cast<unsigned>(requesterSlot),
            static_cast<unsigned long long>(episode));
    if (r == Result::Ok) ++g_net.revivedByRequest;
    else ++g_net.nativeRefused;
    // A return refused only because this player is in an event is owed to the downing (the episode is
    // consumed by now): no remint, the next frame out of the event applies it.
    std::uint64_t identity = 0;
    const bool identityKnown = enemysync::SessionIdentity(identity);
    const bool owed = g_owedReturn.Owe(kind, r == Result::InEvent, identityKnown, identity);
    if (owed)
        Log("[downed] return owed kind=%u episode=%llX: in an event, applied when it ends", static_cast<unsigned>(kind),
            static_cast<unsigned long long>(episode));
    if (r != Result::Ok && !owed) {
        uint32_t flags = 0;
        const bool dead = g_down.logical ||
            (g_down.downedActor && ReadHitTrace(g_down.downedActor + 0x9B8, flags) && (flags & 4U));
        if (ShouldRemint(g_down.state == State::Downed, dead, g_down.episodeFrames, true)) RemintEpisode("native-refusal");
    }
    Log("[downed] revive-request native requester=%u seq=%llu episode=%llX kind=%u result=%d",
        static_cast<unsigned>(requesterSlot), static_cast<unsigned long long>(seq),
        static_cast<unsigned long long>(episode), static_cast<unsigned>(kind), static_cast<int>(r));
    return static_cast<int>(r);
}

// Debug requester (fixture): a teammate's streamed downed episode from the
// validated puppet pose, sent through enemysync's captured-context ring.
static Result RequestRevive(uint32_t targetSlot) {
    if (!g_down.requested || !OwnerThread()) return Result::Off;
    for (const auto& driver : g_puppets) {
        if (!driver.have || static_cast<uint32_t>(driver.pose.pose.ownerSlot) != targetSlot) continue;
        if (!(driver.pose.pose.flags & kh2coop::AvatarDowned) || !driver.pose.pose.downedEpisode) return Result::WrongState;
        uint64_t seq = 0;
        const bool sent = enemysync::SendReviveRequest(static_cast<uint8_t>(targetSlot), driver.pose.pose.downedEpisode, seq);
        g_net.lastSentSeq = seq;
        if (sent) ++g_net.requestsSent; else ++g_net.requestFailures;
        Log("[downed] revive-request sent=%u target=%u episode=%llX seq=%llu", sent ? 1u : 0u, targetSlot,
            static_cast<unsigned long long>(driver.pose.pose.downedEpisode), static_cast<unsigned long long>(seq));
        return sent ? Result::Ok : Result::Unsafe;
    }
    return Result::NotCanonical;
}

// ---------------------------------------------------------------------------
// Player-facing revive prompt (VUH-1504). Default off: KH2COOP_REVIVE_PROMPT=1,
// effective only with KH2COOP_DOWNED_SPIKE=1 and outside the control run.
// Reads only; the one action is the existing RequestRevive(slot).
// ---------------------------------------------------------------------------

// HookedInputCollector, after ApplyPrimaryMailboxInput: the raw slot-0 buttons the
// game will consume this frame (local pad, or the runtime/kh2ctl mailbox override).
static void PromptInput(void* inputStruct) {
    if (!g_prompt.requested || !inputStruct) return;
    uint16_t buttons = 0;
    if (ReadHitTrace(reinterpret_cast<uintptr_t>(inputStruct) + offsets::input::RAW_SLOT0 + offsets::input::BUTTONS, buttons) &&
        reviveprompt::TriangleHeld(buttons)) // L1+Triangle is the native shortcut, not a revive
        g_promptTriangle.store(true, std::memory_order_release);
}


static void PromptTick(const Snap& s) {
    if (!g_prompt.requested) return;
    reviveprompt::Facts f {};
    f.enabled = !g_down.control && g_down.state == State::Ready;
    const auto difficulty = SessionDifficulty();
    const auto policy = downedpolicy::For(difficulty, ReviveConfig());
    f.reviveAllowed = policy.reviveAllowed;
    f.holdFrames = policy.holdFrames;
    if (difficulty != g_prompt.difficulty) {
        g_prompt.difficulty = difficulty;
        Log("[revive-prompt] policy difficulty=%s revive=%u holdFrames=%u forcedBeginner=%u frame=%u",
            downedpolicy::DifficultyName(difficulty), policy.reviveAllowed ? 1u : 0u, policy.holdFrames,
            ReviveConfig().forceBeginner ? 1u : 0u, g_frameCounter);
    }
    f.localCanonical = s.canonical;
    f.localDowned = (s.flags & 4U) != 0 || LogicallyDowned();
    f.inEvent = s.inEvent;
    uint8_t menu = 0xFF;
    f.menuOpen = !ReadHitTrace(g_exeBase + offsets::OPEN_MENU, menu) || menu != 0xFF;
    f.transition = warp::TransitionPending();
    // Native reaction command (calibrated, run 004215): the prompt yields while
    // REACT_CMD_STEAM is nonzero in settled field play. The KH2Lib address is still
    // recorded beside it for the fixture channel only.
    uint16_t reactLib = 0, reactShifted = 0;
    ReadHitTrace(g_exeBase + offsets::REACT_CMD, reactLib);              // KH2Lib 0x2A110E2 (wrong on Steam)
    if (!ReadHitTrace(g_exeBase + offsets::REACT_CMD_STEAM, reactShifted)) reactShifted = 0; // 0x2A11162
    if ((reactLib != g_prompt.reactLib || reactShifted != g_prompt.reactShifted) && g_prompt.reactLogs < 32) {
        ++g_prompt.reactLogs;
        Log("[revive-prompt] native-rc 0x2A11162=%04X (kh2lib 0x2A110E2=%04X) frame=%u",
            reactShifted, reactLib, g_frameCounter);
    }
    g_prompt.reactLib = reactLib;
    g_prompt.reactShifted = reactShifted;
    const bool fieldPlay = f.enabled && f.localCanonical && !f.localDowned && !f.inEvent && !f.menuOpen && !f.transition;
    f.nativeReaction = reviveprompt::NativeReactionFor(g_prompt.rcGate,
        fieldPlay ? enemysync::ActionableNativeReaction(reactShifted) : 0, fieldPlay);
    f.localHp = s.hp;
    f.triangle = g_promptTriangle.exchange(false, std::memory_order_acq_rel);
    if (f.triangle) ++g_prompt.triangleFrames;
    for (const auto& d : g_puppets) {
        const auto& a = d.pose.pose;
        reviveprompt::TargetFacts t {};
        t.have = d.have;
        t.poseAgeFrames = g_frameCounter - d.poseFrame;
        t.downed = (a.flags & kh2coop::AvatarDowned) != 0;
        t.inCutscene = (a.flags & kh2coop::AvatarInCutscene) != 0;
        t.held = (a.flags & kh2coop::AvatarHeld) != 0;
        t.episode = a.downedEpisode;
        t.sameRoom = a.worldId == s.room.world && a.roomId == s.room.room;
        if (!reviveprompt::TargetCandidate(t)) continue;
        const float dx = a.position.x - s.pos[0], dy = a.position.y - s.pos[1], dz = a.position.z - s.pos[2];
        const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (!std::isfinite(dist) || dist >= f.distance) continue;
        f.targetValid = true;
        f.targetSlot = static_cast<uint8_t>(a.ownerSlot);
        f.targetEpisode = a.downedEpisode;
        f.distance = dist;
    }
    g_prompt.out = reviveprompt::Step(g_prompt.state, f);
    const bool yielding = g_prompt.out.hide == reviveprompt::Hide::NativeReaction &&
        f.targetValid && f.distance <= reviveprompt::kRange;
    if (yielding && !g_prompt.yielding && g_prompt.yieldLogs < 256) {
        ++g_prompt.yieldLogs;  // every suppression episode, bounded per process
        Log("[revive-prompt] yield to native RC 0x2A11162=%04X slot=%u episode=%llX dist=%.1f frame=%u",
            static_cast<unsigned>(f.nativeReaction), static_cast<unsigned>(f.targetSlot),
            static_cast<unsigned long long>(f.targetEpisode), f.distance, g_frameCounter);
    }
    g_prompt.yielding = yielding;
    if (g_prompt.out.hide != g_prompt.lastHide && g_prompt.logs < 32) {
        ++g_prompt.logs;
        Log("[revive-prompt] %s slot=%u episode=%llX dist=%.1f nativeRC=%u", reviveprompt::HideName(g_prompt.out.hide),
            static_cast<unsigned>(f.targetSlot), static_cast<unsigned long long>(f.targetEpisode), f.distance,
            static_cast<unsigned>(f.nativeReaction));
    }
    g_prompt.lastHide = g_prompt.out.hide;
    if (g_prompt.out.fire) {
        const Result r = RequestRevive(g_prompt.out.slot);
        ++g_prompt.fires;
        Log("[revive-prompt] hold complete -> RequestRevive(slot=%u episode=%llX) result=%d", static_cast<unsigned>(g_prompt.out.slot),
            static_cast<unsigned long long>(f.targetEpisode), static_cast<int>(r));
    }
}

// After the local player's native update, hold its downed XZ against enemy shove
// (20261019-133539), but preserve native Y from gravity and terrain collision.
// Live11 retained a downed height of -251.985, beyond the unchanged 3D revive reach.
// VUH-2640 Leg A trace (the lead's decision 2): a sit-out friend's own local Thorn runs its own brain, and its
// atk_thorn cinematic attaches and moves the canonical player, here the held downed one. How far this
// update carried a held outage downing before the pin is logged once per episode; not checked live yet.
constexpr float kOutageMovedLog = 200.0f;
static void PinDowned(uintptr_t actor) {
    if (!LogicallyDowned() || actor != g_down.downedActor || !OwnerThread()) return;
    const uintptr_t pos = actor + offsets::actor::ENTITY_TRANSFORM + offsets::entity::POS_X;
    float before[3] {};
    bool read = false;
    __try {
        auto* p = reinterpret_cast<float*>(pos);
        before[0] = p[0]; before[1] = p[1]; before[2] = p[2]; read = true;
        if (HoldDownedHorizontal(p, g_down.downedPos)) ++g_down.pinWrites;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    if (!read || !g_outageDuty.Active() || g_down.episode == g_outageMovedEpisode) return;
    const float dx = before[0] - g_down.downedPos[0], dz = before[2] - g_down.downedPos[2];
    if (dx * dx + dz * dz <= kOutageMovedLog * kOutageMovedLog) return;
    g_outageMovedEpisode = g_down.episode;
    Log("[downed] outage held-player-moved episode=%llX dist=%.0f from=(%.0f,%.0f,%.0f) held=(%.0f,%.0f,%.0f) frame=%u",
        static_cast<unsigned long long>(g_down.episode), std::sqrt(dx * dx + dz * dz), before[0], before[1], before[2],
        g_down.downedPos[0], g_down.downedPos[1], g_down.downedPos[2], g_frameCounter);
}

// HookedInputCollector, last: a logically downed player has no input (VUH-2465). Neutral sticks,
// no buttons; true when it held this callback's input.
// keep: the orphaned pause's own press emitted in this callback (VUH-2640), never gameplay input.
static bool HoldInput(void* inputStruct, uint16_t keep) {
    if (!g_logicalHeld.load(std::memory_order_acquire) || !inputStruct) return false;
    return collectororder::NeutralizeHeld(inputStruct, keep);
}

// ---------------------------------------------------------------------------
// VUH-2632 (D17): a downed teammate who sits out comes back when the host's forced battle ends (the
// encounter is beaten) or when a living player stands at a save point. Requests go per target episode
// (bounded retries while it stays downed), kind EncounterDefeat (host only, the relay checks) or
// SavePoint; the target owner applies one per episode on every difficulty. A host that sat the
// encounter out comes back locally first, and its peer returns wait until it has published itself
// living: the relay refuses a downed requester.
// ---------------------------------------------------------------------------
static downedreturn::EncounterTracker g_encounter;
static downedreturn::MissionCompleteTracker g_missionComplete;
static downedreturn::PendingEncounter g_encounterReturn;
static downedreturn::LivingGate g_living;
static downedreturn::ReturnLatch g_returnLatch;
static reviveprompt::RcGate g_returnRcGate;
static bool PartnerReviveAllowed() { return SessionRevivePolicy().reviveAllowed; }
static void SendReturns(uint8_t kind, const Snap& s) {
    for (const auto& d : g_puppets) {
        const auto& a = d.pose.pose;
        if (!d.have || g_frameCounter - d.poseFrame > 30 || !(a.flags & kh2coop::AvatarDowned) || !a.downedEpisode ||
            a.worldId != s.room.world || a.roomId != s.room.room)
            continue;
        const auto slot = static_cast<uint8_t>(a.ownerSlot);
        if (!g_returnLatch.Take(slot, a.downedEpisode, g_frameCounter)) continue;
        uint64_t seq = 0;
        const bool sent = enemysync::SendReviveRequest(slot, a.downedEpisode, seq, kind);
        if (sent) ++g_down.returnsSent;
        Log("[downed] return %s target=%u episode=%llX seq=%llu sent=%u frame=%u",
            downedreturn::KindName(static_cast<downedreturn::Kind>(kind)), static_cast<unsigned>(slot),
            static_cast<unsigned long long>(a.downedEpisode), static_cast<unsigned long long>(seq), sent ? 1u : 0u, g_frameCounter);
    }
}
// The encounter's targets at its edge (Quill R3): fresh, downed, in the room, with the seat's connection.
static void ClaimEncounterTargets(const Snap& s) {
    enemysync::DownedScope scope;
    const bool scoped = enemysync::CaptureDownedScope(scope);
    for (const auto& d : g_puppets) {
        const auto& a = d.pose.pose;
        if (!d.have || g_frameCounter - d.poseFrame > 30 || !(a.flags & kh2coop::AvatarDowned) || !a.downedEpisode ||
            a.worldId != s.room.world || a.roomId != s.room.room)
            continue;
        const auto slot = static_cast<uint8_t>(a.ownerSlot);
        const uint64_t connection = scoped && slot < 3 ? scope.connections[slot] : 0;
        const bool claimed = g_encounterReturn.Claim({slot, connection, a.downedEpisode});
        Log("[downed] encounter-defeat claim target=%u episode=%llX connection=%llX claimed=%u frame=%u",
            static_cast<unsigned>(slot), static_cast<unsigned long long>(a.downedEpisode),
            static_cast<unsigned long long>(connection), claimed ? 1u : 0u, g_frameCounter);
    }
}
// Those claims only, retried while each target is still the same downing.
static void SendClaimedReturns(const Snap& s) {
    enemysync::DownedScope scope;
    const bool scoped = enemysync::CaptureDownedScope(scope);
    g_encounterReturn.ForEachClaim([&](const downedreturn::ReturnClaim& c) {
        const PuppetDriver* target = nullptr;
        for (const auto& d : g_puppets)
            if (d.have && static_cast<uint8_t>(d.pose.pose.ownerSlot) == c.slot) target = &d;
        const auto* a = target ? &target->pose.pose : nullptr;
        const auto check = downedreturn::CheckClaim(c, scoped, scoped ? scope.connections[c.slot] : 0,
            target && g_frameCounter - target->poseFrame <= 30, a && (a->flags & kh2coop::AvatarDowned),
            a ? a->downedEpisode : 0, a && a->worldId == s.room.world && a->roomId == s.room.room);
        if (check == downedreturn::ClaimCheck::Send && g_returnLatch.Take(c.slot, c.episode, g_frameCounter)) {
            uint64_t seq = 0;
            const bool sent = enemysync::SendReviveRequest(c.slot, c.episode, seq,
                                                           static_cast<uint8_t>(downedreturn::Kind::EncounterDefeat));
            if (sent) ++g_down.returnsSent;
            Log("[downed] return encounter-defeat target=%u episode=%llX seq=%llu sent=%u frame=%u",
                static_cast<unsigned>(c.slot), static_cast<unsigned long long>(c.episode),
                static_cast<unsigned long long>(seq), sent ? 1u : 0u, g_frameCounter);
        } else if (check == downedreturn::ClaimCheck::Drop) {
            Log("[downed] encounter-defeat claim dropped target=%u episode=%llX now=%llX frame=%u",
                static_cast<unsigned>(c.slot), static_cast<unsigned long long>(c.episode),
                static_cast<unsigned long long>(a ? a->downedEpisode : 0), g_frameCounter);
        }
        return check;
    });
}
static void ReturnTick(const Snap& s) {
    if (g_down.control || g_down.state == State::Off || g_down.state == State::Refused) return;
    uint8_t menu = 0xFF;
    const bool menuOpen = !ReadHitTrace(g_exeBase + offsets::OPEN_MENU, menu) || menu != 0xFF;
    const bool field = s.canonical && !s.inEvent && !menuOpen && !warp::TransitionPending();
    // This frame's publication (Tick published before this call) says living with a checked scope.
    const bool living = g_down.state == State::Ready && s.hp > 0;
    g_living.Step(living && g_down.published == PublishKind::Alive && g_net.published.sampledAtMs != 0 &&
                  !g_net.published.downed);
    if (enemysync::ActivationRole() == 1) {
        uint8_t battle = 0;
        if (!ReadHitTrace(g_exeBase + offsets::BATTLE_STATUS, battle)) battle = 0xFF; // unread: never an edge
        const bool cage = battle != 0xFF && g_encounter.Step(battle, warp::TransitionSerial(), warp::LoadSerial(), field);
        const bool mission = g_missionComplete.Step(enemysync::HostMissionCompletes(), warp::TransitionSerial(),
                                                    warp::LoadSerial(), warp::TransitionPending() || warp::LoadPending());
        if ((cage || mission) && g_encounterReturn.Armed()) {
            Log("[downed] encounter defeated again (%s) room=%u/%u frame=%u: its returns are already pending",
                cage ? "forced battle ended" : "mission complete", s.room.world, s.room.room, g_frameCounter);
        } else if (cage || mission) {
            Log("[downed] encounter defeated: %s room=%u/%u frame=%u", cage ? "forced battle ended" : "mission complete",
                s.room.world, s.room.room, g_frameCounter);
            g_encounterReturn.Arm(warp::TransitionSerial(), warp::LoadSerial());
            ClaimEncounterTargets(s); // before the self-revive and the wait for living
            if (g_down.state == State::Downed) {
                const Result r = TryRevive(g_down.episode);
                std::uint64_t identity = 0;
                const bool identityKnown = enemysync::SessionIdentity(identity);
                const bool owed = g_owedReturn.Owe(static_cast<uint8_t>(downedreturn::Kind::EncounterDefeat),
                                                   r == Result::InEvent, identityKnown, identity);
                Log("[downed] return encounter-defeat self episode=%llX result=%d owed=%u",
                    static_cast<unsigned long long>(g_down.episode), static_cast<int>(r), owed ? 1u : 0u);
            }
        }
        switch (g_encounterReturn.Tick(warp::TransitionSerial(), warp::LoadSerial(), g_living.Settled())) {
        case downedreturn::PendingEncounter::Step::Send:
            SendClaimedReturns(s);
            break;
        case downedreturn::PendingEncounter::Step::Dropped:
            Log("[downed] encounter-defeat returns ended: the room changed frame=%u", g_frameCounter);
            break;
        default: break;
        }
    }
    uint16_t react = 0;
    if (!ReadHitTrace(g_exeBase + offsets::REACT_CMD_STEAM, react)) react = 0;
    const auto settled = reviveprompt::NativeReactionFor(g_returnRcGate, react, field && living);
    if (downedreturn::AtSavePoint(settled, living) && g_living.Settled())
        SendReturns(static_cast<uint8_t>(downedreturn::Kind::SavePoint), s);
}

// EntityHook PublishCoopHud: the HUD fields for this frame's prompt.
static void PromptHud(uint8_t& kind, uint8_t& slot, uint16_t& progress) {
    kind = 0; slot = 255; progress = 0;
    if (!g_prompt.requested) return;
    kind = static_cast<uint8_t>(g_prompt.out.kind);
    slot = g_prompt.out.slot;
    progress = g_prompt.out.progress;
}

// A downing the room change resolved (solo, the full party's Continue, or an owed return): 25% on the new
// room's first settled field frame, through the stat funnel.
static void ResolveAfterTransition(const Snap& s, const char* why) {
    const int target = ReviveTarget(s.maxHp);
    if (s.hp < target) {
        const auto apply = reinterpret_cast<PFN_ApplyStatDelta>(g_exeBase + RVA_APPLY_STAT);
        __try { (void)apply(reinterpret_cast<void*>(s.actor), target - s.hp, 0, 0); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    int hp = -1;
    ReadHitTrace(s.status, hp);
    Log("[downed] logical resolve after transition actor=%llX hp=%d -> %d target=%d%s%s", static_cast<unsigned long long>(s.actor),
        s.hp, hp, target, why ? " " : "", why ? why : "");
}

// VUH-2656: the downing held now (0 when not downed), for the mission mirror to bind an admitted end to.
static std::uint64_t CurrentDowning() { return g_down.state == State::Downed ? g_down.downingSerial : 0; }

// A logical downing begun by this owner, not by a lethal hit (a sit-out in a new room, VUH-2632; an outage
// sit-out, VUH-2640): the same hold as a lethal hit (engine HP 1, dead bit clear, the downed motion), under a
// new episode.
static void BeginLogicalHold(const Snap& s) {
    BeginDowned(s.actor, 0, 4);
    g_down.logical = true;
    g_logicalActor.store(s.actor, std::memory_order_release);
    if (s.hp > 1) {
        const auto apply = reinterpret_cast<PFN_ApplyStatDelta>(g_exeBase + RVA_APPLY_STAT);
        g_down.reviving = true;
        __try { (void)apply(reinterpret_cast<void*>(s.actor), 1 - s.hp, 0, 0); } __except (EXCEPTION_EXECUTE_HANDLER) {}
        g_down.reviving = false;
    }
    if (g_down.setMotion) {
        __try { g_down.setMotion(s.actor + 0x158, kDownedMotion, 0.0f, 0.0f); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
}

// HookedPerEntityUpdate, head-of-frame block, after g_soraActor is updated.
static void Tick(uintptr_t head) {
    if (!g_down.requested || g_down.state == State::Off) return;
    if (g_down.thread == 0) {
        g_down.thread = GetCurrentThreadId();
        Log("[downed] owner thread=%lu", static_cast<unsigned long>(g_down.thread));
    }
    if (!OwnerThread()) return;
    Snap s = Capture(head);
    g_down.invulnTimer = 0.0f;
    if (s.actor) ReadHitTrace(s.actor + 0xD70, g_down.invulnTimer);
    if (g_down.graceActor && (s.actor != g_down.graceActor || g_down.state != State::Ready)) g_down.graceActor = 0;
    if (GraceActive()) {
        if (s.hp < g_down.graceLastHp) ++g_down.graceHpDrops;
        g_down.graceLastHp = s.hp;
    }
    {
        // VUH-2632 (D17): this frame's teammates, before a transition erases them (Quill R1: on the first
        // suspended frame this owner still runs first; afterwards no pose is left to count).
        unsigned known = 0, downed = 0, owned = 0;
        std::uint64_t identityBefore = 0, identityAfter = 0;
        const bool knownBefore = enemysync::SessionIdentity(identityBefore); // before the authority (R4)
        const auto authority = enemysync::CapturePuppetAuthority();
        for (int i = 0; i < 2; ++i) {
            const auto& d = g_puppets[i];
            if (downedreturn::CountsForSession(d.have, g_frameCounter - d.poseFrame, d.pose.provenance,
                                               static_cast<uint8_t>(d.pose.pose.ownerSlot), i, authority))
                ++owned;
            if (!d.have || g_frameCounter - d.poseFrame > 600) continue;
            ++known;
            if (d.pose.pose.flags & kh2coop::AvatarDowned) ++downed;
        }
        // The old room's disposition takes every cached pose (R1); the session's history only poses its
        // current authority owns, under an identity unchanged across the capture (R4).
        const bool knownAfter = enemysync::SessionIdentity(identityAfter);
        g_sessionTeammates.StepBookended(knownBefore, identityBefore, owned, knownAfter, identityAfter);
        if (enemysync::ActivationRole() == 2 && knownBefore && knownAfter && identityBefore == identityAfter && identityAfter &&
            !eventholdnative::LifetimeHeartbeatExpired()) {
            g_lastLiveIdentity = identityAfter;            // VUH-2640: the lifetime that may expire next
            g_lastLiveGeneration = enemysync::WorldSessionGeneration();
            // The mirror's newest host-owned boss bind (the previous frame's, under this same live session):
            // retained with the session and room generation it was bound in.
            const uint32_t bound = enemymirror::HostBrainBoundFrame();
            if (bound && bound != g_bossReceipt.frame)
                g_bossReceipt = {bound, identityAfter, g_lastLiveGeneration, warp::TransitionSerial(), warp::LoadSerial()};
        }
        if (g_down.state == State::Downed) g_disposition.Observe(known, downed);
    }
    {
        // VUH-2640 (F1): this friend's event-control lifetime expired (HeartbeatExpired) in a host-owned boss
        // fight, proven by the retained receipt (Thorn bound to the host's authority under the session this
        // seat last had live, in the room generation still current, within two seconds) and this seat's own
        // Thorn mission. It sits out, as D17 logical downings, under the outage duty until that duty's
        // positive end (OutageFrame). Judged once per expired lifetime; a recovered lifetime re-arms it.
        const bool expired = enemysync::ActivationRole() != 1 && eventholdnative::LifetimeHeartbeatExpired();
        if (expired && !g_outageExpirySeen) {
            g_outageExpirySeen = true;
            const auto receipt = g_bossReceipt;
            char mission[24] {};
            downedreturn::OutageExpiryFacts facts {};
            facts.frame = g_frameCounter;
            facts.lastLiveIdentity = g_lastLiveIdentity;
            facts.lastLiveGeneration = g_lastLiveGeneration;
            facts.transition = warp::TransitionSerial();
            facts.load = warp::LoadSerial();
            facts.thornMission = enemysync::NativeMissionFile(mission) && std::strncmp(mission, "TT34_MS304.bar", sizeof(mission)) == 0;
            const bool boss = downedreturn::OutageBossQualified(receipt, facts);
            const uint32_t recoveryAt = warp::RecoveryBootstrapLoadSerial();
            if (boss && g_down.state == State::Downed) g_outageDuty.Begin(recoveryAt);
            else if (boss) { g_down.outagePending = true; g_down.outageRoom = s.room; g_down.outageRecoveryAt = recoveryAt; }
            Log("[downed] outage expiry boss=%u downed=%u receipt=%u/%llX/%u mission=%s room=%u/%u frame=%u", boss ? 1u : 0u,
                g_down.state == State::Downed ? 1u : 0u, receipt.frame, static_cast<unsigned long long>(receipt.identity),
                receipt.generation, mission[0] ? mission : "-", s.room.world, s.room.room, g_frameCounter);
        } else if (!expired) {
            g_outageExpirySeen = false;
        }
        // VUH-2640 Leg A: a fenced friend the host's boss sit-out carried into a reviewed boss arena arrives
        // sitting out: F1's pending outage in that room, anchored to the arrival's own recovery Bootstrap, so
        // only a later recovery (Leg B) ends it. Judged once per arrival (DownedReturn's rule).
        enemysync::SitOutArrival arrival;
        if (enemysync::LatestSitOutArrival(arrival) && arrival.serial != g_sitOutArrivalSeen) {
            g_sitOutArrivalSeen = arrival.serial;
            const auto* arena = kh2coop::bossarena::ById(arrival.arena);
            downedreturn::SitOutArrivalFacts f;
            f.serial = arrival.serial; f.arena = arrival.arena;
            f.generation = arrival.generation; f.transition = arrival.transition; f.load = arrival.load;
            f.recoveryLoad = arrival.recoveryLoad;
            f.generationNow = enemysync::WorldSessionGeneration();
            f.transitionNow = warp::TransitionSerial(); f.loadNow = warp::LoadSerial();
            f.lifetimeExpired = expired;
            f.inArena = arena && s.room.ok && s.room.world == arena->location[0] && s.room.room == arena->location[1] &&
                        s.room.door == arena->location[2] && s.room.map == arena->location[3] &&
                        s.room.battle == arena->location[4] && s.room.event == arena->location[5];
            const bool qualified = downedreturn::SitOutArrivalQualified(f);
            if (qualified && g_down.state == State::Downed) g_outageDuty.Begin(arrival.recoveryLoad);
            else if (qualified) { g_down.outagePending = true; g_down.outageRoom = s.room; g_down.outageRecoveryAt = arrival.recoveryLoad; }
            if (qualified) g_veil = {arrival};  // its own load: any copy hidden before is gone with the old one
            Log("[downed] outage arrival qualified=%u request=%llu arena=%u downed=%u room=%u/%u load=%u recoveryLoad=%u frame=%u",
                qualified ? 1u : 0u, static_cast<unsigned long long>(arrival.request), static_cast<unsigned>(arrival.arena),
                g_down.state == State::Downed ? 1u : 0u, s.room.world, s.room.room, arrival.load, arrival.recoveryLoad,
                g_frameCounter);
        }
        uint8_t outageMenu = 0;
        const bool ready = g_down.state == State::Ready && s.canonical && !s.inEvent && s.status && s.objentry &&
            s.hp > 0 && s.maxHp > 0 && !(s.flags & 4U) && s.task == 0 && !warp::TransitionPending() && LogicalScope(s.objentry) &&
            ReadHitTrace(g_exeBase + offsets::OPEN_MENU, outageMenu) && outageMenu == 0xFF; // after F2 closed the pause
        switch (downedreturn::OutageStartStep(g_down.outagePending, s.room == g_down.outageRoom, expired,
                                              g_down.state == State::Downed, ready)) {
        case downedreturn::OutageStart::Promote:
            g_down.outagePending = false;          // a lethal hit downed it meanwhile, here: that downing sits out
            g_outageDuty.Begin(g_down.outageRecoveryAt);
            Log("[downed] outage sit-out episode=%llX: the pending outage is this downing frame=%u",
                static_cast<unsigned long long>(g_down.episode), g_frameCounter);
            break;
        case downedreturn::OutageStart::Begin:
            g_down.outagePending = false;
            BeginLogicalHold(s);
            g_outageDuty.Begin(g_down.outageRecoveryAt);
            Log("[downed] outage sit-out episode=%llX actor=%llX hp=%d room=%u/%u frame=%u: the link expired in a boss fight",
                static_cast<unsigned long long>(g_down.episode), static_cast<unsigned long long>(s.actor), s.hp,
                s.room.world, s.room.room, g_frameCounter);
            break;
        case downedreturn::OutageStart::Drop:
            g_down.outagePending = false;
            Log("[downed] outage sit-out dropped (%s) frame=%u", expired ? "the room changed" : "a recovered lifetime",
                g_frameCounter);
            break;
        default: break;
        }
    }
    if (enemysync::ActivationRole() == 2) {
        // VUH-2656 (v33): the host's latest admitted mission_complete, judged once, before this frame's
        // room-change check (an end admitted last frame in the old room is still that downing's).
        enemysync::HostMissionEnd e;
        bool producerCurrent = false;
        if (enemysync::LatestHostMissionEnd(e, producerCurrent) && e.serial != g_down.hostEndSerial) {
            g_down.hostEndSerial = e.serial;
            downedreturn::HostEndFacts f {};
            f.downedNow = g_down.state == State::Downed;
            f.downingNow = f.downedNow ? g_down.downingSerial : 0;
            f.downingAtAdmission = e.downing;
            f.windowAtAdmission = e.window;
            f.transitionNow = warp::TransitionSerial(); f.transitionAtAdmission = e.transition;
            f.loadNow = warp::LoadSerial(); f.loadAtAdmission = e.load;
            f.generationNow = enemysync::WorldSessionGeneration(); f.generationAtAdmission = e.generation;
            f.sessionKnown = enemysync::SessionIdentity(f.sessionNow); f.sessionAtAdmission = e.session;
            f.producerCurrent = producerCurrent;
            f.sessionAtDowning = g_down.downingSession;
            f.transitionAtDowning = g_down.downingTransition; f.loadAtDowning = g_down.downingLoad;
            f.encounterKnown = g_down.encounterKnown;
            f.sameMission = g_down.encounterKnown && std::strncmp(e.file, g_down.encounterFile, sizeof(e.file)) == 0;
            f.sameRoom = e.worldId == g_down.downedRoom.world && e.roomId == g_down.downedRoom.room;
            const auto verdict = downedreturn::HostEndFor(f);
            const bool owed = verdict == downedreturn::HostEndVerdict::Owes &&
                              g_owedReturn.OweHostEnd(true, f.sessionKnown, f.sessionNow);
            Log("[downed] host mission complete file=%s room=%u/%u verdict=%s owed=%u episode=%llX frame=%u", e.file,
                e.worldId, e.roomId, downedreturn::HostEndName(verdict), owed ? 1u : 0u,
                static_cast<unsigned long long>(g_down.episode), g_frameCounter);
        }
    }
    if (g_down.state == State::Downed) {
        HoldLost lost = HoldLost::None;
        if (!s.canonical || s.actor != g_down.downedActor) lost = HoldLost::Actor;
        else if (!(s.room == g_down.downedRoom)) lost = HoldLost::Room;
        else if (s.handle0 != g_down.downedHandle0) lost = HoldLost::Handle;
        else if (s.objentry != g_down.downedObjentry) lost = HoldLost::Objentry;
        // VUH-2640 (round 3, Quill r2 P1-1/P1-2): an open outage duty is stepped on every held frame, before
        // the loss rule. Its positive end (a recovery Bootstrap completed since the expiry, or networking
        // stopped for good) ends this hold whether or not the hold saw a loss; a loss while it is open sits
        // out again, still the outage.
        const auto outageStep = g_down.logical
            ? downedreturn::OutageFrame(g_outageDuty, warp::RecoveryBootstrapLoadSerial(), enemysync::NetworkTerminal(),
                                        downedreturn::OutagePlace::Held, lost != HoldLost::None)
            : downedreturn::OutageStep {};
        if ((lost != HoldLost::None || outageStep.act) && g_down.logical) {
            // VUH-2465: a forced transition (a host-led load or a story event's room change) ends a
            // logical downing. The old actor and episode retire (a late request finds no Downed state).
            // VUH-2632 (D17): the player then sits out into the next room, downed again on the first
            // settled field frame of the new actor, unless every teammate it saw in the old room was down
            // too (the host's Continue retry) or this is solo play; then it is resolved at 25% there, as
            // before. Decided from the old room's latched teammates; their poses are gone by now.
            // An owed return (the encounter was won during the event that ends here) is carried in its
            // receipt instead, still checked against the session when it resolves in the new room.
            // VUH-2640 (F1): under an open outage duty its step decides instead (reason 0: the duty ended).
            const auto decision = outageStep.act ? outageStep.decision
                : downedreturn::AfterTransitionFor(g_owedReturn, g_disposition, g_sessionTeammates.Seen());
            g_down.outagePending = false;
            const bool sitOut = decision.after == downedreturn::AfterTransition::SitOut;
            const bool carried = decision.why == downedreturn::Disposition::Returned;
            const unsigned known = g_disposition.Known(), downed = g_disposition.Downed();
            g_down.state = State::Ready;
            g_down.logical = false;
            g_down.downedActor = 0;
            g_down.resolvePending = !sitOut && !carried;
            g_down.sitOutPending = sitOut;
            g_logicalActor.store(0, std::memory_order_release);
            g_logicalHeld.store(false, std::memory_order_release);
            if (sitOut)
                Log("[downed] logical hold ended reason=%u episode=%llX retired; sitting out into the next room "
                    "(%s: teammates known=%u downed=%u)", static_cast<unsigned>(lost), static_cast<unsigned long long>(g_down.episode),
                    downedreturn::DispositionName(decision.why), known, downed);
            else
                Log("[downed] logical hold ended reason=%u episode=%llX retired; resolving at %d%% on the next field frame "
                    "(%s: teammates known=%u downed=%u)", static_cast<unsigned>(lost), static_cast<unsigned long long>(g_down.episode),
                    kRevivePercent, downedreturn::DispositionName(decision.why), known, downed);
        } else if (lost != HoldLost::None) {
            // Terminal: Sora may still be natively dead with no game over; the
            // owned process must be closed by the runner (kh2ctl).
            g_down.state = State::Refused;
            g_down.holdLost = static_cast<uint32_t>(lost);
            g_outageDuty.Clear();
            Log("[downed] hold lost reason=%u actor %llX -> %llX canonical=%u room %u/%u -> %u/%u handle %08X -> %08X; spike refused",
                static_cast<unsigned>(lost), static_cast<unsigned long long>(g_down.downedActor),
                static_cast<unsigned long long>(s.actor), s.canonical ? 1u : 0u, g_down.downedRoom.world,
                g_down.downedRoom.room, s.room.world, s.room.room, g_down.downedHandle0, s.handle0);
        } else {
            ++g_down.downedFrames;
            ++g_down.episodeFrames;
            if (ShouldRemint(true, (s.flags & 4U) != 0 || g_down.logical, g_down.episodeFrames, false)) RemintEpisode("no-accepted-request");
            if (g_down.logical && g_down.setInvuln) {
                // The game's own ignore-hit timer (0x3D65A0 skips non-heal hits while +0xD70 > 0), kept up.
                __try { g_down.setInvuln(s.actor, 30.0f); } __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
            if (g_down.logical && enemysync::ActivationRole() == 1) {
                // VUH-2465 (VUH-2266): the host is down and so is every teammate whose fresh pose is in this
                // room. One second of that and the host lets its own native death run: the game's game
                // over and Continue retry the room from its checkpoint (no save load or write), and the
                // friends follow that host-led reload, which resolves their downing. VUH-2632 (D17): a host
                // that has never seen a teammate in this session is everyone down by itself. VUH-2640: so is
                // a host every other seat of which is absent on real evidence (no roster connection, or a
                // returning seat quarantined until its recovery); pose silence alone never counts.
                bool any = false, all = true;
                for (const auto& d : g_puppets) {
                    const auto& a = d.pose.pose;
                    if (!d.have || g_frameCounter - d.poseFrame > 30 || a.worldId != s.room.world || a.roomId != s.room.room) continue;
                    any = true;
                    if (!(a.flags & kh2coop::AvatarDowned)) all = false;
                }
                const auto bound = enemysync::CaptureSeatAuthority(); // returning IDs bookend the roster
                const auto seats = downedreturn::SeatEvidenceFrom(bound.valid,
                    bound.authority.mode == kh2coop::PuppetAuthorityMode::Network, bound.authority.localSlot,
                    bound.authority.connectionIds, bound.returning);
                g_down.allDownFrames = downedreturn::EveryoneDown(any, all, g_sessionTeammates.Seen(),
                    downedreturn::EveryOtherSeatAbsent(seats)) ? g_down.allDownFrames + 1 : 0;
                if (g_down.allDownFrames >= 60 && g_origApplyStatDelta) {
                    Log("[downed] full party down episode=%llX: the host takes the native game over (Continue retries the room)",
                        static_cast<unsigned long long>(g_down.episode));
                    g_down.fullPartyDown = true;
                    g_down.fullPartyLoad = warp::LoadSerial();
                    g_owedReturn.Clear();
                    g_down.state = State::Ready;
                    g_down.logical = false;
                    g_down.downedActor = 0;
                    g_down.allDownFrames = 0;
                    g_logicalActor.store(0, std::memory_order_release);
                    g_logicalHeld.store(false, std::memory_order_release);
                    __try { (void)g_origApplyStatDelta(reinterpret_cast<void*>(s.actor), -s.hp, 0, 0); }
                    __except (EXCEPTION_EXECUTE_HANDLER) { Log("[downed] full party down: native death FAULT"); }
                }
            }
            // An HP orb heals outside the 3D2EB0 funnel (run 20261019-131234: 1 -> 3 with no stat call).
            // Not a revive: put it back to 1 through the funnel itself, from the HP read now. VUH-2640: an outage
            // sit-out begins its hold earlier in this frame (20 -> 1), so the frame-start s.hp is stale; undoing
            // from it took 19 more and ran the native death (L1 live, run 20261022-085607). Unreadable: no undo.
            int heldHp = 0;
            const int undo = g_down.logical && s.status && ReadHitTrace(s.status, heldHp)
                ? downedreturn::HeldHpUndo(true, heldHp) : 0;
            if (undo) {
                const auto apply = reinterpret_cast<PFN_ApplyStatDelta>(g_exeBase + RVA_APPLY_STAT);
                g_down.reviving = true;
                __try { (void)apply(reinterpret_cast<void*>(s.actor), undo, 0, 0); } __except (EXCEPTION_EXECUTE_HANDLER) {}
                g_down.reviving = false;
                ++g_down.healsUndone;
            }
            if (g_down.logical && g_down.setMotion && s.motionId != static_cast<uint32_t>(kDownedMotion) &&
                g_frameCounter - g_down.motionFrame >= 30) {
                // A hit reaction or knockback that was running at the downing replaces the downed motion
                // when it ends; ask for it again (at most twice a second).
                g_down.motionFrame = g_frameCounter;
                ++g_down.motionRequests;
                __try { g_down.setMotion(s.actor + 0x158, kDownedMotion, 0.0f, 0.0f); } __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
            if (!s.deadAction) ++g_down.actionLost;
            if (!(s.flags & 4U)) ++g_down.deadFlagLost;
            if (!s.controllerOff) ++g_down.controllerOn;
            if (s.hp != g_down.downedHp) ++g_down.hpChanges;
            // Per-frame horizontal step, split by whether the game's processed
            // movement stick was active: input-driven motion vs contact/root
            // motion/sliding. Read-only.
            const bool input = StickActive(s.stickX, s.stickY);
            if (g_down.lastPosValid) {
                const float dx = s.pos[0] - g_down.lastPos[0], dz = s.pos[2] - g_down.lastPos[2];
                const float step = std::sqrt(dx * dx + dz * dz);
                if (std::isfinite(step)) (input ? g_down.driftInput : g_down.driftNoInput) += step;
            }
            ++(input ? g_down.inputFrames : g_down.noInputFrames);
            for (int k = 0; k < 3; ++k) g_down.lastPos[k] = s.pos[k];
            g_down.lastPosValid = true;
            if (g_down.downedFrames % 60 == 0)
                Log("[downed] hold logical=%u skips=%u healsUndone=%u motionRequests=%u pins=%u hitsRefused=%u frames=%u hp=%d deadAction=%u flagLost=%u actionLost=%u ctrlOn=%u statCalls=%u hits=%u enemyHits=%u drift in/no=%.2f/%.2f frames in/no=%u/%u motion=%u task=%llX",
                    g_down.logical ? 1u : 0u, g_down.logicalSkips, g_down.healsUndone, g_down.motionRequests, g_down.pinWrites,
                    static_cast<unsigned>(allyhit::DownedRefusals()), g_down.downedFrames, s.hp, s.deadAction ? 1u : 0u, g_down.deadFlagLost, g_down.actionLost,
                    g_down.controllerOn, g_down.statCalls, g_down.hitAttempts, g_down.enemyHits,
                    g_down.driftInput, g_down.driftNoInput, g_down.inputFrames, g_down.noInputFrames, s.motionId, static_cast<unsigned long long>(s.task));
        }
    }
    const bool resolveReady = g_down.state == State::Ready && s.canonical && !s.inEvent && s.status && s.hp > 0 &&
                              s.maxHp > 0 && !(s.flags & 4U) && !warp::TransitionPending();
    if (g_owedReturn.Owed()) {
        // The owed return's receipt (Quill r3 R1/R2): checked against the session at each use; the native
        // funnel at most once.
        downedreturn::OwedFacts f {};
        f.identityKnown = enemysync::SessionIdentity(f.identity);
        f.downed = g_down.state == State::Downed;
        uint8_t openMenu = 0xFF;
        f.applyReady = !s.inEvent && !warp::TransitionPending() && !warp::LoadPending() &&
                       ReadHitTrace(g_exeBase + offsets::OPEN_MENU, openMenu) && openMenu == 0xFF;
        f.resolveReady = resolveReady;
        f.frame = g_frameCounter;
        const unsigned kind = g_owedReturn.KindOwed();
        Result r = Result::Pending;
        const auto action = downedreturn::OwedTick(g_owedReturn, f, [&] {
            r = TryRevive(g_down.episode);
            return r == Result::Ok ? downedreturn::OwedOutcome::Applied
                 : r == Result::Fault || r == Result::Incomplete || r == Result::WrongState || r == Result::Off
                     ? downedreturn::OwedOutcome::Terminal : downedreturn::OwedOutcome::NotReady;
        });
        switch (action) {
        case downedreturn::OwedAction::Attempt:
            if (r != Result::Ok || !g_owedReturn.Owed())
                Log("[downed] owed return kind=%u episode=%llX result=%d%s frame=%u", kind,
                    static_cast<unsigned long long>(g_down.episode), static_cast<int>(r),
                    r == Result::Ok ? " applied" : g_owedReturn.Owed() ? " not ready" : " retired", g_frameCounter);
            break;
        case downedreturn::OwedAction::Resolve:
            ResolveAfterTransition(s, "owed return");
            break;
        case downedreturn::OwedAction::RevokedSitOut:
        case downedreturn::OwedAction::RevokedResolve:
            // Carried into a room and the session changed before it was applied: the room change's own rule.
            g_down.sitOutPending = action == downedreturn::OwedAction::RevokedSitOut;
            g_down.resolvePending = !g_down.sitOutPending;
            Log("[downed] owed return revoked: another session before the new room; %s instead",
                g_down.sitOutPending ? "sitting out" : "resolving");
            break;
        case downedreturn::OwedAction::Forgotten:
            Log("[downed] owed return forgotten (another session, or no longer downed) episode=%llX",
                static_cast<unsigned long long>(g_down.episode));
            break;
        default: break;
        }
    }
    if (g_outageDuty.Active() && !(g_down.state == State::Downed && g_down.logical)) {
        // VUH-2640 (round 3): the outage ended between holds (an actor lost before the recovery Bootstrap
        // completed, then that completion): the pending re-down resolves instead.
        const auto step = downedreturn::OutageFrame(g_outageDuty, warp::RecoveryBootstrapLoadSerial(),
            enemysync::NetworkTerminal(),
            g_down.sitOutPending ? downedreturn::OutagePlace::SitOut : downedreturn::OutagePlace::Elsewhere, false);
        if (step.act) {
            g_down.sitOutPending = false;
            g_down.resolvePending = true;
            Log("[downed] outage ended before the sit-out re-down (%s); resolving at %d%% on the next field frame frame=%u",
                downedreturn::DispositionName(step.decision.why), kRevivePercent, g_frameCounter);
        } else if (!g_outageDuty.Active()) {
            Log("[downed] outage ended while not downed frame=%u", g_frameCounter);
        }
    }
    if (g_down.resolvePending && resolveReady) {
        g_down.resolvePending = false;
        ResolveAfterTransition(s, nullptr);
    }
    if (g_down.sitOutPending && g_down.state == State::Ready && s.canonical && !s.inEvent && s.status && s.objentry &&
        s.hp > 0 && s.maxHp > 0 && !(s.flags & 4U) && s.task == 0 && !warp::TransitionPending() && LogicalScope(s.objentry)) {
        // VUH-2632 (D17): still sitting out in the new room: the same logical downing as a lethal hit (engine
        // HP 1, dead bit clear, the downed motion), under a new episode.
        g_down.sitOutPending = false;
        BeginLogicalHold(s);
        ++g_down.sitOuts;
        Log("[downed] sitting out in the new room episode=%llX actor=%llX hp=%d room=%u/%u outage=%u frame=%u",
            static_cast<unsigned long long>(g_down.episode), static_cast<unsigned long long>(s.actor), s.hp,
            s.room.world, s.room.room, g_outageDuty.Active() ? 1u : 0u, g_frameCounter);
    }
    if (g_down.fullPartyDown && warp::LoadSerial() != g_down.fullPartyLoad && s.canonical && !s.inEvent && s.status &&
        s.hp > 0 && !(s.flags & 4U) && s.task == 0 && !warp::TransitionPending()) {
        g_down.fullPartyDown = false;
        Log("[downed] full party retry done: alive in field hp=%d/%d actor=%llX", s.hp, s.maxHp,
            static_cast<unsigned long long>(s.actor));
    }
    g_logicalHeld.store(LogicallyDowned(), std::memory_order_release);
    PublishLocalDownedState(PublishFor(g_down.state, s.actor != 0 && (s.flags & 4U) != 0), g_down.episode);
    PromptTick(s);
    ReturnTick(s);
    Channel* c = g_down.ch;
    if (c && c->requestSeq != c->doneSeq) {
        const long seq = c->requestSeq;
        MemoryBarrier();
        Result r = Result::BadCommand;
        if (g_down.state == State::Refused) r = Result::NotReady;
        else if (c->command == static_cast<uint32_t>(Command::Kill)) r = Kill(s);
        else if (c->command == static_cast<uint32_t>(Command::Revive)) r = TryRevive(g_down.episode); // debug stand-in for ReviveRequest
        else if (c->command == static_cast<uint32_t>(Command::RequestRevive)) r = RequestRevive(c->arg);
        c->result = static_cast<int32_t>(r);
        Log("[downed] command seq=%ld cmd=%u result=%d", seq, c->command, static_cast<int>(r));
        s = Capture(head);
        Publish(s);
        InterlockedExchange(&c->doneSeq, seq);
        return;
    }
    Publish(s);
}

// Initialize, after the ApplyStatDelta hook and MinHook are set up.
static void Install(uintptr_t exeBase) {
    char v[2] {};
    g_down.requested = GetEnvironmentVariableA("KH2COOP_DOWNED_SPIKE", v, sizeof(v)) == 1 && v[0] == '1';
    if (!g_down.requested) return;
    char c[2] {};
    g_down.control = GetEnvironmentVariableA("KH2COOP_DOWNED_SPIKE_CONTROL", c, sizeof(c)) == 1 && c[0] == '1';
    // The test channel (Kill / Revive / RequestRevive commands and telemetry) is
    // mapped only for the fixture: KH2COOP_DOWNED_SPIKE_FIXTURE=1, or the control
    // run, which exists only for the fixture. Product play never maps it.
    char f[2] {};
    const bool fixture = g_down.control ||
        (GetEnvironmentVariableA("KH2COOP_DOWNED_SPIKE_FIXTURE", f, sizeof(f)) == 1 && f[0] == '1');
    // Install runs on the loader thread after the ApplyStatDelta hook is live.
    // Plain stores are safe: the game thread ignores the spike until `thread`
    // is set at its first Tick and `state` (stored last) becomes Ready.
    LARGE_INTEGER qpc {};
    QueryPerformanceCounter(&qpc);
    const uint64_t mix = static_cast<uint64_t>(qpc.QuadPart) ^ (GetTickCount64() << 20) ^
        (static_cast<uint64_t>(GetCurrentProcessId()) << 40);
    const uint32_t nonce = static_cast<uint32_t>(mix ^ (mix >> 32));
    g_down.episode = FirstEpisodeBase(nonce); // first intercept = base + 1
    uint32_t mask = 0;
    const bool req0 = Matches(exeBase, RVA_GAMEOVER_REQ0, kReq0Bytes, sizeof(kReq0Bytes));
    const bool req3 = Matches(exeBase, RVA_GAMEOVER_REQ3, kReq3Bytes, sizeof(kReq3Bytes));
    const bool revive = Matches(exeBase, RVA_REVIVE, kReviveBytes, sizeof(kReviveBytes));
    const bool resolve = Matches(exeBase, RVA_RESOLVE, kResolveBytes, sizeof(kResolveBytes));
    uintptr_t tableEntry = 0, vtable = 0;
    const bool action = ReadHitTrace(exeBase + RVA_ACTION_TABLE + ACTION_DEAD * 8, tableEntry) &&
        tableEntry == exeBase + RVA_ACTION_DEADSORA &&
        ReadHitTrace(exeBase + RVA_ACTION_DEADSORA, vtable) && vtable == exeBase + RVA_VT_DEADSORA;
    if (resolve && action) {
        g_down.resolve = reinterpret_cast<PFN_Resolve>(exeBase + RVA_RESOLVE);
        mask |= InstallResolve;
    }
    if (g_origApplyStatDelta) mask |= InstallStatHook;
    if (!g_down.control && Matches(exeBase, RVA_SET_MOTION, kSetMotionBytes, sizeof(kSetMotionBytes))) {
        g_down.setMotion = reinterpret_cast<PFN_SetMotion>(exeBase + RVA_SET_MOTION);
        mask |= InstallStand;
    }
    if (!g_down.control && Matches(exeBase, RVA_SET_INVULN, kSetInvulnBytes, sizeof(kSetInvulnBytes))) {
        g_down.setInvuln = reinterpret_cast<PFN_SetInvuln>(exeBase + RVA_SET_INVULN);
        mask |= InstallGrace;
    }
    const bool verified = req0 && req3 && revive && resolve && action && g_origApplyStatDelta;
    if (!verified) {
        g_down.state = State::Off;
        g_down.installMask = mask;
        Log("[downed] configure requested=1 control=%u REFUSED req0=%u req3=%u revive=%u resolve=%u action=%u statHook=%u",
            g_down.control ? 1u : 0u, req0, req3, revive, resolve, action, g_origApplyStatDelta ? 1u : 0u);
        return;
    }
    wchar_t name[64];
    std::swprintf(name, 64, L"%s%lu", kChannelPrefix, GetCurrentProcessId());
    if (fixture)
        g_down.mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Channel), name);
    if (g_down.mapping) {
        g_down.ch = static_cast<Channel*>(MapViewOfFile(g_down.mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Channel)));
        if (g_down.ch) {
            std::memset(g_down.ch, 0, sizeof(Channel));
            g_down.ch->magic = kChannelMagic;
            g_down.ch->version = kChannelVersion;
            mask |= InstallChannel;
        }
    }
    if (!g_down.control) {
        void* t0 = reinterpret_cast<void*>(exeBase + RVA_GAMEOVER_REQ0);
        void* t3 = reinterpret_cast<void*>(exeBase + RVA_GAMEOVER_REQ3);
        MH_STATUS s0 = MH_CreateHook(t0, reinterpret_cast<void*>(&GameOverRequest0),
                                     reinterpret_cast<void**>(&g_down.origReq0));
        MH_STATUS s3 = MH_CreateHook(t3, reinterpret_cast<void*>(&GameOverRequest3),
                                     reinterpret_cast<void**>(&g_down.origReq3));
        const bool created0 = s0 == MH_OK, created3 = s3 == MH_OK;
        if (created0 && created3) {
            s0 = MH_EnableHook(t0);
            s3 = MH_EnableHook(t3);
        }
        if (created0 && created3 && s0 == MH_OK && s3 == MH_OK) {
            mask |= InstallGate0 | InstallGate3;
            g_down.revive = reinterpret_cast<PFN_Revive>(exeBase + RVA_REVIVE);
            mask |= InstallRevive;
        } else {
            // Only our own hooks: a target another owner already hooked is left alone.
            if (created0) { MH_DisableHook(t0); MH_RemoveHook(t0); }
            if (created3) { MH_DisableHook(t3); MH_RemoveHook(t3); }
            g_down.origReq0 = g_down.origReq3 = nullptr;
            Log("[downed] gate hooks failed: %d/%d", s0, s3);
        }
    }
    if (!g_down.control) enemysync::SetReviveApply(&ApplyReviveRequest);
    if (!g_down.control) enemysync::SetDowningProbe(&CurrentDowning);
    char rp[2] {};
    g_prompt.requested = !g_down.control &&
        GetEnvironmentVariableA("KH2COOP_REVIVE_PROMPT", rp, sizeof(rp)) == 1 && rp[0] == '1';
    if (g_prompt.requested) {
        const auto& config = ReviveConfig();
        Log("[revive-prompt] configure requested=1 range=%.0f holdFrames=%u standardHold=%u hard=%s forcedBeginner=%u "
            "nativeRcYield=0x2A11162 settle=%u", reviveprompt::kRange, reviveprompt::kHoldFrames,
            downedpolicy::For(downedpolicy::Difficulty::Standard, config).holdFrames,
            config.hard == downedpolicy::HardRule::Standard ? "standard" : "off",
            config.forceBeginner ? 1u : 0u, reviveprompt::kRcSettleFrames);
    }
    g_down.installMask = mask;
    const uint32_t needed = g_down.control ? (InstallResolve | InstallStatHook | InstallChannel)
                                           : (fixture ? InstallAll : (InstallAll & ~InstallChannel));
    g_down.state = (mask & needed) == needed ? State::Ready : State::Off;
    if (g_down.ch) {
        g_down.ch->installMask = mask;
        g_down.ch->state = static_cast<uint32_t>(g_down.state);
    }
    Log("[downed] configure requested=1 control=%u fixture=%u installMask=%u state=%s revivePercent=%d episodeBase=%llX",
        g_down.control ? 1u : 0u, fixture ? 1u : 0u, mask, g_down.state == State::Ready ? "ready" : "off", kRevivePercent,
        static_cast<unsigned long long>(g_down.episode));
}

// VUH-2640 Leg A veil, after this frame's tracker (enemysync::OnFrameStart) and outage step (Tick): hide this client's
// own unbound copy of the arena's boss while the arrival's sit-out is open, and show it again if the sit-out ends
// with that copy still in place (downedreturn::RunSitOutVeil, which orders every read). Nothing is sent; game thread.
void VeilTick() {
    if (!g_down.requested || g_down.state == State::Off || !OwnerThread()) return;
    if (!g_veil.arrival.serial && !g_veil.veiled) return;
    const auto transition = warp::TransitionSerial(), load = warp::LoadSerial();
    downedreturn::VeilWorld w;
    w.client = enemysync::ActivationRole() == 2;
    w.latched = g_veil.arrival.serial != 0;
    w.attempted = g_veil.attempted;
    w.veiled = g_veil.veiled;
    w.sitOutOpen = g_outageDuty.Active() || g_down.outagePending;
    w.transitionPending = warp::TransitionPending();
    w.worldCurrent = w.latched && g_veil.arrival.generation == enemysync::WorldSessionGeneration() &&
                     g_veil.arrival.transition == transition && g_veil.arrival.load == load;
    w.loadCurrent = g_veil.veiled && g_veil.transition == transition && g_veil.load == load;
    const auto run = downedreturn::RunSitOutVeil(w, g_veil.actor, g_veil.handle,
        [] { return scriptmirror::VeilReady(); },
        [](uintptr_t& actor, bool& bound) {
            enemysync::SitOutBossCopy copy;
            if (!enemysync::FindSitOutBossCopy(g_veil.arrival.arena, copy)) return false;
            actor = copy.actor; bound = copy.bound; return true;
        },
        [](uintptr_t actor) { return scriptmirror::VeilHandle(actor); },
        [](uintptr_t actor, uint32_t handle) { return scriptmirror::VeilHandleNames(actor, handle); },
        [](uintptr_t actor) { return enemysync::SitOutCopyBound(actor); },
        [](uintptr_t actor, uint32_t handle, bool hide) { return scriptmirror::Veil(actor, handle, hide); });
    switch (run.step) {
    case downedreturn::VeilStep::Hide:
        g_veil.attempted = true;
        if (run.ok) { g_veil.veiled = true; g_veil.actor = run.actor; g_veil.handle = run.handle; g_veil.transition = transition; g_veil.load = load; }
        break;
    case downedreturn::VeilStep::Show:     // shown or not, once: the sit-out's arrival is spent
    case downedreturn::VeilStep::Forget:
        g_veil = {};
        break;
    case downedreturn::VeilStep::None:
        if (!g_veil.veiled && (!w.client || (!w.transitionPending && !w.worldCurrent))) g_veil = {};  // spent
        return;
    }
    if (g_veilLogs < 32) {
        ++g_veilLogs;
        Log("[downed] sit-out veil %s why=%s ok=%u actor=%llX handle=%08X bound=%u open=%u duty=%u load=%u transition=%u "
            "frame=%u (%u/32)",
            run.step == downedreturn::VeilStep::Hide ? "hide" : run.step == downedreturn::VeilStep::Show ? "show" : "forget",
            run.why, run.ok ? 1u : 0u, static_cast<unsigned long long>(run.actor), run.handle, run.bound ? 1u : 0u,
            w.sitOutOpen ? 1u : 0u, g_outageDuty.Active() ? 1u : 0u, load, transition, g_frameCounter, g_veilLogs);
    }
}

} // namespace downedspike
