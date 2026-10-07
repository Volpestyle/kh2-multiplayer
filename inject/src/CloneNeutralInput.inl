// CloneNeutralInput.inl - VUH-1489 follow-up, private candidate v3, default off.
// Included by EntityHook.cpp after the clone registry (g_clonesNow,
// IsPlayerClassActor). Enabled only by KH2COOP_CLONE_NEUTRAL_INPUT=1, and only
// once Install() has verified the image and hooked 0x3A89A0.
//
// Two native input paths reach a player-class actor [GHIDRA]:
//  1. Its own pad pointer actor+0xDB8, set by the constructor 0x3A7A40 to
//     GetPadEntry(0) (= exe+0xBF31A0). Buttons and the bit-3 / kind 0x18/0x35
//     movers read **(+0xDB8) and *(+0xDB8)+0x30..0x3C.
//  2. The single shared FIELD_COMMAND (exe+0x2A10620, every player's
//     +0xDC0/+0xDD0). PerEntityUpdate 0x3BFD30 calls type slot +0x18 (FC tick
//     0x3A8980 -> 0x3B2340: copies [FC+0xAE8]+0x30 = entry 0's stick into
//     FC+0xB50 and refills the command record FC+0x08) and then +0x20 (movement
//     update 0x3A89A0: mover via +0x128 reads *(+0xDD0)+0xB50 for ordinary
//     actions, then executes the FC command record on THIS actor through
//     0x3D6FC0). So a clone inside its own update moves with the player's stick
//     and executes the player's FC commands (live 233109).
//
// Fix, per actor and on the owner thread only:
//  A. Gate (pre-call in HookedPerEntityUpdate): a non-canonical P_EX100 clone's
//     own +0xDB8 points at a neutral entry (VirtualAlloc'd, never freed).
//  B. HookedMovementUpdate (0x3A89A0 detour): for an actor whose +0xDB8 is the
//     neutral entry and whose +0xDD0 is FIELD_COMMAND, run the original with
//     FC's command record neutralised exactly as 0x3D7F00 resets it (id = 0,
//     flags byte &= 0xE0, target sub-struct +0x10 = 0x3BDA50's {0,0,0,-1}) and
//     FC's stick copy zeroed, then restore them in __finally. The window
//     restores the values FC held when it opened, so FC ends up exactly as if
//     the clone's movement update had never run. (Usually the clone's own FC
//     tick at +0x18 has just written them; in 0x3BFD30's 0x3BA530 branch, or
//     with FC unbound, there was no tick and they are whatever the previous
//     actor left. Restoring is correct either way.) FC's only stick writer runs
//     inside player updates on this same thread. A non-gated movement update
//     reached while a window is open (not seen statically) gets the real
//     values for its duration and is counted as nestedPassThrough.
//  The global entry 0 is never written; the canonical player's +0xDB8 is only
//  ever written to undo this module's own value.
//
// Page layout: +0x000 neutral entry (0x68 bytes, the only part a pad reader
// sees), +0x100 Stats (DLL-only counters; the fixture reads them read-only).

namespace cloneneutral {

static constexpr uintptr_t ACTOR_PAD_ENTRY = 0xDB8;       // [GHIDRA] 0x3A7A80 MOV [RSI+0xDB8],RBX
static constexpr uintptr_t ACTOR_FIELD_COMMAND = 0xDD0;   // [GHIDRA] 0x3A7A9E
static constexpr uintptr_t RVA_NATIVE_PLAYER = 0x2A105D0;  // [GHIDRA] set by 0x3A7A40
static constexpr uintptr_t RVA_FIELD_COMMAND = 0x2A10620;  // [GHIDRA] YS::FIELD_COMMAND static (0x24560)
static constexpr uintptr_t RVA_MOVEMENT_UPDATE = 0x3A89A0; // [GHIDRA] type slot +0x20 body (0x404A40, 0x3A7090, ...)
static constexpr uintptr_t FC_COMMAND_ID = 0x08;           // u16, reset to 0 by 0x3D7F00(FC+8)
static constexpr uintptr_t FC_COMMAND_FLAGS = 0x0C;        // u8, & 0xE0 by 0x3D7F00
static constexpr uintptr_t FC_COMMAND_TARGET = 0x10;       // 16 bytes, 0x3BDA50: {handle(0)=0, 0, handle(0)=0, -1}
static constexpr uintptr_t FC_STICK = 0xB50;               // 4 floats, written by 0x3B2340
static constexpr size_t FC_STICK_BYTES = 0x10;
static constexpr size_t PAD_ENTRY_BYTES = 0x68;            // offsets::input::PROCESSED_STRIDE
static constexpr size_t PAD_STATE_BYTES = 0x40;            // buttons, edges, repeat, both sticks
static constexpr size_t STATS_OFFSET = 0x100;
static constexpr std::uint32_t STATS_MAGIC = 0x33494E43;   // "CNI3"
static constexpr std::uint32_t SORA_OBJECT_ID = 84;        // P_EX100
static constexpr std::uint32_t IMAGE_TIMESTAMP = 0x669E384A, IMAGE_SIZE = 0x2C2B000;  // exe 9002b2de
static constexpr unsigned LOG_BUDGET = 32;
// 0x3A89A0..+0x30 of exe 9002b2de: push rdi; sub rsp,50h; test [rcx+9B8h] bit 2; the
// +0x128 mover call; the +0xDD0 load. MinHook relocates only the first instructions.
static constexpr std::uint8_t MOVEMENT_PROLOGUE[48] = {
    0x40, 0x57, 0x48, 0x83, 0xEC, 0x50, 0x8B, 0x81, 0xB8, 0x09, 0x00, 0x00, 0x48, 0x8B, 0xF9, 0xC1,
    0xE8, 0x02, 0xA8, 0x01, 0x0F, 0x85, 0x42, 0x01, 0x00, 0x00, 0x8B, 0x09, 0x48, 0x89, 0x74, 0x24,
    0x68, 0xE8, 0xAA, 0x48, 0x10, 0x00, 0x48, 0x8B, 0xD7, 0x48, 0x8B, 0xC8, 0x4C, 0x8B, 0x00, 0x41};

struct Stats {
    std::uint32_t magic;
    std::uint32_t neutralized;       // clone pointer moved to the neutral entry
    std::uint32_t restored;          // a now-canonical actor got entry 0 back
    std::uint32_t refusedPointer;    // P_EX100 clone whose pointer was not entry 0
    std::uint32_t transitionSkips;   // eligible clone left alone while a transition is pending
    std::uint32_t hookInstalled;     // 1 once 0x3A89A0 is verified and hooked
    std::uint64_t rezero;            // updates of an already-neutral actor (proves Gate still sees it)
    std::uint64_t movementGated;     // 0x3A89A0 calls run with FC's command and stick neutralised
    std::uint64_t commandSuppressed; // ... of which FC held a non-zero command id
    std::uint64_t nestedPassThrough; // non-gated 0x3A89A0 calls while a gated window was open
};
// Owner-thread plain increments. The fixture reads them by RPM from another process;
// each counter is one aligned field, so a read is never torn, only possibly one update stale.
static_assert(sizeof(Stats) == 0x38 && STATS_OFFSET >= PAD_ENTRY_BYTES, "stats layout");

using PFN_MovementUpdate = void(__fastcall*)(void* actor);
static PFN_MovementUpdate g_origMovementUpdate = nullptr;
static std::uint8_t* g_neutral = nullptr;
static Stats* g_stats = nullptr;
static std::atomic<int> g_state {0};   // 0 off/unconfigured, 1 on (page + hook ready)
static unsigned g_logBudget = LOG_BUDGET;

static bool Enabled() { return g_state.load(std::memory_order_acquire) == 1; }

static bool SoraDescriptor(uintptr_t actor) {
    const auto obj = *reinterpret_cast<const uintptr_t*>(actor + offsets::actor::OBJENTRY_PTR);
    if (obj <= g_exeBase || obj >= g_exeBase + 0x3000000) return false;
    return *reinterpret_cast<const std::uint32_t*>(obj + offsets::objentry::OBJECT_ID) == SORA_OBJECT_ID &&
        *reinterpret_cast<const std::uint8_t*>(obj + offsets::objentry::TYPE_FLAGS) == 0 &&
        std::memcmp(reinterpret_cast<const char*>(obj + offsets::objentry::NAME), "P_EX100", 8) == 0;
}

static bool CanonicalActor(uintptr_t actor) {
    return actor == *reinterpret_cast<const uintptr_t*>(g_exeBase + offsets::active_entity_list::HEAD) ||
        actor == *reinterpret_cast<const uintptr_t*>(g_exeBase + RVA_NATIVE_PLAYER);
}

static void Note(const char* what, uintptr_t actor, uintptr_t from, uintptr_t to, bool registered) {
    if (g_logBudget == 0) return;
    --g_logBudget;
    Log("[cloneneutral] %s frame=%u actor=%p from=%p to=%p registered=%u neutralized=%u restored=%u "
        "refusedPointer=%u transitionSkips=%u rezero=%llu movementGated=%llu commandSuppressed=%llu",
        what, g_frameCounter, reinterpret_cast<void*>(actor), reinterpret_cast<void*>(from),
        reinterpret_cast<void*>(to), registered ? 1u : 0u, g_stats->neutralized, g_stats->restored,
        g_stats->refusedPointer, g_stats->transitionSkips, static_cast<unsigned long long>(g_stats->rezero),
        static_cast<unsigned long long>(g_stats->movementGated),
        static_cast<unsigned long long>(g_stats->commandSuppressed));
}

// True only for the exact exe this candidate was reverse-engineered on.
static bool VerifyMovementImage(uintptr_t base) {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x1000) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + static_cast<uintptr_t>(dos->e_lfanew));
    return nt->Signature == IMAGE_NT_SIGNATURE && nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
        nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
        nt->FileHeader.TimeDateStamp == IMAGE_TIMESTAMP && nt->OptionalHeader.SizeOfImage == IMAGE_SIZE &&
        std::memcmp(reinterpret_cast<const void*>(base + RVA_MOVEMENT_UPDATE), MOVEMENT_PROLOGUE,
                    sizeof(MOVEMENT_PROLOGUE)) == 0;
}

// Called from HookedPerEntityUpdate's pre-call block (inside its __try), on
// the owner thread, before the actor's own update reads its pad.
static void Gate(uintptr_t actor) {
    if (!actor || !Enabled()) return;
    // Player-class first: no other class's +0xDB8 is ever read or written.
    if (!IsPlayerClassActor(actor)) return;
    auto* slot = reinterpret_cast<uintptr_t*>(actor + ACTOR_PAD_ENTRY);
    const uintptr_t entry0 = g_exeBase + offsets::input::PROCESSED_ENTRY0;
    const uintptr_t neutral = reinterpret_cast<uintptr_t>(g_neutral);
    if (CanonicalActor(actor)) {
        // The canonical player is never neutral. Only undo our own value.
        if (*slot == neutral) {
            *slot = entry0;
            ++g_stats->restored;
            Note("restore", actor, neutral, entry0, false);
        }
        return;
    }
    if (*slot == neutral) {
        // Sticky for the actor's lifetime (before bind, bound, after release,
        // transitions), including player-class forms/children that inherited
        // it. Reset anything a consumer wrote into the shared buffer.
        std::memset(g_neutral, 0, PAD_STATE_BYTES);
        ++g_stats->rezero;
        return;
    }
    // Same predicate as NoteActorForClones (player-class, not the head),
    // narrowed to P_EX100 and not the player pointer.
    if (!SoraDescriptor(actor)) return;
    // Like the registry, never select during a room/load transition: a new
    // canonical can briefly be neither head nor player pointer (review S3).
    if (warp::TransitionPending()) {
        ++g_stats->transitionSkips;
        return;
    }
    const bool registered = g_clonesNow[0] == actor || g_clonesNow[1] == actor;
    if (*slot != entry0) {
        ++g_stats->refusedPointer;
        Note("refuse-pointer", actor, *slot, 0, registered);
        return;
    }
    std::memset(g_neutral, 0, PAD_STATE_BYTES);
    // Tail (+0x40 context, +0x48 raw-slot reference) copied from the live
    // entry so a reader that follows it sees valid game data.
    std::memcpy(g_neutral + PAD_STATE_BYTES, reinterpret_cast<const void*>(entry0 + PAD_STATE_BYTES),
                PAD_ENTRY_BYTES - PAD_STATE_BYTES);
    *slot = neutral;
    ++g_stats->neutralized;
    Note("neutralize", actor, entry0, neutral, registered);
}

// The movement update is gated only for an actor Gate already made neutral.
static bool MovementGated(uintptr_t actor) {
    if (!actor || !Enabled() || !IsPlayerClassActor(actor)) return false;
    return *reinterpret_cast<const uintptr_t*>(actor + ACTOR_PAD_ENTRY) == reinterpret_cast<uintptr_t>(g_neutral) &&
        *reinterpret_cast<const uintptr_t*>(actor + ACTOR_FIELD_COMMAND) == g_exeBase + RVA_FIELD_COMMAND &&
        !CanonicalActor(actor);
}

// The FC bytes a gated window neutralises and restores (plain data, copied by value).
struct FcWindow {
    std::uint16_t id;
    std::uint8_t flags;
    std::uint8_t target[0x10];
    std::uint8_t stick[FC_STICK_BYTES];
};
static FcWindow* g_open = nullptr;   // innermost open gated window (owner thread only)

static std::uint8_t* FieldCommand() { return reinterpret_cast<std::uint8_t*>(g_exeBase + RVA_FIELD_COMMAND); }
static void SaveFc(FcWindow& w) {
    auto* fc = FieldCommand();
    std::memcpy(&w.id, fc + FC_COMMAND_ID, sizeof(w.id));
    w.flags = fc[FC_COMMAND_FLAGS];
    std::memcpy(w.target, fc + FC_COMMAND_TARGET, sizeof(w.target));
    std::memcpy(w.stick, fc + FC_STICK, sizeof(w.stick));
}
static void WriteFc(const FcWindow& w) {
    auto* fc = FieldCommand();
    std::memcpy(fc + FC_STICK, w.stick, sizeof(w.stick));
    std::memcpy(fc + FC_COMMAND_TARGET, w.target, sizeof(w.target));
    fc[FC_COMMAND_FLAGS] = w.flags;
    std::memcpy(fc + FC_COMMAND_ID, &w.id, sizeof(w.id));
}
// Exactly 0x3D7F00(FC+8): id = 0, 0x3BDA50 on the target sub-struct, flags &= 0xE0; plus a zero stick.
static void NeutraliseFc(std::uint8_t savedFlags) {
    FcWindow n {};
    n.flags = static_cast<std::uint8_t>(savedFlags & 0xE0);
    const std::uint32_t target[4] = {0, 0, 0, 0xFFFFFFFFu};
    std::memcpy(n.target, target, sizeof(n.target));
    WriteFc(n);
}

static void __fastcall HookedMovementUpdate(void* actorObj) {
    const auto actor = reinterpret_cast<uintptr_t>(actorObj);
    bool gated = false;
    __try {
        gated = MovementGated(actor);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        gated = false;
    }
    if (!gated) {
        FcWindow* const outer = g_open;
        if (!outer) {
            g_origMovementUpdate(actorObj);
            return;
        }
        // A non-gated actor inside a gated window: give it the real FC values,
        // keep whatever it writes as the window's restore values, re-neutralise.
        ++g_stats->nestedPassThrough;
        FcWindow inner;
        SaveFc(inner);
        WriteFc(*outer);
        g_open = nullptr;
        __try {
            g_origMovementUpdate(actorObj);
        } __finally {
            SaveFc(*outer);
            WriteFc(inner);
            g_open = outer;
        }
        return;
    }
    FcWindow saved;
    SaveFc(saved);
    ++g_stats->movementGated;
    if (saved.id != 0) ++g_stats->commandSuppressed;
    NeutraliseFc(saved.flags);
    FcWindow* const previous = g_open;
    g_open = &saved;
    __try {
        g_origMovementUpdate(actorObj);
    } __finally {
        WriteFc(saved);
        g_open = previous;
    }
}

// Called once from Initialize after MinHook is up. Any refusal leaves the
// whole module off (vanilla behaviour); nothing is half-enabled.
static void Install(uintptr_t exeBase) {
    char v[2] {};
    if (GetEnvironmentVariableA("KH2COOP_CLONE_NEUTRAL_INPUT", v, sizeof(v)) != 1 || v[0] != '1') return;
    const char* refused = nullptr;
    if (playerkit::BlocksNativeSoraPuppets()) refused = "player-kit";
    else if (!VerifyMovementImage(exeBase)) refused = "image";
    std::uint8_t* page = nullptr;
    if (!refused) {
        // Process-lifetime storage, intentionally never freed. Zero-filled.
        page = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (!page) refused = "alloc";
    }
    if (!refused) {
        g_neutral = page;
        g_stats = reinterpret_cast<Stats*>(page + STATS_OFFSET);
        g_stats->magic = STATS_MAGIC;
        auto* target = reinterpret_cast<void*>(exeBase + RVA_MOVEMENT_UPDATE);
        if (MH_CreateHook(target, reinterpret_cast<void*>(&HookedMovementUpdate),
                          reinterpret_cast<void**>(&g_origMovementUpdate)) != MH_OK) refused = "hook-create";
        else if (MH_EnableHook(target) != MH_OK) refused = "hook-enable";
    }
    if (refused) {
        Log("[cloneneutral] configured=0 refused=%s", refused);
        return;
    }
    g_stats->hookInstalled = 1;
    g_state.store(1, std::memory_order_release);
    Log("[cloneneutral] configured=1 neutral=%p stats=%p hook=%p", g_neutral, g_stats,
        reinterpret_cast<void*>(exeBase + RVA_MOVEMENT_UPDATE));
}

}  // namespace cloneneutral
