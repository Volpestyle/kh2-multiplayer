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
};
static SpikeState g_down;

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
    uint32_t triangleFrames = 0, fires = 0, logs = 0, reactLogs = 0;
    uint16_t reactLib = 0, reactShifted = 0;
    reviveprompt::Hide lastHide = reviveprompt::Hide::Off;
};
static PromptRuntime g_prompt;
static std::atomic<bool> g_promptTriangle {false};

static bool GraceActive() { return g_down.graceActor != 0 && g_down.invulnTimer > 0.0f; }

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
    if (ReadHitTrace(actor + 0x5C0, s.status) && s.status) {
        ReadHitTrace(s.status, s.hp);
        ReadHitTrace(s.status + 4, s.maxHp);
    }
    uint32_t c24 = 0;
    s.controllerOff = s.ctrl && ReadHitTrace(s.ctrl + 0x24, c24) && (c24 & 1U) != 0;
    s.deadAction = ResolveIsDeadAction(actor);
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
static bool Intercept(uintptr_t actor, uint32_t mode) {
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
    g_down.episodeFrames = 0;
    g_down.hitAttempts = g_down.enemyHits = g_down.hitLogs = g_down.holdLost = 0;
    g_down.inputFrames = g_down.noInputFrames = 0;
    g_down.driftInput = g_down.driftNoInput = 0.0f;
    g_down.lastPosValid = false;
    g_down.downedFrames = g_down.actionLost = g_down.deadFlagLost = g_down.controllerOn = g_down.statCalls = 0;
    g_down.hpChanges = 0;
    g_down.lastGateMode = mode;
    ++g_down.gateCount;
    if (g_down.gateLogs < 16) {
        ++g_down.gateLogs;
        Log("[downed] gate intercept mode=%u episode=%llX actor=%llX hp=%d flags9B8=0x%X room=%u/%u/%u/%u/%u/%u frame=%u",
            mode, static_cast<unsigned long long>(g_down.episode), static_cast<unsigned long long>(actor), hp, flags,
            g_down.downedRoom.world, g_down.downedRoom.room, g_down.downedRoom.door, g_down.downedRoom.map,
            g_down.downedRoom.battle, g_down.downedRoom.event, g_frameCounter);
    }
    return true;
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
    if (attacker && attacker != g_down.downedActor &&
        ReadHitTrace(attacker + offsets::actor::OBJENTRY_PTR, objentry) && objentry &&
        ReadHitTrace(objentry + offsets::objentry::TYPE_FLAGS, type) && (type == 3 || type == 4))
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
    const bool enemy = attacker && attacker != self &&
        ReadHitTrace(attacker + offsets::actor::OBJENTRY_PTR, objentry) && objentry &&
        ReadHitTrace(objentry + offsets::objentry::TYPE_FLAGS, type) && (type == 3 || type == 4);
    if (attackerOut) *attackerOut = attacker;
    if (typeOut) *typeOut = type;
    return enemy;
}

// ---------------------------------------------------------------------------
// HookedApplyStatDelta, first statement. Rewrites only the guarded revive heal.
// ---------------------------------------------------------------------------
static int StatDelta(void* actorPtr, int delta, int idx) {
    if (!g_down.requested || !OwnerThread()) return delta;
    const auto actor = reinterpret_cast<uintptr_t>(actorPtr);
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
static Result TryRevive(uint64_t episode) {
    if (!g_down.requested || g_down.control || !g_down.revive) return Result::Off;
    if (!OwnerThread()) return Result::NotReady;
    if (g_down.state != State::Downed) return Result::WrongState;
    if (episode == 0 || episode != g_down.episode) return Result::WrongState;
    const Snap s = Capture(g_soraActor);
    if (!s.canonical || s.actor != g_down.downedActor) return Result::NotCanonical;
    if (s.inEvent) return Result::InEvent;
    if (!(s.flags & 4U) || !s.ctrl || !s.aux || !s.status || s.maxHp <= 0 || s.task != 0) return Result::Unsafe;
    const int target = ReviveTarget(s.maxHp);
    g_down.reviveTarget = target;
    g_down.guard = {true, false, s.actor, target, s.maxHp};
    ++g_down.reviveCount;
    Log("[downed] revive episode=%llX actor=%llX hp=%d/%d target=%d frame=%u",
        static_cast<unsigned long long>(episode),
        static_cast<unsigned long long>(s.actor), s.hp, s.maxHp, target, g_frameCounter);
    bool fault = false;
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

static int ApplyReviveRequest(uint64_t episode, uint8_t requesterSlot, uint64_t seq) {
    const Result r = TryRevive(episode);
    if (r == Result::Ok) ++g_net.revivedByRequest;
    else ++g_net.nativeRefused;
    if (r != Result::Ok) {
        uint32_t flags = 0;
        const bool dead = g_down.downedActor && ReadHitTrace(g_down.downedActor + 0x9B8, flags) && (flags & 4U);
        if (ShouldRemint(g_down.state == State::Downed, dead, g_down.episodeFrames, true)) RemintEpisode("native-refusal");
    }
    Log("[downed] revive-request native requester=%u seq=%llu episode=%llX result=%d",
        static_cast<unsigned>(requesterSlot), static_cast<unsigned long long>(seq),
        static_cast<unsigned long long>(episode), static_cast<int>(r));
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
    f.localCanonical = s.canonical;
    f.localDowned = (s.flags & 4U) != 0;
    f.inEvent = s.inEvent;
    uint8_t menu = 0xFF;
    f.menuOpen = !ReadHitTrace(g_exeBase + offsets::OPEN_MENU, menu) || menu != 0xFF;
    f.transition = warp::TransitionPending();
    // Native reaction command: both candidate addresses are read and recorded for
    // calibration; the yield is UNVERIFIED and uses neither (avoids a false hide).
    uint16_t reactLib = 0, reactShifted = 0;
    ReadHitTrace(g_exeBase + offsets::REACT_CMD, reactLib);           // [KH2LIB] 0x2A110E2
    ReadHitTrace(g_exeBase + offsets::REACT_CMD + 0x80, reactShifted); // likely Steam address 0x2A11162
    if ((reactLib != g_prompt.reactLib || reactShifted != g_prompt.reactShifted) && g_prompt.reactLogs < 32) {
        ++g_prompt.reactLogs;
        Log("[revive-prompt] native-rc candidates (UNVERIFIED, not used) 0x2A110E2=%04X 0x2A11162=%04X frame=%u",
            reactLib, reactShifted, g_frameCounter);
    }
    g_prompt.reactLib = reactLib;
    g_prompt.reactShifted = reactShifted;
    f.nativeReaction = 0;
    f.localHp = s.hp;
    f.triangle = g_promptTriangle.exchange(false, std::memory_order_acq_rel);
    if (f.triangle) ++g_prompt.triangleFrames;
    for (const auto& d : g_puppets) {
        const auto& a = d.pose.pose;
        if (!d.have || g_frameCounter - d.poseFrame > 30 || !(a.flags & kh2coop::AvatarDowned) ||
            (a.flags & kh2coop::AvatarInCutscene) || !a.downedEpisode ||
            a.worldId != s.room.world || a.roomId != s.room.room) continue;
        const float dx = a.position.x - s.pos[0], dy = a.position.y - s.pos[1], dz = a.position.z - s.pos[2];
        const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (!std::isfinite(dist) || dist >= f.distance) continue;
        f.targetValid = true;
        f.targetSlot = static_cast<uint8_t>(a.ownerSlot);
        f.targetEpisode = a.downedEpisode;
        f.distance = dist;
    }
    g_prompt.out = reviveprompt::Step(g_prompt.state, f);
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

// EntityHook PublishCoopHud: the HUD fields for this frame's prompt.
static void PromptHud(uint8_t& kind, uint8_t& slot, uint16_t& progress) {
    kind = 0; slot = 255; progress = 0;
    if (!g_prompt.requested) return;
    kind = static_cast<uint8_t>(g_prompt.out.kind);
    slot = g_prompt.out.slot;
    progress = g_prompt.out.progress;
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
    if (g_down.state == State::Downed) {
        HoldLost lost = HoldLost::None;
        if (!s.canonical || s.actor != g_down.downedActor) lost = HoldLost::Actor;
        else if (!(s.room == g_down.downedRoom)) lost = HoldLost::Room;
        else if (s.handle0 != g_down.downedHandle0) lost = HoldLost::Handle;
        else if (s.objentry != g_down.downedObjentry) lost = HoldLost::Objentry;
        if (lost != HoldLost::None) {
            // Terminal: Sora may still be natively dead with no game over; the
            // owned process must be closed by the runner (kh2ctl).
            g_down.state = State::Refused;
            g_down.holdLost = static_cast<uint32_t>(lost);
            Log("[downed] hold lost reason=%u actor %llX -> %llX canonical=%u room %u/%u -> %u/%u handle %08X -> %08X; spike refused",
                static_cast<unsigned>(lost), static_cast<unsigned long long>(g_down.downedActor),
                static_cast<unsigned long long>(s.actor), s.canonical ? 1u : 0u, g_down.downedRoom.world,
                g_down.downedRoom.room, s.room.world, s.room.room, g_down.downedHandle0, s.handle0);
        } else {
            ++g_down.downedFrames;
            ++g_down.episodeFrames;
            if (ShouldRemint(true, (s.flags & 4U) != 0, g_down.episodeFrames, false)) RemintEpisode("no-accepted-request");
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
                Log("[downed] hold frames=%u hp=%d deadAction=%u flagLost=%u actionLost=%u ctrlOn=%u statCalls=%u hits=%u enemyHits=%u drift in/no=%.2f/%.2f frames in/no=%u/%u motion=%u task=%llX",
                    g_down.downedFrames, s.hp, s.deadAction ? 1u : 0u, g_down.deadFlagLost, g_down.actionLost,
                    g_down.controllerOn, g_down.statCalls, g_down.hitAttempts, g_down.enemyHits,
                    g_down.driftInput, g_down.driftNoInput, g_down.inputFrames, g_down.noInputFrames, s.motionId, static_cast<unsigned long long>(s.task));
        }
    }
    PublishLocalDownedState(PublishFor(g_down.state, s.actor != 0 && (s.flags & 4U) != 0), g_down.episode);
    PromptTick(s);
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
    char rp[2] {};
    g_prompt.requested = !g_down.control &&
        GetEnvironmentVariableA("KH2COOP_REVIVE_PROMPT", rp, sizeof(rp)) == 1 && rp[0] == '1';
    if (g_prompt.requested) Log("[revive-prompt] configure requested=1 range=%.0f holdFrames=%u nativeRcYield=unverified(disabled)",
                                reviveprompt::kRange, reviveprompt::kHoldFrames);
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

} // namespace downedspike
