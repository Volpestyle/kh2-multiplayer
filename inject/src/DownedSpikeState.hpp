#pragma once
// ============================================================================
// DownedSpikeState — pure rules and the fixture channel layout for the VUH-1504
// downed/revive spike. No game memory, no Windows calls: the native adapter is
// DownedSpike.inl (included inside EntityHook). Default off.
// ============================================================================

#include <cstddef>
#include <cstdint>

namespace kh2coop::inject::downedspike {

// Fixture channel "Local\kh2coop_downed_<KH2_PID>", created only when the spike
// is requested and KH2COOP_DOWNED_SPIKE_FIXTURE=1 (or _CONTROL=1) is set. [client] fields are written by the fixture, [dll] by the DLL.
constexpr const wchar_t* kChannelPrefix = L"Local\\kh2coop_downed_";
constexpr std::uint32_t kChannelMagic = 0x4E574448;  // "HDWN"
constexpr std::uint32_t kChannelVersion = 7;

enum class Command : std::uint32_t { None = 0, Kill = 1, Revive = 2, RequestRevive = 3 /* arg = target slot */ };
enum class Result : std::int32_t {
    Pending = 0, Ok = 1, Off = 2, NotReady = 3, NotCanonical = 4, InEvent = 5,
    WrongState = 6, Unsafe = 7, Fault = 8, Unverified = 9, BadCommand = 10, Incomplete = 11,
    Branch = 12 // native death would take a non-gated branch (bit 17, drive, summon)
};
enum class State : std::uint32_t { Off = 0, Ready = 1, Downed = 2, Refused = 3 };

// installMask bits
constexpr std::uint32_t InstallGate0 = 1, InstallGate3 = 2, InstallRevive = 4,
    InstallResolve = 8, InstallStatHook = 16, InstallChannel = 32, InstallGrace = 64,
    InstallStand = 128, InstallAll = 255;

// What the per-frame P3 setter would publish. Unavailable maps to P3's empty
// LocalDownedState {} (no revive authority), distinct from Alive (downed=false):
// it is used whenever Sora is natively dead but not in a held downed episode.
enum class PublishKind : std::uint32_t { None = 0, Alive = 1, Downed = 2, Unavailable = 3 };
enum class HoldLost : std::uint32_t { None = 0, Actor = 1, Room = 2, Handle = 3, Objentry = 4 };

#pragma pack(push, 4)
struct Channel {
    std::uint32_t magic;          // [dll]
    std::uint32_t version;        // [dll]
    volatile long requestSeq;     // [client] bump after writing command
    volatile long doneSeq;        // [dll] = requestSeq once handled
    std::uint32_t command;        // [client] Command
    std::int32_t result;          // [dll] Result of the last handled request
    // [dll] snapshot, refreshed at the start of every gameplay frame
    volatile long liveFrame;
    std::uint32_t state;          // State
    std::uint64_t actor;          // tracked canonical player this frame
    std::int32_t hp, maxHp;       // *(actor+0x5C0) +0 / +4
    std::uint32_t flags9B8;       // bit 2 = native dead flag
    std::uint32_t deadAction;     // 1 when actor+0xC resolves to ACTION_DEADSORA
    std::uint32_t controllerOff;  // *(actor+0xDC0)+0x24 bit 0
    std::uint32_t publishKind;    // PublishKind handed to the P3 setter stub this frame
    std::uint64_t gameOverTask;   // *(exe+0x2AE8050)
    std::uint64_t episode;        // per-boot nonce << 32 | counter; new per intercept
    std::uint32_t gateCount;      // game-over requests skipped
    std::uint32_t passCount;      // game-over requests passed to the game
    std::uint32_t killCount, reviveCount;
    std::uint32_t downedFrame;    // DLL frame of the last intercept
    std::uint32_t downedFrames;   // frames held in the current/last downed state
    std::uint32_t actionLostFrames;    // downed frames without ACTION_DEADSORA
    std::uint32_t deadFlagLostFrames;  // downed frames without 0x9B8 bit 2
    std::uint32_t controllerOnFrames;  // downed frames with the controller enabled
    std::uint32_t statCallsWhileDowned; // ApplyStatDelta calls on the downed actor
    std::int32_t hpChangesWhileDowned;  // frames whose HP differed from the intercept HP
    std::int32_t reviveTarget, reviveHpAfter;
    std::uint32_t reviveGuardUsed;
    std::uint32_t installMask;
    std::uint32_t lastGateMode;   // 0 or 3
    std::uint32_t lastPassReason; // GateReason of the last pass-through
    std::uint32_t hitAttemptsWhileDowned;      // BuildHit records with the downed actor as victim
    std::uint32_t enemyHitAttemptsWhileDowned; // ... whose attacker is objentry type 3/4
    std::uint32_t holdLostReason; // HoldLost
    // v3 drift telemetry (VUH-1504 treatment 205632 moved 5.9 units while downed)
    float posX, posY, posZ;       // actor+0x640+0x30
    float velX, velZ;             // actor+0xB98 / +0xBA0
    std::uint32_t motionId;       // actor+0x180
    float motionTime;             // actor+0x19C
    float stickX, stickY;         // processed slot-0 movement stick (exe+0xBF31A0+0x30)
    std::uint32_t inputActive;    // |stick| > kStickActive this frame
    std::uint32_t inputFramesWhileDowned, noInputFramesWhileDowned;
    float driftWithInput, driftWithoutInput; // summed horizontal (XZ) per-frame steps while downed
    // v4: post-revive grace, P3 network contract, requester
    std::uint32_t arg;            // [client] Command argument (RequestRevive: target slot)
    float invulnTimer;            // actor+0xD70 (native ignore-hit timer, frames)
    std::uint32_t graceHits, graceEnemyHits, graceHpDrops, graceActive;
    std::uint32_t netPublished;   // 1 when this frame's LocalDownedState carried a checked scope
    std::uint32_t netEpoch;
    std::uint32_t reviveSeen, reviveGateRefused, reviveConsumed, reviveNativeRefused, revivedByRequest;
    std::uint32_t requestsSent, requestFailures;
    std::uint32_t puppetSlot[2], puppetDowned[2];
    std::uint64_t puppetEpisode[2];
    std::uint64_t lastSentSeq;
    std::uint32_t localSlot;
    // v5 (round-5 review S1): episodes re-minted while still downed
    std::uint32_t episodeRemints;
    std::uint32_t episodeFrames;  // frames since the current episode was minted
    // v6: player-facing revive prompt (KH2COOP_REVIVE_PROMPT=1)
    std::uint32_t promptKind, promptSlot, promptProgress, promptHide;
    std::uint32_t triangleFrames; // frames whose raw slot-0 input had Triangle (after the mailbox apply)
    std::uint32_t promptFires;    // requests sent by a completed hold
    // v7: native reaction command. reactCmdShifted is REACT_CMD_STEAM 0x2A11162 (the
    // calibrated address the prompt yields to); reactCmdLib is KH2Lib's 0x2A110E2 (wrong on Steam).
    std::uint32_t reactCmdLib, reactCmdShifted;
};
#pragma pack(pop)
static_assert(sizeof(Channel) == 360, "fixture channel layout is mirrored in run_downed.py");
static_assert(offsetof(Channel, actor) == 32 && offsetof(Channel, gameOverTask) == 64 &&
              offsetof(Channel, episode) == 72, "layout");

constexpr float kStickActive = 0.1f;
// Post-revive grace through the native ignore-hit timer actor+0xD70 (frames;
// the frame delta 0x717480 defaults to 1.0): ~2 s at 60 fps.
constexpr float kReviveGraceFrames = 120.0f;
// Round-5 review S1: a refused or lost revive must not strand a downed player.
// While still downed, a new episode (never reused) is minted after a native
// refusal, or when no request was accepted for this many frames (~30 s).
constexpr std::uint32_t kEpisodeRemintFrames = 1800;
// After a successful revive the player is set to idle (motion 0) through the
// native motion setter 0x3C86A0, the same call the death path uses for 0x36.
constexpr std::int32_t kStandMotion = 0;
constexpr bool ShouldRemint(bool downed, bool deadFlag, std::uint32_t episodeFrames, bool refusedNow) noexcept {
    return downed && deadFlag && (refusedNow || episodeFrames >= kEpisodeRemintFrames);
}
constexpr bool StickActive(float x, float y) noexcept { return x * x + y * y > kStickActive * kStickActive; }

constexpr PublishKind PublishFor(State s, bool nativeDead) noexcept {
    switch (s) {
    case State::Off: return PublishKind::None;
    case State::Downed: return PublishKind::Downed;
    case State::Ready:
    case State::Refused: return nativeDead ? PublishKind::Unavailable : PublishKind::Alive;
    }
    return PublishKind::Unavailable;
}

// Episodes pair with P3's lifetime-monotonic contract: nonzero, strictly
// increasing within the process, and seeded per boot so a restarted owner
// never reissues an ID a stale ReviveRequest could still carry.
constexpr std::uint64_t FirstEpisodeBase(std::uint32_t bootNonce) noexcept {
    return static_cast<std::uint64_t>(bootNonce ? bootNonce : 1u) << 32;
}
constexpr std::uint64_t NextEpisode(std::uint64_t current) noexcept { return current + 1; }

// Gate: skip a game-over request only for the canonical local player, already
// natively dead, on the owner thread, with no game-over task, while Ready.
struct GateFacts {
    bool enabled = false, ownerThread = false;
    bool actorTracked = false, actorIsPlayer = false, actorIsHead = false;
    bool deadFlag = false, taskIdle = false, stateReady = false;
};
enum class GateReason : std::uint8_t {
    Intercept = 0, Off, ForeignThread, NotCanonical, NotDead, TaskActive, WrongState, NoActor
};
constexpr GateReason Decide(const GateFacts& f) noexcept {
    if (!f.enabled) return GateReason::Off;
    if (!f.ownerThread) return GateReason::ForeignThread;
    if (!f.actorTracked || !f.actorIsPlayer || !f.actorIsHead) return GateReason::NotCanonical;
    if (!f.stateReady) return GateReason::WrongState;
    if (!f.deadFlag) return GateReason::NotDead;
    if (!f.taskIdle) return GateReason::TaskActive;
    return GateReason::Intercept;
}
constexpr const char* GateReasonName(GateReason r) noexcept {
    switch (r) {
    case GateReason::Intercept: return "intercept";
    case GateReason::Off: return "off";
    case GateReason::ForeignThread: return "foreign-thread";
    case GateReason::NotCanonical: return "not-canonical";
    case GateReason::NotDead: return "not-dead";
    case GateReason::TaskActive: return "task-active";
    case GateReason::WrongState: return "wrong-state";
    case GateReason::NoActor: return "no-actor";
    }
    return "?";
}

// Product default (lead, 2026-10-06): revive restores 25% of max HP, at least 1.
constexpr int kRevivePercent = 25;
constexpr int ReviveTarget(int maxHp) noexcept {
    if (maxHp <= 0) return 0;
    const long long t = static_cast<long long>(maxHp) * kRevivePercent / 100;
    return t < 1 ? 1 : static_cast<int>(t);
}
// The one +max heal issued by 0x3AA8D0 is rewritten to land exactly on target.
// Returns the original delta when the heal doesn't match the guarded shape.
// The native heal is exactly +max (0x404EE0 -> 0x3A85F0 -> 0x3D5E50 pass it unscaled).
constexpr int GuardedReviveDelta(int delta, int idx, int currentHp, int maxHp, int target) noexcept {
    if (idx != 0 || delta <= 0 || delta != maxHp || target <= 0 || target > maxHp || currentHp < 0 || currentHp >= target)
        return delta;
    return target - currentHp;
}

} // namespace kh2coop::inject::downedspike
