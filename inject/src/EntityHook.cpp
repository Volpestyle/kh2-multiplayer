// ============================================================================
// EntityHook — PerEntityUpdate hook + friend AI replacement
//
// This is the core of Strategy B (AI replacement hook). It:
//   1. Hooks PerEntityUpdate (exe+0x3BFD30) via MinHook detour
//   2. Identifies friend entities by comparing actor pointers against
//      the known friend actor pointers from Slot1+0x220/+0x228
//   3. Discovers the friend AI vtable+0x10 function at runtime
//   4. Hooks the friend AI function to suppress AI and inject player input
//   5. Reads input from network mailbox (if runtime is connected) or
//      local gamepads for Friend1 (gamepad 1) and Friend2 (gamepad 2)
//   6. Writes movement velocity/acceleration to actor struct fields
//      that EntityPositionPhysics reads for movement integration
//
// The game's entity update call chain:
//   EntityUpdateLoop (exe+0x3BF5E0)
//     └─► PerEntityUpdate (exe+0x3BFD30) — OUR HOOK
//           ├─► vtable+0x10 — AI dispatch (suppressed for friends)
//           ├─► vtable+0x18 — post-main update
//           ├─► vtable+0x28 — pre-physics update
//           └─► EntityPositionPhysics (exe+0x3B89A0) — runs normally
//
// All hook functions run on the game's main thread (single-threaded).
// No synchronization is needed between PerEntityUpdate and AI hooks.
// ============================================================================

#include "EntityHook.hpp"
#include "PatternScan.hpp"
#include "kh2coop/KH2Offsets.hpp"
#include "kh2coop/InputMailbox.hpp"

#include <Windows.h>
#include <MinHook.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace kh2coop {
namespace inject {

// ============================================================================
// Function pointer typedefs (from Ghidra decompilation)
// ============================================================================

// PerEntityUpdate: void(void* actorObj)
//   x64 ABI: RCX = actorObj (the actor/entity object pointer)
//   Ghidra sig: void PerEntityUpdate(undefined4* param_1)
using PFN_PerEntityUpdate = void(__fastcall*)(void* actorObj);

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
static PFN_ResolveEntityType g_resolveEntityType     = nullptr;
static PFN_FriendAI          g_origFriendAI          = nullptr;
static PFN_FriendAI          g_origFriendPrePhysics  = nullptr;
static PFN_FollowSteering   g_origFollowSteering    = nullptr;

// Motion set function pointers (not hooked — called directly)
static PFN_SetMotion        g_setMotion             = nullptr;
static PFN_SetMotionSimple  g_setMotionSimple       = nullptr;

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

// (Animation is driven by calling the game's motion set functions every frame,
// replicating what the original friend AI does. See InjectMovementInput.)

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
static bool     g_mailboxAvailable     = false;
static uint32_t g_lastMailboxCheckFrame = 0;
static constexpr uint32_t MAILBOX_RETRY_INTERVAL = 120;  // ~2 sec at 60fps

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

static bool TryReadMailbox() {
    if (!g_mailboxAvailable) {
        // Periodically retry opening the mailbox (runtime may start later)
        if (g_frameCounter - g_lastMailboxCheckFrame >= MAILBOX_RETRY_INTERVAL) {
            g_lastMailboxCheckFrame = g_frameCounter;
            if (g_mailboxReader.Open()) {
                g_mailboxAvailable = true;
                Log("Network input mailbox CONNECTED (runtime PID=%lu)",
                    static_cast<unsigned long>(g_mailboxReader.RuntimePid()));
            }
        }
        if (!g_mailboxAvailable) return false;
    }

    // Periodic liveness check (~every 2s): verify the runtime process is still
    // alive. If it died, close the stale mapping and fall back to local gamepads.
    if (g_frameCounter - g_lastMailboxCheckFrame >= MAILBOX_RETRY_INTERVAL) {
        g_lastMailboxCheckFrame = g_frameCounter;
        DWORD rtPid = g_mailboxReader.RuntimePid();
        if (rtPid != 0) {
            HANDLE hProc = OpenProcess(SYNCHRONIZE, FALSE, rtPid);
            if (!hProc) {
                Log("Network input mailbox DISCONNECTED — runtime PID=%lu no longer alive, falling back to local gamepads",
                    static_cast<unsigned long>(rtPid));
                g_mailboxReader.Close();
                g_mailboxAvailable = false;
                return false;
            }
            CloseHandle(hProc);
        }
    }

    bool anyRead = false;

    for (int padIdx = 0; padIdx < 2; ++padIdx) {
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
        // If TryReadSlot returns false, the previous GamepadState is retained
        // (from a prior mailbox read within this frame's loop iteration).
    }

    return anyRead;
}

static void ReadGamepads() {
    using namespace offsets;

    g_inputControllerCount = 0;
    g_activeInputSlot = -1;

    // Try network mailbox first — if the runtime is delivering remote input,
    // skip the local gamepad read entirely. This is the P3 IPC path.
    // NOTE: Do NOT zero g_gamepad[] before this call. TryReadMailbox() writes
    // only the slots that have new data; slots without new data retain their
    // previous values from the prior frame.
    if (TryReadMailbox()) {
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

    // In solo mode, prefer the per-frame processed-stick snapshot for pad 0.
    // It was captured BEFORE SuppressSoraInput zeroed the processed entry, so
    // it reflects the real physical left/right stick mapping the game uses.
    if (g_soloTestMode && g_processedStickFrame == g_frameCounter) {
        auto& snap = g_processedStickSnapshot;
        if (snap.connected) {
            snap.buttons = g_gamepad[0].buttons;
            g_gamepad[0] = snap;
        }
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
// When the friend AI is suppressed, the velocity/acceleration fields retain
// stale values. We must write something every frame to prevent drift.
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
        const float speed = (magnitude > 0.7f) ? RUN_SPEED : WALK_SPEED;
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

    // Update facing direction from the world-space velocity vector.
    //
    // KH2 facing convention (verified via CE live read of Sora's entity):
    //   ROT_Y (+0x4C) = atan2(velX, velZ)
    //   +0x40 (labeled COS_FACING in offsets) = sin(ROT_Y)  [historically mislabeled]
    //   +0x48 (labeled SIN_FACING in offsets) = cos(ROT_Y)  [historically mislabeled]
    //
    // Previous code used atan2(-velX, -velZ) which adds PI, flipping forward/back.
    // Left/right worked because the lateral symmetry canceled the PI offset, but
    // forward/backward was 180° wrong.
    if (magnitude > 0.01f) {
        using namespace offsets;
        uintptr_t entityBase = reinterpret_cast<uintptr_t>(actor) +
                               actor::ENTITY_TRANSFORM;

        float angle = std::atan2(velX, velZ);
        *reinterpret_cast<float*>(entityBase + entity::ROT_Y)      = angle;
        // +0x40 stores sin(angle), +0x48 stores cos(angle)
        // (the offset names COS_FACING/SIN_FACING are swapped relative to math convention)
        *reinterpret_cast<float*>(entityBase + entity::COS_FACING)  = std::sin(angle);
        *reinterpret_cast<float*>(entityBase + entity::SIN_FACING)  = std::cos(angle);
    }

    // Animation is handled by the original friend AI (which we no longer
    // suppress). The AI calls the game's motion set functions every frame,
    // and the animation system picks idle/walk/run based on entity state.
}

// ============================================================================
// Friend AI hook — intercepts vtable+0x10 dispatch
//
// When PerEntityUpdate processes a friend entity, it sets
// g_currentFriendSlot before calling the original. The original calls
// vtable+0x10 (AI dispatch), which lands here. If the entity is a
// controlled friend, we inject gamepad input and skip AI.
// ============================================================================

static void __fastcall HookedFriendAI(void* typeHandler, void* actorObj) {
    // ALWAYS call the original AI — it drives the animation system via
    // the game's motion set functions (FUN_1403b6670/FUN_1403b6630).
    // Without these calls, the entity has no animation playback at all.
    //
    // The original AI also computes follow-steering, but that's intercepted
    // separately by HookedFollowSteering which returns zeros for controlled
    // friends. Movement is overridden in the post-physics path of
    // HookedPerEntityUpdate. So the AI runs for animations only.
    if (g_origFriendAI) {
        g_origFriendAI(typeHandler, actorObj);
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
    if (g_soloTestMode && g_currentFriendSlot != 0) {
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
// Called for EVERY entity every frame. For non-friends, passes through
// to the original immediately. For friends, sets the slot indicator so
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
                g_processedStickFrame = UINT32_MAX;  // allow fresh snapshot

                // Track Sora's actor — he's always the entity list head.
                // Needed for entity-level movement suppression.
                g_soraActor = addr;
            }
        }

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
        Log("EXCEPTION in HookedPerEntityUpdate pre-call, falling through");
        g_currentFriendSlot = 0;
    }

    // Always call original — even if our logic crashed, the game must continue.
    int savedFriendSlot = g_currentFriendSlot;
    g_origPerEntityUpdate(actorObj);

    // POST-PHYSICS overrides — run after EntityPositionPhysics has finished
    // so we get the last word on velocity for the next frame.
    __try {
        if (savedFriendSlot != 0) {
            // Re-inject movement input after physics overwrites
            InjectMovementInput(actorObj, savedFriendSlot);
        }

        // Suppress Sora's movement at the entity level (zero velocity/accel).
        // This runs after Sora's own physics pass, preventing him from moving
        // while leaving the input system untouched for camera control.
        if (g_soloTestMode && g_soraActor != 0 &&
            reinterpret_cast<uintptr_t>(actorObj) == g_soraActor) {
            SuppressSoraMovement();
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("EXCEPTION in post-physics override");
    }

    // Reset slot indicator
    g_currentFriendSlot = 0;
}

// ============================================================================
// Public API
// ============================================================================

bool Initialize(uintptr_t exeBase) {
    if (g_initialized) return true;

    g_exeBase = exeBase;

    // Open log file in the game directory
    g_logFile = fopen("kh2coop_inject.log", "w");
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
    Log("  SetMotion: 0x%llX  SetMotionSimple: 0x%llX",
        static_cast<unsigned long long>(exeBase + RVA_SET_MOTION),
        static_cast<unsigned long long>(exeBase + RVA_SET_MOTION_SIMPLE));

    // --- Install PerEntityUpdate hook ---
    mhStatus = MH_CreateHook(
        reinterpret_cast<void*>(perEntityUpdateAddr),
        reinterpret_cast<void*>(&HookedPerEntityUpdate),
        reinterpret_cast<void**>(&g_origPerEntityUpdate));

    if (mhStatus != MH_OK) {
        Log("ERROR: MH_CreateHook(PerEntityUpdate) failed: %d (%s)",
            mhStatus, MH_StatusToString(mhStatus));
        MH_Uninitialize();
        return false;
    }

    mhStatus = MH_EnableHook(reinterpret_cast<void*>(perEntityUpdateAddr));
    if (mhStatus != MH_OK) {
        Log("ERROR: MH_EnableHook(PerEntityUpdate) failed: %d (%s)",
            mhStatus, MH_StatusToString(mhStatus));
        MH_Uninitialize();
        return false;
    }

    Log("  PerEntityUpdate hook installed");
    Log("Initialization complete — waiting for friend entities...");
    Log("  Press F5 to toggle solo test mode (control Friend1 with KH2 controller 0)");

    // Try to open the network input mailbox (runtime may not be running yet).
    // If not available now, we'll retry periodically in TryReadMailbox().
    if (g_mailboxReader.Open()) {
        g_mailboxAvailable = true;
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
    if (!g_initialized) return;

    Log("Shutting down...");

    // Restore camera before unhooking (game needs its original pointer back)
    RestoreCameraToSora();

    // Close network input mailbox
    if (g_mailboxAvailable) {
        g_mailboxReader.Close();
        g_mailboxAvailable = false;
        Log("  Network input mailbox closed");
    }

    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();

    g_initialized = false;
    g_friendAIHooked = false;
    g_friendPrePhysicsHooked = false;
    g_followSteeringHooked = false;
    g_origPerEntityUpdate = nullptr;
    g_origFriendAI = nullptr;
    g_origFriendPrePhysics = nullptr;
    g_origFollowSteering = nullptr;
    g_resolveEntityType = nullptr;
    g_hookedAITarget = nullptr;
    g_hookedPrePhysicsTarget = nullptr;
    g_hookedFollowSteeringTarget = nullptr;
    g_friend1Actor = 0;
    g_friend2Actor = 0;
    g_soraActor = 0;

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
