#pragma once
// ============================================================================
// VUH-2897: native adapter for the combat-admission barrier (CombatAdmission.hpp).
//
// Owns the single MinHook detour on 3BF2E0 (frozen-group clear, a non-atomic
// read-modify-write of CONTROLLABLE). The Axel boundary trace observes through
// this detour instead of hooking it a second time.
//
// Only the owner (game) thread defers, services or releases. Shutdown from
// another thread only signals and waits, bounded, for the owner to release.
// Every deferral, decision, release and retirement is logged with its reason.
// ============================================================================
#include <Windows.h>
#include <MinHook.h>
#include <intrin.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <random>
#include "AxelBoundaryTrace.hpp"
#include "CombatAdmission.hpp"
#include "kh2coop/KH2Offsets.hpp"

namespace kh2coop::inject::combatadmission {
using LogFn = void(*)(const char*, ...);
struct Providers {
    bool (*ownerThread)() = nullptr;                                         // spawncontroller game thread
    bool (*identity)(std::uint32_t& load, std::uint32_t& transition, bool& pending) = nullptr; // warp
    bool (*session)(SessionView&) = nullptr;                                 // enemysync
};
namespace detail {
using OneFn = void(__fastcall*)(std::uintptr_t);
inline std::uintptr_t base = 0;
inline LogFn log = nullptr;
inline OneFn unfreeze = nullptr;     // original 3BF2E0 trampoline, process lifetime
inline OneFn freeze = nullptr;       // native 3BF2C0 (unhooked), byte-pinned at Install
inline Providers providers {};
inline Machine machine;
inline std::atomic<bool> installed {false}, live {false}, shutdownRequested {false};
inline std::uint64_t challengeState = 0;
inline bool releasing = false;       // owner-thread reentrancy guard around the original calls
inline std::uint64_t (*clock)() noexcept = nullptr; // tests only; GetTickCount64 otherwise
inline std::uint64_t Now() noexcept { return clock ? clock() : GetTickCount64(); }

inline std::uint64_t NextChallenge() noexcept {
    // splitmix64 over a random_device seed: a per-lease nonce, never zero.
    // Authentication is the relay's; this only separates incarnations.
    for (;;) {
        std::uint64_t z = (challengeState += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        if (z) return z;
    }
}
inline bool Owner() noexcept { return providers.ownerThread && providers.ownerThread(); }
// 3AC350/3AD2D0: the event's descriptor is at context+0x48; its flag byte +2 bit 3
// set means install did not freeze groups 0/1. Own SEH, so a bad descriptor reads
// as Unknown without making the rest of the sample unreadable.
inline EventFreeze EventFreezeOf(std::uintptr_t context) noexcept {
    if (!context) return EventFreeze::Unknown;
    __try {
        const auto descriptor = *reinterpret_cast<const volatile std::uintptr_t*>(context + 0x48);
        if (!descriptor) return EventFreeze::Unknown;
        return (*reinterpret_cast<const volatile std::uint8_t*>(descriptor + 2) & 0x08) ? EventFreeze::Spares
                                                                                      : EventFreeze::Freezes;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return EventFreeze::Unknown; }
}
inline NativeView Sample() noexcept {
    NativeView v;
    if (!base || !providers.identity || !providers.identity(v.load, v.transition, v.pending)) return v;
    __try {
        const auto* now = reinterpret_cast<const volatile std::uint8_t*>(base + offsets::NOW);
        v.room.world = now[0]; v.room.room = now[1]; v.room.door = now[2];
        v.room.map = *reinterpret_cast<const volatile std::uint16_t*>(now + 4);
        v.room.battle = *reinterpret_cast<const volatile std::uint16_t*>(now + 6);
        v.room.event = *reinterpret_cast<const volatile std::uint16_t*>(now + 8);
        v.eventContext = *reinterpret_cast<const volatile std::uintptr_t*>(base + offsets::EVENT_CONTEXT);
        v.cutscene = *reinterpret_cast<const volatile std::int32_t*>(base + offsets::CUTSCENE_STATE);
        v.mask = *reinterpret_cast<const volatile std::uint32_t*>(base + offsets::CONTROLLABLE);
        v.menu = *reinterpret_cast<const volatile std::uint8_t*>(base + offsets::OPEN_MENU);
        v.inField = *reinterpret_cast<const volatile std::uint8_t*>(base + offsets::IN_FIELD) != 0;
        v.readable = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { v.readable = false; }
    if (v.readable) v.eventFreeze = EventFreezeOf(static_cast<std::uintptr_t>(v.eventContext));
    return v;
}
inline SessionView Session() noexcept {
    SessionView s;
    if (!providers.session || !providers.session(s)) return {};
    return s;
}
inline void LogLease(const char* what, const Lease& l, const NativeView& v, std::uint64_t now) {
    if (!log) return;
    log("[combat-admission] %s incarnation=%llu outcome=%s failed=%u elapsedMs=%llu epoch=%u load=%u transition=%u "
        "deferred=%X owned=%X released=%X missingSeats=%X seats=%u/%u mask=%08X event=%llX menu=%u challenge=%016llX",
        what, static_cast<unsigned long long>(l.incarnation), OutcomeName(l.outcome), Failed(l.outcome) ? 1u : 0u,
        static_cast<unsigned long long>(now - l.deferredMs), l.epoch, l.load, l.transition, l.deferred, l.owned, l.released,
        machine.MissingSeats(), (l.seats[1].required ? 1u : 0u) + (l.seats[2].required ? 1u : 0u),
        (l.seats[1].acked ? 1u : 0u) + (l.seats[2].acked ? 1u : 0u), v.mask,
        static_cast<unsigned long long>(v.eventContext), v.menu, static_cast<unsigned long long>(l.challenge));
}
// Owner thread only: apply the machine's action through the original, in order.
inline void Apply(const Action& action, const NativeView& v, std::uint64_t now) {
    if (action.kind == Action::None) return;
    const Lease& l = machine.Current();
    if (action.kind == Action::Retire) {
        LogLease("retired-without-release", l, v, now);
        axeltrace::Point("admission-retire", action.incarnation);
        return;
    }
    // One owner pass, ascending: the deferred player/party calls (0, 1), then the
    // lease's own BOSS/ZAKO freezes (3, 4). Never frees 3/4 while 0/1 stay held.
    releasing = true;
    for (std::uint32_t group = 0; group < 8; ++group)
        if (action.groups & (1u << group)) unfreeze(group);
    releasing = false;
    machine.NoteReleased(action.incarnation, action.groups);
    if (log && action.via != Via::Clear)
        log("[combat-admission] release-via %s incarnation=%llu event=%llX groups=%X", ViaName(action.via),
            static_cast<unsigned long long>(action.incarnation), static_cast<unsigned long long>(v.eventContext),
            action.groups);
    LogLease(Failed(l.outcome) ? "released failed-barrier native-fallback" : "released admitted", l, v, now);
    axeltrace::Point(Failed(l.outcome) ? "admission-release-failed" : "admission-release", action.incarnation);
}
inline void ServiceOwner(std::uint64_t now) {
    if (!installed.load(std::memory_order_acquire) || releasing || !machine.Live() || !Owner()) return;
    const auto before = machine.Current().phase;
    if (shutdownRequested.load(std::memory_order_acquire)) machine.RequestShutdown(now);
    const auto v = Sample();
    const auto action = machine.Service(v, Session(), now);
    const auto& l = machine.Current();
    if (before == Phase::Held && l.phase != Phase::Held) {
        LogLease("decided", l, v, now);
        axeltrace::Point("admission-decided", static_cast<std::uintptr_t>(l.outcome));
    }
    if (action.kind == Action::None && machine.FirstWait(action.wait)) LogLease(WaitName(action.wait), l, v, now);
    Apply(action, v, now);
    live.store(machine.Live(), std::memory_order_release);
}
inline void __fastcall Unfreeze(std::uintptr_t group) {
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - base;
    const auto stack = reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress());
    const auto call = axeltrace::UnfreezeEnter(group, caller, stack);
    if ((caller == ReleaseGroup0Caller || caller == ReleaseGroup1Caller) && !releasing &&
        installed.load(std::memory_order_acquire) && Owner()) {
        const auto error = GetLastError();
        const auto now = detail::Now();
        const auto v = Sample();
        ForwardWhy why = ForwardWhy::NotCandidate;
        const auto decision = machine.OnUnfreeze(static_cast<std::uint32_t>(group), caller, v, Session(), now,
                                                 NextChallenge(), why);
        live.store(machine.Live(), std::memory_order_release);
        if (decision == Decision::Defer) {
            const auto& l = machine.Current();
            // First deferral: also freeze BOSS/ZAKO (Axel is BOSS, group 3), owning only
            // the bits this lease set; a bit the game already set is left to the game.
            // 3BF2C0 is a pinned, unconditional OR, so the lease owns exactly what it set.
            if (const auto own = machine.OwnCandidates(v)) {
                for (std::uint32_t g = 0; g < 8; ++g) if (own & (1u << g)) freeze(g);
                machine.NoteOwned(l.incarnation, own);
            }
            if (log) log("[combat-admission] deferred group=%u caller=%llX incarnation=%llu load=%u transition=%u "
                         "room=%02X/%02X btl=%u event=%llX mask=%08X owned=%X seats=%u deadlineMs=%llu challenge=%016llX",
                         static_cast<unsigned>(group), static_cast<unsigned long long>(caller),
                         static_cast<unsigned long long>(l.incarnation), l.load, l.transition, v.room.world, v.room.room,
                         v.room.battle, static_cast<unsigned long long>(v.eventContext), v.mask, l.owned,
                         (l.seats[1].required ? 1u : 0u) + (l.seats[2].required ? 1u : 0u),
                         static_cast<unsigned long long>(DeadlineMs), static_cast<unsigned long long>(l.challenge));
            axeltrace::Point("admission-defer", group);
            axeltrace::UnfreezeExit(call, group, false, stack);
            SetLastError(error);
            return; // native freeze stays in force; the original call is owed to Apply
        }
        if (why != ForwardWhy::NotCandidate && why != ForwardWhy::NotArmed && log)
            log("[combat-admission] forwarded group=%u caller=%llX why=%s room=%02X/%02X btl=%u",
                static_cast<unsigned>(group), static_cast<unsigned long long>(caller), ForwardName(why),
                v.room.world, v.room.room, v.room.battle);
        SetLastError(error);
    }
    bool unwound = true;
    __try { unfreeze(group); unwound = false; }
    __finally { axeltrace::UnfreezeExit(call, group, unwound, stack); }
}
} // namespace detail

inline bool Installed() noexcept { return detail::installed.load(std::memory_order_acquire); }
inline bool Live() noexcept { return detail::live.load(std::memory_order_acquire); }

// Host native request, owner thread, before it is forwarded. `continueRetry`: the
// game-over task is live, so this is its Continue retry, logged as a diagnostic (VUH-2906).
// Every entry point below runs inside a native or owner callback: it preserves LastError.
inline void NoteHostRequest(const Destination& from, const Destination& to, std::uint32_t nextTransition,
                            bool continueRetry = false) {
    if (!Installed() || !detail::Owner()) return;
    const auto error = GetLastError();
    const auto now = detail::Now();
    if (detail::machine.Held()) {
        detail::machine.BeforeNativeTransition(now);
        detail::LogLease("decided", detail::machine.Current(), detail::Sample(), now);
        axeltrace::Point("admission-decided", static_cast<std::uintptr_t>(detail::machine.Current().outcome));
        detail::ServiceOwner(now); // release while the old native identity still matches
    }
    detail::machine.NoteRequest(from, to, nextTransition);
    // Every Continue retry's exact destination (in Axel I: the 02/15 checkpoint, never armed).
    if (continueRetry && detail::log)
        detail::log("[combat-admission] continue-retry from=%02X/%02X btl=%u to=%02X/%02X door=%u map=%u btl=%u evt=%u "
                    "transition=%u armed=%u", from.world, from.room, from.battle, to.world, to.room, to.door, to.map,
                    to.battle, to.event, nextTransition, detail::machine.Requested() ? 1u : 0u);
    SetLastError(error);
}
inline void NoteLoad(std::uint32_t load, std::uint32_t transition) {
    if (!Installed() || !detail::Owner()) return;
    const auto error = GetLastError();
    const auto now = detail::Now();
    const auto v = detail::Sample();
    const auto action = detail::machine.NoteLoad(load, transition, now);
    detail::Apply(action, v, now);
    detail::live.store(detail::machine.Live(), std::memory_order_release);
    SetLastError(error);
}
// Input collector (continues during native pause) and the entity-head frame.
inline void Service() noexcept {
    if (!detail::live.load(std::memory_order_acquire) && !detail::shutdownRequested.load(std::memory_order_acquire)) return;
    const auto error = GetLastError();
    detail::ServiceOwner(detail::Now());
    SetLastError(error);
}
// S3: relaxations for the host's room publication and progress sample only.
inline bool PublicationHoldQualifies() noexcept {
    return Live() && detail::Owner() && detail::machine.PublicationHoldQualifies(detail::Sample());
}
inline std::uint64_t PublicationChallenge() noexcept {
    return Live() && detail::Owner() ? detail::machine.PublicationChallenge(detail::Sample()) : 0;
}
inline void NotePublished(std::uint32_t epoch, std::uint64_t challenge) {
    if (!challenge || !detail::Owner()) return;
    const auto error = GetLastError();
    const bool bound = detail::machine.NotePublished(epoch, challenge);
    if (detail::log) detail::log("[combat-admission] published epoch=%u incarnation=%llu bound=%u", epoch,
                                 static_cast<unsigned long long>(detail::machine.Current().incarnation), bound ? 1u : 0u);
    if (bound) axeltrace::Point("admission-published", epoch);
    SetLastError(error);
}
// EnemySync has authenticated the envelope source as this slot's current
// connection and delivery. A refused ACK is a logged no-op and never arms.
inline AckResult Ack(std::uint8_t slot, std::uint64_t connection, std::uint64_t delivery, std::uint32_t epoch,
                     const Destination& room, std::uint64_t challenge) {
    if (!Installed() || !detail::Owner()) return AckResult::NoLease;
    const auto error = GetLastError();
    const auto result = detail::machine.Ack(slot, connection, delivery, epoch, room, challenge);
    if (detail::log) detail::log("[combat-admission] ack slot=%u epoch=%u result=%s incarnation=%llu missingSeats=%X",
                                 static_cast<unsigned>(slot), epoch, AckName(result),
                                 static_cast<unsigned long long>(detail::machine.Current().incarnation),
                                 detail::machine.MissingSeats());
    if (result == AckResult::Accepted) {
        axeltrace::Point("admission-ack", slot);
        Service(); // release on this owner pass if this was the last seat
    }
    SetLastError(error);
    return result;
}

inline void Install(std::uintptr_t base, LogFn log, Providers providers) {
    char value[2] {};
    const bool disabled = GetEnvironmentVariableA("KH2COOP_COMBAT_ADMISSION", value, sizeof(value)) == 1 && value[0] == '0';
    // Receipts: 3BF2E0 qword dump and 3ACBFC..3ACC0D call sites (xor ecx,ecx; call 3BF2E0;
    // mov ecx,1; call 3BF2E0), docs/testing/vuh2897-axel-arrival/BARRIER.md.
    constexpr std::uint8_t unfreezeBytes[] = {0x8B,0xC1,0x48,0x8D,0x15,0xFF,0x7E,0x65,0x02,0x48,0xC1,0xE8,0x05,0x83,0xE1,0x1F};
    constexpr std::uint8_t callerBytes[] = {0x33,0xC9,0xE8,0xDD,0x26,0x01,0x00,0xB9,0x01,0x00,0x00,0x00,0xE8,0xD3,0x26,0x01,0x00};
    // 3BF2C0: mov eax,ecx; lea rdx,[2A171E8]; shr rax,5; and ecx,1F; lea rdx,[rdx+rax*4];
    // mov eax,1; shl eax,cl; or [rdx],eax; ret -- the whole native group setter.
    constexpr std::uint8_t freezeBytes[] = {0x8B,0xC1,0x48,0x8D,0x15,0x1F,0x7F,0x65,0x02,0x48,0xC1,0xE8,0x05,0x83,0xE1,0x1F,
                                            0x48,0x8D,0x14,0x82,0xB8,0x01,0x00,0x00,0x00,0xD3,0xE0,0x09,0x02,0xC3};
    if (std::memcmp(reinterpret_cast<const void*>(base + 0x3BF2E0), unfreezeBytes, sizeof(unfreezeBytes)) ||
        std::memcmp(reinterpret_cast<const void*>(base + 0x3BF2C0), freezeBytes, sizeof(freezeBytes)) ||
        std::memcmp(reinterpret_cast<const void*>(base + 0x3ACBFC), callerBytes, sizeof(callerBytes))) {
        log("[combat-admission] REFUSED bytes; Axel arrival stays native"); return;
    }
    if (!providers.ownerThread || !providers.identity || !providers.session) {
        log("[combat-admission] REFUSED providers"); return;
    }
    try {
        std::random_device random;
        detail::challengeState = (static_cast<std::uint64_t>(random()) << 32) ^ random() ^ detail::Now();
    } catch (...) { log("[combat-admission] REFUSED challenge source"); return; }
    detail::base = base; detail::log = log; detail::providers = providers;
    detail::freeze = reinterpret_cast<detail::OneFn>(base + 0x3BF2C0);
    detail::machine.enabled = !disabled;
    void* target = reinterpret_cast<void*>(base + 0x3BF2E0);
    if (MH_CreateHook(target, reinterpret_cast<void*>(detail::Unfreeze), reinterpret_cast<void**>(&detail::unfreeze)) != MH_OK) {
        log("[combat-admission] REFUSED hook-create"); return;
    }
    if (MH_EnableHook(target) != MH_OK) {
        MH_RemoveHook(target);
        log("[combat-admission] REFUSED hook-enable"); return;
    }
    detail::installed.store(true, std::memory_order_release);
    log("[combat-admission] configured=1 enabled=%u scope=02/13:btl134->02/14:d0/m137/b137/e137 deadlineMs=%llu "
        "deferred=0,1 owned=3,4 callers=%llX,%llX", detail::machine.enabled ? 1u : 0u,
        static_cast<unsigned long long>(DeadlineMs), static_cast<unsigned long long>(ReleaseGroup0Caller),
        static_cast<unsigned long long>(ReleaseGroup1Caller));
}

// Any thread. On the owner thread it releases now; elsewhere it signals the
// owner (the input collector services it) and waits at most waitMs. It never
// writes the mask off the owner thread; an unreleased lease is logged, and the
// next native load resets the groups (S2).
// HP-loss receipt while the hold is in force: combat exclusion failed for this
// lease. Logged only; the deadline and downing paths still own the outcome.
inline void NoteHpLoss(std::uintptr_t actor, int delta, int result) noexcept {
    if (!Live() || !detail::log || !detail::Owner() || !detail::machine.Held()) return;
    const auto error = GetLastError();
    detail::log("[combat-admission] hp-loss-during-hold actor=%llX delta=%d result=%d incarnation=%llu elapsedMs=%llu",
                static_cast<unsigned long long>(actor), delta, result,
                static_cast<unsigned long long>(detail::machine.Current().incarnation),
                static_cast<unsigned long long>(detail::Now() - detail::machine.Current().deferredMs));
    SetLastError(error);
}

inline void Shutdown(unsigned waitMs = 500) noexcept {
    if (!Installed()) return;
    detail::shutdownRequested.store(true, std::memory_order_release);
    if (detail::Owner()) detail::ServiceOwner(detail::Now()); // the owner cannot wait on itself
    else {
        const auto until = detail::Now() + waitMs;
        while (Live() && detail::Now() < until) Sleep(10);
    }
    if (Live() && detail::log) detail::log("[combat-admission] shutdown left lease unreleased; owner did not service within %ums", waitMs);
    detail::installed.store(false, std::memory_order_release);
}
} // namespace kh2coop::inject::combatadmission
