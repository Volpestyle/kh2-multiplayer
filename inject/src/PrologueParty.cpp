// PrologueParty — see PrologueParty.hpp (VUH-2464).
#include "PrologueParty.hpp"
#include "NativePrivateStatus.hpp"
#include "PlayerKit.hpp"
#include "Warp.hpp"
#include "kh2coop/EventScenes.hpp"
#include "kh2coop/KH2Offsets.hpp"
#include "kh2coop/PrologueSeats.hpp"
#include "kh2coop/RouteSpawnPolicy.hpp"

#include "MinHook.h"

#include <Windows.h>
#include <atomic>
#include <cstdlib>
#include <cstring>

namespace kh2coop::inject::prologueparty {
namespace {
using prologue::Hold;
std::uintptr_t g_base = 0;
LogFn g_log = nullptr;
bool g_requested = false;
std::uint16_t g_localProfile = 0;   // 0: not declared (an unparsable KH2COOP_PROFILE)
prologue::Seats g_seats {};
// This seat's last report and its echo.
SeatState g_sent {};
bool g_sentAny = false, g_echoed = false;
std::uint64_t g_sentMs = 0;
unsigned g_resends = 0;
std::uint32_t g_seq = 0;
// The construction of the current load at a prologue site.
struct Construction {
    std::array<std::uint8_t, 10> location {};
    std::uint32_t load = 0;
    std::uintptr_t clone = 0;
    bool bound = false;
    const char* reason = "none";
} g_construction {};
// Published hold for the input collector: reason (0 released) and the 10-byte NOW it applies to.
std::atomic<std::uint8_t> g_hold {0};
std::atomic<std::uint64_t> g_holdNowLow {0};
std::atomic<std::uint16_t> g_holdNowHigh {0};
Hold g_loggedHold = Hold::None;
std::array<std::uint8_t, 10> g_loggedAt {};
std::array<std::uint8_t, 10> g_unpinnedAt {};
bool g_released = false;   // this seat has been released at a site once
prologue::UnpinnedStart g_unpinnedStart;  // VUH-2486
std::atomic<std::uint8_t> g_notice {0};
// VUH-1512: the NOW of the load whose member 0 ResolveLocal replaced (all 0xFF: none).
std::atomic<std::uint64_t> g_bodyAtLow {~0ull};
std::atomic<std::uint16_t> g_bodyAtHigh {0xFFFF};
std::uint16_t g_refusalLogged = 0;
std::atomic<std::uint16_t> g_refused {0};
// VUH-1512: the area loader's object registration (FUN_1403E0460(object, priority, kind)) and its file
// queue (FUN_1403E0660(object, priority, kind, childKind)), build 9002b2de. The loader registers the player
// placeholder 566 from member 0 and the friend seats 567/568 through the party row, which is 00/12/12/12
// in the prologue, so only the local body's files are loaded. A clone of another body then finds no
// files: its construction (FUN_1403E6120 -> FUN_14039CC40) writes through a null file entry. Right after
// 566 is registered at a checked prologue site, the other seat's body is queued with the friend kind (5)
// the loader uses for seat 567. Calling FUN_1403E0660 directly keeps FUN_1403E0460's player-override
// global (set for a type-0 object) untouched.
constexpr std::uintptr_t RVA_REGISTER_OBJECT = 0x3E0460, RVA_QUEUE_OBJECT = 0x3E0660;
constexpr std::uint8_t kRegisterBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48,
                                           0x83, 0xEC, 0x20, 0x8B, 0xC1, 0x41, 0x8B, 0xF8, 0x8B, 0xF2, 0x8B, 0xD9};
constexpr std::uint8_t kQueueBytes[] = {0x40, 0x53, 0x55, 0x57, 0x41, 0x55, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x30};
constexpr std::uint32_t PLAYER_PLACEHOLDER = 0x236;
constexpr int FRIEND_KIND = 5;
using RegisterFn = void(__fastcall*)(std::uint64_t, std::uint64_t, int);
using QueueFn = void(__fastcall*)(std::uint64_t, int, int, int);
RegisterFn g_register = nullptr;
QueueFn g_queue = nullptr;
constexpr std::uint64_t ECHO_RETRY_MS = 2000;
constexpr unsigned MAX_RESENDS = 15;

template <class T> bool Read(std::uintptr_t rva, T& out) {
    __try { out = *reinterpret_cast<const T*>(g_base + rva); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool ReadNow(std::array<std::uint8_t, 10>& now) {
    __try { std::memcpy(now.data(), reinterpret_cast<const void*>(g_base + offsets::NOW), now.size()); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
prologue::Location LocationOf(const std::array<std::uint8_t, 10>& now) {
    return {now[0], now[1], static_cast<std::uint16_t>(now[8] | (now[9] << 8))};
}
const routespawn::MissingPartySite* PrologueSite(const std::array<std::uint8_t, 10>& now) {
    const auto* site = routespawn::MissingPartySiteAt(now);
    return site && site->prologue ? site : nullptr;
}
// The live field gate: in field, no menu, cutscene or event (the VUH-2338 fixture's FIELD).
bool FieldOpen() {
    std::uint8_t inField = 0, menu = 0; std::int32_t cutscene = -1; std::uint32_t context = 1, context2 = 1;
    return Read(offsets::IN_FIELD, inField) && inField && Read(offsets::OPEN_MENU, menu) && menu == 0xFF &&
        Read(offsets::CUTSCENE_STATE, cutscene) && !cutscene && Read(offsets::EVENT_CONTEXT, context) && !context &&
        Read(offsets::EVENT_CONTEXT + 4, context2) && !context2;
}
// This load's construction still stands: bound at this exact NOW, and the clone is still the
// current private missing-party actor (same stamp/load, canonical local, owned private status).
bool LocalBoundHere(const std::array<std::uint8_t, 10>& now) {
    return g_construction.bound && g_construction.location == now && g_construction.clone &&
        warp::LoadSerial() == g_construction.load + 1 && !warp::TransitionPending() &&
        privatestatus::MissingPartyPuppetAllowed(g_construction.clone);
}
std::array<std::uint16_t, 3> Latched() {
    std::array<std::uint16_t, 3> v {};
    if (!prologue::Vector(g_seats, v)) v = {};
    return v;
}
void Publish(Hold hold, const std::array<std::uint8_t, 10>& now) {
    std::uint64_t low = 0; std::uint16_t high = 0;
    std::memcpy(&low, now.data(), 8); std::memcpy(&high, now.data() + 8, 2);
    g_holdNowLow.store(low, std::memory_order_relaxed);
    g_holdNowHigh.store(high, std::memory_order_relaxed);
    g_hold.store(static_cast<std::uint8_t>(hold), std::memory_order_release);
}
// The other seat's body when it differs from this seat's own, at a checked prologue site (loading thread).
bool OtherBody(std::uint16_t& other, std::array<std::uint8_t, 10>& now);
bool Queue(std::uint16_t object) {
    __try { g_queue(object, -1, FRIEND_KIND, -1); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void __fastcall HookedRegister(std::uint64_t object, std::uint64_t priority, int kind) {
    g_register(object, priority, kind);
    if (static_cast<std::uint32_t>(object) != PLAYER_PLACEHOLDER) return;
    std::uint16_t other = 0;
    std::array<std::uint8_t, 10> now {};
    if (!OtherBody(other, now)) return;
    const bool queued = Queue(other);
    if (g_log) g_log("[prologue] other body 0x%X files %s at=%02X/%02X evt=%u (kind %d)", other,
                     queued ? "queued" : "FAULTED", now[0], now[1], static_cast<unsigned>(now[8] | (now[9] << 8)), FRIEND_KIND);
}
bool ParseProfile(std::uint16_t& out) {
    char text[16] {};
    const DWORD n = GetEnvironmentVariableA("KH2COOP_PROFILE", text, sizeof(text));
    if (n == 0) { out = prologue::DEFAULT_PROFILE; return true; }
    if (n >= sizeof(text)) return false;
    char* end = nullptr;
    const unsigned long v = std::strtoul(text, &end, 0);
    if (!end || *end || !v || v > 0xFFFF) return false;
    out = static_cast<std::uint16_t>(v);
    return true;
}
} // namespace

void Install(std::uintptr_t exeBase, LogFn log) {
    g_base = exeBase; g_log = log;
    char off[2] {};
    const bool optOut = GetEnvironmentVariableA("KH2COOP_PROLOGUE_PARTY", off, sizeof(off)) == 1 && off[0] == '0';
    // The clone needs the shipped two-player private-status dependency (TARGET_ALL + MIRROR).
    g_requested = !optOut && playerkit::BcCloneRequested();
    if (!g_requested) {
        if (log) log("[prologue] off (%s)", optOut ? "KH2COOP_PROLOGUE_PARTY=0" : "shipped TARGET_ALL/MIRROR dependency off");
        return;
    }
    if (!ParseProfile(g_localProfile)) g_localProfile = 0;
    // VUH-1512: the file preload for a clone whose body differs from the local one (Roxas + Sora).
    const auto* reg = reinterpret_cast<const void*>(exeBase + RVA_REGISTER_OBJECT);
    const auto* queue = reinterpret_cast<const void*>(exeBase + RVA_QUEUE_OBJECT);
    if (std::memcmp(reg, kRegisterBytes, sizeof(kRegisterBytes)) || std::memcmp(queue, kQueueBytes, sizeof(kQueueBytes))) {
        if (log) log("[prologue] REFUSED: object registration bytes differ from 9002b2de; prologue seats off");
        g_requested = false;
        return;
    }
    g_queue = reinterpret_cast<QueueFn>(exeBase + RVA_QUEUE_OBJECT);
    MH_STATUS st = MH_CreateHook(const_cast<void*>(reg), reinterpret_cast<void*>(&HookedRegister), reinterpret_cast<void**>(&g_register));
    if (st == MH_OK) st = MH_EnableHook(const_cast<void*>(reg));
    if (st != MH_OK) {
        if (log) log("[prologue] REFUSED: object registration hook failed %d (%s); prologue seats off", st, MH_StatusToString(st));
        g_requested = false;
        return;
    }
    const char* refusal = g_localProfile ? prologue::ProfileRefusal(g_localProfile) : nullptr;
    if (log) log("[prologue] seats on: profile=0x%X%s%s; two-player layout (Roxas/Sora) at %u checked site(s)", g_localProfile,
                 g_localProfile ? (refusal ? " refused: " : "") : " (KH2COOP_PROFILE unreadable: undeclared)",
                 refusal ? refusal : "", static_cast<unsigned>(std::size(routespawn::MissingPartySites) - 1));
}

bool Tick(std::uint32_t generation, const std::array<std::uint64_t, 3>& roster, std::uint8_t localSlot,
          std::uint64_t nowMs, SeatState& out) {
    if (!g_requested) return false;
    if (prologue::Rebase(g_seats, generation, roster, localSlot)) {
        g_sentAny = false; g_echoed = false; g_resends = 0;
        if (g_log && generation)
            g_log("[prologue] roster generation=%u slot=%u connections=%llu/%llu/%llu: profiles forgotten, declaring 0x%X",
                  generation, localSlot, static_cast<unsigned long long>(roster[0]), static_cast<unsigned long long>(roster[1]),
                  static_cast<unsigned long long>(roster[2]), g_localProfile);
    }
    std::array<std::uint8_t, 10> now {};
    const bool nowRead = ReadNow(now);
    // The hold for this frame (published for the input collector).
    Hold hold = Hold::None;
    const auto* site = nowRead ? PrologueSite(now) : nullptr;
    const bool fieldOpen = nowRead && FieldOpen();
    const bool decided = generation && site && fieldOpen;
    // An open prologue field with no checked site (the last frame of an event program or opening scene
    // reads as one while its load is already pending, which is not playable). Before this seat's first
    // release it is held too, so that transient is never played solo. The friend's blocked opening exit
    // is not one: it has no player actor and member 0 is still Sora. VUH-2486: a session that starts at
    // such a field (a mid-prologue save) has no layout to bind there, so the hold is bounded
    // (prologue::UnpinnedStart) and then this seat plays native solo, with a note on screen.
    // VUH-2487: a checked event-only scene location (EventScenes.hpp) reads as an open field for a frame
    // before its event starts; a friend that follows the host's scene loads it too. It is never a field.
    const bool scene = nowRead && eventscenes::Qualified(now[0], now[1], now[2], static_cast<std::uint16_t>(now[4] | (now[5] << 8)),
                                                         static_cast<std::uint16_t>(now[6] | (now[7] << 8)),
                                                         LocationOf(now).evt);
    const bool unpinned = generation && !site && !scene && fieldOpen && !warp::TransitionPending() && PrologueScope();
    const auto start = g_unpinnedStart.Tick(unpinned && !g_released, now, nowMs);
    const bool early = start == prologue::UnpinnedStart::Step::Hold;
    if (start == prologue::UnpinnedStart::Step::Released && g_log)
        g_log("[prologue] unpinned start at=%02X/%02X evt=%u: no checked two-player site here; released after %llu ms, "
              "native solo until a checked site", now[0], now[1], LocationOf(now).evt,
              static_cast<unsigned long long>(prologue::UnpinnedGraceMs));
    if (start == prologue::UnpinnedStart::Step::Released) g_loggedHold = Hold::None;  // released, not left
    g_notice.store(static_cast<std::uint8_t>(early ? UnpinnedNotice::Holding
                                             : g_unpinnedStart.NoticeVisible(nowMs) ? UnpinnedNotice::PlayingSolo
                                                                                     : UnpinnedNotice::None),
                   std::memory_order_release);
    if (decided) hold = prologue::Release(g_seats, LocationOf(now), LocalBoundHere(now));
    else if (early) hold = Hold::Unpinned;
    g_refused.store(decided && hold == Hold::UnqualifiedProfile ? prologue::RefusedProfile(g_seats) : 0,
                    std::memory_order_release);
    Publish(hold, now);
    if (decided && hold == Hold::None) g_released = true;
    // One line per decision at each prologue field location (a seat that arrives after its peer bound
    // is released at its first field frame), and one when a hold ends by leaving the field.
    if (g_log && (decided || early) && (hold != g_loggedHold || now != g_loggedAt)) {
        g_log("[prologue] %s at=%02X/%02X evt=%u site=%s localBound=%u", hold == Hold::None ? "control released" : "hold",
              now[0], now[1], LocationOf(now).evt, site ? site->name : "none", LocalBoundHere(now) ? 1u : 0u);
        if (hold != Hold::None) g_log("[prologue] hold reason=%s", prologue::HoldName(hold));
        g_loggedHold = hold; g_loggedAt = now;
    } else if (g_log && !decided && !early && g_loggedHold != Hold::None) {
        g_log("[prologue] hold ended without release: left the prologue field at=%02X/%02X evt=%u", now[0], now[1], LocationOf(now).evt);
        g_loggedHold = Hold::None;
    }
    // After the first release an unpinned field plays native solo and unheld; only this line marks one
    // (leaving a site for an event program also ends a hold).
    if (decided) {
        g_unpinnedAt = {};
    } else if (g_log && unpinned && !early && now != g_unpinnedAt) {
        g_log("[prologue] unpinned field at=%02X/%02X evt=%u: native solo, unheld", now[0], now[1], LocationOf(now).evt);
        g_unpinnedAt = now;
    }
    if (!generation || localSlot > 2 || !g_localProfile) return false;
    // This seat's report: acknowledged once the roster's profile vector is complete; bound at a
    // prologue site whose construction still stands.
    SeatState m {};
    m.roster = roster; m.profile = g_localProfile; m.latched = Latched();
    m.phase = m.latched == std::array<std::uint16_t, 3>{} ? SeatPhase::Declared : SeatPhase::Acknowledged;
    if (m.phase == SeatPhase::Acknowledged && site && LocalBoundHere(now)) {
        const auto at = LocationOf(now);
        m.phase = SeatPhase::Bound; m.worldId = at.world; m.roomId = at.room; m.eventProgram = at.evt;
    }
    const bool changed = !g_sentAny || m.roster != g_sent.roster || m.profile != g_sent.profile || m.latched != g_sent.latched ||
        m.phase != g_sent.phase || m.worldId != g_sent.worldId || m.roomId != g_sent.roomId || m.eventProgram != g_sent.eventProgram;
    const bool retry = !changed && !g_echoed && nowMs - g_sentMs >= ECHO_RETRY_MS && g_resends < MAX_RESENDS;
    if (!changed && !retry) return false;
    if (changed) g_resends = 0; else ++g_resends;
    m.seq = ++g_seq;
    out = m;
    return true;
}

void NoteSent(const SeatState& sent, std::uint64_t nowMs) {
    g_sent = sent; g_sentAny = true; g_echoed = false; g_sentMs = nowMs;
    if (g_log) g_log("[prologue] seat sent seq=%u phase=%u profile=0x%X latched=%X/%X/%X at=%u/%u/%u resend=%u",
                     sent.seq, static_cast<unsigned>(sent.phase), sent.profile, sent.latched[0], sent.latched[1], sent.latched[2],
                     sent.worldId, sent.roomId, sent.eventProgram, g_resends);
}

void Receive(const SeatState& m) {
    if (!g_requested) return;
    const bool accepted = prologue::Accept(g_seats, m);
    if (accepted && m.ownerSlot == g_seats.localSlot && g_sentAny && m.seq == g_sent.seq) g_echoed = true;
    if (g_log) g_log("[prologue] seat %s owner=%u seq=%u phase=%u profile=0x%X latched=%X/%X/%X at=%u/%u/%u agreement=%s",
                     accepted ? "accepted" : "ignored", m.ownerSlot, m.seq, static_cast<unsigned>(m.phase), m.profile,
                     m.latched[0], m.latched[1], m.latched[2], m.worldId, m.roomId, m.eventProgram,
                     prologue::HoldName(prologue::Agreement(g_seats)));
    // VUH-1512: say once per profile why the layout cannot use it; that seat is never given another body.
    if (const char* refusal = accepted ? prologue::ProfileRefusal(m.profile) : nullptr; refusal && g_refusalLogged != m.profile) {
        g_refusalLogged = m.profile;
        if (g_log) g_log("[prologue] seat %u profile 0x%X refused: %s; both seats hold", m.ownerSlot, m.profile, refusal);
    }
}

bool CloneProfile(std::uint16_t& object, const char*& reason) {
    object = 0;
    if (!g_requested) { reason = "prologue-off"; return false; }
    Hold hold = Hold::None;
    if (!prologue::RemoteProfile(g_seats, object, hold)) { reason = prologue::HoldName(hold); return false; }
    reason = "agreed";
    return true;
}

bool LocalBody(std::uint16_t& object, std::uint16_t& statusKey) {
    object = 0; statusKey = 0;
    Hold hold = Hold::None;
    if (!g_requested || !prologue::LocalProfile(g_seats, object, hold)) { object = 0; return false; }
    const int key = kitStatusKey(object);
    if (key <= 0) { object = 0; return false; }
    statusKey = static_cast<std::uint16_t>(key);
    return true;
}

namespace {
bool OtherBody(std::uint16_t& other, std::array<std::uint8_t, 10>& now) {
    other = 0;
    if (!g_requested || !ReadNow(now) || !PrologueSite(now)) return false;
    std::uint16_t local = 0, key = 0, remote = 0;
    const char* reason = nullptr;
    if (!LocalBody(local, key) || !CloneProfile(remote, reason) || remote == local) return false;
    other = remote;
    return true;
}
} // namespace

void ResolveLocal(std::uintptr_t resolvedMembers) {
    if (!g_requested || !resolvedMembers) return;
    std::array<std::uint8_t, 10> now {};
    if (!ReadNow(now) || !PrologueSite(now)) return;
    std::uint16_t body = 0, key = 0;
    if (!LocalBody(body, key) || body == prologue::DEFAULT_PROFILE) return;
    bool replaced = false;
    __try {
        auto* member0 = reinterpret_cast<std::uint16_t*>(resolvedMembers);
        if (*member0 == prologue::DEFAULT_PROFILE) { *member0 = body; replaced = true; }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return; }
    if (!replaced) return;
    std::uint64_t low = 0; std::uint16_t high = 0;
    std::memcpy(&low, now.data(), 8); std::memcpy(&high, now.data() + 8, 2);
    g_bodyAtHigh.store(high, std::memory_order_relaxed);
    g_bodyAtLow.store(low, std::memory_order_release);
    if (g_log) g_log("[prologue] local body at=%02X/%02X evt=%u: member 0 Roxas -> chosen 0x%X (status key %u)",
                     now[0], now[1], LocationOf(now).evt, body, key);
}

void NoteConstruction(const std::array<std::uint8_t, 10>& location, bool bound, const char* reason, std::uintptr_t clone) {
    g_construction = {location, warp::LoadSerial(), clone, bound, reason};
    if (g_log) g_log("[prologue] construction at=%02X/%02X evt=%u load=%u bound=%u clone=%llX reason=%s",
                     location[0], location[1], static_cast<unsigned>(location[8] | (location[9] << 8)), g_construction.load,
                     bound ? 1u : 0u, static_cast<unsigned long long>(clone), reason);
}

bool HoldInput(void* inputStruct) {
    const auto hold = g_hold.load(std::memory_order_acquire);
    if (!hold || !inputStruct) return false;
    std::array<std::uint8_t, 10> now {};
    std::uint64_t low = g_holdNowLow.load(std::memory_order_relaxed);
    std::uint16_t high = g_holdNowHigh.load(std::memory_order_relaxed);
    if (!ReadNow(now) || std::memcmp(now.data(), &low, 8) || std::memcmp(now.data() + 8, &high, 2)) return false;
    __try {
        if (*reinterpret_cast<std::uintptr_t*>(g_base + offsets::INPUT_STRUCT_PTR) != reinterpret_cast<std::uintptr_t>(inputStruct)) return false;
        auto* raw = reinterpret_cast<std::uint8_t*>(inputStruct) + offsets::input::RAW_SLOT0;
        *reinterpret_cast<std::uint16_t*>(raw + offsets::input::BUTTONS) = 0;
        raw[offsets::input::LSTICK_X] = raw[offsets::input::LSTICK_Y] = 128;
        raw[offsets::input::RSTICK_X] = raw[offsets::input::RSTICK_Y] = 128;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

std::uint8_t PublishedHold() { return g_hold.load(std::memory_order_acquire); }
UnpinnedNotice Notice() { return static_cast<UnpinnedNotice>(g_notice.load(std::memory_order_acquire)); }
std::uint16_t RefusedProfile() { return g_refused.load(std::memory_order_acquire); }

bool PrologueScope() {
    std::uint8_t world = 0xFF; std::uint16_t member0 = 0;
    if (!g_requested || !g_base || !Read(offsets::WORLD_ID, world) || world != 2 ||
        !Read(playerkit::RVA_RESOLVED_MEMBERS, member0)) return false;
    if (member0 == prologue::DEFAULT_PROFILE) return true;
    // VUH-1512: this seat's chosen body, set by ResolveLocal for the load at this exact NOW.
    std::array<std::uint8_t, 10> now {};
    std::uint64_t low = 0; std::uint16_t high = 0;
    if (member0 != g_localProfile || !ReadNow(now)) return false;
    std::memcpy(&low, now.data(), 8); std::memcpy(&high, now.data() + 8, 2);
    return g_bodyAtLow.load(std::memory_order_acquire) == low && g_bodyAtHigh.load(std::memory_order_relaxed) == high;
}

} // namespace kh2coop::inject::prologueparty
