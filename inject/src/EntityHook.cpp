// ============================================================================
// EntityHook — PerEntityUpdate hook + friend AI replacement
//
// This is the core of Strategy B (AI replacement hook). It:
//   1. Hooks PerEntityUpdate (exe+0x3BFD30) via MinHook detour
//   2. Identifies friend entities by comparing actor pointers against
//      the known friend actor pointers from Slot1+0x220/+0x228
//   3. Discovers the friend AI vtable+0x10 function at runtime
//   4. Hooks the friend AI function so controlled friend slots can expose
//      their movement state before the game's own motion-selection calls
//   5. Reads input from network mailbox (if runtime is connected) or
//      local gamepads for Friend1 (gamepad 1) and Friend2 (gamepad 2)
//   6. Writes movement velocity/acceleration to actor struct fields
//      that EntityPositionPhysics reads for movement integration
//
// The game's entity update call chain:
//   EntityUpdateLoop (exe+0x3BF5E0)
//     └─► PerEntityUpdate (exe+0x3BFD30) — OUR HOOK
//           ├─► vtable+0x10 — AI dispatch
//           │     For controlled friends: SKIPPED. We inject movement +
//           │     animation ourselves (via FUN_1403c6dc0 = SetAnimDirect).
//           │     For others: original AI runs normally.
//           ├─► vtable+0x18 — post-main update
//           ├─► vtable+0x28 — pre-physics update
//           └─► EntityPositionPhysics (exe+0x3B89A0) — runs normally
//
// All hook functions run on the game's main thread (single-threaded).
// No synchronization is needed between PerEntityUpdate and AI hooks.
// ============================================================================

#include "EntityHook.hpp"
#include "PatternScan.hpp"
#include "RenderHook.hpp"
#include "Warp.hpp"
#include "SaveGuard.hpp"
#include "CrashDump.hpp"
#include "EnemySync.hpp"
#include "EventHoldNativeInput.hpp"
#include "NativeSpawnController.hpp"
#include "NativeResourceTrace.hpp"
#include "NativeLifecycleTrace.hpp"
#include "NativeLifetimeTrace.hpp"
#include "NativePrivateStatus.hpp"
#include "NativeHitTrace.hpp"
#include "DamagePolicy.hpp"
#include "kh2coop/KH2Offsets.hpp"
#include "kh2coop/InputMailbox.hpp"
#include "kh2coop/AvatarBridge.hpp"
#include "kh2coop/AvatarCapture.hpp"
#include "kh2coop/HitChannel.hpp"

#include <Windows.h>
#include <MinHook.h>
#include <intrin.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "DownedSpikeState.hpp"

namespace kh2coop {
namespace inject {

// ============================================================================
// Function pointer typedefs (from Ghidra decompilation)
// ============================================================================

// PerEntityUpdate: void(void* actorObj)
//   x64 ABI: RCX = actorObj (the actor/entity object pointer)
//   Ghidra sig: void PerEntityUpdate(undefined4* param_1)
using PFN_PerEntityUpdate = void(__fastcall*)(void* actorObj);
using PFN_InputCollector = void(__fastcall*)(void* inputStruct);

// ResolveEntityType: void*(uint32_t typeId)
//   x64 ABI: ECX = typeId, returns RAX = type handler pointer
//   Located at exe+0x4AD270. Maps entity type ID → type handler object.
using PFN_ResolveEntityType = void*(__fastcall*)(uint32_t typeId);

// Friend AI dispatch: void(void* typeHandler, void* actorObj)
//   x64 ABI: RCX = typeHandler, RDX = actorObj
//   Called via vtable+0x10 on the type handler returned by ResolveEntityType.
//   This is the function that makes AI decisions for friend entities.
using PFN_FriendAI = void(__fastcall*)(void* typeHandler, void* actorObj);

// Follow-steering: void*(void* typeHandler, void* outVec4, void* entity, float dt)
//   Called via vtable+0x40 from inside EntityPositionPhysics (exe+0x3B89A0).
//   Returns a pointer to a 4-float follow-steering vector that the physics
//   pipeline writes directly to actor+0xB98 (velocity). THIS is the tether.
using PFN_FollowSteering = void*(__fastcall*)(void* typeHandler, void* outVec4,
                                               void* entity, float dt);

// Motion set functions — the game's own animation trigger API.
// These are what the friend AI calls every frame to drive animations.
// Writing directly to actor+0x180 does NOT work because the animation
// system uses a complex motion playback chain managed by these functions.
//
// FUN_1403b6670(actor, motionChannel, flag, param4, param5):
//   Sets a motion on a specific channel. The friend AI calls this as:
//     FUN_1403b6670(actor, 2, 1, 0, 0)  — channel 2
//   Internally reads actor+0x80 (motion set pointer), searches for the
//   matching channel, creates a motion playback object via FUN_1402c6b30.
//
// FUN_1403b6630(actor, motionChannel, flag, param4):
//   Sets motion on a channel (simpler wrapper). The friend AI calls:
//     FUN_1403b6630(actor, 1, 1, 0)  — channel 1
using PFN_SetMotion = uint64_t(__fastcall*)(void* actor, uint32_t motionChannel,
                                             uint32_t flag, uint32_t param4, int64_t param5);
using PFN_SetMotionSimple = uint64_t(__fastcall*)(void* actor, uint32_t motionChannel,
                                                   uint32_t flag, uint32_t param4);

// FUN_1403c6dc0 — Motion controller animation setter.
// Takes the motion controller sub-object (actor+0x158) and an animation ID.
// Looks up the animation in the entity's motion set and triggers playback.
// Short-circuits if the requested animation is already playing (safe to call
// every frame). Both Sora and friends ultimately use this path.
//
// Traced via Ghidra + CE data breakpoint on actor+0x188 (motion controller's
// active animation DWORD at motCtrl+0x30). All animation writes come through
// FUN_1403c8a40 which is called from FUN_1403c6dc0 → FUN_1403c86a0.
//
// x64 calling convention:
//   RCX  = motionCtrl pointer (actor + 0x158)
//   EDX  = animation ID (0=IDLE, 1=WALK, 2=RUN, 0x36=friend follow-attack, etc.)
//   XMM2 = start time (float, usually 0.0)
//   XMM3 = blend parameter (float, usually 0.0)
using PFN_SetAnimationDirect = void(__fastcall*)(void* motCtrl, int animId,
                                                  float startTime, float blendParam);

// FUN_1403c88c0 — Motion chain animation setter.
// Sets an animation on the motion controller. Called by the motCtrl tick
// at loop boundaries and by FUN_1403c86a0 for explicit animation changes.
// Returns 1 on success, 0 on failure (animation not found in motion set).
using PFN_MotionChainSetAnim = uint8_t(__fastcall*)(void* motCtrl, int animId,
                                                      float startTime, float blendParam);

// FUN_1403d5e50 — Movement dispatch with deceleration handling.
// This is the function that the friend AI calls through vtable+0xE8
// to drive movement animation transitions (idle ↔ walk ↔ run).
// Handles both acceleration (delta > 0) and deceleration (delta < 0).
// When decelerating past 0, calls FUN_1403d3cf0 → FUN_1403c20a0 which
// resets the movement state and triggers the idle animation path.
// When accelerating, updates the motion accumulator and triggers walk/run.
//
// x64 calling convention:
//   RCX = actor pointer
//   EDX = speed delta (positive=accel, negative=decel)
//   R8D = channel (0 for movement)
//   R9B = flag (0 or 1)
using PFN_MovementDispatch = void(__fastcall*)(void* actor, int speedDelta,
                                                int channel, uint8_t flag);

// ============================================================================
// AOB Signature — PerEntityUpdate prologue
//
// Live bytes from CE (Steam Global, 2026-03-31):
//   40 53 48 83 EC 30 48 8B D9 0F 29 74 24 20 8B 49 04
//
// Disassembly:
//   40 53              PUSH RBX           (REX + PUSH)
//   48 83 EC 30        SUB RSP, 0x30
//   48 8B D9           MOV RBX, RCX
//   0F 29 74 24 20     MOVAPS [RSP+0x20], XMM6
//   8B 49 04           MOV ECX, [RCX+0x4]
//
// All 17 bytes are non-relocatable (no RIP-relative offsets), making this
// a robust signature across builds.
// ============================================================================
static constexpr const char* AOB_PER_ENTITY_UPDATE =
    "40 53 48 83 EC 30 48 8B D9 0F 29 74 24 20 8B 49 04";

// Fallback RVA for Steam Global build
static constexpr uint64_t RVA_PER_ENTITY_UPDATE =
    offsets::entity_update::PER_ENTITY_UPDATE;  // 0x3BFD30

// RVA for ResolveEntityType (no AOB scan yet — stable across sessions)
static constexpr uint64_t RVA_RESOLVE_ENTITY_TYPE = 0x4AD270;

// RVAs for motion set functions (the game's animation trigger API)
static constexpr uint64_t RVA_SET_MOTION        = 0x3B6670;
static constexpr uint64_t RVA_SET_MOTION_SIMPLE  = 0x3B6630;

// Historical name: this is the four-argument TakeDamage helper, not the
// five-argument type-handler virtual at +0xE8. Keep the original ABI intact.
static constexpr uint64_t RVA_MOVEMENT_DISPATCH = 0x3D5E50;
// Verified TakeDamage entry in the saved Steam Global image, before detouring.
static constexpr uint8_t kTakeDamageBytes[] = {
    0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c,
    0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57};

// RVA for the direct animation setter (FUN_1403c6dc0).
// Discovered by tracing writes to actor+0x188 (motionCtrl+0x30) via CE
// data breakpoint → FUN_1403c8a40 → called from FUN_1403c86a0 → called
// from FUN_1403c6dc0. Both Sora's and friends' movement animation
// ultimately flows through this function.
static constexpr uint64_t RVA_SET_ANIMATION_DIRECT = 0x3C6DC0;

// RVA for the underlying animation setter (FUN_1403c86a0).
// This is the ACTUAL animation change function — it clears the motion
// playback chain and calls FUN_1403c88c0 to set the new animation.
//
// FUN_1403c6dc0 (SetAnimationDirect) wraps this but adds a
// FUN_1403a6420() check that depends on global register state from a
// prior entity lookup. That check makes FUN_1403c6dc0 fail when called
// from our hook context. FUN_1403c86a0 skips that check entirely.
//
// Used by the game's own FUN_1403c3c30 (friend delta replacement) to
// transition from animation 0x36 → 0x37 during combat follow-up.
// Safe to call from hook context.
static constexpr uint64_t RVA_SET_ANIMATION_UNDERLYING = 0x3C86A0;

// RVA for the motion chain animation setter (FUN_1403c88c0).
// This is the function that actually writes actor+0x180 (animation ID)
// and sets up the motion playback object. Called by:
//   1. FUN_1403c86a0 (underlying setter) — for explicit animation changes
//   2. FUN_1403c6740 (motCtrl tick) — at animation LOOP boundaries
//   3. Itself (recursive) — for chained animation transitions
//
// Hooking this lets us intercept ALL animation changes, including the
// tick's loop-boundary re-evaluation. By replacing the animation ID in
// the hook, we make the tick's own loop logic seamlessly play our
// desired animation without fighting it every frame.
static constexpr uint64_t RVA_MOTION_CHAIN_SET_ANIM = 0x3C88C0;

// ============================================================================
// Movement field offsets within actor object
//
// EntityPositionPhysics (exe+0x3B89A0) reads these to apply movement.
// The friend AI normally writes here; we replace those writes.
// Source: Ghidra decompile of EntityPositionPhysics, confirmed in RE session.
// ============================================================================
static constexpr uint64_t ACTOR_VELOCITY_X   = 0xB98;  // float
static constexpr uint64_t ACTOR_VELOCITY_Y   = 0xB9C;  // float
static constexpr uint64_t ACTOR_VELOCITY_Z   = 0xBA0;  // float
static constexpr uint64_t ACTOR_ACCEL_X      = 0xA58;  // float
static constexpr uint64_t ACTOR_ACCEL_Y      = 0xA5C;  // float
static constexpr uint64_t ACTOR_ACCEL_Z      = 0xA60;  // float
// EntityPositionPhysics subtracts frame time from actor+0xBA8 and only calls
// the friend follow-steering callback when the result goes negative. Holding
// this timer positive disables the residual vanilla tether to Sora.
static constexpr uint64_t ACTOR_FOLLOW_TIMER = 0xBA8;  // float
static constexpr float    DISABLE_FOLLOW_TIMER = 999.0f;

// Animation ID — DWORD at actor+0x180, maps to OpenKH MotionSet enum.
// Writing this tells the game which animation to play.
// Confirmed via live CE: the game reads +0x180 for motion playback.
// +0x184 is the animation sub-state / variant (ANIM_SUB), NOT the motion ID.
static constexpr uint64_t ACTOR_ANIM_ID      = 0x180;  // DWORD — motion ID (MotionSet enum)
static constexpr uint32_t ANIM_IDLE          = 0;
static constexpr uint32_t ANIM_WALK          = 1;
static constexpr uint32_t ANIM_RUN           = 2;

// ============================================================================
// Configuration
// ============================================================================

// Set to false to disable movement injection (AI is still suppressed,
// friend will idle in place). Useful for isolating hook issues.
static constexpr bool ENABLE_MOVEMENT_INJECTION = true;

// Movement speed constants (units/frame at 60fps)
static constexpr float WALK_SPEED    = 3.0f;
static constexpr float RUN_SPEED     = 6.0f;
static constexpr float STICK_DEADZONE = 0.25f;

// ============================================================================
// Global state
// ============================================================================

static uintptr_t g_exeBase = 0;

// Hook trampolines (set by MH_CreateHook)
static PFN_PerEntityUpdate   g_origPerEntityUpdate  = nullptr;
static PFN_InputCollector    g_origInputCollector   = nullptr;
static PFN_ResolveEntityType g_resolveEntityType     = nullptr;
static PFN_FriendAI          g_origFriendAI          = nullptr;
static PFN_FriendAI          g_origFriendPrePhysics  = nullptr;
static PFN_MovementDispatch  g_origMovementDispatch  = nullptr;
static PFN_FollowSteering   g_origFollowSteering    = nullptr;
static PFN_MotionChainSetAnim g_origMotionChainSetAnim = nullptr;

// Motion set function pointers (not hooked — called directly)
static PFN_SetMotion        g_setMotion             = nullptr;
static PFN_SetMotionSimple  g_setMotionSimple       = nullptr;

// Direct animation setter — FUN_1403c6dc0.
// Called to set idle/walk/run on controlled friends in place of the vanilla
// AI's follow-distance-based animation selection.
static PFN_SetAnimationDirect g_setAnimationDirect  = nullptr;

// Underlying animation setter — FUN_1403c86a0.
// Bypasses FUN_1403c6dc0's FUN_1403a6420() validation that fails from hook
// context. This is the function that actually changes the animation.
static PFN_SetAnimationDirect g_setAnimationUnderlying = nullptr;

// Friend entity tracking — refreshed every PerEntityUpdate call
static uintptr_t g_friend1Actor = 0;
static uintptr_t g_friend2Actor = 0;

// Which friend slot is currently being processed by PerEntityUpdate.
// Set before calling original, read by the AI hook. Safe because the
// entity update loop is single-threaded.
//   0 = not a friend
//   1 = Friend1 (Donald / gamepad 1)
//   2 = Friend2 (Goofy / gamepad 2)
static int g_currentFriendSlot = 0;

// Friend AI hook state
static bool  g_friendAIHooked  = false;
static void* g_hookedAITarget  = nullptr;
static bool  g_friendPrePhysicsHooked = false;
static void* g_hookedPrePhysicsTarget = nullptr;
static bool  g_followSteeringHooked = false;
static void* g_hookedFollowSteeringTarget = nullptr;

// XInput gamepad state (read once per frame)
struct GamepadState {
    bool          connected = false;
    bool          worldSpace = false;
    std::uint16_t buttons   = 0;
    float         leftX     = 0.0f;
    float         leftY     = 0.0f;
    float         rightX    = 0.0f;
    float         rightY    = 0.0f;
};

static GamepadState g_gamepad[2] = {};
static GamepadState g_mailboxFriendPad[2] = {};
static GamepadState g_primaryMailboxPad = {};
static std::uint16_t g_primaryRawButtons = 0;
static int          g_inputControllerCount = 0;
static int          g_activeInputSlot = -1;

// Solo test mode — F5 toggles this.
// When active: gamepad 0 controls Friend1, Sora's input is suppressed.
// Emulates "being player 2" with a single controller.
static bool g_soloTestMode     = false;
static bool g_f5WasDown        = false;   // edge detection for F5 key

// In-process camera retargeting — points camStruct+0x50 at the friend actor.
// Much simpler than the runtime process approach (no fake actor allocation)
// because we can redirect to the REAL friend actor object directly.
static uintptr_t g_origCameraActorPtr = 0;
static bool      g_cameraRetargeted   = false;

// Per-frame processed-stick snapshot. Must be captured BEFORE
// SuppressSoraInput zeros the processed entry.
static GamepadState g_processedStickSnapshot = {};
static uint32_t     g_processedStickFrame    = UINT32_MAX;

// Per-friend cached facing angle. When the stick is released, we keep writing
// the last known facing so the vanilla AI can't snap the friend toward Sora.
// Index 0 = Friend1, Index 1 = Friend2.
static float g_lastFacingAngle[2]  = {0.0f, 0.0f};
static bool  g_facingAngleValid[2] = {false, false};

// Last stick magnitude per friend — used to select animation in HookedFriendAI.
static float g_lastStickMagnitude[2] = {0.0f, 0.0f};

// Last animation we set via the override — avoids resetting the same animation
// every frame (which would restart it from frame 0 and look broken).
// -1 means "not set yet".
static int g_lastOverrideAnim[2] = {-1, -1};

// Animation override diagnostic counters
static uint32_t g_animOverrideCount  = 0;
static uint32_t g_animOverrideLogFrame = 0;

// Guard flag: set to true while OUR code is calling FUN_1403c86a0 to set
// animation on a controlled friend. This lets HookedMotionChainSetAnim
// distinguish our intentional animation changes from the game's per-frame
// FUN_1403c88c0 calls (which reset the animation time and cause the
// "stuck at frame 0" bug). All calls except ours are blocked.
static bool g_inOurAnimSet = false;

// Sora actor pointer — needed for suppressing Sora's movement at entity level
static uintptr_t g_soraActor = 0;

// Diagnostics
static uint32_t g_frameCounter  = 0;
static bool     g_initialized   = false;
static FILE*    g_logFile        = nullptr;
static uint32_t g_lastMovementLogFrame = 0;

// Network input mailbox — shared memory bridge from the runtime process.
// When available, overrides local gamepad reads with network-received InputFrames.
static kh2coop::MailboxReader g_mailboxReader;

// Avatar telemetry to the runtime (VUH-1490) and puppet poses back (VUH-1491).
static kh2coop::AvatarBridge g_avatarBridge;

// ============================================================================
// Puppet driver (VUH-1491): friend slots 1/2 follow a remote avatar stream
// from AvatarBridge::TryReadPuppet instead of their AI. Reuses the Session 5
// motion control: skip the AI, set the motion only when it changes (our
// call passes the HookedMotionChainSetAnim guard), let the motCtrl tick
// advance it, and correct the clock when it drifts. Position/rotation are
// written after the entity's own update, with velocity zeroed and the
// follow timer held so physics can't pull the puppet back toward Sora.
// ============================================================================

static void Log(const char* fmt, ...);

struct PuppetDriver {
    kh2coop::PuppetPose pose {};
    bool have = false;           // a pose has arrived
    uint32_t poseFrame = 0;      // DLL frame of the latest new pose
    uintptr_t actor = 0;         // actor we last drove (re-resolved per frame)
    int lastAnim = -1;           // motion we last set on `actor`
    uint32_t savedTeam = 0;      // `actor`'s team before we zeroed it
    bool teamSaved = false;
    bool savedNoCollide = false; // `actor`'s no-collision bit before we set it
    bool noCollideSaved = false;
    bool applied = false;       // prior native drive, independent of current pose permission
    nativehittrace::ActorSnapshot boundActor {}; // checked metadata for bounded release
};
static PuppetDriver g_puppets[2];
static bool g_inPuppetAnimSet = false;

// Team 0 can't be hit: no attack's hit mask includes bit 0 (repos-60's
// static analysis, VUH-1491). Party members are team 1, enemies team 2.
static constexpr uintptr_t ACTOR_TEAM = 0x4DC;
// actor+0x18C bit 6 skips actor separation and terrain collision for this
// actor only (repos-60, VUH-1502/1492). Puppets set it so the game's per-frame
// push-out stops fighting the pose we write. The objentry has a similar bit
// shared by every actor of that model: never touch that one.
static constexpr uintptr_t ACTOR_COLLISION_FLAGS = 0x18C;
static constexpr uint8_t ACTOR_NO_COLLIDE = 0x40;
// Sora's drive gauge (real slot base, Steam Global). Holding it at 0 blocks
// Drive forms and Summons, which consume or animate party members.
static constexpr uint64_t SORA_DRIVE_BARS = 0x2A23749;     // u8
static constexpr uint64_t SORA_DRIVE_PARTIAL = 0x2A23748;  // u8
static bool g_driveHeld = false;
static uint8_t g_savedDriveBars = 0;
static uint8_t g_savedDrivePartial = 0;
static bool g_anyPuppetActive = false;

// Limits grey out while puppets are active: a limit's cutscene would grab
// the partner actor, which is now a puppet (repos-60's static trace).
//   0x3D88E0(actor, cmd*, state) -> menu state for a command (5 = greyed,
//            what the game returns while a limit runs or MP recharges)
//   0x3E7800(cmdId) -> limt entry if the limit is usable now, else 0; the
//            execute path 0x3D8B40 re-checks it
//   0x3E7C30(cmdId) -> limt entry for a command id, or 0 (plain lookup)
using PFN_LimitMenuState = int(__fastcall*)(void* actor, uint16_t* cmd, int state);
using PFN_LimitLookup = uintptr_t(__fastcall*)(uint32_t cmdId);
static constexpr uint64_t RVA_LIMIT_MENU_STATE = 0x3D88E0;
static constexpr uint64_t RVA_LIMIT_USABLE = 0x3E7800;
static constexpr uint64_t RVA_LIMIT_BY_CMD = 0x3E7C30;
static constexpr uint8_t kLimitMenuStateBytes[] = {
    0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57};
static constexpr uint8_t kLimitUsableBytes[] = {
    0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57};
static constexpr uint8_t kLimitByCmdBytes[] = {
    0x4c, 0x8b, 0x1d, 0x59, 0xda, 0x6f, 0x02, 0x45, 0x33, 0xc9, 0x4d, 0x63, 0x53, 0x04, 0x4d, 0x85};
static constexpr int LIMIT_STATE_GREYED = 5;
static PFN_LimitMenuState g_origLimitMenuState = nullptr;
static PFN_LimitLookup g_origLimitUsable = nullptr;
static PFN_LimitLookup g_limitByCmd = nullptr;

// ApplyStatDelta 0x3D2EB0(actor, delta, idx, reactFlag) -> new value: the
// single funnel for HP changes (repos-60's decompile, VUH-1501). idx 0 =
// HP, damage is a negative delta, HP reaching 0 runs vtable+0xB0 (death).
// Logged with a return-address stack so the attacker-side resolver above
// the TakeDamage thunk can be identified.
// Diagnostic reads are independent of native-call exception handling. A failed
// read only clears availability; it must never turn into a successful hit.
static bool HitTraceAddress(uintptr_t address, std::size_t size) {
    constexpr uintptr_t end = 0x7FFFFFFFFFFFULL;
    return address > 0x10000 && address < end && size <= end - address;
}
static bool ReadHitTraceMemory(uintptr_t address, void* out, std::size_t size) {
    if (!HitTraceAddress(address, size)) return false;
    __try {
        std::memcpy(out, reinterpret_cast<const void*>(address), size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template <typename T> static bool ReadHitTrace(uintptr_t address, T& out) {
    return ReadHitTraceMemory(address, &out, sizeof(out));
}
static std::uint32_t g_hitTraceImageSize = 0;
static bool HitTraceCaller(uintptr_t caller, uintptr_t& rva) {
    if (!g_hitTraceImageSize || caller < g_exeBase || caller - g_exeBase >= g_hitTraceImageSize) return false;
    rva = caller - g_exeBase;
    return true;
}
static nativehittrace::ActorSnapshot CaptureHitActor(uintptr_t actor) {
    using namespace nativehittrace;
    ActorSnapshot out {};
    out.actor = actor;
    if (!HitTraceAddress(actor, offsets::actor::OBJENTRY_PTR + sizeof(uintptr_t))) return out;
    if (ReadHitTrace(actor + offsets::actor::OBJENTRY_PTR, out.objentry)) out.readMask |= ActorObject;
    if (ReadHitTrace(actor + 0x5C0, out.status)) out.readMask |= ActorStatus;
    if (HitTraceAddress(out.objentry, offsets::objentry::NAME + sizeof(out.namePrefix))) {
        if (ReadHitTrace(out.objentry + offsets::objentry::TYPE_FLAGS, out.type)) out.readMask |= ActorType;
        if (ReadHitTrace(out.objentry + offsets::objentry::OBJECT_ID, out.objectId)) out.readMask |= ActorId;
        if (ReadHitTrace(out.objentry + offsets::objentry::NAME, out.namePrefix)) out.readMask |= ActorName;
    }
    if (ReadHitTrace(actor + 0x4DC, out.team)) out.readMask |= ActorTeam;
    if (HitTraceAddress(out.status, 8)) {
        if (ReadHitTrace(out.status, out.hp)) out.readMask |= ActorHp;
        if (ReadHitTrace(out.status + 4, out.maxHp)) out.readMask |= ActorMaxHp;
    }
    uintptr_t obj = 0, status = 0;
    std::uint32_t id = 0, team = 0;
    std::uint16_t namePrefix = 0;
    std::uint8_t type = 0;
    if (out.objentry && out.status &&
        (out.readMask & (ActorObject | ActorStatus | ActorType | ActorId | ActorTeam | ActorName)) ==
                       (ActorObject | ActorStatus | ActorType | ActorId | ActorTeam | ActorName) &&
        ReadHitTrace(actor + offsets::actor::OBJENTRY_PTR, obj) && obj == out.objentry &&
        ReadHitTrace(actor + 0x5C0, status) && status == out.status &&
        ReadHitTrace(obj + offsets::objentry::OBJECT_ID, id) && id == out.objectId &&
        ReadHitTrace(obj + offsets::objentry::TYPE_FLAGS, type) && type == out.type &&
        ReadHitTrace(obj + offsets::objentry::NAME, namePrefix) && namePrefix == out.namePrefix &&
        ReadHitTrace(actor + 0x4DC, team) && team == out.team) out.readMask |= ActorRepeated;
    return out;
}

using PFN_ApplyStatDelta = int(__fastcall*)(void* actor, int delta, int idx, int reactFlag);
static constexpr uint64_t RVA_APPLY_STAT_DELTA = 0x3D2EB0;
static constexpr uint8_t kApplyStatDeltaBytes[] = {
    0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c, 0x24, 0x18, 0x56, 0x48, 0x83, 0xec, 0x20, 0x48};
static PFN_ApplyStatDelta g_origApplyStatDelta = nullptr;
static uint32_t g_hpDamageLogged = 0;
static constexpr uint32_t HP_DAMAGE_LOG_LIMIT = 40;

static void LogHpDelta(void* actor, int delta, int idx, int reactFlag, int result) {
    char name[33] = "?";
    const auto obj = *reinterpret_cast<const uintptr_t*>(
        reinterpret_cast<uintptr_t>(actor) + offsets::actor::OBJENTRY_PTR);
    uint8_t type = 0xFF;
    if (obj > g_exeBase && obj < g_exeBase + 0x3000000) {
        std::memcpy(name, reinterpret_cast<const void*>(obj + offsets::objentry::NAME), 32);
        name[32] = '\0';
        type = *reinterpret_cast<const uint8_t*>(obj + offsets::objentry::TYPE_FLAGS);
    }
    void* frames[12] = {};
    const USHORT n = RtlCaptureStackBackTrace(1, 12, frames, nullptr);
    char stack[256] = {};
    size_t used = 0;
    for (USHORT i = 0; i < n && used < sizeof(stack) - 16; ++i) {
        const auto f = reinterpret_cast<uintptr_t>(frames[i]);
        if (f >= g_exeBase && f < g_exeBase + 0x3000000) {
            used += std::snprintf(stack + used, sizeof(stack) - used, " %llX",
                                  static_cast<unsigned long long>(f - g_exeBase));
        }
    }
    Log("[hp] frame %u victim=%s type=%u actor=%p idx=%d delta=%d react=%d -> %d | stack%s",
        g_frameCounter, name, type, actor, idx, delta, reactFlag, result, stack);
}

static int ApplyStatDeltaBody(void* actor, int delta, int idx, int reactFlag) {
    const int result = g_origApplyStatDelta(actor, delta, idx, reactFlag);
    if (idx == 0 && delta < 0 && g_hpDamageLogged < HP_DAMAGE_LOG_LIMIT) {
        ++g_hpDamageLogged;
        __try {
            LogHpDelta(actor, delta, idx, reactFlag, result);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log("[hp] exception while logging a hit");
        }
    }
    return result;
}

#include "DownedSpike.inl"

static int __fastcall HookedApplyStatDelta(void* actor, int delta, int idx, int reactFlag) {
    delta = downedspike::StatDelta(actor, delta, idx);
    using namespace nativehittrace;
    if (!CanCaptureChild()) return ApplyStatDeltaBody(actor, delta, idx, reactFlag);
    const auto address = reinterpret_cast<uintptr_t>(actor);
    uintptr_t callerRva = 0;
    const bool callerAvailable = HitTraceCaller(reinterpret_cast<uintptr_t>(_ReturnAddress()), callerRva);
    ChildToken token {};
    const ActorSnapshot before = CaptureHitActor(address);
    BeginStat(token, address, delta, idx, reactFlag, callerRva, callerAvailable, before);
    ActorSnapshot after {};
    int result = 0;
    bool normal = false;
    __try {
        result = ApplyStatDeltaBody(actor, delta, idx, reactFlag);
        normal = true;
        if (token.active) after = CaptureHitActor(address);
    } __finally {
        EndStat(token, normal && !AbnormalTermination(), result,
                normal && !AbnormalTermination() ? &after : nullptr);
    }
    return result;
}

// ----------------------------------------------------------------------------
// Hit log (VUH-1501 criterion 1), from repos-60's decompile:
//   0x3D1730(ATTACK** atk, victim, contact) = ResolveHit. A = *atk:
//            A+0x10 owner (attacker) handle, A+0x30 -> atkp entry (id u16 +2)
//   0x3D23C0(A, victim, u16, u8) -> hit record: damage i32 +0x28, stat u8 +0x25
// Attacker handles are resolved with the engine's handle lookup (below).
// ----------------------------------------------------------------------------
// The damage calculation receives the ATTACK object itself, so hooking it
// alone gives attacker, atkp, victim and damage.
using PFN_BuildHit = uintptr_t(__fastcall*)(void* atk, void* victim, uint32_t a3, uint32_t a4);
static constexpr uint64_t RVA_BUILD_HIT = 0x3D23C0;
static constexpr uint8_t kBuildHitBytes[] = {
    0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57};
static PFN_BuildHit g_origBuildHit = nullptr;
static uint32_t g_hitsLogged = 0;
static constexpr uint32_t HIT_LOG_LIMIT = 60;

struct HandleEntry { uint32_t handle; uintptr_t actor; };
static HandleEntry g_handleMap[128];
static int g_handleCount = 0;
static void BeginHandleFrame() { g_handleCount = 0; }
// Attack owner handles carry the actor address's low 24 bits (plus a tag in
// the top byte): live, Sora at ...612AB820 attacked with handle 852AB820.
// The engine resolves them with 0x4AD270(handle) -> object (repos-60); the
// per-frame low-24-bit map is the fallback if that function doesn't match.
using PFN_ResolveHandle = uintptr_t(__fastcall*)(uint32_t handle);
static constexpr uint64_t RVA_RESOLVE_HANDLE = 0x4AD270;
static constexpr uint8_t kResolveHandleBytes[] = {
    0x85, 0xc9, 0x75, 0x03, 0x33, 0xc0, 0xc3, 0xe9, 0x74, 0x01, 0x00, 0x00};
static PFN_ResolveHandle g_resolveHandle = nullptr;

static void NoteActorHandle(uintptr_t actor) {
    if (g_handleCount < 128) {
        g_handleMap[g_handleCount++] = {static_cast<uint32_t>(actor & 0xFFFFFF), actor};
    }
}
static uintptr_t ActorForHandle(uint32_t handle) {
    if (g_resolveHandle) return g_resolveHandle(handle);
    for (int i = 0; i < g_handleCount; ++i) {
        if (g_handleMap[i].handle == (handle & 0xFFFFFF)) return g_handleMap[i].actor;
    }
    return 0;
}

// Copies an actor's objentry name (or "?") into out[32].
static void ActorName(uintptr_t actor, char* out) {
    std::strcpy(out, "?");
    if (actor == 0) return;
    const auto obj = *reinterpret_cast<const uintptr_t*>(actor + offsets::actor::OBJENTRY_PTR);
    if (obj > g_exeBase && obj < g_exeBase + 0x3000000) {
        std::memcpy(out, reinterpret_cast<const void*>(obj + offsets::objentry::NAME), 31);
        out[31] = '\0';
    }
}

// The most recent hit record, so ApplyHitDamage (called with the record
// right after it's built) knows who dealt it.
struct LastHit { uintptr_t hit; uintptr_t victim; uintptr_t attacker; uint32_t atkpId; };
static LastHit g_lastHit = {};

static uintptr_t __fastcall HookedBuildHit(void* atk, void* victim, uint32_t a3, uint32_t a4) {
    const uintptr_t hit = g_origBuildHit(atk, victim, a3, a4);
    if (hit == 0) return hit;
    downedspike::NoteHit(atk, victim);
    __try {
        const auto A = reinterpret_cast<uintptr_t>(atk);
        const uint32_t ownerHandle = *reinterpret_cast<const uint32_t*>(A + 0x10);
        const uintptr_t atkp = *reinterpret_cast<const uintptr_t*>(A + 0x30);
        const uint32_t atkpId = atkp ? *reinterpret_cast<const uint16_t*>(atkp + 2) : 0xFFFF;
        const uintptr_t attackerActor = ActorForHandle(ownerHandle);
        g_lastHit = {hit, reinterpret_cast<uintptr_t>(victim), attackerActor, atkpId};
        if (g_hitsLogged >= HIT_LOG_LIMIT) return hit;
        const int32_t damage = *reinterpret_cast<const int32_t*>(hit + 0x28);
        const uint8_t stat = *reinterpret_cast<const uint8_t*>(hit + 0x25);
        char attacker[32], target[32];
        ActorName(attackerActor, attacker);
        ActorName(reinterpret_cast<uintptr_t>(victim), target);
        const int32_t* hp = nullptr;
        const auto status = *reinterpret_cast<const uintptr_t*>(
            reinterpret_cast<uintptr_t>(victim) + 0x5C0);
        if (status > 0x10000) hp = reinterpret_cast<const int32_t*>(status);
        ++g_hitsLogged;
        Log("[hit] frame %u attacker=%s (handle %08X) atkp=%u -> victim=%s@%llX damage=%d stat=%u hpBefore=%d",
            g_frameCounter, attacker, ownerHandle, atkpId, target,
            static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(victim)),
            damage, stat, hp ? *hp : -1);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("[hit] exception while logging");
    }
    return hit;
}

// ----------------------------------------------------------------------------
// Hit ownership (VUH-1501): drop filter + claims, host apply, lethal blow.
// See kh2coop/HitChannel.hpp.
//   0x3D3BA0(victim, hit) ApplyHitDamage: consumes hit+0x28 (first checks
//            and sets bit 1 of hit+0x18, the "applied" flag).
// ----------------------------------------------------------------------------
using PFN_ApplyHitDamage = uintptr_t(__fastcall*)(void* victim, void* hit);
static constexpr uint64_t RVA_APPLY_HIT_DAMAGE = 0x3D3BA0;
static constexpr uint8_t kApplyHitDamageBytes[] = {
    0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xf1, 0x48, 0x8b, 0xfa};
static PFN_ApplyHitDamage g_origApplyHitDamage = nullptr;
static HANDLE g_hitMapping = nullptr;
static HitChannel* g_hitChannel = nullptr;
static uint32_t g_dropsLogged = 0;

static int32_t ActorHp(uintptr_t actor) {
    const auto status = *reinterpret_cast<const uintptr_t*>(actor + 0x5C0);
    return status > 0x10000 ? *reinterpret_cast<const int32_t*>(status) : -1;
}

static bool IsEnemyVictim(uintptr_t actor) {
    const auto obj = *reinterpret_cast<const uintptr_t*>(actor + offsets::actor::OBJENTRY_PTR);
    if (obj <= g_exeBase || obj >= g_exeBase + 0x3000000) return false;
    const uint8_t type = *reinterpret_cast<const uint8_t*>(obj + offsets::objentry::TYPE_FLAGS);
    return type == offsets::objentry::TYPE_BOSS || type == offsets::objentry::TYPE_MOB;
}

static bool SyncDropsHit(void* victim) {
    __try {
        return enemysync::DropLocalEnemyDamage(reinterpret_cast<uintptr_t>(victim));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// This snapshot is a calculated ordinary HP hit, not observed HP loss.
// Keep native reads in a POD-only SEH leaf; EnemySync validates the current
// session/binding outside this boundary. g_lastHit and the diagnostic
// low-address handle fallback cannot establish authoritative ownership.
static bool CaptureLocalPlayerEnemyHit(uintptr_t victim, uintptr_t hit,
                                       enemysync::LocalPlayerEnemyHit& out) {
    out = {};
    if (!spawncontroller::IsDiagnosticGameThread()) return false;
    if (!victim || !hit || !g_resolveHandle) return false;
    __try {
        if ((*reinterpret_cast<const uint32_t*>(hit + 0x18) & 2u) != 0 ||
            *reinterpret_cast<const uint8_t*>(hit + 0x25) != 0) return false;
        const int32_t damage = *reinterpret_cast<const int32_t*>(hit + 0x28);
        if (damage <= 0 || !IsEnemyVictim(victim) || ActorHp(victim) <= 0) return false;
        const uintptr_t hitAtkp = g_resolveHandle(*reinterpret_cast<const uint32_t*>(hit + 0x20));
        if (!hitAtkp) return false;
        const uint8_t attackType = *reinterpret_cast<const uint8_t*>(hitAtkp + 4);
        // ApplyHitDamage keeps the positive delta for these healing types.
        if (attackType == 5 || attackType == 6) return false;
        const uintptr_t attack = g_resolveHandle(*reinterpret_cast<const uint32_t*>(hit + 0x1C));
        if (!attack) return false;
        const uintptr_t owner = g_resolveHandle(*reinterpret_cast<const uint32_t*>(attack + 0x10));
        static constexpr uintptr_t RVA_NATIVE_PLAYER = 0x2A105D0;
        const uintptr_t player = *reinterpret_cast<const uintptr_t*>(g_exeBase + RVA_NATIVE_PLAYER);
        const uintptr_t head = *reinterpret_cast<const uintptr_t*>(g_exeBase + offsets::active_entity_list::HEAD);
        // Remote Sora copies have the same name/type. Require the canonical
        // player and current head tracked by our game-thread update hook.
        if (!owner || owner != player || owner != head || owner != g_soraActor) return false;
        const uintptr_t atkp = *reinterpret_cast<const uintptr_t*>(attack + 0x30);
        if (!atkp) return false;
        const uint32_t attackId = *reinterpret_cast<const uint16_t*>(atkp + 2);
        const auto position = owner + offsets::actor::ENTITY_TRANSFORM + offsets::entity::POS_X;
        const float x = *reinterpret_cast<const float*>(position);
        const float y = *reinterpret_cast<const float*>(position + sizeof(float));
        const float z = *reinterpret_cast<const float*>(position + 2 * sizeof(float));
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
        out = {victim, owner, attackId, damage, {x, y, z}};
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out = {};
        return false;
    }
}

// This is a diagnostic identity lookup only. It never replays a hit and never
// uses the diagnostic low-address actor map as authority.
static bool ResolveHitTraceHandle(std::uint32_t handle, uintptr_t& out) {
    out = 0;
    if (!g_resolveHandle) return false; // Installed only after the resolver byte guard.
    __try { out = g_resolveHandle(handle); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static nativehittrace::ApplyFacts CaptureHitFacts(uintptr_t victim, uintptr_t hit, bool afterCall = false,
                                                bool captureContext = true) {
    using namespace nativehittrace;
    ApplyFacts out {};
    // Bracket all diagnostic reads and the native call with the two contexts.
    if (captureContext && !afterCall) out.context = enemysync::CaptureNativeHitContext();
    out.victim = CaptureHitActor(victim);
    auto& h = out.hit;
    h.hit = hit;
    if (HitTraceAddress(hit, 0x40)) {
        if (ReadHitTrace(hit + 0x18, h.flags)) h.readMask |= HitFlags;
        if (ReadHitTrace(hit + 0x25, h.stat)) h.readMask |= HitStat;
        if (ReadHitTrace(hit + 0x28, h.damage)) h.readMask |= HitDamage;
        uintptr_t atkp = 0;
        if (ReadHitTrace(hit + 0x20, h.atkpHandle) &&
            ResolveHitTraceHandle(h.atkpHandle, atkp) && HitTraceAddress(atkp, 5) &&
            ReadHitTrace(atkp + 4, h.kind)) {
            std::uint32_t checkHandle = 0;
            std::uint8_t checkKind = 0;
            if (ReadHitTrace(hit + 0x20, checkHandle) && checkHandle == h.atkpHandle &&
                ReadHitTrace(atkp + 4, checkKind) && checkKind == h.kind) h.readMask |= HitKind;
        }
        if (ReadHitTrace(hit + 0x1C, h.attackHandle) &&
            ResolveHitTraceHandle(h.attackHandle, h.attack) && HitTraceAddress(h.attack, 0x38)) {
            std::uint32_t checkHandle = 0;
            if (ReadHitTrace(hit + 0x1C, checkHandle) && checkHandle == h.attackHandle) h.readMask |= HitAttack;
            if (ReadHitTrace(h.attack + 0x10, h.ownerHandle) &&
                ResolveHitTraceHandle(h.ownerHandle, h.owner) && h.owner) {
                out.source = CaptureHitActor(h.owner);
                std::uint32_t checkOwner = 0;
                if ((h.readMask & HitAttack) &&
                    ReadHitTrace(h.attack + 0x10, checkOwner) && checkOwner == h.ownerHandle &&
                    ReadHitTrace(hit + 0x1C, checkHandle) && checkHandle == h.attackHandle) h.readMask |= HitOwner;
            }
            uintptr_t attackParams = 0, checkParams = 0;
            std::uint16_t id = 0, checkId = 0;
            if (ReadHitTrace(h.attack + 0x30, attackParams) && HitTraceAddress(attackParams, 4) &&
                ReadHitTrace(attackParams + 2, id) &&
                ReadHitTrace(h.attack + 0x30, checkParams) && checkParams == attackParams &&
                ReadHitTrace(attackParams + 2, checkId) && checkId == id &&
                ReadHitTrace(hit + 0x1C, checkHandle) && checkHandle == h.attackHandle) {
                h.attackId = id;
                h.readMask |= HitAttackId;
            }
        }
    }
    h.tracked = g_soraActor;
    uintptr_t player = 0, head = 0;
    if (ReadHitTrace(g_exeBase + 0x2A105D0, h.canonicalPlayer) &&
        ReadHitTrace(g_exeBase + offsets::active_entity_list::HEAD, h.head) &&
        ReadHitTrace(g_exeBase + 0x2A105D0, player) && player == h.canonicalPlayer &&
        ReadHitTrace(g_exeBase + offsets::active_entity_list::HEAD, head) && head == h.head &&
        h.tracked == g_soraActor) h.readMask |= HitCanonical;
    if (captureContext && afterCall) out.context = enemysync::CaptureNativeHitContext();
    return out;
}

struct DamageObservation {
    damagepolicy::Facts facts {};
    damagepolicy::Decision decision {};
    nativehittrace::Context context {};
    nativehittrace::ActorSnapshot victim {}, source {};
    nativehittrace::HitSnapshot hit {};
    std::uint64_t roster[3] {};
};
static DamageObservation CaptureDamagePolicy(uintptr_t victim, uintptr_t hit);
static bool DamageObservationCurrent(const DamageObservation& observation);

static uintptr_t ApplyHitDamageBody(void* victim, void* hit, nativehittrace::HitSnapshot* observation,
                                   nativehittrace::PolicyObservation* policyObservation) {
    const DamageObservation authority = CaptureDamagePolicy(reinterpret_cast<uintptr_t>(victim),
                                                            reinterpret_cast<uintptr_t>(hit));
    if (policyObservation) {
        *policyObservation = {};
        policyObservation->authority.context = authority.context;
        policyObservation->authority.victim = authority.victim;
        policyObservation->authority.source = authority.source;
        policyObservation->authority.hit = authority.hit;
        policyObservation->facts = authority.facts;
        policyObservation->decision = authority.decision;
        std::memcpy(policyObservation->roster, authority.roster, sizeof(authority.roster));
        policyObservation->recorded = true;
    }
    HitChannel* ch = g_hitChannel;
    const bool filterOn = ch && ch->dropEnabled;
    // A client in a synced session never changes an enemy's HP itself: the
    // host owns it (VUH-1502); the hit still plays its local reaction.
    const bool syncDrop = SyncDropsHit(victim);
    if (observation) {
        observation->manualFilterOn = filterOn;
        observation->syncDrop = syncDrop;
    }
    enemysync::LocalPlayerEnemyHit localHit {};
    const bool claimCandidate = authority.decision.supported
        ? authority.decision.action == damagepolicy::Action::ClaimThenZeroHp : syncDrop;
    if (claimCandidate && CaptureLocalPlayerEnemyHit(reinterpret_cast<uintptr_t>(victim),
                                              reinterpret_cast<uintptr_t>(hit), localHit)) {
        // Rejection never re-enables client HP authority. This noexcept
        // callback retains copied values only, never the native hit record.
        if (policyObservation) policyObservation->claimAttempted = true;
        const bool queued = enemysync::RecordLocalPlayerEnemyHit(localHit);
        if (policyObservation) policyObservation->claimQueued = queued;
    }
    if (filterOn || syncDrop) {
        __try {
            const auto v = reinterpret_cast<uintptr_t>(victim);
            const auto h = reinterpret_cast<uintptr_t>(hit);
            const uintptr_t attacker =
                (g_lastHit.hit == h && g_lastHit.victim == v) ? g_lastHit.attacker : 0;
            auto* damage = reinterpret_cast<int32_t*>(h + 0x28);
            const bool match = *damage > 0 && (syncDrop || (filterOn
                && (ch->dropAttacker == 0 || ch->dropAttacker == attacker)
                && (ch->dropVictim == 0 || ch->dropVictim == v)
                && (!ch->enemyVictimsOnly || IsEnemyVictim(v))));
            if (match && !ch) {
                *damage = 0;
            } else if (match) {
                const long n = InterlockedIncrement(&ch->claimCount) - 1;
                HitClaim& c = ch->claims[n % HIT_CLAIM_CAPACITY];
                c.frame = g_frameCounter;
                c.atkpId = g_lastHit.hit == h ? g_lastHit.atkpId : 0xFFFF;
                c.attacker = attacker;
                c.victim = v;
                c.damage = *damage;
                c.victimHp = ActorHp(v);
                if (g_dropsLogged++ < HIT_LOG_LIMIT) {
                    char a[32], t[32];
                    ActorName(attacker, a);
                    ActorName(v, t);
                    Log("[drop] frame %u %s atkp=%u -> %s@%llX damage %d zeroed (claim #%ld, hp %d)",
                        g_frameCounter, a, c.atkpId, t, static_cast<unsigned long long>(v),
                        c.damage, n, c.victimHp);
                }
                *damage = 0;
            }
            // syncDrop short-circuits the manual selectors in the original
            // expression. Do not claim they matched when they were not tested.
            if (observation && match && filterOn && !syncDrop) observation->manualDrop = true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log("[drop] exception in filter");
        }
    }
    // New authority vetoes never enter the manual HitChannel or synthesize a
    // network claim. The native call below still consumes the record and owns
    // its genuine result and remaining effects. A changed/unreadable record is
    // not reported as suppressed and is never repaired with a helper replay.
    if (authority.decision.supported && authority.decision.action != damagepolicy::Action::Native) {
        if (policyObservation) policyObservation->revalidationAttempted = true;
        const bool current = DamageObservationCurrent(authority);
        if (policyObservation) policyObservation->revalidationPassed = current;
        if (current) {
            if (policyObservation) policyObservation->zeroAttempted = true;
            const auto zeroResult = damagepolicy::TryZeroHp(reinterpret_cast<uintptr_t>(hit), authority.facts.hit);
            if (policyObservation) policyObservation->zeroResult = zeroResult;
        }
    }
    return g_origApplyHitDamage(victim, hit);
}

static uintptr_t __fastcall HookedApplyHitDamage(void* victim, void* hit) {
    using namespace nativehittrace;
    if (!CanCaptureApply()) return ApplyHitDamageBody(victim, hit, nullptr, nullptr);
    const auto actor = reinterpret_cast<uintptr_t>(victim);
    const auto record = reinterpret_cast<uintptr_t>(hit);
    uintptr_t callerRva = 0;
    const bool callerAvailable = HitTraceCaller(reinterpret_cast<uintptr_t>(_ReturnAddress()), callerRva);
    ApplyToken token {};
    const ApplyFacts before = CaptureHitFacts(actor, record);
    BeginApply(token, callerRva, callerAvailable, before);
    ApplyFacts after {};
    uintptr_t result = 0;
    bool normal = false;
    __try {
        result = ApplyHitDamageBody(victim, hit, token.active ? &token.event.before.hit : nullptr,
                                   token.active ? &token.event.policy : nullptr);
        normal = true;
        if (token.active) {
            after = CaptureHitFacts(actor, record, true);
            after.hit.syncDrop = token.event.before.hit.syncDrop;
            after.hit.manualFilterOn = token.event.before.hit.manualFilterOn;
            after.hit.manualDrop = token.event.before.hit.manualDrop;
        }
    } __finally {
        EndApply(token, normal && !AbnormalTermination(), result,
                 normal && !AbnormalTermination() ? &after : nullptr);
    }
    return result;
}

static bool IsLiveActor(uintptr_t actor) {
    for (int i = 0; i < g_handleCount; ++i) {
        if (g_handleMap[i].actor == actor) return true;
    }
    return false;
}

// Runs a pending apply request on the game thread. Called at frame start,
// after the previous frame's actor map is complete (before it's reset).
static void ProcessHitRequest() {
    HitChannel* ch = g_hitChannel;
    if (!ch || ch->requestSeq == ch->doneSeq) return;
    const auto victim = static_cast<uintptr_t>(ch->victim);
    HitStatus status = HitStatus::Ok;
    ch->hpBefore = ch->hpAfter = -1;
    if (!IsLiveActor(victim)) {
        status = HitStatus::UnknownVictim;
    } else {
        __try {
            ch->hpBefore = ActorHp(victim);
            // No status block (HP reads -1): not a damageable actor, e.g.
            // B_MU110 in 08/00 is typed as a boss but has no stats.
            if (ch->hpBefore < 0) {
                status = HitStatus::NoStats;
            } else
            switch (static_cast<HitOp>(ch->op)) {
            case HitOp::Damage:
                if (!g_origMovementDispatch) { status = HitStatus::Unavailable; break; }
                // TakeDamage(actor, -damage, HP, react) — the path a real hit takes.
                g_origMovementDispatch(reinterpret_cast<void*>(victim), -ch->amount, 0, 1);
                break;
            case HitOp::Lethal:
                if (!g_origApplyStatDelta) { status = HitStatus::Unavailable; break; }
                g_origApplyStatDelta(reinterpret_cast<void*>(victim), -ch->hpBefore, 0, 0);
                break;
            default:
                status = HitStatus::BadOp;
            }
            ch->hpAfter = ActorHp(victim);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log("[apply] exception applying op %u", ch->op);
        }
    }
    char name[32];
    ActorName(status == HitStatus::UnknownVictim ? 0 : victim, name);
    Log("[apply] frame %u op=%u amount=%d -> %s@%llX status=%d hp %d -> %d", g_frameCounter,
        ch->op, ch->amount, name, static_cast<unsigned long long>(victim),
        static_cast<int>(status), ch->hpBefore, ch->hpAfter);
    ch->status = static_cast<int32_t>(status);
    ch->frame = g_frameCounter;
    InterlockedExchange(&ch->doneSeq, ch->requestSeq);
}

static void OpenHitChannel() {
    wchar_t name[96];
    swprintf_s(name, L"%s%lu", HIT_NAME_PREFIX, GetCurrentProcessId());
    g_hitMapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                      sizeof(HitChannel), name);
    if (g_hitMapping) {
        g_hitChannel = static_cast<HitChannel*>(
            MapViewOfFile(g_hitMapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(HitChannel)));
    }
    if (!g_hitChannel) {
        Log("  WARNING: hit channel creation failed (%lu)", GetLastError());
        return;
    }
    std::memset(g_hitChannel, 0, sizeof(HitChannel));
    g_hitChannel->magic = HIT_MAGIC;
    g_hitChannel->version = HIT_VERSION;
    Log("  Hit channel open (Local\\kh2coop_hit_%lu)", GetCurrentProcessId());
}

static int __fastcall HookedLimitMenuState(void* actor, uint16_t* cmd, int state) {
    const int result = g_origLimitMenuState(actor, cmd, state);
    if (g_anyPuppetActive && cmd != nullptr && g_limitByCmd(*cmd) != 0) {
        return LIMIT_STATE_GREYED;
    }
    return result;
}

static uintptr_t __fastcall HookedLimitUsable(uint32_t cmdId) {
    if (g_anyPuppetActive && g_limitByCmd(cmdId & 0xFFFF) != 0) return 0;
    return g_origLimitUsable(cmdId);
}

// Release a puppet back to its AI when poses stop arriving (the runtime
// normally clears `active` itself; this covers a dead writer).
static constexpr uint32_t PUPPET_STALE_FRAMES = 120;
// Motions applied as-is. Others (Sora's attacks etc.) may not exist in the
// friend's moveset until the puppet model is decided (VUH-1489), so they
// fall back to the nearest basic motion instead of risking a bad id.
static constexpr uint32_t PUPPET_MAX_BASIC_MOTION = 8;
static constexpr uint32_t PUPPET_CLONE_UNSAFE_MOTION = 9;
static constexpr float PUPPET_TIME_DRIFT_FRAMES = 2.0f;
static constexpr uintptr_t ACTOR_MOTCTRL = 0x158;        // embedded motion controller
static constexpr uintptr_t MOTCTRL_CURRENT_TIME = 0x44;  // float frames
static constexpr uintptr_t ACTOR_VELOCITY = 0xB98;       // 3 floats
static constexpr uintptr_t ACTOR_CARRIED_DISPLACEMENT = 0x690;  // 3 floats, added per frame
static constexpr uintptr_t ACTOR_ACCEL_BLOCK = 0xA48;           // accel +0xA48..+0xA60
static constexpr size_t ACTOR_ACCEL_BLOCK_BYTES = 0x18;

// Puppet i (0/1) has a fresh, active pose.
// Room transitions: native request/load callbacks identify room generations,
// including same-room reloads. The current room's actors are about
// to be torn down. Puppets are suspended and every cached actor pointer is
// dropped without touching it; driving resumes on the first gameplay frame
// after the load (soak crash 2026-10-02: heap corruption after a door
// transition with a puppet active).
static bool g_puppetsSuspended = false;
static uint32_t g_puppetTransitionSerial = 0;
static uint32_t g_puppetLoadSerial = 0;

static bool IsPuppetActive(int index) {
    if (index < 0 || index > 1 || g_puppetsSuspended || warp::TransitionPending()) return false;
    const PuppetDriver& d = g_puppets[index];
    return d.have && d.pose.active && g_frameCounter - d.poseFrame <= PUPPET_STALE_FRAMES &&
        ValidPuppetProvenance(d.pose.provenance, static_cast<std::uint8_t>(d.pose.pose.ownerSlot),
                              index, enemysync::CapturePuppetAuthority());
}

// Owner-thread projection only: already captured avatar values, never extra native reads.
static void PublishCoopHud(const AvatarState& local, const PuppetAuthority& before) {
    hud::Input input;
    input.before = before;
    input.localAvailable = true;
    input.gameplayCurrent = !g_puppetsSuspended && !warp::TransitionPending();
    input.local = local;
    (void)g_avatarBridge.TryReadRosterNames(input.names);
    for (int i = 0; i < 2; ++i) {
        auto& remote = input.remote[static_cast<std::size_t>(i)];
        remote.active = IsPuppetActive(i);
        remote.provenance = g_puppets[i].pose.provenance;
        remote.avatar = g_puppets[i].pose.pose;
    }
    input.after = enemysync::CapturePuppetAuthority();
    render::PublishCoopHud(hud::Project(input, GetTickCount64(), g_frameCounter));
}

// Sora clones: player-class actors (objentry type 0) other than the real
// Sora, e.g. a Sora the world party table spawned into a friend slot
// (VUH-1489). They don't appear in the friend-slot pointers (with a clone
// in the party, slot 1's friend pointer is the next companion), so they're
// collected as entities update. g_clones is the last complete frame's list.
static uintptr_t g_clones[2] = {0, 0};
static uintptr_t g_clonesNow[2] = {0, 0};
static int g_cloneCountNow = 0;

static bool IsPlayerClassActor(uintptr_t actor) {
    const auto obj = *reinterpret_cast<const uintptr_t*>(actor + offsets::actor::OBJENTRY_PTR);
    if (obj <= g_exeBase || obj >= g_exeBase + 0x3000000) return false;
    return *reinterpret_cast<const uint8_t*>(obj + offsets::objentry::TYPE_FLAGS) == 0;
}

static void BeginCloneFrame() {
    g_clones[0] = g_clonesNow[0];
    g_clones[1] = g_clonesNow[1];
    g_clonesNow[0] = g_clonesNow[1] = 0;
    g_cloneCountNow = 0;
}

static void NoteActorForClones(uintptr_t actor) {
    if (warp::TransitionPending()) return;
    if (actor != g_soraActor && g_cloneCountNow < 2 && IsPlayerClassActor(actor)) {
        g_clonesNow[g_cloneCountNow++] = actor;
    }
}

// The actor puppet i drives: Sora clones when the room has any, otherwise
// the friend-slot actors (Donald/Goofy).
static uintptr_t PuppetTarget(int index) {
    if (g_clones[0] != 0) return g_clones[index];
    return index == 0 ? g_friend1Actor : g_friend2Actor;
}

static int PuppetIndexFor(uintptr_t actor) {
    if (actor == 0) return -1;
    for (int i = 0; i < 2; ++i) {
        if (PuppetTarget(i) == actor && IsPuppetActive(i)) return i;
    }
    return -1;
}

// Permission to treat a native friend as AI-owned is positive, frame-local
// evidence from the branch which actually invokes the original friend AI.
// Cached poses and a negative PuppetIndexFor result cannot grant permission.
struct NativeAiStamp {
    nativehittrace::Context context {};
    nativehittrace::ActorSnapshot actor {};
    std::uint64_t roster[3] {};
    uintptr_t friendPointer = 0;
    bool valid = false;
};
static NativeAiStamp g_nativeAiStamps[2] {};
static void ClearNativeAiStamps() { g_nativeAiStamps[0] = {}; g_nativeAiStamps[1] = {}; }

static bool SameDamageContext(const nativehittrace::Context& a, const nativehittrace::Context& b) {
    return a.available && b.available && a.readMask == nativehittrace::ContextComplete &&
        b.readMask == nativehittrace::ContextComplete && a.frame == b.frame && a.generation == b.generation &&
        a.epoch == b.epoch && a.transitionSerial == b.transitionSerial && a.loadSerial == b.loadSerial &&
        a.connectionId == b.connectionId && a.hostConnectionId == b.hostConnectionId &&
        a.role == b.role && a.slot == b.slot && std::memcmp(a.location, b.location, sizeof(a.location)) == 0;
}
static bool DamageObservationCurrent(const DamageObservation& observation) {
    if (!spawncontroller::IsDiagnosticGameThread()) return false;
    nativehittrace::Context context {}; std::uint64_t roster[3] {};
    if (!enemysync::CaptureDamageContext(context, roster) || !SameDamageContext(observation.context, context) ||
        std::memcmp(observation.roster, roster, sizeof(roster)) != 0) return false;
    const auto fresh = CaptureHitFacts(observation.victim.actor, observation.hit.hit, false, false);
    const auto sameKnownActor = [](const nativehittrace::ActorSnapshot& a, const nativehittrace::ActorSnapshot& b) {
        return a.actor == b.actor && (b.readMask & a.readMask) == a.readMask &&
            (!(a.readMask & nativehittrace::ActorObject) || a.objentry == b.objentry) &&
            (!(a.readMask & nativehittrace::ActorStatus) || a.status == b.status) &&
            (!(a.readMask & nativehittrace::ActorType) || a.type == b.type) &&
            (!(a.readMask & nativehittrace::ActorId) || a.objectId == b.objectId) &&
            (!(a.readMask & nativehittrace::ActorTeam) || a.team == b.team) &&
            (!(a.readMask & nativehittrace::ActorName) || a.namePrefix == b.namePrefix) &&
            (!(a.readMask & nativehittrace::ActorMaxHp) || a.maxHp == b.maxHp);
    };
    const auto& a = observation.hit; const auto& b = fresh.hit;
    using namespace nativehittrace;
    if (!sameKnownActor(observation.victim, fresh.victim) || (b.readMask & a.readMask) != a.readMask ||
        ((a.readMask & HitAttack) && (a.attackHandle != b.attackHandle || a.attack != b.attack)) ||
        ((a.readMask & HitOwner) && (a.ownerHandle != b.ownerHandle || a.owner != b.owner ||
                                    !sameKnownActor(observation.source, fresh.source))) ||
        ((a.readMask & HitKind) && (a.atkpHandle != b.atkpHandle || a.kind != b.kind)) ||
        ((a.readMask & HitAttackId) && a.attackId != b.attackId) ||
        ((a.readMask & HitCanonical) && (a.canonicalPlayer != b.canonicalPlayer || a.head != b.head || a.tracked != b.tracked))) return false;
    nativehittrace::Context finalContext {}; std::uint64_t finalRoster[3] {};
    return enemysync::CaptureDamageContext(finalContext, finalRoster) && SameDamageContext(context, finalContext) &&
        std::memcmp(roster, finalRoster, sizeof(roster)) == 0;
}
static bool SameDamageActor(const nativehittrace::ActorSnapshot& a, const nativehittrace::ActorSnapshot& b) {
    return a.readMask == nativehittrace::ActorComplete && b.readMask == nativehittrace::ActorComplete &&
        a.actor && a.objentry && a.status && a.actor == b.actor && a.objentry == b.objentry && a.status == b.status &&
        a.objectId == b.objectId && a.type == b.type && a.namePrefix == b.namePrefix &&
        a.maxHp == b.maxHp && a.team == b.team;
}
static bool ReadDamageFriends(uintptr_t (&friends)[2]) {
    const auto slot = g_exeBase + offsets::SLOT0_BASE + offsets::SLOT_STRIDE;
    uintptr_t repeated[2] {};
    return ReadHitTrace(slot + offsets::slot::FRIEND1_ACTOR_PTR, friends[0]) &&
        ReadHitTrace(slot + offsets::slot::FRIEND2_ACTOR_PTR, friends[1]) &&
        ReadHitTrace(slot + offsets::slot::FRIEND1_ACTOR_PTR, repeated[0]) &&
        ReadHitTrace(slot + offsets::slot::FRIEND2_ACTOR_PTR, repeated[1]) &&
        friends[0] == repeated[0] && friends[1] == repeated[1];
}
static bool DamageReservedActor(uintptr_t actor, const nativehittrace::Context& context,
                                const std::uint64_t (&roster)[3]) {
    if (!actor) return false;
    // A stale/inactive driver is still an exclusion, never positive peer
    // incarnation proof. Slot assignment follows AvatarSync's ascending order.
    for (const auto& driver : g_puppets) if (driver.actor == actor) return true;
    unsigned index = 0;
    for (unsigned slot = 0; slot < 3; ++slot) {
        if (slot == context.slot) continue;
        if (roster[slot] && PuppetTarget(static_cast<int>(index)) == actor) return true;
        ++index;
    }
    return false;
}
static bool NativeAiPermission(const nativehittrace::ActorSnapshot& actor,
                               const nativehittrace::Context& context, const std::uint64_t (&roster)[3]) {
    if (g_soloTestMode || context.frame != g_frameCounter || actor.type != 1 || actor.hp <= 0 || actor.maxHp <= 0 ||
        DamageReservedActor(actor.actor, context, roster)) return false;
    uintptr_t friends[2] {};
    // PuppetTarget uses the cached friend pointers. Never grant permission
    // from a fresh replacement while reservation still names the old actor.
    if (!ReadDamageFriends(friends) || friends[0] == friends[1] ||
        friends[0] != g_friend1Actor || friends[1] != g_friend2Actor) return false;
    for (unsigned i = 0; i < 2; ++i) {
        const auto& stamp = g_nativeAiStamps[i];
        if (stamp.valid && friends[i] == actor.actor && stamp.friendPointer == actor.actor &&
            SameDamageActor(stamp.actor, actor) && SameDamageContext(stamp.context, context) &&
            std::memcmp(stamp.roster, roster, sizeof(stamp.roster)) == 0) return true;
    }
    return false;
}
static damagepolicy::ActorClass ClassifyDamageActor(const nativehittrace::ActorSnapshot& actor,
    const nativehittrace::HitSnapshot& hit, const nativehittrace::Context& context,
    const std::uint64_t (&roster)[3]) {
    using damagepolicy::ActorClass;
    if (actor.readMask != nativehittrace::ActorComplete || !actor.actor || !actor.objentry || !actor.status)
        return ActorClass::Unknown;
    if ((hit.readMask & nativehittrace::HitCanonical) == 0 || !hit.canonicalPlayer ||
        hit.canonicalPlayer != hit.head || hit.canonicalPlayer != hit.tracked) return ActorClass::Unknown;
    if (actor.actor == hit.canonicalPlayer)
        return actor.type == 0 ? ActorClass::LocalAvatar : ActorClass::Unknown;
    // Type-zero actors other than the checked local triple are nonlocal player
    // representations. This is an ownership veto, not authenticated membership.
    if (actor.type == 0) return ActorClass::NonLocalPlayer;
    if (DamageReservedActor(actor.actor, context, roster)) return ActorClass::RemoteRepresentation;
    if (actor.type == 1)
        return NativeAiPermission(actor, context, roster) ? ActorClass::NativeCompanion : ActorClass::Unknown;
    if ((actor.type == offsets::objentry::TYPE_BOSS || actor.type == offsets::objentry::TYPE_MOB) &&
        actor.namePrefix != 0x5F46 && actor.objectId && actor.hp > 0 && actor.maxHp > 0) return ActorClass::Enemy;
    // A typed, checked native actor outside the player/companion/enemy classes
    // may affect the local victim, but never receives host-enemy permission.
    return ActorClass::OtherNative;
}
static DamageObservation CaptureDamagePolicy(uintptr_t victim, uintptr_t hit) {
    DamageObservation out {};
    // Existing non-policy paths retain their behavior on unsupported threads.
    if (!spawncontroller::IsDiagnosticGameThread()) { out.decision = damagepolicy::Evaluate(out.facts); return out; }
    out.facts.ownerThread = true;
    nativehittrace::Context before {}, after {};
    std::uint64_t roster[3] {}, repeated[3] {};
    if (!enemysync::CaptureDamageContext(before, roster)) { out.decision = damagepolicy::Evaluate(out.facts); return out; }
    const auto captured = CaptureHitFacts(victim, hit, false, false);
    out.victim = captured.victim; out.source = captured.source; out.hit = captured.hit;
    auto& f = out.facts;
    f.role = static_cast<damagepolicy::Role>(before.role);
    f.hit.flags = captured.hit.flags; f.hit.stat = captured.hit.stat; f.hit.amount = captured.hit.damage; f.hit.kind = captured.hit.kind;
    if (captured.hit.readMask & nativehittrace::HitFlags) f.hit.readMask |= damagepolicy::FlagsAvailable;
    if (captured.hit.readMask & nativehittrace::HitStat) f.hit.readMask |= damagepolicy::StatAvailable;
    if (captured.hit.readMask & nativehittrace::HitDamage) f.hit.readMask |= damagepolicy::AmountAvailable;
    if (captured.hit.readMask & nativehittrace::HitKind) f.hit.readMask |= damagepolicy::KindAvailable;
    f.victim = ClassifyDamageActor(captured.victim, captured.hit, before, roster);
    if ((captured.hit.readMask & (nativehittrace::HitAttack | nativehittrace::HitOwner)) ==
        (nativehittrace::HitAttack | nativehittrace::HitOwner) && captured.hit.owner == captured.source.actor)
        f.source = ClassifyDamageActor(captured.source, captured.hit, before, roster);
    f.contextAvailable = enemysync::CaptureDamageContext(after, repeated) && SameDamageContext(before, after) &&
        std::memcmp(roster, repeated, sizeof(roster)) == 0;
    if (f.contextAvailable) { out.context = after; std::memcpy(out.roster, repeated, sizeof(out.roster)); }
    out.decision = damagepolicy::Evaluate(f);
    return out;
}

static int BeginNativeAiStamp(uintptr_t actor, uintptr_t typeHandler) {
    if (!spawncontroller::IsDiagnosticGameThread()) return -1;
    for (auto& stamp : g_nativeAiStamps) if (stamp.actor.actor == actor) stamp = {};
    if (!g_origFriendAI || !g_friendAIHooked || !g_hookedAITarget || g_soloTestMode) return -1;
    NativeAiStamp stamp {};
    if (!enemysync::CaptureDamageContext(stamp.context, stamp.roster) || stamp.context.frame != g_frameCounter ||
        DamageReservedActor(actor, stamp.context, stamp.roster)) return -1;
    uintptr_t vtable = 0, target = 0, friends[2] {};
    if (!ReadHitTrace(typeHandler, vtable) || !ReadHitTrace(vtable + 0x10, target) ||
        target != reinterpret_cast<uintptr_t>(g_hookedAITarget) || !ReadDamageFriends(friends) || friends[0] == friends[1] ||
        friends[0] != g_friend1Actor || friends[1] != g_friend2Actor) return -1;
    const int index = friends[0] == actor ? 0 : (friends[1] == actor ? 1 : -1);
    if (index < 0) return -1;
    stamp.actor = CaptureHitActor(actor);
    if (stamp.actor.type != 1 || stamp.actor.hp <= 0 || stamp.actor.maxHp <= 0 || !SameDamageActor(stamp.actor, stamp.actor)) return -1;
    nativehittrace::Context repeatedContext {}; std::uint64_t repeatedRoster[3] {}; uintptr_t repeatedFriends[2] {};
    if (!enemysync::CaptureDamageContext(repeatedContext, repeatedRoster) || !SameDamageContext(stamp.context, repeatedContext) ||
        std::memcmp(stamp.roster, repeatedRoster, sizeof(stamp.roster)) != 0 || !ReadDamageFriends(repeatedFriends) ||
        repeatedFriends[0] != g_friend1Actor || repeatedFriends[1] != g_friend2Actor ||
        repeatedFriends[index] != actor || DamageReservedActor(actor, repeatedContext, repeatedRoster)) return -1;
    stamp.friendPointer = actor; stamp.valid = true; g_nativeAiStamps[index] = stamp;
    return index;
}
static void EndNativeAiStamp(int index, bool normal) {
    if (index < 0 || index > 1) return;
    auto& stamp = g_nativeAiStamps[index];
    if (!normal) { stamp = {}; return; } // Abnormal cleanup never reads native state.
    nativehittrace::Context context {}; std::uint64_t roster[3] {};
    const auto actor = CaptureHitActor(stamp.actor.actor);
    if (!enemysync::CaptureDamageContext(context, roster) || !NativeAiPermission(actor, context, roster)) stamp = {};
}

// Friend slots our code drives instead of the vanilla AI.
static bool IsDrivenFriend(int friendSlot) {
    if (friendSlot == 0) return false;
    if (g_soloTestMode) return true;
    return PuppetIndexFor(friendSlot == 1 ? g_friend1Actor : g_friend2Actor) >= 0;
}

// Puts back the team of an actor we stopped driving, if it's still the
// same actor (after a room load the old actor is gone and the new one
// starts with its own team).
static bool PuppetReleaseLifecycleCurrent(std::uint32_t transition, std::uint32_t load) {
    __try {
        return transition == g_puppetTransitionSerial && load == g_puppetLoadSerial &&
            transition == warp::TransitionSerial() && load == warp::LoadSerial() && !warp::TransitionPending();
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static bool PuppetReleaseCanonicalCurrent(uintptr_t actor) {
    const auto tracked = g_soraActor;
    uintptr_t player = 0, head = 0, repeatedPlayer = 0, repeatedHead = 0;
    return tracked != 0 && actor != tracked &&
        ReadHitTrace(g_exeBase + 0x2A105D0, player) &&
        ReadHitTrace(g_exeBase + offsets::active_entity_list::HEAD, head) &&
        ReadHitTrace(g_exeBase + 0x2A105D0, repeatedPlayer) &&
        ReadHitTrace(g_exeBase + offsets::active_entity_list::HEAD, repeatedHead) &&
        player == tracked && head == tracked && repeatedPlayer == tracked && repeatedHead == tracked &&
        g_soraActor == tracked;
}

static void RestorePuppetTeam(PuppetDriver& d, int index) {
    const auto transition = warp::TransitionSerial();
    const auto load = warp::LoadSerial();
    uintptr_t friends[2] {}, repeated[2] {};
    const auto& bound = d.boundActor;
    const bool eligible = index >= 0 && index < 2 && d.actor != 0 && PuppetReleaseCanonicalCurrent(d.actor) &&
        PuppetReleaseLifecycleCurrent(transition, load);
    const bool currentFriend = eligible && g_clones[0] == 0 &&
        ReadDamageFriends(friends) && friends[0] != friends[1] && friends[index] == d.actor;
    // Cached clone selection is only a candidate. Reuse the existing complete
    // canonical census to prove current membership, then reread actor metadata.
    const bool currentClone = eligible && g_clones[0] != 0 && g_clones[index] == d.actor &&
        bound.type == 0 && enemysync::CurrentPuppetActor(d.actor, transition, load);
    const auto now = CaptureHitActor(currentFriend || currentClone ? d.actor : 0);
    constexpr auto metadata = nativehittrace::ActorObject | nativehittrace::ActorStatus |
        nativehittrace::ActorType | nativehittrace::ActorId | nativehittrace::ActorName | nativehittrace::ActorRepeated;
    const bool same = (bound.readMask & metadata) == metadata && (now.readMask & metadata) == metadata &&
        bound.actor == now.actor && bound.objentry == now.objentry && bound.status == now.status &&
        bound.objectId == now.objectId && bound.type == now.type && bound.namePrefix == now.namePrefix;
    const bool membership =
        (currentFriend && now.type == 1 && ReadDamageFriends(repeated) &&
         repeated[0] == friends[0] && repeated[1] == friends[1]) ||
        (currentClone && now.type == 0 && g_clones[0] != 0 && g_clones[index] == d.actor);
    if (membership && same && PuppetReleaseCanonicalCurrent(d.actor) &&
        PuppetReleaseLifecycleCurrent(transition, load)) {
        if (d.teamSaved) *reinterpret_cast<uint32_t*>(d.actor + ACTOR_TEAM) = d.savedTeam;
        if (d.noCollideSaved && !d.savedNoCollide) {
            *reinterpret_cast<uint8_t*>(d.actor + ACTOR_COLLISION_FLAGS) &= ~ACTOR_NO_COLLIDE;
        }
    }
    d.teamSaved = false;
    d.noCollideSaved = false;
    d.applied = false;
}

// Frame start: take new poses, release slots that stopped being puppets,
// and hold the drive gauge while any puppet is active.
// Forgets a driver's cached actor without writing to it (the actor may be
// mid-teardown or already freed).
static void ForgetPuppetActor(PuppetDriver& d) {
    d.actor = 0;
    d.teamSaved = false;
    d.noCollideSaved = false;
    d.lastAnim = -1;
    d.applied = false;
    d.boundActor = {};
}

// Frame start: suspend puppets when a transition is requested and resume
// after the load. Returns true while suspended.
static bool UpdatePuppetSuspension() {
    const auto transition = warp::TransitionSerial();
    const auto load = warp::LoadSerial();
    const bool pending = warp::TransitionPending();
    if (transition != g_puppetTransitionSerial || load != g_puppetLoadSerial || pending) {
        render::InvalidateCoopHud();
        ClearNativeAiStamps();
        // Invalidate on the actual native lifecycle, including same-room
        // reloads. No restoration writes may reach a prior room's actors.
        for (auto& d : g_puppets) {
            ForgetPuppetActor(d);
            d.pose = {};
            d.have = false;
            d.poseFrame = 0;
        }
        g_clones[0] = g_clones[1] = 0;
        g_clonesNow[0] = g_clonesNow[1] = 0;
        g_cloneCountNow = 0;
        g_friend1Actor = g_friend2Actor = 0;
        g_handleCount = 0;
    }
    if (pending != g_puppetsSuspended) {
        Log("Puppets %s: transition=%u load=%u", pending ? "suspended" : "resumed", transition, load);
    }
    g_puppetTransitionSerial = transition;
    g_puppetLoadSerial = load;
    g_puppetsSuspended = pending;
    return g_puppetsSuspended;
}

static void PollPuppetPoses() {
    const bool suspended = UpdatePuppetSuspension();
    if (suspended || !hud::ValidAuthority(enemysync::CapturePuppetAuthority()))
        render::InvalidateCoopHud();
    if (!g_avatarBridge.IsOpen()) return;
    bool anyActive = false;
    for (int i = 0; i < 2; ++i) {
        auto& driver = g_puppets[i];
        // Authority can retire between DLL frames. Previous application is a
        // separate fact: asking the current predicate cannot detect its edge.
        const bool wasApplied = driver.applied;
        if (!IsPuppetActive(i)) {
            driver.pose = {};
            driver.have = false;
        }
        kh2coop::PuppetPose pose;
        if (g_avatarBridge.TryReadPuppet(i, pose)) {
            if (!suspended && pose.active && ValidPuppetProvenance(pose.provenance,
                    static_cast<std::uint8_t>(pose.pose.ownerSlot), i, enemysync::CapturePuppetAuthority())) {
                driver.pose = pose;
                driver.have = true;
                driver.poseFrame = g_frameCounter;
            } else {
                driver.pose = {};
                driver.have = false;
            }
        }
        const bool active = IsPuppetActive(i);
        if (wasApplied && !active) {
            RestorePuppetTeam(driver, i);
            driver.lastAnim = -1;
            Log("Puppet %d released", i);
        } else if (!wasApplied && active) {
            Log("Puppet %d active", i);
        }
        anyActive = anyActive || active;
    }
    g_anyPuppetActive = anyActive;

    auto* bars = reinterpret_cast<uint8_t*>(g_exeBase + SORA_DRIVE_BARS);
    auto* partial = reinterpret_cast<uint8_t*>(g_exeBase + SORA_DRIVE_PARTIAL);
    if (anyActive) {
        if (!g_driveHeld) {
            g_savedDriveBars = *bars;
            g_savedDrivePartial = *partial;
            g_driveHeld = true;
            Log("Puppets active: holding drive gauge at 0 (was %u bars, %u partial)",
                g_savedDriveBars, g_savedDrivePartial);
        }
        *bars = 0;
        *partial = 0;
    } else if (g_driveHeld) {
        *bars = g_savedDriveBars;
        *partial = g_savedDrivePartial;
        g_driveHeld = false;
        Log("No puppets: drive gauge restored");
    }
}
static bool     g_mailboxAvailable     = false;
// Wall-clock timers: the DLL frame counter only advances during entity
// updates, so it stands still on the title screen and in loads, where
// kh2ctl pulses still need to reach the input collector.
static uint64_t g_lastMailboxCheckMs = 0;
static constexpr uint64_t MAILBOX_RETRY_INTERVAL_MS = 2000;  // runtime liveness check
// Connect retries are cheap (OpenFileMapping on a missing name) and kh2ctl
// pulses only keep the mailbox open for their duration, so poll quickly.
static constexpr uint64_t MAILBOX_CONNECT_INTERVAL_MS = 100;

// ============================================================================
// Logging
// ============================================================================

static void Log(const char* fmt, ...) {
    if (!g_logFile) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(g_logFile, fmt, args);
    va_end(args);
    fprintf(g_logFile, "\n");
    fflush(g_logFile);
}

// ============================================================================
// Friend entity identification
// ============================================================================

// Read friend actor pointers from the game's unit slot data.
// These are direct in-process pointer dereferences — zero overhead.
// Called on every PerEntityUpdate to stay current across room transitions.
static void RefreshFriendPointers() {
    using namespace offsets;
    uintptr_t slot1 = g_exeBase + SLOT0_BASE + SLOT_STRIDE;

    uintptr_t f1 = 0, f2 = 0;
    __try {
        f1 = *reinterpret_cast<uintptr_t*>(slot1 + slot::FRIEND1_ACTOR_PTR);
        f2 = *reinterpret_cast<uintptr_t*>(slot1 + slot::FRIEND2_ACTOR_PTR);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        f1 = f2 = 0;
    }

    // Validate: pointers should be within the game module's address space
    auto valid = [](uintptr_t p, uintptr_t base) {
        return p != 0 && p > base && p < base + 0x3000000;
    };

    g_friend1Actor = valid(f1, g_exeBase) ? f1 : 0;
    g_friend2Actor = valid(f2, g_exeBase) ? f2 : 0;
}

// ============================================================================
// KH2 input-buffer controller reading
// ============================================================================

static constexpr int MAX_GAMEPAD_SLOTS = 4;

static float NormalizeRawStickX(std::uint8_t value) {
    int centered = static_cast<int>(value) - 0x80;
    return (centered >= 0)
        ? static_cast<float>(centered) / 127.0f
        : static_cast<float>(centered) / 128.0f;
}

static float NormalizeRawStickY(std::uint8_t value) {
    int centered = 0x80 - static_cast<int>(value);
    return (centered >= 0)
        ? static_cast<float>(centered) / 127.0f
        : static_cast<float>(centered) / 128.0f;
}

static float StickMagnitude(float x, float y) {
    return std::sqrt(x * x + y * y);
}

static float ApplyRadialDeadzone(float* x, float* y) {
    float magnitude = StickMagnitude(*x, *y);
    if (magnitude < STICK_DEADZONE) {
        *x = 0.0f;
        *y = 0.0f;
        return 0.0f;
    }

    float scale = (magnitude - STICK_DEADZONE) / (1.0f - STICK_DEADZONE);
    *x = (*x / magnitude) * scale;
    *y = (*y / magnitude) * scale;
    return scale;
}

// SelectMovementStick removed: only left stick drives movement.
// Right stick is reserved for camera control (handled by the game).

static float ClampUnit(float value) {
    if (value > 1.0f) return 1.0f;
    if (value < -1.0f) return -1.0f;
    return value;
}

static std::uint8_t EncodeRawStickX(float value) {
    const float clamped = ClampUnit(value);
    const int centered = (clamped >= 0.0f)
        ? static_cast<int>(std::lround(clamped * 127.0f))
        : -static_cast<int>(std::lround(-clamped * 128.0f));
    const int raw = 0x80 + centered;
    return static_cast<std::uint8_t>(std::clamp(raw, 0, 0xFF));
}

static std::uint8_t EncodeRawStickY(float value) {
    const float clamped = ClampUnit(value);
    const int centered = (clamped >= 0.0f)
        ? static_cast<int>(std::lround(clamped * 127.0f))
        : -static_cast<int>(std::lround(-clamped * 128.0f));
    const int raw = 0x80 - centered;
    return static_cast<std::uint8_t>(std::clamp(raw, 0, 0xFF));
}

static bool TryReadSoloProcessedStick(GamepadState* out) {
    if (!out) return false;

    using namespace offsets;

    __try {
        // KH2's input mapper swaps the stick fields in the processed entry:
        //   +0x20 ("left" field)  = physical RIGHT stick (camera)
        //   +0x30 ("right" field) = physical LEFT stick (movement)
        // Verified via live CE probe: pushing physical left stick shows at +0x30.
        const auto* physRight = reinterpret_cast<const float*>(
            g_exeBase + input::PROCESSED_ENTRY0 + 0x20);
        const auto* physLeft = reinterpret_cast<const float*>(
            g_exeBase + input::PROCESSED_ENTRY0 + 0x30);

        out->connected = true;
        out->leftX  = ClampUnit(physLeft[0]);   // movement
        out->leftY  = ClampUnit(physLeft[1]);
        out->rightX = ClampUnit(physRight[0]);  // camera
        out->rightY = ClampUnit(physRight[1]);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static int ResolveRawSlotForController(int controllerIndex, int activeInputSlot) {
    if (controllerIndex < 0) {
        return -1;
    }
    if (activeInputSlot <= 0) {
        return controllerIndex;
    }
    if (controllerIndex == activeInputSlot) {
        return 0;
    }
    if (controllerIndex == 0) {
        return activeInputSlot;
    }
    return controllerIndex;
}

// ============================================================================
// Network input mailbox — try to read friend input from the runtime process
//
// Called before falling back to local gamepad reads. If the runtime has
// written fresh InputFrame data to shared memory, we consume it here.
// Returns true if at least one friend slot was populated from the mailbox.
// ============================================================================

static void ClearMailboxCachedState() {
    g_primaryMailboxPad = {};
    g_primaryRawButtons = 0;
    g_mailboxFriendPad[0] = {};
    g_mailboxFriendPad[1] = {};
}

static bool PollMailbox() {
    if (!g_mailboxAvailable) {
        // Periodically retry opening the mailbox (runtime may start later)
        const uint64_t nowMs = GetTickCount64();
        if (nowMs - g_lastMailboxCheckMs >= MAILBOX_CONNECT_INTERVAL_MS) {
            g_lastMailboxCheckMs = nowMs;
            if (g_mailboxReader.Open()) {
                g_mailboxAvailable = true;
                ClearMailboxCachedState();
                Log("Network input mailbox CONNECTED (runtime PID=%lu)",
                    static_cast<unsigned long>(g_mailboxReader.RuntimePid()));
            }
        }
        if (!g_mailboxAvailable) return false;
    }

    // Periodic liveness check (~every 2s): verify the runtime process is still
    // alive. If it died, close the stale mapping and fall back to local gamepads.
    if (GetTickCount64() - g_lastMailboxCheckMs >= MAILBOX_RETRY_INTERVAL_MS) {
        g_lastMailboxCheckMs = GetTickCount64();
        DWORD rtPid = g_mailboxReader.RuntimePid();
        if (rtPid != 0) {
            HANDLE hProc = OpenProcess(SYNCHRONIZE, FALSE, rtPid);
            if (!hProc) {
                Log("Network input mailbox DISCONNECTED — runtime PID=%lu no longer alive, falling back to local gamepads",
                    static_cast<unsigned long>(rtPid));
                g_mailboxReader.Close();
                g_mailboxAvailable = false;
                ClearMailboxCachedState();
                return false;
            }
            CloseHandle(hProc);
        }
    }

    kh2coop::MailboxReadResult result {};

    if (g_mailboxReader.TryReadSlot(kh2coop::MAILBOX_SLOT_PLAYER, result)) {
        g_primaryMailboxPad.connected = true;
        g_primaryMailboxPad.worldSpace = false;
        g_primaryMailboxPad.buttons = 0;
        g_primaryMailboxPad.leftX = result.leftStickX;
        g_primaryMailboxPad.leftY = result.leftStickY;
        g_primaryMailboxPad.rightX = result.rightStickX;
        g_primaryMailboxPad.rightY = result.rightStickY;
        g_primaryRawButtons = result.rawButtons;
#if 0
        kh2coop::MailboxReadResult result {};
        if (g_mailboxReader.TryReadSlot(padIdx, result)) {
            GamepadState& pad = g_gamepad[padIdx];
            pad.connected = true;
            pad.worldSpace = true;
            // Store packed MailboxButton bitmask. This is NOT KH2's raw input
            // format — it uses the kh2coop::MailboxButton enum layout. When P2
            // combat wires button consumption, use UnpackButtons() to decode.
            pad.buttons   = static_cast<std::uint16_t>(result.buttons & 0xFFFF);
            pad.leftX     = result.leftStickX;
            pad.leftY     = result.leftStickY;
            pad.rightX    = result.rightStickX;
            pad.rightY    = result.rightStickY;
            anyRead = true;
        }
#endif
        // If TryReadSlot returns false, the previous GamepadState is retained
        // (from a prior mailbox read within this frame's loop iteration).
    }

    for (int slotIndex = kh2coop::MAILBOX_SLOT_FRIEND1;
         slotIndex <= kh2coop::MAILBOX_SLOT_FRIEND2;
         ++slotIndex) {
        if (g_mailboxReader.TryReadSlot(slotIndex, result)) {
            GamepadState& pad =
                g_mailboxFriendPad[slotIndex - kh2coop::MAILBOX_SLOT_FRIEND1];
            pad.connected = true;
            pad.worldSpace = true;
            pad.buttons = static_cast<std::uint16_t>(result.buttons & 0xFFFF);
            pad.leftX = result.leftStickX;
            pad.leftY = result.leftStickY;
            pad.rightX = result.rightStickX;
            pad.rightY = result.rightStickY;
        }
    }

    return true;
}

static bool HasPrimaryMailboxOverride() {
    if (!g_primaryMailboxPad.connected) {
        return false;
    }

    return g_primaryRawButtons != 0 ||
           std::fabs(g_primaryMailboxPad.leftX) > 0.001f ||
           std::fabs(g_primaryMailboxPad.leftY) > 0.001f ||
           std::fabs(g_primaryMailboxPad.rightX) > 0.001f ||
           std::fabs(g_primaryMailboxPad.rightY) > 0.001f;
}

static void ApplyPrimaryMailboxInput(void* inputStruct) {
    if (!inputStruct || !HasPrimaryMailboxOverride()) {
        return;
    }

    auto* raw = reinterpret_cast<std::uint8_t*>(
        reinterpret_cast<uintptr_t>(inputStruct) + offsets::input::RAW_SLOT0);
    *reinterpret_cast<std::uint16_t*>(raw + offsets::input::BUTTONS) =
        g_primaryRawButtons;
    raw[offsets::input::LSTICK_X] = EncodeRawStickX(g_primaryMailboxPad.leftX);
    raw[offsets::input::LSTICK_Y] = EncodeRawStickY(g_primaryMailboxPad.leftY);
    raw[offsets::input::RSTICK_X] = EncodeRawStickX(g_primaryMailboxPad.rightX);
    raw[offsets::input::RSTICK_Y] = EncodeRawStickY(g_primaryMailboxPad.rightY);
}

static void ReadGamepads() {
    using namespace offsets;

    g_inputControllerCount = 0;
    g_activeInputSlot = -1;

    // Try network mailbox first — if the runtime is delivering remote input,
    // skip the local gamepad read entirely. This is the P3 IPC path.
    // NOTE: mailbox-backed control now comes from PollMailbox() and cached
    // slot state, so we only zero g_gamepad[] on the local fallback path.
    if (PollMailbox()) {
        g_gamepad[0] = g_mailboxFriendPad[0];
        g_gamepad[1] = g_mailboxFriendPad[1];
        return;
    }

    // Fallback: read from KH2's local raw input buffer (solo/offline mode).
    // Zero gamepads here (not above) so the mailbox path can retain stale data.
    g_gamepad[0] = {};
    g_gamepad[1] = {};

    uintptr_t inputStruct = 0;
    __try {
        inputStruct = *reinterpret_cast<uintptr_t*>(g_exeBase + INPUT_STRUCT_PTR);
        if (inputStruct == 0) {
            return;
        }

        g_inputControllerCount = *reinterpret_cast<int*>(
            inputStruct + input::CONTROLLER_COUNT);
        g_activeInputSlot = *reinterpret_cast<int*>(
            inputStruct + input::ACTIVE_RAW_SLOT_INDEX);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }

    if (g_inputControllerCount < 0) {
        g_inputControllerCount = 0;
    } else if (g_inputControllerCount > MAX_GAMEPAD_SLOTS) {
        g_inputControllerCount = MAX_GAMEPAD_SLOTS;
    }

    if (g_activeInputSlot < 0 || g_activeInputSlot >= g_inputControllerCount) {
        g_activeInputSlot = 0;
    }

    const int sourceControllers[2] = {
        g_soloTestMode ? 0 : 1,
        g_soloTestMode ? -1 : 2,
    };

    for (int padIdx = 0; padIdx < 2; ++padIdx) {
        const int controllerIndex = sourceControllers[padIdx];
        if (controllerIndex < 0 || controllerIndex >= g_inputControllerCount) {
            continue;
        }

        const int rawSlot = ResolveRawSlotForController(
            controllerIndex, g_activeInputSlot);
        if (rawSlot < 0 || rawSlot >= g_inputControllerCount) {
            continue;
        }

        __try {
            auto* raw = reinterpret_cast<const std::uint8_t*>(
                inputStruct + input::RAW_SLOT0 +
                static_cast<std::uint64_t>(rawSlot) * input::RAW_SLOT_STRIDE);

            GamepadState sample {};
            sample.connected = true;
            sample.worldSpace = false;
            sample.buttons = *reinterpret_cast<const std::uint16_t*>(
                raw + input::BUTTONS);
            sample.leftX = NormalizeRawStickX(raw[input::LSTICK_X]);
            sample.leftY = NormalizeRawStickY(raw[input::LSTICK_Y]);
            sample.rightX = NormalizeRawStickX(raw[input::RSTICK_X]);
            sample.rightY = NormalizeRawStickY(raw[input::RSTICK_Y]);
            g_gamepad[padIdx] = sample;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_gamepad[padIdx] = {};
        }
    }

    // In solo mode, KH2's input mapper swaps the stick fields:
    //   raw LSTICK = physical right stick (camera)
    //   raw RSTICK = physical left stick (movement)
    // The processed entry at +0x30 had the correct mapping but was DIGITAL
    // (0 or ±1 only). We keep the raw ANALOG values but swap the axes so
    // physical left stick → g_gamepad[0].leftX/Y (movement).
    if (g_soloTestMode) {
        auto& pad = g_gamepad[0];
        float tmpX = pad.leftX;
        float tmpY = pad.leftY;
        pad.leftX  = pad.rightX;
        pad.leftY  = -pad.rightY;   // raw Y polarity is inverted vs processed
        pad.rightX = tmpX;
        pad.rightY = tmpY;
    }
}

#if 0
static void ReadGamepadsLegacy() {
    if (g_soloTestMode) {
        // Solo test mode: gamepad 0 (primary controller) → Friend1
        DWORD result = XInputGetState(0, &g_gamepad[0]);
        g_gamepadConnected[0] = (result == ERROR_SUCCESS);
        g_gamepadConnected[1] = false;
    } else {
        // Normal mode: gamepad 1 → Friend1, gamepad 2 → Friend2
        for (int i = 0; i < 2; ++i) {
            DWORD result = XInputGetState(static_cast<DWORD>(i + 1), &g_gamepad[i]);
            g_gamepadConnected[i] = (result == ERROR_SUCCESS);
        }
    }
}
#endif

// Suppress Sora's MOVEMENT while preserving full camera control.
//
// Previous approach zeroed the processed input entry and restored the camera
// stick — but that killed camera orbit because the camera may read from
// additional sources or the zero-restore timing was wrong.
//
// New approach: leave the processed input entry completely untouched (camera
// continues to work normally) and instead suppress Sora at the ENTITY level
// by zeroing his velocity/acceleration fields each frame. This is the same
// technique we use for de-tethering Donald.
static void SuppressSoraMovement() {
    if (g_soraActor == 0) return;

    __try {
        auto* actor = reinterpret_cast<uint8_t*>(g_soraActor);

        // Zero movement velocity — Sora stands still
        *reinterpret_cast<float*>(actor + ACTOR_VELOCITY_X) = 0.0f;
        *reinterpret_cast<float*>(actor + ACTOR_VELOCITY_Y) = 0.0f;
        *reinterpret_cast<float*>(actor + ACTOR_VELOCITY_Z) = 0.0f;

        // Zero acceleration
        *reinterpret_cast<float*>(actor + ACTOR_ACCEL_X) = 0.0f;
        *reinterpret_cast<float*>(actor + ACTOR_ACCEL_Y) = 0.0f;
        *reinterpret_cast<float*>(actor + ACTOR_ACCEL_Z) = 0.0f;

        // Force idle animation on Sora
        *reinterpret_cast<uint32_t*>(actor + ACTOR_ANIM_ID) = ANIM_IDLE;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Sora actor became invalid
    }
}

// Zero just the movement stick in the processed entry to prevent Sora from
// receiving movement commands. The camera stick (+0x20) and buttons are left
// intact so camera orbit and menu navigation continue working.
static void ZeroMovementStickInProcessedEntry() {
    using namespace offsets;
    auto* entry = reinterpret_cast<uint8_t*>(g_exeBase + input::PROCESSED_ENTRY0);

    __try {
        // +0x30 = physical left stick (movement) — zero it
        memset(entry + 0x30, 0, 16);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// ============================================================================
// In-process camera retargeting
//
// When controlling a friend entity, redirect the game's camera to follow
// that friend instead of Sora. Since we're in the game's own process, we
// can simply swap the actor pointer in the camera struct — no fake actor
// allocation needed (unlike the external runtime process approach).
//
// Camera struct layout (exe+0x718C60):
//   +0x50: qword — pointer to followed actor object
//   The game reads actor+0x640+0x30 (entity transform position) each frame.
// ============================================================================

static void RetargetCameraToFriend() {
    if (g_friend1Actor == 0) return;

    using namespace offsets;
    auto camActorPtrAddr = reinterpret_cast<uintptr_t*>(
        g_exeBase + CAMERA_STRUCT + camera::ACTOR_PTR);

    __try {
        if (!g_cameraRetargeted) {
            g_origCameraActorPtr = *camActorPtrAddr;
            if (g_origCameraActorPtr == 0) return;
        }
        *camActorPtrAddr = g_friend1Actor;
        g_cameraRetargeted = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("EXCEPTION in RetargetCameraToFriend");
    }
}

static void RestoreCameraToSora() {
    if (!g_cameraRetargeted) return;

    using namespace offsets;
    auto camActorPtrAddr = reinterpret_cast<uintptr_t*>(
        g_exeBase + CAMERA_STRUCT + camera::ACTOR_PTR);

    __try {
        // Use Sora's actor (entity list head) as the restore target.
        // g_origCameraActorPtr may be wrong if the game's camera was already
        // pointing at a non-Sora entity when we first retargeted.
        uintptr_t soraPtr = g_soraActor;
        if (soraPtr == 0) {
            // Fallback: read entity list head directly
            soraPtr = *reinterpret_cast<uintptr_t*>(
                g_exeBase + active_entity_list::HEAD);
        }
        if (soraPtr != 0) {
            *camActorPtrAddr = soraPtr;
            Log("Camera restored to Sora actor %p", reinterpret_cast<void*>(soraPtr));
        } else if (g_origCameraActorPtr != 0) {
            // Last resort: use whatever was saved
            *camActorPtrAddr = g_origCameraActorPtr;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("EXCEPTION in RestoreCameraToSora");
    }

    g_origCameraActorPtr = 0;
    g_cameraRetargeted = false;
}

// Check F5 key for solo test mode toggle (edge-triggered)
static void CheckTestModeHotkey() {
    bool f5Down = (GetAsyncKeyState(VK_F5) & 0x8000) != 0;
    if (f5Down && !g_f5WasDown) {
        g_soloTestMode = !g_soloTestMode;
        Log("Solo test mode %s (F5) — gamepad 0 → Friend1, Sora input %s, camera → %s",
            g_soloTestMode ? "ON" : "OFF",
            g_soloTestMode ? "suppressed" : "restored",
            g_soloTestMode ? "Friend1" : "Sora");

        // Toggle camera target with solo mode
        if (g_soloTestMode) {
            RetargetCameraToFriend();

            // Reset animation override tracking so the first frame in solo
            // mode detects the target as "changed" and calls FUN_1403c86a0.
            g_lastOverrideAnim[0] = -1;
            g_lastOverrideAnim[1] = -1;
        } else {
            RestoreCameraToSora();

            // Re-enable vanilla follow behavior immediately when solo mode is
            // disabled so Donald snaps back to the normal friend AI rules.
            __try {
                if (g_friend1Actor != 0) {
                    *reinterpret_cast<float*>(g_friend1Actor + ACTOR_FOLLOW_TIMER) = 0.0f;
                }
                if (g_friend2Actor != 0) {
                    *reinterpret_cast<float*>(g_friend2Actor + ACTOR_FOLLOW_TIMER) = 0.0f;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                Log("EXCEPTION resetting friend follow timer on solo-mode exit");
            }
        }
    }
    g_f5WasDown = f5Down;
}

// ============================================================================
// Input injection — write gamepad state to actor movement fields
//
// Controlled friends need their movement state visible in two places:
//   1. BEFORE the original friend AI runs, so its motion-channel calls can
//      see the current movement state and pick idle/walk/run correctly.
//   2. AFTER physics, so our injected velocity wins the final write race.
//
// This helper is reused for both phases.
//
// The exact format of actor+0xB98 (velocity) and actor+0xA58 (acceleration)
// is derived from Ghidra analysis of EntityPositionPhysics. These are
// experimental — the movement speed and axis mapping may need calibration
// after live testing.
// ============================================================================

static void InjectMovementInput(void* actorObj, int friendSlot) {
    auto actor = reinterpret_cast<uint8_t*>(actorObj);

    if (!ENABLE_MOVEMENT_INJECTION) {
        // Zero velocity to prevent drift when movement is disabled
        float zero = 0.0f;
        *reinterpret_cast<float*>(actor + ACTOR_VELOCITY_X) = zero;
        *reinterpret_cast<float*>(actor + ACTOR_VELOCITY_Y) = zero;
        *reinterpret_cast<float*>(actor + ACTOR_VELOCITY_Z) = zero;
        *reinterpret_cast<float*>(actor + ACTOR_ACCEL_X) = zero;
        *reinterpret_cast<float*>(actor + ACTOR_ACCEL_Y) = zero;
        *reinterpret_cast<float*>(actor + ACTOR_ACCEL_Z) = zero;
        return;
    }

    // Select gamepad (slot 1 → gamepad index 0, slot 2 → gamepad index 1)
    int padIdx = friendSlot - 1;
    const GamepadState& pad = g_gamepad[padIdx];
    bool connected = pad.connected;

    float velX = 0.0f;
    float velY = 0.0f;
    float velZ = 0.0f;
    float magnitude = 0.0f;

    if (connected && pad.worldSpace) {
        // Mailbox samples come from the runtime's world-space actor velocity,
        // not from local normalized stick coordinates.
        velX = pad.leftX;
        velZ = pad.leftY;
        magnitude = ClampUnit(StickMagnitude(velX, velZ) / RUN_SPEED);
    } else {
        // Use left stick only for movement. Right stick is for camera.
        // Stick Y is inverted — pushing up gives negative Y from the processed
        // entry, but we want positive Y = forward.
        float moveX = 0.0f;
        float moveY = 0.0f;
        if (connected) {
            moveX = pad.leftX;
            moveY = -pad.leftY;
        }

        magnitude = ApplyRadialDeadzone(&moveX, &moveY);

        // ---- Camera-relative stick-to-world transform ----
        // KH2 world coordinates: Y-negative is up. Movement is on the XZ plane.
        // The stick input (moveX = right, moveY = forward) must be rotated by
        // the camera's horizontal angle so movement is relative to what the
        // player sees on screen, matching how Sora's own movement works.
        // Scale speed proportionally to stick magnitude (like Sora).
        // Blend from WALK_SPEED at low tilt to RUN_SPEED at full tilt.
        const float t = (magnitude - STICK_DEADZONE) / (1.0f - STICK_DEADZONE);
        const float speed = WALK_SPEED + (RUN_SPEED - WALK_SPEED) * (t > 1.0f ? 1.0f : t);
        if (magnitude > 0.01f) {
            using namespace offsets;
            uintptr_t camBase = g_exeBase + CAMERA_STRUCT;

            // Read camera eye and look-at to compute horizontal direction
            float eyeX = 0, eyeZ = 0, lookX = 0, lookZ = 0;
            __try {
                lookX = *reinterpret_cast<float*>(camBase + camera::SMOOTH_LOOKAT);
                lookZ = *reinterpret_cast<float*>(camBase + camera::SMOOTH_LOOKAT + 8);
                eyeX  = *reinterpret_cast<float*>(camBase + camera::EYE_POS);
                eyeZ  = *reinterpret_cast<float*>(camBase + camera::EYE_POS + 8);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                // Fallback to world-absolute if camera read fails
            }

            float fwdX = lookX - eyeX;
            float fwdZ = lookZ - eyeZ;
            float fwdLen = std::sqrt(fwdX * fwdX + fwdZ * fwdZ);

            if (fwdLen > 0.001f) {
                fwdX /= fwdLen;
                fwdZ /= fwdLen;

                // Right direction: 90° clockwise rotation of forward on XZ plane.
                // forward=(fwdX,fwdZ), right=(fwdZ,-fwdX).
                float rightX = fwdZ;
                float rightZ = -fwdX;

                velX = (moveX * rightX + moveY * fwdX) * speed;
                velZ = (moveX * rightZ + moveY * fwdZ) * speed;
            } else {
                // Camera direction unavailable — fallback to world-absolute
                velX = moveX * speed;
                velZ = moveY * speed;
            }
        }
    }

    // Write velocity
    *reinterpret_cast<float*>(actor + ACTOR_VELOCITY_X) = velX;
    *reinterpret_cast<float*>(actor + ACTOR_VELOCITY_Y) = velY;
    *reinterpret_cast<float*>(actor + ACTOR_VELOCITY_Z) = velZ;

    // Write acceleration (same values — physics integrates from these)
    *reinterpret_cast<float*>(actor + ACTOR_ACCEL_X) = velX;
    *reinterpret_cast<float*>(actor + ACTOR_ACCEL_Y) = velY;
    *reinterpret_cast<float*>(actor + ACTOR_ACCEL_Z) = velZ;

    // Suppress the residual vanilla follow-steering path. EntityPositionPhysics
    // only calls the tether callback when this timer goes negative.
    *reinterpret_cast<float*>(actor + ACTOR_FOLLOW_TIMER) = DISABLE_FOLLOW_TIMER;

    if (g_soloTestMode && friendSlot == 1 && connected) {
        const bool shouldLogActiveMovement =
            magnitude > 0.01f && (g_frameCounter - g_lastMovementLogFrame) >= 30;
        if (shouldLogActiveMovement) {
            Log("[move %u] leftStick=(%.2f,%.2f) vel=(%.2f,%.2f) mag=%.2f",
                g_frameCounter,
                pad.leftX, pad.leftY,
                velX, velZ, magnitude);
            g_lastMovementLogFrame = g_frameCounter;
        }
    }

    // Store stick magnitude for the caller (HookedFriendAI uses this to
    // select idle/walk/run animation).
    int facingIdx = friendSlot - 1;
    if (facingIdx >= 0 && facingIdx < 2) {
        g_lastStickMagnitude[facingIdx] = magnitude;
    }

    // Update facing direction from the world-space velocity vector.
    //
    // KH2 facing convention (verified via CE live read of Sora's entity):
    //   ROT_Y (+0x4C) = atan2(velX, velZ)
    //   +0x40 (labeled COS_FACING in offsets) = sin(ROT_Y)  [historically mislabeled]
    //   +0x48 (labeled SIN_FACING in offsets) = cos(ROT_Y)  [historically mislabeled]
    //
    // When the stick is active, compute and cache the facing angle.
    // When the stick is released, keep writing the LAST facing angle so the
    // game's own AI/physics can't snap the friend back toward Sora.
    {
        using namespace offsets;
        uintptr_t entityBase = reinterpret_cast<uintptr_t>(actor) +
                               actor::ENTITY_TRANSFORM;

        if (magnitude > 0.01f) {
            float angle = std::atan2(velX, velZ);
            if (facingIdx >= 0 && facingIdx < 2) {
                g_lastFacingAngle[facingIdx] = angle;
                g_facingAngleValid[facingIdx] = true;
            }
            *reinterpret_cast<float*>(entityBase + entity::ROT_Y)      = angle;
            *reinterpret_cast<float*>(entityBase + entity::COS_FACING)  = std::sin(angle);
            *reinterpret_cast<float*>(entityBase + entity::SIN_FACING)  = std::cos(angle);
        } else if (facingIdx >= 0 && facingIdx < 2 && g_facingAngleValid[facingIdx]) {
            // Stick released — persist the last facing direction every frame
            float angle = g_lastFacingAngle[facingIdx];
            *reinterpret_cast<float*>(entityBase + entity::ROT_Y)      = angle;
            *reinterpret_cast<float*>(entityBase + entity::COS_FACING)  = std::sin(angle);
            *reinterpret_cast<float*>(entityBase + entity::SIN_FACING)  = std::cos(angle);
        }
    }
}

// ============================================================================
// Movement dispatch hook — intercepts FUN_1403d5e50
//
// This is the function that drives idle ↔ walk ↔ run animation transitions.
// It's called from the friend AI's behavior timer via:
//   FUN_1403c3bd0 → vtable+0xE8 → FUN_1401b03d0 → FUN_1403d5e50
// and for Sora via a parallel path through FUN_1403a85f0.
//
// For controlled friends: replace the speed delta with a value derived from
// the player's stick magnitude. This makes the animation system naturally
// select idle/walk/run to match the player's input, using the exact same
// deceleration (→ idle) and acceleration (→ run) paths the game uses.
//
// For all other entities: pass through to the original unchanged.
// ============================================================================

// ============================================================================
// Motion chain animation hook — intercepts FUN_1403c88c0
//
// This is the single point where animations are SET on the motion controller.
// Called by the motCtrl tick (FUN_1403c6740) at animation loop boundaries
// and by FUN_1403c86a0 for explicit animation changes.
//
// For controlled friends: replace the animation ID with our stick-based
// target. This makes the tick's own loop logic seamlessly play our desired
// animation without constant fighting/restarting.
// ============================================================================

static uint32_t g_motionChainOverrides = 0;
static uint32_t g_motionChainLogFrame  = 0;

static uint8_t __fastcall HookedMotionChainSetAnim(void* motCtrl, int animId,
                                                     float startTime, float blendParam) {
    {
        // Derive actor address from motCtrl: actor = motCtrl - 0x158
        auto actorAddr = reinterpret_cast<uintptr_t>(motCtrl) - 0x158;

        int friendSlot = 0;
        if (g_friend1Actor != 0 && actorAddr == g_friend1Actor) friendSlot = 1;
        else if (g_friend2Actor != 0 && actorAddr == g_friend2Actor) friendSlot = 2;

        // Puppets: block the game's calls, pass our own through unchanged
        // (DrivePuppetMotion already chose the stream's motion).
        if (PuppetIndexFor(actorAddr) >= 0) {
            if (!g_inOurAnimSet) return 1;
            if (g_inPuppetAnimSet) {
                return g_origMotionChainSetAnim
                    ? g_origMotionChainSetAnim(motCtrl, animId, startTime, blendParam)
                    : 0;
            }
        }

        if (friendSlot != 0 && g_soloTestMode) {
            // Session 5 fix: Block ALL FUN_1403c88c0 calls for controlled
            // friends EXCEPT our own (from HookedFriendAI via FUN_1403c86a0).
            //
            // The game calls FUN_1403c88c0 once per frame for every entity
            // through a code path outside the AI (likely from the motion
            // playback system itself). Each call goes through FUN_1403c8cd0
            // → FUN_1403c8a40, which has a blend path that writes 2.0 to
            // motCtrl+0x44 (curTime). This resets the animation time every
            // frame, causing the "stuck at frame 0" appearance.
            //
            // By blocking these per-frame calls, the tick can advance time
            // normally and the animation plays through its full loop.
            // Our own calls (guarded by g_inOurAnimSet) still go through
            // to set the initial animation on transitions.
            if (!g_inOurAnimSet) {
                ++g_motionChainOverrides;
                if (g_frameCounter - g_motionChainLogFrame >= 120) {
                    Log("[motChain BLOCKED %u] friend%d: anim=%d start=%.2f blend=%.2f (blocked=%u)",
                        g_frameCounter, friendSlot, animId, startTime, blendParam,
                        g_motionChainOverrides);
                    g_motionChainLogFrame = g_frameCounter;
                }
                return 1;  // Pretend success — don't call original
            }

            // This is our own call (via FUN_1403c86a0 from HookedFriendAI).
            // Replace animId with our target and pass through.
            int friendIdx = friendSlot - 1;
            float mag = (friendIdx >= 0 && friendIdx < 2)
                ? g_lastStickMagnitude[friendIdx] : 0.0f;

            int targetAnim;
            if (mag < STICK_DEADZONE) {
                targetAnim = 0;   // IDLE
            } else if (mag < 0.7f) {
                targetAnim = 1;   // WALK
            } else {
                targetAnim = 2;   // RUN
            }

            if (animId != targetAnim) {
                animId = targetAnim;
            }
        }
    }

    if (g_origMotionChainSetAnim) {
        return g_origMotionChainSetAnim(motCtrl, animId, startTime, blendParam);
    }
    return 0;
}

// NOTE (VUH-1501, 2026-10-02): 0x3D5E50 is not a movement dispatch. It's
// the TakeDamage helper (actor, delta, statIdx, reactFlag): it adds drive
// gauge to the victim and calls ApplyStatDelta 0x3D2EB0, and a live enemy
// hit on Sora passes through it (stack ...3D613C <- 3A8DC5). The hook
// below only counts and logs calls, so it's left in place under its old
// name; "speedDelta"/"channel" are really the stat delta and stat index.
// Diagnostic counters for movement dispatch hook
static uint32_t g_movDispatchTotalCalls = 0;
static uint32_t g_movDispatchFriendCalls = 0;
static uint32_t g_movDispatchLogFrame = 0;

static void MovementDispatchBody(void* actor, int speedDelta, int channel, uint8_t flag) {
    auto actorAddr = reinterpret_cast<uintptr_t>(actor);
    ++g_movDispatchTotalCalls;

    // Check if this actor is a controlled friend
    if (g_soloTestMode && channel == 0) {
        int friendSlot = 0;
        if (g_friend1Actor != 0 && actorAddr == g_friend1Actor) friendSlot = 1;
        else if (g_friend2Actor != 0 && actorAddr == g_friend2Actor) friendSlot = 2;

        if (friendSlot != 0) {
            ++g_movDispatchFriendCalls;

            // Diagnostic: read the bit-2 flag that gates both decel and accel
            // paths for friends. If bit 2 is set, the original function is a
            // NOP for this entity regardless of our delta replacement.
            uint32_t flags9B8 = 0;
            __try {
                flags9B8 = *reinterpret_cast<uint32_t*>(actorAddr + 0x9B8);
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
            bool bit2Set = (flags9B8 >> 2) & 1;

            // Log periodically (every ~1s at 60fps)
            if (g_frameCounter - g_movDispatchLogFrame >= 60) {
                Log("[movDisp %u] friend%d: delta=%d ch=%d flag=%d "
                    "flags9B8=0x%08X bit2=%d (NOP=%s) totalCalls=%u friendCalls=%u",
                    g_frameCounter, friendSlot, speedDelta, channel, flag,
                    flags9B8, bit2Set ? 1 : 0,
                    bit2Set ? "YES" : "no",
                    g_movDispatchTotalCalls, g_movDispatchFriendCalls);
                g_movDispatchLogFrame = g_frameCounter;
            }

            // NOTE: Session 4 RE discovery — the original FUN_1403d5e50 is
            // gated by actor+0x9B8 bit 2:
            //   - FUN_1403d3cf0 (decel handler): returns immediately if bit 2 SET
            //   - FUN_1403d2eb0 (accumulator):   returns 0 if bit 2 SET and delta < 0
            //   For friends, bit 2 is SET → both paths are NOPs.
            // The delta replacement below has NO EFFECT on the animation.
            // Animation is now handled by direct FUN_1403c86a0 calls in
            // HookedFriendAI (see above). We still pass through to the
            // original for any other side-effects it may have.
        }
    }

    // Pass through to original for ALL entities (including friends).
    // For friends this is effectively a NOP but we don't want to break
    // any subtle side-effects by skipping it.
    if (g_origMovementDispatch) {
        g_origMovementDispatch(actor, speedDelta, channel, flag);
    }
}

static void __fastcall HookedMovementDispatch(void* actor, int speedDelta, int channel, uint8_t flag) {
    using namespace nativehittrace;
    if (!CanCaptureChild()) { MovementDispatchBody(actor, speedDelta, channel, flag); return; }
    const auto address = reinterpret_cast<uintptr_t>(actor);
    uintptr_t callerRva = 0;
    const bool callerAvailable = HitTraceCaller(reinterpret_cast<uintptr_t>(_ReturnAddress()), callerRva);
    ChildToken token {};
    const ActorSnapshot before = CaptureHitActor(address);
    BeginTake(token, address, speedDelta, channel, flag, callerRva, callerAvailable, before);
    ActorSnapshot after {};
    bool normal = false;
    __try {
        MovementDispatchBody(actor, speedDelta, channel, flag);
        normal = true;
        if (token.active) after = CaptureHitActor(address);
    } __finally {
        EndTake(token, normal && !AbnormalTermination(),
                normal && !AbnormalTermination() ? &after : nullptr);
    }
}

// ============================================================================
// Friend AI hook — intercepts vtable+0x10 dispatch
//
// When PerEntityUpdate processes a friend entity, it sets
// g_currentFriendSlot before calling the original. The original calls
// vtable+0x10 (AI dispatch), which lands here.
//
// The original AI runs for animation playback, and its call to the movement
// dispatch (FUN_1403d5e50) is intercepted by HookedMovementDispatch above,
// which replaces the follow-distance speed delta with our stick-based one.
// ============================================================================

// Puppet motion: from the friend AI hook for companions (replacing the AI),
// after the entity update for Sora clones (which have no friend AI).
static void BindPuppetDrive(int index, uintptr_t actor) {
    PuppetDriver& d = g_puppets[index];
    if (d.actor != actor) {  // new room / reloaded actor
        d.actor = actor;
        d.lastAnim = -1;
        d.teamSaved = false;
        d.noCollideSaved = false;
        d.applied = false;
    }

    if (!d.applied) d.boundActor = CaptureHitActor(actor);
    d.applied = true;
}

static void DrivePuppetMotion(void* actorObj, int index) {
    if (!IsPuppetActive(index) || PuppetTarget(index) != reinterpret_cast<uintptr_t>(actorObj)) return;
    const auto actor = reinterpret_cast<uintptr_t>(actorObj);
    BindPuppetDrive(index, actor);
    PuppetDriver& d = g_puppets[index];
    const auto& pose = d.pose.pose;
    uint32_t motion = pose.motionId;
    // A Sora clone has Sora's moveset. But setting motion 9 (seen during a
    // Fire cast) on a clone crashed the game (VUH-1489, 2026-10-02), so it's
    // held at idle until the spell path is understood. Attack 151 and the
    // basic motions play fine.
    if (motion == PUPPET_CLONE_UNSAFE_MOTION && IsPlayerClassActor(actor)) {
        motion = ANIM_IDLE;
    } else if (motion > PUPPET_MAX_BASIC_MOTION && !IsPlayerClassActor(actor)) {
        // Unknown to the friend's moveset for now: idle if still, else run.
        const float speed2 = pose.velocity.x * pose.velocity.x + pose.velocity.z * pose.velocity.z;
        motion = speed2 > 1.0f ? ANIM_RUN : ANIM_IDLE;
    }

    auto* motCtrl = reinterpret_cast<void*>(actor + ACTOR_MOTCTRL);
    auto* time = reinterpret_cast<float*>(actor + ACTOR_MOTCTRL + MOTCTRL_CURRENT_TIME);
    if (static_cast<int>(motion) != d.lastAnim) {
        Log("[puppet %d] frame %u motion %d -> %u (stream %u, time %.1f) actor=%p",
            index, g_frameCounter, d.lastAnim, motion, pose.motionId, pose.motionTime, actorObj);
        g_inOurAnimSet = true;
        g_inPuppetAnimSet = true;
        g_setAnimationUnderlying(motCtrl, static_cast<int>(motion), 0.0f, 0.0f);
        g_inPuppetAnimSet = false;
        g_inOurAnimSet = false;
        d.lastAnim = static_cast<int>(motion);
    }
    if (motion == pose.motionId && std::fabs(*time - pose.motionTime) > PUPPET_TIME_DRIFT_FRAMES) {
        *time = pose.motionTime;
    }
}

// Puppet transform, after the entity's own update so it has the last word.
// KH2COOP_PUPPET_TRACE=1: log, every frame, puppet 0's position as the
// game's own update left it next to the pose we write (jitter diagnosis).
static int PuppetTraceBudget() {
    static int budget = [] {
        char v[8] = {};
        return GetEnvironmentVariableA("KH2COOP_PUPPET_TRACE", v, sizeof(v)) > 0 ? 7200 : 0;
    }();
    return budget > 0 ? budget-- : 0;
}

static void ApplyPuppetTransform(void* actorObj, int index) {
    if (!IsPuppetActive(index) || PuppetTarget(index) != reinterpret_cast<uintptr_t>(actorObj)) return;
    BindPuppetDrive(index, reinterpret_cast<uintptr_t>(actorObj));
    const auto& pose = g_puppets[index].pose.pose;
    const auto actor = reinterpret_cast<uintptr_t>(actorObj);
    const uintptr_t entity = actor + offsets::actor::ENTITY_TRANSFORM;
    if (index == 0 && PuppetTraceBudget() > 0) {
        Log("[ptrace] f=%u t=%llu game=(%.1f,%.1f,%.1f) pose=(%.1f,%.1f,%.1f) motion=%u",
            g_frameCounter, static_cast<unsigned long long>(GetTickCount64()),
            *reinterpret_cast<float*>(entity + offsets::entity::POS_X),
            *reinterpret_cast<float*>(entity + offsets::entity::POS_Y),
            *reinterpret_cast<float*>(entity + offsets::entity::POS_Z),
            pose.position.x, pose.position.y, pose.position.z, pose.motionId);
    }
    *reinterpret_cast<float*>(entity + offsets::entity::POS_X) = pose.position.x;
    *reinterpret_cast<float*>(entity + offsets::entity::POS_Y) = pose.position.y;
    *reinterpret_cast<float*>(entity + offsets::entity::POS_Z) = pose.position.z;
    *reinterpret_cast<float*>(entity + offsets::entity::ROT_Y) = pose.rotationY;
    std::memset(reinterpret_cast<void*>(actor + ACTOR_VELOCITY), 0, 3 * sizeof(float));
    // Other terms EntityPositionPhysics (0x3B89A0) adds each frame (repos-60):
    // a carried displacement vector and the acceleration block. With
    // collision off there's no ground snap, so leftovers here drift the
    // puppet until our write.
    std::memset(reinterpret_cast<void*>(actor + ACTOR_CARRIED_DISPLACEMENT), 0, 3 * sizeof(float));
    std::memset(reinterpret_cast<void*>(actor + ACTOR_ACCEL_BLOCK), 0, ACTOR_ACCEL_BLOCK_BYTES);
    if (!IsPlayerClassActor(actor)) {
        // Companion follow timer; not a known field on player-class actors.
        *reinterpret_cast<float*>(actor + ACTOR_FOLLOW_TIMER) = DISABLE_FOLLOW_TIMER;
    }

    // Untouchable: team 0 is in no attack's hit mask. Re-applied every frame
    // in case the game resets it; the original team comes back on release.
    PuppetDriver& d = g_puppets[index];
    auto* team = reinterpret_cast<uint32_t*>(actor + ACTOR_TEAM);
    if (d.actor == actor && !d.teamSaved) {
        // 0 would be our own write surviving a reset; companions are team 1.
        d.savedTeam = *team == 0 ? 1 : *team;
        d.teamSaved = true;
    }
    *team = 0;

    // Non-colliding while driven; the original bit comes back on release.
    auto* collision = reinterpret_cast<uint8_t*>(actor + ACTOR_COLLISION_FLAGS);
    if (d.actor == actor && !d.noCollideSaved) {
        d.savedNoCollide = (*collision & ACTOR_NO_COLLIDE) != 0;
        d.noCollideSaved = true;
    }
    *collision |= ACTOR_NO_COLLIDE;
}

static void __fastcall HookedFriendAI(void* typeHandler, void* actorObj) {
    const int puppet = PuppetIndexFor(reinterpret_cast<uintptr_t>(actorObj));
    if (puppet >= 0) {
        __try {
            DrivePuppetMotion(actorObj, puppet);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log("EXCEPTION in DrivePuppetMotion (puppet %d)", puppet);
        }
        return;  // never run the vanilla AI for a puppet
    }

    if (g_currentFriendSlot != 0 && g_soloTestMode) {
        // ---- Controlled friend: SKIP vanilla AI entirely ----
        //
        // Session 5 fix: The vanilla friend AI calls FUN_1403c86a0 every
        // frame (through the behavior timer) to set follow-distance-based
        // animation. Each call creates a NEW motion object at motCtrl+0x18,
        // resetting the animation to frame 0. This is why every previous
        // override approach failed — we'd set RUN, then the AI would call
        // FUN_1403c86a0(IDLE) on the very same frame, and the tick would
        // process IDLE from time 0. Even our FUN_1403c88c0 hook correctly
        // replaced the animation ID, but since FUN_1403c86a0 was called
        // every frame, the animation restarted every frame (stuck on frame 0).
        //
        // The fix: DON'T call the original AI at all. This prevents the
        // per-frame FUN_1403c86a0 calls. We call it ourselves ONLY when
        // the target animation changes (idle↔walk↔run transitions).
        // The motCtrl tick then loops our animation naturally — it just
        // advances time and handles loop boundaries without re-deciding
        // which animation to play.
        //
        // FUN_1403c86a0 writes a QWORD zero at motCtrl+0x50, clearing
        // both the queue size and queue index. This guarantees the tick
        // takes the LOOP path (queueIndex == queueSize == 0).
        //
        // Tradeoff: skipping the AI loses combat reactions, ability triggers,
        // and battle targeting. This is acceptable for now — the priority is
        // fixing movement animation. Combat AI can be re-enabled selectively
        // in a future session.

        // Inject movement velocity/facing from player's stick input
        InjectMovementInput(actorObj, g_currentFriendSlot);

        // Compute target animation from stick magnitude
        int friendIdx = g_currentFriendSlot - 1;
        if (friendIdx >= 0 && friendIdx < 2) {
            float mag = g_lastStickMagnitude[friendIdx];

            int targetAnim;
            if (mag < STICK_DEADZONE) {
                targetAnim = ANIM_IDLE;   // 0
            } else if (mag < 0.7f) {
                targetAnim = ANIM_WALK;   // 1
            } else {
                targetAnim = ANIM_RUN;    // 2
            }

            // Only call FUN_1403c86a0 when the target animation CHANGES.
            // Calling it every frame would restart the animation from frame 0
            // each time (the exact problem we're fixing).
            if (targetAnim != g_lastOverrideAnim[friendIdx]) {
                auto* motCtrl = reinterpret_cast<void*>(
                    reinterpret_cast<uintptr_t>(actorObj) + 0x158);

                int oldAnim = g_lastOverrideAnim[friendIdx];

                // Set the guard flag so HookedMotionChainSetAnim knows
                // this FUN_1403c88c0 call is ours (not the game's per-frame call).
                g_inOurAnimSet = true;
                g_setAnimationUnderlying(motCtrl, targetAnim, 0.0f, 0.0f);
                g_inOurAnimSet = false;

                g_lastOverrideAnim[friendIdx] = targetAnim;

                ++g_animOverrideCount;
                Log("[animOverride %u] friend%d: %d → %d (mag=%.2f, total=%u)",
                    g_frameCounter, g_currentFriendSlot,
                    oldAnim, targetAnim, mag, g_animOverrideCount);
            }
        }

        // Do NOT call the original AI — it would call FUN_1403c86a0
        // every frame and reset our animation.
        return;
    }

    // Non-controlled friend (or solo mode off): run the original AI normally.
    if (g_origFriendAI) {
        const int stamp = BeginNativeAiStamp(reinterpret_cast<uintptr_t>(actorObj), reinterpret_cast<uintptr_t>(typeHandler));
        bool normal = false;
        __try { g_origFriendAI(typeHandler, actorObj); normal = true; }
        __finally { EndNativeAiStamp(stamp, normal && !AbnormalTermination()); }
    }
}

static void __fastcall HookedFriendPrePhysics(void* typeHandler, void* actorObj) {
    // Always call original — same reasoning as HookedFriendAI.
    if (g_origFriendPrePhysics) {
        g_origFriendPrePhysics(typeHandler, actorObj);
    }
}

// ============================================================================
// Follow-steering hook — intercepts vtable+0x40 (the actual tether)
//
// EntityPositionPhysics calls vtable+0x40 on the type handler to compute
// follow-steering velocity. The result is written directly to actor+0xB98,
// overriding any velocity we set in the AI or pre-physics hooks. This is
// the function that makes friends follow Sora — the "magnetism" / tether.
//
// For controlled friends: return a zero vector (no follow steering).
// For other entities: call the original.
// ============================================================================

static alignas(16) float g_zeroVec4[4] = {0.0f, 0.0f, 0.0f, 0.0f};

static void* __fastcall HookedFollowSteering(void* typeHandler, void* outVec4,
                                               void* entity, float dt) {
    if (IsDrivenFriend(g_currentFriendSlot)) {
        // Controlled friend: zero the output so physics gets no follow-steering.
        // Return pointer to our zero buffer so the caller's MEMCPY_4FLOATS
        // copies zeroes into the velocity field.
        memset(outVec4, 0, 16);
        return outVec4;
    }

    if (g_origFollowSteering) {
        return g_origFollowSteering(typeHandler, outVec4, entity, dt);
    }

    memset(outVec4, 0, 16);
    return outVec4;
}

// Diagnostic only: callback entry liveness, not native thread ownership or a
// script-freeze predicate. Published before enabling the existing input hook.
static std::atomic<uintptr_t> g_inputTraceBase {0};
static std::atomic<unsigned long long> g_inputTraceOwnerFrame {0};
static std::atomic<DWORD> g_inputTraceOwnerTid {0};
static SRWLOCK g_inputTraceLogLock = SRWLOCK_INIT;

struct InputTraceSample {
    std::int32_t eventState, frozen, pauseBlockers;
    uintptr_t eventContext;
    std::uint8_t menu;
    unsigned available;
    DWORD error;
};

template <typename T>
static void ReadInputTraceField(uintptr_t address, T& value, unsigned bit,
                                InputTraceSample& sample) {
    __try {
        value = *reinterpret_cast<const volatile T*>(address);
        sample.available |= bit;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (!sample.error) sample.error = GetExceptionCode();
    }
}

static InputTraceSample ReadInputTraceSample(uintptr_t base) {
    InputTraceSample s {};
    ReadInputTraceField(base + offsets::CUTSCENE_STATE, s.eventState, 1, s);
    ReadInputTraceField(base + offsets::EVENT_CONTEXT, s.eventContext, 2, s);
    ReadInputTraceField(base + offsets::OPEN_MENU, s.menu, 4, s);
    ReadInputTraceField(base + offsets::CONTROLLABLE, s.frozen, 8, s);
    ReadInputTraceField(base + offsets::PAUSE_STATUS, s.pauseBlockers, 16, s);
    return s; // The event-context pointer is an identity only; never dereferenced.
}

static bool SameInputTraceSample(const InputTraceSample& a, const InputTraceSample& b) {
    return a.available == b.available && a.error == b.error &&
        a.eventState == b.eventState && a.eventContext == b.eventContext &&
        a.menu == b.menu && a.frozen == b.frozen && a.pauseBlockers == b.pauseBlockers;
}

static void TraceInputCallback() {
    const auto base = g_inputTraceBase.load(std::memory_order_acquire);
    if (!base) return;
    // Independent atomic observations, not a coherent frame/thread binding.
    const auto ownerFrame = g_inputTraceOwnerFrame.load(std::memory_order_acquire);
    const auto ownerTid = g_inputTraceOwnerTid.load(std::memory_order_relaxed);
    // Trivial TLS, no destructor, allocation, environment lookup or entity-frame
    // globals. A new thread starts a separate count; no main-thread assumption.
    struct TraceState {
        unsigned long long calls, lastCalls, lastMs, windowMs, suppressed;
        unsigned emitted;
        InputTraceSample before, after;
    };
    static thread_local TraceState t {};
    ++t.calls;
    const auto now = GetTickCount64();
    const auto before = ReadInputTraceSample(base);
    const auto after = ReadInputTraceSample(base);
    const bool first = t.calls == 1;
    const bool changed = first || !SameInputTraceSample(before, t.before) ||
        !SameInputTraceSample(after, t.after);
    t.before = before;
    t.after = after;
    if (first || now - t.windowMs >= 2000) {
        t.windowMs = now;
        t.emitted = 0;
    }
    const bool heartbeat = !first && now - t.lastMs >= 2000;
    if (!first && !changed && !heartbeat) return;
    if (t.emitted >= 8) {
        if (changed) ++t.suppressed;
        return;
    }

    // Only the logging part touches the existing logger. Shutdown disables the
    // probe under this lock before the logger can close; reads above are local.
    AcquireSRWLockShared(&g_inputTraceLogLock);
    __try {
        if (g_inputTraceBase.load(std::memory_order_acquire) != base) return;
        Log("[input-callback] tid=%lu calls=%llu ms=%llu deltaCalls=%llu deltaMs=%llu "
            "reason=%s suppressedChanges=%llu available=%02X/%02X error=%08lX/%08lX "
            "bracketEqual=%u eventState=%d/%d eventContext=%llX/%llX "
            "menu=%u/%u frozen=%d/%d pauseBlockers=%08X/%08X ownerFrame=%llu ownerTid=%lu",
            GetCurrentThreadId(), t.calls, now, t.calls - t.lastCalls,
            first ? 0ULL : now - t.lastMs, first ? "first" : changed ? "change" : "heartbeat",
            t.suppressed, before.available, after.available, before.error, after.error,
            before.available == 31 && after.available == 31 &&
                SameInputTraceSample(before, after) ? 1u : 0u,
            before.eventState, after.eventState,
            static_cast<unsigned long long>(before.eventContext),
            static_cast<unsigned long long>(after.eventContext),
            static_cast<unsigned>(before.menu), static_cast<unsigned>(after.menu),
            before.frozen, after.frozen, static_cast<unsigned>(before.pauseBlockers),
            static_cast<unsigned>(after.pauseBlockers), ownerFrame, ownerTid);
        t.lastCalls = t.calls;
        t.lastMs = now;
        t.suppressed = 0;
        ++t.emitted;
    } __finally {
        ReleaseSRWLockShared(&g_inputTraceLogLock);
    }
}

static void __fastcall HookedInputCollector(void* inputStruct) {
    TraceInputCallback();
    const bool control = eventholdnative::EnterInput();
    __try {
        if (g_origInputCollector) g_origInputCollector(inputStruct);
        __try {
            const bool held = control && eventholdnative::HoldingInput();
            if (PollMailbox() && !held) ApplyPrimaryMailboxInput(inputStruct);
            if (control && eventholdnative::ApplyInput(inputStruct)) {
                // Consume held automation, retaining friends' independent input.
                // The original collector supplies fresh physical input every call.
                g_primaryMailboxPad = {};
                g_primaryRawButtons = 0;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            if (control) eventholdnative::AbortInput();
            Log("EXCEPTION in HookedInputCollector post-call");
        }
    } __finally {
        if (control) eventholdnative::LeaveInput();
    }
}

// ============================================================================
// Discover and hook the friend AI vtable+0x10 function
//
// Called once when a friend entity is first encountered in PerEntityUpdate.
// Uses the game's own ResolveEntityType function to look up the friend's
// type handler, then reads vtable+0x10 to find the AI function.
// ============================================================================

static bool DiscoverAndHookFriendAI(void* actorObj) {
    if (!g_resolveEntityType) return false;

    uint32_t typeId = *reinterpret_cast<uint32_t*>(actorObj);

    // Resolve type handler via the game's own function
    void* typeHandler = nullptr;
    __try {
        typeHandler = g_resolveEntityType(typeId);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("  ERROR: ResolveEntityType crashed for typeId=%u", typeId);
        return false;
    }

    if (!typeHandler) {
        Log("  ResolveEntityType returned null for typeId=%u", typeId);
        return false;
    }

    // Read vtable pointer (first QWORD of type handler)
    uintptr_t vtable = *reinterpret_cast<uintptr_t*>(typeHandler);
    if (vtable == 0) {
        Log("  Type handler vtable is null");
        return false;
    }

    // Read vtable+0x10 — the AI dispatch function
    void* aiFunc = *reinterpret_cast<void**>(vtable + 0x10);
    if (!aiFunc) {
        Log("  vtable+0x10 is null");
        return false;
    }

    // Read vtable+0x28 — pre-physics friend steering / orientation update.
    void* prePhysicsFunc = *reinterpret_cast<void**>(vtable + 0x28);
    if (!prePhysicsFunc) {
        Log("  vtable+0x28 is null");
        return false;
    }

    // NOTE: vtable+0x40 on handler-from-actor+0x00 is NOT the follow-steering
    // tether. That callback has signature (handler, actor)->char and is called
    // from PerEntityUpdate. The ACTUAL follow-steering is at vtable+0x40 on a
    // DIFFERENT handler resolved from actor+0x0C, called from inside
    // EntityPositionPhysics with signature (handler, outVec4, entity, dt)->ptr.
    // Hooking the wrong one with the wrong calling convention corrupts entity
    // state. De-tethering is handled by holding actor+0xBA8 positive instead.

    Log("  Friend hooks discovered: ai=%p prePhysics=%p typeId=%u handler=%p vtable=%p",
        aiFunc, prePhysicsFunc, typeId, typeHandler, reinterpret_cast<void*>(vtable));

    if (g_hookedAITarget != aiFunc) {
        MH_STATUS status = MH_CreateHook(
            aiFunc, reinterpret_cast<void*>(&HookedFriendAI),
            reinterpret_cast<void**>(&g_origFriendAI));

        if (status != MH_OK) {
            Log("  ERROR: MH_CreateHook(ai) failed: %d (%s)",
                status, MH_StatusToString(status));
            return false;
        }

        status = MH_EnableHook(aiFunc);
        if (status != MH_OK) {
            Log("  ERROR: MH_EnableHook(ai) failed: %d (%s)",
                status, MH_StatusToString(status));
            return false;
        }

        g_hookedAITarget = aiFunc;
        g_friendAIHooked = true;
        Log("  Friend AI hook installed successfully at %p", aiFunc);
    } else {
        g_friendAIHooked = true;
    }

    if (g_hookedPrePhysicsTarget != prePhysicsFunc) {
        MH_STATUS status = MH_CreateHook(
            prePhysicsFunc, reinterpret_cast<void*>(&HookedFriendPrePhysics),
            reinterpret_cast<void**>(&g_origFriendPrePhysics));

        if (status != MH_OK) {
            Log("  ERROR: MH_CreateHook(prePhysics) failed: %d (%s)",
                status, MH_StatusToString(status));
            return false;
        }

        status = MH_EnableHook(prePhysicsFunc);
        if (status != MH_OK) {
            Log("  ERROR: MH_EnableHook(prePhysics) failed: %d (%s)",
                status, MH_StatusToString(status));
            return false;
        }

        g_hookedPrePhysicsTarget = prePhysicsFunc;
        g_friendPrePhysicsHooked = true;
        Log("  Friend pre-physics hook installed successfully at %p", prePhysicsFunc);
    } else {
        g_friendPrePhysicsHooked = true;
    }

    // vtable+0x40 hook removed — see note above about calling convention mismatch.
    // De-tethering uses follow-timer suppression (actor+0xBA8 = 999.0) instead.
    g_followSteeringHooked = true;  // mark as "done" so the detection gate doesn't re-fire

    return g_friendAIHooked && g_friendPrePhysicsHooked;
}

// ============================================================================
// PerEntityUpdate hook — main interception point
//
// Called for entities selected by the native dependency/update passes.
// Callback coverage is not a complete active-list census. For non-friends,
// passes through to the original. For friends, sets the slot indicator so
// the AI hook knows to suppress AI and inject input.
// ============================================================================

static void __fastcall HookedPerEntityUpdate(void* actorObj) {
    __try {
        // Detect frame boundary: if this is the entity list head, a new
        // frame has started. This works even when OnFrame() is never called
        // (e.g., CE injection without Panacea).
        uintptr_t addr = reinterpret_cast<uintptr_t>(actorObj);
        {
            uintptr_t listHead = 0;
            __try {
                listHead = *reinterpret_cast<uintptr_t*>(
                    g_exeBase + offsets::active_entity_list::HEAD);
            } __except (EXCEPTION_EXECUTE_HANDLER) {}

            if (addr == listHead && listHead != 0) {
                ++g_frameCounter;
                lifetimetrace::Drain(&Log);
                privatestatus::Drain(&Log); // Only actual registered owner drains.
                if (g_inputTraceBase.load(std::memory_order_acquire)) {
                    g_inputTraceOwnerTid.store(GetCurrentThreadId(), std::memory_order_relaxed);
                    g_inputTraceOwnerFrame.fetch_add(1, std::memory_order_release);
                }
                ClearNativeAiStamps();
                g_processedStickFrame = UINT32_MAX;  // allow fresh snapshot

                // Track Sora's actor — he's always the entity list head.
                // Needed for entity-level movement suppression.
                g_soraActor = addr;
                downedspike::Tick(addr);

                // Hand pending room warps to the game on its own thread.
                enemysync::OnFrameStart(g_frameCounter);
                warp::OnFrameStart(g_frameCounter, addr);
                BeginCloneFrame();
                PollPuppetPoses();
                ProcessHitRequest();
                BeginHandleFrame();
            }
        }
        NoteActorForClones(addr);
        NoteActorHandle(addr);
        enemysync::NoteActor(addr);

        // Check hotkey (handles standalone mode where OnFrame isn't called)
        CheckTestModeHotkey();

        // Snapshot the processed stick once per frame, BEFORE we zero the
        // movement stick. The frame-boundary detection above resets the flag
        // so this fires exactly once per frame on the first entity.
        if (g_soloTestMode && g_processedStickFrame != g_frameCounter) {
            TryReadSoloProcessedStick(&g_processedStickSnapshot);
            g_processedStickFrame = g_frameCounter;
        }

        // Refresh friend pointers (direct memory dereference, negligible)
        RefreshFriendPointers();

        // Identify friend entities
        if (g_friend1Actor != 0 && addr == g_friend1Actor) {
            g_currentFriendSlot = 1;
        } else if (g_friend2Actor != 0 && addr == g_friend2Actor) {
            g_currentFriendSlot = 2;
        } else {
            g_currentFriendSlot = 0;
        }

        // Log friend detection and install the AI hook once per session.
        if (g_currentFriendSlot != 0 &&
            (!g_friendAIHooked || !g_friendPrePhysicsHooked || !g_followSteeringHooked)) {
            Log("=== Friend entity detected (slot %d) at actor=%p ===",
                g_currentFriendSlot, actorObj);

            __try {
                auto actor = reinterpret_cast<uint8_t*>(actorObj);
                Log("  actor handle      = 0x%08X",
                    *reinterpret_cast<uint32_t*>(actor));
                Log("  actor type id     = 0x%08X",
                    *reinterpret_cast<uint32_t*>(actor + 0x0C));
                Log("  actor+0x640 entity= %p",
                    actor + offsets::actor::ENTITY_TRANSFORM);
                Log("  actor+0x9B8 flags = 0x%08X",
                    *reinterpret_cast<uint32_t*>(actor + 0x9B8));
                Log("  actor+0x9C0 state = 0x%llX",
                    static_cast<unsigned long long>(
                        *reinterpret_cast<uint64_t*>(actor + 0x9C0)));
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                Log("  (exception reading friend actor metadata)");
            }

            if (!DiscoverAndHookFriendAI(actorObj)) {
                Log("  ERROR: failed to install friend AI hook");
            }
        }

        // Read gamepads when processing a friend entity
        if (g_currentFriendSlot != 0) {
            ReadGamepads();
        }

        // In solo test mode: suppress Sora's movement and keep camera on friend.
        // IMPORTANT: we only zero the movement stick in the processed entry (+0x30)
        // and leave the camera stick (+0x20) and buttons untouched. This preserves
        // full camera orbit control on the right stick.
        if (g_soloTestMode) {
            ZeroMovementStickInProcessedEntry();
            // Camera retarget: apply every frame since the game may reset
            // camStruct+0x50 during cutscenes, room transitions, or events.
            if (g_friend1Actor != 0) {
                RetargetCameraToFriend();
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        render::InvalidateCoopHud();
        Log("EXCEPTION in HookedPerEntityUpdate pre-call, falling through");
        g_currentFriendSlot = 0;
    }

    // Always call original — even if our logic crashed, the game must continue.
    int savedFriendSlot = g_currentFriendSlot;
    g_origPerEntityUpdate(actorObj);

    // POST-UPDATE overrides — run after g_origPerEntityUpdate has finished.
    // This is AFTER the motion controller tick (FUN_1403c6740) which writes
    // the animation based on the motion chain. Our override here gets the
    // LAST WORD on the animation before rendering.
    __try {
        // Local avatar for the runtime (VUH-1490): Sora's state after his own
        // update this frame. seq carries the DLL's game-frame counter so a
        // recording shows dropped frames; the runtime restamps it to send.
        if (reinterpret_cast<uintptr_t>(actorObj) == g_soraActor && g_avatarBridge.IsOpen()) {
            const bool inEvent =
                *reinterpret_cast<const std::int32_t*>(g_exeBase + offsets::CUTSCENE_STATE) != 0 ||
                *reinterpret_cast<const uintptr_t*>(g_exeBase + offsets::EVENT_CONTEXT) != 0;
            const auto hudAuthority = enemysync::CapturePuppetAuthority();
            AvatarState avatar = captureAvatar(DirectMemory {}, g_exeBase, g_soraActor,
                                               inEvent, false);
            avatar.seq = g_frameCounter;
            g_avatarBridge.PublishLocal(avatar);
            PublishCoopHud(avatar, hudAuthority);
        }

        const int puppet = PuppetIndexFor(reinterpret_cast<uintptr_t>(actorObj));
        if (puppet >= 0) {
            // Sora clones don't run the friend AI hook, so their motion is
            // set here; companions got theirs in HookedFriendAI.
            if (IsPlayerClassActor(reinterpret_cast<uintptr_t>(actorObj))) {
                DrivePuppetMotion(actorObj, puppet);
            }
            ApplyPuppetTransform(actorObj, puppet);
        } else if (savedFriendSlot != 0) {
            // Re-inject movement input after physics overwrites.
            // The motion controller tick (FUN_1403c6740) has already run
            // by this point, so our velocity/facing here gets the LAST WORD
            // before rendering.
            InjectMovementInput(actorObj, savedFriendSlot);

            // Animation is handled by the skip-AI approach in HookedFriendAI.
            // We call FUN_1403c86a0 only on animation transitions, and the
            // motCtrl tick loops our animation naturally. No POST animation
            // override needed.
        }

        // Suppress Sora's movement at the entity level (zero velocity/accel).
        // This runs after Sora's own physics pass, preventing him from moving
        // while leaving the input system untouched for camera control.
        if (g_soloTestMode && g_soraActor != 0 &&
            reinterpret_cast<uintptr_t>(actorObj) == g_soraActor) {
            SuppressSoraMovement();
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        render::InvalidateCoopHud();
        Log("EXCEPTION in post-update override");
    }

    // Reset slot indicator
    g_currentFriendSlot = 0;
}

// ============================================================================
// Public API
// ============================================================================

static void UninitializeMinHookUnlessRetained() {
    if (resourcetrace::RetainsMinHookResources() || (lifetimetrace::RetainsMinHookResources() || privatestatus::RetainsMinHookResources())) {
        Log("[native-trace] global MinHook teardown retained until process exit");
        return;
    }
    MH_Uninitialize();
}

bool Initialize(uintptr_t exeBase) {
    if (resourcetrace::RejectReinitialization() || (lifetimetrace::RetainsMinHookResources() || privatestatus::RetainsMinHookResources())) return false;
    if (g_initialized) return true;

    g_exeBase = exeBase;
    PFN_MovementDispatch verifiedTakeDamage = nullptr;
    std::uint32_t hitTraceVerified = 0, hitTraceInstalled = 0;

    // One log per process so several instances don't clobber each other.
    // kh2ctl launch sets KH2COOP_LOG_DIR; otherwise the log lands in the
    // game's working directory.
    {
        char logDir[MAX_PATH] = {};
        const DWORD dirLen =
            GetEnvironmentVariableA("KH2COOP_LOG_DIR", logDir, MAX_PATH);
        char logPath[MAX_PATH] = {};
        if (dirLen > 0 && dirLen < MAX_PATH) {
            snprintf(logPath, sizeof(logPath), "%s\\kh2coop_inject_%lu.log",
                     logDir, GetCurrentProcessId());
        } else {
            snprintf(logPath, sizeof(logPath), "kh2coop_inject_%lu.log",
                     GetCurrentProcessId());
        }
        g_logFile = fopen(logPath, "w");
    }
    Log("=== kh2coop_inject v0.2 ===");
    Log("Initializing...");
    Log("  exe base: 0x%llX", static_cast<unsigned long long>(exeBase));

    // --- Initialize MinHook ---
    MH_STATUS mhStatus = MH_Initialize();
    if (mhStatus != MH_OK && mhStatus != MH_ERROR_ALREADY_INITIALIZED) {
        Log("ERROR: MH_Initialize failed: %d (%s)",
            mhStatus, MH_StatusToString(mhStatus));
        return false;
    }
    Log("  MinHook initialized");

    // Before anything that can fail: an injected instance never writes saves.
    saveguard::Install(&Log);
    eventholdnative::Install(exeBase, &Log);
    crashdump::Install(&Log);

    // --- Find PerEntityUpdate ---
    uintptr_t perEntityUpdateAddr = 0;

    auto scanResult = PatternScan(exeBase, AOB_PER_ENTITY_UPDATE);
    if (scanResult) {
        perEntityUpdateAddr = *scanResult;
        Log("  PerEntityUpdate: AOB match at 0x%llX (RVA 0x%llX)",
            static_cast<unsigned long long>(perEntityUpdateAddr),
            static_cast<unsigned long long>(perEntityUpdateAddr - exeBase));
    } else {
        // Fallback to known RVA
        perEntityUpdateAddr = exeBase + RVA_PER_ENTITY_UPDATE;
        Log("  WARNING: AOB scan failed, using fallback RVA 0x%llX",
            static_cast<unsigned long long>(RVA_PER_ENTITY_UPDATE));
    }

    // Validate: address should be in .text section.
    // Non-fatal — some PE layouts or protections may report wrong section sizes.
    // The real KH2 .text is ~5.7MB; if FindTextSection reports < 1MB, the check
    // is unreliable and we proceed anyway (MH_CreateHook will fail safely if
    // the address is truly invalid).
    auto textInfo = FindTextSection(exeBase);
    if (textInfo) {
        constexpr size_t MIN_PLAUSIBLE_TEXT_SIZE = 0x100000;  // 1MB
        if (textInfo->size < MIN_PLAUSIBLE_TEXT_SIZE) {
            Log("  WARNING: .text section only %llu bytes (expected ~5.7MB) — skipping validation",
                static_cast<unsigned long long>(textInfo->size));
        } else if (perEntityUpdateAddr < textInfo->start ||
                   perEntityUpdateAddr >= textInfo->start + textInfo->size) {
            Log("  WARNING: PerEntityUpdate 0x%llX is outside .text [0x%llX..0x%llX] — proceeding anyway",
                static_cast<unsigned long long>(perEntityUpdateAddr),
                static_cast<unsigned long long>(textInfo->start),
                static_cast<unsigned long long>(textInfo->start + textInfo->size));
        } else {
            Log("  PerEntityUpdate validated within .text [0x%llX..0x%llX]",
                static_cast<unsigned long long>(textInfo->start),
                static_cast<unsigned long long>(textInfo->start + textInfo->size));
        }
    }

    // --- Resolve ResolveEntityType ---
    g_resolveEntityType = reinterpret_cast<PFN_ResolveEntityType>(
        exeBase + RVA_RESOLVE_ENTITY_TYPE);
    Log("  ResolveEntityType: 0x%llX",
        static_cast<unsigned long long>(exeBase + RVA_RESOLVE_ENTITY_TYPE));

    // --- Resolve motion set functions (animation API) ---
    g_setMotion = reinterpret_cast<PFN_SetMotion>(exeBase + RVA_SET_MOTION);
    g_setMotionSimple = reinterpret_cast<PFN_SetMotionSimple>(exeBase + RVA_SET_MOTION_SIMPLE);
    g_setAnimationDirect = reinterpret_cast<PFN_SetAnimationDirect>(
        exeBase + RVA_SET_ANIMATION_DIRECT);
    g_setAnimationUnderlying = reinterpret_cast<PFN_SetAnimationDirect>(
        exeBase + RVA_SET_ANIMATION_UNDERLYING);
    Log("  SetMotion: 0x%llX  SetMotionSimple: 0x%llX  SetAnimDirect: 0x%llX",
        static_cast<unsigned long long>(exeBase + RVA_SET_MOTION),
        static_cast<unsigned long long>(exeBase + RVA_SET_MOTION_SIMPLE),
        static_cast<unsigned long long>(exeBase + RVA_SET_ANIMATION_DIRECT));
    Log("  SetAnimUnderlying: 0x%llX (FUN_1403c86a0 — bypasses FUN_1403a6420 check)",
        static_cast<unsigned long long>(exeBase + RVA_SET_ANIMATION_UNDERLYING));

    const auto inputCollectorAddr =
        exeBase + offsets::input::INPUT_COLLECTOR_FUNC;
    Log("  InputCollector: 0x%llX",
        static_cast<unsigned long long>(inputCollectorAddr));

    char inputTraceSetting[2] {};
    const bool inputTrace = GetEnvironmentVariableA("KH2COOP_INPUT_CALLBACK_TRACE",
        inputTraceSetting, sizeof(inputTraceSetting)) == 1 && inputTraceSetting[0] == '1';
    if (inputTrace) {
        Log("[input-callback] configured=1 entryOnly=1 heartbeatMs=2000 maxLinesPerThreadWindow=8 "
            "mask=eventState:01,context:02,menu:04,frozen:08,pauseBlockers:10 "
            "zeroWithoutAvailabilityIsUnknown=1 bracketIsNotAtomic=1");
        g_inputTraceBase.store(exeBase, std::memory_order_release);
    }

    mhStatus = MH_CreateHook(
        reinterpret_cast<void*>(inputCollectorAddr),
        reinterpret_cast<void*>(&HookedInputCollector),
        reinterpret_cast<void**>(&g_origInputCollector));
    if (mhStatus != MH_OK) {
        Log("ERROR: MH_CreateHook(InputCollector) failed: %d (%s)",
            mhStatus, MH_StatusToString(mhStatus));
        UninitializeMinHookUnlessRetained();
        return false;
    }

    mhStatus = MH_EnableHook(reinterpret_cast<void*>(inputCollectorAddr));
    if (mhStatus != MH_OK) {
        Log("ERROR: MH_EnableHook(InputCollector) failed: %d (%s)",
            mhStatus, MH_StatusToString(mhStatus));
        UninitializeMinHookUnlessRetained();
        return false;
    }

    // --- Install PerEntityUpdate hook ---
    mhStatus = MH_CreateHook(
        reinterpret_cast<void*>(perEntityUpdateAddr),
        reinterpret_cast<void*>(&HookedPerEntityUpdate),
        reinterpret_cast<void**>(&g_origPerEntityUpdate));

    if (mhStatus != MH_OK) {
        Log("ERROR: MH_CreateHook(PerEntityUpdate) failed: %d (%s)",
            mhStatus, MH_StatusToString(mhStatus));
        UninitializeMinHookUnlessRetained();
        return false;
    }

    mhStatus = MH_EnableHook(reinterpret_cast<void*>(perEntityUpdateAddr));
    if (mhStatus != MH_OK) {
        Log("ERROR: MH_EnableHook(PerEntityUpdate) failed: %d (%s)",
            mhStatus, MH_StatusToString(mhStatus));
        UninitializeMinHookUnlessRetained();
        return false;
    }

    Log("  PerEntityUpdate hook installed");

    // --- Install historical MovementDispatch hook (actually TakeDamage) ---
    {
        void* movDispAddr = reinterpret_cast<void*>(exeBase + RVA_MOVEMENT_DISPATCH);
        if (std::memcmp(movDispAddr, kTakeDamageBytes, sizeof(kTakeDamageBytes)) == 0) {
            hitTraceVerified |= 2U;
            mhStatus = MH_CreateHook(
                movDispAddr,
                reinterpret_cast<void*>(&HookedMovementDispatch),
                reinterpret_cast<void**>(&g_origMovementDispatch));
            if (mhStatus == MH_OK) mhStatus = MH_EnableHook(movDispAddr);
            if (mhStatus == MH_OK) {
                hitTraceInstalled |= 2U;
                verifiedTakeDamage = g_origMovementDispatch;
                Log("  MovementDispatch hook installed at RVA 0x%llX",
                    static_cast<unsigned long long>(RVA_MOVEMENT_DISPATCH));
            } else {
                Log("  WARNING: TakeDamage hook failed: %d (%s); network hit apply unavailable",
                    mhStatus, MH_StatusToString(mhStatus));
            }
        } else {
            Log("  WARNING: TakeDamage bytes don't match; hook and network hit apply unavailable");
        }
    }

    // --- Install MotionChainSetAnim hook (FUN_1403c88c0) ---
    // This intercepts ALL animation changes on the motion controller,
    // including the motCtrl tick's loop-boundary re-evaluation.
    {
        void* motChainAddr = reinterpret_cast<void*>(exeBase + RVA_MOTION_CHAIN_SET_ANIM);
        mhStatus = MH_CreateHook(
            motChainAddr,
            reinterpret_cast<void*>(&HookedMotionChainSetAnim),
            reinterpret_cast<void**>(&g_origMotionChainSetAnim));

        if (mhStatus == MH_OK) {
            mhStatus = MH_EnableHook(motChainAddr);
        }

        if (mhStatus == MH_OK) {
            Log("  MotionChainSetAnim hook installed at RVA 0x%llX",
                static_cast<unsigned long long>(RVA_MOTION_CHAIN_SET_ANIM));
        } else {
            Log("  WARNING: MotionChainSetAnim hook failed: %d (%s) — animation may not match stick",
                mhStatus, MH_StatusToString(mhStatus));
        }
    }

    // Screenshots, clips and the debug overlay (VUH-1485). Optional: the DLL
    // keeps working if the renderer can't be hooked.
    // Limit gate for puppets (VUH-1491): hook only when all three functions
    // match this build.
    {
        auto matches = [&](uint64_t rva, const uint8_t* bytes, size_t n) {
            return std::memcmp(reinterpret_cast<const void*>(exeBase + rva), bytes, n) == 0;
        };
        if (matches(RVA_LIMIT_MENU_STATE, kLimitMenuStateBytes, sizeof(kLimitMenuStateBytes)) &&
            matches(RVA_LIMIT_USABLE, kLimitUsableBytes, sizeof(kLimitUsableBytes)) &&
            matches(RVA_LIMIT_BY_CMD, kLimitByCmdBytes, sizeof(kLimitByCmdBytes))) {
            g_limitByCmd = reinterpret_cast<PFN_LimitLookup>(exeBase + RVA_LIMIT_BY_CMD);
            void* menuState = reinterpret_cast<void*>(exeBase + RVA_LIMIT_MENU_STATE);
            void* usable = reinterpret_cast<void*>(exeBase + RVA_LIMIT_USABLE);
            MH_STATUS st = MH_CreateHook(menuState, reinterpret_cast<void*>(&HookedLimitMenuState),
                                         reinterpret_cast<void**>(&g_origLimitMenuState));
            if (st == MH_OK) st = MH_EnableHook(menuState);
            if (st == MH_OK) {
                st = MH_CreateHook(usable, reinterpret_cast<void*>(&HookedLimitUsable),
                                   reinterpret_cast<void**>(&g_origLimitUsable));
            }
            if (st == MH_OK) st = MH_EnableHook(usable);
            if (st == MH_OK) {
                Log("  Limit gate hooks installed (0x3D88E0, 0x3E7800)");
            } else {
                Log("  WARNING: limit gate hooks failed: %d (%s)", st, MH_StatusToString(st));
            }
        } else {
            Log("  WARNING: limit gate functions don't match this build; limits not gated");
        }

        // HP funnel logging (VUH-1501).
        if (matches(RVA_APPLY_STAT_DELTA, kApplyStatDeltaBytes, sizeof(kApplyStatDeltaBytes))) {
            hitTraceVerified |= 4U;
            void* target = reinterpret_cast<void*>(exeBase + RVA_APPLY_STAT_DELTA);
            MH_STATUS st = MH_CreateHook(target, reinterpret_cast<void*>(&HookedApplyStatDelta),
                                         reinterpret_cast<void**>(&g_origApplyStatDelta));
            if (st == MH_OK) st = MH_EnableHook(target);
            if (st == MH_OK) hitTraceInstalled |= 4U;
            Log(st == MH_OK ? "  ApplyStatDelta hook installed (0x3D2EB0)"
                            : "  WARNING: ApplyStatDelta hook failed");
        } else {
            Log("  WARNING: ApplyStatDelta bytes don't match this build; HP not hooked");
        }

        if (matches(RVA_RESOLVE_HANDLE, kResolveHandleBytes, sizeof(kResolveHandleBytes))) {
            g_resolveHandle = reinterpret_cast<PFN_ResolveHandle>(exeBase + RVA_RESOLVE_HANDLE);
            Log("  Handle resolver verified (0x4AD270)");
        } else {
            Log("  WARNING: handle resolver bytes don't match; attackers resolve by address bits");
        }

        // Hit log: damage calculation post-hook (attacker + atkp in scope).
        if (matches(RVA_BUILD_HIT, kBuildHitBytes, sizeof(kBuildHitBytes))) {
            void* target = reinterpret_cast<void*>(exeBase + RVA_BUILD_HIT);
            MH_STATUS st = MH_CreateHook(target, reinterpret_cast<void*>(&HookedBuildHit),
                                         reinterpret_cast<void**>(&g_origBuildHit));
            if (st == MH_OK) st = MH_EnableHook(target);
            Log(st == MH_OK ? "  BuildHit hook installed (0x3D23C0)"
                            : "  WARNING: BuildHit hook failed");
        } else {
            Log("  WARNING: BuildHit bytes don't match this build; hits not logged");
        }

        // Hit drop filter (client side of hit ownership).
        if (matches(RVA_APPLY_HIT_DAMAGE, kApplyHitDamageBytes, sizeof(kApplyHitDamageBytes))) {
            hitTraceVerified |= 1U;
            void* target = reinterpret_cast<void*>(exeBase + RVA_APPLY_HIT_DAMAGE);
            MH_STATUS st = MH_CreateHook(target, reinterpret_cast<void*>(&HookedApplyHitDamage),
                                         reinterpret_cast<void**>(&g_origApplyHitDamage));
            if (st == MH_OK) st = MH_EnableHook(target);
            if (st == MH_OK) hitTraceInstalled |= 1U;
            Log(st == MH_OK ? "  ApplyHitDamage hook installed (0x3D3BA0)"
                            : "  WARNING: ApplyHitDamage hook failed");
        } else {
            Log("  WARNING: ApplyHitDamage bytes don't match this build; hits can't be dropped");
        }
        OpenHitChannel();
        enemysync::Install(exeBase, &Log, g_origApplyStatDelta, verifiedTakeDamage);
        enemysync::SetHashDiagnosticSink([](const std::string& row) {
            if (!g_logFile) return false;
            const bool written = fprintf(g_logFile,"%s\n",row.c_str()) >= 0;
            const bool flushed = fflush(g_logFile) == 0;
            return written && flushed && !ferror(g_logFile);
        });
    }

    char hitTraceSetting[2] {};
    const bool hitTrace = GetEnvironmentVariableA("KH2COOP_TRACE_HITS", hitTraceSetting,
                                                  sizeof(hitTraceSetting)) == 1 && hitTraceSetting[0] == '1';
    g_hitTraceImageSize = 0;
    if (hitTrace) {
        IMAGE_DOS_HEADER dos {};
        IMAGE_NT_HEADERS64 nt {};
        if (ReadHitTrace(exeBase, dos) && dos.e_magic == IMAGE_DOS_SIGNATURE &&
            dos.e_lfanew >= static_cast<LONG>(sizeof(dos)) && dos.e_lfanew <= 0x100000 &&
            ReadHitTrace(exeBase + static_cast<uintptr_t>(dos.e_lfanew), nt) &&
            nt.Signature == IMAGE_NT_SIGNATURE && nt.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
            nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
            g_hitTraceImageSize = nt.OptionalHeader.SizeOfImage;
    }
    nativehittrace::Configure(hitTrace, hitTraceVerified, hitTraceInstalled);

    render::Install(exeBase, &Log);
    warp::Install(exeBase, &Log);
    downedspike::Install(exeBase);
    char spawnTraceSetting[2] {};
    const bool spawnTrace = GetEnvironmentVariableA("KH2COOP_SPAWN_TRACE", spawnTraceSetting,
                                                   sizeof(spawnTraceSetting)) == 1 &&
                            spawnTraceSetting[0] == '1';
    spawncontroller::Install(exeBase, &Log, &enemysync::ActivationRole,
                             &enemysync::CaptureHostActivation, &enemysync::CopyHostActivation, spawnTrace);
    resourcetrace::Initialize(exeBase, spawncontroller::GetTraceStats().constructionConfigured);
    lifecycletrace::Install(exeBase, &Log, &enemysync::ActivationRole, spawnTrace);
    if (!privatestatus::Initialize(exeBase)) Log("[privatestatus] initialization refused; profile unqualified");
    lifetimetrace::Initialize(exeBase);
    if (lifetimetrace::GetStatistics().requested) {
        const auto s = lifetimetrace::GetStatistics();
        Log("[lifetimetrace] install verified=%u installed=%u failed=%u retained=%u diagnostic-only=1",
            s.verified, s.installed, s.failed, s.retained);
    }
    if (spawnTrace) {
        const auto coverage = lifecycletrace::GetStats();
        Log("[lifecycletrace] %s requested=%u verifiedMask=%u installedMask=%u failedMask=%u diagnostic-only=1",
            coverage.installedMask == lifecycletrace::AllHooks ? "ready" : "unavailable",
            coverage.requested ? 1u : 0u, coverage.verifiedMask, coverage.installedMask, coverage.failedMask);
    }
    if (g_avatarBridge.Open(GetCurrentProcessId())) {
        Log("  Avatar bridge open (Local\\kh2coop_avatar_%lu)", GetCurrentProcessId());
    } else {
        Log("  WARNING: avatar bridge failed to open (%lu)", GetLastError());
    }

    Log("  InputCollector hook installed");
    Log("Initialization complete — waiting for friend entities...");
    Log("  Press F5 to toggle solo test mode (control Friend1 with KH2 controller 0)");

    // Try to open the network input mailbox (runtime may not be running yet).
    // If not available now, we'll retry periodically in PollMailbox().
    if (g_mailboxReader.Open()) {
        g_mailboxAvailable = true;
        ClearMailboxCachedState();
        Log("  Input source: network mailbox (runtime PID=%lu)",
            static_cast<unsigned long>(g_mailboxReader.RuntimePid()));
    } else {
        Log("  Input source: KH2 raw input buffer (local gamepads)");
        Log("  Network mailbox not available — will retry periodically");
    }

    g_initialized = true;
    return true;
}

void Shutdown() {
    render::InvalidateCoopHud();
    eventholdnative::Disable();
    AcquireSRWLockExclusive(&g_inputTraceLogLock);
    g_inputTraceBase.store(0, std::memory_order_release);
    ReleaseSRWLockExclusive(&g_inputTraceLogLock);
    privatestatus::StopNewAllocations();
    lifetimetrace::StopRecording();
    resourcetrace::StopRecording(); // Retired callbacks use only process-lifetime storage.
    if (!g_initialized) return;

    Log("Shutting down...");

    // Restore camera before unhooking (game needs its original pointer back)
    RestoreCameraToSora();

    // Close network input mailbox
    if (g_mailboxAvailable) {
        g_mailboxReader.Close();
        g_mailboxAvailable = false;
        ClearMailboxCachedState();
        Log("  Network input mailbox closed");
    }

    render::Shutdown();
    nativehittrace::Shutdown(); // Quiescent teardown: stop observations before hooks/context retire.
    ClearNativeAiStamps();
    lifecycletrace::Shutdown();
    spawncontroller::Shutdown();
    warp::Shutdown();
    enemysync::Shutdown();
    MH_DisableHook(MH_ALL_HOOKS);
    UninitializeMinHookUnlessRetained();

    g_initialized = false;
    g_friendAIHooked = false;
    g_friendPrePhysicsHooked = false;
    g_followSteeringHooked = false;
    g_origPerEntityUpdate = nullptr;
    g_origFriendAI = nullptr;
    g_origFriendPrePhysics = nullptr;
    g_origFollowSteering = nullptr;
    g_origMovementDispatch = nullptr;
    g_origMotionChainSetAnim = nullptr;
    g_resolveEntityType = nullptr;
    g_hookedAITarget = nullptr;
    g_hookedPrePhysicsTarget = nullptr;
    g_hookedFollowSteeringTarget = nullptr;
    g_friend1Actor = 0;
    g_friend2Actor = 0;
    g_soraActor = 0;
    g_setAnimationDirect = nullptr;
    g_setAnimationUnderlying = nullptr;
    g_facingAngleValid[0] = g_facingAngleValid[1] = false;
    g_lastStickMagnitude[0] = g_lastStickMagnitude[1] = 0.0f;
    g_lastOverrideAnim[0] = g_lastOverrideAnim[1] = -1;
    g_animOverrideCount = 0;
    ClearMailboxCachedState();

    if (g_logFile) {
        Log("Shutdown complete");
        fclose(g_logFile);
        g_logFile = nullptr;
    }
}

void OnFrame() {
    if (!g_initialized) return;

    ++g_frameCounter;

    // Check F5 hotkey for solo test mode toggle
    CheckTestModeHotkey();

    // Read controller samples once per frame (for Panacea plugin mode)
    ReadGamepads();

    // In solo test mode: zero movement stick and maintain camera every frame
    if (g_soloTestMode) {
        ZeroMovementStickInProcessedEntry();
        RefreshFriendPointers();
        if (g_friend1Actor != 0) {
            RetargetCameraToFriend();
        }
    }

    // Periodic status log (~5 seconds at 60fps)
    if (g_frameCounter % 300 == 0) {
        RefreshFriendPointers();
        Log("[frame %u] f1=%p f2=%p aiHook=%d pad1=%d pad2=%d solo=%d sora=%p cam=%d pads=%d active=%d mailbox=%d",
            g_frameCounter,
            reinterpret_cast<void*>(g_friend1Actor),
            reinterpret_cast<void*>(g_friend2Actor),
            g_friendAIHooked ? 1 : 0,
            g_gamepad[0].connected ? 1 : 0,
            g_gamepad[1].connected ? 1 : 0,
            g_soloTestMode ? 1 : 0,
            reinterpret_cast<void*>(g_soraActor),
            g_cameraRetargeted ? 1 : 0,
            g_inputControllerCount,
            g_activeInputSlot,
            g_mailboxAvailable ? 1 : 0);
    }
}

} // namespace inject
} // namespace kh2coop
