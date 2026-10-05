#include "NativeLifecycleTrace.hpp"
#include "NativeTraceFiber.hpp"
#include "kh2coop/KH2Offsets.hpp"

#include <Windows.h>
#include <MinHook.h>
#include <intrin.h>
#include <array>
#include <atomic>
#include <cstring>
#include <type_traits>

namespace kh2coop::inject::lifecycletrace {
namespace {

using OneFn = void (__fastcall*)(void*);
using PairFn = void (__fastcall*)(void*, void*);
using PredicateFn = std::uint8_t (__fastcall*)(void*); // genuine AL, never normalize to bool
constexpr std::size_t kQueueCap = 128;
constexpr std::uint32_t kDepthCap = 32;
constexpr std::array<uintptr_t, 8> kRvas {0x3FFD90, 0x3B45C0, 0x3D4A40, 0x3FED10, 0x3FED40, 0x3DAC30, 0x3B4420, 0x3CE550};
// Independently checked against the saved PE. Every prefix ends at an
// instruction boundary; MinHook still owns relocation of the original code.
constexpr std::uint8_t kRemovalBytes[] {0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x8B,0x82,0xB8,0x09,0x00,0x00};
constexpr std::uint8_t kDisposalBytes[] {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x20};
constexpr std::uint8_t kDeathMarkBytes[] {0x40,0x53,0x48,0x83,0xEC,0x20,0x8B,0x91,0xB8,0x09,0x00,0x00,0x48,0x8B,0xD9};
constexpr std::uint8_t kDeathBookBytes[] {0xF6,0x41,0x04,0x01,0x75,0x24,0x8B,0x82,0xC8,0x06,0x00,0x00,0xC1,0xE8,0x0B};
constexpr std::uint8_t kCountBytes[] {0x48,0x83,0xEC,0x28,0xF6,0x41,0x04,0x01,0x0F,0x85,0x88,0x01,0x00,0x00};
// Entire bodies: branch attribution is invalid on a matching entry alone.
constexpr std::uint8_t kPredicateBytes[] {
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0xE8,0xE2,0x97,0xFD,0xFF,0x84,0xC0,
    0x74,0x66,0x48,0x83,0xBB,0x80,0x00,0x00,0x00,0x00,0x74,0x0A,0x48,0x83,0xBB,0x98,
    0x00,0x00,0x00,0x00,0x75,0x52,0x8B,0x8B,0xB4,0x0B,0x00,0x00,0xE8,0x0F,0x26,0x0D,
    0x00,0x48,0x85,0xC0,0x74,0x13,0x8B,0x8B,0xB4,0x0B,0x00,0x00,0xE8,0xFF,0x25,0x0D,
    0x00,0x48,0x8B,0xC8,0xE8,0xE7,0x35,0xFF,0xFF,0x8B,0x8B,0xB4,0x0B,0x00,0x00,0xE8,
    0xEC,0x25,0x0D,0x00,0x48,0x85,0xC0,0x74,0x17,0x8B,0x8B,0xB4,0x0B,0x00,0x00,0xE8,
    0xDC,0x25,0x0D,0x00,0x48,0x8B,0xC8,0xE8,0xB4,0x38,0xFF,0xFF,0x84,0xC0,0x75,0x08,
    0xB0,0x01,0x48,0x83,0xC4,0x20,0x5B,0xC3,0x32,0xC0,0x48,0x83,0xC4,0x20,0x5B,0xC3};
constexpr std::uint8_t kScriptBytes[] {
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x8B,0x89,0xB0,0x05,0x00,0x00,
    0x48,0x85,0xC9,0x74,0x17,0x33,0xD2,0xE8,0x44,0xD8,0x02,0x00,0x85,0xC0,0x75,0x0C,
    0x48,0x8D,0x8B,0x90,0x03,0x00,0x00,0xE8,0x04,0x61,0x01,0x00,0x83,0xBB,0xB8,0x05,
    0x00,0x00,0x00,0x0F,0x94,0xC0,0x48,0x83,0xC4,0x20,0x5B,0xC3};
constexpr std::uint8_t kAuxiliaryBytes[] {0x83,0x79,0x14,0x00,0x0F,0x95,0xC0,0xC3};
constexpr std::uint8_t kPredicateCallerBytes[] {0x41,0xFF,0x50,0x40};
constexpr std::uint8_t kPredicateThunkBytes[] {0x48,0x8B,0xCA,0xE9,0x98,0x10,0xFC,0xFF};
static_assert(sizeof(kPredicateBytes) == 128 && sizeof(kScriptBytes) == 60 && sizeof(kAuxiliaryBytes) == 8);

uintptr_t g_exeBase = 0;
std::uint32_t g_imageSize = 0;
spawncontroller::LogFn g_log = nullptr;
spawncontroller::RoleFn g_role = nullptr;
PairFn g_removal = nullptr, g_deathBook = nullptr;
OneFn g_disposal = nullptr, g_deathMark = nullptr, g_count = nullptr;
PredicateFn g_predicate = nullptr, g_script = nullptr, g_auxiliary = nullptr;
std::atomic<bool> g_requested {false};
std::atomic<std::uint32_t> g_verified {0}, g_installed {0}, g_failed {0}, g_exception {0};
std::atomic<std::uint64_t> g_started {0}, g_published {0}, g_dropped {0}, g_unavailable {0};
std::atomic<std::uint64_t> g_outOfScope {0}, g_faults {0}, g_unwound {0}, g_depthOverflow {0};
std::atomic<std::uint64_t> g_coverageGeneration {0};
std::atomic<std::uint64_t> g_predicateStarted {0}, g_predicatePublished {0}, g_predicateDropped {0};
std::atomic<std::uint64_t> g_predicateForeign {0}, g_predicateUnmatched {0}, g_predicateUnwound {0};
std::atomic<std::uint64_t> g_predicateDepthOverflow {0}, g_predicateCountOverflow {0};
SRWLOCK g_queueLock = SRWLOCK_INIT;
std::array<Event, kQueueCap> g_queue {};
std::size_t g_read = 0, g_size = 0;
struct Frame {Event event{};uintptr_t anchor{};std::uint64_t serial{};bool predicate{};};
struct Local {unsigned depth{};std::array<Frame,kDepthCap> frames{};};
tracefiber::Store<Local> g_storage;
std::atomic<bool> g_recording{};
std::atomic<std::uint32_t> g_enableAttempted{};
std::atomic<std::uint64_t> g_scopeSerial{};
Frame* Parent(Local*& local) {
    local=g_storage.Current();if(!local || !local->depth || !g_recording)return nullptr;
    if(local->depth>kDepthCap){g_storage.Reject(local);local=nullptr;return nullptr;}
    auto& frame=local->frames[local->depth-1];
    if(!g_storage.Anchor(frame.anchor)){g_storage.Reject(local);local=nullptr;return nullptr;}return &frame;
}
bool Current(Local* local,unsigned depth,std::uint64_t serial) {
    const bool valid=g_storage.Same(local) && local->depth==depth && depth>0 && depth<=kDepthCap &&
        local->frames[depth-1].serial==serial && g_storage.Anchor(local->frames[depth-1].anchor);
    if(!valid)g_storage.Abandon(local);
    return valid;
}
static_assert(std::is_trivially_copyable_v<Event> && std::is_trivially_destructible_v<Event>);
RemovalOperands CaptureRemovalOperands(uintptr_t actor);

bool Range(uintptr_t address, std::size_t size) {
    constexpr uintptr_t end = 0x7FFFFFFFFFFFULL;
    return address > 0x10000 && address < end && size <= end - address;
}
bool Copy(uintptr_t address, void* out, std::size_t size) {
    if (!Range(address, size)) return false;
    __try { std::memcpy(out, reinterpret_cast<const void*>(address), size); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template <typename T> bool Read(uintptr_t address, T& value) { return Copy(address, &value, sizeof(value)); }

std::uint32_t ReadImageSize(uintptr_t base) {
    IMAGE_DOS_HEADER dos {};
    IMAGE_NT_HEADERS64 nt {};
    if (!Range(base, sizeof(dos)) || !Read(base, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew < static_cast<LONG>(sizeof(dos)) || dos.e_lfanew > 0x100000 ||
        !Range(base, static_cast<std::size_t>(dos.e_lfanew) + sizeof(nt)) ||
        !Read(base + static_cast<uintptr_t>(dos.e_lfanew), nt) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.OptionalHeader.SizeOfImage < 0x411830 || !Range(base, nt.OptionalHeader.SizeOfImage)) return 0;
    return nt.OptionalHeader.SizeOfImage;
}

bool SameActorMetadata(const ActorSnapshot& a, const ActorSnapshot& b) {
    return a.available && b.available && a.actor == b.actor && a.objentry == b.objentry &&
        a.status == b.status && a.controller == b.controller && a.record == b.record &&
        a.objectId == b.objectId && a.type == b.type && a.combat == b.combat &&
        a.recordAvailable == b.recordAvailable && (!a.recordAvailable ||
        (a.recordId == b.recordId && a.recordMode == b.recordMode && a.recordStage == b.recordStage));
}

ActorSnapshot CaptureActor(uintptr_t actor) {
    ActorSnapshot result;
    result.actor = actor;
    char name[2] {};
    if (!Range(actor, 0xAB4) || !Read(actor + offsets::actor::OBJENTRY_PTR, result.objentry) || !Range(result.objentry, 0x60) ||
        !Read(result.objentry + offsets::objentry::OBJECT_ID, result.objectId) ||
        !Read(result.objentry + offsets::objentry::TYPE_FLAGS, result.type) ||
        !Copy(result.objentry + offsets::objentry::NAME, name, sizeof(name))) return result;
    uintptr_t classifiedObject = 0;
    std::uint32_t classifiedId = 0;
    std::uint8_t classifiedType = 0;
    char classifiedName[2] {};
    if (!Read(actor + offsets::actor::OBJENTRY_PTR, classifiedObject) || classifiedObject != result.objentry ||
        !Read(result.objentry + offsets::objentry::OBJECT_ID, classifiedId) || classifiedId != result.objectId ||
        !Read(result.objentry + offsets::objentry::TYPE_FLAGS, classifiedType) || classifiedType != result.type ||
        !Copy(result.objentry + offsets::objentry::NAME, classifiedName, sizeof(classifiedName)) ||
        std::memcmp(name, classifiedName, sizeof(name)) != 0) return result;
    result.classificationAvailable = true;
    result.combat = (result.type == offsets::objentry::TYPE_BOSS || result.type == offsets::objentry::TYPE_MOB) &&
                    !(name[0] == 'F' && name[1] == '_');
    // Noncombat may have smaller layouts. Do not probe combat-only fields.
    if (!result.combat) return result;
    if (!Read(actor + 0x5C0, result.status) || !Range(result.status, 8) ||
        !Read(result.status, result.hp) || !Read(result.status + 4, result.maxHp) ||
        !Read(actor + 0x9E8, result.controller) || !Read(actor + 0x9F0, result.record) ||
        !Read(actor + 0x120, result.flags120) || !Read(actor + 0x9B8, result.flags9B8) ||
        !Read(actor + 0x6C8, result.flags6C8) || !Read(actor + 0xA08, result.fadeA08) ||
        !Read(actor + 0xA0C, result.slopeA0C) || !Read(actor + 0xAAC, result.fadeAAC) ||
        !Read(actor + 0xAB0, result.slopeAB0)) return result;
    if (result.record) {
        if (!Range(result.record, 0x40) || !Read(result.record + 0x1E, result.recordId) || !Read(result.record + 0x1C, result.recordMode) ||
            !Read(result.record + 0x30, result.recordStage)) return result;
        result.recordAvailable = true;
    }
    uintptr_t objentry = 0, status = 0, controller = 0, record = 0;
    std::uint32_t objectId = 0;
    std::uint8_t type = 0;
    char checkName[2] {};
    if (!Read(actor + offsets::actor::OBJENTRY_PTR, objentry) || objentry != result.objentry ||
        !Read(actor + 0x5C0, status) || status != result.status ||
        !Read(actor + 0x9E8, controller) || controller != result.controller ||
        !Read(actor + 0x9F0, record) || record != result.record ||
        !Read(objentry + offsets::objentry::OBJECT_ID, objectId) || objectId != result.objectId ||
        !Read(objentry + offsets::objentry::TYPE_FLAGS, type) || type != result.type ||
        !Copy(objentry + offsets::objentry::NAME, checkName, sizeof(checkName)) ||
        std::memcmp(name, checkName, sizeof(name)) != 0) return result;
    if (record) {
        std::uint16_t id = 0;
        std::uint8_t mode = 0, stage = 0;
        if (!Read(record + 0x1E, id) || id != result.recordId ||
            !Read(record + 0x1C, mode) || mode != result.recordMode ||
            !Read(record + 0x30, stage) || stage != result.recordStage) return result;
    }
    result.available = true;
    return result;
}

void Publish(const Event& event) {
    const bool predicate = event.kind == Kind::RemovalPredicate;
    if (!TryAcquireSRWLockExclusive(&g_queueLock)) { ++g_dropped; if (predicate) ++g_predicateDropped; return; }
    if (g_size == kQueueCap) { ++g_dropped; if (predicate) ++g_predicateDropped; }
    else {
        g_queue[(g_read + g_size) % kQueueCap] = event; ++g_size; ++g_published;
        if (predicate) ++g_predicatePublished;
    }
    ReleaseSRWLockExclusive(&g_queueLock);
}

void InvokeOriginal(Kind kind, void* controller, void* actor) {
    switch (kind) {
    case Kind::RemovalBookkeeping: g_removal(controller, actor); break;
    case Kind::Disposal: g_disposal(actor); break;
    case Kind::DeathMark: g_deathMark(actor); break;
    case Kind::DeathBookkeeping: g_deathBook(controller, actor); break;
    case Kind::CountDecrement: g_count(controller); break;
    case Kind::RemovalPredicate: break; // Typed AL-returning scope below; never dispatched here.
    }
}

int NoteFault(Event* event, unsigned long code) {
    event->exceptionCode = static_cast<std::uint32_t>(code);
    g_exception.store(static_cast<std::uint32_t>(code));
    ++g_faults;
    return EXCEPTION_CONTINUE_SEARCH;
}

// This filter observes exceptions from the original only. Read faults in the
// diagnostic snapshots remain unavailable and are never native-fault counts.
void CallOriginal(Event* event, void* controller, void* actor) {
    __try { InvokeOriginal(event->kind, controller, actor); }
    __except (NoteFault(event, GetExceptionCode())) { /* unreachable */ }
}

void BeginEvent(Event& event, Kind kind, void* controller, void* actor, uintptr_t caller) {
    event.kind = kind;
    event.sequence = ++g_started;
    Local* local=nullptr;auto* parent=Parent(local);
    event.parentSequence=parent?parent->event.sequence:0;event.depth=local?local->depth:0;
    event.actor = reinterpret_cast<uintptr_t>(actor);
    event.controller = reinterpret_cast<uintptr_t>(controller);
    event.callerInImage = g_imageSize && caller >= g_exeBase && caller - g_exeBase < g_imageSize;
    if (event.callerInImage) event.callerRva = caller - g_exeBase;
    event.roleAvailable = g_role && spawncontroller::IsDiagnosticGameThread();
    if (event.roleAvailable) event.role = g_role();
    event.beforeStampAvailable = spawncontroller::CaptureDiagnosticStamp(g_exeBase, event.beforeStamp);
    if (kind != Kind::CountDecrement) {
        event.beforeActor = CaptureActor(event.actor);
        if (kind == Kind::Disposal || kind == Kind::DeathMark || kind == Kind::RemovalPredicate) {
            event.controller = event.beforeActor.available ? event.beforeActor.controller : 0;
            event.controllerFromActor = event.beforeActor.available;
        } else if (event.beforeActor.available) {
            event.actorControllerMismatch = event.beforeActor.controller != event.controller;
        }
    }
    event.beforeState = spawncontroller::CaptureDiagnosticState(g_exeBase, event.controller);
}

void FinishEvent(Event& event, bool completed) {
    event.originalReturned = completed;
    event.unwound = !completed;
    if (completed) {
        event.afterStampAvailable = spawncontroller::CaptureDiagnosticStamp(g_exeBase, event.afterStamp);
        event.lifecycleStable = event.beforeStampAvailable && event.afterStampAvailable &&
            event.beforeStamp.transition == event.afterStamp.transition && event.beforeStamp.load == event.afterStamp.load &&
            event.beforeStamp.location == event.afterStamp.location;
        // A changed lifecycle invalidates every old native address before reads.
        if (event.lifecycleStable) {
            if (event.kind == Kind::RemovalPredicate) event.removal.after = CaptureRemovalOperands(event.actor);
            event.afterState = spawncontroller::CaptureDiagnosticState(g_exeBase, event.controller);
            event.controllerComparable = event.beforeState.controllerAvailable && event.afterState.controllerAvailable &&
                event.beforeState.header == event.afterState.header && event.beforeState.spawnArray == event.afterState.spawnArray &&
                event.beforeState.key == event.afterState.key && event.beforeState.headerId == event.afterState.headerId &&
                event.beforeState.recordCount == event.afterState.recordCount && event.beforeState.nativeType == event.afterState.nativeType;
            if (event.kind != Kind::CountDecrement && event.beforeActor.available) {
                event.afterActor = CaptureActor(event.actor);
                event.postActorComparable = SameActorMetadata(event.beforeActor, event.afterActor);
                if (!event.postActorComparable) event.afterActor.available = false;
            }
        }
    } else {
        // Do not inspect native memory while an exception is unwinding.
        event.afterActor = {};
        event.afterState = {};
        event.afterStamp = {};
        event.afterStampAvailable = event.lifecycleStable = event.postActorComparable = event.controllerComparable = false;
        ++g_unwound;
    }
    // Unknown classification is not a successful noncombat exclusion.
    event.outOfScope = event.kind != Kind::CountDecrement && event.beforeActor.classificationAvailable && !event.beforeActor.combat;
    event.unavailable = !completed || !event.roleAvailable || !event.lifecycleStable || event.actorControllerMismatch ||
        (event.kind == Kind::RemovalPredicate && event.removal.branch == RemovalBranch::Unknown) ||
        !event.beforeState.cacheAvailable || !event.afterState.cacheAvailable ||
        ((event.controller || event.kind == Kind::CountDecrement) && !event.controllerComparable) ||
        (event.kind != Kind::CountDecrement && !event.outOfScope &&
         (!event.beforeActor.available || !event.postActorComparable));
    if (event.outOfScope) ++g_outOfScope;
    if (event.unavailable) ++g_unavailable;
    Publish(event);
}

// POD-only SEH scope: exactly one original invocation, no exception swallowing,
// and validated retained-frame restoration on normal return or unwind. Events publish child-first;
// sequence/parent/depth express entry order rather than queue order.
void Run(Kind kind,void* controller,void* actor,uintptr_t caller) {
    const auto error=GetLastError();Local* local=nullptr;(void)Parent(local);
    if(!g_recording || !local || local->depth>=kDepthCap) {
        if(local && local->depth>=kDepthCap)g_storage.Reject(local);
        ++g_dropped;SetLastError(error);InvokeOriginal(kind,controller,actor);return;
    }
    const unsigned index=local->depth;auto& frame=local->frames[index];frame={};
    frame.anchor=reinterpret_cast<uintptr_t>(_AddressOfReturnAddress());frame.serial=++g_scopeSerial;
    BeginEvent(frame.event,kind,controller,actor,caller);const auto serial=frame.serial;++local->depth;
    bool completed=false;
    __try {SetLastError(error);CallOriginal(&frame.event,controller,actor);completed=true;}
    __finally {const auto nativeError=GetLastError();
        if(Current(local,index+1,serial)) {FinishEvent(frame.event,completed);frame={};local->depth=index;}
        else ++g_dropped;
        SetLastError(nativeError);
    }
}

RemovalOperands CaptureRemovalOperands(uintptr_t actor) {
    RemovalOperands out;
    // Independent reads: one missing field does not erase another observation.
    if (!Range(actor, 0xBB8)) return out;
    if (Read(actor + 0x5B0, out.scriptState)) out.availableMask |= 1;
    if (Read(actor + 0x5B8, out.scriptTest)) out.availableMask |= 2;
    if (Read(actor + 0x80, out.field80)) out.availableMask |= 4;
    if (Read(actor + 0x98, out.field98)) out.availableMask |= 8;
    if (Read(actor + 0xBB4, out.auxiliaryHandle)) out.availableMask |= 16;
    return out;
}

void IncrementPredicateCount(RemovalPredicates& facts, std::uint32_t& count) {
    if (count != UINT32_MAX) { ++count; return; }
    if (!facts.countOverflow) ++g_predicateCountOverflow;
    facts.countOverflow = true;
}

RemovalBranch ClassifyRemoval(const RemovalPredicates& p) {
    if (!p.originalReturned || !p.resultAvailable || !p.coverageStable ||
        (p.coverageMask & RemovalPredicateHooks) != RemovalPredicateHooks ||
        p.countOverflow || p.nestedAmbiguous || p.faultMask || p.unwindMask ||
        p.scriptCalls != 1 || p.scriptReturned != 1 || p.auxiliaryCalls > 1 ||
        p.auxiliaryReturned != p.auxiliaryCalls) return RemovalBranch::Unknown;
    // These are executed-return facts from fully guarded code, not arithmetic
    // on before/after operand snapshots. Preserve the genuine result bytes.
    if (!p.scriptResult)
        return !p.parentResult && !p.auxiliaryCalls ? RemovalBranch::ScriptBlocked : RemovalBranch::Unknown;
    if (!p.auxiliaryCalls)
        return p.parentResult ? RemovalBranch::AuxiliaryMissingAllowed : RemovalBranch::PointersBlocked;
    if (p.auxiliaryResult)
        return !p.parentResult ? RemovalBranch::AuxiliaryBlocked : RemovalBranch::Unknown;
    return p.parentResult ? RemovalBranch::AuxiliaryAllowed : RemovalBranch::Unknown;
}

int NotePredicateChildFault(Event* event, std::uint32_t bit, unsigned long code) {
    event->removal.faultMask |= bit;
    return NoteFault(event, code);
}

int NotePredicateParentFault(Event* event, unsigned long code) {
    event->removal.faultMask |= HookBit(Kind::RemovalPredicate);
    return NoteFault(event, code);
}

std::uint8_t CallPredicateOriginal(Event* event, PredicateFn fn, void* argument, std::uint32_t childBit) {
    __try { return fn(argument); }
    __except (childBit ? NotePredicateChildFault(event, childBit, GetExceptionCode()) :
                         NotePredicateParentFault(event, GetExceptionCode())) { /* unreachable */ }
    return 0;
}

std::uint8_t RunPredicateChild(bool script, void* argument, uintptr_t caller) {
    const auto error=GetLastError();const auto fn = script ? g_script : g_auxiliary;
    if (!spawncontroller::IsDiagnosticGameThread()) { ++g_predicateForeign;SetLastError(error); return fn(argument); }
    Local* local=nullptr;auto* frame=Parent(local);
    auto* event=frame && frame->predicate?&frame->event:nullptr;
    const auto bit = script ? ScriptPredicateHook : AuxiliaryPredicateHook;
    if (!event || caller != g_exeBase + (script ? 0x3DAC3E : 0x3DAC9C) ||
        (script && reinterpret_cast<uintptr_t>(argument) != event->actor)) {
        ++g_predicateUnmatched;
        SetLastError(error);return fn(argument);
    }
    const auto depth=local->depth;const auto serial=frame->serial;
    auto& p = event->removal;
    auto& calls = script ? p.scriptCalls : p.auxiliaryCalls;
    auto& returned = script ? p.scriptReturned : p.auxiliaryReturned;
    IncrementPredicateCount(p, calls);
    const bool first = calls == 1 && !p.countOverflow;
    if (!script && first) {
        p.auxiliaryArgument = reinterpret_cast<uintptr_t>(argument);
        if (Range(p.auxiliaryArgument, 0x18) && Read(p.auxiliaryArgument + 0x14, p.auxiliaryBefore))
            p.auxiliaryAvailableMask |= 1;
    }
    bool completed = false;
    std::uint8_t result = 0;
    __try {
        SetLastError(error);result = CallPredicateOriginal(event, fn, argument, bit);
        completed = true;
    } __finally {
        const auto nativeError=GetLastError();
        if (!Current(local,depth,serial)) {++g_predicateDropped;}
        else if (completed) {
            IncrementPredicateCount(p, returned);
            if (first) {
                if (script) {
                    p.scriptResult = result;
                    p.afterScript = CaptureRemovalOperands(event->actor);
                } else {
                    p.auxiliaryResult = result;
                    if (Range(p.auxiliaryArgument, 0x18) && Read(p.auxiliaryArgument + 0x14, p.auxiliaryAfter))
                        p.auxiliaryAvailableMask |= 2;
                }
            }
        } else {
            // POD writes only; no native reads or helper replay during unwind.
            p.unwindMask |= bit;
        }
        SetLastError(nativeError);
    }
    return result;
}

std::uint8_t PassPredicate(void* actor) {
    const auto error=GetLastError();Local* local=nullptr;auto* parent=Parent(local);
    if(parent && parent->predicate)parent->event.removal.nestedAmbiguous=true;
    if(!local || local->depth>=kDepthCap){if(local)g_storage.Reject(local);SetLastError(error);return g_predicate(actor);}
    const auto index=local->depth;auto& frame=local->frames[index];frame={};
    frame.anchor=reinterpret_cast<uintptr_t>(_AddressOfReturnAddress());frame.serial=++g_scopeSerial;
    const auto serial=frame.serial;++local->depth;std::uint8_t result=0;
    __try {SetLastError(error);result=g_predicate(actor);}
    __finally {const auto nativeError=GetLastError();
        if(Current(local,index+1,serial)){frame={};local->depth=index;}else ++g_predicateDropped;
        SetLastError(nativeError);
    }return result;
}
std::uint8_t RunPredicate(void* actor,uintptr_t caller) {
    const auto error=GetLastError();
    if(!g_recording || !spawncontroller::IsDiagnosticGameThread()){++g_predicateForeign;SetLastError(error);return PassPredicate(actor);}
    if(caller!=g_exeBase+0x3BFD6F){++g_predicateUnmatched;SetLastError(error);return PassPredicate(actor);}
    Local* local=nullptr;auto* parent=Parent(local);
    if(!local || local->depth>=kDepthCap){++g_depthOverflow;++g_predicateDepthOverflow;SetLastError(error);return PassPredicate(actor);}
    if(parent && parent->predicate)parent->event.removal.nestedAmbiguous=true;
    ++g_predicateStarted;const auto index=local->depth;auto& frame=local->frames[index];frame={};
    frame.anchor=reinterpret_cast<uintptr_t>(_AddressOfReturnAddress());frame.serial=++g_scopeSerial;frame.predicate=true;
    BeginEvent(frame.event,Kind::RemovalPredicate,nullptr,actor,caller);
    auto& event=frame.event;auto& p=event.removal;
    p.coverageGeneration=g_coverageGeneration.load();p.coverageMask=g_installed.load()&g_verified.load()&RemovalPredicateHooks;
    p.before=CaptureRemovalOperands(event.actor);const auto serial=frame.serial;++local->depth;
    bool completed=false;std::uint8_t result=0;
    __try {SetLastError(error);result=CallPredicateOriginal(&event,g_predicate,actor,0);completed=true;}
    __finally {const auto nativeError=GetLastError();
        if(Current(local,index+1,serial)) {
            p.originalReturned=p.resultAvailable=completed;
            if(completed){p.parentResult=result;p.coverageStable=p.coverageGeneration==g_coverageGeneration.load() &&
                p.coverageMask==(g_installed.load()&g_verified.load()&RemovalPredicateHooks);p.branch=ClassifyRemoval(p);}
            else{p.after={};p.unwindMask|=HookBit(Kind::RemovalPredicate);p.branch=RemovalBranch::Unknown;++g_predicateUnwound;}
            FinishEvent(event,completed);frame={};local->depth=index;
        }else ++g_predicateDropped;
        SetLastError(nativeError);
    }return result;
}

std::uint8_t __fastcall Predicate(void* actor) { return RunPredicate(actor, reinterpret_cast<uintptr_t>(_ReturnAddress())); }
std::uint8_t __fastcall ScriptPredicate(void* actor) { return RunPredicateChild(true, actor, reinterpret_cast<uintptr_t>(_ReturnAddress())); }
std::uint8_t __fastcall AuxiliaryPredicate(void* auxiliary) { return RunPredicateChild(false, auxiliary, reinterpret_cast<uintptr_t>(_ReturnAddress())); }

void __fastcall Removal(void* controller, void* actor) { Run(Kind::RemovalBookkeeping, controller, actor, reinterpret_cast<uintptr_t>(_ReturnAddress())); }
void __fastcall Disposal(void* actor) { Run(Kind::Disposal, nullptr, actor, reinterpret_cast<uintptr_t>(_ReturnAddress())); }
void __fastcall DeathMark(void* actor) { Run(Kind::DeathMark, nullptr, actor, reinterpret_cast<uintptr_t>(_ReturnAddress())); }
void __fastcall DeathBook(void* controller, void* actor) { Run(Kind::DeathBookkeeping, controller, actor, reinterpret_cast<uintptr_t>(_ReturnAddress())); }
void __fastcall Count(void* controller) { Run(Kind::CountDecrement, controller, nullptr, reinterpret_cast<uintptr_t>(_ReturnAddress())); }

bool VerifyBytes(uintptr_t address, const std::uint8_t* bytes, std::size_t length) {
    std::array<std::uint8_t, 128> actual {};
    return length <= actual.size() && Copy(address, actual.data(), length) &&
        std::memcmp(actual.data(), bytes, length) == 0;
}

bool InstallOne(unsigned index, const std::uint8_t* bytes, std::size_t length, void* hook, void** original) {
    const auto bit = 1u << index;
    auto* target = reinterpret_cast<void*>(g_exeBase + kRvas[index]);
    if (!VerifyBytes(reinterpret_cast<uintptr_t>(target), bytes, length) ||
        (index == static_cast<unsigned>(Kind::RemovalPredicate) &&
         (!VerifyBytes(g_exeBase + 0x3BFD6B, kPredicateCallerBytes, sizeof(kPredicateCallerBytes)) ||
          !VerifyBytes(g_exeBase + 0x419B90, kPredicateThunkBytes, sizeof(kPredicateThunkBytes))))) {
        g_failed.fetch_or(bit);
        if (g_log) g_log("[lifecycletrace] hook kind=%u rva=%llX verified=0 installed=0 reason=bytes",
                        index, static_cast<unsigned long long>(kRvas[index]));
        return false;
    }
    g_verified.fetch_or(bit);
    auto status = MH_CreateHook(target, hook, original);
    const bool created = status == MH_OK;
    if (created) {
        g_enableAttempted.fetch_or(bit);
        status = MH_EnableHook(target);
        if (status != MH_OK) MH_DisableHook(target); // retain potentially exposed original
    }
    if (status == MH_OK) g_installed.fetch_or(bit);
    else { g_failed.fetch_or(bit); if (!created) *original = nullptr; }
    if (g_log) g_log("[lifecycletrace] hook kind=%u rva=%llX verified=1 installed=%u status=%d",
                    index, static_cast<unsigned long long>(kRvas[index]),
                    status == MH_OK ? 1u : 0u, status);
    return status == MH_OK;
}

bool InstallOne(Kind kind, const std::uint8_t* bytes, std::size_t length, void* hook, void** original) {
    return InstallOne(static_cast<unsigned>(kind), bytes, length, hook, original);
}

} // namespace

bool Install(uintptr_t exeBase, spawncontroller::LogFn log, spawncontroller::RoleFn role, bool trace) {
    if (g_installed.load()) return true;
    if (g_enableAttempted.load()) {if(log)log("[lifecycletrace] unavailable reason=retained-partial-install");return false;}
    g_requested.store(trace);
    if (!trace) return false;
    if(tracefiber::PrepareRequested()){if(log)log("[lifecycletrace] unavailable reason=PREPARE-enabled diagnostic-profile");return false;}
    if(!g_storage.Init(reinterpret_cast<const void*>(&Install))){if(log)log("[lifecycletrace] unavailable reason=FLS-storage");return false;}
    g_recording=true;
    g_exeBase = exeBase;
    g_imageSize = ReadImageSize(exeBase);
    g_log = log;
    g_role = role;
    ++g_coverageGeneration;
    g_verified.store(0); g_failed.store(0);
    InstallOne(Kind::RemovalBookkeeping, kRemovalBytes, sizeof(kRemovalBytes), reinterpret_cast<void*>(&Removal), reinterpret_cast<void**>(&g_removal));
    InstallOne(Kind::Disposal, kDisposalBytes, sizeof(kDisposalBytes), reinterpret_cast<void*>(&Disposal), reinterpret_cast<void**>(&g_disposal));
    InstallOne(Kind::DeathMark, kDeathMarkBytes, sizeof(kDeathMarkBytes), reinterpret_cast<void*>(&DeathMark), reinterpret_cast<void**>(&g_deathMark));
    InstallOne(Kind::DeathBookkeeping, kDeathBookBytes, sizeof(kDeathBookBytes), reinterpret_cast<void*>(&DeathBook), reinterpret_cast<void**>(&g_deathBook));
    InstallOne(Kind::CountDecrement, kCountBytes, sizeof(kCountBytes), reinterpret_cast<void*>(&Count), reinterpret_cast<void**>(&g_count));
    // Children become callable before the parent can claim complete coverage.
    InstallOne(6, kScriptBytes, sizeof(kScriptBytes), reinterpret_cast<void*>(&ScriptPredicate), reinterpret_cast<void**>(&g_script));
    InstallOne(7, kAuxiliaryBytes, sizeof(kAuxiliaryBytes), reinterpret_cast<void*>(&AuxiliaryPredicate), reinterpret_cast<void**>(&g_auxiliary));
    InstallOne(Kind::RemovalPredicate, kPredicateBytes, sizeof(kPredicateBytes), reinterpret_cast<void*>(&Predicate), reinterpret_cast<void**>(&g_predicate));
    return g_installed.load() != 0;
}

Stats GetStats() {
    Stats s;
    s.requested = g_requested.load(); s.verifiedMask = g_verified.load();
    s.installedMask = g_installed.load(); s.failedMask = g_failed.load();
    s.started = g_started.load(); s.published = g_published.load(); s.dropped = g_dropped.load();
    s.unavailable = g_unavailable.load(); s.outOfScope = g_outOfScope.load();
    s.nativeFaults = g_faults.load(); s.unwound = g_unwound.load(); s.depthOverflow = g_depthOverflow.load();
    s.predicateStarted = g_predicateStarted.load(); s.predicatePublished = g_predicatePublished.load();
    s.predicateDropped = g_predicateDropped.load(); s.predicateForeign = g_predicateForeign.load();
    s.predicateUnmatched = g_predicateUnmatched.load(); s.predicateUnwound = g_predicateUnwound.load();
    s.predicateDepthOverflow = g_predicateDepthOverflow.load(); s.predicateCountOverflow = g_predicateCountOverflow.load();
    s.lastNativeException = g_exception.load();
    return s;
}

bool PopEvent(Event& event) {
    AcquireSRWLockExclusive(&g_queueLock);
    const bool present = g_size != 0;
    if (present) { event = g_queue[g_read]; g_read = (g_read + 1) % kQueueCap; --g_size; }
    ReleaseSRWLockExclusive(&g_queueLock);
    return present;
}

void Shutdown() {
    g_recording=false;
    if(g_storage.Ready()) {++g_coverageGeneration;
        for(unsigned i=0;i<kRvas.size();++i)if(g_enableAttempted.load()&(1u<<i))MH_DisableHook(reinterpret_cast<void*>(g_exeBase+kRvas[i]));
        return; // originals/trampolines/FLS retained for suspended fibers
    }
    ++g_coverageGeneration;
    const auto installed = g_installed.exchange(0);
    for (unsigned i = 0; i < kRvas.size(); ++i) {
        if (!(installed & (1u << i))) continue;
        auto* target = reinterpret_cast<void*>(g_exeBase + kRvas[i]);
        MH_DisableHook(target);
        MH_RemoveHook(target);
    }
    // Keep queued observations and lifetime counters available for a final drain.
    g_removal = g_deathBook = nullptr;
    g_disposal = g_deathMark = g_count = nullptr;
    g_predicate = g_script = g_auxiliary = nullptr;
    // No stack-address scope remains.
}

} // namespace kh2coop::inject::lifecycletrace
