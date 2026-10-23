// AllyHit — see AllyHit.hpp / AllyHitPolicy.hpp.
#include "AllyHit.hpp"

#include <Windows.h>
#include <atomic>
#include <cstring>

#ifndef KH2COOP_ALLYHIT_TESTING
#include "MinHook.h"
#endif

namespace kh2coop::inject::allyhit {
namespace {
using CanHitOriginal = std::uint64_t(__fastcall*)(std::uintptr_t attack, std::uintptr_t victim);
CanHitOriginal g_original = nullptr;
std::uintptr_t g_base = 0;
LogFn g_log = nullptr;
ResolveFn g_resolve = nullptr;
FrameFn g_frame = nullptr;
DrivenFn g_driven = nullptr;
std::atomic<DownedFn> g_downed {nullptr};
std::atomic<std::uint64_t> g_downedRefusals {0};
std::atomic<std::uint8_t> g_mode{static_cast<std::uint8_t>(Mode::Off)};
std::atomic<bool> g_friendlyFire{true};
std::atomic<std::uint64_t> g_calls{}, g_pairs{}, g_allowed{}, g_refused{}, g_kindKept{}, g_faults{}, g_viaSource{};
std::atomic<std::uint64_t> g_puppetRefused{};
std::atomic<unsigned> g_traced{}, g_tracedZero{};
std::uint64_t g_lastLogged = ~0ull;
std::uint32_t g_lastTick = 0;
// Each (attack, victim) pair is traced once: a refused pair is re-asked every frame while the swing lasts.
struct Seen { std::uintptr_t attack, victim; };
Seen g_seen[64] {};
unsigned g_seenNext = 0;

struct ActorFacts { Side side = Side::Other; std::uint32_t team = 0xFFFFFFFF; char name[16] = "?"; };

// SEH leaf, POD only. A player-class actor = objentry type 0 inside the image.
bool ReadActor(std::uintptr_t actor, std::uintptr_t player, ActorFacts& out) {
    __try {
        if (actor < 0x10000) return true;
        const auto obj = *reinterpret_cast<const std::uintptr_t*>(actor + ACTOR_OBJENTRY);
        out.team = *reinterpret_cast<const std::uint32_t*>(actor + ACTOR_TEAM);
        if (obj <= g_base || obj >= g_base + 0x3000000) return true;
        std::memcpy(out.name, reinterpret_cast<const void*>(obj + OBJENTRY_NAME), sizeof(out.name) - 1);
        out.name[sizeof(out.name) - 1] = '\0';
        if (*reinterpret_cast<const std::uint8_t*>(obj + OBJENTRY_TYPE) == 0)
            out.side = actor == player ? Side::LocalPlayer : Side::RemotePlayer;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
struct AttackFacts { std::uintptr_t owner = 0, source = 0; std::uint8_t team = 0xFF, mask = 0, kind = 0xFF; std::uint16_t atkp = 0xFFFF; };
bool ReadAttack(std::uintptr_t attack, AttackFacts& out) {
    __try {
        const auto handle = *reinterpret_cast<const std::uint32_t*>(attack + ATTACK_OWNER);
        const auto sourceHandle = *reinterpret_cast<const std::uint32_t*>(attack + ATTACK_SOURCE);
        out.team = *reinterpret_cast<const std::uint8_t*>(attack + ATTACK_TEAM);
        out.mask = *reinterpret_cast<const std::uint8_t*>(attack + ATTACK_MASK);
        const auto atkp = *reinterpret_cast<const std::uintptr_t*>(attack + ATTACK_ATKP);
        if (atkp > 0x10000) {
            out.atkp = *reinterpret_cast<const std::uint16_t*>(atkp + ATKP_ID);
            out.kind = *reinterpret_cast<const std::uint8_t*>(atkp + ATKP_KIND);
        }
        out.owner = handle && g_resolve ? g_resolve(handle) : 0;
        out.source = sourceHandle && g_resolve ? g_resolve(sourceHandle) : 0;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
std::uintptr_t Player() {
    __try { return *reinterpret_cast<const std::uintptr_t*>(g_base + RVA_NATIVE_PLAYER); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
bool FirstSight(std::uintptr_t attack, std::uintptr_t victim) {
    for (const auto& s : g_seen) if (s.attack == attack && s.victim == victim) return false;
    g_seen[g_seenNext++ % 64] = {attack, victim};
    return true;
}
void TraceRow(const char* mode, const ActorFacts& o, std::uintptr_t attacker, const ActorFacts& v, std::uintptr_t victim,
              const AttackFacts& a, bool allows, Verdict verdict, bool viaSource) {
    g_log("[allyhit] f=%u %s attacker=%s@%llX(%s team=%u) -> victim=%s@%llX(%s team=%u) atkTeam=%u mask=0x%02X kind=%u atkp=%u native=%u verdict=%s via=%s",
          g_frame ? g_frame() : 0u, mode,
          o.name, static_cast<unsigned long long>(attacker), SideName(o.side), o.team,
          v.name, static_cast<unsigned long long>(victim), SideName(v.side), v.team,
          a.team, a.mask, a.kind, a.atkp, allows ? 1u : 0u, verdict == Verdict::Refuse ? "refuse" : "native",
          viaSource ? "source" : "owner");
}
// VUH-2617 trace (KH2COOP_THORN_HIT_TRACE=1, default off): the first check of each enemy attack against a
// player-class victim, with the native answer. Reads only; never changes the answer. Its own seen table
// and budget, so it neither spends nor hides the ally rows.
std::atomic<bool> g_enemyTrace{false};
std::atomic<unsigned> g_enemyTraced{};
constexpr unsigned ENEMY_TRACE_BUDGET = 400;
Seen g_enemySeen[64] {};
unsigned g_enemySeenNext = 0;
void TraceEnemyCheck(std::uintptr_t attack, std::uintptr_t victim, std::uint64_t native) {
    if (g_enemyTraced.load(std::memory_order_relaxed) >= ENEMY_TRACE_BUDGET) return;
    const std::uintptr_t player = Player();
    ActorFacts v {};
    if (!ReadActor(victim, player, v) || !PlayerSide(v.side)) return;
    AttackFacts a {};
    if (!ReadAttack(attack, a)) return;
    ActorFacts o {};
    if (!ReadActor(a.owner, player, o) || PlayerSide(o.side)) return;
    for (const auto& s : g_enemySeen) if (s.attack == attack && s.victim == victim) return;
    g_enemySeen[g_enemySeenNext++ % 64] = {attack, victim};
    g_enemyTraced.fetch_add(1, std::memory_order_relaxed);
    g_log("[thorn-trace] canhit f=%u attacker=%s@%llX atkp=%u kind=%u atkTeam=%u mask=0x%02X victim=%s@%llX(%s) native=%u",
          g_frame ? g_frame() : 0u, o.name, static_cast<unsigned long long>(a.owner), a.atkp, a.kind, a.team, a.mask,
          v.name, static_cast<unsigned long long>(victim), SideName(v.side), (native & 0xFF) ? 1u : 0u);
}
// VUH-2617 trace, same switch: checks of any attack against Thorn (B_EX100), with the attack's owner and source,
// Thorn's hit pattern at that check, the native answer and the answer this hook returns (they differ only where a rule below narrowed it). An attack is
// asked again every frame it lasts and its first check is often a native no, so a (attack, Thorn) pointer pair gets
// at most two rows while it stays in the 64-entry seen table: its first check, and its first native yes. Eviction can
// let an old pair log again and pointer reuse can hide a new attack, so the rows are positive observations, never a
// census. Thorn's own attacks refused against Thorn are not rows, except its projectile (atkp 1041). Reads only; its
// own seen table and a 600-row budget.
// The victim's hit pattern, (+0xD10 | +0xD14) & ~+0xD18 (docs/TWILIGHT_THORN.md): bit 9 nullifies a hit after this
// check, in the victim gate. SEH leaf; 0xFFFFFFFF is the unreadable sentinel, not a reading with every bit set.
std::uint32_t ReadPattern(std::uintptr_t actor) {
    __try {
        const auto* f = reinterpret_cast<const std::uint32_t*>(actor + 0xD10);
        return (f[0] | f[1]) & ~f[2];
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0xFFFFFFFFu; }
}
constexpr std::uint16_t THORN_ORB_ATKP = 1041;  // Thorn's 202 projectile (docs/THORN_BOTS.md)
std::atomic<unsigned> g_thornTraced{};
constexpr unsigned THORN_TRACE_BUDGET = 600;
struct ThornSeen { std::uintptr_t attack, victim; std::uint8_t logged; };  // logged: bit 0 any row, bit 1 a yes
ThornSeen g_thornSeen[64] {};
unsigned g_thornSeenNext = 0;
void TraceThornVictim(std::uintptr_t attack, std::uintptr_t victim, std::uint64_t native, std::uint64_t answer) {
    if (g_thornTraced.load(std::memory_order_relaxed) >= THORN_TRACE_BUDGET) return;
    const std::uintptr_t player = Player();
    ActorFacts v {};
    if (!ReadActor(victim, player, v) || std::strncmp(v.name, "B_EX100", 7) != 0) return;
    AttackFacts a {};
    if (!ReadAttack(attack, a)) return;
    const bool yes = (native & 0xFF) != 0;
    // Thorn's own attacks asked against Thorn and natively refused would spend the budget: not rows. Its orb sent back
    // by a Reversal names another source, so it stays traced, and so would any native yes. The exception is its
    // projectile, atkp 1041: a refused check of it against Thorn is a row, so a run tells an orb that never reaches
    // Thorn's check from one that reaches it still Thorn's and is refused.
    if (!yes && a.owner == victim && (!a.source || a.source == victim) && a.atkp != THORN_ORB_ATKP) return;
    ThornSeen* seen = nullptr;
    for (auto& s : g_thornSeen) if (s.attack == attack && s.victim == victim) seen = &s;
    if (seen && (!yes || (seen->logged & 2))) return;
    if (!seen) { seen = &g_thornSeen[g_thornSeenNext++ % 64]; *seen = {attack, victim, 0}; }
    seen->logged |= static_cast<std::uint8_t>(1 | (yes ? 2 : 0));
    g_thornTraced.fetch_add(1, std::memory_order_relaxed);
    ActorFacts o {}, s {};
    (void)ReadActor(a.owner, player, o);
    if (a.source) (void)ReadActor(a.source, player, s);
    g_log("[thorn-trace] victim f=%u attacker=%s@%llX(%s) source=%s@%llX(%s) atkp=%u kind=%u atkTeam=%u mask=0x%02X victim=%s@%llX pattern=0x%X native=%u answer=%u",
          g_frame ? g_frame() : 0u, o.name, static_cast<unsigned long long>(a.owner), SideName(o.side),
          a.source ? s.name : "-", static_cast<unsigned long long>(a.source), a.source ? SideName(s.side) : "-",
          a.atkp, a.kind, a.team, a.mask, v.name, static_cast<unsigned long long>(victim), ReadPattern(victim), yes ? 1u : 0u,
          (answer & 0xFF) ? 1u : 0u);
}
// Puppet mode (VUH-1808 default). Only ever narrows a native yes, and only when the attacker (the
// attack's owner, or its source, as native checks both) is a puppet actor driven right now.
std::uint64_t PuppetCanHit(std::uintptr_t attack, std::uintptr_t victim, std::uint64_t native) {
    if ((native & 0xFF) == 0 || !g_driven) return native;
    std::uintptr_t driven[2] {};
    const unsigned n = g_driven(driven);
    if (n == 0) return native; // no co-op puppet: no reads at all
    const auto isDriven = [&](std::uintptr_t actor) {
        for (unsigned i = 0; i < n && i < 2; ++i) if (actor && driven[i] == actor) return true;
        return false;
    };
    // Victim first: only a player-class or team-1 victim can be refused, so every other check (puppet on
    // enemy, enemy on anything) costs one victim read and never touches the attack or the handle lookup.
    const std::uintptr_t player = Player();
    ActorFacts v {};
    if (!ReadActor(victim, player, v)) { g_faults.fetch_add(1); return native; }
    if (!PlayerSide(v.side) && v.team != 1) return native;
    g_calls.fetch_add(1, std::memory_order_relaxed); // puppet mode: checks on an eligible victim
    AttackFacts a {};
    if (!ReadAttack(attack, a)) { g_faults.fetch_add(1); return native; }
    std::uintptr_t attacker = 0; bool viaSource = false;
    if (isDriven(a.owner)) attacker = a.owner;
    else if (a.source && a.source != a.owner && isDriven(a.source)) { attacker = a.source; viaSource = true; }
    // The local player's own attacks are never a puppet's, even if a driver ever named it.
    if (!attacker || attacker == victim || attacker == player) return native;
    ActorFacts o {};
    if (!ReadActor(attacker, player, o)) { g_faults.fetch_add(1); return native; }
    // Puppet mode counts only driven-puppet attacks on eligible victims (never puppet-on-enemy rows).
    g_pairs.fetch_add(1, std::memory_order_relaxed);
    g_allowed.fetch_add(1, std::memory_order_relaxed);
    Pair pair {Mode::Puppet, o.side, v.side, true, a.kind, v.team};
    pair.attackerDriven = true;
    const Verdict verdict = Decide(pair);
    if (BypassKind(a.kind)) g_kindKept.fetch_add(1, std::memory_order_relaxed);
    if (verdict == Verdict::Refuse) { g_refused.fetch_add(1, std::memory_order_relaxed); g_puppetRefused.fetch_add(1, std::memory_order_relaxed); }
    if (viaSource) g_viaSource.fetch_add(1, std::memory_order_relaxed);
    // Refusals only, so kept rows can't spend TRACE_BUDGET before a real overlap happens.
    if (verdict == Verdict::Refuse && g_log && g_traced.load() < TRACE_BUDGET && FirstSight(attack, victim)) {
        g_traced.fetch_add(1);
        TraceRow("puppet", o, attacker, v, victim, a, true, verdict, viaSource);
    }
    return verdict == Verdict::Refuse ? (native & ~0xFFull) : native;
}
} // namespace

// The answer this hook gives for a native one: the downed rule, then the installed mode's rules.
static std::uint64_t CanHitAnswer(std::uintptr_t attack, std::uintptr_t victim, std::uint64_t native) {
    if (const auto downed = g_downed.load(std::memory_order_acquire); downed && victim && victim == downed()) {
        if (native & 0xFF) g_downedRefusals.fetch_add(1, std::memory_order_relaxed);
        return native & ~std::uint64_t {0xFF}; // VUH-2465: nothing hits a logically downed player
    }
    auto mode = static_cast<Mode>(g_mode.load(std::memory_order_acquire));
    if (mode == Mode::Off) return native;
    // VUH-2421: host friendly fire off refuses what either rule refuses: a driven puppet's hit (including a
    // companion-kit puppet, which CoOp's player-class test misses) and every player-class pair.
    // In every hooked mode (D14.4 review): CoOp/Trace alone classify a companion-kit attacker as Other.
    const bool friendlyFireOff = !g_friendlyFire.load(std::memory_order_acquire);
    if (mode == Mode::Puppet || friendlyFireOff) {
        const std::uint64_t puppet = PuppetCanHit(attack, victim, native);
        if (!friendlyFireOff || puppet != native) return puppet;
    }
    if (friendlyFireOff) mode = Mode::CoOp;
    g_calls.fetch_add(1, std::memory_order_relaxed);
    const std::uintptr_t player = Player();
    ActorFacts v {};
    if (!ReadActor(victim, player, v)) { g_faults.fetch_add(1); return native; }
    if (!PlayerSide(v.side) && v.team != 1) return native; // cheap exit: player-class or team-1 (party) victims only
    AttackFacts a {};
    if (!ReadAttack(attack, a)) { g_faults.fetch_add(1); return native; }
    // The attacker: the owner (+0x10), or, as native also checks, the source (+0x14) when the owner is not a
    // player (a player's projectile or magic owned by another object).
    ActorFacts o {};
    if (!ReadActor(a.owner, player, o)) { g_faults.fetch_add(1); return native; }
    std::uintptr_t attacker = a.owner; bool viaSource = false;
    if (!PlayerSide(o.side) && a.source && a.source != a.owner) {
        ActorFacts s {};
        if (!ReadActor(a.source, player, s)) { g_faults.fetch_add(1); return native; }
        if (PlayerSide(s.side)) { o = s; attacker = a.source; viaSource = true; }
    }
    if (!PlayerSide(o.side)) return native;
    g_pairs.fetch_add(1, std::memory_order_relaxed);
    const bool allows = (native & 0xFF) != 0;
    if (allows) g_allowed.fetch_add(1, std::memory_order_relaxed);
    const Verdict verdict = Decide(Pair{mode, o.side, v.side, allows, a.kind, v.team});
    if (allows && mode == Mode::CoOp && BypassKind(a.kind)) g_kindKept.fetch_add(1, std::memory_order_relaxed);
    if (verdict == Verdict::Refuse) g_refused.fetch_add(1, std::memory_order_relaxed);
    if (viaSource) g_viaSource.fetch_add(1, std::memory_order_relaxed);
    const bool zeroBudget = allows || g_tracedZero.load() < TRACE_NATIVE_ZERO_BUDGET;
    if (g_log && attacker != victim && zeroBudget && g_traced.load() < TRACE_BUDGET && FirstSight(attack, victim)) {
        g_traced.fetch_add(1);
        if (!allows) g_tracedZero.fetch_add(1);
        TraceRow(mode == Mode::CoOp ? "coop" : "trace", o, attacker, v, victim, a, allows, verdict, viaSource);
    }
    return verdict == Verdict::Refuse ? (native & ~0xFFull) : native;
}

std::uint64_t __fastcall HookedCanHit(std::uintptr_t attack, std::uintptr_t victim) {
    const std::uint64_t native = g_original(attack, victim);
    const bool trace = g_enemyTrace.load(std::memory_order_relaxed) && g_log;
    if (trace) TraceEnemyCheck(attack, victim, native);
    const std::uint64_t answer = CanHitAnswer(attack, victim, native);
    if (trace) TraceThornVictim(attack, victim, native, answer);
    return answer;
}

void Tick(std::uint32_t frame) {
    if (static_cast<Mode>(g_mode.load(std::memory_order_acquire)) == Mode::Off || !g_log) return;
    if (frame - g_lastTick < 600) return;
    g_lastTick = frame;
    const auto key = g_pairs.load() * 1000003ull + g_refused.load() * 7919ull + g_faults.load();
    if (key == g_lastLogged) return;
    g_lastLogged = key;
    const Stats s = GetStats();
    g_log("[allyhit] stats f=%u calls=%llu playerPairs=%llu nativeAllowed=%llu refused=%llu puppetRefused=%llu kindKept=%llu viaSource=%llu faults=%llu traced=%u",
          frame, static_cast<unsigned long long>(s.calls), static_cast<unsigned long long>(s.playerPairs),
          static_cast<unsigned long long>(s.nativeAllowed), static_cast<unsigned long long>(s.refused),
          static_cast<unsigned long long>(s.puppetRefused),
          static_cast<unsigned long long>(s.kindKept), static_cast<unsigned long long>(s.viaSource),
          static_cast<unsigned long long>(s.faults), s.traced);
}

Mode CurrentMode() { return static_cast<Mode>(g_mode.load(std::memory_order_acquire)); }
void SetFriendlyFire(bool on) {
    if (g_friendlyFire.exchange(on, std::memory_order_acq_rel) == on || !g_log) return;
    g_log("[allyhit] friendly fire %s (host setting)", on ? "on" : "off");
}
Stats GetStats() {
    Stats s;
    s.calls = g_calls.load(); s.playerPairs = g_pairs.load(); s.nativeAllowed = g_allowed.load();
    s.refused = g_refused.load(); s.kindKept = g_kindKept.load(); s.faults = g_faults.load(); s.traced = g_traced.load();
    s.viaSource = g_viaSource.load(); s.tracedNativeZero = g_tracedZero.load(); s.puppetRefused = g_puppetRefused.load();
    return s;
}

#ifdef KH2COOP_ALLYHIT_TESTING
void TestSetOriginal(CanHitFn original) { g_original = original; }
#endif

void SetDownedVictim(DownedFn downed) { g_downed.store(downed, std::memory_order_release); }
std::uint64_t DownedRefusals() { return g_downedRefusals.load(std::memory_order_relaxed); }

bool Install(std::uintptr_t exeBase, LogFn log, ResolveFn resolve, FrameFn frame, DrivenFn driven) {
    char text[8] {};
    // Unset (0) or empty: Puppet mode, the VUH-1808 default. "0" is the explicit opt-out.
    const DWORD n = GetEnvironmentVariableA("KH2COOP_ALLY_HIT", text, sizeof(text));
    bool valid = false;
    const Mode mode = n < sizeof(text) ? ParseMode(n == 0 ? nullptr : text, valid) : Mode::Off;
    if (!valid || n >= sizeof(text)) {
        if (log) log("[allyhit] REFUSED: KH2COOP_ALLY_HIT must be 0, 1 or trace; not hooked");
        return false;
    }
    if (mode == Mode::Off) {
        if (log) log("[allyhit] off (KH2COOP_ALLY_HIT=0): not hooked; the host's friendly fire setting has no effect");
        return true;
    }
    g_base = exeBase; g_log = log; g_resolve = resolve; g_frame = frame; g_driven = driven;
    if (mode == Mode::Puppet && !driven) {
        if (log) log("[allyhit] REFUSED: puppet mode has no driven-puppet source; not hooked");
        return false;
    }
    if (!resolve) {
        if (log) log("[allyhit] REFUSED: the engine handle lookup (4AD270) is unverified; not hooked");
        return false;
    }
    if (std::memcmp(reinterpret_cast<const void*>(exeBase + RVA_CAN_HIT), kCanHitBytes, sizeof(kCanHitBytes)) != 0) {
        if (log) log("[allyhit] REFUSED: 3D2060 bytes differ from 9002b2de; not hooked");
        return false;
    }
#ifndef KH2COOP_ALLYHIT_TESTING
    auto* target = reinterpret_cast<void*>(exeBase + RVA_CAN_HIT);
    MH_STATUS st = MH_CreateHook(target, reinterpret_cast<void*>(&HookedCanHit), reinterpret_cast<void**>(&g_original));
    if (st == MH_OK) st = MH_EnableHook(target);
    if (st != MH_OK) {
        if (log) log("[allyhit] REFUSED: hook failed %d (%s)", st, MH_StatusToString(st));
        return false;
    }
#endif
    g_mode.store(static_cast<std::uint8_t>(mode), std::memory_order_release);
    char trace[2] {};
    g_enemyTrace.store(GetEnvironmentVariableA("KH2COOP_THORN_HIT_TRACE", trace, sizeof(trace)) == 1 && trace[0] == '1',
                       std::memory_order_release);
    if (g_enemyTrace.load() && log)
        log("[allyhit] VUH-2617 enemy-on-player check trace on, with every check against Thorn (KH2COOP_THORN_HIT_TRACE=1)");
    if (log) log("[allyhit] installed mode=%s: %s",
                 mode == Mode::CoOp ? "coop" : mode == Mode::Puppet ? "puppet" : "trace",
                 mode == Mode::CoOp ? "ally player-to-player hits refused at 3D2060 (atkp kinds 5/6 kept); traced"
                 : mode == Mode::Puppet ? "driven-puppet hits on players and the team-1 party refused at 3D2060 (atkp kinds 5/6 kept); traced"
                                        : "ally player-to-player hit checks traced only (no change)");
    return true;
}

} // namespace kh2coop::inject::allyhit
