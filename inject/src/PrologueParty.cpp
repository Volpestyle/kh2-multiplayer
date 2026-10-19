// PrologueParty — see PrologueParty.hpp (VUH-2464).
#include "PrologueParty.hpp"
#include "NativePrivateStatus.hpp"
#include "PlayerKit.hpp"
#include "Warp.hpp"
#include "kh2coop/KH2Offsets.hpp"
#include "kh2coop/PrologueSeats.hpp"
#include "kh2coop/RouteSpawnPolicy.hpp"

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
    if (log) log("[prologue] seats on: profile=0x%X%s; Roxas+Roxas layout at %u checked site(s)", g_localProfile,
                 g_localProfile ? (prologue::ProfileQualified(g_localProfile) ? "" : " (not prologue-qualified)") : " (KH2COOP_PROFILE unreadable: undeclared)",
                 static_cast<unsigned>(std::size(routespawn::MissingPartySites) - 1));
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
    const bool decided = generation && site && FieldOpen();
    if (decided) hold = prologue::Release(g_seats, LocationOf(now), LocalBoundHere(now));
    Publish(hold, now);
    // One line per decision at each prologue field location (a seat that arrives after its peer bound
    // is released at its first field frame), and one when a hold ends by leaving the field.
    if (g_log && decided && (hold != g_loggedHold || now != g_loggedAt)) {
        g_log("[prologue] %s at=%02X/%02X evt=%u site=%s localBound=%u", hold == Hold::None ? "control released" : "hold",
              now[0], now[1], LocationOf(now).evt, site->name, LocalBoundHere(now) ? 1u : 0u);
        if (hold != Hold::None) g_log("[prologue] hold reason=%s", prologue::HoldName(hold));
        g_loggedHold = hold; g_loggedAt = now;
    } else if (g_log && !decided && g_loggedHold != Hold::None) {
        g_log("[prologue] hold ended without release: left the prologue field at=%02X/%02X evt=%u", now[0], now[1], LocationOf(now).evt);
        g_loggedHold = Hold::None;
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
}

bool CloneProfile(std::uint16_t& object, const char*& reason) {
    object = 0;
    if (!g_requested) { reason = "prologue-off"; return false; }
    Hold hold = Hold::None;
    if (!prologue::RemoteProfile(g_seats, object, hold)) { reason = prologue::HoldName(hold); return false; }
    reason = "agreed";
    return true;
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

bool PrologueScope() {
    std::uint8_t world = 0xFF; std::uint16_t member0 = 0;
    return g_requested && g_base && Read(offsets::WORLD_ID, world) && world == 2 &&
        Read(playerkit::RVA_RESOLVED_MEMBERS, member0) && member0 == prologue::DEFAULT_PROFILE;
}

} // namespace kh2coop::inject::prologueparty
