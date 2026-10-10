// Ally player-to-player hits: the pure policy (AllyHitPolicy.hpp) and the real 3D2060 hook body
// (AllyHit.cpp, compiled with KH2COOP_ALLYHIT_TESTING: no MinHook) over owned memory only.
// No game process. Layout facts: AllyHit.hpp; the native rule: AllyHitPolicy.hpp.
#include "AllyHit.hpp"

#include <Windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

using namespace kh2coop::inject::allyhit;

static int g_fail = 0, g_pass = 0;
#define CHECK(name, cond) do { if (cond) ++g_pass; else { ++g_fail; std::printf("FAIL %s\n", name); } } while (0)

static std::string g_logged;
static void Log(const char* f, ...) { char b[512]; va_list a; va_start(a, f); std::vsnprintf(b, sizeof(b), f, a); va_end(a); g_logged += b; g_logged += '\n'; }
static bool Has(const char* s) { return g_logged.find(s) != std::string::npos; }

static std::uintptr_t g_image = 0, g_heap = 0;
static std::uintptr_t g_local = 0, g_clone = 0, g_clone2 = 0, g_enemy = 0, g_bullet = 0, g_companion = 0;
static std::uint64_t g_nativeAnswer = 1;
static std::uintptr_t __fastcall Resolve(std::uint32_t h) { return h == 1 ? g_local : h == 2 ? g_clone : h == 3 ? g_clone2 : h == 4 ? g_enemy : h == 5 ? g_bullet : h == 6 ? g_companion : 0; }
static std::uint32_t Frame() { return 77; }
static std::uint64_t __fastcall Native(std::uintptr_t, std::uintptr_t) { return g_nativeAnswer; }
static std::uintptr_t g_drivenActors[2] {};
static unsigned g_drivenCalls = 0;
static unsigned Driven(std::uintptr_t (&out)[2]) {
    ++g_drivenCalls; unsigned n = 0;
    for (auto a : g_drivenActors) if (a) out[n++] = a;
    return n;
}
template <class T> static void Put(std::uintptr_t p, T v) { std::memcpy(reinterpret_cast<void*>(p), &v, sizeof(v)); }

static std::uintptr_t Objentry(unsigned slot, std::uint8_t type, const char* name) {
    const std::uintptr_t o = g_image + 0x1000000 + slot * 0x60;
    Put<std::uint8_t>(o + OBJENTRY_TYPE, type); std::memcpy(reinterpret_cast<void*>(o + OBJENTRY_NAME), name, std::strlen(name) + 1);
    return o;
}
static std::uintptr_t Actor(unsigned slot, std::uintptr_t objentry, std::uint32_t team) {
    const std::uintptr_t a = g_heap + slot * 0x1000;
    Put(a + ACTOR_OBJENTRY, objentry); Put(a + ACTOR_TEAM, team);
    return a;
}
static std::uintptr_t Attack(unsigned slot, std::uint32_t ownerHandle, std::uint8_t kind, std::uint16_t atkpId, std::uint8_t team, std::uint32_t sourceHandle = 0) {
    const std::uintptr_t a = g_heap + 0x10000 + slot * 0x100, atkp = a + 0x80;
    Put(a + ATTACK_OWNER, ownerHandle); Put(a + ATTACK_SOURCE, sourceHandle); Put(a + ATTACK_ATKP, atkp); Put<std::uint8_t>(a + ATTACK_TEAM, team);
    Put<std::uint8_t>(a + ATTACK_MASK, static_cast<std::uint8_t>(~((1u << team) | 1u)));
    Put<std::uint16_t>(atkp + ATKP_ID, atkpId); Put<std::uint8_t>(atkp + ATKP_KIND, kind);
    return a;
}
static bool InstallMode(const char* text) { SetEnvironmentVariableA("KH2COOP_ALLY_HIT", text); return Install(g_image, &Log, &Resolve, &Frame); }

int main() {
    // ---- pure policy
    const Mode M[] {Mode::Off, Mode::Trace, Mode::CoOp};
    const Side S[] {Side::Other, Side::LocalPlayer, Side::RemotePlayer};
    unsigned refusals = 0, cells = 0;
    for (auto m : M) for (auto a : S) for (auto v : S) for (bool n : {false, true}) for (int ki : {0, 4, 5, 6}) {
        const auto k = static_cast<std::uint8_t>(ki); ++cells;
        const bool refuse = Decide(Pair{m, a, v, n, k}) == Verdict::Refuse;
        const bool want = m == Mode::CoOp && n && PlayerSide(a) && PlayerSide(v) && k != 5 && k != 6;
        if (refuse != want) { ++g_fail; std::printf("FAIL decide m=%u a=%u v=%u n=%u k=%u\n", unsigned(m), unsigned(a), unsigned(v), unsigned(n), unsigned(k)); }
        refusals += refuse;
    }
    CHECK("policy grid: refuses exactly co-op, native-allowed, player-to-player, non-5/6 kinds", refusals == 2 * 2 * 2 && cells == 3 * 3 * 3 * 2 * 4);
    CHECK("policy: never widens a native refusal", Decide(Pair{Mode::CoOp, Side::RemotePlayer, Side::LocalPlayer, false, 0}) == Verdict::Native);
    CHECK("policy: enemies stay native", Decide(Pair{Mode::CoOp, Side::Other, Side::LocalPlayer, true, 0}) == Verdict::Native &&
                                       Decide(Pair{Mode::CoOp, Side::RemotePlayer, Side::Other, true, 0}) == Verdict::Native);
    // Gap 2 (two players: a clone beside native Goofy): a clone never hits a team-1 non-player.
    unsigned gap2 = 0, gap2Cells = 0;
    for (auto m : M) for (auto a : S) for (std::uint32_t team : {0u, 1u, 2u}) for (bool n : {false, true}) for (int ki : {0, 5}) {
        ++gap2Cells; const auto k = static_cast<std::uint8_t>(ki);
        const bool refuse = Decide(Pair{m, a, Side::Other, n, k, team}) == Verdict::Refuse;
        const bool want = m == Mode::CoOp && n && a == Side::RemotePlayer && team == 1 && k != 5;
        if (refuse != want) { ++g_fail; std::printf("FAIL gap2 m=%u a=%u team=%u n=%u k=%u\n", unsigned(m), unsigned(a), team, unsigned(n), unsigned(k)); }
        gap2 += refuse;
    }
    CHECK("gap 2 grid: refuses exactly co-op, native-allowed, clone -> team-1 non-player, non-5/6", gap2 == 1 && gap2Cells == 3 * 3 * 3 * 2 * 2);
    bool ok = false;
    CHECK("parse: unset/empty puppet (VUH-1808 default), 0 off", ParseMode(nullptr, ok) == Mode::Puppet && ok &&
          ParseMode("", ok) == Mode::Puppet && ok && ParseMode("0", ok) == Mode::Off && ok);
    // Puppet mode (VUH-1808): refuses exactly a native-allowed, non-5/6 hit whose attacker is a driven
    // puppet, on a player-class or team-1 victim. The attacker's own side does not matter (a driven
    // friend-slot companion is Side::Other).
    unsigned puppetRefusals = 0, puppetCells = 0;
    for (auto a : S) for (auto v : S) for (std::uint32_t team : {0u, 1u, 2u}) for (bool n : {false, true})
    for (bool driven : {false, true}) for (int ki : {0, 4, 5, 6}) {
        ++puppetCells; const auto k = static_cast<std::uint8_t>(ki);
        Pair p {Mode::Puppet, a, v, n, k, team}; p.attackerDriven = driven;
        const bool refuse = Decide(p) == Verdict::Refuse;
        const bool want = n && driven && k != 5 && k != 6 && (PlayerSide(v) || team == 1);
        if (refuse != want) { ++g_fail; std::printf("FAIL puppet a=%u v=%u team=%u n=%u d=%u k=%u\n", unsigned(a), unsigned(v), team, unsigned(n), unsigned(driven), unsigned(k)); }
        puppetRefusals += refuse;
    }
    // 3 attackers x (2 player victims x 3 teams + Other x team 1) x 2 kinds = 42
    CHECK("puppet grid: refuses exactly driven, native-allowed, player or team-1 victims, non-5/6",
          puppetRefusals == 42 && puppetCells == 3 * 3 * 3 * 2 * 2 * 4);
    {
        Pair p {Mode::CoOp, Side::Other, Side::LocalPlayer, true, 0, 1}; p.attackerDriven = true;
        CHECK("policy: attackerDriven changes nothing outside puppet mode", Decide(p) == Verdict::Native);
    }
    CHECK("parse: 1 co-op, trace", ParseMode("1", ok) == Mode::CoOp && ok && ParseMode("trace", ok) == Mode::Trace && ok);
    CHECK("parse: anything else invalid", ParseMode("2", ok) == Mode::Off && !ok && ParseMode("on", ok) == Mode::Off && !ok);

    // ---- the hook body over owned memory
    g_image = reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr, 0x3000000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    g_heap = reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr, 0x40000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    CHECK("owned memory", g_image && g_heap);
    if (!g_image || !g_heap) return 1;
    const auto sora = Objentry(0, 0, "P_EX100"), mickey = Objentry(1, 0, "P_EX200"), shadow = Objentry(2, 4, "M_EX020");
    const auto proj = Objentry(3, 7, "F_PROJ");
    g_local = Actor(0, sora, 1); g_bullet = Actor(4, proj, 0); g_clone = Actor(1, mickey, 0); g_clone2 = Actor(2, sora, 0); g_enemy = Actor(3, shadow, 2);
    Put(g_image + RVA_NATIVE_PLAYER, g_local);
    TestSetOriginal(&Native);

    CHECK("0: off, no hook, no log", InstallMode("0") && g_logged.empty() && CurrentMode() == Mode::Off);
    SetEnvironmentVariableA("KH2COOP_ALLY_HIT", nullptr);
    CHECK("unset without a driven-puppet source refuses (native)",
          !Install(g_image, &Log, &Resolve, &Frame) && Has("no driven-puppet source") && CurrentMode() == Mode::Off);
    CHECK("bytes mismatch refuses", InstallMode("1") == false && Has("3D2060 bytes differ"));
    std::memcpy(reinterpret_cast<void*>(g_image + RVA_CAN_HIT), kCanHitBytes, sizeof(kCanHitBytes));
    CHECK("invalid value refuses", InstallMode("yes") == false && Has("must be 0, 1 or trace"));
    SetEnvironmentVariableA("KH2COOP_ALLY_HIT", "1");
    CHECK("no resolver refuses", !Install(g_image, &Log, nullptr, &Frame) && Has("handle lookup"));
    CHECK("co-op installs", InstallMode("1") && CurrentMode() == Mode::CoOp && Has("[allyhit] installed mode=coop"));

    const auto mickeySwing = Attack(0, 2, 0, 1190, 0), soraSwing = Attack(1, 1, 0, 126, 1), clone2Swing = Attack(2, 3, 0, 126, 0);
    const auto enemySwing = Attack(3, 4, 0, 500, 2), heal = Attack(4, 2, 5, 900, 0);
    g_nativeAnswer = 0x1234501;
    CHECK("co-op: clone -> local refused, upper bits kept", HookedCanHit(mickeySwing, g_local) == 0x1234500);
    CHECK("co-op: the refusal is traced with the measured teams and mask",
          Has("attacker=P_EX200@") && Has("(clone team=0) -> victim=P_EX100@") && Has("(local team=1) atkTeam=0 mask=0xFE kind=0 atkp=1190 native=1 verdict=refuse"));
    CHECK("co-op: local -> clone refused", HookedCanHit(soraSwing, g_clone) == 0x1234500);
    CHECK("co-op: clone -> clone refused", HookedCanHit(clone2Swing, g_clone) == 0x1234500);
    CHECK("co-op: clone -> enemy native", HookedCanHit(mickeySwing, g_enemy) == 0x1234501);
    CHECK("co-op: enemy -> local native", HookedCanHit(enemySwing, g_local) == 0x1234501);
    CHECK("co-op: local -> enemy native", HookedCanHit(soraSwing, g_enemy) == 0x1234501);
    CHECK("co-op: kind 5 between players kept native", HookedCanHit(heal, g_local) == 0x1234501 && GetStats().kindKept == 1);
    g_nativeAnswer = 0;
    CHECK("co-op: a native refusal stays a refusal", HookedCanHit(Attack(5, 2, 0, 1191, 0), g_local) == 0 && Has("atkp=1191 native=0 verdict=native"));
    const auto before = g_logged.size();
    g_nativeAnswer = 1; HookedCanHit(mickeySwing, g_local); HookedCanHit(mickeySwing, g_local);
    CHECK("trace: each (attack, victim) pair is traced once", g_logged.size() == before);
    const Stats s = GetStats();
    CHECK("stats: player pairs, refusals, no faults", s.playerPairs == 7 && s.refused == 5 && s.faults == 0 && s.nativeAllowed == 6);
    Tick(700);
    CHECK("tick: one stats line", Has("[allyhit] stats f=700") && Has("refused=5"));
    const auto afterTick = g_logged.size(); Tick(800); Tick(1400);
    CHECK("tick: silent within 600 frames and when unchanged", g_logged.size() == afterTick);

    // Gap 1 (review): a player's projectile/magic whose owner (+0x10) is another object; native also checks the source (+0x14).
    g_nativeAnswer = 1;
    const auto shot = Attack(7, 5, 0, 1300, 0, 2), enemyShot = Attack(8, 5, 0, 1301, 0, 4), bare = Attack(9, 5, 0, 1302, 0, 0);
    CHECK("gap 1: clone-sourced projectile -> local refused", HookedCanHit(shot, g_local) == 0 && GetStats().viaSource == 1);
    CHECK("gap 1: traced with the source as attacker", Has("attacker=P_EX200@") && Has("atkp=1300 native=1 verdict=refuse via=source"));
    CHECK("gap 1: enemy-sourced projectile -> local native", HookedCanHit(enemyShot, g_local) == 1);
    CHECK("gap 1: a sourceless object attack -> local native", HookedCanHit(bare, g_local) == 1 && GetStats().viaSource == 1);
    CHECK("owner-sourced rows say via=owner", Has("atkp=1190 native=1 verdict=refuse via=owner"));
    // Gap 4: owner == victim is never traced; native=0 rows are capped so refusals stay visible.
    g_nativeAnswer = 0;
    auto size0 = g_logged.size(); HookedCanHit(Attack(10, 2, 0, 1303, 0), g_clone);
    CHECK("gap 4: an attack on its own owner is not traced", g_logged.size() == size0);
    for (unsigned i = 0; i < 40; ++i) HookedCanHit(Attack(11 + i, 2, 0, static_cast<std::uint16_t>(1400 + i), 0), g_local);
    CHECK("gap 4: native=0 rows stop at their cap", GetStats().tracedNativeZero == TRACE_NATIVE_ZERO_BUDGET);
    g_nativeAnswer = 1;
    HookedCanHit(Attack(60, 2, 0, 1500, 0), g_local);
    CHECK("gap 4: a refusal is still traced after the cap", Has("atkp=1500 native=1 verdict=refuse"));

    // Gap 2 through the hook: native Goofy (objentry type 1, team 1) beside a clone.
    g_nativeAnswer = 1;
    const auto goofyObj = Objentry(4, 1, "P_EX030"); const auto goofy = Actor(5, goofyObj, 1);
    CHECK("gap 2: clone -> Goofy refused", HookedCanHit(Attack(70, 2, 0, 1600, 0), goofy) == 0 && Has("victim=P_EX030@") && Has("(other team=1) atkTeam=0 mask=0xFE kind=0 atkp=1600 native=1 verdict=refuse"));
    CHECK("gap 2: local -> Goofy native (its own mask decides)", HookedCanHit(Attack(71, 1, 0, 1601, 1), goofy) == 1);
    CHECK("gap 2: enemy -> Goofy native", HookedCanHit(Attack(72, 4, 0, 1602, 2), goofy) == 1);
    CHECK("gap 2: clone heal (kind 5) -> Goofy native", HookedCanHit(Attack(73, 2, 5, 1603, 0), goofy) == 1);
    const auto propObj = Objentry(5, 7, "F_PROP"); const auto prop = Actor(6, propObj, 0);
    const auto before2 = GetStats().playerPairs;
    CHECK("gap 2: a team-0 non-player victim stays native and uncounted", HookedCanHit(Attack(74, 2, 0, 1604, 0), prop) == 1 && GetStats().playerPairs == before2);

    CHECK("trace mode installs", InstallMode("trace") && CurrentMode() == Mode::Trace);
    g_nativeAnswer = 1;
    const auto traceSwing = Attack(6, 2, 0, 1192, 0);
    CHECK("trace: clone -> local untouched, traced as native", HookedCanHit(traceSwing, g_local) == 1 && Has("trace attacker=P_EX200@") && Has("atkp=1192 native=1 verdict=native"));

    // ---- puppet mode (VUH-1808 default: KH2COOP_ALLY_HIT unset)
    SetEnvironmentVariableA("KH2COOP_ALLY_HIT", nullptr);
    CHECK("unset: puppet mode installs", Install(g_image, &Log, &Resolve, &Frame, &Driven) && CurrentMode() == Mode::Puppet &&
          Has("[allyhit] installed mode=puppet"));
    g_nativeAnswer = 0x1234501;
    const Stats p0 = GetStats();
    const auto cloneSwing = Attack(80, 2, 0, 1700, 0);
    CHECK("puppet: no driven puppet -> clone -> local native, no reads", HookedCanHit(cloneSwing, g_local) == 0x1234501 &&
          GetStats().calls == p0.calls && g_drivenCalls > 0);
    g_drivenActors[0] = g_clone;
    CHECK("puppet: driven clone -> local refused, upper bits kept", HookedCanHit(cloneSwing, g_local) == 0x1234500);
    CHECK("puppet: the refusal is traced", Has("puppet attacker=P_EX200@") && Has("atkp=1700 native=1 verdict=refuse via=owner"));
    CHECK("puppet: enemy -> local native", HookedCanHit(Attack(81, 4, 0, 1701, 2), g_local) == 0x1234501);
    CHECK("puppet: local -> enemy native (client claim path untouched)", HookedCanHit(Attack(82, 1, 0, 1702, 1), g_enemy) == 0x1234501);
    CHECK("puppet: local -> clone native (its own mask decides)", HookedCanHit(Attack(83, 1, 0, 1703, 1), g_clone) == 0x1234501);
    CHECK("puppet: driven clone -> enemy native", HookedCanHit(Attack(84, 2, 0, 1704, 0), g_enemy) == 0x1234501);
    CHECK("puppet: driven clone heal (kind 5) -> local native", HookedCanHit(Attack(85, 2, 5, 1705, 0), g_local) == 0x1234501);
    CHECK("puppet: an undriven player-class actor -> local native", HookedCanHit(Attack(86, 3, 0, 1706, 0), g_local) == 0x1234501);
    CHECK("puppet: driven-clone-sourced projectile -> local refused", HookedCanHit(Attack(87, 5, 0, 1707, 0, 2), g_local) == 0x1234500 &&
          Has("atkp=1707 native=1 verdict=refuse via=source"));
    CHECK("puppet: enemy-sourced projectile -> local native", HookedCanHit(Attack(88, 5, 0, 1708, 0, 4), g_local) == 0x1234501);
    CHECK("puppet: driven clone -> Goofy (team 1) refused", HookedCanHit(Attack(89, 2, 0, 1709, 0), goofy) == 0x1234500);
    CHECK("puppet: local -> Goofy native", HookedCanHit(Attack(90, 1, 0, 1710, 1), goofy) == 0x1234501);
    CHECK("puppet: driven clone -> team-0 prop native", HookedCanHit(Attack(91, 2, 0, 1711, 0), prop) == 0x1234501);
    g_nativeAnswer = 0;
    CHECK("puppet: a native refusal stays a refusal", HookedCanHit(Attack(92, 2, 0, 1712, 0), g_local) == 0);
    g_nativeAnswer = 0x1234501;
    // A friend-slot companion puppet (objentry type 1) driven with team 0: its swings are a remote player's too.
    const auto donaldObj = Objentry(6, 1, "P_EX020");
    g_companion = Actor(7, donaldObj, 0);
    g_drivenActors[1] = g_companion;
    CHECK("puppet: driven companion puppet -> local refused", HookedCanHit(Attack(93, 6, 0, 1713, 0), g_local) == 0x1234500 &&
          Has("attacker=P_EX020@"));
    // Defensive: a driver that ever named the canonical player never vetoes the local player's own hits.
    g_drivenActors[1] = g_local;
    CHECK("puppet: the local player's own swing is never a puppet's", HookedCanHit(Attack(94, 1, 0, 1714, 1), goofy) == 0x1234501 &&
          HookedCanHit(Attack(95, 1, 0, 1715, 1), g_clone) == 0x1234501);
    g_drivenActors[0] = g_drivenActors[1] = 0;
    CHECK("puppet: released puppet -> native again", HookedCanHit(Attack(96, 2, 0, 1716, 0), g_local) == 0x1234501);
    const Stats p1 = GetStats();
    CHECK("puppet stats: refusals counted", p1.puppetRefused == 4 && p1.refused - p0.refused == 4 && p1.faults == 0);
    Tick(2100);
    CHECK("puppet tick: stats line names puppetRefused", Has("puppetRefused=4"));

    std::printf("allyhit: %d passed, %d failed\n", g_pass, g_fail);
    if (g_fail) std::printf("--- log ---\n%s", g_logged.c_str());
    return g_fail ? 1 : 0;
}
