// ============================================================================
// EntityHook — PerEntityUpdate hook + friend AI replacement
//
// This is the core of Strategy B (AI replacement hook). It:
//   1. Hooks PerEntityUpdate (exe+0x3BFD30) via MinHook detour
//   2. Identifies friend entities by comparing actor pointers against
//      the known friend actor pointers from Slot1+0x220/+0x228
//   3. Discovers the friend AI vtable+0x10 function at runtime
//   4. Hooks the friend AI function to suppress AI and inject player input
//   5. Reads XInput gamepads for Friend1 (gamepad 1) and Friend2 (gamepad 2)
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

// XInput gamepad state (read once per frame)
struct GamepadState {
    bool          connected = false;
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

// Diagnostics
static uint32_t g_frameCounter  = 0;
static bool     g_initialized   = false;
static FILE*    g_logFile        = nullptr;
static uint32_t g_lastMovementLogFrame = 0;
static int      g_lastMovementStick = -1;

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

struct MovementStick {
    float x = 0.0f;
    float y = 0.0f;
    bool useRightStick = false;
};

static MovementStick SelectMovementStick(const GamepadState& pad) {
    MovementStick stick {};

    const float leftMagnitude = StickMagnitude(pad.leftX, pad.leftY);
    const float rightMagnitude = StickMagnitude(pad.rightX, pad.rightY);

    stick.x = pad.leftX;
    stick.y = pad.leftY;

    // Prefer the left stick for movement, but fall back to the right stick if
    // the device path only exposes motion there on this machine.
    if (leftMagnitude < STICK_DEADZONE && rightMagnitude > STICK_DEADZONE) {
        stick.x = pad.rightX;
        stick.y = pad.rightY;
        stick.useRightStick = true;
    }

    return stick;
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

static void ReadGamepads() {
    using namespace offsets;

    g_gamepad[0] = {};
    g_gamepad[1] = {};
    g_inputControllerCount = 0;
    g_activeInputSlot = -1;

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

// Suppress Sora's input by clearing the transient state in processed entry 0.
// Preserve the metadata tail (including the raw-input pointer at +0x48), or
// the next button-mapper pass will dereference a null pointer and crash.
static void SuppressSoraInput() {
    using namespace offsets;
    auto* entry = reinterpret_cast<uint8_t*>(g_exeBase + input::PROCESSED_ENTRY0);
    static constexpr std::size_t kTransientStateBytes = 0x40;
    memset(entry, 0, kTransientStateBytes);
}

// Check F5 key for solo test mode toggle (edge-triggered)
static void CheckTestModeHotkey() {
    bool f5Down = (GetAsyncKeyState(VK_F5) & 0x8000) != 0;
    if (f5Down && !g_f5WasDown) {
        g_soloTestMode = !g_soloTestMode;
        Log("Solo test mode %s (F5) — gamepad 0 → Friend1, Sora input %s",
            g_soloTestMode ? "ON" : "OFF",
            g_soloTestMode ? "suppressed" : "restored");
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

    // Prefer left-stick movement, but keep a right-stick fallback so solo mode
    // remains testable on controller paths that surface the wrong raw axes.
    float moveX = 0.0f;
    float moveY = 0.0f;
    bool usingRightStick = false;
    if (connected) {
        MovementStick movementStick = SelectMovementStick(pad);
        moveX = movementStick.x;
        moveY = movementStick.y;
        usingRightStick = movementStick.useRightStick;
    }

    const float magnitude = ApplyRadialDeadzone(&moveX, &moveY);

    // Convert stick to world-space velocity
    // KH2 world coordinates: X = right, Y = up (negative), Z = forward
    // Gamepad stick: X = right, Y = forward
    float speed = (magnitude > 0.7f) ? RUN_SPEED : WALK_SPEED;
    float velX = moveX * speed;
    float velY = 0.0f;
    float velZ = moveY * speed;

    // Write velocity
    *reinterpret_cast<float*>(actor + ACTOR_VELOCITY_X) = velX;
    *reinterpret_cast<float*>(actor + ACTOR_VELOCITY_Y) = velY;
    *reinterpret_cast<float*>(actor + ACTOR_VELOCITY_Z) = velZ;

    // Write acceleration (same values — physics integrates from these)
    *reinterpret_cast<float*>(actor + ACTOR_ACCEL_X) = velX;
    *reinterpret_cast<float*>(actor + ACTOR_ACCEL_Y) = velY;
    *reinterpret_cast<float*>(actor + ACTOR_ACCEL_Z) = velZ;

    if (g_soloTestMode && friendSlot == 1 && connected) {
        const int stickId = usingRightStick ? 1 : 0;
        const bool stickChanged = stickId != g_lastMovementStick;
        const bool shouldLogActiveMovement =
            magnitude > 0.01f && (g_frameCounter - g_lastMovementLogFrame) >= 30;
        if (stickChanged || shouldLogActiveMovement) {
            Log("[move %u] stick=%s rawL=(%.2f,%.2f) rawR=(%.2f,%.2f) move=(%.2f,%.2f) vel=(%.2f,%.2f)",
                g_frameCounter,
                usingRightStick ? "right-fallback" : "left",
                pad.leftX, pad.leftY,
                pad.rightX, pad.rightY,
                moveX, moveY,
                velX, velZ);
            g_lastMovementLogFrame = g_frameCounter;
            g_lastMovementStick = stickId;
        }
    }

    // Update facing direction from the final movement vector so facing and
    // translation stay in the same space/sign convention.
    if (magnitude > 0.01f) {
        using namespace offsets;
        uintptr_t entityBase = reinterpret_cast<uintptr_t>(actor) +
                               actor::ENTITY_TRANSFORM;

        float angle = std::atan2(velX, velZ);
        *reinterpret_cast<float*>(entityBase + entity::ROT_Y)      = angle;
        *reinterpret_cast<float*>(entityBase + entity::COS_FACING)  = std::cos(angle);
        *reinterpret_cast<float*>(entityBase + entity::SIN_FACING)  = std::sin(angle);
    }
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
    if (g_currentFriendSlot != 0) {
        // Controlled friend: suppress AI and drive the movement fields
        // that EntityPositionPhysics consumes later in the same update.
        InjectMovementInput(actorObj, g_currentFriendSlot);
        return;
    }

    // Not a controlled friend — call original AI
    if (g_origFriendAI) {
        g_origFriendAI(typeHandler, actorObj);
    }
}

static void __fastcall HookedFriendPrePhysics(void* typeHandler, void* actorObj) {
    if (g_currentFriendSlot != 0) {
        // Skip the vanilla pre-physics friend steering update and re-apply our
        // movement just before EntityPositionPhysics consumes it.
        InjectMovementInput(actorObj, g_currentFriendSlot);
        return;
    }

    if (g_origFriendPrePhysics) {
        g_origFriendPrePhysics(typeHandler, actorObj);
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
        // Check hotkey (handles standalone mode where OnFrame isn't called)
        CheckTestModeHotkey();

        // Refresh friend pointers (direct memory dereference, negligible)
        RefreshFriendPointers();

        // Identify friend entities
        uintptr_t addr = reinterpret_cast<uintptr_t>(actorObj);

        if (g_friend1Actor != 0 && addr == g_friend1Actor) {
            g_currentFriendSlot = 1;
        } else if (g_friend2Actor != 0 && addr == g_friend2Actor) {
            g_currentFriendSlot = 2;
        } else {
            g_currentFriendSlot = 0;
        }

        // Log friend detection and install the AI hook once per session.
        // Keep this path minimal and safe: actorObj is not a C++ vtable root,
        // and ResolveEntityType expects a type id, not the actor handle.
        if (g_currentFriendSlot != 0 &&
            (!g_friendAIHooked || !g_friendPrePhysicsHooked)) {
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

        // In solo test mode, suppress Sora's input continuously
        if (g_soloTestMode) {
            SuppressSoraInput();
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("EXCEPTION in HookedPerEntityUpdate pre-call, falling through");
        g_currentFriendSlot = 0;
    }

    // Always call original — even if our logic crashed, the game must continue.
    g_origPerEntityUpdate(actorObj);

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
    Log("=== kh2coop_inject v0.1 ===");
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

    // Validate: address should be in .text section
    auto textInfo = FindTextSection(exeBase);
    if (textInfo) {
        if (perEntityUpdateAddr < textInfo->start ||
            perEntityUpdateAddr >= textInfo->start + textInfo->size) {
            Log("ERROR: PerEntityUpdate 0x%llX is outside .text [0x%llX..0x%llX]",
                static_cast<unsigned long long>(perEntityUpdateAddr),
                static_cast<unsigned long long>(textInfo->start),
                static_cast<unsigned long long>(textInfo->start + textInfo->size));
            MH_Uninitialize();
            return false;
        }
    }

    // --- Resolve ResolveEntityType ---
    g_resolveEntityType = reinterpret_cast<PFN_ResolveEntityType>(
        exeBase + RVA_RESOLVE_ENTITY_TYPE);
    Log("  ResolveEntityType: 0x%llX",
        static_cast<unsigned long long>(exeBase + RVA_RESOLVE_ENTITY_TYPE));

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
    Log("  Input source: KH2 raw input buffer (XInput/Steam Input aware)");
    Log("  Press F5 to toggle solo test mode (control Friend1 with KH2 controller 0)");

    g_initialized = true;
    return true;
}

void Shutdown() {
    if (!g_initialized) return;

    Log("Shutting down...");

    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();

    g_initialized = false;
    g_friendAIHooked = false;
    g_friendPrePhysicsHooked = false;
    g_origPerEntityUpdate = nullptr;
    g_origFriendAI = nullptr;
    g_origFriendPrePhysics = nullptr;
    g_resolveEntityType = nullptr;
    g_hookedAITarget = nullptr;
    g_hookedPrePhysicsTarget = nullptr;
    g_friend1Actor = 0;
    g_friend2Actor = 0;

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

    // In solo test mode, suppress Sora's input every frame
    if (g_soloTestMode) {
        SuppressSoraInput();
    }

    // Periodic status log (~5 seconds at 60fps)
    if (g_frameCounter % 300 == 0) {
        RefreshFriendPointers();
        Log("[frame %u] f1=%p f2=%p aiHook=%d pad1=%d pad2=%d solo=%d pads=%d active=%d",
            g_frameCounter,
            reinterpret_cast<void*>(g_friend1Actor),
            reinterpret_cast<void*>(g_friend2Actor),
            g_friendAIHooked ? 1 : 0,
            g_gamepad[0].connected ? 1 : 0,
            g_gamepad[1].connected ? 1 : 0,
            g_soloTestMode ? 1 : 0,
            g_inputControllerCount,
            g_activeInputSlot);
    }
}

} // namespace inject
} // namespace kh2coop
