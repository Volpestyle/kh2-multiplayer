#pragma once
// VUH-2897: bounded observations only. Never grants authority, changes a native
// flag, holds an update, or calls a native function except its own trampoline.
#include <Windows.h>
#include <MinHook.h>
#include <intrin.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include "kh2coop/KH2Offsets.hpp"

namespace kh2coop::inject::axeltrace {
using LogFn = void(*)(const char*, ...);
constexpr unsigned MaxRows = 16384;
constexpr std::uint64_t WindowMs = 30000;
struct Owner {
    std::uint64_t ms = 0, frame = 0, session = 0, connection = 0, host = 0, delivery = 0;
    std::uint32_t generation = 0, epoch = 0, load = 0, transition = 0, mask = 0;
    unsigned role = 0;
};
namespace detail {
inline SRWLOCK lock = SRWLOCK_INIT;
inline std::atomic<bool> enabled {false}, active {false};
inline std::atomic<unsigned> dropped {0};
inline std::atomic<unsigned> missedArms {0}, missedReasons {0}, reportedMisses {0};
inline std::uintptr_t base = 0;
inline LogFn log = nullptr;
inline Owner owner {};
inline std::uint64_t start = 0, sequence = 0;
inline unsigned lines = 0;
inline bool finished = false;
inline bool issued = false;
inline std::uint16_t target[6] {};
inline void ReportMissedLocked() noexcept {
    const auto count = missedArms.load();
    if (log && count != reportedMisses.load()) {
        const auto reasons = missedReasons.load();
        const char* why = reasons == 1 ? "lock-contention" : reasons == 2 ? "native-read-unavailable" :
                          reasons == 4 ? "source-tuple" : reasons == 8 ? "window-finished" : "multiple";
        log("[axel-boundary] arm-missed reason=%s reasonMask=%u count=%u target=02/14 targetBtl=137 flush=locked",
            why, reasons, count);
        reportedMisses.store(count);
    }
}
inline void Missed(unsigned reason) noexcept {
    missedReasons.fetch_or(reason, std::memory_order_relaxed);
    missedArms.fetch_add(1, std::memory_order_release);
}
inline bool IsFieldGroup(std::uintptr_t group) noexcept {
    // base is immutable before hooks enable and survives Shutdown. This is a
    // fixed image scalar, so classify before contending for the logging lock.
    __try { return group == *reinterpret_cast<const volatile std::uintptr_t*>(base + 0x716868); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
struct Sample {
    std::uint16_t room[6] {};
    std::uintptr_t event = 0, group = 0, player = 0;
    std::uint32_t frozen = 0, blockers = 0, fieldState = 0;
    std::int32_t cutscene = 0;
    std::uint8_t menu = 0;
    bool available = false;
};
inline Sample Read() noexcept {
    Sample s;
    __try {
        const auto* now = reinterpret_cast<const volatile std::uint8_t*>(base + offsets::NOW);
        s.room[0] = now[0]; s.room[1] = now[1];
        s.room[2] = now[2];
        for (unsigned i = 3; i < 6; ++i)
            s.room[i] = *reinterpret_cast<const volatile std::uint16_t*>(now + 2 * i - 2);
        s.event = *reinterpret_cast<const volatile std::uintptr_t*>(base + offsets::EVENT_CONTEXT);
        s.frozen = *reinterpret_cast<const volatile std::uint32_t*>(base + offsets::CONTROLLABLE);
        s.blockers = *reinterpret_cast<const volatile std::uint32_t*>(base + offsets::PAUSE_STATUS);
        s.fieldState = *reinterpret_cast<const volatile std::uint32_t*>(base + 0x716884);
        s.group = *reinterpret_cast<const volatile std::uintptr_t*>(base + 0x716868);
        s.player = *reinterpret_cast<const volatile std::uintptr_t*>(base + 0x2A105D0);
        s.cutscene = *reinterpret_cast<const volatile std::int32_t*>(base + offsets::CUTSCENE_STATE);
        s.menu = *reinterpret_cast<const volatile std::uint8_t*>(base + offsets::OPEN_MENU);
        s.available = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return s; // Independent scalar observations, not an atomic native snapshot.
}
inline void Stop(const char* reason, std::uint64_t now, bool budgetLost = false) noexcept {
    active.store(false, std::memory_order_release);
    finished = true;
    ReportMissedLocked();
    log("[axel-boundary] stop reason=%s ms=%llu elapsedMs=%llu lines=%u dropped=%u lossObserved=%u missedArms=%u windowOnly=1",
        reason, now, now - start, lines, dropped.load(), dropped.load() != 0 || budgetLost ? 1u : 0u, missedArms.load());
}
inline std::uint64_t Emit(const char* site, std::uint64_t call, std::uintptr_t arg,
                          std::uintptr_t caller, std::uintptr_t stack, int value = 0) noexcept {
    if (!active.load(std::memory_order_acquire)) return 0;
    const auto error = GetLastError();
    if (!TryAcquireSRWLockExclusive(&lock)) {
        if (active.load(std::memory_order_acquire)) dropped.fetch_add(1, std::memory_order_relaxed);
        SetLastError(error); return 0;
    }
    std::uint64_t result = 0;
    if (active.load(std::memory_order_relaxed) && log) {
        const auto now = GetTickCount64();
        const auto s = Read();
        ReportMissedLocked();
        if (now - start >= WindowMs) Stop("trace-deadline", now);
        else if (lines >= MaxRows) Stop("trace-budget", now, true);
        else {
            result = ++sequence; ++lines;
            log("[axel-boundary] site=%s seq=%llu call=%llu ms=%llu tid=%lu fiber=%llX stack=%llX callerRva=%llX arg=%llX value=%d player=%llX "
                "read=%u room=%02X/%02X door=%u map=%u btl=%u evt=%u event=%llX frozen=%08X blockers=%08X fieldState=%u fieldGroup=%llX cutscene=%d menu=%u "
                "ownerMs=%llu frame=%llu sessionSalt=%llX generation=%u delivery=%llu connection=%llu host=%llu role=%u epoch=%u load=%u transition=%u ownerMask=%X "
                "target=%02X/%02X targetDoor=%u targetMap=%u targetBtl=%u targetEvt=%u dropped=%u",
                site, result, call, now, GetCurrentThreadId(),
                IsThreadAFiber() ? reinterpret_cast<std::uintptr_t>(GetCurrentFiber()) : 0, stack, caller, arg, value, s.player,
                s.available ? 1u : 0u, s.room[0], s.room[1], s.room[2], s.room[3], s.room[4], s.room[5],
                s.event, s.frozen, s.blockers, s.fieldState, s.group, s.cutscene, s.menu,
                owner.ms, owner.frame, owner.session, owner.generation, owner.delivery, owner.connection,
                owner.host, owner.role, owner.epoch, owner.load, owner.transition, owner.mask,
                target[0], target[1], target[2], target[3], target[4], target[5], dropped.load());
        }
    }
    ReleaseSRWLockExclusive(&lock);
    SetLastError(error);
    return result;
}
using OneFn = void(__fastcall*)(std::uintptr_t);
inline OneFn dispatch = nullptr, intro = nullptr, unfreeze = nullptr;
inline void __fastcall Dispatch(std::uintptr_t group) {
    // Only the field mode's task group; other groups never take the log lock
    // or count as losses. A copied group is never dereferenced.
    std::uint64_t call = 0;
    if (active.load(std::memory_order_acquire)) {
        const auto error = GetLastError();
        if (IsFieldGroup(group)) call = Emit("field-enter", 0, group,
                reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - base,
                reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress()));
        SetLastError(error);
    }
    __try { dispatch(group); }
    __finally { if (call) Emit(AbnormalTermination() ? "field-unwind" : "field-return", call, group, 0,
                              reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress())); }
}
inline void __fastcall Intro(std::uintptr_t task) {
    const auto call = Emit("intro-update-enter", 0, task,
        reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - base,
        reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress()));
    __try { intro(task); }
    __finally { if (call) Emit(AbnormalTermination() ? "intro-update-unwind" : "intro-update-return", call, task, 0,
                              reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress())); }
}
inline void __fastcall Unfreeze(std::uintptr_t group) {
    const auto call = Emit("unfreeze-enter", 0, group,
        reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - base,
        reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress()));
    __try { unfreeze(group); }
    __finally { if (call) Emit(AbnormalTermination() ? "unfreeze-unwind" : "unfreeze-return", call, group, 0,
                              reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress())); }
}
inline OneFn load = nullptr;
inline void __fastcall Load(std::uintptr_t task) {
    const auto call = Emit("load-task-start", 0, task,
        reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - base,
        reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress()));
    __try { load(task); }
    __finally { if (call) Emit(AbnormalTermination() ? "load-task-unwind" : "load-task-return", call, task, 0,
                              reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress())); }
}
} // namespace detail
inline bool Enabled() noexcept { return detail::enabled.load(std::memory_order_acquire); }
inline bool Active() noexcept { return detail::active.load(std::memory_order_acquire); }
inline void FlushMissed() noexcept {
    if (!Enabled() || detail::missedArms.load() == detail::reportedMisses.load()) return;
    const auto error = GetLastError();
    if (TryAcquireSRWLockExclusive(&detail::lock)) {
        detail::ReportMissedLocked();
        ReleaseSRWLockExclusive(&detail::lock);
    }
    SetLastError(error);
}
inline void Publish(Owner value) noexcept {
    if (!Enabled()) return;
    const auto error = GetLastError();
    value.ms = GetTickCount64();
    if (TryAcquireSRWLockExclusive(&detail::lock)) {
        detail::owner = value;
        ReleaseSRWLockExclusive(&detail::lock);
    } else if (Active()) detail::dropped.fetch_add(1, std::memory_order_relaxed);
    SetLastError(error);
}
inline void Point(const char* site, std::uintptr_t arg = 0) noexcept {
    detail::Emit(site, 0, arg, 0, reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress()));
}
inline std::uint64_t BeginHp(std::uintptr_t actor, int delta) noexcept {
    return detail::Emit("hp-delta-enter", 0, actor, 0,
        reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress()), delta);
}
inline void EndHp(std::uint64_t call, std::uintptr_t actor, int result) noexcept {
    if (call) detail::Emit("hp-delta-return", call, actor, 0,
        reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress()), result);
}
inline void Request(const std::uint16_t (&target)[6], bool issued) noexcept {
    if (!Enabled()) return;
    const auto error = GetLastError();
    const bool candidate = target[0] == 2 && target[1] == 0x14 && target[4] == 137;
    if (TryAcquireSRWLockExclusive(&detail::lock)) {
        const auto s = detail::Read();
        const bool hadWindow = detail::active.load();
        if (detail::active.load() &&
            (std::memcmp(detail::target,target,sizeof(target)) != 0 || (issued && detail::issued)))
            detail::Stop("request-superseded",GetTickCount64());
        if (!detail::finished && !detail::active.load() && s.available &&
            s.room[0] == 2 && s.room[1] == 0x13 && s.room[4] == 134 &&
            target[0] == 2 && target[1] == 0x14 && target[4] == 137) {
            std::memcpy(detail::target, target, sizeof(target));
            detail::start = GetTickCount64();
            detail::active.store(true, std::memory_order_release);
        } else if (candidate && !detail::active.load() && !hadWindow) {
            detail::Missed(detail::finished ? 8u : !s.available ? 2u : 4u);
        }
        if (detail::active.load() && issued) detail::issued = true;
        detail::ReportMissedLocked();
        ReleaseSRWLockExclusive(&detail::lock);
        Point(issued ? "request-issue" : "request-observed", issued ? 1 : 0);
    } else if (Active()) detail::dropped.fetch_add(1, std::memory_order_relaxed);
    else if (candidate) detail::Missed(1);
    SetLastError(error);
}
inline void Input() noexcept {
    FlushMissed(); // A failed arm must be reportable even with no active window.
    if (!detail::active.load(std::memory_order_acquire)) return;
    const auto error = GetLastError();
    static thread_local std::uint64_t last = 0;
    const auto now = GetTickCount64();
    if (now - last >= 250) { last = now; Point("input-entry"); }
    SetLastError(error);
}
inline void Install(std::uintptr_t base, LogFn log) {
    char value[2] {};
    if (GetEnvironmentVariableA("KH2COOP_AXEL_BOUNDARY_TRACE", value, sizeof(value)) != 1 || value[0] != '1') return;
    // Static decompile + qword receipts: .local/ghidra-trace-{boundaries,bytes}.log.
    constexpr std::uint8_t dispatchBytes[] = {0x45,0x33,0xC0,0x33,0xD2,0xE9,0x36,0x01,0x00,0x00};
    constexpr std::uint8_t introBytes[] = {0x40,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xF9,0x48,0x8B,0x0D,0x68,0x54,0x66,0x02};
    constexpr std::uint8_t unfreezeBytes[] = {0x8B,0xC1,0x48,0x8D,0x15,0xFF,0x7E,0x65,0x02,0x48,0xC1,0xE8,0x05,0x83,0xE1,0x1F};
    // 152990 -> fade/teardown task152A90 -> schedules field-load task152680.
    // Entry is after request-task readiness gates/teardown and NOW commit,
    // before the field-load pipeline. It is not the request/fade-start stamp.
    constexpr std::uint8_t loadBytes[] = {0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x50};
    struct Hook { std::uintptr_t rva; const void* bytes; std::size_t count; detail::OneFn detour; detail::OneFn* original; };
    const Hook hooks[] = {
        {0x14FC20,dispatchBytes,sizeof(dispatchBytes),detail::Dispatch,&detail::dispatch},
        {0x3AC000,introBytes,sizeof(introBytes),detail::Intro,&detail::intro},
        {0x3BF2E0,unfreezeBytes,sizeof(unfreezeBytes),detail::Unfreeze,&detail::unfreeze},
        {0x152680,loadBytes,sizeof(loadBytes),detail::Load,&detail::load}};
    constexpr unsigned count = sizeof(hooks) / sizeof(hooks[0]);
    for (const auto& h : hooks) if (std::memcmp(reinterpret_cast<const void*>(base+h.rva),h.bytes,h.count)) {
        log("[axel-boundary] REFUSED bytes rva=%llX",h.rva); return;
    }
    unsigned created = 0;
    for (const auto& h : hooks) {
        if (MH_CreateHook(reinterpret_cast<void*>(base+h.rva),reinterpret_cast<void*>(h.detour),
                          reinterpret_cast<void**>(h.original)) != MH_OK) break;
        ++created;
    }
    bool ready = created == count;
    // Fully initialize immutable callback storage before enabling any hook.
    detail::base = base; detail::log = log;
    if (ready) {
        for (const auto& h : hooks) if (MH_EnableHook(reinterpret_cast<void*>(base+h.rva)) != MH_OK) {
            ready = false; break;
        }
    }
    if (!ready) {
        // Remove only hooks this installation created; never alter another hook.
        for (unsigned i = 0; i < created; ++i) {
            MH_DisableHook(reinterpret_cast<void*>(base+hooks[i].rva));
            MH_RemoveHook(reinterpret_cast<void*>(base+hooks[i].rva));
        }
        log("[axel-boundary] REFUSED hook-install"); return;
    }
    detail::enabled.store(true,std::memory_order_release);
    log("[axel-boundary] configured=1 diagnosticOnly=1 scope=02/13:btl134->02/14:btl137 deadlineMs=30000 maxLines=16384 scalarReadsNotAtomic=1 ownerSnapshotMayLag=1");
}
inline void Shutdown() noexcept {
    AcquireSRWLockExclusive(&detail::lock);
    detail::ReportMissedLocked();
    if (detail::active.load() && detail::log) detail::Stop("shutdown",GetTickCount64());
    detail::active.store(false,std::memory_order_release);
    detail::enabled.store(false,std::memory_order_release);
    detail::log = nullptr;
    // Keep base and original trampolines process-lifetime for in-flight calls.
    ReleaseSRWLockExclusive(&detail::lock);
}
} // namespace kh2coop::inject::axeltrace
