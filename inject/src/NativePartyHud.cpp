#include "NativePartyHud.hpp"
#include "NativePartyHudState.hpp"
#include "EnemySync.hpp"
#include "Warp.hpp"
#include "kh2coop/KH2Offsets.hpp"
#include <Windows.h>
#include <MinHook.h>
#include <intrin.h>
#include <algorithm>
#include <atomic>
#include <cstring>

namespace kh2coop::inject::nativehud {
namespace {
using LookupFn = std::uintptr_t(__fastcall*)(int);
using GaugeFn = void(__fastcall*)(std::uintptr_t);
LookupFn g_lookup{};
GaugeFn g_tick{}, g_bind{}, g_visibility{};
std::uintptr_t g_base{};
LogFn g_log{};
std::atomic<bool> g_installed{}, g_retained{}, g_retiring{};
DWORD g_thread{};
State g_state;
SeatVitals g_local{};
std::uint64_t g_localMs{}, g_observedMs{};
std::uint32_t g_load{};
std::uint16_t g_world{}, g_room{};
std::uintptr_t g_localActor{};
std::array<std::uintptr_t, 3> g_bodies{};
std::array<std::uintptr_t, 8> g_humanBodies{}; // excluded from NPC fallback until the native load changes
// The native HUD only reads these three actor fields: status, face resource package and downed
// flag. These are DISPLAY views, never registered with any gameplay/native actor system.
struct View {
    alignas(16) std::array<std::uint8_t, 0x9C0> actor{};
    alignas(16) std::array<std::uint8_t, 0x278> status{};
    std::uintptr_t face{};
    std::uint8_t owner{0xFF};
};
std::array<View, 2> g_views;
struct Tint {
    std::uintptr_t gauge{};
    std::uint32_t flags{}, transformFlags{};
    float rgb[3]{}, scale[3]{};
    bool owned{};
};
std::array<Tint, 2> g_tints;
template<class T> void Put(std::uint8_t* p, std::size_t offset, const T& value) { std::memcpy(p + offset, &value, sizeof(value)); }
bool Readable(std::uintptr_t p, std::size_t n) {
    MEMORY_BASIC_INFORMATION m{};
    if (!p || !n || p + n < p || !VirtualQuery(reinterpret_cast<void*>(p), &m, sizeof(m)) || m.State != MEM_COMMIT ||
        (m.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    const auto end = reinterpret_cast<std::uintptr_t>(m.BaseAddress) + m.RegionSize;
    return p + n <= end && (m.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
        PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
}
template<class T> bool Read(std::uintptr_t p, T& value) {
    if (!Readable(p, sizeof(value))) return false;
    std::memcpy(&value, reinterpret_cast<const void*>(p), sizeof(value)); return true;
}
bool Current() {
    const auto now = GetTickCount64();
    if (g_retiring || !g_installed || GetCurrentThreadId() != g_thread || now < g_observedMs || now - g_observedMs > FreshMs ||
        warp::LoadSerial() != g_load || warp::TransitionPending()) return false;
    const auto a = enemysync::CapturePuppetAuthority();
    std::uint8_t world{}, room{};
    std::int32_t cutscene{}; std::uintptr_t event{};
    return a.mode == PuppetAuthorityMode::Network && a.generation && a.generation == g_state.generation &&
        a.localSlot == g_state.localSlot && a.connectionIds == g_state.roster &&
        Read(g_base + offsets::WORLD_ID, world) && Read(g_base + offsets::ROOM_ID, room) && world == g_world && room == g_room &&
        Read(g_base + offsets::CUTSCENE_STATE, cutscene) && !cutscene && Read(g_base + offsets::EVENT_CONTEXT, event) && !event;
}
std::uintptr_t FaceFor(std::uintptr_t body, std::uint16_t expected) {
    std::uintptr_t obj{}, face{}; std::uint16_t object{};
    return body && Read(body + offsets::actor::OBJENTRY_PTR, obj) && Read(obj, object) && object == expected &&
        Read(body + 0x928, face) && Readable(face, 16) ? face : 0;
}
std::uintptr_t Project(int seat) {
    const auto owners = g_state.Owners();
    const auto index = static_cast<std::size_t>(seat - 1);
    const auto owner = owners[index];
    if (owner < 3) {
        auto& v = g_views[index];
        const auto* stats = g_state.Current(owner, g_world, g_room, GetTickCount64());
        if (!stats) return 0;
        auto face = FaceFor(g_bodies[owner], stats->objectId);
        // The legacy three-seat puppet can use a companion stand-in. Duplicate player profiles
        // still share the correct loaded face package with the canonical local body.
        if (!face) face = FaceFor(g_localActor, stats->objectId);
        if (!face) return 0;
        Put(v.actor.data(), 0x5C0, reinterpret_cast<std::uintptr_t>(v.status.data()));
        Put(v.actor.data(), 0x928, face);
        const std::uint32_t flags = (stats->flags & SeatVitals::Downed) ? 4u : 0u;
        Put(v.actor.data(), 0x9B8, flags);
        Put(v.status.data(), 0, stats->hp); Put(v.status.data(), 4, stats->maxHp);
        const auto mp = stats->maxMp ? stats->mp : 1;
        Put(v.status.data(), 0x180, mp);
        // Profiles without MP have a full, inert native MP track (avoid native 0/0).
        const auto maxMp = (std::max)(1, stats->maxMp);
        Put(v.status.data(), 0x184, maxMp);
        v.face = face; v.owner = owner;
        return reinterpret_cast<std::uintptr_t>(v.actor.data());
    }
    // Humans have priority; keep the first distinct real NPC in the remaining slot. A player-class
    // clone in the original HUD table must not appear a second time as an NPC.
    const auto humanCount = static_cast<std::size_t>((owners[0] < 3) + (owners[1] < 3));
    const auto npcIndex = index - humanCount;
    std::array<std::uintptr_t, 2> candidates{};
    std::size_t count = 0;
    for (int i = 1; i <= 2; ++i) {
        const auto actor = g_lookup(i);
        std::uintptr_t obj{}; std::uint8_t type{};
        if (!actor || std::find(g_humanBodies.begin(), g_humanBodies.end(), actor) != g_humanBodies.end() ||
            (count && candidates[0] == actor) || !Read(actor + offsets::actor::OBJENTRY_PTR, obj) ||
            !Read(obj + offsets::objentry::TYPE_FLAGS, type) || type != 1) continue;
        candidates[count++] = actor;
    }
    return npcIndex < count ? candidates[npcIndex] : 0;
}
bool HudCaller(std::uintptr_t caller) {
    const auto rva = caller - g_base;
    return (rva >= 0x16C3A0 && rva < 0x16C610) || (rva >= 0x182400 && rva < 0x182494) ||
        (rva >= 0x182610 && rva < 0x182724) || (rva >= 0x182730 && rva < 0x1829A7);
}
std::uintptr_t __fastcall Lookup(int seat) {
    if (seat >= 1 && seat <= 2 && HudCaller(reinterpret_cast<std::uintptr_t>(_ReturnAddress())) && Current()) return Project(seat);
    return g_lookup(seat);
}
void TintFace(std::uintptr_t gauge, std::size_t index, bool owned) {
    auto& t = g_tints[index];
    const auto face = gauge + 0x50;
    if (t.gauge != gauge) t = {};  // old object's memory belongs to the game after a load
    if (owned && !t.owned) {
        t.gauge = gauge;
        std::memcpy(&t.flags, reinterpret_cast<void*>(face + 0x10), 4);
        std::memcpy(t.rgb, reinterpret_cast<void*>(face + 0x17C), 12);
        std::memcpy(&t.transformFlags, reinterpret_cast<void*>(face + 0x108), 4);
        std::memcpy(t.scale, reinterpret_cast<void*>(face + 0x198), 12);
        t.owned = true;
    }
    if (owned) {
        // dk::Sprite draw (16ABF0): flags 9+10 inherit parent alpha and retain these RGB factors.
        constexpr float colors[3][3] {{1.f, .78f, .38f}, {.42f, .65f, 1.f}, {.45f, 1.f, .55f}};
        const auto flags = t.flags | 0x600u;
        std::memcpy(reinterpret_cast<void*>(face + 0x10), &flags, 4);
        std::memcpy(reinterpret_cast<void*>(face + 0x17C), colors[g_views[index].owner], 12);
        // Player face assets are authored larger than companion faces. Native scale setters
        // 193310/193370/1933D0 use RectParam+178/17C/180 and flag +E8 bit 1; Sprite embeds it +20.
        const auto object = g_state.samples[g_views[index].owner].seat.vitals.objectId;
        const float fit = object == 0x54 || object == 0x5A ? .65f : 1.f;
        const float scale[3] {t.scale[0], t.scale[1] * fit, t.scale[2] * fit};
        const auto transform = fit == 1.f ? t.transformFlags : t.transformFlags | 2u;
        std::memcpy(reinterpret_cast<void*>(face + 0x198), scale, 12);
        std::memcpy(reinterpret_cast<void*>(face + 0x108), &transform, 4);
    } else if (t.owned) {
        std::memcpy(reinterpret_cast<void*>(face + 0x10), &t.flags, 4);
        std::memcpy(reinterpret_cast<void*>(face + 0x17C), t.rgb, 12);
        std::memcpy(reinterpret_cast<void*>(face + 0x198), t.scale, 12);
        std::memcpy(reinterpret_cast<void*>(face + 0x108), &t.transformFlags, 4);
        t.owned = false;
    }
}
void __fastcall Tick(std::uintptr_t gauge) {
    int index = -1;
    if (!Read(gauge + 0x38, index) || index < 0 || index > 1 || !Readable(gauge, 0x1710)) { g_tick(gauge); return; }
    const auto i = static_cast<std::size_t>(index);
    const bool current = Current();
    const auto target = current ? Project(index + 1) : g_lookup(index + 1);
    std::uintptr_t previous{}; Read(gauge + 0x40, previous);
    const bool ours = target == reinterpret_cast<std::uintptr_t>(g_views[i].actor.data());
    const bool wasOurs = previous == reinterpret_cast<std::uintptr_t>(g_views[i].actor.data());
    // Resource address can change on a same-address actor rebuild too.
    static std::array<std::uintptr_t, 2> boundFaces{};
    if (previous != target || (ours && boundFaces[i] != g_views[i].face)) {
        TintFace(gauge, i, false);
        g_bind(gauge); // HUD-scoped lookup returns target; native lookup when our authority expires
        g_visibility(gauge);
        boundFaces[i] = ours ? g_views[i].face : 0;
        if (g_log && !g_retiring) g_log("[nativehud] slot=%d owner=%u target=%llX current=%u hp=%d/%d mp=%d/%d", index,
            ours ? g_views[i].owner : 255u, static_cast<unsigned long long>(target), current ? 1u : 0u,
            ours ? g_state.samples[g_views[i].owner].seat.vitals.hp : 0, ours ? g_state.samples[g_views[i].owner].seat.vitals.maxHp : 0,
            ours ? g_state.samples[g_views[i].owner].seat.vitals.mp : 0, ours ? g_state.samples[g_views[i].owner].seat.vitals.maxMp : 0);
    }
    // The friend gauge does not poll HP changes (native actor hit notifications normally do that).
    // Reflect owner damage/healing in its trailing HP gauge without sending native gameplay events.
    if (ours) {
        const auto hp = g_state.samples[g_views[i].owner].seat.vitals.hp;
        std::int32_t old{}; Read(gauge + 0x30, old);
        if (old != hp) { std::memcpy(reinterpret_cast<void*>(gauge + 0x2C), &old, 4); std::memcpy(reinterpret_cast<void*>(gauge + 0x30), &hp, 4); }
    }
    if (ours || wasOurs) TintFace(gauge, i, ours);
    g_tick(gauge);
}
} // namespace

void Install(std::uintptr_t base, LogFn log) {
    g_base = base; g_log = log;
    const std::uint8_t lookupBytes[] {0x48,0x63,0xC1,0x48,0x8D,0x0D,0x3E,0x46,0x95,0x00,0x48,0x8B,0x04,0xC1,0xC3};
    const std::uint8_t tickBytes[] {0x40,0x53,0x48,0x83,0xEC,0x30,0x48,0x8B,0xD9,0xE8,0x22,0xC7,0xFF,0xFF};
    const std::uint8_t bindBytes[] {0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9};
    const std::uint8_t visibleBytes[] {0x44,0x8B,0x41,0x34,0x41,0x83,0xC0,0x03,0x44,0x03,0x41,0x38};
    if (std::memcmp(reinterpret_cast<void*>(base + 0x1689A0), lookupBytes, sizeof(lookupBytes)) ||
        std::memcmp(reinterpret_cast<void*>(base + 0x182730), tickBytes, sizeof(tickBytes)) ||
        std::memcmp(reinterpret_cast<void*>(base + 0x182400), bindBytes, sizeof(bindBytes)) ||
        std::memcmp(reinterpret_cast<void*>(base + 0x17ECE0), visibleBytes, sizeof(visibleBytes))) {
        if (log) log("[nativehud] REFUSED: HUD bytes differ from Steam 9002b2de"); return;
    }
    g_bind = reinterpret_cast<GaugeFn>(base + 0x182400); g_visibility = reinterpret_cast<GaugeFn>(base + 0x17ECE0);
    HMODULE module{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&Install), &module)) {
        if (log) log("[nativehud] REFUSED: cannot pin HUD views/callbacks until process exit"); return;
    }
    const auto a = reinterpret_cast<void*>(base + 0x1689A0), b = reinterpret_cast<void*>(base + 0x182730);
    auto st = MH_CreateHook(a, reinterpret_cast<void*>(&Lookup), reinterpret_cast<void**>(&g_lookup));
    if (st == MH_OK) { g_retained = true; st = MH_CreateHook(b, reinterpret_cast<void*>(&Tick), reinterpret_cast<void**>(&g_tick)); }
    if (st == MH_OK) st = MH_EnableHook(a);
    if (st == MH_OK) st = MH_EnableHook(b);
    if (st != MH_OK) {
        MH_DisableHook(a); MH_DisableHook(b);
        if (log) log("[nativehud] REFUSED: hook status=%d", st); return;
    }
    g_installed = true;
    if (log) log("[nativehud] native friend HUD on: owner SeatState -> display-only status; local gauge unchanged");
}
bool RetainsMinHookResources() { return g_retained; }
void Retire() { g_retiring = true; }
bool QueuePreserveRestoration() {
    if (!g_installed) return true;
    return MH_QueueEnableHook(reinterpret_cast<void*>(g_base + 0x1689A0)) == MH_OK &&
        MH_QueueEnableHook(reinterpret_cast<void*>(g_base + 0x182730)) == MH_OK;
}
void Session(std::uint32_t generation, const std::array<std::uint64_t, 3>& roster, std::uint8_t local) {
    g_state.Rebase(generation, roster, local);
}
void Receive(const SeatState& m) { (void)g_state.Receive(m, GetTickCount64()); }
void Observe(const AvatarState& local, std::uintptr_t actor, const PuppetAuthority& a,
             const std::array<std::uintptr_t, 3>& bodies, const std::array<std::uintptr_t, 2>& excluded) {
    g_thread = GetCurrentThreadId();
    Session(a.mode == PuppetAuthorityMode::Network ? a.generation : 0, a.connectionIds, a.localSlot);
    g_local = {}; g_localMs = GetTickCount64();
    g_observedMs = g_localMs;
    const auto currentLoad = warp::LoadSerial();
    if (currentLoad != g_load) g_humanBodies = {};
    g_load = currentLoad; g_world = local.worldId; g_room = local.roomId; g_bodies = bodies; g_localActor = actor;
    const std::array<std::uintptr_t, 5> known {bodies[0], bodies[1], bodies[2], excluded[0], excluded[1]};
    for (const auto body : known) {
        if (!body || std::find(g_humanBodies.begin(), g_humanBodies.end(), body) != g_humanBodies.end()) continue;
        const auto empty = std::find(g_humanBodies.begin(), g_humanBodies.end(), 0);
        if (empty != g_humanBodies.end()) *empty = body;
    }
    std::uintptr_t status{}, obj{}; std::uint16_t object{}; float charge{}, chargeMax{};
    std::int32_t hp{}, maxHp{}, mp{}, maxMp{};
    if (local.flags & AvatarInCutscene || !actor || warp::TransitionPending() ||
        !Read(actor + 0x5C0, status) || status < g_base + 0x2A17290 || status >= g_base + 0x2A23810 ||
        (status - g_base - 0x2A17290) % 0x278 != 0 ||
        !Read(status, hp) || !Read(status + 4, maxHp) || !Read(status + 0x180, mp) || !Read(status + 0x184, maxMp) ||
        !Read(actor + offsets::actor::OBJENTRY_PTR, obj) || !Read(obj, object) ||
        !Read(status + 0x1BC, charge) || !Read(status + 0x1C0, chargeMax)) return;
    // Logical downed keeps native HP at one; all other numbers come directly from this owner's
    // validated pool record, never captureAvatar's historical fixed-slot fallback.
    if (hp < 0 || maxHp <= 0 || hp > maxHp) return;
    if (local.hp == 0) hp = 0;
    SeatVitals v{hp, maxHp, mp, maxMp, charge, chargeMax, object,
        static_cast<std::uint8_t>(SeatVitals::Available | ((local.hp == 0) ? SeatVitals::Downed : 0))};
    if (validSeatVitals(v)) g_local = v;
}
void Fill(SeatState& m, std::uint64_t now) {
    m.vitals = now >= g_localMs && now - g_localMs <= FreshMs ? g_local : SeatVitals{};
    if (m.vitals.flags & SeatVitals::Available) { m.worldId = g_world; m.roomId = g_room; }
}
}
