// PlayerKit — see PlayerKit.hpp (VUH-1513 step 2).
#include "PlayerKit.hpp"

#include <Windows.h>
#include <atomic>
#include <cstdlib>
#include <cstring>

#ifndef KH2COOP_PLAYERKIT_POLICY_ONLY
#include "MinHook.h"
#endif

namespace kh2coop::inject::playerkit {

// ---------------------------------------------------------------- policy
bool KitAllowed(std::uint16_t kit) { return kit == ROXAS; } // 0x323/0x5B: later, one fixture each

bool ParseKit(const char* text, std::uint16_t& kit) {
    kit = 0;
    if (!text || !*text) return true;
    char* end = nullptr;
    const unsigned long v = std::strtoul(text, &end, 0);
    if (!end || *end != '\0' || v > 0xFFFF) return false;
    kit = static_cast<std::uint16_t>(v);
    return kit == 0 || KitAllowed(kit);
}

bool WorldQualified(std::uint8_t world) { return world == 4; } // BB (5) after its own fixture

bool KitEnvBlocksPuppets(const char* text) { return text && *text && std::strcmp(text, "0") != 0; }

Reason Decide(std::uint16_t kit, const LoadContext& c) {
    if (!KitAllowed(kit)) return Reason::Disabled;
    if (!WorldQualified(c.world)) return Reason::WorldNotQualified;
    if (c.evtProgram != 0) return Reason::EventRoom;
    if (c.eventContext != 0 || c.cutsceneState != 0) return Reason::EventActive;
    if (c.resolved0 == kit) return Reason::AlreadyKit;
    if (c.resolved0 != SORA) return Reason::NativeNotSora; // costumes, forms, TT Roxas, etc.
    return Reason::Applied;
}

const char* ReasonName(Reason r) {
    switch (r) {
    case Reason::Applied: return "applied";
    case Reason::Disabled: return "disabled";
    case Reason::WorldNotQualified: return "world-not-qualified";
    case Reason::EventRoom: return "event-room";
    case Reason::EventActive: return "event-active";
    case Reason::NativeNotSora: return "native-not-sora";
    case Reason::AlreadyKit: return "already-kit";
    }
    return "?";
}

std::uint8_t RosterFromObjectId(std::uint32_t objectId) {
    switch (objectId) {
    case ROXAS: return 1;
    case ROXAS_DW: return 2;
    case MICKEY: return 3;
    default: return 0;
    }
}

Reason ApplyAfterResolve(std::uint16_t kit, const LoadContext& c, std::uint16_t* resolved,
                         std::uint16_t* original) {
    const Reason r = Decide(kit, c);
    if (r != Reason::Applied || !resolved) return r;
    if (resolved[0] != c.resolved0) return Reason::NativeNotSora; // changed under us: never guess
    if (original) *original = resolved[0];
    resolved[0] = kit;
    return r;
}

bool RemoteEnvRequested(const char* text) { return text && text[0] == '1' && text[1] == '\0'; }

RemoteReason DecideRemote(const RemoteContext& c, RemoteValues* set) {
    if (!WorldQualified(c.load.world)) return RemoteReason::WorldNotQualified;
    if (c.load.evtProgram != 0) return RemoteReason::EventRoom;
    if (c.load.eventContext != 0 || c.load.cutsceneState != 0) return RemoteReason::EventActive;
    // Exact remote-puppet layout only: player 0, Friend1 = member 3, Friend2 Goofy, no world ally.
    if (c.row[0] != 0 || c.row[1] != FRIEND1_SELECTOR || c.row[2] != 2 || c.row[3] != 0x12) return RemoteReason::RowNotRemoteLayout;
    if (c.load.resolved0 != SORA) return RemoteReason::NativeNotSora; // costumes, forms, TT Roxas: never guess
    if (set) {
        set->member0 = c.roster == 1 ? ROXAS : SORA; // clone / puppet target; only Roxas is qualified
        set->member3 = SORA;                         // the receiver's own player stays native Sora
    }
    return RemoteReason::Applied;
}

const char* RemoteReasonName(RemoteReason r) {
    switch (r) {
    case RemoteReason::Applied: return "applied";
    case RemoteReason::WorldNotQualified: return "world-not-qualified";
    case RemoteReason::EventRoom: return "event-room";
    case RemoteReason::EventActive: return "event-active";
    case RemoteReason::RowNotRemoteLayout: return "row-not-remote-layout";
    case RemoteReason::NativeNotSora: return "native-not-sora";
    case RemoteReason::ChangedUnderUs: return "changed-under-us";
    }
    return "?";
}

RemoteReason ApplyRemote(const RemoteContext& c, std::uint16_t* resolved, RemoteValues* original, RemoteValues* set) {
    RemoteValues v;
    const RemoteReason r = DecideRemote(c, &v);
    if (r != RemoteReason::Applied || !resolved) return r;
    if (resolved[PUPPET_TARGET_MEMBER] != c.load.resolved0 || resolved[OWN_PLAYER_MEMBER] != c.native3)
        return RemoteReason::ChangedUnderUs; // never guess
    if (original) { original->member0 = resolved[PUPPET_TARGET_MEMBER]; original->member3 = resolved[OWN_PLAYER_MEMBER]; }
    resolved[PUPPET_TARGET_MEMBER] = v.member0;
    resolved[OWN_PLAYER_MEMBER] = v.member3;
    if (set) *set = v;
    return r;
}

bool RestoreRemote(const RemoteValues& set, const RemoteValues& original, bool recorded, std::uint16_t* resolved) {
    if (!resolved || !recorded) return true;
    // Each member independently, only while it still holds our value (a later load re-resolves natively).
    if (resolved[PUPPET_TARGET_MEMBER] == set.member0) resolved[PUPPET_TARGET_MEMBER] = original.member0;
    if (resolved[OWN_PLAYER_MEMBER] == set.member3) resolved[OWN_PLAYER_MEMBER] = original.member3;
    return true;
}

bool RestoreResolved(std::uint16_t kit, std::uint16_t original, std::uint16_t* resolved) {
    if (!resolved || !KitAllowed(kit) || original != SORA) return true; // nothing of ours recorded
    if (resolved[0] != kit) return true; // a later load already resolved natively; never overwrite
    resolved[0] = original;
    return true;
}

#ifndef KH2COOP_PLAYERKIT_POLICY_ONLY
// ---------------------------------------------------------------- native
namespace {
// First 33 bytes of 3E2EB0 on 9002b2de. They include the RIP-relative loads of
// NOW.world (0x717008, at +19) and the resolved array (0x2A25300, at +26), so
// a match pins those two globals; the other RVAs (room, evt, event context,
// cutscene) rely on the exe identity this build is pinned to elsewhere.
constexpr std::uint8_t kResolveMembersBytes[] = {
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x41, 0x56, 0x41, 0x57,
    0x48, 0x83, 0xEC, 0x20, 0x0F, 0xB6, 0x35, 0x3E, 0x41, 0x33, 0x00, 0x48, 0x8D, 0x3D, 0x2F,
    0x24, 0x64, 0x02};
static_assert(sizeof(kResolveMembersBytes) == 33);

using ResolveFn = void(__fastcall*)();
ResolveFn g_original = nullptr;
std::uintptr_t g_base = 0;
LogFn g_log = nullptr;
std::atomic<bool> g_active {false};
std::uint16_t g_kit = 0;
std::uint16_t g_original0 = 0;  // exact replaced value of the last application (0 = none)
Stats g_stats {};
std::atomic<bool> g_blockPuppets {false};
std::atomic<std::uint32_t> g_cloneLogLoad {0xFFFFFFFFu};
std::atomic<std::uint32_t> g_refusedClones {0};
std::atomic<bool> g_remote {false};
std::atomic<std::uint8_t> g_remoteRoster {0};
RemoteValues g_remoteSet{}, g_remoteOriginal{};
bool g_remoteRecorded = false;
std::uint32_t g_remoteApplied = 0, g_remoteSkipped = 0;

void HookedResolveMembers() {
    g_original();
    if (!g_active.load(std::memory_order_acquire)) return;
    LoadContext c;
    Reason r = Reason::Disabled;
    bool ok = false;
    RemoteContext rc;
    RemoteReason rr = RemoteReason::WorldNotQualified;
    RemoteValues rv{};
    const bool remote = g_remote.load(std::memory_order_acquire);
    __try {
        auto* resolved = reinterpret_cast<std::uint16_t*>(g_base + RVA_RESOLVED_MEMBERS);
        c.world = *reinterpret_cast<const std::uint8_t*>(g_base + RVA_WORLD);
        c.room = *reinterpret_cast<const std::uint8_t*>(g_base + RVA_ROOM);
        c.evtProgram = *reinterpret_cast<const std::uint16_t*>(g_base + RVA_EVT_PROGRAM);
        c.eventContext = *reinterpret_cast<const std::uint64_t*>(g_base + RVA_EVENT_CONTEXT);
        c.cutsceneState = *reinterpret_cast<const std::int32_t*>(g_base + RVA_CUTSCENE_STATE);
        c.resolved0 = resolved[0];
        ++g_stats.loads;
        g_stats.lastWorld = c.world; g_stats.lastRoom = c.room;
        if (g_kit != 0) {
            std::uint16_t original = 0;
            r = ApplyAfterResolve(g_kit, c, resolved, &original);
            g_stats.lastReason = r;
            if (r == Reason::Applied) { ++g_stats.applied; g_original0 = original; }
            else { ++g_stats.skipped; if (r != Reason::AlreadyKit) g_original0 = 0; }
        }
        if (remote) {
            rc.load = c;
            const auto* row = reinterpret_cast<const std::uint8_t*>(g_base + RVA_SAVE + PARTY_ROWS_OFFSET + 4u * c.world);
            for (int i = 0; i < 4; ++i) rc.row[i] = row[i];
            rc.load.resolved0 = resolved[PUPPET_TARGET_MEMBER];
            rc.native3 = resolved[OWN_PLAYER_MEMBER];
            rc.roster = g_remoteRoster.load(std::memory_order_acquire);
            RemoteValues original{};
            rr = ApplyRemote(rc, resolved, &original, &rv);
            if (rr == RemoteReason::Applied) {
                ++g_remoteApplied; g_remoteSet = rv; g_remoteOriginal = original; g_remoteRecorded = true;
            } else { ++g_remoteSkipped; g_remoteRecorded = false; }
        }
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ++g_stats.faults;
    }
    // Logged outside the SEH scope so a CRT fault can't strand the FILE lock.
    if (ok && g_log && g_kit != 0)
        g_log("[playerkit] load world=%u room=%u evt=%u eventCtx=%d cutscene=%d native0=0x%X kit=0x%X result=%s",
              c.world, c.room, c.evtProgram, c.eventContext != 0, c.cutsceneState, c.resolved0, g_kit,
              ReasonName(r));
    if (ok && g_log && remote)
        g_log("[playerkit] remote load world=%u room=%u evt=%u row=%02X/%02X/%02X/%02X roster=%u native0=0x%X native3=0x%X set0=0x%X set3=0x%X result=%s",
              c.world, c.room, c.evtProgram, rc.row[0], rc.row[1], rc.row[2], rc.row[3], rc.roster, rc.load.resolved0, rc.native3,
              rr == RemoteReason::Applied ? rv.member0 : rc.load.resolved0, rr == RemoteReason::Applied ? rv.member3 : rc.native3,
              RemoteReasonName(rr));
}
} // namespace

bool Install(std::uintptr_t exeBase, LogFn log) {
    g_base = exeBase; g_log = log;
    char text[16] {};
    const DWORD n = GetEnvironmentVariableA("KH2COOP_PLAYER_KIT", text, sizeof(text));
    char remoteText[4] {};
    const DWORD rn = GetEnvironmentVariableA("KH2COOP_REMOTE_KIT_SLOT", remoteText, sizeof(remoteText));
    if (n == 0 && rn == 0) return true; // default OFF: no hook, no reads
    std::uint16_t kit = 0;
    if (n != 0) {
        g_stats.requested = true;
        if (n >= sizeof(text) || KitEnvBlocksPuppets(text)) {
            g_blockPuppets.store(true, std::memory_order_release);
            if (log) log("[playerkit] native-Sora clone puppets REFUSED while KH2COOP_PLAYER_KIT=%s is set (VUH-1519 owns per-puppet member slots)", text);
        }
        if (n >= sizeof(text) || !ParseKit(text, kit)) {
            if (log) log("[playerkit] REFUSED: KH2COOP_PLAYER_KIT=%s is not 0 or 0x5A (puppets stay blocked)", text);
            return false;
        }
        if (kit == 0 && log) log("[playerkit] kit 0 (Sora): not installed%s", BlocksNativeSoraPuppets() ? "; puppets stay blocked (non-literal 0)" : "");
    }
    bool remote = false;
    if (rn != 0) {
        if (rn >= sizeof(remoteText) || !RemoteEnvRequested(remoteText)) {
            if (log) log("[playerkit] REFUSED: KH2COOP_REMOTE_KIT_SLOT must be exactly 1");
        } else if (BlocksNativeSoraPuppets()) {
            if (log) log("[playerkit] REFUSED: remote kit member needs native puppets, but KH2COOP_PLAYER_KIT blocks them");
        } else {
            remote = true;
        }
    }
    if (kit == 0 && !remote) return true;
    const void* target = reinterpret_cast<const void*>(exeBase + RVA_RESOLVE_MEMBERS);
    if (std::memcmp(target, kResolveMembersBytes, sizeof(kResolveMembersBytes)) != 0) {
        if (log) log("[playerkit] REFUSED: 3E2EB0 bytes differ from 9002b2de; not hooked");
        return false;
    }
    MH_STATUS st = MH_CreateHook(const_cast<void*>(target), reinterpret_cast<void*>(&HookedResolveMembers),
                                 reinterpret_cast<void**>(&g_original));
    if (st == MH_OK) st = MH_EnableHook(const_cast<void*>(target));
    if (st != MH_OK) {
        if (log) log("[playerkit] REFUSED: hook failed %d (%s)", st, MH_StatusToString(st));
        return false;
    }
    g_kit = kit; g_stats.kit = kit; g_stats.installed = true;
    g_remote.store(remote, std::memory_order_release);
    g_active.store(true, std::memory_order_release);
    if (kit != 0 && log) log("[playerkit] installed kit=0x%X (roster %u); applies on the next area load in world 4, outside events",
                             kit, RosterFromObjectId(kit));
    if (remote && log) log("[playerkit] remote kit member installed: GoA row 00/03/02/12; member 0 (clone, puppet target) shows the streamed kit (roster 1 -> 0x5A, else 0x54), member %u (own player) stays Sora",
                           OWN_PLAYER_MEMBER);
    return true;
}

void Shutdown() {
    if (!g_stats.installed) return;
    g_active.store(false, std::memory_order_release);
    MH_DisableHook(reinterpret_cast<void*>(g_base + RVA_RESOLVE_MEMBERS));
    __try {
        auto* resolved = reinterpret_cast<std::uint16_t*>(g_base + RVA_RESOLVED_MEMBERS);
        const std::uint16_t before = resolved[0];
        RestoreResolved(g_kit, g_original0, resolved);
        if (g_log && g_kit != 0)
            g_log("[playerkit] shutdown: member0 0x%X -> 0x%X (applied=%u skipped=%u faults=%u); the live actor keeps its kit until the next load",
                  before, resolved[0], g_stats.applied, g_stats.skipped, g_stats.faults);
        if (g_remote.exchange(false)) {
            const std::uint16_t before0 = resolved[PUPPET_TARGET_MEMBER], before3 = resolved[OWN_PLAYER_MEMBER];
            RestoreRemote(g_remoteSet, g_remoteOriginal, g_remoteRecorded, resolved);
            if (g_log)
                g_log("[playerkit] shutdown: member0 0x%X -> 0x%X, member3 0x%X -> 0x%X (remote applied=%u skipped=%u); live actors keep their kit until the next load",
                      before0, resolved[PUPPET_TARGET_MEMBER], before3, resolved[OWN_PLAYER_MEMBER], g_remoteApplied, g_remoteSkipped);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ++g_stats.faults;
    }
    g_stats.installed = false;
}

Stats GetStats() { return g_stats; }

bool BlocksNativeSoraPuppets() { return g_blockPuppets.load(std::memory_order_acquire); }
bool KitRequested() { return g_blockPuppets.load(std::memory_order_acquire); }
bool RemoteKitMemberActive() { return g_remote.load(std::memory_order_acquire); }
void NoteRemoteRoster(int index, std::uint8_t roster) {
    if (index != 0 || roster > 3 || !g_remote.load(std::memory_order_acquire)) return;
    const std::uint8_t before = g_remoteRoster.exchange(roster, std::memory_order_acq_rel);
    // One line per change (owner thread): the evidence that the resolver will see the new kit.
    if (before != roster && g_log) g_log("[playerkit] remote roster puppet 0: %u -> %u", before, roster);
}

void NoteRefusedClone(std::uintptr_t actor) {
    const std::uint32_t total = g_refusedClones.fetch_add(1, std::memory_order_relaxed) + 1;
    const std::uint32_t load = g_stats.loads;
    if (g_cloneLogLoad.exchange(load, std::memory_order_relaxed) != load && g_log)
        g_log("[playerkit] refused native-Sora clone puppet actor=%llX roster=%u (kit set; total=%u)",
              static_cast<unsigned long long>(actor), RosterForActor(actor), total);
}

std::uint8_t RosterForActor(std::uintptr_t actor) {
    if (!actor) return 0;
    __try {
        const auto obj = *reinterpret_cast<const std::uintptr_t*>(actor + 0x918);
        if (!obj) return 0;
        return RosterFromObjectId(*reinterpret_cast<const std::uint32_t*>(obj));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}
#endif

} // namespace kh2coop::inject::playerkit
