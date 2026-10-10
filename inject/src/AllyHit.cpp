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
std::atomic<std::uint8_t> g_mode{static_cast<std::uint8_t>(Mode::Off)};
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
    g_calls.fetch_add(1, std::memory_order_relaxed);
    AttackFacts a {};
    if (!ReadAttack(attack, a)) { g_faults.fetch_add(1); return native; }
    std::uintptr_t attacker = 0; bool viaSource = false;
    if (isDriven(a.owner)) attacker = a.owner;
    else if (a.source && a.source != a.owner && isDriven(a.source)) { attacker = a.source; viaSource = true; }
    const std::uintptr_t player = Player();
    // The local player's own attacks are never a puppet's, even if a driver ever named it.
    if (!attacker || attacker == victim || attacker == player) return native;
    ActorFacts v {}, o {};
    if (!ReadActor(victim, player, v) || !ReadActor(attacker, player, o)) { g_faults.fetch_add(1); return native; }
    g_pairs.fetch_add(1, std::memory_order_relaxed);
    g_allowed.fetch_add(1, std::memory_order_relaxed);
    Pair pair {Mode::Puppet, o.side, v.side, true, a.kind, v.team};
    pair.attackerDriven = true;
    const Verdict verdict = Decide(pair);
    if (BypassKind(a.kind)) g_kindKept.fetch_add(1, std::memory_order_relaxed);
    if (verdict == Verdict::Refuse) { g_refused.fetch_add(1, std::memory_order_relaxed); g_puppetRefused.fetch_add(1, std::memory_order_relaxed); }
    if (viaSource) g_viaSource.fetch_add(1, std::memory_order_relaxed);
    if (g_log && g_traced.load() < TRACE_BUDGET && FirstSight(attack, victim)) {
        g_traced.fetch_add(1);
        TraceRow("puppet", o, attacker, v, victim, a, true, verdict, viaSource);
    }
    return verdict == Verdict::Refuse ? (native & ~0xFFull) : native;
}
} // namespace

std::uint64_t __fastcall HookedCanHit(std::uintptr_t attack, std::uintptr_t victim) {
    const std::uint64_t native = g_original(attack, victim);
    const auto mode = static_cast<Mode>(g_mode.load(std::memory_order_acquire));
    if (mode == Mode::Off) return native;
    if (mode == Mode::Puppet) return PuppetCanHit(attack, victim, native);
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
    if (mode == Mode::Off) return true;
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
    if (log) log("[allyhit] installed mode=%s: %s",
                 mode == Mode::CoOp ? "coop" : mode == Mode::Puppet ? "puppet" : "trace",
                 mode == Mode::CoOp ? "ally player-to-player hits refused at 3D2060 (atkp kinds 5/6 kept); traced"
                 : mode == Mode::Puppet ? "driven-puppet hits on players and the team-1 party refused at 3D2060 (atkp kinds 5/6 kept); traced"
                                        : "ally player-to-player hit checks traced only (no change)");
    return true;
}

} // namespace kh2coop::inject::allyhit
