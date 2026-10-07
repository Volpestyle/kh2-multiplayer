// ============================================================================
// EnemyTargetRemote.inl - VUH-1515: host enemies target remote players' native
// clones (KH2COOP_ENEMY_TARGET_REMOTE=1, default off). Native adapter.
//
// Included inside EntityHook's namespace after PuppetIndexFor. Uses EntityHook
// statics: Log, g_exeBase, g_soraActor, g_frameCounter, ReadHitTrace(Memory),
// g_resolveHandle, g_puppets, PuppetTarget, IsPuppetActive, IsPlayerClassActor.
// Pure rules: EnemyTargetRemote.hpp. Design: docs/ENEMY_TARGET_REMOTE.md.
//
// Host (enemysync role Host):
//   - swaps the bdscript bank1/121 "target_search" slot exe+0x755B00 (0x4303A0,
//     75-byte match) for the session. The replacement always runs the native
//     selector first. Only when the searching actor is a live, allowlisted enemy
//     (Shadow 302) and the native result is the canonical player form does it
//     apply the policy (nearest live player, hysteresis, hold, per-clone cap) and,
//     when a remote clone wins, write {clone handle, part 0} into that enemy's
//     +0xBF8. The Shadow's mode_battle measures its attack distance to that slot
//     in the same brain step (m_ex020 PC 2275/2290/2078).
//   - keeps a candidate clone's native team and collision (ApplyPuppetTransform);
//   - a hit on a candidate clone from an allowlisted enemy is still zeroed by the
//     existing DamagePolicy (NonLocalPlayer -> ZeroHp/RemoteVictim); in Forward mode
//     (KH2COOP_ENEMY_MIRROR off) it is then forwarded once to the clone's owner as RemoteHit;
//   - advertises TargetAuthority {slotMask, familyMask, mode} on change and every second.
// Owner (enemysync role Client):
//   - an admitted RemoteHit is applied to the canonical local player through the
//     hooked native stat funnel 0x3D2EB0 (ApplyStatDelta, delta -damage, idx 0);
//   - local hits on the canonical player from allowlisted families are zeroed
//     (DamagePolicy HostEnemyAuthority) only while a fresh Forward-mode TargetAuthority
//     names our slot, so one attack is applied once and the owner is never immune.
// Mirror mode: target choice and team keep only; no forward, no cancel (mirrored hits are native).
// Teardown: room change forgets assignments (no writes, actors are gone); a role
// or session change releases every slot still holding our write and restores the
// syscall slot; Shutdown restores the syscall slot only (no actor writes).
// ============================================================================

namespace enemytarget {

namespace R = rules;
using SearchFn = void(__fastcall*)(std::uint32_t* stack);

constexpr uintptr_t RVA_TARGET_SEARCH = 0x4303A0;
constexpr uintptr_t RVA_TARGET_SEARCH_SLOT = 0x755B00;  // bank1 table 0x755370 + 16*121
constexpr uintptr_t RVA_NATIVE_PLAYER = 0x2A105D0;
constexpr uintptr_t RVA_LOCATION = 0x717008;            // u8 world, u8 room
constexpr uintptr_t OFF_TARGET = 0xBF8;
constexpr uintptr_t OFF_STATUS = 0x5C0;
constexpr uintptr_t OFF_FLAGS120 = 0x120;
constexpr uintptr_t OFF_FLAGS9B8 = 0x9B8;
constexpr uintptr_t OFF_TEAM = 0x4DC;
constexpr uintptr_t OFF_NEXT = 0xA90;
constexpr uintptr_t OFF_INVULN = 0xD70;                  // 0x3D65A0 skips non-heal hits while > 0
constexpr std::uint32_t ENEMY_TEAM = 2;
constexpr std::uint32_t POSE_FRESH_FRAMES = 30;
constexpr unsigned LOG_BUDGET = 64;

constexpr unsigned FORWARD_LOG_BUDGET = 256, AUTHORITY_LOG_BUDGET = 64, VETO_LOG_BUDGET = 64, OWNER_LOG_BUDGET = 256;
constexpr std::uint32_t STATS_INTERVAL = 300;

struct Stats {
    std::uint64_t searches = 0, rewrites = 0, keptLocal = 0, notPlayerForm = 0, notEligible = 0, tableFull = 0;
    std::uint64_t releases = 0, forgets = 0, forwards = 0, forwardFailed = 0, forwardUnmatched = 0;
    std::uint64_t ownerSeen = 0, ownerApplied = 0, ownerRefused = 0, keeps = 0;
    std::uint64_t authoritySent = 0, authorityFailed = 0, vetoes = 0, vetoNoAuthority = 0;
    std::uint64_t refusedInvulnerable = 0, invulnOverride = 0, refusedStale = 0;
};

struct State {
    bool requested = false, swapped = false, swapRefused = false, hostActive = false;
    std::uint8_t role = 0;
    std::uint32_t generation = 0, transition = 0, load = 0;
    uintptr_t candidateActor[R::kCandidates] {};
    std::uint32_t candidateHandle[R::kCandidates] {};
    std::uint8_t ownerSlot[R::kCandidates] {};
    R::Candidate candidates[R::kCandidates] {};
    R::Assignment table[R::Policy::TableCapacity] {};
    std::uint32_t written[R::Policy::TableCapacity] {};  // clone handle we wrote, 0 = none
    std::uint32_t native[R::Policy::TableCapacity] {};   // the native player handle we replaced
    std::uint32_t nativePart[R::Policy::TableCapacity] {}; // ... and its part (S4)
    std::uint8_t mode = 0;                               // R::Mode, fixed at Configure (host's mirror flag)
    // Host advertisement.
    std::uint8_t sentMask = 0, sentMode = 0;
    bool sentAny = false;
    std::uint64_t sentMs = 0;
    // Owner.
    enemysync::TargetAuthorityView view {};
    std::uint8_t loggedMask = 0xFF, loggedMode = 0xFF;
    R::VetoEpisode episode {};                           // N2: the i-frames our own cancel started
    std::uint64_t coveredSinceMs = 0;                    // S7/S9: start of the current continuous coverage
    bool graceLatched = false;                           // S10: a live DownedSpike grace episode
    std::uint32_t reviveCount = 0;                       // S10: DownedSpike's revive counter last seen
    std::uint32_t statsFrame = 0;
    Stats stats {};
    unsigned logs = 0, forwardLogs = 0, authorityLogs = 0, vetoLogs = 0, ownerLogs = 0;
};
static State g;
static SearchFn g_nativeSearch = nullptr;

static unsigned long long U(uintptr_t v) { return static_cast<unsigned long long>(v); }
static bool LogAllowed() { if (g.logs >= LOG_BUDGET) return false; ++g.logs; return true; }
static bool Budget(unsigned& used, unsigned cap) { if (used >= cap) return false; ++used; return true; }
static bool Env1(const char* name) {
    char v[8] = {};
    const DWORD n = GetEnvironmentVariableA(name, v, sizeof(v));
    return n == 1 && v[0] == '1';
}
static bool GameThread() noexcept { return spawncontroller::IsDiagnosticGameThread(); }

static bool Resolve(std::uint32_t handle, uintptr_t& out) noexcept {
    out = 0;
    if (!g_resolveHandle || !handle) return false;
    __try { out = g_resolveHandle(handle); } __except (EXCEPTION_EXECUTE_HANDLER) { out = 0; }
    return out != 0;
}
// The 32-bit handle the native list uses for `actor` (its predecessor's next link).
static std::uint32_t ListHandle(uintptr_t actor) noexcept {
    if (!actor) return 0;
    uintptr_t cursor = 0;
    if (!ReadHitTrace(g_exeBase + offsets::active_entity_list::HEAD, cursor)) return 0;
    for (unsigned i = 0; cursor && i < offsets::active_entity_list::MAX_TRAVERSAL; ++i) {
        std::uint32_t link = 0;
        uintptr_t next = 0;
        if (!ReadHitTrace(cursor + OFF_NEXT, link) || !link || !Resolve(link, next)) return 0;
        if (next == actor) return link;
        cursor = next;
    }
    return 0;
}
static bool ReadXZ(uintptr_t actor, float& x, float& z) noexcept {
    const uintptr_t e = actor + offsets::actor::ENTITY_TRANSFORM + offsets::entity::POS_X;
    return ReadHitTrace(e, x) && ReadHitTrace(e + 8, z) && std::isfinite(x) && std::isfinite(z);
}
static bool ReadLive(uintptr_t actor, uintptr_t& status) noexcept {
    std::uint32_t f120 = 0, f9b8 = 0;
    std::int32_t hp = 0;
    status = 0;
    return ReadHitTrace(actor + OFF_FLAGS120, f120) && ReadHitTrace(actor + OFF_FLAGS9B8, f9b8) &&
           ReadHitTrace(actor + OFF_STATUS, status) && status && ReadHitTrace(status, hp) && R::ActorLive(f120, f9b8, hp);
}
static bool Canonical(uintptr_t& player) noexcept {
    player = 0;
    return ReadHitTrace(g_exeBase + RVA_NATIVE_PLAYER, player) && player != 0 && player == g_soraActor;
}
static bool ReadSlot(uintptr_t enemy, R::Slot& s) noexcept { return ReadHitTraceMemory(enemy + OFF_TARGET, &s, sizeof(s)); }

// A live, allowlisted enemy (type mob, team 2, HP > 0) that owns this +0xBF8.
static bool EligibleEnemy(uintptr_t enemy, uintptr_t& objentry) noexcept {
    std::uint8_t type = 0;
    std::uint32_t id = 0, team = 0;
    uintptr_t status = 0;
    objentry = 0;
    return ReadHitTrace(enemy + offsets::actor::OBJENTRY_PTR, objentry) && objentry > g_exeBase &&
           objentry < g_exeBase + 0x3000000 && ReadHitTrace(objentry + offsets::objentry::TYPE_FLAGS, type) &&
           type == offsets::objentry::TYPE_MOB && ReadHitTrace(objentry + offsets::objentry::OBJECT_ID, id) &&
           R::AllowedObject(id) && ReadHitTrace(enemy + OFF_TEAM, team) && team == ENEMY_TEAM && ReadLive(enemy, status);
}

static void Configure() {
    g = {};
    if (!Env1("KH2COOP_ENEMY_TARGET_REMOTE")) return;
    if (Env1("KH2COOP_PARTY_NATIVE")) {  // no joint fixture yet: the two flags refuse each other
        Log("[enemy-target] configure refused=party-native");
        return;
    }
    g.requested = true;
    // Distinct clone status is a candidate requirement; the private-status scope must be able to
    // give a clone one outside GoA (verified rooms only, party row 00/00/02/12; review S2).
    privatestatus::EnableEnemyTargetScope();
    g.mode = static_cast<std::uint8_t>(enemysync::MirrorRequested() ? R::Mode::Mirror : R::Mode::Forward);
    g.reviveCount = downedspike::g_down.reviveCount;
    Log("[enemy-target] configure requested=1 schema=2 allow=302 hysteresis=%.0f hold=%u cap=%u table=%u mirror=%u",
        double(R::Policy::Hysteresis), R::Policy::MinHoldFrames, R::Policy::CloneCap, R::Policy::TableCapacity,
        unsigned(g.mode));
}

// ---- target_search detour -------------------------------------------------------------------
static int FindAssignment(uintptr_t enemy, uintptr_t objentry) noexcept {
    for (unsigned i = 0; i < R::Policy::TableCapacity; ++i)
        if (g.table[i].used && g.table[i].enemy == enemy && g.table[i].objentry == objentry) return static_cast<int>(i);
    return -1;
}
static int NewAssignment(uintptr_t enemy, uintptr_t objentry) noexcept {
    for (unsigned i = 0; i < R::Policy::TableCapacity; ++i) {
        if (g.table[i].used) continue;
        g.table[i] = {};
        g.table[i].used = true; g.table[i].enemy = enemy; g.table[i].objentry = objentry;
        g.table[i].target = 0; g.table[i].since = g_frameCounter;
        g.written[i] = 0;
        return static_cast<int>(i);
    }
    return -1;
}

static void __fastcall SearchDetour(std::uint32_t* stack) {
    const std::uint32_t targetHandle = stack[0];
    g_nativeSearch(stack);  // the native selector always runs first
    if (!g.hostActive || !GameThread()) return;
    uintptr_t t = 0, objentry = 0, player = 0, named = 0;
    if (!Resolve(targetHandle, t) || t < 0x10000 + OFF_TARGET) return;
    const uintptr_t enemy = t - OFF_TARGET;
    if (!EligibleEnemy(enemy, objentry)) { ++g.stats.notEligible; return; }
    ++g.stats.searches;
    R::Slot now {};
    if (!ReadSlot(enemy, now) || !Canonical(player)) return;
    Resolve(now.handle, named);
    if (!R::PlayerForm(now, named == player)) { ++g.stats.notPlayerForm; return; }
    int index = FindAssignment(enemy, objentry);
    const bool fresh = index < 0;  // a new enemy has no current target: no hold, no hysteresis
    if (fresh) index = NewAssignment(enemy, objentry);
    if (index < 0) { ++g.stats.tableFull; return; }
    auto& a = g.table[index];
    std::uint32_t load[R::kCandidates] {};
    for (unsigned i = 0; i < R::Policy::TableCapacity; ++i)
        if (g.table[i].used && static_cast<int>(i) != index && g.table[i].target < R::kCandidates) ++load[g.table[i].target];
    float ex = 0, ez = 0;
    if (!ReadXZ(enemy, ex, ez)) return;
    const int k = R::Choose(g.candidates, ex, ez, fresh ? nullptr : &a, load, g_frameCounter);
    if (k < 0) { if (fresh) a = {}; return; }
    if (fresh || static_cast<std::uint8_t>(k) != a.target) { a.target = static_cast<std::uint8_t>(k); a.since = g_frameCounter; }
    if (k == 0) { g.written[index] = 0; ++g.stats.keptLocal; return; }
    const std::uint32_t handle = g.candidateHandle[k];
    if (!handle) return;
    __try {
        *reinterpret_cast<volatile std::uint32_t*>(enemy + OFF_TARGET) = handle;
        *reinterpret_cast<volatile std::uint32_t*>(enemy + OFF_TARGET + 4) = 0;
        g.written[index] = handle;
        g.native[index] = now.handle;
        g.nativePart[index] = now.part;
        ++g.stats.rewrites;
    } __except (EXCEPTION_EXECUTE_HANDLER) { g.written[index] = 0; }
    if (LogAllowed())
        Log("[enemy-target] assign frame=%u enemy=%llX target=%d owner=%u mode=%u", g_frameCounter, U(enemy), k,
            unsigned(g.ownerSlot[k]), now.mode);
}
static void* const g_ourSearch = reinterpret_cast<void*>(&SearchDetour);

static bool SwapSearch() {
    std::uint8_t body[75] {};
    uintptr_t current = 0;
    MEMORY_BASIC_INFORMATION mbi {};
    const uintptr_t slot = g_exeBase + RVA_TARGET_SEARCH_SLOT, fn = g_exeBase + RVA_TARGET_SEARCH;
    if (!ReadHitTrace(slot, current) || current != fn || !ReadHitTraceMemory(fn, body, sizeof(body)) ||
        !R::TargetSearchShape(body) || VirtualQuery(reinterpret_cast<void*>(slot), &mbi, sizeof(mbi)) != sizeof(mbi) ||
        mbi.State != MEM_COMMIT || mbi.Protect != PAGE_READWRITE) {
        Log("[enemy-target] search-refused slot=%llX current=%llX protect=%lX", U(slot - g_exeBase),
            U(current ? current - g_exeBase : 0), static_cast<unsigned long>(mbi.Protect));
        return false;
    }
    g_nativeSearch = reinterpret_cast<SearchFn>(fn);
    void* prior = InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(slot), g_ourSearch,
                                                    reinterpret_cast<void*>(fn));
    g.swapped = prior == reinterpret_cast<void*>(fn);
    Log("[enemy-target] search-swap ok=%u", unsigned(g.swapped));
    return g.swapped;
}
static void RestoreSearch() noexcept {
    if (!g.swapped) return;
    void* prior = InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(g_exeBase + RVA_TARGET_SEARCH_SLOT),
                                                    reinterpret_cast<void*>(g_exeBase + RVA_TARGET_SEARCH), g_ourSearch);
    Log("[enemy-target] search-restore ok=%u", unsigned(prior == g_ourSearch));
    g.swapped = false;
}

// ---- assignments: release and forget ---------------------------------------------------------
// Owner thread, lifecycle unchanged since the write: put the canonical player back only if the
// slot still holds exactly our write. Never touches an enemy whose objentry changed.
static void Release(unsigned i) noexcept {
    auto& a = g.table[i];
    if (a.used && g.written[i]) {
        uintptr_t objentry = 0, player = 0, back = 0;
        R::Slot now {};
        // The native player handle we replaced, only while it still names the canonical player.
        if (ReadHitTrace(static_cast<uintptr_t>(a.enemy) + offsets::actor::OBJENTRY_PTR, objentry) && objentry == a.objentry &&
            ReadSlot(static_cast<uintptr_t>(a.enemy), now) && now.handle == g.written[i] && now.part == 0 &&
            Canonical(player) && Resolve(g.native[i], back) && back == player) {
            __try {
                *reinterpret_cast<volatile std::uint32_t*>(a.enemy + OFF_TARGET) = g.native[i];
                *reinterpret_cast<volatile std::uint32_t*>(a.enemy + OFF_TARGET + 4) = g.nativePart[i];
                ++g.stats.releases;
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
    }
    a = {};
    g.written[i] = 0; g.native[i] = 0; g.nativePart[i] = 0;
}
static void ForgetAll() noexcept {
    for (unsigned i = 0; i < R::Policy::TableCapacity; ++i) {
        if (g.table[i].used) ++g.stats.forgets;
        g.table[i] = {};
        g.written[i] = 0; g.native[i] = 0; g.nativePart[i] = 0;
    }
}
static void ReleaseAll() noexcept { for (unsigned i = 0; i < R::Policy::TableCapacity; ++i) Release(i); }

static void LogStats();
static void Advertise(bool force);
static void RefreshOwnerView();
static void Stop(const char* why) {
    if (g.hostActive || g.swapped) {
        // Best effort only: in most stop cases (role no longer Host, no session, new generation)
        // the send is refused. The guarantee is the owner's AuthorityMaxAgeMs age-out.
        g.hostActive = false;
        Advertise(true);
        ReleaseAll();
        RestoreSearch();
        Log("[enemy-target] stop why=%s searches=%llu rewrites=%llu keptLocal=%llu releases=%llu forgets=%llu forwards=%llu "
            "forwardFailed=%llu forwardUnmatched=%llu notPlayerForm=%llu tableFull=%llu", why, g.stats.searches,
            g.stats.rewrites, g.stats.keptLocal, g.stats.releases, g.stats.forgets, g.stats.forwards,
            g.stats.forwardFailed, g.stats.forwardUnmatched, g.stats.notPlayerForm, g.stats.tableFull);
        LogStats();
    }
    g.hostActive = false;
    for (auto& c : g.candidates) c = {};
}

// ---- per frame (list head, game thread) -------------------------------------------------------
static void RefreshCandidates() {
    uintptr_t player = 0, status = 0;
    float x = 0, z = 0;
    std::uint8_t loc[2] {};
    const bool haveLocation = ReadHitTraceMemory(g_exeBase + RVA_LOCATION, loc, sizeof(loc));
    uintptr_t soraStatus = 0;
    g.candidates[0] = {};
    g.candidateActor[0] = 0;
    const bool canonical = Canonical(player);
    if (canonical && !ReadHitTrace(player + OFF_STATUS, soraStatus)) soraStatus = 0;  // compared even when downed
    uintptr_t liveStatus = 0;
    if (canonical && ReadLive(player, liveStatus) && ReadXZ(player, x, z)) {
        g.candidates[0] = {true, x, z};
        g.candidateActor[0] = player;
        g.ownerSlot[0] = 0;
    }
    for (int i = 0; i < 2; ++i) {
        const unsigned k = static_cast<unsigned>(i) + 1;
        const uintptr_t actor = PuppetTarget(i);
        const auto& d = g_puppets[i];
        R::CloneFacts f {};
        f.puppetActive = actor && IsPuppetActive(i);
        f.playerClass = actor && IsPlayerClassActor(actor) && actor != g_soraActor;
        f.actorLive = actor && ReadLive(actor, status);
        f.distinctStatus = f.actorLive && soraStatus && status && status != soraStatus;
        f.poseFresh = d.have && g_frameCounter - d.poseFrame <= POSE_FRESH_FRAMES;
        f.sameRoom = haveLocation && d.pose.pose.worldId == loc[0] && d.pose.pose.roomId == loc[1];
        f.downed = (d.pose.pose.flags & kh2coop::AvatarDowned) != 0;
        f.inCutscene = (d.pose.pose.flags & kh2coop::AvatarInCutscene) != 0;
        f.ownerSlot = static_cast<std::uint8_t>(d.pose.pose.ownerSlot);
        const bool ok = R::CloneCandidate(f) && ReadXZ(actor, x, z);
        const std::uint32_t handle = ok ? (g.candidateActor[k] == actor && g.candidateHandle[k] ? g.candidateHandle[k] : ListHandle(actor)) : 0;
        uintptr_t back = 0;
        const bool resolved = handle && Resolve(handle, back) && back == actor;
        g.candidates[k] = {ok && resolved, x, z};
        g.candidateActor[k] = ok && resolved ? actor : 0;
        g.candidateHandle[k] = ok && resolved ? handle : 0;
        g.ownerSlot[k] = f.ownerSlot;
    }
}

static void OnFrameStart() {
    if (!g.requested || !GameThread()) return;
    const auto transition = warp::TransitionSerial();
    const auto load = warp::LoadSerial();
    const bool pending = warp::TransitionPending();
    if (transition != g.transition || load != g.load || pending) {
        // Room change: the enemies we wrote are being torn down; forget without writing.
        ForgetAll();
        g.transition = transition; g.load = load;
        for (auto& c : g.candidates) c = {};
        g.view = {};  // N1: never cancel on a view this pass did not refresh
        g.coveredSinceMs = 0;
        if (pending) return;
    }
    const std::uint8_t role = enemysync::ActivationRole();
    const std::uint32_t generation = enemysync::WorldSessionGeneration();
    if (g_frameCounter - g.statsFrame >= STATS_INTERVAL) { g.statsFrame = g_frameCounter; LogStats(); }
    if (role == 2) RefreshOwnerView(); else { g.view = {}; g.coveredSinceMs = 0; }
    if (role != 1 || !generation) {  // not the host of a live session (1 = Role::Host)
        if (g.hostActive || g.swapped) Stop(role != 1 ? "not-host" : "no-session");
        g.role = role; g.generation = generation;
        return;
    }
    if (g.hostActive && generation != g.generation) Stop("session-changed");
    g.role = role; g.generation = generation;
    if (!g.swapped && !g.swapRefused) {
        if (!SwapSearch()) { g.swapRefused = true; return; }
    }
    if (!g.swapped) return;
    g.hostActive = true;
    RefreshCandidates();
    // Drop assignments whose enemy is gone; release those whose clone is no longer a candidate.
    for (unsigned i = 0; i < R::Policy::TableCapacity; ++i) {
        auto& a = g.table[i];
        if (!a.used) continue;
        uintptr_t objentry = 0;
        if (!EligibleEnemy(static_cast<uintptr_t>(a.enemy), objentry) || objentry != a.objentry) {
            a = {}; g.written[i] = 0; g.native[i] = 0; g.nativePart[i] = 0; ++g.stats.forgets;
            continue;
        }
        if (a.target != 0 && (a.target >= R::kCandidates || !g.candidates[a.target].valid ||
                              (g.written[i] && g.written[i] != g.candidateHandle[a.target])))
            Release(i);
    }
    Advertise(false);
}

// ---- TargetAuthority, stats -------------------------------------------------------------------
static void LogStats() {
    Log("[enemy-target] stats frame=%u role=%u active=%u swapped=%u mode=%u searches=%llu rewrites=%llu keptLocal=%llu "
        "releases=%llu forgets=%llu forwards=%llu forwardFailed=%llu forwardUnmatched=%llu keeps=%llu authoritySent=%llu "
        "authorityFailed=%llu vetoes=%llu vetoNoAuthority=%llu ownerApplied=%llu ownerRefused=%llu refusedInvulnerable=%llu "
        "invulnOverride=%llu refusedStale=%llu authorityHeld=%u heldMask=%u heldMode=%u",
        g_frameCounter, unsigned(g.role), unsigned(g.hostActive), unsigned(g.swapped), unsigned(g.mode), g.stats.searches,
        g.stats.rewrites, g.stats.keptLocal, g.stats.releases, g.stats.forgets, g.stats.forwards, g.stats.forwardFailed,
        g.stats.forwardUnmatched, g.stats.keeps, g.stats.authoritySent, g.stats.authorityFailed, g.stats.vetoes,
        g.stats.vetoNoAuthority, g.stats.ownerApplied, g.stats.ownerRefused, g.stats.refusedInvulnerable,
        g.stats.invulnOverride, g.stats.refusedStale, unsigned(g.view.held), unsigned(g.view.held ? g.view.slotMask : 0),
        unsigned(g.view.held ? g.view.mode : 0));
}
// Host: on every change of slotMask or mode, and at least every AuthorityPeriodFrames.
static void Advertise(bool force) {
    bool valid[R::kCandidates] {};
    for (unsigned k = 0; k < R::kCandidates; ++k) valid[k] = g.candidates[k].valid;
    const std::uint8_t mask = R::SlotMask(g.hostActive, g.swapped, valid, g.ownerSlot);
    const bool changed = !g.sentAny || mask != g.sentMask || g.mode != g.sentMode;
    const std::uint64_t nowMs = GetTickCount64();
    if (!force && !changed && nowMs >= g.sentMs && nowMs - g.sentMs < R::Policy::AuthorityPeriodMs) return;
    std::uint64_t seq = 0;
    const bool sent = enemysync::SendTargetAuthority(mask, R::kFamilyShadow, g.mode, seq);
    if (sent) ++g.stats.authoritySent; else ++g.stats.authorityFailed;
    g.sentMs = nowMs;
    if (sent) { g.sentAny = true; g.sentMask = mask; g.sentMode = g.mode; }
    if ((changed || force) && Budget(g.authorityLogs, AUTHORITY_LOG_BUDGET))
        Log("[enemy-target] authority frame=%u seq=%llu slotMask=%u mode=%u sent=%u", g_frameCounter,
            static_cast<unsigned long long>(seq), unsigned(mask), unsigned(g.mode), unsigned(sent));
}
// Owner: refresh the held advertisement once per frame; log on change.
static void RefreshOwnerView() {
    g.view = enemysync::CurrentTargetAuthority(R::Policy::AuthorityMaxAgeMs);
    uintptr_t player = 0;
    float timer = 0.0f;
    if (Canonical(player) && ReadHitTrace(player + OFF_INVULN, timer)) {
        const std::uint32_t revives = downedspike::g_down.reviveCount;  // read only; DownedSpike unchanged
        const bool reviveEvent = revives != g.reviveCount;
        // A revive's grace write is never "our" rise: no N2 episode is armed or continued across it.
        if (reviveEvent) g.episode = {};
        R::EpisodeTick(g.episode, timer, g_frameCounter);
        g.graceLatched = R::GraceLatch(g.graceLatched, reviveEvent, downedspike::g_down.graceActor != 0, timer);  // S10
        g.reviveCount = revives;
    } else {
        g.episode = {};
        g.graceLatched = false;
        g.reviveCount = downedspike::g_down.reviveCount;
    }
    // S7: continuous coverage of our slot for the allowlisted family; any lapse restarts it.
    const auto now = GetTickCount64();
    const bool covered = R::OwnerCancels(g.view.held, g.view.mode, g.view.slotMask, g.view.familyMask, g.view.localSlot, 302);
    if (!covered) g.coveredSinceMs = 0;
    else if (!g.coveredSinceMs) g.coveredSinceMs = now ? now : 1;
    const std::uint8_t mask = g.view.held ? g.view.slotMask : 0, mode = g.view.held ? g.view.mode : 0;
    if ((mask != g.loggedMask || mode != g.loggedMode) && Budget(g.authorityLogs, AUTHORITY_LOG_BUDGET))
        Log("[enemy-target] authority-rx frame=%u seq=%llu slotMask=%u mode=%u held=%u", g_frameCounter,
            static_cast<unsigned long long>(g.view.seq), unsigned(mask), unsigned(mode), unsigned(g.view.held));
    g.loggedMask = mask; g.loggedMode = mode;
}

// ---- puppet keeps (ApplyPuppetTransform), host only --------------------------------------------
// Only while at least one tracked enemy currently holds our write naming that clone (review S1):
// otherwise the clone is team 0 / no-collide as on main, so no non-allowlisted AI can hit it.
static bool KeepNative(uintptr_t actor) noexcept {
    if (!g.hostActive || !actor) return false;
    for (unsigned k = 1; k < R::kCandidates; ++k) {
        if (g.candidateActor[k] != actor || !g.candidateHandle[k]) continue;
        for (unsigned i = 0; i < R::Policy::TableCapacity; ++i)
            if (g.table[i].used && g.table[i].target == k && g.written[i] == g.candidateHandle[k]) { ++g.stats.keeps; return true; }
    }
    return false;
}
static bool KeepNativeTeam(uintptr_t actor) noexcept { return KeepNative(actor); }
static bool KeepNativeCollision(uintptr_t actor) noexcept { return KeepNative(actor); }

// ---- host: forward a zeroed clone hit (ApplyHitDamageBody, after TryZeroHp == Zeroed) -------------
static void ForwardHit(uintptr_t victim, const nativehittrace::ActorSnapshot& source, std::uint32_t attackId,
                       std::int32_t amount) {
    if (!g.hostActive || g.mode != static_cast<std::uint8_t>(R::Mode::Forward) || !victim || amount <= 0 ||
        !R::AllowedObject(source.objectId) || source.type != offsets::objentry::TYPE_MOB) return;
    unsigned k = 0;
    for (unsigned i = 1; i < R::kCandidates; ++i) if (g.candidateActor[i] == victim) k = i;
    if (!k) return;
    const std::uint16_t netId = enemysync::HostEnemyNetId(source.actor, source.objectId);
    if (!netId) { ++g.stats.forwardUnmatched; return; }
    std::uint64_t seq = 0;
    const bool sent = enemysync::SendRemoteHit(g.ownerSlot[k], netId, source.objectId, attackId, amount, seq);
    if (sent) ++g.stats.forwards; else ++g.stats.forwardFailed;
    if (Budget(g.forwardLogs, FORWARD_LOG_BUDGET))
        Log("[enemy-target] forward frame=%u owner=%u netId=%u objectId=%u attackId=%u damage=%d seq=%llu sent=%u",
            g_frameCounter, unsigned(g.ownerSlot[k]), unsigned(netId), source.objectId, attackId, amount,
            static_cast<unsigned long long>(seq), unsigned(sent));
}

// ---- owner: local veto fact for DamagePolicy (client only) --------------------------------------
// Only with a held forward-mode TargetAuthority naming our slot (review B2): every failure, stale
// or missing advertisement leaves the local hit native, so the owner is never immune.
static bool OwnerVetoesLocalFamily(std::uint32_t sourceObjectId, bool localVictim) noexcept {
    if (!g.requested || enemysync::ActivationRole() != 2 || !R::AllowedObject(sourceObjectId)) return false;  // 2 = Client
    const auto& v = g.view;
    const auto now = GetTickCount64();
    const bool fresh = v.held && v.rxMs != 0 && now >= v.rxMs && now - v.rxMs <= R::Policy::AuthorityMaxAgeMs;
    const bool ready = g.coveredSinceMs && now >= g.coveredSinceMs && R::CoverageReady(now - g.coveredSinceMs);  // S9
    if (ready && R::OwnerCancels(fresh, v.mode, v.slotMask, v.familyMask, v.localSlot, sourceObjectId)) return true;
    if (localVictim) ++g.stats.vetoNoAuthority;
    return false;
}
// After DamagePolicy zeroed a local hit for HostEnemyAuthority (ApplyHitDamageBody).
static void NoteVeto(uintptr_t attacker, std::uint32_t objectId, std::int32_t amount) noexcept {
    ++g.stats.vetoes;
    // Runs before g_origApplyHitDamage: this is the timer before the cancelled hit's own effects.
    uintptr_t player = 0;
    float before = 1.0f;
    if (Canonical(player) && !ReadHitTrace(player + OFF_INVULN, before)) before = 1.0f;
    R::EpisodeOnVeto(g.episode, before, g_frameCounter);
    if (Budget(g.vetoLogs, VETO_LOG_BUDGET))
        Log("[enemy-target] veto frame=%u attacker=%llX objectId=%u amount=%d", g_frameCounter, U(attacker), objectId, amount);
}

// ---- owner: apply an admitted RemoteHit to the local player --------------------------------------
// Through the hooked native stat funnel (HookedApplyStatDelta -> 0x3D2EB0), the same funnel the
// downed fixture's Kill uses: native death/downed handling follows a lethal value.
static int __cdecl OwnerApplyRemoteHit(std::int32_t damage, std::uint32_t attackId, std::uint32_t objectId,
                                       std::uint64_t seq) {
    ++g.stats.ownerSeen;
    if (!g.requested || !GameThread()) { ++g.stats.ownerRefused; return 0; }
    R::OwnerFacts f {};
    uintptr_t player = 0, status = 0;
    std::uint32_t f9b8 = 0;
    std::int32_t cutscene = 1;
    uintptr_t eventContext = 1;
    f.damage = damage;
    const std::uint64_t nowMs = GetTickCount64();
    f.coveredMs = g.coveredSinceMs && nowMs >= g.coveredSinceMs ? nowMs - g.coveredSinceMs : 0;
    f.canonical = Canonical(player);
    f.transition = warp::TransitionPending();
    f.inEvent = !(ReadHitTrace(g_exeBase + offsets::CUTSCENE_STATE, cutscene) && cutscene == 0 &&
                  ReadHitTrace(g_exeBase + offsets::EVENT_CONTEXT, eventContext) && eventContext == 0);
    if (f.canonical) {
        f.dead = !ReadHitTrace(player + OFF_FLAGS9B8, f9b8) || (f9b8 & 4u) != 0;
        if (!ReadHitTrace(player + OFF_STATUS, status) || !status || !ReadHitTrace(status, f.hp)) f.hp = 0;
        if (!ReadHitTrace(player + OFF_INVULN, f.invulnFrames)) f.invulnFrames = 1.0f;
        // S3/N2: only the i-frame episode our own cancel started (timer was 0 before it, rose within
        // VetoRiseFrames, has only counted down since), and never during DownedSpike's revive grace.
        f.invulnFromVeto = R::EpisodeOverrides(g.episode, g.graceLatched);  // S10: latch on revive-synchronous facts
    }
    const auto refusal = R::OwnerApply(f);
    int result = 0;
    if (refusal == R::ApplyRefusal::None) {
        __try { result = HookedApplyStatDelta(reinterpret_cast<void*>(player), -damage, 0, 0); }
        __except (EXCEPTION_EXECUTE_HANDLER) { result = -1; }
    }
    const bool applied = refusal == R::ApplyRefusal::None && result != -1;
    const bool invulnOverrode = applied && f.invulnFrames > 0.0f && f.invulnFromVeto;
    if (applied) ++g.stats.ownerApplied; else ++g.stats.ownerRefused;
    if (refusal == R::ApplyRefusal::Invulnerable) ++g.stats.refusedInvulnerable;
    if (refusal == R::ApplyRefusal::Stale) ++g.stats.refusedStale;
    if (invulnOverrode) ++g.stats.invulnOverride;
    if (Budget(g.ownerLogs, OWNER_LOG_BUDGET))
        Log("[enemy-target] remote-hit seq=%llu objectId=%u attackId=%u damage=%d hpBefore=%d refusal=%u native=%d invulnOverride=%u",
            static_cast<unsigned long long>(seq), objectId, attackId, damage, f.hp, unsigned(refusal), result, unsigned(invulnOverrode));
    return applied ? 1 : 0;
}

static void Install() {
    Configure();
    if (g.requested) enemysync::SetRemoteHitApply(&OwnerApplyRemoteHit);
}

static void Shutdown() {
    if (!g.requested) return;
    // Our handles may stay in live enemies' slots: once the clone handle stops resolving, 0x4319D0
    // re-selects the player for that enemy. No actor write happens off the game thread.
    RestoreSearch();  // no actor writes from the shutdown thread
}

} // namespace enemytarget
